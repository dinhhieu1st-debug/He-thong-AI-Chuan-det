using HisServer.Models;

namespace HisServer.Domain;

/// <summary>
/// The single canonical implementation of the vitals-to-status thresholds.
/// Replaces the 5+ duplicated (and in one case inconsistent) copies of this
/// logic found across the old WinForms views.
///
/// Status is driven by two kinds of signal: hard vitals thresholds (SpO2/HR)
/// and equipment-state flags reported by the firmware (IV line blocked/free-flow,
/// AI autoencoder anomaly, and per-channel sensor connectivity). A bed with a
/// disconnected sensor is not "Stable" — it is unmonitored, which is exactly
/// the condition a nurse needs to be alerted to.
///
/// A vitals threshold is only ever applied to a channel that is actually
/// reporting. With no probe on the patient the firmware sends 0 with the
/// channel's signal flag cleared, and reading that as "SpO2 0% — critical"
/// produced a red alarm for a bed nobody was even connected to, hiding the
/// real condition (the sensor is unplugged) behind a fake one.
/// </summary>
public static class VitalsStatusEvaluator
{
    private const int CriticalSpo2 = 90;
    private const int WarningSpo2 = 95;
    private const int LowHeartRate = 60;
    private const int HighHeartRate = 110;

    /// <summary>
    /// How far a reading has to recover past a threshold before the bed is
    /// allowed to drop back to a less severe status. Applied only on the way
    /// DOWN - a reading that gets worse crosses the plain threshold instantly,
    /// same as before.
    ///
    /// Without this, a value sitting within sensor noise of a threshold (SpO2
    /// bouncing 94/95/96/94...) flips the bed's status every reading, and
    /// AlertTransitionTracker - which only raises an alert ON a transition -
    /// then raises a fresh alert on every flip. Observed live: 4 duplicate
    /// alerts on one bed in about two minutes of otherwise-normal sensor
    /// jitter. This is the textbook cause of alarm fatigue in a ward.
    ///
    /// Judged PER METRIC (the MetricHysteresis below), not off the bed's
    /// overall previous status - an earlier version keyed the buffer off the
    /// whole bed's previous status, which let a bed Warning because of SpO2
    /// also buffer a heart rate that had never itself been abnormal.
    ///
    /// The state carried between readings is a LATCHED bool per metric
    /// (persisted in BedState, updated via <see cref="ComputeNextHysteresis"/>),
    /// not the raw previous value. An earlier version compared each reading
    /// only to the single prior raw value: with noise like 94, 96, 94, 96...
    /// every OTHER reading (96) reads as "not abnormal" by the raw threshold,
    /// which reset the buffer and let the alarm re-fire every couple of
    /// readings - the exact flapping this feature exists to stop. Latching
    /// the decision itself, not the number that produced it, is what makes
    /// the buffer survive noise that crosses the boundary more than once.
    /// </summary>
    private const int Spo2Hysteresis = 2;
    private const int HeartRateHysteresis = 2;

    /// <summary>
    /// Per-metric "is this currently latched abnormal" state, carried from
    /// one reading to the next via BedState. Spo2Warning is true whenever
    /// SpO2 is Warning-or-worse (Critical implies it); HeartRateAbnormal
    /// covers both the low and high side, since only one hysteresis band
    /// is needed regardless of which direction tripped it.
    /// </summary>
    public readonly record struct MetricHysteresis(
        bool Spo2Warning, bool Spo2Critical, bool HeartRateAbnormal)
    {
        public static readonly MetricHysteresis None = new(false, false, false);
    }

    /// <summary>
    /// What the CURRENT reading's own hysteresis state will be, for the
    /// caller to persist and pass back in as `previous` next time. Must be
    /// computed from the same predicates Evaluate/DescribeAlert use, so the
    /// latched state never drifts from what was actually decided.
    /// </summary>
    public static MetricHysteresis ComputeNextHysteresis(BedReading reading, MetricHysteresis previous)
    {
        var critical = IsCriticalSpo2(reading, previous.Spo2Critical);
        var warning = critical || IsWarningSpo2Only(reading, previous.Spo2Warning);
        var hrAbnormal = IsAbnormalHeartRate(reading, previous.HeartRateAbnormal);
        return new MetricHysteresis(warning, critical, hrAbnormal);
    }

