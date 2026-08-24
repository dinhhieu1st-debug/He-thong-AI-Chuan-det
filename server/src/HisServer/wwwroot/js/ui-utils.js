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
    if (bed.monitoring === false || bed.alertsArmed === false) return "";
    // New firmware separates severity from cause. Prefer the explicit branch
    // flags; alertLevel is kept only as a legacy fallback.
    const hasBranches = bed.lineBranch || bed.patientBranch;
    if (!hasBranches && (bed.alertLevel == null || bed.alertLevel === 0)) return "";

    const both = bed.lineBranch && bed.patientBranch;
    const patient = bed.patientBranch || (!hasBranches && bed.alertLevel === 2);
    const bothCritical = both && (bed.finalAlertLevel === 3 || bed.finalAlertLevel == null);
    const spec = both || (!hasBranches && bed.alertLevel === 3)
      ? { cls: bothCritical ? "culprit-critical" : "culprit-line", text: "LINE + PATIENT",
          hint: "Both the infusion line and the patient's vitals are abnormal at the same time — suspected fluid overload." }
      : patient
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

  /* Replaces the browser's native confirm(), which renders as an unstyled
   * "localhost says" dialog outside the console's own theme (so it stayed
   * bright white even in dark mode) and blocks the whole tab's JS while
   * open. Returns a Promise<boolean> so callers just `await` it the same
   * way they used to read confirm()'s return value.
   *
   * message may contain "\n\n" to separate paragraphs, matching how the
   * existing confirm() call sites already wrote their text. */
  function confirm(message, opts = {}) {
    const { title = "Confirm", confirmLabel = "Confirm", cancelLabel = "Cancel", danger = false } = opts;
    return new Promise((resolve) => {
      const backdrop = document.createElement("div");
      backdrop.className = "modal-backdrop";
      const body = String(message).split("\n\n")
        .map((p) => `<p>${escapeHtml(p)}</p>`).join("");
      backdrop.innerHTML = `
        <div class="modal-card confirm-card">
          <h3>${escapeHtml(title)}</h3>
          <div class="confirm-body">${body}</div>
          <div class="modal-actions">
            <button type="button" class="btn" id="confirmCancelBtn">${escapeHtml(cancelLabel)}</button>
            <button type="button" class="btn ${danger ? "danger" : "primary"}" id="confirmOkBtn">${escapeHtml(confirmLabel)}</button>
          </div>
        </div>`;
      document.body.appendChild(backdrop);

      const settle = (result) => {
        document.removeEventListener("keydown", onKeydown);
        backdrop.remove();
        resolve(result);
      };
      const onKeydown = (e) => { if (e.key === "Escape") settle(false); };

      backdrop.querySelector("#confirmCancelBtn").addEventListener("click", () => settle(false));
      backdrop.querySelector("#confirmOkBtn").addEventListener("click", () => settle(true));
      backdrop.addEventListener("click", (e) => { if (e.target === backdrop) settle(false); });
      document.addEventListener("keydown", onKeydown);
      backdrop.querySelector("#confirmOkBtn").focus();
    });
  }

  /* Replaces the browser's native prompt(), same reasoning as confirm()
   * above. Returns a Promise<string|null> - null means cancelled, matching
   * what prompt() itself returns. */
  function prompt(message, opts = {}) {
    const { title = "Enter a value", defaultValue = "", placeholder = "",
            inputType = "text", confirmLabel = "OK" } = opts;
    return new Promise((resolve) => {
      const backdrop = document.createElement("div");
      backdrop.className = "modal-backdrop";
      backdrop.innerHTML = `
        <form class="modal-card confirm-card" id="uiPromptForm">
          <h3>${escapeHtml(title)}</h3>
          <div class="confirm-body"><p>${escapeHtml(message)}</p></div>
          <input type="${escapeHtml(inputType)}" id="uiPromptInput"
                 placeholder="${escapeHtml(placeholder)}" value="${escapeHtml(defaultValue)}">
          <div class="modal-actions">
            <button type="button" class="btn" id="uiPromptCancelBtn">Cancel</button>
            <button type="submit" class="btn primary">${escapeHtml(confirmLabel)}</button>
          </div>
        </form>`;
      document.body.appendChild(backdrop);

      const input = backdrop.querySelector("#uiPromptInput");
      const settle = (result) => {
        document.removeEventListener("keydown", onKeydown);
        backdrop.remove();
        resolve(result);
      };
      const onKeydown = (e) => { if (e.key === "Escape") settle(null); };

      backdrop.querySelector("#uiPromptCancelBtn").addEventListener("click", () => settle(null));
      backdrop.querySelector("#uiPromptForm").addEventListener("submit", (e) => {
        e.preventDefault();
        settle(input.value);
      });
      backdrop.addEventListener("click", (e) => { if (e.target === backdrop) settle(null); });
      document.addEventListener("keydown", onKeydown);
      input.focus();
      input.select();
    });
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

  /* Clinical limits mirrored from the server's VitalsStatusEvaluator
   * (software/server/src/HisServer/Domain/VitalsStatusEvaluator.cs). The server has
   * already decided WARNING/CRITICAL by the time any of this runs - these
   * numbers are used only to WRITE the reason text for a cause the server
   * already flagged (via patientBranch/lineBranch/alertLevel/etc.), never to
   * decide the status itself. If these two files ever drift, the number
   * shown here would be wrong; a change to the real threshold must be made
   * server-side, and this comment is the reminder to update the copy here
   * too. */
  const VITALS_LIMITS = {
    spo2Critical: 90,
    spo2Warning: 95,
    heartRateLow: 60,
    heartRateHigh: 110
  };

  /* Turns one bed's already-decided flags into a list of structured,
   * human-readable causes: { type, severity, sensor, channel, reason,
   * value, threshold?, target?, weight? }.
   *
   * This never decides WARNING/CRITICAL on its own - every branch here is
   * gated on a flag the server already set (patientBranch, lineBranch,
   * lineBlocked, aeAlarm, dripAnomaly, vitalsAnomaly, alertLevel, the
   * *Signal flags). The raw values and VITALS_LIMITS above are used only to
   * explain a cause the gate already confirmed is active - e.g. "the line
   * side is flagged, and dropsPerMin > targetDropsPerMin, so write 'too
   * fast'" - never to invent a new one.
   *
   * Shared by the bed detail page's Active Alert card and (once alertType
   * is enough on its own for alert history rows, see describeAlertType
   * below) kept as the one place this mapping lives, per file, so beds.js
   * and alerts.js cannot drift into two different ideas of what a cause is. */
  function buildAlertCauses(bed) {
    const causes = [];

    // Alert levels are verdicts received from the device/server. The UI only
    // explains those verdicts and never calculates or changes their severity.
    const vitalsLevel = [1, 2, 3].includes(Number(bed.vitalsLevel)) ? Number(bed.vitalsLevel) : null;
    const dripLevel = [1, 2, 3].includes(Number(bed.serverDropLevel)) ? Number(bed.serverDropLevel) : null;
    const patientActive = bed.patientBranch || (vitalsLevel != null && vitalsLevel > 1);
    const lineActive = bed.lineBranch || (dripLevel != null && dripLevel > 1);

    // Once a device reports alertLevel at all (v2 firmware), it has already
    // weighed LineBlocked against the load cell and made its own line
    // verdict - see VitalsStatusEvaluator's deviceJudgedTheLine. Re-reading
    // the raw flag here for that device would resurrect exactly the false
    // "bag running low = blocked line" alarm the load cell exists to remove.
    const deviceJudgedLine = bed.alertLevel != null;

    if (patientActive && bed.spo2Low && bed.spo2Signal) {
      causes.push({
        type: "patient", level: vitalsLevel, sensor: "MAX30102", channel: "SpO2",
        reason: "SpO2 lower than the accepted baseline",
        value: bed.spo2 != null ? `${bed.spo2}%` : "--"
      });
    }

    if (patientActive && bed.heartRateAbnormal && bed.heartRateSignal) {
      let reason = "Heart rate differs from the learned baseline";
      if (bed.heartRate != null && bed.hrBaselineBpm != null) {
        if (bed.heartRate > bed.hrBaselineBpm) reason = "Heart rate higher than the learned baseline";
        else if (bed.heartRate < bed.hrBaselineBpm) reason = "Heart rate lower than the learned baseline";
      }
      causes.push({
        type: "patient", level: vitalsLevel, sensor: "MAX30102", channel: "Heart rate", reason,
        value: bed.heartRate != null ? `${bed.heartRate} bpm` : "--",
        baseline: bed.hrBaselineBpm != null ? `${bed.hrBaselineBpm} bpm` : null
      });
    }

    if (patientActive && !bed.spo2Low && !bed.heartRateAbnormal) {
      causes.push({
        type: "patient", level: vitalsLevel, sensor: "MAX30102", channel: "HR / SpO2",
        reason: "Vitals differ from the learned baseline", value: "--"
      });
    }

    // Infusion line / drop side. dropsPerMin vs targetDropsPerMin only WRITES
    // "too fast"/"too slow" - it is never what decides this branch fires.
    if (lineActive || (bed.lineBlocked && !deviceJudgedLine)) {
      const measured = bed.dropsPerMin;
      const target = bed.targetDropsPerMin;
      let reason = "Infusion line needs checking";
      if (measured != null && target != null && target > 0 && measured !== target) {
        reason = measured > target ? "Drop rate too fast" : "Drop rate too slow";
      }
      if (bed.lineState === 2) reason = "Occlusion / blocked tube";
      else if (bed.lineState === 3) reason = "Free flow / drops dangerously fast";
      else if (bed.lineState === 5) reason = "IV bag empty";
      else if (bed.lineState === 4) reason = "Photodiode drop sensor fault";
      causes.push({
        type: "line", level: dripLevel,
        sensor: "Photodiode drop sensor",
        channel: "Drop rate", reason,
        value: measured != null ? `${measured} dpm` : "--",
        target: target != null ? `${target} dpm` : "--"
      });
    }

    if (bed.aeAlarm) {
      causes.push({ type: "ai", severity: "warning", sensor: "AI (autoencoder)", channel: "HR + SpO2 combination",
        reason: "Abnormal HR/SpO2 combination detected", value: "--" });
    }
    if (bed.dripAnomaly) {
      causes.push({ type: "ai", severity: "warning", sensor: "AI (drip forecaster)", channel: "Drop rate",
        reason: "Infusion flow changing unexpectedly", value: "--" });
    }
    if (bed.vitalsAnomaly) {
      causes.push({ type: "ai", severity: "warning", sensor: "AI (vitals forecaster)", channel: "HR/SpO2 trend",
        reason: "Vitals changing unexpectedly", value: "--" });
    }

    // Signal loss is only its own listed cause for a device too old to
    // report alertLevel - see VitalsStatusEvaluator.HasLostSignal, which is
    // only read on that same fallback path. Newer firmware still shows lost
    // signal in Sensor channels, it just does not count as an alert cause.
    if (!deviceJudgedLine) {
      const lost = [];
      if (!bed.heartRateSignal) lost.push("Heart rate");
      if (!bed.spo2Signal) lost.push("SpO2");
      if (!bed.flowSignal) lost.push("Flow");
      if (!bed.dripRateSignal) lost.push("Drip");
      if (lost.length > 0) {
        causes.push({ type: "sensor", severity: "warning", sensor: "Sensor", channel: lost.join(", "),
          reason: "No signal", value: "--" });
      }
    }

    return causes;
  }

  // What each alert-history row's alertType code means, for display only -
  // alert history (AlertDto) does not snapshot dropsPerMin/lineState/branch
  // flags the way a live bed does, so this is deliberately smaller than
  // buildAlertCauses above: it labels the SOURCE of a past alert from the
  // type code alone, it does not reconstruct full cause detail that was
  // never stored.
  const ALERT_TYPE_INFO = {
    SPO2_LOW_CRITICAL: { source: "Patient vitals", sensor: "MAX30102", channel: "SpO2" },
    SPO2_LOW: { source: "Patient vitals", sensor: "MAX30102", channel: "SpO2" },
    HEART_RATE_ABNORMAL: { source: "Patient vitals", sensor: "MAX30102", channel: "Heart rate" },
    PATIENT_DETERIORATING: { source: "Patient vitals", sensor: "MAX30102", channel: "HR + SpO2" },
    LINE_FAULT: { source: "Infusion line", sensor: "Photodiode drop sensor", channel: "Drop rate" },
    LINE_BLOCKED: { source: "Infusion line", sensor: "Photodiode drop sensor", channel: "Drop rate" },
    FLUID_OVERLOAD_SUSPECTED: { source: "Infusion line + Patient vitals", sensor: "Photodiode + MAX30102", channel: "Combined" },
    AE_ALARM: { source: "AI model", sensor: "AI (autoencoder)", channel: "HR + SpO2 combination" },
    DRIP_MODEL_ANOMALY: { source: "AI model", sensor: "AI (drip forecaster)", channel: "Drop rate" },
    VITALS_MODEL_ANOMALY: { source: "AI model", sensor: "AI (vitals forecaster)", channel: "HR/SpO2 trend" },
    SENSOR_DISCONNECTED: { source: "Sensor", sensor: "Sensor", channel: "Signal" },
    CRITICAL: { source: "XG26 device", sensor: "On-chip fusion", channel: "Combined" },
    WARNING: { source: "XG26 device", sensor: "On-chip fusion", channel: "Combined" },
    VITAL_WARNING: { source: "Patient vitals", sensor: "MAX30102", channel: "Combined" }
  };
  function describeAlertType(alertType) {
    return ALERT_TYPE_INFO[alertType] || { source: "Device", sensor: "XG26", channel: "—" };
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
    statusColor, culpritBadgeHtml, escapeHtml, formatDateTime, formatMetric, signalRowHtml, toast, confirm, prompt,
    getToastHistory, getUnseenErrorCount, markToastHistorySeen,
    severityCompare, loadFilterState, saveFilterState,
    VITALS_LIMITS, buildAlertCauses, describeAlertType
  };
})();
