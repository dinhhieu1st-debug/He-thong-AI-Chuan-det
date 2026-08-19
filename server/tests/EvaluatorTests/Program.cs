// Tests for the one place that decides a bed's status, VitalsStatusEvaluator.
//
// Deliberately a plain console program rather than an xUnit project: it needs no
// NuGet restore, so it runs on a machine with no network - including the Pi.
//
//   dotnet run --project server/tests/EvaluatorTests
//
// The cases below are the ones AI v2 changed. They exist because the difference
// between "the line is blocked" and "the patient is deteriorating" is the whole
// reason the models were split, and it would be easy to lose that distinction in
// a later refactor without any compiler complaining.

using HisServer.Domain;
using HisServer.Ingestion;
using HisServer.Models;

var failures = 0;

void Check(string what, bool ok, string detail)
{
    Console.WriteLine($"  [{(ok ? "PASS" : "FAIL")}] {what,-58} {detail}");
    if (!ok) failures++;
}

// A patient and a line that are both entirely fine.
static BedReading Normal() => new(
    BedId: "Bed-01", Room: "ICU-1", Spo2: 98, HeartRate: 78, Temperature: 36.8,
    DripRate: 100, ReceivedAt: DateTime.UtcNow, FlowRate: 100,
    AlertLevel: 0);

Console.WriteLine("\n== A healthy bed stays Stable ==");
{
    var r = Normal();
    Check("level 0 -> Stable", VitalsStatusEvaluator.Evaluate(r) == BedStatus.Stable,
          VitalsStatusEvaluator.Evaluate(r).ToString());
}

Console.WriteLine("\n== A line fault is a Warning, NOT Critical ==");
Console.WriteLine("   The patient is fine. Escalating this to Critical is how a ward");
Console.WriteLine("   learns to ignore the device.");
{
    var r = Normal() with { AlertLevel = 1, LineBranch = true, LineState = 2 };
    var status = VitalsStatusEvaluator.Evaluate(r);
    var (type, message) = VitalsStatusEvaluator.DescribeAlert(r);
    Check("level 1 -> Warning", status == BedStatus.Warning, status.ToString());
    Check("named as a LINE fault", type == "LINE_FAULT", $"{type}: {message}");
    Check("message says the weight is not moving",
          message.Contains("not getting lighter"), message);
}

Console.WriteLine("\n== A bag simply running out must NOT read as a blockage ==");
{
    var r = Normal() with { AlertLevel = 1, LineBranch = true, LineState = 1, RemainingMin = 18 };
    var (type, message) = VitalsStatusEvaluator.DescribeAlert(r);
    Check("reported as running low, with the time left",
          message.Contains("running low") && message.Contains("18"), message);
    Check("NOT described as blocked", !message.Contains("blocked"), message);
}

Console.WriteLine("\n== A bag running low must not be resurrected as an alarm ==");
Console.WriteLine("   The device sets LineBlocked (the drop ratio IS out of band) but still");
Console.WriteLine("   reports level 0, because the load cell shows the fluid going in fine.");
Console.WriteLine("   Re-reading LineBlocked here would undo the whole point of the load cell.");
{
    var r = Normal() with { AlertLevel = 0, LineBlocked = true, LineState = 1, RemainingMin = 42 };
    var status = VitalsStatusEvaluator.Evaluate(r);
    Check("device says level 0 -> Stable despite LineBlocked",
          status == BedStatus.Stable, status.ToString());
}

Console.WriteLine("\n== A line fault is NEVER Critical, whatever reports it ==");
Console.WriteLine("   Critical is reserved for the patient being in danger. Giving a kinked");
Console.WriteLine("   line the same colour and siren is how a ward stops reacting to red.");
{
    var v1 = Normal() with { AlertLevel = null, LineBlocked = true };
    Check("v1 device, LineBlocked -> Warning, not Critical",
          VitalsStatusEvaluator.Evaluate(v1) == BedStatus.Warning,
          VitalsStatusEvaluator.Evaluate(v1).ToString());

    var v2 = Normal() with { AlertLevel = 1, LineBranch = true, LineState = 2, LineBlocked = true };
    Check("v2 device, occluded line -> Warning",
          VitalsStatusEvaluator.Evaluate(v2) == BedStatus.Warning,
          VitalsStatusEvaluator.Evaluate(v2).ToString());

    // The one case a line fault still reaches Critical: the patient is failing
    // too. That is level 3, and it is the patient half that earns the red.
    var both = Normal() with { AlertLevel = 3, LineBranch = true, PatientBranch = true };
    Check("line AND patient together is still Critical",
          VitalsStatusEvaluator.Evaluate(both) == BedStatus.Critical,
          VitalsStatusEvaluator.Evaluate(both).ToString());

    // And a line fault must not mask a real desaturation happening at once.
    var masked = Normal() with { AlertLevel = 1, LineBlocked = true, Spo2 = 84, Spo2Signal = true };
    Check("a line fault never hides a critical SpO2",
          VitalsStatusEvaluator.Evaluate(masked) == BedStatus.Critical,
          VitalsStatusEvaluator.Evaluate(masked).ToString());
}

