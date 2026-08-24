/* Bell icon in the topbar: everything UiUtils.toast() has shown this session,
 * because a toast disappears after 4s and a message glimpsed while looking at
 * a patient is easy to miss entirely. See ui-utils.js for the history buffer
 * itself - this module is just the button + dropdown reading it. */
const NotificationHistory = (() => {
  function rowHtml(entry) {
    const time = entry.at.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
    return `
      <div class="notification-row${entry.isError ? " is-error" : ""}">
        <div>${UiUtils.escapeHtml(entry.message)}</div>
        <div class="nr-time">${time}</div>
      </div>`;
  }

  function renderDot() {
    const dot = document.getElementById("notificationDot");
    const count = UiUtils.getUnseenErrorCount();
    dot.style.display = count > 0 ? "block" : "none";
  }

  function renderPanel() {
    const panel = document.getElementById("notificationPanel");
    const history = UiUtils.getToastHistory();
    panel.innerHTML = `
      <div class="notification-panel-head">Notifications</div>
      ${history.length === 0
        ? `<div class="notification-row muted">Nothing yet this session.</div>`
        : history.map(rowHtml).join("")}`;
  }

  function togglePanel(show) {
    const panel = document.getElementById("notificationPanel");
    const isOpen = panel.style.display !== "none";
    const next = show === undefined ? !isOpen : show;
    if (next) {
      renderPanel();
      UiUtils.markToastHistorySeen();
    }
    panel.style.display = next ? "block" : "none";
  }

  function init() {
    document.getElementById("notificationBellBtn").addEventListener("click", (e) => {
      e.stopPropagation();
      togglePanel();
    });
    document.addEventListener("click", (e) => {
      const panel = document.getElementById("notificationPanel");
      if (panel.style.display !== "none" && !panel.contains(e.target)) togglePanel(false);
    });
    document.addEventListener("toast-history-changed", renderDot);
    renderDot();
  }

  return { init };
})();
