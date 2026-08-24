const AlertsTab = (() => {
  const persisted = UiUtils.loadFilterState("alerts", { filter: "all", roomFilter: "all" });
  let filter = persisted.filter;
  let roomFilter = persisted.roomFilter;
  let searchTerm = "";
  let page = 1;
  const pageSize = 80;
  let currentItems = [];
  let totalCount = 0;
  let selectedAlertId = null;
  // Bulk-acknowledge selection. Cleared on every load() (page change, filter
  // change, a new alert arriving) rather than carried across - carrying a
  // selection across a page the user can no longer see invites acknowledging
  // an alert they never looked at.
  const selectedIds = new Set();

  function queryParamsForFilter() {
    switch (filter) {
      case "critical": return { level: "Critical" };
      case "warning": return { level: "Warning" };
      case "unacked": return { ack: "false" };
      case "acked": return { ack: "true" };
      default: return {};
    }
  }

  function renderRoomFilter() {
    const select = document.getElementById("alertRoomFilter");
    if (!select) return;
    const rooms = Array.from(new Set(Array.from(State.beds.values()).map((b) => b.room).filter(Boolean))).sort();
    select.innerHTML = [`<option value="all">All rooms</option>`]
      .concat(rooms.map((room) => `<option value="${UiUtils.escapeHtml(room)}">${UiUtils.escapeHtml(room)}</option>`))
      .join("");
    if (roomFilter !== "all" && !rooms.includes(roomFilter)) roomFilter = "all";
    select.value = roomFilter;
  }

  async function load() {
    renderRoomFilter();
    const params = {
      ...queryParamsForFilter(),
      ...(roomFilter !== "all" ? { room: roomFilter } : {}),
      page: String(page),
      pageSize: String(pageSize)
    };
    const result = await Api.getAlerts(params);
    currentItems = result.items;
    totalCount = result.totalCount;
    selectedIds.clear();
    render();
    renderStats();
  }

  async function renderStats() {
    const [all, unackedCritical, unackedWarning, acked] = await Promise.all([
      Api.getAlerts({ page: "1", pageSize: "1" }),
      Api.getAlerts({ ack: "false", level: "Critical", page: "1", pageSize: "1" }),
      Api.getAlerts({ ack: "false", level: "Warning", page: "1", pageSize: "1" }),
      Api.getAlerts({ ack: "true", page: "1", pageSize: "1" })
    ]);
    document.getElementById("alertStatTotal").textContent = all.totalCount;
    document.getElementById("alertStatCritical").textContent = unackedCritical.totalCount;
    document.getElementById("alertStatWarning").textContent = unackedWarning.totalCount;
    document.getElementById("alertStatAcked").textContent = acked.totalCount;

    const badge = document.getElementById("alertBadge");
    const activeCount = unackedCritical.totalCount + unackedWarning.totalCount;
    badge.style.display = activeCount > 0 ? "inline-block" : "none";
    badge.textContent = activeCount;
  }

  function visibleItems() {
    if (!searchTerm) return currentItems;
    const term = searchTerm.toLowerCase();
    return currentItems.filter((a) => `${a.bedId} ${a.message}`.toLowerCase().includes(term));
  }

  /* Alerts carry no patient name of their own - they are a historical record
   * of what a bed's vitals were doing, not of who was in it. Looked up live
   * from State.beds instead: good enough for the active alerts a nurse is
   * actually triaging (viewed minutes after they fire, same patient still in
   * the bed), and avoids needing to freeze a patient name into every alert
   * row at write time just to show one here. */
  function patientNameOf(bedId) {
    return State.beds.get(bedId)?.patientName || null;
  }

  function alertRowHtml(alert) {
    const color = UiUtils.statusColor(alert.level);
    const patientName = patientNameOf(alert.bedId);
    // alertType is the single most severe cause at the time this alert was
    // raised (see VitalsStatusEvaluator.DescribeAlert) - describeAlertType
    // turns that code into the sensor/source label, and alert.message stays
    // the full server-written reason text (already includes measured values
    // where the evaluator has them, e.g. "Critically low SpO2: 84%").
    const info = UiUtils.describeAlertType(alert.alertType);
    // Only unacknowledged alerts are selectable - bulk-acknowledging an
    // already-handled alert has nothing to do.
    const checkbox = alert.acknowledged ? `<span class="alert-item-select"></span>` : `
      <label class="alert-item-select" onclick="event.stopPropagation();">
        <input type="checkbox" data-select-alert="${alert.id}" ${selectedIds.has(alert.id) ? "checked" : ""}>
      </label>`;
    return `
      <div class="alert-item" data-alert-id="${alert.id}">
        ${checkbox}
        <div class="stripe" style="background:${color};"></div>
        <div>
          <div class="title" style="color:${color};">${UiUtils.escapeHtml(alert.level)} · ${UiUtils.escapeHtml(alert.bedId)}${
            patientName ? ` · ${UiUtils.escapeHtml(patientName)}` : ""}</div>
          <div class="desc"><b>${UiUtils.escapeHtml(info.sensor)}</b> — ${UiUtils.escapeHtml(alert.message)}</div>
          <div class="time">${UiUtils.escapeHtml(alert.room || "")} · ${UiUtils.formatDateTime(alert.createdAt)}</div>
          ${alert.acknowledged && alert.acknowledgementNote
            ? `<div class="time">Note: ${UiUtils.escapeHtml(alert.acknowledgementNote)}</div>` : ""}
        </div>
        <div class="actions">
          ${alert.acknowledged
            ? `<span class="chip">Acknowledged${alert.acknowledgedBy ? ` by ${UiUtils.escapeHtml(alert.acknowledgedBy)}` : ""}</span>`
            : `<button class="btn small primary" data-ack="${alert.id}">Acknowledge</button>`}
        </div>
      </div>`;
  }

  function renderDetail() {
    const panel = document.getElementById("alertDetailPanel");
    const alert = currentItems.find((a) => a.id === selectedAlertId);
    if (!alert) {
      panel.style.display = "none";
      return;
    }

    const color = UiUtils.statusColor(alert.level);
    const patientName = patientNameOf(alert.bedId);
    const info = UiUtils.describeAlertType(alert.alertType);
    panel.style.display = "block";
    panel.innerHTML = `
      <h3><span class="status-chip" style="background:${color};">${UiUtils.escapeHtml(alert.level)}</span></h3>
      <div class="sub">${UiUtils.escapeHtml(alert.bedId)} · ${UiUtils.escapeHtml(alert.room || "")}${
        patientName ? ` · ${UiUtils.escapeHtml(patientName)}` : ""}</div>
      <div class="time">Source: ${UiUtils.escapeHtml(info.source)} · Sensor: ${UiUtils.escapeHtml(info.sensor)} · Channel: ${UiUtils.escapeHtml(info.channel)}</div>
      <div class="bed-message">${UiUtils.escapeHtml(alert.message)}</div>
      <div class="metric-row">
        <div class="metric"><b>${UiUtils.formatMetric(alert.spo2, "%")}</b><span>SpO2</span></div>
        <div class="metric"><b>${UiUtils.formatMetric(alert.heartRate, "")}</b><span>Heart Rate</span></div>
        <div class="metric"><b>${UiUtils.formatMetric(alert.dripRate, "%")}</b><span>Drip Rate</span></div>
      </div>
      <div class="bed-updated">Created ${UiUtils.formatDateTime(alert.createdAt)}</div>
      ${alert.acknowledged
        ? `<div class="bed-updated">Acknowledged ${UiUtils.formatDateTime(alert.acknowledgedAt)}${alert.acknowledgedBy ? ` by ${UiUtils.escapeHtml(alert.acknowledgedBy)}` : ""}</div>
           ${alert.acknowledgementNote ? `<div class="bed-message">${UiUtils.escapeHtml(alert.acknowledgementNote)}</div>` : ""}`
        : `<button class="btn primary" style="margin-top:10px;" data-ack="${alert.id}">Acknowledge Alert</button>`}
    `;
  }

  function renderBulkBar() {
    const bar = document.getElementById("alertsBulkBar");
    const count = selectedIds.size;
    bar.style.display = count > 0 ? "flex" : "none";
    document.getElementById("alertsSelectedCount").textContent =
      count > 0 ? `${count} selected` : "";

    const selectableIds = visibleItems().filter((a) => !a.acknowledged).map((a) => a.id);
    const selectAll = document.getElementById("alertsSelectAll");
    selectAll.checked = selectableIds.length > 0 && selectableIds.every((id) => selectedIds.has(id));
  }

  function render() {
    const items = visibleItems();
    document.getElementById("alertsList").innerHTML = items.length === 0
      ? `<div class="empty-state">No alerts match the current filters.</div>`
      : items.map(alertRowHtml).join("");

    const totalPages = Math.max(1, Math.ceil(totalCount / pageSize));
    document.getElementById("alertsPagination").innerHTML = `
      <button class="btn small" id="alertsPrev" ${page <= 1 ? "disabled" : ""}>Prev</button>
      <span>Page ${page} / ${totalPages}</span>
      <button class="btn small" id="alertsNext" ${page >= totalPages ? "disabled" : ""}>Next</button>`;

    document.getElementById("alertsPrev")?.addEventListener("click", () => { page = Math.max(1, page - 1); load(); });
    document.getElementById("alertsNext")?.addEventListener("click", () => { page += 1; load(); });

    const countEl = document.getElementById("alertsCount");
    if (countEl) countEl.textContent = `Showing ${items.length} of ${totalCount}`;

    document.querySelectorAll("[data-select-alert]").forEach((cb) => {
      cb.addEventListener("change", () => {
        const id = Number(cb.getAttribute("data-select-alert"));
        if (cb.checked) selectedIds.add(id); else selectedIds.delete(id);
        renderBulkBar();
      });
    });

    renderBulkBar();
    renderDetail();
  }

  /* One dialog handles both the single "Acknowledge" button and bulk
   * selection - a Critical alert in the batch needs the same "are you sure"
   * whether it is one alert or twelve. Acknowledging is otherwise a single
   * click with no undo: this is the only checkpoint before the alert stops
   * being visibly active.
   *
   * Each alert gets its OWN note field, even in bulk. A note applied
   * identically to five different beds' alerts ("re-clipped sensor") is not
   * a real clinical record for any of them - five patients, five different
   * situations, even if a nurse happened to select them together because
   * they were all sitting unacknowledged at once. */
  function openAckDialog(alerts) {
    const criticalOnes = alerts.filter((a) => (a.level || "").toLowerCase() === "critical");
    const isBulk = alerts.length > 1;

    const rowHtml = (a) => `
      <div class="ack-row">
        <label for="ackNote-${a.id}">
          ${UiUtils.escapeHtml(a.bedId)}
          ${(a.level || "").toLowerCase() === "critical" ? `<span class="chip" style="background:var(--critical);color:white;">CRITICAL</span>` : ""}
          <span class="muted">— ${UiUtils.escapeHtml(a.message)}</span>
        </label>
        <input type="text" id="ackNote-${a.id}" data-ack-note="${a.id}" maxlength="500"
               placeholder="What was done? (optional)">
      </div>`;

    const backdrop = document.createElement("div");
    backdrop.className = "modal-backdrop";
    backdrop.innerHTML = `
      <form class="modal-card ack-modal" id="ackForm">
        <h3>${isBulk ? `Acknowledge ${alerts.length} alerts` : `Acknowledge alert · ${UiUtils.escapeHtml(alerts[0].bedId)}`}</h3>
        ${criticalOnes.length > 0 ? `
          <div class="form-error" style="display:block;background:var(--critical);color:white;padding:8px 10px;border-radius:6px;">
            ${criticalOnes.length === alerts.length
              ? "This is a CRITICAL alert. Only acknowledge once it has actually been addressed."
              : `${criticalOnes.length} of these are CRITICAL: ${criticalOnes.map((a) => UiUtils.escapeHtml(a.bedId)).join(", ")}. Only acknowledge once addressed.`}
          </div>` : ""}
        <div class="ack-rows">${alerts.map(rowHtml).join("")}</div>
        <div class="modal-actions">
          <button type="button" class="btn" id="ackCancel">Cancel</button>
          <button type="submit" class="btn primary">${criticalOnes.length > 0 ? "Confirm — mark as handled" : "Acknowledge"}</button>
        </div>
      </form>`;
    document.body.appendChild(backdrop);

    backdrop.querySelector("#ackCancel").addEventListener("click", () => backdrop.remove());
    backdrop.querySelector("#ackForm").addEventListener("submit", async (e) => {
      e.preventDefault();
      const submitBtn = backdrop.querySelector("button[type=submit]");
      submitBtn.disabled = true;

      // Settled, not all-or-nothing: a network blip on one alert must not
      // hide that the other four went through, and must not silently drop
      // the failed one either - the user needs to know exactly which ID(s)
      // still need a retry.
      const results = await Promise.allSettled(alerts.map((a) => {
        const note = backdrop.querySelector(`[data-ack-note="${a.id}"]`).value.trim();
        return Api.ackAlert(a.id, note).then(() => a);
      }));

      const succeeded = results.filter((r) => r.status === "fulfilled").map((r) => r.value);
      const failed = results.filter((r) => r.status === "rejected");

      if (failed.length === 0) {
        backdrop.remove();
        selectedIds.clear();
        UiUtils.toast(isBulk ? `${alerts.length} alerts acknowledged` : "Alert acknowledged");
      } else if (succeeded.length === 0) {
        submitBtn.disabled = false;
        UiUtils.toast(failed[0].reason?.message || "Could not acknowledge", true);
      } else {
        // Partial success: remove the rows that went through, leave the
        // failed ones in the dialog (with their notes intact) so the user
        // can just retry those instead of starting over.
        succeeded.forEach((a) => {
          selectedIds.delete(a.id);
          backdrop.querySelector(`[data-ack-note="${a.id}"]`)?.closest(".ack-row")?.remove();
        });
        submitBtn.disabled = false;
        UiUtils.toast(
          `${succeeded.length} acknowledged, ${failed.length} failed — fix and retry below`, true);
        return;
      }
      load();
    });
  }

  function applyFilterChip() {
    document.querySelectorAll("#alertFilters .chip").forEach((chip) => {
      chip.classList.toggle("active", chip.getAttribute("data-filter") === filter);
    });
  }

  function persist() {
    UiUtils.saveFilterState("alerts", { filter, roomFilter });
  }

  function init() {
    applyFilterChip();

    document.getElementById("alertFilters").addEventListener("click", (e) => {
      const f = e.target.getAttribute("data-filter");
      if (f) { filter = f; page = 1; persist(); applyFilterChip(); load(); }
    });

    document.getElementById("alertRoomFilter").addEventListener("change", (e) => {
      roomFilter = e.target.value;
      page = 1;
      persist();
      load();
    });

    document.getElementById("alertSearch").addEventListener("input", (e) => {
      searchTerm = e.target.value;
      render();
    });

    document.getElementById("alertsSelectAll").addEventListener("change", (e) => {
      const selectableIds = visibleItems().filter((a) => !a.acknowledged).map((a) => a.id);
      if (e.target.checked) selectableIds.forEach((id) => selectedIds.add(id));
      else selectableIds.forEach((id) => selectedIds.delete(id));
      render();
    });

    document.getElementById("alertsBulkAckBtn").addEventListener("click", () => {
      const alerts = currentItems.filter((a) => selectedIds.has(a.id));
      if (alerts.length > 0) openAckDialog(alerts);
    });

    document.body.addEventListener("click", (e) => {
      const ackId = e.target.getAttribute("data-ack");
      if (ackId) {
        const alert = currentItems.find((a) => a.id === Number(ackId));
        if (alert) openAckDialog([alert]);
        return;
      }

      const row = e.target.closest(".alert-item");
      if (row && !e.target.closest(".alert-item-select")) {
        selectedAlertId = Number(row.getAttribute("data-alert-id"));
        renderDetail();
      }
    });

    State.on("alert-created", () => load());
    State.on("alert-acknowledged", () => load());

    load();
  }

  return { init, render: load };
})();
