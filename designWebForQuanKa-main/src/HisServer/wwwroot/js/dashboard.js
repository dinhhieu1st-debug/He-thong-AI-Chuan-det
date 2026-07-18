const DashboardTab = (() => {
  const recentAlerts = [];
  const MAX_RECENT_ALERTS = 3;

  function bedCardHtml(bed) {
    const color = UiUtils.statusColor(bed.status);
    return `
      <div class="bed-card">
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
          ${bed.alertMessage ? `<div class="bed-message">${UiUtils.escapeHtml(bed.alertMessage)}</div>` : ""}
          <div class="bed-updated">Updated ${UiUtils.formatDateTime(bed.lastUpdated)}</div>
        </div>
      </div>`;
  }

  function alertRowHtml(alert) {
    const color = UiUtils.statusColor(alert.level);
    return `
      <div class="alert-item">
        <div class="stripe" style="background:${color};"></div>
        <div>
          <div class="title" style="color:${color};">${UiUtils.escapeHtml(alert.level)} · ${UiUtils.escapeHtml(alert.bedId)}</div>
          <div class="desc">${UiUtils.escapeHtml(alert.message)}</div>
          <div class="time">${UiUtils.escapeHtml(alert.room || "")} · ${UiUtils.formatDateTime(alert.createdAt)}</div>
        </div>
      </div>`;
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
