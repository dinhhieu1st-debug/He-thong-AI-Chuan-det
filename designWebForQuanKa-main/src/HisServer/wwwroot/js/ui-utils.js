// Small shared rendering helpers used across every tab module.
const UiUtils = (() => {
  const STATUS_COLOR = {
    STABLE: "#1ea050",
    WARNING: "#e69119",
    CRITICAL: "#dc3c3c",
    OFFLINE: "#8a97a8"
  };

  function statusColor(status) {
    return STATUS_COLOR[(status || "").toUpperCase()] || "#8a97a8";
  }

  function escapeHtml(text) {
    const div = document.createElement("div");
    div.textContent = text == null ? "" : String(text);
    return div.innerHTML;
  }

  function formatDateTime(value) {
    if (!value) return "--";
    const date = new Date(value.endsWith("Z") || value.includes("+") ? value : value + "Z");
    return date.toLocaleString();
  }

  function formatMetric(value, suffix) {
    return value === null || value === undefined ? "--" : `${value}${suffix || ""}`;
  }

  // Small "HR / SpO2 / Flow / Drip / Line" pill row showing per-sensor-channel
  // connectivity (green = signal present, gray = no signal / not wired up) plus
  // the IV line blocked/free-flow flag, so a nurse can see equipment problems
  // at a glance without opening the Alerts tab.
  function signalRowHtml(bed) {
    const channels = [
      ["HR", bed.heartRateSignal],
      ["SpO2", bed.spo2Signal],
      ["Flow", bed.flowSignal],
      ["Drip", bed.dripRateSignal],
      ["Line", !bed.lineBlocked]
    ];
    return `<div class="signal-row">${channels.map(([label, ok]) =>
      `<span class="signal-dot ${ok ? "ok" : "lost"}" title="${label}: ${ok ? (label === "Line" ? "Line OK" : "Signal OK") : (label === "Line" ? "Blocked / free-flow" : "No signal")}">${label}</span>`
    ).join("")}</div>`;
  }

  return { statusColor, escapeHtml, formatDateTime, formatMetric, signalRowHtml };
})();
