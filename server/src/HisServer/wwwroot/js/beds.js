const BedsTab = (() => {
  let searchTerm = "";
  let selectedRoom = "all";
  let selectedStatus = "all";
  let selectedBedId = null;

  const STATUS_FILTERS = ["all", "Stable", "Warning", "Critical", "Offline"];

  function matchesFilters(bed) {
    if (selectedRoom !== "all" && bed.room !== selectedRoom) return false;
    if (selectedStatus !== "all" && bed.status !== selectedStatus) return false;
    if (searchTerm) {
      const haystack = `${bed.bedId} ${bed.room}`.toLowerCase();
      if (!haystack.includes(searchTerm.toLowerCase())) return false;
    }
    return true;
  }

  function bedCardHtml(bed) {
    const color = UiUtils.statusColor(bed.status);
    return `
      <div class="bed-card" data-bed-id="${UiUtils.escapeHtml(bed.bedId)}">
        <div class="stripe" style="background:${color};"></div>
        <div class="body">
          <div class="row-top">
            <div><div class="bed-id">${UiUtils.escapeHtml(bed.bedId)}</div><div class="bed-room">${UiUtils.escapeHtml(bed.room)}</div></div>
            <span class="status-chip" style="background:${color};">${UiUtils.escapeHtml((bed.status || "UNKNOWN").toUpperCase())}</span>
          </div>
          <div class="vitals">
            <div class="vital">SpO2<b>${UiUtils.formatMetric(bed.spo2, "%")}</b></div>
            <div class="vital">Heart Rate<b>${UiUtils.formatMetric(bed.heartRate, "")}</b></div>
            <div class="vital">Flow<b>${UiUtils.formatMetric(bed.flowRate, "%")}</b></div>
            <div class="vital">Drip<b>${UiUtils.formatMetric(bed.dripRate, "%")}</b></div>
          </div>
          ${UiUtils.signalRowHtml(bed)}
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
    ).join("");
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
  function vitalsSectionHtml(bed) {
    /* A tile carries the same severity as that metric's chart, so the number
     * a nurse reads first and the trace they check second never disagree.
     * A channel with no signal is dimmed and never coloured by severity: a
     * reading of 0 from an unplugged probe is not a dangerous reading. */
    const tile = (key, unit, label, ok) => {
      const color = (METRIC_BY_KEY[key] || {}).color || "#2470c8";
      const sev = ok === false ? "ok" : severityOfValue(key, bed[key]);
      const cls = (ok === false ? " is-lost" : "") + (sev === "ok" ? "" : ` sev-${sev}`);
      return `
        <div class="bd-vital${cls}" style="--vital-color:${color};">
          <span class="v">${UiUtils.formatMetric(bed[key], "")}<span class="u">${UiUtils.escapeHtml(unit)}</span></span>
          <span class="k">${UiUtils.escapeHtml(label)}${ok === false ? " · no signal" : ""}</span>
        </div>`;
    };

    return `
      <div class="bd-card">
        <h4>Live vitals</h4>
        <div class="bd-vitals">
          ${tile("spo2", "%", "SpO2", bed.spo2Signal)}
          ${tile("heartRate", " bpm", "Heart rate", bed.heartRateSignal)}
          ${tile("flowRate", "%", "Flow vs target", bed.flowSignal)}
          ${tile("dripRate", "%", "Drip vs target", bed.dripRateSignal)}
          ${tile("dropsPerMin", " dpm", "Drops per min", bed.dripRateSignal)}
          ${tile("weightG", " g", "IV bag weight", bed.flowSignal)}
        </div>
      </div>
      <div class="bd-card">
        <h4>Sensor channels</h4>
        <div class="bd-channels">
          ${channelRow("Heart rate", bed.heartRateSignal)}
          ${channelRow("SpO2", bed.spo2Signal)}
          ${channelRow("Flow", bed.flowSignal)}
          ${channelRow("Drip", bed.dripRateSignal)}
          ${channelRow("IV line", !bed.lineBlocked, "Flowing", "Blocked / free-flow")}
        </div>
      </div>
      ${alertCardHtml(bed)}
      ${patientCardHtml(bed)}
      ${faultCardHtml(bed)}
      <div class="bd-card" data-cap="beds.manage">
        <h4>Bed</h4>
        <form id="editBedForm" class="bd-room-form">
          <input type="text" id="editBedRoom" value="${UiUtils.escapeHtml(bed.room)}" placeholder="Room">
          <button type="submit" class="btn primary">Save</button>
        </form>
      </div>`;
  }

  /* The server sends every active cause in one string joined with " · ".
   * Splitting it back into separate lines matters clinically: "Critically low
   * SpO2 · IV line blocked · No signal from: HR" read as one run-on sentence
   * is how the second and third problems get missed. */
  function alertCardHtml(bed) {
    if (!bed.alertMessage) return "";

    const causes = String(bed.alertMessage).split(" · ").filter((c) => c.trim() !== "");
    const critical = (bed.status || "").toLowerCase() === "critical";

    return `
      <div class="bd-card bd-card-alert${critical ? " sev-critical" : " sev-warning"}">
        <h4>Active alert${causes.length > 1 ? ` · ${causes.length} problems` : ""}</h4>
        <ul class="bd-alert-list">
          ${causes.map((c) => `<li>${UiUtils.escapeHtml(c)}</li>`).join("")}
        </ul>
      </div>`;
  }

  function channelRow(label, ok, okText = "Signal OK", lostText = "No signal") {
    return `
      <div class="bd-channel">
        <span class="bd-channel-name">${UiUtils.escapeHtml(label)}</span>
        <span class="bd-channel-state ${ok ? "is-ok" : "is-lost"}">${ok ? okText : lostText}</span>
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
              <button type="submit" class="btn primary">${occupied ? "Updated" : "Admit patient"}</button>
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

  function settingsSectionHtml(bed) {
    return `
      <div class="bed-settings">
        <h4>Doctor-configurable settings</h4>

        <div class="settings-block">
          <label>Load cell</label>
          ${tareStatusHtml(bed)}
          <div class="inline-form" style="margin-top:8px;">
            <button type="button" id="resetTareBtn" class="btn">Reset scale (tare)</button>
          </div>
        </div>

        <div class="settings-block">
          <label>Heart rate</label>
          ${hrStatusHtml(bed)}
          <div class="inline-form" style="margin-top:8px;">
            <button type="button" id="recalibrateHrBtn" class="btn">Recalibrate 60s baseline</button>
          </div>
        </div>

        <div class="settings-block">
          <label>Infusion rate (AI alert target)
            <b class="current-target">${UiUtils.formatMetric(bed.targetFlowMlH, " ml/h")}</b>
          </label>
          <form id="targetFlowForm" class="inline-form">
            <input type="number" min="1" id="targetFlowInput" placeholder="New target" value="">
            <button type="submit" class="btn primary">Set</button>
          </form>
        </div>

        <div class="settings-block">
          <label>Drop rate (AI alert target)
            <b class="current-target">${UiUtils.formatMetric(bed.targetDropsPerMin, " dpm")}</b>
            <span class="measured">measured ${UiUtils.formatMetric(bed.dropsPerMin, " dpm")}</span>
          </label>
          <form id="targetDropsForm" class="inline-form">
            <input type="number" min="1" id="targetDropsInput" placeholder="New target" value="">
            <button type="submit" class="btn primary">Set</button>
          </form>
        </div>
      </div>`;
  }

  function bindSettingsHandlers(bed) {
    document.getElementById("targetFlowForm").addEventListener("submit", async (e) => {
      e.preventDefault();
      const input = document.getElementById("targetFlowInput");
      const value = parseInt(input.value, 10);
      if (!value || value <= 0) return;
      try {
        await Api.setTargetFlow(bed.bedId, value);
        UiUtils.toast(`${bed.bedId}: target flow rate set to ${value} ml/h`);
        input.value = "";
      } catch (err) {
        UiUtils.toast(`${bed.bedId}: could not set target flow rate (${err.message})`, true);
      }
    });

    document.getElementById("targetDropsForm").addEventListener("submit", async (e) => {
      e.preventDefault();
      const input = document.getElementById("targetDropsInput");
      const value = parseInt(input.value, 10);
      if (!value || value <= 0) return;
      try {
        await Api.setTargetDrops(bed.bedId, value);
        UiUtils.toast(`${bed.bedId}: target drop rate set to ${value} dpm`);
        input.value = "";
      } catch (err) {
        UiUtils.toast(`${bed.bedId}: could not set target drop rate (${err.message})`, true);
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
      limits: { warnBelow: 60, warnAbove: 110, critBelow: 45, critAbove: 130 } },
    { key: "spo2",        label: "SpO2",           unit: "%",    color: "#2470c8", minSpan: 8,
      limits: { warnBelow: 95, critBelow: 90 } },
    { key: "flowRate",    label: "Flow vs target", unit: "%",    color: "#0d8f96", minSpan: 40,
      limits: { warnBelow: 70, warnAbove: 130, critBelow: 40, critAbove: 160 } },
    { key: "dripRate",    label: "Drip vs target", unit: "%",    color: "#7a5bd0", minSpan: 40,
      limits: { warnBelow: 70, warnAbove: 130, critBelow: 40, critAbove: 160 } },
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

    let latest = null;
    for (let i = samples.length - 1; i >= 0; i--) {
      const v = samples[i][metric.key];
      if (v != null && Number.isFinite(v)) { latest = v; break; }
    }
    if (latest == null) return "ok";   // no signal is reported by the alert banner, not by a red chart

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

    liveSamples.push({
      recordedAt: bed.lastUpdated,
      spo2: bed.spo2,
      heartRate: bed.heartRate,
      flowRate: bed.flowRate,
      dripRate: bed.dripRate,
      dropsPerMin: bed.dropsPerMin,
      weightG: bed.weightG,
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

  function closeDetail() {
    selectedBedId = null;
    trendSamples = null;
    trendError = null;
    clearLiveSamples();
    stopTrendRefresh();
    renderDetail();
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

    ["targetFlowInput", "targetDropsInput", "editBedRoom"].forEach((id) => {
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
    const statusColor = UiUtils.statusColor(bed.status);
    panel.innerHTML = `
      <div class="bd-header" style="--bd-status:${statusColor};">
        <button type="button" class="btn" id="closeBedDetailBtn">&larr; Beds</button>
        <div class="bd-ident">
          <span class="bd-bed-id">${UiUtils.escapeHtml(bed.bedId)}</span>
          <span class="bd-room">${UiUtils.escapeHtml(bed.room)}</span>
        </div>
        <span class="status-chip" style="background:${statusColor};">${UiUtils.escapeHtml((bed.status || "UNKNOWN").toUpperCase())}</span>
        ${UiUtils.signalRowHtml(bed)}
        <div class="bd-updated">Updated ${UiUtils.formatDateTime(bed.lastUpdated)}</div>
      </div>
      <div class="bd-grid">
        <div class="bd-col bd-col-side">
          ${vitalsSectionHtml(bed)}
        </div>
        <div class="bd-col bd-col-main">
          ${trendsSectionHtml()}
        </div>
        <div class="bd-col bd-col-side bd-col-right">
          <div class="bd-card">
            <h4>AI forecast (on-chip)</h4>
            ${forecastSectionHtml(bed)}
          </div>
          <div class="bd-card" data-cap="bed.control">
            <h4>Doctor-configurable settings</h4>
            ${settingsSectionHtml(bed)}
          </div>
        </div>
      </div>`;

    document.getElementById("closeBedDetailBtn").addEventListener("click", closeDetail);

    // renderDetail() runs on every bed update, so the charts must be repainted
    // from the cached samples each time - loadTrends() is NOT called here, or
    // every SignalR tick would trigger a history query. The reading that drove
    // this re-render is instead appended to the series directly, which is what
    // keeps the traces level with the Live vitals tiles above them.
    appendLiveSample(bed);
    bindTrendHandlers();
    renderTrends();

    // Put back whatever the doctor was typing, AFTER every handler is bound.
    restoreFormState(formState);

    document.getElementById("editBedForm").addEventListener("submit", async (e) => {
      e.preventDefault();
      const room = document.getElementById("editBedRoom").value.trim();
      await Api.updateBed(bed.bedId, { room });
    });

    bindSettingsHandlers(bed);
    bindPatientHandlers(bed);
    bindFaultHandlers(bed);

    /* renderDetail() rebuilds this panel from scratch about once a second, so
     * the capability gating has to be re-applied to the new DOM each time -
     * doing it once at startup would only survive until the next reading. */
    Session.applyTo(panel);
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
      if (!confirm(`Discharge the patient in ${bed.bedId}?`)) return;
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

  function render() {
    renderFilters();
    const beds = Array.from(State.beds.values()).filter(matchesFilters).sort((a, b) => a.bedId.localeCompare(b.bedId));
    const grid = document.getElementById("bedsGrid");
    // Viewing one room means far fewer cards, so let them grow into the space
    // instead of leaving most of the page empty. Kept off for "all rooms",
    // where fitting the whole ward on screen matters more than card size.
    grid.classList.toggle("is-single-room", selectedRoom !== "all");
    grid.innerHTML = beds.length === 0
      ? `<div class="empty-state">No beds match the current filters.</div>`
      : beds.map(bedCardHtml).join("");

    grid.querySelectorAll(".bed-card").forEach((card) => {
      card.addEventListener("click", () => {
        const bedId = card.getAttribute("data-bed-id");
        const switchingBed = bedId !== selectedBedId;
        selectedBedId = bedId;
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

    renderDetail();
  }

  function init() {
    State.on("beds-changed", render);

    document.getElementById("bedSearch").addEventListener("input", (e) => {
      searchTerm = e.target.value;
      render();
    });

    document.getElementById("bedRoomFilter").addEventListener("change", (e) => {
      selectedRoom = e.target.value;
      render();
    });

    document.getElementById("bedStatusFilters").addEventListener("click", (e) => {
      const status = e.target.getAttribute("data-status");
      if (status) { selectedStatus = status; render(); }
    });

    // Removed from the DOM for roles without beds.manage, so bind defensively.
    document.getElementById("addBedBtn")?.addEventListener("click", () => {
      document.getElementById("addBedModal").classList.remove("hidden");
    });

    document.getElementById("addBedForm").addEventListener("submit", async (e) => {
      e.preventDefault();
      const bedId = document.getElementById("newBedId").value.trim();
      const room = document.getElementById("newBedRoom").value.trim();
      if (!bedId) return;
      await Api.createBed(bedId, room);
      document.getElementById("addBedModal").classList.add("hidden");
      document.getElementById("addBedForm").reset();
    });

    render();
  }

  /* Opens a bed's full detail view from outside this module (the dashboard
   * cards use it). Kept here rather than duplicated there so the selection,
   * the history fetch and the refresh timer all stay in one place. */
  function openBed(bedId) {
    if (!State.beds.has(bedId)) return;
    if (bedId !== selectedBedId) {
      trendSamples = null;
      trendError = null;
    }
    selectedBedId = bedId;
    bedFaultReports = [];
    renderDetail();
    loadTrends();
    loadBedFaultReports(bedId);
    startTrendRefresh();
  }

  return { init, render, openBed };
})();
