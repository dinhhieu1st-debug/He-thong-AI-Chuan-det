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
  let searchTerm = "";
  let channelFilter = "all";

  function matchesFilters(r) {
    if (channelFilter !== "all" && r.channel !== channelFilter) return false;
    if (searchTerm) {
      const haystack = `${r.bedId} ${r.deviceId || ""} ${r.note || ""}`.toLowerCase();
      if (!haystack.includes(searchTerm.toLowerCase())) return false;
    }
    return true;
  }

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

    const visible = reports.filter(matchesFilters);

    host.innerHTML = `
      <div class="toolbar">
        <input class="search-input" id="faultSearch" placeholder="Search bed, device or note..." value="${UiUtils.escapeHtml(searchTerm)}">
        <button class="chip ${showResolved ? "" : "active"}" data-filter-open>Open</button>
        <button class="chip ${showResolved ? "active" : ""}" data-filter-all>Including resolved</button>
      </div>
      <div class="chip-row">
        <button class="chip ${channelFilter === "all" ? "active" : ""}" data-channel="all">All parts</button>
        ${Object.entries(CHANNEL_LABEL).map(([key, label]) =>
          `<button class="chip ${channelFilter === key ? "active" : ""}" data-channel="${key}">${UiUtils.escapeHtml(label)}</button>`
        ).join("")}
      </div>
      <span class="muted">Showing ${visible.length} of ${reports.length}</span>
      ${reports.length === 0
        ? `<div class="empty-state">Nothing reported. Nurses raise these from the bed detail screen.</div>`
        : visible.length === 0
          ? `<div class="empty-state">No fault reports match the current filters.</div>`
          : `<table class="data-table">
             <thead><tr>
               <th>Bed / device</th><th>Part</th><th>Reported problem</th>
               <th>Reported by</th><th>Status</th><th></th>
             </tr></thead>
             <tbody>${visible.map(rowHtml).join("")}</tbody>
           </table>`}`;

    host.querySelector("#faultSearch")?.addEventListener("input", (e) => {
      searchTerm = e.target.value;
      render();
      // render() just rebuilt the whole toolbar from a string, which drops
      // focus from under the caret - put it back so typing isn't interrupted
      // every keystroke.
      const input = document.getElementById("faultSearch");
      if (input) { input.focus(); input.setSelectionRange(input.value.length, input.value.length); }
    });

    host.querySelectorAll("[data-channel]").forEach((btn) => {
      btn.addEventListener("click", () => {
        channelFilter = btn.getAttribute("data-channel");
        render();
      });
    });

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
