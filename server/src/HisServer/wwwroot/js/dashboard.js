const DashboardTab = (() => {
  const recentAlerts = [];
  const MAX_RECENT_ALERTS = 3;

  function bedCardHtml(bed) {
    const color = UiUtils.statusColor(bed.status);
    return `
      <div class="bed-card" data-bed-id="${UiUtils.escapeHtml(bed.bedId)}">
        <div class="stripe" style="background:${color};"></div>
        <div class="body">
          <div class="row-top">
            <div><div class="bed-id">${UiUtils.escapeHtml(bed.bedId)}</div><div class="bed-room">${UiUtils.escapeHtml(bed.room)}</div></div>
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
          ${bed.alertMessage ? `<div class="bed-message">${UiUtils.escapeHtml(bed.alertMessage)}</div>` : ""}
          <div class="bed-updated">Updated ${UiUtils.formatDateTime(bed.lastUpdated)}</div>
        </div>
      </div>`;
  }

  /* The dashboard alert card is deliberately bigger and bolder than the row in
   * the Alerts tab: this is the thing a nurse must catch from across the
   * station, and the old one-line row left most of the width empty. Each cause
   * the server reported gets its own chip so "blocked line + no HR signal"
   * cannot be skim-read as a single problem. */
  function alertRowHtml(alert) {
    const color = UiUtils.statusColor(alert.level);
    const level = (alert.level || "").toUpperCase();
    const causes = String(alert.message || "")
      .split(" · ")
      .filter((part) => part.trim() !== "");

    return `
      <div class="dash-alert sev-${UiUtils.escapeHtml(level.toLowerCase())}"
           data-bed-id="${UiUtils.escapeHtml(alert.bedId)}" style="--alert-color:${color};">
        <div class="dash-alert-head">
          <span class="dash-alert-level">${UiUtils.escapeHtml(level)}</span>
          <span class="dash-alert-bed">${UiUtils.escapeHtml(alert.bedId)}</span>
          <span class="dash-alert-room">${UiUtils.escapeHtml(alert.room || "")}</span>
          <span class="dash-alert-time">${UiUtils.formatDateTime(alert.createdAt)}</span>
        </div>
        <div class="dash-alert-causes">
          ${causes.map((c) => `<span class="dash-alert-cause">${UiUtils.escapeHtml(c)}</span>`).join("")}
        </div>
      </div>`;
  }

  /* Clicking a bed anywhere on the dashboard opens that bed's detail view
   * directly, instead of making the user find it again in the Beds tab. The
   * tab still switches underneath so the back button lands somewhere sensible
   * and the detail's "Back to beds" means what it says. */
  function bindOpenBedHandlers(container) {
    container.querySelectorAll("[data-bed-id]").forEach((el) => {
      el.addEventListener("click", () => {
        const bedId = el.getAttribute("data-bed-id");
        if (!bedId) return;
        document.querySelector('.nav-btn[data-tab="beds"]').click();
        BedsTab.openBed(bedId);
      });
    });
  }

  function render() {
    const beds = Array.from(State.beds.values()).sort((a, b) => a.bedId.localeCompare(b.bedId));
    const total = beds.length;
    const critical = beds.filter((b) => b.status === "Critical").length;
    const warning = beds.filter((b) => b.status === "Warning").length;
    const stable = beds.filter((b) => b.status === "Stable").length;
    const offline = beds.filter((b) => b.status === "Offline").length;

    document.getElementById("statTotal").textContent = total;
    document.getElementById("statCritical").textContent = critical;
    document.getElementById("statWarning").textContent = warning;
    document.getElementById("statStable").textContent = stable;
    document.getElementById("statOffline").textContent = offline;

    const grid = document.getElementById("dashboardGrid");
    grid.innerHTML = total === 0
      ? `<div class="empty-state">No bed data received yet.</div>`
      : beds.map(bedCardHtml).join("");

    const alertsBox = document.getElementById("dashboardAlerts");
    alertsBox.innerHTML = recentAlerts.length === 0
      ? `<div class="empty-state">No recent alerts.</div>`
      : recentAlerts.map(alertRowHtml).join("");

    bindOpenBedHandlers(grid);
    bindOpenBedHandlers(alertsBox);

    const connText = document.getElementById("connText");
    const connDot = document.getElementById("connDot");
    if (total === 0) {
      connText.textContent = "No data yet";
      connDot.classList.add("stale");
    } else {
      connText.textContent = `${total} bed(s) monitored`;
      connDot.classList.remove("stale");
    }
  }

  async function loadRecentAlerts() {
    try {
      const result = await Api.getAlerts({ ack: "false", page: "1", pageSize: String(MAX_RECENT_ALERTS) });
      recentAlerts.splice(0, recentAlerts.length, ...result.items);
      render();
    } catch (err) {
      console.error("Failed to load recent alerts:", err);
    }
  }

  function init() {
    State.on("beds-changed", render);
    State.on("alert-created", (alert) => {
      recentAlerts.unshift(alert);
      recentAlerts.length = Math.min(recentAlerts.length, MAX_RECENT_ALERTS);
      render();
    });
    State.on("connection-status", (status) => {
      const connDot = document.getElementById("connDot");
      connDot.classList.toggle("stale", status !== "connected");
    });
    loadRecentAlerts();
    render();
  }

  return { init, render };
})();
