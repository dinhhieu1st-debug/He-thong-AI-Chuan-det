const AlertsTab = (() => {
  let filter = "all";
  let searchTerm = "";
  let page = 1;
  const pageSize = 80;
  let currentItems = [];
  let totalCount = 0;
  let selectedAlertId = null;

  function queryParamsForFilter() {
    switch (filter) {
      case "critical": return { level: "Critical" };
      case "warning": return { level: "Warning" };
      case "unacked": return { ack: "false" };
      case "acked": return { ack: "true" };
      default: return {};
    }
  }

  async function load() {
    const params = { ...queryParamsForFilter(), page: String(page), pageSize: String(pageSize) };
    const result = await Api.getAlerts(params);
    currentItems = result.items;
    totalCount = result.totalCount;
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

  function alertRowHtml(alert) {
    const color = UiUtils.statusColor(alert.level);
    return `
      <div class="alert-item" data-alert-id="${alert.id}">
        <div class="stripe" style="background:${color};"></div>
        <div>
          <div class="title" style="color:${color};">${UiUtils.escapeHtml(alert.level)} · ${UiUtils.escapeHtml(alert.bedId)}</div>
          <div class="desc">${UiUtils.escapeHtml(alert.message)}</div>
          <div class="time">${UiUtils.escapeHtml(alert.room || "")} · ${UiUtils.formatDateTime(alert.createdAt)}</div>
        </div>
        <div class="actions">
          ${alert.acknowledged
            ? `<span class="chip">Acknowledged</span>`
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
    panel.style.display = "block";
    panel.innerHTML = `
      <h3><span class="status-chip" style="background:${color};">${UiUtils.escapeHtml(alert.level)}</span></h3>
      <div class="sub">${UiUtils.escapeHtml(alert.bedId)} · ${UiUtils.escapeHtml(alert.room || "")}</div>
      <div class="bed-message">${UiUtils.escapeHtml(alert.message)}</div>
      <div class="metric-row">
        <div class="metric"><b>${UiUtils.formatMetric(alert.spo2, "%")}</b><span>SpO2</span></div>
        <div class="metric"><b>${UiUtils.formatMetric(alert.heartRate, "")}</b><span>Heart Rate</span></div>
        <div class="metric"><b>${UiUtils.formatMetric(alert.dripRate, "%")}</b><span>Drip Rate</span></div>
      </div>
      <div class="bed-updated">Created ${UiUtils.formatDateTime(alert.createdAt)}</div>
      ${alert.acknowledged
        ? `<div class="bed-updated">Acknowledged ${UiUtils.formatDateTime(alert.acknowledgedAt)}</div>`
        : `<button class="btn primary" style="margin-top:10px;" data-ack="${alert.id}">Acknowledge Alert</button>`}
    `;
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

    renderDetail();
  }

  async function acknowledge(id) {
    await Api.ackAlert(id);
  }

  function init() {
    document.getElementById("alertFilters").addEventListener("click", (e) => {
      const f = e.target.getAttribute("data-filter");
      if (f) { filter = f; page = 1; load(); }
    });

    document.getElementById("alertSearch").addEventListener("input", (e) => {
      searchTerm = e.target.value;
      render();
    });

    document.body.addEventListener("click", (e) => {
      const ackId = e.target.getAttribute("data-ack");
      if (ackId) acknowledge(Number(ackId));

      const row = e.target.closest(".alert-item");
      if (row && !e.target.hasAttribute("data-ack")) {
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
