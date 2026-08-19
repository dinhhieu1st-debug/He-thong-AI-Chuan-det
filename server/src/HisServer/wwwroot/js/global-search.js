/* One search box in the topbar, jumping straight to a bed or device instead
 * of making the user find the right tab first. No backend search endpoint -
 * matches against what the role already has loaded client-side
 * (State.beds / State.devices), which is everything at ward scale. */
const GlobalSearch = (() => {
  const MAX_RESULTS = 8;

  function matches(haystack, term) {
    return haystack.toLowerCase().includes(term);
  }

  function bedResults(term) {
    return Array.from(State.beds.values())
      .filter((b) => matches(`${b.bedId} ${b.room || ""}`, term))
      .slice(0, MAX_RESULTS)
      .map((b) => ({
        kind: "bed",
        id: b.bedId,
        title: b.bedId,
        sub: b.room || "",
        open: () => {
          document.querySelector('.nav-btn[data-tab="beds"]').click();
          BedsTab.openBed(b.bedId);
        }
      }));
  }

  function deviceResults(term) {
    if (!Session.can(Session.CAP.manageDevices)) return [];
    return Array.from(State.devices.values())
      .filter((d) => matches(`${d.deviceId} ${d.assignedBedId || ""}`, term))
      .slice(0, MAX_RESULTS)
      .map((d) => ({
        kind: "device",
        id: d.deviceId,
        title: d.deviceId,
        sub: d.assignedBedId ? `Assigned to ${d.assignedBedId}` : "Unassigned",
        open: () => {
          document.querySelector('.nav-btn[data-tab="devices"]').click();
          DevicesTab.openDevice?.(d.deviceId);
        }
      }));
  }

  function resultRowHtml(r, i) {
    return `
      <div class="global-search-result" data-result-index="${i}">
        <div>${UiUtils.escapeHtml(r.title)}</div>
        <div class="gsr-sub">${UiUtils.escapeHtml(r.kind === "bed" ? "Bed" : "Device")} · ${UiUtils.escapeHtml(r.sub)}</div>
      </div>`;
  }

  let results = [];

  function render(term) {
    const box = document.getElementById("globalSearchResults");
    if (term.length < 2) {
      box.style.display = "none";
      results = [];
      return;
    }

    results = [...bedResults(term), ...deviceResults(term)];
    box.style.display = "block";
    box.innerHTML = results.length === 0
      ? `<div class="global-search-empty">No match for "${UiUtils.escapeHtml(term)}"</div>`
      : results.map(resultRowHtml).join("");

    box.querySelectorAll("[data-result-index]").forEach((el) => {
      el.addEventListener("click", () => {
        const r = results[Number(el.getAttribute("data-result-index"))];
        r?.open();
        close();
      });
    });
  }

  function close() {
    const input = document.getElementById("globalSearchInput");
    const box = document.getElementById("globalSearchResults");
    input.value = "";
    box.style.display = "none";
    results = [];
  }

  function init() {
    const wrap = document.getElementById("globalSearchWrap");
    if (!wrap) return;

    // Nothing to search for a role with neither capability (Admin).
    if (!Session.can(Session.CAP.viewWard) && !Session.can(Session.CAP.viewBedDirectory)) {
      wrap.remove();
      return;
    }

    const input = document.getElementById("globalSearchInput");
    input.addEventListener("input", (e) => render(e.target.value.trim()));
    input.addEventListener("keydown", (e) => { if (e.key === "Escape") close(); });
    document.addEventListener("click", (e) => { if (!wrap.contains(e.target)) close(); });
  }

  return { init };
})();
