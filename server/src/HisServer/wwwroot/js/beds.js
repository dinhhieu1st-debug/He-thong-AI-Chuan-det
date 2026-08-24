const BedsTab = (() => {
  const persisted = UiUtils.loadFilterState("beds", { selectedRoom: "all", selectedStatus: "all", myBedsOnly: false });
  let searchTerm = "";
  let selectedRoom = persisted.selectedRoom;
  let selectedStatus = persisted.selectedStatus;
  let myBedsOnly = persisted.myBedsOnly;
  let myBedIds = null;   // Set, lazily loaded from the duty roster - null means "not loaded yet"
  let selectedBedId = null;
  let renderedPatientSignature = null;

  const STATUS_FILTERS = ["all", "Stable", "Warning", "Critical", "Offline"];

  function persist() {
    UiUtils.saveFilterState("beds", { selectedRoom, selectedStatus, myBedsOnly });
  }

  // Same roster ProfileTab ("My shift") already reads - loaded once per tab
  // activation, not per render, since it only changes when an administrator
  // edits assignments.
  async function loadMyBedIds() {
    try {
      const data = await Api.getMyAssignment();
      myBedIds = new Set(data.beds.filter((b) => b.mine).map((b) => b.bedId));
    } catch {
      myBedIds = new Set();
    }
    renderFilters();
  }

  function matchesFilters(bed) {
    if (selectedRoom !== "all" && bed.room !== selectedRoom) return false;
    if (selectedStatus !== "all" && bed.status !== selectedStatus) return false;
    if (myBedsOnly && myBedIds && !myBedIds.has(bed.bedId)) return false;
    if (searchTerm) {
      const haystack = `${bed.bedId} ${bed.room}`.toLowerCase();
      if (!haystack.includes(searchTerm.toLowerCase())) return false;
    }
    return true;
  }

  function bedCardHtml(bed) {
    const color = UiUtils.statusColor(bed.status);
    return `
      <div class="bed-card${bed.monitoring === false ? " standby" : ""}" data-bed-id="${UiUtils.escapeHtml(bed.bedId)}">
        <div class="stripe" style="background:${color};"></div>
        <div class="body">
          <div class="row-top">
            <div>
              <div class="bed-id">${UiUtils.escapeHtml(bed.bedId)}</div>
              <div class="bed-room">${UiUtils.escapeHtml(bed.room)}</div>
              ${bed.patientName ? `<div class="bed-patient-name">${UiUtils.escapeHtml(bed.patientName)}</div>` : ""}
            </div>
            <div class="chip-stack">
              <span class="status-chip" style="background:${color};">${UiUtils.escapeHtml((bed.status || "UNKNOWN").toUpperCase())}</span>
              ${UiUtils.culpritBadgeHtml(bed)}
            </div>
          </div>
          <div class="vitals">
            <div class="vital">SpO2<b>${UiUtils.formatMetric(bed.spo2, "%")}</b></div>
            <div class="vital">Heart Rate<b>${UiUtils.formatMetric(bed.heartRate, "")}</b></div>
            <div class="vital">Drip<b>${UiUtils.formatMetric(bed.dripRate, "%")}</b></div>
          </div>
          ${UiUtils.signalRowHtml(bed)}
          ${bed.monitoring === false
            ? `<div><span class="bed-standby-tag">STANDBY — not monitoring</span></div>`
            : ""}
          <div class="bed-updated">Updated ${UiUtils.formatDateTime(bed.lastUpdated)}</div>
        </div>
      </div>`;
  }

  function renderFilters() {
    const beds = Array.from(State.beds.values());
    const rooms = Array.from(new Set(beds.map((b) => b.room).filter(Boolean))).sort();

    // The bed count per room saves opening a room just to find it empty.
    const select = document.getElementById("bedRoomFilter");
    const options = [`<option value="all">All rooms (${beds.length})</option>`].concat(
      rooms.map((room) => {
        const count = beds.filter((b) => b.room === room).length;
        return `<option value="${UiUtils.escapeHtml(room)}">${UiUtils.escapeHtml(room)} (${count})</option>`;
      })
    );
    select.innerHTML = options.join("");

    // A room can disappear when its last bed is removed; fall back to "all"
    // rather than leaving the list showing a filter that matches nothing.
    if (selectedRoom !== "all" && !rooms.includes(selectedRoom)) {
      selectedRoom = "all";
    }
    select.value = selectedRoom;

    document.getElementById("bedStatusFilters").innerHTML = STATUS_FILTERS.map((status) =>
      `<button class="chip ${status === selectedStatus ? "active" : ""}" data-status="${status}">${status === "all" ? "All statuses" : status}</button>`
    ).join("") + (myBedIds && myBedIds.size > 0
      ? `<button class="chip ${myBedsOnly ? "active" : ""}" data-my-beds>My beds</button>`
      : "");
  }

  // One-shot event flags (tare_just_completed / hr_baseline_just_completed)
  // pulse true for a single reading then the firmware clears them - the
  // PERSISTENT confirmation ("Baseline captured at HH:MM:SS" / "Last tared
  // at HH:MM:SS") comes from bed.hrBaselineCapturedAt / bed.lastTareCompletedAt
  // (stamped server-side, always present once it's happened at least once),
  // so the doctor sees it whenever they open the panel, not just in the
  // instant the pulse fires. The toast is just a nice-to-have on top.
  const shownEventKeys = new Set();

  function maybeShowEventToast(bed) {
    if (bed.tareJustCompleted) {
      const key = `${bed.bedId}:tare:${bed.lastUpdated}`;
      if (!shownEventKeys.has(key)) {
        shownEventKeys.add(key);
        UiUtils.toast(`${bed.bedId}: loadcell tare complete - scale is at 0g`);
      }
    }
    if (bed.hrBaselineJustCompleted) {
      const key = `${bed.bedId}:hr:${bed.lastUpdated}`;
      if (!shownEventKeys.has(key)) {
        shownEventKeys.add(key);
        UiUtils.toast(`${bed.bedId}: HR 60s baseline sample complete`);
      }
    }
  }

  function hrStatusHtml(bed) {
    const remaining = bed.hrBaselineSecondsRemaining;
    if (remaining && remaining > 0) {
      return `<div class="status-line status-line-active">Calibrating baseline… <b>${remaining}s</b> remaining</div>`;
    }
    if (bed.hrBaselineCapturedAt) {
      const bpmSuffix = bed.hrBaselineBpm != null ? ` (${bed.hrBaselineBpm} bpm)` : "";
      return `<div class="status-line status-line-done">Baseline captured at ${UiUtils.formatDateTime(bed.hrBaselineCapturedAt)}${bpmSuffix}</div>`;
    }
    return `<div class="status-line status-line-muted">No baseline captured yet</div>`;
  }

  function tareStatusHtml(bed) {
    if (bed.tareInProgress) {
      return `<div class="status-line status-line-active">Taring in progress…</div>`;
    }
    if (bed.lastTareCompletedAt) {
      return `<div class="status-line status-line-done">Last tared at ${UiUtils.formatDateTime(bed.lastTareCompletedAt)}</div>`;
    }
    return `<div class="status-line status-line-muted">Not tared yet</div>`;
  }

  /* On-chip time-series forecaster output (ts_monitor.c). This is the part that
   * a plain threshold cannot produce: where the vitals are HEADING, not just
   * where they are now.
   *
   * The trend arrow only appears at all when the forecaster says the movement
   * is large enough to be real (the firmware applies a +-10 bpm/min deadband -
   * below that, short-term heart-rate direction measured on this hardware is
   * mostly noise and was only ~66% accurate in evaluation). */
  /* What the load cell concluded, and how long the bag has left.
   *
   * The states matter because "fewer drops per minute" has two completely
   * different causes and only the bag's WEIGHT tells them apart: if the bag
   * keeps getting lighter the fluid is going in and the bag is simply running
   * out (normal - a full bag has a taller fluid column, so higher pressure, so
   * faster flow); if the weight holds still, the fluid is not leaving at all. */
  const LINE_STATE = {
    0: { text: "Flow and weight agree", cls: "ls-ok" },
    1: { text: "Bag running low", cls: "ls-info" },
    2: { text: "LINE BLOCKED — drops slowed and the bag is not getting lighter", cls: "ls-alarm" },
    3: { text: "FREE FLOW — emptying far faster than prescribed", cls: "ls-alarm" },
    4: { text: "Drop sensor fault — drops counted but the weight is not moving", cls: "ls-warn" },
    5: { text: "Bag empty", cls: "ls-warn" },
  };

  /* How the device arrived at its final level, shown as the two branch
   * verdicts that produced it.
   *
   * The chip fuses one vitals level and one drip level into the number the
   * ward acts on, by a rule with no middle ground: 1+1 is 1, 3+3 is 3, and
   * every other combination is 2. The badge above says WHICH side is at fault;
   * this says how bad each side is, which is the difference between "the line
   * needs looking at" and "the line is why this bed is red".
   *
   * Both branch levels come straight from the chip. The server does not
   * recompute them, so if these two do not produce the final level shown, the
   * device and the console disagree and that is worth knowing.
   */
  function fusionSectionHtml(bed) {
    if (bed.vitalsLevel == null && bed.serverDropLevel == null) {
      // A device too old to report the branch levels. Say so rather than
      // drawing an empty box that looks like "both branches normal".
      return "";
    }

    const LEVEL = {
      1: { text: "1 · normal", cls: "status-line-done" },
      2: { text: "2 · attention", cls: "status-line-active" },
      3: { text: "3 · alarm", cls: "status-line-active" },
    };
    const cell = (v) => {
      const spec = LEVEL[v];
      return spec
        ? `<b class="${spec.cls}">${spec.text}</b>`
        : `<b class="status-line-muted">--</b>`;
    };

    /* The measured gap is the number the drip level is computed from: within
     * 200 ms of target is level 1, past 800 ms is level 3. Printing it next to
     * the verdict answers "why is this a 2?" without a serial cable. */
    const gap = bed.dropIntervalMs != null
      ? `<div class="status-line status-line-muted">Measured drop gap:
           <b>${UiUtils.escapeHtml(String(bed.dropIntervalMs))} ms</b>
           <span class="fc-sub">(within 200 ms of target = 1, past 800 ms = 3)</span>
         </div>`
      : "";

    const causes = [];
    if (bed.spo2Low) causes.push("SpO2 low");
    if (bed.heartRateAbnormal) causes.push("Heart rate abnormal");
    if (bed.lineBlocked) causes.push("Line blocked");
    if (bed.aeAlarm) causes.push("Vitals combination");
    const causeLine = causes.length
      ? `<div class="status-line status-line-active">Reported cause:
           <b>${UiUtils.escapeHtml(causes.join(" · "))}</b></div>`
      : "";

    return `
      <div class="line-status">
        <h4>How the device decided</h4>
        <div class="status-line">Patient branch: ${cell(bed.vitalsLevel)}</div>
        <div class="status-line">IV line branch: ${cell(bed.serverDropLevel)}</div>
        <div class="status-line">Final level: ${cell(bed.finalAlertLevel)}</div>
        ${gap}
        ${causeLine}
      </div>`;
  }

  function lineSectionHtml(bed) {
    if (bed.lineState == null) {
      /* Not "everything is fine" - the device has not worked it out yet. The
       * weight trend needs a full 60 seconds before it means anything, and
       * showing a green "OK" during that minute would be a lie. */
      return `
        <div class="line-status">
          <h4>Infusion line (load cell)</h4>
          <div class="status-line status-line-muted">
            Measuring the bag's weight trend…
          </div>
        </div>`;
    }

    const spec = LINE_STATE[bed.lineState] || LINE_STATE[0];
    // The load-cell telemetry uses one extra decimal digit for the remaining
    // volume. Convert only the displayed mL value; keep the original weight
    // and server payload untouched (e.g. 7897 -> 789 mL).
    const rawRemainingMl = Number(bed.remainingMl);
    const displayRemainingMl = bed.remainingMl != null && Number.isFinite(rawRemainingMl)
      ? Math.floor(rawRemainingMl / 10)
      : null;
    const remaining = (bed.remainingMl != null || bed.remainingMin != null)
      ? `<div class="ls-remaining">
           <span class="fc-label">Remaining</span>
           <b>${displayRemainingMl != null ? UiUtils.escapeHtml(String(displayRemainingMl)) + " mL" : "--"}</b>
           <span class="fc-sub">${bed.remainingMin != null
             ? "about " + UiUtils.escapeHtml(String(bed.remainingMin)) + " min at the current rate"
             : "rate too low to estimate"}</span>
         </div>`
      : "";

    const models = [];
    if (bed.dripAnomaly) {
      models.push(`<span class="fc-flag fc-alarm" title="The drip forecaster's error stayed above threshold for 11 consecutive seconds.">Drip model</span>`);
    }
    if (bed.vitalsAnomaly) {
      models.push(`<span class="fc-flag fc-alarm" title="The vitals forecaster's error stayed above threshold for 11 consecutive seconds.">Vitals model</span>`);
    }
    if (bed.aeAlarm) {
      models.push(`<span class="fc-flag fc-alarm" title="The autoencoder found this combination of heart rate and SpO2 abnormal, even though neither reading has crossed its own limit.">Vitals combination</span>`);
    }

    return `
      <div class="line-status">
        <h4>Infusion line (load cell)</h4>
        <div class="ls-verdict ${spec.cls}">${UiUtils.escapeHtml(spec.text)}</div>
        ${remaining}
        ${models.length ? `<div class="fc-flags">${models.join("")}</div>` : ""}
      </div>`;
  }

  function forecastSectionHtml(bed) {
    if (!bed.tsReady) {
      return `
        <div class="bed-forecast">
          <h4>AI forecast (on-chip)</h4>
          <div class="status-line status-line-muted">
            Collecting the first 64 seconds of history…
          </div>
        </div>`;
    }

    // For heart rate a RISING trend is the worrying one, so it takes the alarm
    // colour. For the drop rate it is the opposite: drops slowing down is the
    // early sign of an occlusion forming, so FALLING is what gets highlighted.
    const trendOf = (code, worryOn) => {
      const spec = code === 1 ? { arrow: "▲", word: "rising" }
                 : code === 2 ? { arrow: "▼", word: "falling" }
                 : { arrow: "▬", word: "steady" };
      spec.cls = code === 0 ? "trend-steady"
               : (code === worryOn ? "trend-worry" : "trend-ok");
      return spec;
    };

    const trend = trendOf(bed.tsTrend, 1);          // HR: tang la dang lo
    const dropTrend = trendOf(bed.dropsTrend, 2);   // Giot: giam la dang lo

    const rateText = (bed.hrTrendBpmPerMin != null && bed.tsTrend !== 0)
      ? `${bed.hrTrendBpmPerMin > 0 ? "+" : ""}${bed.hrTrendBpmPerMin} bpm/min` : "";
    const dropRateText = (bed.dropsTrendDpmPerMin != null && bed.dropsTrend !== 0)
      ? `${bed.dropsTrendDpmPerMin > 0 ? "+" : ""}${bed.dropsTrendDpmPerMin} dpm/min` : "";

    // Score is sent x100 by the firmware to keep 2 decimals over an integer wire.
    const score = bed.tsAnomalyScoreX100 != null
      ? (bed.tsAnomalyScoreX100 / 100).toFixed(2) : "--";

    const flags = [];
    if (bed.tsEarlyWarning) {
      flags.push(`<span class="fc-flag fc-warn" title="The forecast crosses a clinical limit within the next 16 seconds, while the current reading is still inside it.">Early warning</span>`);
    }
    if (bed.tsAnomaly) {
      flags.push(`<span class="fc-flag fc-alarm" title="Forecast error stayed above threshold for 11 consecutive seconds - a sustained deviation, not a transient blip.">Sustained anomaly</span>`);
    }

    return `
      <div class="bed-forecast">
        <h4>AI forecast (on-chip)</h4>
        <div class="fc-row">
          <div class="fc-card">
            <span class="fc-label">Drip rate trend</span>
            <b class="${dropTrend.cls}">${dropTrend.arrow} ${dropTrend.word}</b>
            <span class="fc-sub">${UiUtils.escapeHtml(dropRateText || "within deadband")}</span>
          </div>
          <div class="fc-card${bed.dropsForecastTrusted === false ? " fc-untrusted" : ""}">
            <span class="fc-label">${bed.dropsForecastTrusted === false
              ? "Drips: expected if normal" : "Drips in 16s"}</span>
            <b>${UiUtils.formatMetric(bed.dropsForecast16s, " dpm")}</b>
            <span class="fc-sub">now ${UiUtils.formatMetric(bed.dropsPerMin, " dpm")}${
              bed.dropsForecastTrusted === false ? " — not a forecast" : ""}</span>
          </div>
          <div class="fc-card">
            <span class="fc-label">Heart rate trend</span>
            <b class="${trend.cls}">${trend.arrow} ${trend.word}</b>
            <span class="fc-sub">${UiUtils.escapeHtml(rateText || "within deadband")}</span>
          </div>
          <div class="fc-card${bed.hrForecastTrusted === false && bed.hrForecast16s != null ? " fc-untrusted" : ""}">
            <span class="fc-label">${bed.hrForecastTrusted === false && bed.hrForecast16s != null
              ? "HR: expected if normal" : "HR in 16s"}</span>
            <b>${UiUtils.formatMetric(bed.hrForecast16s, " bpm")}</b>
            <span class="fc-sub">${bed.hrForecast16s == null ? "no sensor signal"
                                    : "now " + UiUtils.formatMetric(bed.heartRate, " bpm")
                                      + (bed.hrForecastTrusted === false ? " — not a forecast" : "")}</span>
          </div>
          <div class="fc-card">
            <span class="fc-label">SpO2 in 16s</span>
            <b>${UiUtils.formatMetric(bed.spo2Forecast16s, "%")}</b>
            <span class="fc-sub">${bed.spo2Forecast16s == null ? "no sensor signal"
                                    : "now " + UiUtils.formatMetric(bed.spo2, "%")}</span>
          </div>
          <div class="fc-card">
            <span class="fc-label">Anomaly score</span>
            <b>${score}</b>
            <span class="fc-sub">alarms above 5.61</span>
          </div>
        </div>
        ${flags.length ? `<div class="fc-flags">${flags.join("")}</div>` : ""}
        ${(bed.dropsForecastTrusted === false || (bed.hrForecastTrusted === false && bed.hrForecast16s != null))
          ? `<div class="fc-note" title="The model was trained only on normal behaviour, so it cannot predict an abnormal channel. What it reports instead is the value a healthy line would be showing now; the gap between that and the real reading is what raises the anomaly score.">
             <b>“Expected if normal”</b> = what a healthy line would read now, not a
             prediction. The gap raises the anomaly score.</div>`
          : ""}
      </div>`;
  }

  /* Left rail of the detail view: every live number for this bed in one
   * block, so "how is the patient right now" is answered without scrolling
   * and without reading it off a chart.
   *
   * A channel whose sensor is reporting no signal is dimmed rather than
   * hidden - a missing reading is itself information the nurse needs, and
   * removing the tile would make the layout jump around as sensors drop. */
  function liveVitalsSectionHtml(bed) {
    /* A tile carries the same severity as that metric's chart, so the number
     * a nurse reads first and the trace they check second never disagree.
     * A channel with no signal is dimmed and never coloured by severity: a
     * reading of 0 from an unplugged probe is not a dangerous reading. */
    // A lost channel shows "--", never the raw reading (which is often a
    // stale or physically-impossible 0 from an unplugged probe) - a nurse
    // must never be able to read "0%"/"0 bpm" as a real patient value.
    const tile = (key, unit, label, ok) => {
      const color = (METRIC_BY_KEY[key] || {}).color || "#2470c8";
      const sev = ok === false ? "ok" : severityOfValue(key, bed[key]);
      const cls = (ok === false ? " is-lost" : "") + (sev === "ok" ? "" : ` sev-${sev}`);
      const value = ok === false ? "--" : UiUtils.formatMetric(bed[key], "");
      return `
        <div class="bd-vital${cls}" style="--vital-color:${color};">
          <span class="v">${value}${ok === false ? "" : `<span class="u">${UiUtils.escapeHtml(unit)}</span>`}</span>
          <span class="k">${UiUtils.escapeHtml(label)}${ok === false ? " · no signal" : ""}</span>
        </div>`;
    };

    // Computed once and shared between the Active alert card and the Sensor
    // channels row highlight below, so both read the exact same verdict.
    const causes = UiUtils.buildAlertCauses(bed);
    const lineCauseActive = causes.some((c) => c.type === "line");

    return `
      <div class="bd-card">
        <h4>Live vitals</h4>
        <div class="bd-vitals">
          ${tile("spo2", "%", "SpO2", bed.spo2Signal)}
          ${tile("heartRate", " bpm", "Heart rate", bed.heartRateSignal)}
          ${tile("dripRate", "%", "Drip vs target", bed.dripRateSignal)}
          ${tile("dropsPerMin", " dpm", "Drops per min", bed.dripRateSignal)}
          ${tile("weightG", " g", "IV bag weight", bed.flowSignal)}
        </div>
      </div>
      ${aiBaselineCardHtml(bed)}
      ${activeAlertCardHtml(bed, causes)}
      <div class="bd-card">
        <h4>Sensor channels</h4>
        <div class="bd-channels">
          ${channelRow("Heart rate", bed.heartRateSignal)}
          ${channelRow("SpO2", bed.spo2Signal)}
          ${channelRow("Flow", bed.flowSignal)}
          ${channelRow("Drip", bed.dripRateSignal, "Signal OK", "No signal", lineCauseActive && bed.dripRateSignal)}
          ${channelRow("IV line", !bed.lineBlocked, "Flowing", "Blocked / free-flow")}
        </div>
      </div>`;
  }

  /* AI-learned HR/SpO2 baseline (vitals_ai.hr_baseline / vitals_ai.spo2_baseline
   * on the chip), NOT the 16s forecast and NOT the live reading - those answer
   * different questions ("where is it heading" / "what is it now" vs "what does
   * this AI consider this patient's normal").
   *
   * Readiness is vitalsTrainingSamples reaching 64 (vitals_ai.history_samples on
   * the chip) - the same counter the Monitoring block's "HR+SpO2: n/64" progress
   * bar already uses, so the two never disagree.
   *
   * The current wire protocol has no dedicated SpO2-baseline attribute. After
   * training is ready, show the filtered SpO2 sample actually supplied to the
   * on-chip AI (aiInputSpo2). Older firmware does not report that field, so use
   * the live value only while its sensor-valid flag is true. Never use the 16 s
   * forecast: a prediction is not an observed SpO2 reference value. */
  function baselineTileHtml(value, unit, label, ready, note) {
    return `
      <div class="bd-vital${ready ? "" : " is-lost"}">
        <span class="v">${UiUtils.escapeHtml(value)}${unit ? `<span class="u">${UiUtils.escapeHtml(unit)}</span>` : ""}</span>
        <span class="k">${UiUtils.escapeHtml(label)}${note ? ` · ${UiUtils.escapeHtml(note)}` : ""}</span>
      </div>`;
  }

  function aiBaselineCardHtml(bed) {
    const samples = Math.max(0, Math.min(64, Number(bed.vitalsTrainingSamples) || 0));
    const ready = samples >= 64;
    const aiSpo2 = bed.aiInputSpo2 != null
      ? Number(bed.aiInputSpo2)
      : (bed.spo2Signal && bed.spo2 != null ? Number(bed.spo2) : null);
    const hasAiSpo2 = Number.isFinite(aiSpo2) && aiSpo2 > 0 && aiSpo2 <= 100;

    const hrTile = ready
      ? baselineTileHtml(bed.hrBaselineBpm != null ? String(bed.hrBaselineBpm) : "--", " bpm", "Heart rate", true)
      : baselineTileHtml("Learning…", "", "Heart rate", false);
    const spo2Tile = ready
      ? (hasAiSpo2
          ? baselineTileHtml(String(Math.round(aiSpo2)), " %", "SpO2 AI input", true)
          : baselineTileHtml("--", "", "SpO2", false, "no valid signal"))
      : baselineTileHtml("Learning…", "", "SpO2", false);

    return `
      <div class="bd-card">
        <h4>AI learned baseline</h4>
        ${ready
          ? `<div class="status-line status-line-done">✓ READY · 64/64</div>`
          : `<div class="status-line status-line-active">${samples} / 64 samples</div>`}
        <div class="bd-vitals">${hrTile}${spo2Tile}</div>
      </div>`;
  }

  /* One line of text per active cause, built from buildAlertCauses() - never
   * from re-deriving WARNING/CRITICAL here. A bed with no cause list AND an
   * active status (a v2 device's canonical fallback text, e.g. "Level 3
   * critical alert from XG26") still falls back to the server's own message
   * rather than showing nothing. */
  function causeItemHtml(cause) {
    let detail = "";
    if (cause.type === "line") {
      detail = ` — ${cause.value}${cause.target && cause.target !== "--" ? ` / target ${cause.target}` : ""}`;
    } else if (cause.threshold) {
      detail = ` — ${cause.value} (limit ${cause.threshold})`;
    } else if (cause.value && cause.value !== "--") {
      detail = ` — ${cause.value}`;
    }
    if (cause.baseline) detail += ` · baseline ${cause.baseline}`;
    const level = cause.level != null ? `Level ${cause.level} · ` : "";
    return `<li><b>${level}${UiUtils.escapeHtml(cause.sensor)}</b> · ${UiUtils.escapeHtml(cause.channel)}: ${UiUtils.escapeHtml(cause.reason)}${detail}</li>`;
  }

  function activeAlertCardHtml(bed, causes) {
    const status = (bed.status || "").toLowerCase();
    const active = status === "warning" || status === "critical";

    if (!active) {
      return `
        <div class="bd-card bd-card-alert">
          <h4>Active alert</h4>
          <div class="status-line status-line-done">✓ No active alert</div>
        </div>`;
    }

    const list = causes.length > 0
      ? causes.map(causeItemHtml).join("")
      : `<li>${UiUtils.escapeHtml(bed.alertMessage || "Vitals require attention")}</li>`;
    const badge = status === "critical" ? "🔴 CRITICAL" : "⚠ WARNING";

    return `
      <div class="bd-card bd-card-alert sev-${status}">
        <h4>Active alert${causes.length > 1 ? ` · ${causes.length} active causes` : ""}</h4>
        <div class="status-line status-line-active">${badge}</div>
        <ul class="bd-alert-list">${list}</ul>
      </div>`;
  }

  function vitalsSectionHtml(bed) {
    return `
      <div id="bedLiveState">
        ${liveVitalsSectionHtml(bed)}
      </div>
      ${patientCardHtml(bed)}
      ${faultCardHtml(bed)}`;
  }

  function channelRow(label, ok, okText = "Signal OK", lostText = "No signal", warn = false) {
    const cls = warn ? "is-warn" : (ok ? "is-ok" : "is-lost");
    const text = warn ? "WARNING" : (ok ? okText : lostText);
    return `
      <div class="bd-channel">
        <span class="bd-channel-name">${UiUtils.escapeHtml(label)}</span>
        <span class="bd-channel-state ${cls}">${UiUtils.escapeHtml(text)}</span>
      </div>`;
  }

  /* Who is in the bed. Shown to everyone who can see the ward - knowing which
   * patient a reading belongs to is the whole point of a bed number - but only
   * editable with the patient.edit capability. */
  function patientCardHtml(bed) {
    const admitted = bed.admittedAt
      ? `<div class="status-line status-line-muted">Admitted ${UiUtils.formatDateTime(bed.admittedAt)}</div>`
      : "";

    const occupied = bed.patientName && bed.patientName.trim();

    return `
      <div class="bd-card">
        <h4>Patient</h4>
        ${occupied
          ? `<div class="bd-patient-name">${UiUtils.escapeHtml(bed.patientName)}</div>
             ${bed.patientCode ? `<div class="status-line status-line-muted">Patient ID: ${UiUtils.escapeHtml(bed.patientCode)}</div>` : ""}
             ${admitted}`
          : `<div class="status-line status-line-muted">Empty bed</div>`}
        <div data-cap="patient.edit">
          <form id="patientForm" class="bd-patient-form">
            <input type="text" id="patientNameInput" placeholder="Patient name"
                   value="${UiUtils.escapeHtml(bed.patientName || "")}">
            <input type="text" id="patientCodeInput" placeholder="Patient ID"
                   value="${UiUtils.escapeHtml(bed.patientCode || "")}">
            <div class="bd-patient-actions">
              <button type="submit" class="btn primary">${occupied ? "Update" : "Admit patient"}</button>
              ${occupied ? `<button type="button" class="btn" id="dischargeBtn">Discharge</button>` : ""}
            </div>
          </form>
        </div>
      </div>`;
  }

  /* Equipment fault reporting, from the bedside.
   *
   * The nurse describes the SYMPTOM and picks the part; the technician records
   * the diagnosis. Letting a nurse mark equipment "broken" or "fixed" would
   * fill the device list with guesses made by whoever happened to be standing
   * there.
   *
   * The open reports for this bed are shown right below the button on purpose:
   * a nurse who reports a fault and never learns whether anyone picked it up
   * goes back to the telephone, and the queue dies in its first week. */
  let bedFaultReports = [];

  function faultCardHtml(bed) {
    const open = bedFaultReports.filter((r) => r.status !== "RESOLVED");
    const recent = bedFaultReports.slice(0, 3);

    const list = recent.map((r) => {
      const label = r.status === "OPEN" ? "waiting for technician"
                  : r.status === "IN_PROGRESS" ? `with ${UiUtils.escapeHtml(r.handledBy || "technician")}`
                  : `fixed by ${UiUtils.escapeHtml(r.handledBy || "technician")}`;
      return `<div class="status-line ${r.status === "RESOLVED" ? "status-line-done" : "status-line-active"}">
                ${UiUtils.escapeHtml(r.channel)} · ${label}
              </div>`;
    }).join("");

    return `
      <div class="bd-card" data-cap="faults.report">
        <h4>Equipment</h4>
        ${open.length > 0
          ? `<div class="status-line status-line-active">${open.length} open report(s)</div>`
          : `<div class="status-line status-line-muted">No open reports</div>`}
        ${list}
        <button type="button" class="btn" id="reportFaultBtn" style="margin-top:8px;">
          Report device problem
        </button>
      </div>`;
  }

  function bindFaultHandlers(bed) {
    document.getElementById("reportFaultBtn")?.addEventListener("click", () => {
      const backdrop = document.createElement("div");
      backdrop.className = "modal-backdrop";
      backdrop.innerHTML = `
        <form class="modal-card" id="faultForm">
          <h3>Report a device problem · ${UiUtils.escapeHtml(bed.bedId)}</h3>
          <label for="faultChannel">Which part looks wrong?</label>
          <select id="faultChannel">
            <option value="SPO2">SpO2 sensor</option>
            <option value="HR">Heart rate sensor</option>
            <option value="FLOW">Load cell / scale</option>
            <option value="DROPS">Drop sensor</option>
            <option value="OTHER">Something else</option>
          </select>
          <label for="faultNote">What did you see?</label>
          <input type="text" id="faultNote" maxlength="500"
                 placeholder="e.g. re-seated the clip twice, still reads nothing">
          <div class="form-error" id="faultError"></div>
          <div class="modal-actions">
            <button type="button" class="btn" id="faultCancel">Cancel</button>
            <button type="submit" class="btn primary">Send to technician</button>
          </div>
        </form>`;
      document.body.appendChild(backdrop);

      backdrop.querySelector("#faultCancel").addEventListener("click", () => backdrop.remove());
      backdrop.querySelector("#faultForm").addEventListener("submit", async (e) => {
        e.preventDefault();
        try {
          await Api.reportFault(bed.bedId,
                                backdrop.querySelector("#faultChannel").value,
                                backdrop.querySelector("#faultNote").value.trim());
          backdrop.remove();
          UiUtils.toast("Sent to the technician");
          await loadBedFaultReports(bed.bedId);
        } catch (err) {
          const box = backdrop.querySelector("#faultError");
          box.textContent = err.message;
          box.style.display = "block";
        }
      });
    });
  }

  async function loadBedFaultReports(bedId) {
    if (!Session.can(Session.CAP.reportFaults)) return;
    try {
      bedFaultReports = await Api.getBedFaultReports(bedId);
      renderDetail();
    } catch (err) {
      // A failure here must not take the vitals panel down with it.
      console.error("Could not load fault reports:", err);
    }
  }

  /* Bắt đầu / tạm dừng theo dõi.
   *
   * Đặt ở TRÊN CÙNG khối cài đặt, vì đây là việc cuối cùng y tá làm sau khi
   * treo bình, kẹp cảm biến và tare cân - và là thứ quyết định thiết bị có
   * được phép báo động hay không. Nút phải nói rõ nó đang ở trạng thái nào,
   * chứ không chỉ nói nó sẽ làm gì khi bấm: một cái nút ghi "Bắt đầu" thì
   * không phân biệt được "chưa bắt đầu" với "đã bắt đầu rồi". */
  function monitoringSectionHtml(bed) {
    const on = bed.monitoring !== false;
    const calibrating = on && bed.alertsArmed === false;
    const drops = Math.max(0, Math.min(20, Number(bed.dropTrainingSamples) || 0));
    const vitals = Math.max(0, Math.min(64, Number(bed.vitalsTrainingSamples) || 0));
    const color = calibrating ? "#e68a00" : (on ? "#1ea050" : "#8a97a8");
    return `
      <div class="settings-block monitoring-block ${calibrating ? "is-calibrating" : (on ? "is-on" : "is-standby")}">
        <label>Monitoring</label>
        <div class="monitoring-state">
          <span class="status-chip" style="background:${color};">
            ${calibrating ? "COLLECTING DATA" : (on ? "MONITORING" : "STANDBY")}
          </span>
          <span class="muted">${on
            ? (calibrating
              ? "Collecting startup samples. AI alarms remain off until both counters finish."
              : "AI and alarms are running for this bed.")
            : "Sensors are read and shown, but no AI and no alarms yet."}</span>
        </div>
        <div id="monitoringProgress" style="${calibrating ? "" : "display:none;"}margin-top:10px;">
          <div style="display:flex;justify-content:space-between;font-size:12px;"><span>Drip intervals</span><b id="dropTrainingText">${drops}/20</b></div>
          <progress id="dropTrainingProgress" max="20" value="${drops}" style="width:100%;"></progress>
          <div style="display:flex;justify-content:space-between;font-size:12px;margin-top:5px;"><span>HR/SpO2 samples</span><b id="vitalsTrainingText">${vitals}/64</b></div>
          <progress id="vitalsTrainingProgress" max="64" value="${vitals}" style="width:100%;"></progress>
          <div class="muted" style="font-size:12px;margin-top:4px;">Alarms start automatically after 20/20 and 64/64.</div>
        </div>
        <div class="inline-form" style="margin-top:8px;">
          <button type="button" id="monitoringBtn" class="btn ${on ? "" : "primary"}">
            ${on ? "Pause monitoring" : "Start monitoring"}
          </button>
        </div>
        <div class="muted" style="margin-top:6px;font-size:12px;">
          ${on
            ? `Pauses AI and alarms for this bed only. Not for taking a reading —
               use "Reset scale (tare)" or "Recalibrate 60s baseline" below for
               that; neither one needs monitoring paused first.`
            : `Hang the bag, attach the sensors and tare the scale first —
               readings taken while setting up would otherwise be the AI's
               first impression of this patient.`}
        </div>
      </div>`;
  }

  /* Real measurement next to what the AI is actually being fed.
   *
   * The pairing is the point. A toast only says the SERVER accepted the
   * command; these two figures diverging is the first thing on any screen that
   * proves the write travelled server -> gateway -> MQTT -> Zigbee -> chip and
   * was applied. If "Real" moves and "AI test" does not, the command stopped
   * somewhere in that chain.
   *
   * Both fall back to "--" rather than 0 when the device has not reported
   * them: an older firmware sends neither, and a fabricated zero here would
   * read as a patient with no pulse. */
  function vitalsTestHtml(bed) {
    const mode = bed.vitalsTestMode;
    const label = mode === 2 ? "Fake HR L2"
                : mode === 3 ? "Fake HR+O2 L3"
                : mode === 0 ? "Real data"
                : "unknown";
    const active = mode === 2 || mode === 3;

    const real = `${UiUtils.formatMetric(bed.heartRateSignal ? bed.heartRate : null, " bpm")}`
               + ` / ${UiUtils.formatMetric(bed.spo2Signal ? bed.spo2 : null, "%")}`;
    const aiInput = `${UiUtils.formatMetric(bed.aiInputHeartRate, " bpm")}`
                  + ` / ${UiUtils.formatMetric(bed.aiInputSpo2, "%")}`;

    return `
      <div class="status-line ${active ? "status-line-active" : "status-line-done"}">
        Mode: <b>${UiUtils.escapeHtml(label)}</b>
      </div>
      <div class="status-line status-line-muted">Real HR / SpO2: <b>${real}</b></div>
      <div class="status-line status-line-muted">AI test HR / SpO2: <b>${aiInput}</b></div>`;
  }

  function settingsSectionHtml(bed) {
    return `
      <div class="bed-settings">
        <h4>Doctor-configurable settings</h4>

        ${monitoringSectionHtml(bed)}

        <div class="settings-block">
          <label>Load cell</label>
          <div id="tareStatusState">${tareStatusHtml(bed)}</div>
          <div class="inline-form" style="margin-top:8px;">
            <button type="button" id="resetTareBtn" class="btn">Reset scale (tare)</button>
          </div>
        </div>

        <div class="settings-block">
          <label>Heart rate</label>
          <div id="hrStatusState">${hrStatusHtml(bed)}</div>
          <div class="inline-form" style="margin-top:8px;">
            <button type="button" id="recalibrateHrBtn" class="btn">Recalibrate 60s baseline</button>
          </div>
        </div>

        <div class="settings-block">
          <label>Vitals test mode</label>
          <div id="vitalsTestState">${vitalsTestHtml(bed)}</div>
          <div class="inline-form" style="margin-top:8px;">
            <button type="button" class="btn" data-test-mode="0">Real data</button>
            <button type="button" class="btn" data-test-mode="2">Fake HR L2</button>
            <button type="button" class="btn" data-test-mode="3">Fake HR+O2 L3</button>
          </div>
        </div>

        <div class="settings-block">
          <label>Drop rate (AI alert target)
            <b class="current-target" id="targetDropsCurrent">${UiUtils.formatMetric(bed.targetDropsPerMin, " dpm")}</b>
            <span class="measured" id="targetDropsMeasured">measured ${UiUtils.formatMetric(bed.dropsPerMin, " dpm")}</span>
          </label>
          <form id="targetDropsForm" class="inline-form">
            <input type="number" min="1" max="240" id="targetDropsInput" placeholder="New target (dpm)" value="">
            <button type="submit" class="btn primary">Set</button>
          </form>
        </div>
      </div>`;
  }

  function bindSettingsHandlers(bed) {
    document.getElementById("targetDropsForm").addEventListener("submit", async (e) => {
      e.preventDefault();
      const input = document.getElementById("targetDropsInput");
      const value = parseInt(input.value, 10);
      if (!value || value < 1 || value > 240) {
        UiUtils.toast("Drop target must be between 1 and 240 dpm", true);
        return;
      }
      try {
        const result = await Api.setTargetDrops(bed.bedId, value);
        const latest = State.beds.get(bed.bedId) || bed;
        State.upsertBed({ ...latest, ...result });
        UiUtils.toast(`${bed.bedId}: target drop rate set to ${value} dpm`);
        input.value = "";
      } catch (err) {
        UiUtils.toast(`${bed.bedId}: could not set target drop rate (${err.message})`, true);
      }
    });

    document.getElementById("monitoringBtn")?.addEventListener("click", async () => {
      const btn = document.getElementById("monitoringBtn");
      // This handler survives live readings, so consult the latest snapshot.
      const currentBed = State.beds.get(bed.bedId) || bed;
      const turningOn = currentBed.monitoring === false;

      /* Tạm dừng thì hỏi lại, bắt đầu thì không. Dừng theo dõi một giường đang
       * truyền dịch là làm chiếc máy im trong lúc bệnh nhân vẫn nằm đó. */
      if (!turningOn && !(await UiUtils.confirm(
            `Pause monitoring for ${bed.bedId}?\n\n`
          + `The device keeps reading and showing values, but will raise no `
          + `alarms for this patient until monitoring is started again.\n\n`
          + `This is not the same as taking a sample — Reset scale and `
          + `Recalibrate baseline below do not need monitoring paused.`,
          { title: "Pause monitoring?", confirmLabel: "Pause monitoring", danger: true }))) {
        return;
      }

      btn.disabled = true;
      try {
        const result = await Api.setMonitoring(bed.bedId, turningOn);
        // The command is already accepted and persisted at this point. Apply
        // the response immediately instead of waiting for another telemetry
        // frame, which can be delayed while the gateway changes mode.
        const latest = State.beds.get(bed.bedId) || bed;
        State.upsertBed({ ...latest, ...result });
        UiUtils.toast(turningOn
          ? `${bed.bedId}: monitoring started`
          : `${bed.bedId}: monitoring paused`);
      } catch (err) {
        UiUtils.toast(err.message, true);
      } finally {
        // updateLiveDetail deliberately keeps this DOM node alive, so unlock
        // it explicitly for the next Start/Pause action.
        const currentButton = document.getElementById("monitoringBtn");
        if (currentButton) currentButton.disabled = false;
      }
    });

    document.getElementById("resetTareBtn").addEventListener("click", async () => {
      try {
        await Api.resetTare(bed.bedId);
        UiUtils.toast(`${bed.bedId}: tare command sent - remove any load from the scale`);
      } catch (err) {
        UiUtils.toast(`${bed.bedId}: could not send tare command (${err.message})`, true);
      }
    });

    document.getElementById("recalibrateHrBtn").addEventListener("click", async () => {
      try {
        await Api.recalibrateHr(bed.bedId);
        UiUtils.toast(`${bed.bedId}: HR recalibration started - measuring for 60s`);
      } catch (err) {
        UiUtils.toast(`${bed.bedId}: could not start HR recalibration (${err.message})`, true);
      }
    });

    /* The toast deliberately points at the AI test figures rather than
     * claiming success: the request reaching the server is not the same event
     * as the chip applying it, and only the second one matters here. */
    document.querySelectorAll("[data-test-mode]").forEach((btn) => {
      btn.addEventListener("click", async () => {
        const mode = Number(btn.getAttribute("data-test-mode"));
        try {
          await Api.setVitalsTestMode(bed.bedId, mode);
          UiUtils.toast(`${bed.bedId}: test mode requested - watch "AI test HR / SpO2" to confirm the chip applied it`);
        } catch (err) {
          UiUtils.toast(`${bed.bedId}: could not change test mode (${err.message})`, true);
        }
      });
    });
  }

  /* ---------------- Trends (per-metric time series) ----------------------
   *
   * The charts are rendered SEPARATELY from renderDetail(). The detail panel
   * re-renders on every SignalR bed update (~once a second), and re-fetching
   * a few hundred history points at that rate would hammer the DB and make
   * the charts flicker as their DOM is replaced. So history is fetched only
   * when the panel opens, when the range changes, or on the slow refresh
   * timer below, and painted into a container the detail re-render preserves.
   */
  const TREND_RANGES = [
    { label: "15m", minutes: 15 },
    { label: "1h", minutes: 60 },
    { label: "6h", minutes: 360 },
    { label: "24h", minutes: 1440 }
  ];

  // Metrics get their own y-scale floor (minSpan) so a steady reading doesn't
  // get magnified into a scary-looking sawtooth by autoscaling. Values here
  // are "the smallest range worth looking at" for each channel clinically.
  /* `limits` mirrors the server-side thresholds in VitalsStatusEvaluator, so a
   * chart never turns red for a reading the server calls Stable (or stays calm
   * for one it calls Critical). A metric with no clinical limit - drops per
   * minute, bag weight - simply keeps its own colour: colouring those by
   * arbitrary bounds would cry wolf.
   *
   * Colours here are the "all is well" colour. As soon as a metric breaches a
   * limit, severityOf() overrides it with amber or red - see metricChart(). */
  const TREND_METRICS = [
    // Heart rate uses a FIXED 1-150 bpm axis (not autoscaled): the doctor always
    // reads the same scale, so the shape of the trace is comparable between beds
    // and between visits. A reading above 150 is clamped to the top of the plot -
    // the numeric readout and the "max" figure below the chart still show the
    // true value, so nothing is hidden.
    { key: "heartRate",   label: "Heart rate",     unit: " bpm", color: "#1ea050", yRange: [1, 150],
      zeroMeansNoSignal: true,
      limits: { warnBelow: 60, warnAbove: 110, critBelow: 45, critAbove: 130 } },
    { key: "spo2",        label: "SpO2",           unit: "%",    color: "#2470c8", minSpan: 8,
      zeroMeansNoSignal: true,
      limits: { warnBelow: 95, critBelow: 90 } },
    /* Nhỏ giọt lệch y lệnh là việc của người đi kiểm tra dây, không phải việc
     * chạy tới giường. Nên nó chỉ tới mức vàng - đỏ để dành cho bệnh nhân.
     * Ngoại lệ duy nhất: nhỏ giọt bất thường CÙNG LÚC sinh hiệu bất thường,
     * và cái đó do bộ hợp nhất trên chip quyết (cấp 3), không phải do ngưỡng
     * của một biểu đồ. */
    { key: "dripRate",    label: "Drip vs target", unit: "%",    color: "#7a5bd0", minSpan: 40,
      zeroMeansNoSignal: true,
      limits: { warnBelow: 70, warnAbove: 130 } },
    { key: "dropsPerMin", label: "Drops per min",  unit: " dpm", color: "#4f46e5", minSpan: 10 },
    { key: "weightG",     label: "IV bag weight",  unit: " g",   color: "#64748b", minSpan: 50 }
  ];

  /* No metric may use amber or red as its "all is well" colour: those two
   * belong to warning and critical alone. Drip used to be amber, which made a
   * perfectly normal drip chart look like a warning at a glance. */
  const METRIC_BY_KEY = Object.fromEntries(TREND_METRICS.map((m) => [m.key, m]));

  function severityOfValue(key, value) {
    const metric = METRIC_BY_KEY[key];
    if (!metric || !metric.limits || value == null || !Number.isFinite(value)) return "ok";
    if (metric.zeroMeansNoSignal && value === 0) return "ok";   // xem severityOf()
    const l = metric.limits;
    if ((l.critBelow != null && value < l.critBelow)
     || (l.critAbove != null && value > l.critAbove)) return "critical";
    if ((l.warnBelow != null && value < l.warnBelow)
     || (l.warnAbove != null && value > l.warnAbove)) return "warning";
    return "ok";
  }

  /* Severity of the most recent reading of one metric. Judged on the latest
   * value rather than the worst value in the window: the chart answers "how is
   * this bed right now", and a spike an hour ago that has since resolved must
   * not keep the card flashing. */
  function severityOf(metric, samples) {
    if (!metric.limits) return "ok";

    /* Bỏ qua số 0 trên những kênh mà 0 là bất khả thi về mặt sinh lý.
     *
     * Cảm biến chưa cắm, hoặc bệnh nhân bỏ tay ra, thì firmware đọc 0. Coi số 0
     * đó là số đo thật nghĩa là 0 < critBelow(45) → thẻ đỏ nhấp nháy cho một
     * giường mà đơn giản là chưa có ai kẹp cảm biến. Việc mất tín hiệu đã được
     * báo ở dải cảnh báo và ở hàng trạng thái từng kênh; nó không cần thêm một
     * cái thẻ đỏ nói sai sự thật.
     *
     * Nguyên tắc này repo đã ghi từ trước: "chưa có dữ liệu" không phải là "ổn
     * định", cũng không phải là "nguy kịch". Biểu đồ trước đây chưa áp dụng. */
    let latest = null;
    for (let i = samples.length - 1; i >= 0; i--) {
      const v = samples[i][metric.key];
      if (v == null || !Number.isFinite(v)) continue;
      if (metric.zeroMeansNoSignal && v === 0) continue;
      latest = v; break;
    }
    if (latest == null) return "ok";   // mất tín hiệu -> dải cảnh báo lo, không phải thẻ đỏ

    const l = metric.limits;
    if ((l.critBelow != null && latest < l.critBelow)
     || (l.critAbove != null && latest > l.critAbove)) return "critical";
    if ((l.warnBelow != null && latest < l.warnBelow)
     || (l.warnAbove != null && latest > l.warnAbove)) return "warning";
    return "ok";
  }

  const TREND_REFRESH_MS = 15000;

  let trendMinutes = 60;
  let trendSamples = null;
  let trendLoading = false;
  let trendError = null;
  let trendTimer = null;

  /* ---- Live tail: keeping the charts level with the Live vitals tiles ----
   *
   * The stored history is always behind the tiles, by design and on two
   * counts: the server only writes a vital_samples row every
   * VitalsSave.IntervalSeconds (10s), and the browser only refetches that
   * table every TREND_REFRESH_MS (15s). The tiles meanwhile repaint on every
   * SignalR update, about once a second - so a nurse could read 143 bpm on the
   * tile while the trace beside it still ended at a value from up to ~25s ago.
   * Two views of the same instant disagreeing is exactly what a monitoring
   * screen must not do.
   *
   * Neither knob is safe to just turn down: persisting every reading bloats
   * the history table, and polling the history endpoint every second puts a
   * DB query per bed per second behind an already once-a-second push.
   *
   * So the SignalR reading the tiles use is also appended to the chart series
   * as a live point. The DB fetch stays the authority for everything it
   * covers; live points only ever extend the series past the newest stored
   * sample, and are dropped as soon as a fetch catches up with them. */
  let liveSamples = [];

  function clearLiveSamples() {
    liveSamples = [];
  }

  /* Turn the bed's current state into a sample shaped exactly like a row from
   * /history, so renderTrends() cannot tell the two apart. Deduped on
   * lastUpdated: renderDetail() also runs for re-renders that carry no new
   * reading (opening the panel, typing in a field), which must not stack
   * duplicate points on top of each other. */
  function appendLiveSample(bed) {
    if (!bed || !bed.lastUpdated) return;

    const t = new Date(bed.lastUpdated);
    if (Number.isNaN(t.getTime())) return;

    const last = liveSamples[liveSamples.length - 1];
    if (last && new Date(last.recordedAt).getTime() === t.getTime()) return;

    // A lost channel becomes a chart GAP (null), same as the stored history
    // already does - never the raw reading, which can be a stale or
    // physically-impossible 0 from an unplugged probe. Charts.metricChart
    // skips nulls when drawing the line AND when picking the "now" corner
    // value, so a disconnected sensor never paints as a flatlining 0.
    liveSamples.push({
      recordedAt: bed.lastUpdated,
      spo2: bed.spo2Signal ? bed.spo2 : null,
      heartRate: bed.heartRateSignal ? bed.heartRate : null,
      flowRate: bed.flowSignal ? bed.flowRate : null,
      dripRate: bed.dripRateSignal ? bed.dripRate : null,
      dropsPerMin: bed.dripRateSignal ? bed.dropsPerMin : null,
      weightG: bed.flowSignal ? bed.weightG : null,
      lineBlocked: bed.lineBlocked,
      aeAlarm: bed.aeAlarm
    });

    // A live point is only ever a tail on top of the stored series, so the tail
    // never needs to be longer than the gap a fetch can leave behind. Cap it
    // anyway: a panel left open for hours would otherwise grow without bound.
    const cutoff = Date.now() - trendMinutes * 60 * 1000;
    liveSamples = liveSamples.filter((s) => new Date(s.recordedAt).getTime() >= cutoff);
  }

  /* Drop live points the freshly fetched history now covers, so a reading is
   * never plotted twice - once as the live estimate and once as the stored
   * row. */
  function pruneLiveSamples() {
    if (!trendSamples || trendSamples.length === 0) return;
    const newestStored = new Date(trendSamples[trendSamples.length - 1].recordedAt).getTime();
    liveSamples = liveSamples.filter((s) => new Date(s.recordedAt).getTime() > newestStored);
  }

  function chartSamples() {
    return (trendSamples || []).concat(liveSamples);
  }

  async function loadTrends() {
    if (!selectedBedId) return;
    const bedId = selectedBedId;

    trendLoading = trendSamples === null;   // only show the spinner on a cold load
    trendError = null;
    renderTrends();

    try {
      const result = await Api.getBedHistory(bedId, trendMinutes);
      // The user may have closed the panel or switched beds while the
      // request was in flight - dropping a stale response avoids charting
      // one bed's history under another bed's name.
      if (selectedBedId !== bedId) return;
      trendSamples = result.samples || [];
      pruneLiveSamples();
    } catch (err) {
      if (selectedBedId !== bedId) return;
      trendError = err.message;
    } finally {
      trendLoading = false;
      renderTrends();
    }
  }

  function startTrendRefresh() {
    stopTrendRefresh();
    trendTimer = setInterval(loadTrends, TREND_REFRESH_MS);
  }

  function stopTrendRefresh() {
    if (trendTimer) {
      clearInterval(trendTimer);
      trendTimer = null;
    }
  }

  function trendsSectionHtml() {
    const chips = TREND_RANGES.map((r) =>
      `<button type="button" class="chip ${r.minutes === trendMinutes ? "active" : ""}" data-trend-minutes="${r.minutes}">${r.label}</button>`
    ).join("");

    return `
      <div class="bed-trends">
        <div class="trends-head">
          <h4>Trends</h4>
          <div class="chip-row" id="trendRangeChips">${chips}</div>
        </div>
        <div id="trendCharts"></div>
      </div>`;
  }

  function renderTrends() {
    const host = document.getElementById("trendCharts");
    if (!host) return;

    if (trendError) {
      host.innerHTML = `<div class="empty-state">Could not load history: ${UiUtils.escapeHtml(trendError)}</div>`;
      return;
    }
    if (trendLoading) {
      host.innerHTML = `<div class="empty-state">Loading history…</div>`;
      return;
    }
    const samples = chartSamples();
    if (samples.length === 0) {
      host.innerHTML = `<div class="empty-state">No history recorded for this period yet.
        History is sampled periodically once the bed starts sending data.</div>`;
      return;
    }

    host.innerHTML = TREND_METRICS.map((metric) => Charts.metricChart({
      label: metric.label,
      unit: metric.unit,
      color: metric.color,
      // A metric declares EITHER a fixed yRange or an autoscale floor, never
      // both - metricChart() lets yRange win when present.
      minSpan: metric.minSpan,
      yRange: metric.yRange,
      severity: severityOf(metric, samples),
      points: samples.map((s) => ({
        t: new Date(s.recordedAt),
        v: s[metric.key],
        // Shade the periods the device itself flagged, so a dip in the line
        // can be matched against when the AI actually alarmed.
        alarm: s.lineBlocked || s.aeAlarm
      }))
    })).join("");
  }

  function bindTrendHandlers() {
    const chips = document.getElementById("trendRangeChips");
    if (!chips) return;
    chips.addEventListener("click", (e) => {
      const minutes = e.target.getAttribute("data-trend-minutes");
      if (!minutes) return;
      trendMinutes = parseInt(minutes, 10);
      trendSamples = null;    // force the spinner: the new window is a cold load
      clearLiveSamples();
      renderDetail();
      loadTrends();
    });
  }

  function closeDetail({ updateRoute = true } = {}) {
    selectedBedId = null;
    renderedPatientSignature = null;
    trendSamples = null;
    trendError = null;
    clearLiveSamples();
    stopTrendRefresh();
    renderDetail();
    if (updateRoute) AppRoute.setBed(null);
  }

  /* The detail panel is rebuilt from scratch (innerHTML) on every bed update,
   * which arrives about once a second over SignalR. That wipes whatever the
   * doctor is currently typing into the target inputs - in practice it was
   * impossible to finish typing a new target before the field cleared itself.
   *
   * Rather than restructure the whole panel into fine-grained DOM updates,
   * capture the state that must survive a rebuild (input contents, which field
   * had focus, and the caret position) and put it back afterwards. Caret
   * position matters too: without it the cursor jumps to the start of the field
   * on every tick, so typing "120" would come out as "021". */
  function captureFormState() {
    const active = document.activeElement;
    const state = { values: {}, focusId: null, selStart: null, selEnd: null };

    /* patientNameInput/patientCodeInput belong here, not just in
     * captureInteractionState() below: that one only preserves whichever ONE
     * field currently has focus, so typing a name then tabbing to the ID
     * field let the next rebuild revert the name back to blank - it was never
     * the focused field at rebuild time, so nothing kept it. This list
     * preserves every field in it regardless of which one is focused. */
    ["targetFlowInput", "targetDropsInput", "patientNameInput", "patientCodeInput"].forEach((id) => {
      const el = document.getElementById(id);
      if (el) state.values[id] = el.value;
    });

    if (active && state.values.hasOwnProperty(active.id)) {
      state.focusId = active.id;
      try {
        state.selStart = active.selectionStart;
        state.selEnd = active.selectionEnd;
      } catch (e) {
        // Some input types don't expose a selection range - not worth failing over.
      }
    }
    return state;
  }

  function restoreFormState(state) {
    Object.entries(state.values).forEach(([id, value]) => {
      const el = document.getElementById(id);
      // Only restore a non-empty value, so a field the user just submitted
      // (and which the submit handler cleared) stays cleared.
      if (el && value !== "" && value != null) el.value = value;
    });

    if (state.focusId) {
      const el = document.getElementById(state.focusId);
      if (el) {
        el.focus();
        if (state.selStart != null) {
          try {
            el.setSelectionRange(state.selStart, state.selEnd);
          } catch (e) { /* as above */ }
        }
      }
    }
  }

  /* Chụp lại trạng thái người dùng đang thao tác, trước khi innerHTML bị thay.
   *
   * renderDetail() dựng lại toàn bộ panel mỗi khi có dữ liệu mới - tức là mỗi
   * giây. Hai thứ bị mất mỗi lần:
   *
   *   - Vị trí cuộn của cột phải. Y tá cuộn xuống ô "Drop rate", một gói dữ
   *     liệu tới, panel dựng lại, cuộn nhảy về đầu. Cuộn xuống nữa, lại nhảy.
   *     Trên thực tế là không thao tác nổi.
   *   - Nội dung đang gõ dở trong ô nhập, cùng vị trí con trỏ.
   *
   * Cách đúng về lâu dài là chỉ cập nhật những nút text thay đổi thay vì dựng
   * lại cả cây DOM. Cho tới lúc đó, chụp và khôi phục là cách sửa nhỏ mà giải
   * quyết đúng triệu chứng. */
  function captureInteractionState() {
    const scrolls = Array.from(document.querySelectorAll("#bedDetailPanel .bd-col"))
      .map((el, i) => ({ i, top: el.scrollTop }));

    const active = document.activeElement;
    const focus = (active && active.id && active.closest("#bedDetailPanel"))
      ? { id: active.id, value: active.value,
          start: active.selectionStart, end: active.selectionEnd }
      : null;

    return { scrolls, focus };
  }

  function restoreInteractionState(state) {
    if (!state) return;
    const cols = document.querySelectorAll("#bedDetailPanel .bd-col");
    state.scrolls.forEach(({ i, top }) => { if (cols[i]) cols[i].scrollTop = top; });

    if (!state.focus) return;
    const el = document.getElementById(state.focus.id);
    if (!el) return;
    if (state.focus.value !== undefined) el.value = state.focus.value;
    el.focus();
    if (state.focus.start != null && el.setSelectionRange) {
      try { el.setSelectionRange(state.focus.start, state.focus.end); } catch { /* not a text input */ }
    }
  }

  function detailHeaderHtml(bed) {
    const statusColor = UiUtils.statusColor(bed.status);
    return `
      <div class="bd-header" id="bedDetailHeader" style="--bd-status:${statusColor};">
        <button type="button" class="btn" id="closeBedDetailBtn">&larr; Beds</button>
        <div class="bd-ident">
          <span class="bd-bed-id">${UiUtils.escapeHtml(bed.bedId)}</span>
          <span class="bd-room">${UiUtils.escapeHtml(bed.room)}</span>
          ${bed.patientName ? `<span class="bd-patient-name-header">${UiUtils.escapeHtml(bed.patientName)}</span>` : ""}
        </div>
        <span class="status-chip" style="background:${statusColor};">${UiUtils.escapeHtml((bed.status || "UNKNOWN").toUpperCase())}</span>
        ${UiUtils.culpritBadgeHtml(bed)}
        ${UiUtils.signalRowHtml(bed)}
        <div class="bd-updated">Updated ${UiUtils.formatDateTime(bed.lastUpdated)}</div>
      </div>`;
  }

  function bindCloseDetailHandler() {
    document.getElementById("closeBedDetailBtn")?.addEventListener("click", closeDetail);
  }

  function renderDetail() {
    const panel = document.getElementById("bedDetailPanel");
    const bed = selectedBedId ? State.beds.get(selectedBedId) : null;
    if (!bed) {
      panel.classList.remove("fullscreen-open");
      panel.style.display = "none";
      document.body.classList.remove("no-scroll");
      return;
    }

    const formState = captureFormState();

    maybeShowEventToast(bed);

    panel.classList.add("fullscreen-open");
    // Clear the inline display rather than setting "block": the overlay is a
    // flex column, and an inline display would beat the stylesheet and let the
    // 3-column workspace size to its content instead of to the viewport.
    panel.style.display = "";
    document.body.classList.add("no-scroll");

    /* Phải chụp TRƯỚC khi gán innerHTML - sau đó các phần tử cũ đã biến mất. */
    const interaction = captureInteractionState();

    renderedPatientSignature = JSON.stringify([
      bed.patientName || null,
      bed.patientCode || null,
      bed.admittedAt || null
    ]);
    panel.innerHTML = `
      ${detailHeaderHtml(bed)}
      <div class="bd-grid">
        <div class="bd-col bd-col-side">
          ${vitalsSectionHtml(bed)}
        </div>
        <div class="bd-col bd-col-main">
          ${trendsSectionHtml()}
        </div>
        <div class="bd-col bd-col-side bd-col-right">
          <div class="bd-card" id="bedLineState">
            ${lineSectionHtml(bed)}
          </div>
          <div class="bd-card" id="bedFusionState">
            ${fusionSectionHtml(bed)}
          </div>
          <div class="bd-card" id="bedForecastState">
            <h4>AI forecast (on-chip)</h4>
            ${forecastSectionHtml(bed)}
          </div>
          <div class="bd-card" data-cap="bed.control">
            <h4>Doctor-configurable settings</h4>
            ${settingsSectionHtml(bed)}
          </div>
        </div>
      </div>`;

    restoreInteractionState(interaction);

    bindCloseDetailHandler();

    // The initial render paints the cached samples. Subsequent SignalR readings
    // update only the live regions and append to these existing chart nodes.
    appendLiveSample(bed);
    bindTrendHandlers();
    renderTrends();

    // Put back whatever the doctor was typing, AFTER every handler is bound.
    restoreFormState(formState);

    bindSettingsHandlers(bed);
    bindPatientHandlers(bed);
    bindFaultHandlers(bed);

    // Capability gating is applied whenever the panel is intentionally rebuilt.
    Session.applyTo(panel);
  }

  function updateLiveDetail(bed) {
    if (!bed || bed.bedId !== selectedBedId) return;

    const patientSignature = JSON.stringify([
      bed.patientName || null,
      bed.patientCode || null,
      bed.admittedAt || null
    ]);

    // Admission/discharge changes are rare and alter controls as well as text,
    // so a deliberate one-time rebuild is appropriate for those transitions.
    if (patientSignature !== renderedPatientSignature ||
        !document.getElementById("bedLiveState")) {
      renderDetail();
      return;
    }

    maybeShowEventToast(bed);

    const header = document.getElementById("bedDetailHeader");
    if (header) {
      header.outerHTML = detailHeaderHtml(bed);
      bindCloseDetailHandler();
    }

    document.getElementById("bedLiveState").innerHTML = liveVitalsSectionHtml(bed);

    const line = document.getElementById("bedLineState");
    if (line) line.innerHTML = lineSectionHtml(bed);
    /* Repainted on every update like the rest: these are the numbers somebody
     * stares at while a level changes, so a stale copy is worse than none. */
    const fusion = document.getElementById("bedFusionState");
    if (fusion) fusion.innerHTML = fusionSectionHtml(bed);

    const forecast = document.getElementById("bedForecastState");
    if (forecast) {
      forecast.innerHTML = `<h4>AI forecast (on-chip)</h4>${forecastSectionHtml(bed)}`;
    }

    const tare = document.getElementById("tareStatusState");
    if (tare) tare.innerHTML = tareStatusHtml(bed);
    const hr = document.getElementById("hrStatusState");
    if (hr) hr.innerHTML = hrStatusHtml(bed);
    /* Must be patched here, not only at first render: this block IS the
     * confirmation that a test-mode command reached the chip, and a panel that
     * never repaints it would show the moment before the command every time. */
    const vitalsTest = document.getElementById("vitalsTestState");
    if (vitalsTest) vitalsTest.innerHTML = vitalsTestHtml(bed);
    const target = document.getElementById("targetDropsCurrent");
    if (target) target.textContent = UiUtils.formatMetric(bed.targetDropsPerMin, " dpm");
    const measured = document.getElementById("targetDropsMeasured");
    if (measured) measured.textContent = `measured ${UiUtils.formatMetric(bed.dropsPerMin, " dpm")}`;

    const monitoringOn = bed.monitoring !== false;
    const calibrating = monitoringOn && bed.alertsArmed === false;
    const monitoringBlock = document.querySelector("#bedDetailPanel .monitoring-block");
    monitoringBlock?.classList.toggle("is-on", monitoringOn && !calibrating);
    monitoringBlock?.classList.toggle("is-standby", !monitoringOn);
    monitoringBlock?.classList.toggle("is-calibrating", calibrating);
    const monitoringChip = monitoringBlock?.querySelector(".status-chip");
    if (monitoringChip) {
      monitoringChip.textContent = calibrating ? "COLLECTING DATA" : (monitoringOn ? "MONITORING" : "STANDBY");
      monitoringChip.style.background = calibrating ? "#e68a00" : (monitoringOn ? "#1ea050" : "#8a97a8");
    }
    const monitoringDescription = monitoringBlock?.querySelector(".monitoring-state .muted");
    if (monitoringDescription) {
      monitoringDescription.textContent = monitoringOn
        ? (calibrating
          ? "Collecting startup samples. AI alarms remain off until both counters finish."
          : "AI and alarms are running for this bed.")
        : "Sensors are read and shown, but no AI and no alarms yet.";
    }
    const dropSamples = Math.max(0, Math.min(20, Number(bed.dropTrainingSamples) || 0));
    const vitalsSamples = Math.max(0, Math.min(64, Number(bed.vitalsTrainingSamples) || 0));
    const progress = document.getElementById("monitoringProgress");
    if (progress) progress.style.display = calibrating ? "" : "none";
    const dropProgress = document.getElementById("dropTrainingProgress");
    if (dropProgress) dropProgress.value = dropSamples;
    const vitalsProgress = document.getElementById("vitalsTrainingProgress");
    if (vitalsProgress) vitalsProgress.value = vitalsSamples;
    const dropText = document.getElementById("dropTrainingText");
    if (dropText) dropText.textContent = `${dropSamples}/20`;
    const vitalsText = document.getElementById("vitalsTrainingText");
    if (vitalsText) vitalsText.textContent = `${vitalsSamples}/64`;
    const monitoringBtn = document.getElementById("monitoringBtn");
    if (monitoringBtn) {
      monitoringBtn.textContent = monitoringOn ? "Pause monitoring" : "Start monitoring";
      monitoringBtn.classList.toggle("primary", !monitoringOn);
    }

    // Preserve chart canvases, range selection, form values, focus and scroll.
    appendLiveSample(bed);
    renderTrends();
  }

  function bindPatientHandlers(bed) {
    const form = document.getElementById("patientForm");
    if (!form) return;   // removed for roles without patient.edit

    form.addEventListener("submit", async (e) => {
      e.preventDefault();
      const patientName = document.getElementById("patientNameInput").value.trim();
      if (!patientName) {
        UiUtils.toast("Enter a patient name, or use Discharge to free the bed", true);
        return;
      }
      try {
        await Api.setPatient(bed.bedId, {
          patientName,
          patientCode: document.getElementById("patientCodeInput").value.trim()
        });
        UiUtils.toast(`${bed.bedId}: patient updated`);
      } catch (err) {
        UiUtils.toast(err.message, true);
      }
    });

    document.getElementById("dischargeBtn")?.addEventListener("click", async () => {
      if (!(await UiUtils.confirm(`Discharge the patient in ${bed.bedId}?`,
            { title: "Discharge patient?", confirmLabel: "Discharge", danger: true }))) return;
      try {
        // Clearing the name is the discharge; the server clears code and
        // admission time with it so nothing is left to misread later.
        await Api.setPatient(bed.bedId, { patientName: null, patientCode: null });
        UiUtils.toast(`${bed.bedId}: patient discharged`);
      } catch (err) {
        UiUtils.toast(err.message, true);
      }
    });
  }

  function renderList() {
    renderFilters();
    const total = State.beds.size;
    const beds = Array.from(State.beds.values())
      .filter(matchesFilters)
      .sort((a, b) => UiUtils.severityCompare(a.status, b.status) || a.bedId.localeCompare(b.bedId));
    const grid = document.getElementById("bedsGrid");
    // Viewing one room means far fewer cards, so let them grow into the space
    // instead of leaving most of the page empty. Kept off for "all rooms",
    // where fitting the whole ward on screen matters more than card size.
    grid.classList.toggle("is-single-room", selectedRoom !== "all");
    grid.innerHTML = beds.length === 0
      ? `<div class="empty-state">No beds match the current filters.</div>`
      : beds.map(bedCardHtml).join("");

    const countEl = document.getElementById("bedsCount");
    if (countEl) countEl.textContent = beds.length === total ? `${total} beds` : `Showing ${beds.length} of ${total}`;

    grid.querySelectorAll(".bed-card").forEach((card) => {
      card.addEventListener("click", () => {
        const bedId = card.getAttribute("data-bed-id");
        const switchingBed = bedId !== selectedBedId;
        selectedBedId = bedId;
        AppRoute.setBed(bedId);
        if (switchingBed) {
          trendSamples = null;   // never show the previous bed's history
          trendError = null;
          clearLiveSamples();
        }
        renderDetail();
        loadTrends();
        startTrendRefresh();
      });
    });

  }

  function render() {
    renderList();
    renderDetail();
  }

  function handleBedsChanged() {
    renderList();
    if (!selectedBedId) return;

    const bed = State.beds.get(selectedBedId);
    if (!bed) {
      closeDetail();
      return;
    }
    updateLiveDetail(bed);
  }

  function init() {
    State.on("beds-changed", handleBedsChanged);
    loadMyBedIds();

    document.getElementById("bedSearch").addEventListener("input", (e) => {
      searchTerm = e.target.value;
      render();
    });

    document.getElementById("bedRoomFilter").addEventListener("change", (e) => {
      selectedRoom = e.target.value;
      persist();
      render();
    });

    document.getElementById("bedStatusFilters").addEventListener("click", (e) => {
      const status = e.target.getAttribute("data-status");
      if (status) { selectedStatus = status; persist(); render(); }
      if (e.target.hasAttribute("data-my-beds")) { myBedsOnly = !myBedsOnly; persist(); render(); }
    });

    render();
  }

  /* Opens a bed's full detail view from outside this module (the dashboard
   * cards use it). Kept here rather than duplicated there so the selection,
   * the history fetch and the refresh timer all stay in one place. */
  function openBed(bedId, { updateRoute = true } = {}) {
    if (!State.beds.has(bedId)) return;
    if (bedId !== selectedBedId) {
      trendSamples = null;
      trendError = null;
    }
    selectedBedId = bedId;
    if (updateRoute) AppRoute.setBed(bedId);
    bedFaultReports = [];
    renderDetail();
    loadTrends();
    loadBedFaultReports(bedId);
    startTrendRefresh();
  }

  return { init, render, openBed, closeDetail };
})();
