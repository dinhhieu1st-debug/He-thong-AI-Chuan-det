/* Full-screen critical alarm.
 *
 * A red chart in a side panel only works for someone already looking at the
 * right bed on the right tab. When a bed goes Critical the ward needs to know
 * regardless of what is on screen, so this takes over the whole viewport with
 * the hazard triangle, the bed, and every reason the server gave.
 *
 * Two rules keep it from becoming wallpaper that nobody reads:
 *   - it only fires on the TRANSITION into Critical, never on the repeat
 *     readings that follow (one per second over SignalR)
 *   - once dismissed, that bed stays quiet until it actually recovers and
 *     goes Critical again — - a nurse who has acknowledged a blocked line
 *     should not have to dismiss the same banner every second while they walk
 *     over to fix it
 * The bed staying Critical is still visible everywhere else (status chip,
 * alert card, pulsing charts); this banner is only the "look up NOW" signal.
 */
const CriticalAlarm = (() => {
  // Beds whose banner the user has already dismissed, cleared when the bed
  // recovers. Dismissing the banner is NOT the same as acknowledging the
  // alert - see the button labels/titles below.
  const dismissed = new Set();
  // What we last saw, so we can spot the Stable/Warning -> Critical edge.
  const wasCritical = new Set();

  let queue = [];        // beds currently shown in the banner
  let overlay = null;

  function isCritical(bed) {
    return (bed.status || "").toLowerCase() === "critical";
  }

  function causesOf(bed) {
    return String(bed.alertMessage || "Critical condition")
      .split(" · ")
      .filter((c) => c.trim() !== "");
  }

  function hazardIconSvg() {
    // Hazard triangle, drawn inline so it needs no network and scales cleanly.
    return `
      <svg class="ca-icon" viewBox="0 0 24 24" role="img" aria-label="Danger">
        <path d="M12 2.2 1.3 20.8h21.4L12 2.2Z" fill="currentColor"/>
        <path d="M12 8.6v5.2" stroke="#fff" stroke-width="2.1" stroke-linecap="round"/>
        <circle cx="12" cy="17.4" r="1.25" fill="#fff"/>
      </svg>`;
  }

  function bedBlockHtml(bed) {
    return `
      <div class="ca-bed">
        <div class="ca-bed-head">
          <span class="ca-bed-id">${UiUtils.escapeHtml(bed.bedId)}</span>
          <span class="ca-bed-room">${UiUtils.escapeHtml(bed.room || "")}</span>
          ${bed.patientName ? `<span class="ca-patient-name">${UiUtils.escapeHtml(bed.patientName)}</span>` : ""}
        </div>
        <ul class="ca-causes">
          ${causesOf(bed).map((c) => `<li>${UiUtils.escapeHtml(c)}</li>`).join("")}
        </ul>
        <div class="ca-vitals">
          <span><b>${UiUtils.formatMetric(bed.spo2, "%")}</b> SpO2</span>
          <span><b>${UiUtils.formatMetric(bed.heartRate, "")}</b> bpm</span>
          <span><b>${UiUtils.formatMetric(bed.dripRate, "%")}</b> drip</span>
        </div>
        <div class="ca-actions">
          <button type="button" class="btn ca-open" data-bed-id="${UiUtils.escapeHtml(bed.bedId)}">
            Open ${UiUtils.escapeHtml(bed.bedId)}
          </button>
          <button type="button" class="btn ca-ack" data-bed-id="${UiUtils.escapeHtml(bed.bedId)}"
                  title="Hides this full-screen banner only - the alert stays unacknowledged in the Alerts tab until you acknowledge it there.">
            Dismiss banner
          </button>
        </div>
      </div>`;
  }

  function render() {
    overlay = overlay || document.getElementById("criticalAlarm");
    if (!overlay) return;

    if (queue.length === 0) {
      overlay.classList.add("hidden");
      overlay.innerHTML = "";
      return;
    }

    const plural = queue.length > 1;
    overlay.innerHTML = `
      <div class="ca-panel" role="alertdialog" aria-modal="true" aria-labelledby="caTitle">
        <div class="ca-head">
          ${hazardIconSvg()}
          <div>
            <div class="ca-title" id="caTitle">CRITICAL${plural ? ` · ${queue.length} beds` : ""}</div>
            <div class="ca-sub">Immediate attention required</div>
          </div>
        </div>
        <div class="ca-body">
          ${queue.map(bedBlockHtml).join("")}
        </div>
        <div class="ca-foot">
          <button type="button" class="btn ca-ack-all"
                  title="Hides these banners only - the alerts still need acknowledging in the Alerts tab.">
            Dismiss all banners
          </button>
        </div>
      </div>`;
    overlay.classList.remove("hidden");

    overlay.querySelectorAll(".ca-open").forEach((btn) => {
      btn.addEventListener("click", () => {
        const bedId = btn.getAttribute("data-bed-id");
        dismiss(bedId);
        document.querySelector('.nav-btn[data-tab="beds"]').click();
        BedsTab.openBed(bedId);
      });
    });

    overlay.querySelectorAll(".ca-ack").forEach((btn) => {
      btn.addEventListener("click", () => dismiss(btn.getAttribute("data-bed-id")));
    });

    overlay.querySelector(".ca-ack-all").addEventListener("click", () => {
      queue.slice().forEach((bed) => dismiss(bed.bedId));
    });
  }

  function dismiss(bedId) {
    dismissed.add(bedId);
    queue = queue.filter((b) => b.bedId !== bedId);
    render();
  }

  function onBedsChanged() {
    const beds = Array.from(State.beds.values());
    let changed = false;

    beds.forEach((bed) => {
      const critical = isCritical(bed);

      if (!critical) {
        // Recovered: forget it, so a future relapse alarms again.
        if (wasCritical.delete(bed.bedId)) changed = true;
        dismissed.delete(bed.bedId);
        if (queue.some((b) => b.bedId === bed.bedId)) {
          queue = queue.filter((b) => b.bedId !== bed.bedId);
          changed = true;
        }
        return;
      }

      if (!wasCritical.has(bed.bedId)) {
        wasCritical.add(bed.bedId);
        if (!dismissed.has(bed.bedId)) {
          queue.push(bed);
          changed = true;
        }
      } else {
        // Already alarming: refresh the figures shown without re-opening.
        const idx = queue.findIndex((b) => b.bedId === bed.bedId);
        if (idx !== -1) {
          queue[idx] = bed;
          changed = true;
        }
      }
    });

    // A bed that disappears entirely should not keep the banner up.
    const known = new Set(beds.map((b) => b.bedId));
    const before = queue.length;
    queue = queue.filter((b) => known.has(b.bedId));
    if (queue.length !== before) changed = true;

    if (changed) render();
  }

  function init() {
    State.on("beds-changed", onBedsChanged);

    // Escape dismisses every banner, so a keyboard user is never trapped -
    // the underlying alerts still need real acknowledgement in the Alerts tab.
    document.addEventListener("keydown", (e) => {
      if (e.key === "Escape" && queue.length > 0) {
        queue.slice().forEach((bed) => dismiss(bed.bedId));
      }
    });

    onBedsChanged();
  }

  return { init };
})();
