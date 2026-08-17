/* The technician's work queue: equipment faults reported from the bedside.
 *
 * Deliberately shows the bed number, the channel and the nurse's own words —
 * and no vitals. What a technician needs is "which box, which part, who said
 * so"; a heart rate would not change what they put in the toolbox. */
const FaultsTab = (() => {
  const CHANNEL_LABEL = {
    HR: "Heart rate sensor",
    SPO2: "SpO2 sensor",
    FLOW: "Load cell / scale",
    DROPS: "Drop sensor",
    OTHER: "Other"
  };

  const STATUS_LABEL = {
    OPEN: "Open",
    IN_PROGRESS: "In progress",
    RESOLVED: "Resolved"
  };

  let reports = [];
  let showResolved = false;
  let error = null;

  async function load() {
    try {
      reports = await Api.getFaultReports(!showResolved);
      error = null;
    } catch (err) {
      error = err.message;
    }
    render();
    updateBadge();
  }

  /* The sidebar badge is the whole point of the queue for a technician who is
   * not looking at this tab: it is how they learn a report exists at all. */
  function updateBadge() {
    const badge = document.getElementById("faultBadge");
    if (!badge) return;
    const open = reports.filter((r) => r.status !== "RESOLVED").length;
    badge.textContent = open;
    badge.style.display = open > 0 ? "" : "none";
  }

  function rowHtml(r) {
    const canClaim = r.status === "OPEN";
    const canResolve = r.status !== "RESOLVED";

    return `
      <tr>
        <td>
          <b>${UiUtils.escapeHtml(r.bedId)}</b>
          <div class="muted">${UiUtils.escapeHtml(r.deviceId || "no device assigned")}</div>
        </td>
        <td>${UiUtils.escapeHtml(CHANNEL_LABEL[r.channel] || r.channel)}</td>
        <td>${UiUtils.escapeHtml(r.note || "—")}</td>
        <td>
          ${UiUtils.escapeHtml(r.reportedBy)}
          <div class="muted">${UiUtils.formatDateTime(r.reportedAt)}</div>
        </td>
        <td>
          <span class="fault-status fault-${r.status.toLowerCase()}">${STATUS_LABEL[r.status] || r.status}</span>
          ${r.handledBy ? `<div class="muted">${UiUtils.escapeHtml(r.handledBy)}</div>` : ""}
          ${r.resolutionNote ? `<div class="muted">${UiUtils.escapeHtml(r.resolutionNote)}</div>` : ""}
        </td>
        <td style="text-align:right; white-space:nowrap;">
          ${canClaim ? `<button class="btn" data-claim="${r.reportId}">Take it</button>` : ""}
          ${canResolve ? `<button class="btn primary" data-resolve="${r.reportId}">Resolve</button>` : ""}
        </td>
      </tr>`;
  }

  function render() {
    const host = document.getElementById("tab-faults");
    if (!host) return;

    if (error) {
      host.innerHTML = `<div class="empty-state">Could not load fault reports: ${UiUtils.escapeHtml(error)}</div>`;
      return;
    }

    host.innerHTML = `
      <div class="toolbar">
        <button class="chip ${showResolved ? "" : "active"}" data-filter-open>Open</button>
        <button class="chip ${showResolved ? "active" : ""}" data-filter-all>Including resolved</button>
      </div>
      ${reports.length === 0
        ? `<div class="empty-state">Nothing reported. Nurses raise these from the bed detail screen.</div>`
        : `<table class="data-table">
             <thead><tr>
               <th>Bed / device</th><th>Part</th><th>Reported problem</th>
               <th>Reported by</th><th>Status</th><th></th>
             </tr></thead>
             <tbody>${reports.map(rowHtml).join("")}</tbody>
           </table>`}`;

    host.querySelector("[data-filter-open]")?.addEventListener("click", () => {
      showResolved = false; load();
    });
    host.querySelector("[data-filter-all]")?.addEventListener("click", () => {
      showResolved = true; load();
    });

    host.querySelectorAll("[data-claim]").forEach((btn) => {
      btn.addEventListener("click", async () => {
        await act(() => Api.claimFaultReport(btn.getAttribute("data-claim")), "Assigned to you");
      });
    });

    host.querySelectorAll("[data-resolve]").forEach((btn) => {
      btn.addEventListener("click", async () => {
        const note = prompt("What fixed it? (optional)");
        if (note === null) return;   // cancelled, as opposed to left blank
        await act(() => Api.resolveFaultReport(btn.getAttribute("data-resolve"), note),
                  "Marked as resolved");
      });
    });
  }

  async function act(action, message) {
    try {
      await action();
      UiUtils.toast(message);
      await load();
    } catch (err) {
      UiUtils.toast(err.message, true);
    }
  }

  function init() {
    // Pushed live, so a report raised at a bedside appears here without the
    // technician refreshing anything.
    State.on("fault-reported", () => load());
    load();
  }

  function activate() {
    load();
  }

  return { init, activate, render };
})();