    public static BedStatus Evaluate(BedReading reading, MetricHysteresis previous)
    {
        /* Chưa theo dõi thì chưa phán xét gì cả.
         *
         * Ở chế độ chờ, y tá đang treo bình, kẹp cảm biến và tare cân. SpO2 đọc
         * 0 vì chưa kẹp vào ngón tay, giọt chưa chảy, cân đang bị tay đè lên -
         * mọi con số đều là rác, và mọi cảnh báo sinh ra từ chúng đều là báo
         * giả. Một khoa quen với báo giả là một khoa sẽ bỏ qua tiếng còi thật.
         *
         * Trả về Stable chứ không phải Offline: thiết bị vẫn đang nói chuyện
         * bình thường, chỉ là chưa được giao việc. Offline nghĩa là mất liên
         * lạc, một chuyện hoàn toàn khác và cần người đi kiểm tra. */
        if (!reading.Monitoring)
        {
            return BedStatus.Stable;
        }

        // The device's own four-level verdict comes first when it is available.
        // It is decided on-chip from three independent models plus the hard
        // clinical rules plus the load-cell cross-check, and it has information
        // this server does not: a 64-second history per channel, and whether the
        // bag is actually getting lighter.
        //
        // That last point is why the device's LINE_WARNING is not promoted to
        // Critical here. Slowing drops on a nearly-empty bag are normal physics
        // - the fluid column is shorter, so the pressure is lower - and the
        // device can tell that apart from an occlusion because it watches the
        // weight. The old rule below could not, and escalated both to Critical.
        //
        // This does NOT move the decision off this server. Every rule below
        // still runs, and for the PATIENT side they can only ever raise the
        // level: a device reporting "normal" while this server can see an SpO2
        // of 84 must not produce a green bed.
        //
        // The LINE side is the one exception, and it is deliberate. LineBlocked
        // means "the drop ratio is outside its prescribed band" - which is true
        // both when the line is occluded AND when the bag is simply running out,
        // because a shorter fluid column means lower pressure means fewer drops.
        // This server cannot tell those apart; it has one number. The device
        // can, because it watches the bag's WEIGHT: if the bag keeps getting
        // lighter the fluid is going in and nothing is wrong. So when the device
        // reports an AlertLevel, its line verdict supersedes LineBlocked.
        //
        // Deferring here is not the server giving up its authority - it is the
        // server declining to overrule a judgement made with strictly more
        // information. Calling a nearly-empty bag "Critical" every time is how a
        // ward learns to ignore the alarm that matters.
        var deviceStatus = reading.AlertLevel switch
        {
            3 => BedStatus.Critical,   // line AND patient failing together
            2 => BedStatus.Critical,   // the patient is in trouble
            1 => BedStatus.Warning,    // the infusion line, patient still fine
            0 => BedStatus.Stable,
            _ => BedStatus.Stable,     // v1 device: says nothing, decide below
        };

        if (deviceStatus == BedStatus.Critical)
        {
            return BedStatus.Critical;
        }

        // Critical is reserved for the PATIENT being in danger. A blocked or
        // free-flowing line is a real problem and someone has to walk over and
        // fix it, but it is not the same class of event as a desaturation, and
        // giving it the same colour and the same siren is how a ward stops
        // reacting to the colour at all.
        //
        // Note LineBlocked is absent here. It used to promote any drip-rate
        // deviation straight to Critical, which meant every bag that ran low,
        // every kinked line and every miscounted drop looked exactly like a
        // patient in trouble.
        if (IsCriticalSpo2(reading, previous.Spo2Critical))
        {
            return BedStatus.Critical;
        }

        // Note LineBlocked is absent from this list. When the device has judged
        // the line, LineBlocked is not extra evidence - it is an INPUT the
        // device already weighed, and weighed better. A bag running low makes
        // the device set line_blocked while deliberately reporting level 0,
        // because the load cell shows the fluid still going in. Re-reading that
        // flag here would resurrect exactly the false alarm the load cell was
        // added to remove.
        var deviceJudgedTheLine = reading.AlertLevel is not null;

        if (deviceStatus == BedStatus.Warning
            // A line fault from a device too old to classify it itself. Warning,
            // not Critical - see above.
            || (reading.LineBlocked && !deviceJudgedTheLine)
            || IsWarningSpo2(reading, previous.Spo2Critical, previous.Spo2Warning)
            || IsAbnormalHeartRate(reading, previous.HeartRateAbnormal)
            || reading.AeAlarm
            || HasLostSignal(reading))
        {
            return BedStatus.Warning;
        }

        return BedStatus.Stable;
    }