Console.WriteLine("\n== A patient problem IS Critical ==");
{
    var r = Normal() with { AlertLevel = 2, PatientBranch = true };
    var status = VitalsStatusEvaluator.Evaluate(r);
    var (type, message) = VitalsStatusEvaluator.DescribeAlert(r);
    Check("level 2 -> Critical", status == BedStatus.Critical, status.ToString());
    Check("says the line is fine", message.Contains("line is behaving normally"),
          $"{type}: {message}");
}

Console.WriteLine("\n== Both at once -> suspected fluid overload ==");
{
    var r = Normal() with { AlertLevel = 3, LineBranch = true, PatientBranch = true };
    var (type, message) = VitalsStatusEvaluator.DescribeAlert(r);
    Check("level 3 -> Critical",
          VitalsStatusEvaluator.Evaluate(r) == BedStatus.Critical, type);
    Check("named as fluid overload", type == "FLUID_OVERLOAD_SUSPECTED", message);
}

Console.WriteLine("\n== The server can still OVERRULE a device that says it is fine ==");
Console.WriteLine("   The device's verdict may raise the level, never lower it.");
{
    var r = Normal() with { AlertLevel = 0, Spo2 = 84, Spo2Signal = true };
    var status = VitalsStatusEvaluator.Evaluate(r);
    Check("device says 0, server sees SpO2 84 -> Critical",
          status == BedStatus.Critical, status.ToString());
}

Console.WriteLine("\n== A v1 device (no AlertLevel at all) still works ==");
{
    var r = Normal() with { AlertLevel = null, Spo2 = 84 };
    Check("critical SpO2 still Critical",
          VitalsStatusEvaluator.Evaluate(r) == BedStatus.Critical, "AlertLevel=null");

    var healthy = Normal() with { AlertLevel = null };
    Check("healthy v1 bed still Stable",
          VitalsStatusEvaluator.Evaluate(healthy) == BedStatus.Stable, "AlertLevel=null");
}

Console.WriteLine("\n== A lost sensor is never Stable ==");
{
    var r = Normal() with { Spo2Signal = false };
    Check("lost SpO2 -> Warning",
          VitalsStatusEvaluator.Evaluate(r) == BedStatus.Warning,
          VitalsStatusEvaluator.Evaluate(r).ToString());
}

Console.WriteLine("\n== The worst alert must never be lost to a column limit ==");
Console.WriteLine("   Level 3 concatenates the most causes, so it produces the longest");
Console.WriteLine("   message the system can make - and it used to be rejected by MySQL,");
Console.WriteLine("   dropping the entire alert.");
{
    var r = Normal() with
    {
        AlertLevel = 3, LineBranch = true, PatientBranch = true, LineState = 3,
        Spo2 = 86, Spo2Signal = true, HeartRate = 155, HeartRateSignal = true,
        AeAlarm = true, DripAnomaly = true, VitalsAnomaly = true,
    };
    var (_, message) = VitalsStatusEvaluator.DescribeAlert(r);
    Check("message fits the 255-char column", message.Length <= 255,
          $"{message.Length} chars");
    Check("the most severe cause survives truncation",
          message.StartsWith("Infusion line AND patient"), message[..Math.Min(60, message.Length)]);
    Check("no cause is cut mid-word - whole causes are dropped instead",
          !message.EndsWith("…"), message[^28..]);
    Check("and the reader is told something was dropped",
          message.Contains("more)"), message[^28..]);
}

// The real thing: a line captured verbatim off the board's VCOM port, parsed by
// the same BedDataParser the TCP ingestion path uses.
//
// This is the test that catches the failure nobody notices - a field renamed on
// one side of the wire. Everything still compiles, everything still runs, and
// the dashboard quietly shows a default forever. chip_sample.json is real
// output, not a fixture someone wrote by hand to match the parser.
Console.WriteLine("\n== A real line captured from the board parses end to end ==");
{
    var path = Path.Combine(AppContext.BaseDirectory, "chip_sample.json");
    if (!File.Exists(path))
    {
        Check("chip_sample.json present", false, path);
    }
    else
    {
        var reading = BedDataParser.Parse(File.ReadAllText(path));
        Check("alertLevel survived the wire", reading.AlertLevel == 1,
              $"AlertLevel={reading.AlertLevel?.ToString() ?? "null"}");
        Check("lineBranch survived the wire", reading.LineBranch, "true");
        Check("patientBranch survived the wire", !reading.PatientBranch, "false");
        // The board sends -1 while its 60 s weight trend is still filling. That
        // must arrive as null - "not known yet" - and never as a real reading.
        Check("lineState -1 becomes null, not 0", reading.LineState is null,
              reading.LineState?.ToString() ?? "null");
        Check("remainingMin -1 becomes null", reading.RemainingMin is null,
              reading.RemainingMin?.ToString() ?? "null");

        var status = VitalsStatusEvaluator.Evaluate(reading);
        Check("board with no PPG attached -> Warning, not Stable",
              status == BedStatus.Warning, status.ToString());
    }
}

