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
    const rooms = ["all", ...new Set(Array.from(State.beds.values()).map((b) => b.room).filter(Boolean))];
    document.getElementById("bedRoomFilters").innerHTML = rooms.map((room) =>
      `<button class="chip ${room === selectedRoom ? "active" : ""}" data-room="${UiUtils.escapeHtml(room)}">${room === "all" ? "All rooms" : UiUtils.escapeHtml(room)}</button>`
    ).join("");

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
          ? `<div class="fc-note">The model only learned NORMAL behaviour. A channel marked
             <b>“expected if normal”</b> is far outside that range right now, so the figure shown is
             the level the model expects for a healthy line — <b>not</b> a prediction of what will
             happen. The gap between it and the current reading is what raises the anomaly score.</div>`
          : ""}
      </div>`;
  }

  function settingsSectionHtml(bed) {
    return `
      <div class="bed-settings">
        <h4>Doctor-configurable settings</h4>

        <div class="settings-block">
          <label>Load cell</label>
          <div class="metric-row">
            <div class="metric"><b>${UiUtils.formatMetric(bed.weightG, " g")}</b><span>IV bag weight</span></div>
          </div>
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
          <label>Infusion rate (AI alert target)</label>
          <div class="current-target">Current: <b>${UiUtils.formatMetric(bed.targetFlowMlH, " ml/h")}</b></div>
          <form id="targetFlowForm" class="inline-form">
            <input type="number" min="1" id="targetFlowInput" placeholder="New target" value="">
            <button type="submit" class="btn primary">Set</button>
          </form>
        </div>

        <div class="settings-block">
          <label>Drop rate (AI alert target)</label>
          <div class="current-target">Current: <b>${UiUtils.formatMetric(bed.targetDropsPerMin, " dpm")}</b>
            &nbsp;·&nbsp; Measured: <b>${UiUtils.formatMetric(bed.dropsPerMin, " dpm")}</b></div>
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
  const TREND_METRICS = [
    // Heart rate uses a FIXED 1-150 bpm axis (not autoscaled): the doctor always
    // reads the same scale, so the shape of the trace is comparable between beds
    // and between visits. A reading above 150 is clamped to the top of the plot -
    // the numeric readout and the "max" figure below the chart still show the
    // true value, so nothing is hidden.
    { key: "heartRate",   label: "Heart rate",     unit: " bpm", color: "#dc3c3c", yRange: [1, 150] },
    { key: "spo2",        label: "SpO2",           unit: "%",    color: "#2470c8", minSpan: 8 },
    { key: "flowRate",    label: "Flow vs target", unit: "%",    color: "#1ea050", minSpan: 40 },
    { key: "dripRate",    label: "Drip vs target", unit: "%",    color: "#e69119", minSpan: 40 },
    { key: "dropsPerMin", label: "Drops per min",  unit: " dpm", color: "#7a5bd0", minSpan: 10 },
    { key: "weightG",     label: "IV bag weight",  unit: " g",   color: "#0d8f96", minSpan: 50 }
  ];

  const TREND_REFRESH_MS = 15000;

  let trendMinutes = 60;
  let trendSamples = null;
  let trendLoading = false;
  let trendError = null;
  let trendTimer = null;

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
    if (!trendSamples || trendSamples.length === 0) {
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
      points: trendSamples.map((s) => ({
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
      renderDetail();
      loadTrends();
    });
  }

  function closeDetail() {
    selectedBedId = null;
    trendSamples = null;
    trendError = null;
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
    panel.style.display = "block";
    document.body.classList.add("no-scroll");
    panel.innerHTML = `
      <div class="fullscreen-header">
        <button type="button" class="btn" id="closeBedDetailBtn">&larr; Back to beds</button>
      </div>
      <div class="fullscreen-body">
        <h3>${UiUtils.escapeHtml(bed.bedId)}</h3>
        <div class="sub">${UiUtils.escapeHtml(bed.room)} · ${UiUtils.escapeHtml((bed.status || "").toUpperCase())}</div>
        <div class="metric-row">
          <div class="metric"><b>${UiUtils.formatMetric(bed.spo2, "%")}</b><span>SpO2</span></div>
          <div class="metric"><b>${UiUtils.formatMetric(bed.heartRate, "")}</b><span>Heart Rate</span></div>
          <div class="metric"><b>${UiUtils.formatMetric(bed.flowRate, "%")}</b><span>Flow Rate</span></div>
          <div class="metric"><b>${UiUtils.formatMetric(bed.dripRate, "%")}</b><span>Drip Rate</span></div>
        </div>
        ${UiUtils.signalRowHtml(bed)}
        ${bed.alertMessage ? `<div class="bed-message">${UiUtils.escapeHtml(bed.alertMessage)}</div>` : ""}
        <form id="editBedForm">
          <label>Room</label>
          <input type="text" id="editBedRoom" value="${UiUtils.escapeHtml(bed.room)}">
          <button type="submit" class="btn primary">Save</button>
        </form>
        ${forecastSectionHtml(bed)}
        ${trendsSectionHtml()}
        ${settingsSectionHtml(bed)}
      </div>`;

    document.getElementById("closeBedDetailBtn").addEventListener("click", closeDetail);

    // renderDetail() runs on every bed update, so the charts must be repainted
    // from the cached samples each time - loadTrends() is NOT called here, or
    // every SignalR tick would trigger a history query.
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
  }

  function render() {
    renderFilters();
    const beds = Array.from(State.beds.values()).filter(matchesFilters).sort((a, b) => a.bedId.localeCompare(b.bedId));
    const grid = document.getElementById("bedsGrid");
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

    document.getElementById("bedRoomFilters").addEventListener("click", (e) => {
      const room = e.target.getAttribute("data-room");
      if (room) { selectedRoom = room; render(); }
    });

    document.getElementById("bedStatusFilters").addEventListener("click", (e) => {
      const status = e.target.getAttribute("data-status");
      if (status) { selectedStatus = status; render(); }
    });

    document.getElementById("addBedBtn").addEventListener("click", () => {
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

  return { init, render };
})();