    /// <summary>
    /// Builds the human-readable alert message and alert-type code for a
    /// non-stable reading.
    ///
    /// EVERY active cause is listed, most severe first, rather than only the
    /// first one matched: a bed can be simultaneously blocked, anomalous and
    /// half-disconnected, and a nurse reading "Critically low SpO2" alone
    /// would go on to miss the blocked line. The alert-type code still
    /// reflects the single most severe cause, since it keys the alert
    /// history and the mobile push category.
    /// </summary>
    public static (string AlertType, string Message) DescribeAlert(
        BedReading reading, MetricHysteresis previous)
    {
        var causes = new List<(string Type, string Text)>();

        // Level 3 leads, ahead even of a critical SpO2. Not because it is more
        // urgent - they are the same event - but because it is the DIAGNOSIS
        // rather than a symptom, and it changes what the nurse does: fluid
        // overload means stop the infusion, and treating the desaturation while
        // the fluid keeps running is treating the wrong thing.
        //
        // The individual readings still follow in the same message, so nothing
        // is hidden; only the order and the alert type change.
        if (reading.AlertLevel == 3)
        {
            causes.Add(("FLUID_OVERLOAD_SUSPECTED",
                "Infusion line AND patient vitals both abnormal — suspected fluid overload"));
        }

        if (IsCriticalSpo2(reading, previous.Spo2Critical))
        {
            causes.Add(("SPO2_LOW_CRITICAL", $"Critically low SpO2: {reading.Spo2}%"));
        }

        if (reading.PatientBranch && reading.AlertLevel == 2)
        {
            causes.Add(("PATIENT_DETERIORATING",
                "Patient vitals abnormal — the infusion line is behaving normally"));
        }
        else if (reading.LineBranch && reading.AlertLevel == 1)
        {
            causes.Add(("LINE_FAULT", DescribeLineState(reading)));
        }

        // Only for a device that cannot judge the line itself. A v2 device has
        // already said something more precise above, and repeating this vaguer
        // line underneath would contradict it.
        if (reading.LineBlocked && reading.AlertLevel is null)
        {
            causes.Add(("LINE_BLOCKED", "IV line blocked or free-flowing — check tubing immediately"));
        }

        if (IsWarningSpo2(reading, previous.Spo2Critical, previous.Spo2Warning))
        {
            causes.Add(("SPO2_LOW", $"Low SpO2: {reading.Spo2}%"));
        }

        if (IsAbnormalHeartRate(reading, previous.HeartRateAbnormal))
        {
            var direction = reading.HeartRate < LowHeartRate ? "Low" : "High";
            causes.Add(("HEART_RATE_ABNORMAL", $"{direction} heart rate: {reading.HeartRate} bpm"));
        }

        if (reading.AeAlarm)
        {
            causes.Add(("AE_ALARM",
                "AI: this combination of heart rate and SpO2 is abnormal even "
                + "though neither reading has crossed its own limit"));
        }

        if (reading.DripAnomaly)
        {
            causes.Add(("DRIP_MODEL_ANOMALY", "AI: the infusion flow is changing unexpectedly"));
        }

        if (reading.VitalsAnomaly)
        {
            causes.Add(("VITALS_MODEL_ANOMALY", "AI: vitals are changing unexpectedly"));
        }

        if (HasLostSignal(reading))
        {
            causes.Add(("SENSOR_DISCONNECTED", $"No signal from: {DescribeLostChannels(reading)}"));
        }

        if (causes.Count == 0)
        {
            return ("VITAL_WARNING", "Vitals require attention");
        }

        return (causes[0].Type, Fit(causes.Select(c => c.Text)));
    }

    /// <summary>
    /// A channel only counts against the patient when its sensor is actually
    /// reporting — see the class comment.
    /// </summary>
    /// <summary>
    /// Longest alert message the database will accept (alerts.message and
    /// beds.alert_message are both VARCHAR(255)).
    /// </summary>
    private const int MaxMessageLength = 255;

