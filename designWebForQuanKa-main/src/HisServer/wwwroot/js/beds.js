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

  function closeDetail() {
    selectedBedId = null;
    renderDetail();
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
        ${settingsSectionHtml(bed)}
      </div>`;

    document.getElementById("closeBedDetailBtn").addEventListener("click", closeDetail);

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
        selectedBedId = card.getAttribute("data-bed-id");
        renderDetail();
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