// ---------------------------------------------------------------- OTA ----
Console.WriteLine("\n== Firmware update state ==");
{
    var ota = new HisServer.Services.OtaStatusRegistry();
    const string dev = "0x64028ffffe641802";

    Check("an unknown device is Unknown, not UpToDate",
          ota.Get(dev).State == OtaState.Unknown, ota.Get(dev).State.ToString());

    ota.Update(dev, "available", null, null, null, out _);
    Check("a check reporting an image maps to Available",
          ota.Get(dev).State == OtaState.Available, ota.Get(dev).State.ToString());
    Check("Available does not count as in flight",
          !ota.Get(dev).InFlight, "InFlight=false");

    ota.Update(dev, "updating", 47, 120, null, out _);
    var mid = ota.Get(dev);
    Check("progress is carried", mid.Progress == 47, $"{mid.Progress}%");
    Check("Updating counts as in flight", mid.InFlight, "InFlight=true");

    // zigbee2mqtt sends plenty of messages during a transfer that carry the
    // state but no percentage. Writing null over a real 47 makes the bar
    // collapse to empty and jump back - which reads as a stalled or restarted
    // update, the most alarming thing a progress bar can do mid-flash.
    ota.Update(dev, "updating", null, null, null, out _);
    Check("a message with no percentage keeps the last one",
          ota.Get(dev).Progress == 47, $"{ota.Get(dev).Progress}%");

    ota.Update(dev, "done", 100, 0, "Update finished", out _);
    Check("finishing clears in flight", !ota.Get(dev).InFlight, "InFlight=false");

    ota.Update(dev, "error", null, null, "No OTA cluster", out _);
    Check("an error maps to Failed and keeps the reason",
          ota.Get(dev).State == OtaState.Failed
          && ota.Get(dev).Message == "No OTA cluster", ota.Get(dev).Message ?? "");

    // --- what the firmware history hangs off -----------------------------
    //
    // The device republishes its OTA state about once a second. History must
    // be written on TRANSITIONS only, or the three lines a technician wants to
    // read end up buried under thousands saying "still idle".
    var histDev = "0xHIST";
    ota.Update(histDev, "available", null, null, null, out var prev0);
    Check("first sight of a device: previous state is Unknown",
          prev0 == OtaState.Unknown, prev0.ToString());

    ota.Update(histDev, "available", null, null, null, out var prev1);
    Check("same state repeated: previous == current, so no history row",
          prev1 == OtaState.Available
          && ota.Get(histDev).State == OtaState.Available, "Available -> Available");

    ota.Update(histDev, "updating", 5, null, null, out var prev2);
    Check("a real transition is visible to the caller",
          prev2 == OtaState.Available
          && ota.Get(histDev).State == OtaState.Updating, "Available -> Updating");

    // Versions are per device: two beds updating at once must not have their
    // version numbers attributed to each other.
    ota.Update("bedA", "updating", 1, null, null, out _, installedVersion: 1, latestVersion: 2);
    ota.Update("bedB", "updating", 1, null, null, out _, installedVersion: 7, latestVersion: 9);
    Check("versions are kept per device, not per server",
          ota.VersionsFor("bedA") == (1, 2) && ota.VersionsFor("bedB") == (7, 9),
          $"bedA={ota.VersionsFor("bedA")} bedB={ota.VersionsFor("bedB")}");

    // A version arriving without a partner must not wipe the one already known.
    ota.Update("bedA", "updating", 2, null, null, out _, installedVersion: null, latestVersion: 2);
    Check("a missing version does not erase the one already recorded",
          ota.VersionsFor("bedA") == (1, 2), ota.VersionsFor("bedA").ToString());

    // A device removed and re-added must not inherit its previous life.
    ota.Forget(dev);
    Check("forgetting a device resets it",
          ota.Get(dev).State == OtaState.Unknown, ota.Get(dev).State.ToString());

    // The gateway sends -1 for "not known"; the ingestion layer turns that into
    // null before it reaches here. Guard the shape the registry actually gets.
    ota.Update("other", "starting", null, null, "Update requested", out _);
    Check("Starting counts as in flight, so the button hides immediately",
          ota.Get("other").InFlight, "InFlight=true");
}

Console.WriteLine(failures == 0
    ? "\nALL CHECKS PASSED  (0 failures)\n"
    : $"\nSOME CHECKS FAILED  ({failures} failures)\n");
return failures == 0 ? 0 : 1;