    /// <summary>
    /// Truncates an alert message to what the column can hold.
    ///
    /// This is not cosmetic. Before it existed, a level-3 alert - infusion line
    /// AND patient failing together, which concatenates the most causes and is
    /// therefore the longest message the system can produce - was rejected by
    /// MySQL with "Data too long for column 'alert_message'" and the ENTIRE
    /// ALERT WAS DROPPED. The single most serious alarm was the one most likely
    /// to be lost, and nothing surfaced except a line in a server log.
    ///
    /// Causes are ordered most-severe-first, so WHOLE causes are dropped from
    /// the end rather than the string being cut mid-word. A nurse reading
    /// "AI: the infu…" learns nothing and is left wondering what was hidden;
    /// dropping the least important cause entirely, and saying how many were
    /// dropped, is honest and readable. Losing a minor cause is survivable;
    /// losing the alert is not.
    /// </summary>
    private static string Fit(IEnumerable<string> causes)
    {
        var kept = new List<string>();
        var length = 0;

        foreach (var cause in causes)
        {
            var extra = (kept.Count == 0 ? 0 : 3) + cause.Length;   // " · " + text
            // Leave room for the "(+N more)" suffix in case anything is dropped.
            if (length + extra > MaxMessageLength - 12 && kept.Count > 0) break;
            kept.Add(cause);
            length += extra;
        }

        var total = causes.Count();
        var message = string.Join(" · ", kept);
        if (kept.Count < total)
        {
            message += $" (+{total - kept.Count} more)";
        }

        // A single cause longer than the whole budget is the only case left.
        return message.Length <= MaxMessageLength
            ? message
            : message[..(MaxMessageLength - 1)] + "…";
    }

    /// <summary>
    /// Turns the device's load-cell verdict into words. The whole point of the
    /// load cell is that "fewer drops per minute" has two completely different
    /// causes, and only the bag's weight distinguishes them: if the bag keeps
    /// getting lighter the fluid IS going in and the bag is simply running out;
    /// if the weight holds still, the fluid is not leaving the bag at all.
    /// </summary>
    private static string DescribeLineState(BedReading reading) => reading.LineState switch
    {
        2 => "IV line blocked — drops have slowed and the bag is not getting lighter",
        3 => "Free flow — the bag is emptying far faster than prescribed",
        4 => "Drop sensor fault — drops are being counted but the bag weight is not moving",
        5 => "Bag empty",
        1 => reading.RemainingMin is int min
             ? $"Bag running low — about {min} minutes left"
             : "Bag running low",
        _ => "Infusion line needs checking — patient vitals are normal",
    };

    private static bool IsCriticalSpo2(BedReading reading, bool previousCritical)
    {
        // Latched, not the raw previous value: require the reading to climb
        // past the buffered threshold before letting go, instead of the
        // moment it touches 90 - see the class-level comment for why a raw
        // previous-value comparison isn't enough.
        var threshold = previousCritical ? CriticalSpo2 + Spo2Hysteresis : CriticalSpo2;
        return reading.Spo2Signal && reading.Spo2 < threshold;
    }

    /// <summary>Warning-or-worse SpO2, WITHOUT excluding Critical - used only
    /// to compute the next latched state (Critical implies Warning-or-worse
    /// too). Callers deciding an actual status must use
    /// <see cref="IsWarningSpo2"/> instead, which excludes Critical so the
    /// two levels stay mutually exclusive in the causes list.</summary>
    private static bool IsWarningSpo2Only(BedReading reading, bool previousWarning)
    {
        var threshold = previousWarning ? WarningSpo2 + Spo2Hysteresis : WarningSpo2;
        return reading.Spo2Signal && reading.Spo2 < threshold;
    }

    private static bool IsWarningSpo2(BedReading reading, bool previousCritical, bool previousWarning)
    {
        if (IsCriticalSpo2(reading, previousCritical))
        {
            return false;
        }
        return IsWarningSpo2Only(reading, previousWarning);
    }

    private static bool IsAbnormalHeartRate(BedReading reading, bool previousAbnormal)
    {
        if (!reading.HeartRateSignal)
        {
            return false;
        }
        var low = previousAbnormal ? LowHeartRate + HeartRateHysteresis : LowHeartRate;
        var high = previousAbnormal ? HighHeartRate - HeartRateHysteresis : HighHeartRate;
        return reading.HeartRate < low || reading.HeartRate > high;
    }

    private static bool HasLostSignal(BedReading reading) =>
        !reading.HeartRateSignal || !reading.Spo2Signal || !reading.FlowSignal || !reading.DripRateSignal;

    private static string DescribeLostChannels(BedReading reading)
    {
        var lost = new List<string>();
        if (!reading.HeartRateSignal) lost.Add("HR");
        if (!reading.Spo2Signal) lost.Add("SpO2");
        if (!reading.FlowSignal) lost.Add("Flow");
        if (!reading.DripRateSignal) lost.Add("Drip");
        return string.Join(", ", lost);
    }
}
