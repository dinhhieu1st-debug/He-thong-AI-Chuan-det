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

  /* AI v2: WHICH SIDE is at fault.
   *
   * This is the single most useful thing on the card, and it is what v1 could
   * not say. One network consumed vitals and drip data together and emitted one
   * anomaly score, so "something is wrong" was the most it could ever manage.
   * Three separate models give three attributable signals, and a nurse reading
   * this badge knows whether to bring a new giving set or to go to the bedside.
   *
   * Renders nothing at all for a bed that is fine, and nothing for a device too
   * old to report it - an empty badge is better than a confident wrong one. */
  function culpritBadgeHtml(bed) {
    if (bed.alertLevel == null || bed.alertLevel === 0) return "";

    const spec = bed.alertLevel === 3
      ? { cls: "culprit-critical", text: "LINE + PATIENT",
          hint: "Both the infusion line and the patient's vitals are abnormal at the same time — suspected fluid overload." }
      : bed.alertLevel === 2
      ? { cls: "culprit-patient", text: "PATIENT",
          hint: "The patient's vitals are abnormal. The infusion line is behaving normally." }
      : { cls: "culprit-line", text: "IV LINE",
          hint: "The infusion line needs checking. The patient's vitals are normal." };

    return `<span class="culprit-badge ${spec.cls}" title="${UiUtils.escapeHtml(spec.hint)}">${spec.text}</span>`;
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

  // Every toast shown this session, newest first - a toast itself vanishes
  // after 4s, and a message glimpsed out of the corner of an eye while looking
  // at a patient is easy to miss entirely. The bell icon in the topbar reads
  // this. Session-only (no localStorage/backend): this is a "what just
  // happened" scratchpad, not an audit trail - System Log already is that.
  const MAX_TOAST_HISTORY = 30;
  const toastHistory = [];
  let unseenErrorCount = 0;

  function toastHistoryChanged() {
    document.dispatchEvent(new CustomEvent("toast-history-changed"));
  }

  // Small dismissable notification for the result of an action button (e.g.
  // "target flow rate set", "tare command sent"). Falls back to console.log
  // if the toast container isn't present in the page yet.
  function toast(message, isError) {
    toastHistory.unshift({ message, isError: !!isError, at: new Date() });
    toastHistory.length = Math.min(toastHistory.length, MAX_TOAST_HISTORY);
    if (isError) unseenErrorCount += 1;
    toastHistoryChanged();

    const container = document.getElementById("toastContainer");
    if (!container) {
      console.log(`[toast] ${message}`);
      return;
    }

    const el = document.createElement("div");
    el.className = `toast ${isError ? "error" : ""}`;
    el.textContent = message;
    container.appendChild(el);

    setTimeout(() => el.classList.add("visible"), 10);
    setTimeout(() => {
      el.classList.remove("visible");
      setTimeout(() => el.remove(), 300);
    }, 4000);
  }

  function getToastHistory() {
    return toastHistory;
  }

  function getUnseenErrorCount() {
    return unseenErrorCount;
  }

  function markToastHistorySeen() {
    unseenErrorCount = 0;
    toastHistoryChanged();
  }

  // Critical > Warning > Stable > Offline, so a busy tab shows what needs
  // attention first instead of burying it alphabetically.
  const STATUS_RANK = { Critical: 0, Warning: 1, Stable: 2, Offline: 3 };
  function severityCompare(a, b) {
    const ra = STATUS_RANK[a] ?? 4, rb = STATUS_RANK[b] ?? 4;
    return ra - rb;
  }

  // Per-tab filter state persisted across reloads. Namespaced under
  // "filters:" so it never collides with the "theme" key or anything else
  // main.js/login.js keep in localStorage.
  function loadFilterState(key, defaults) {
    try {
      return { ...defaults, ...JSON.parse(localStorage.getItem(`filters:${key}`) || "{}") };
    } catch {
      return { ...defaults };
    }
  }

  function saveFilterState(key, state) {
    localStorage.setItem(`filters:${key}`, JSON.stringify(state));
  }

  return {
    statusColor, culpritBadgeHtml, escapeHtml, formatDateTime, formatMetric, signalRowHtml, toast,
    getToastHistory, getUnseenErrorCount, markToastHistorySeen,
    severityCompare, loadFilterState, saveFilterState
  };
})();
