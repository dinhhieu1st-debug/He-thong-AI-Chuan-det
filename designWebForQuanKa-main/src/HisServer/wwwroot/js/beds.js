const BedsTab = (() => {
  let searchTerm = "";
  let selectedRoom = "all";
  let selectedStatus = "all";
  let selectedBedId = null;

  const STATUS_FILTERS = ["all", "Stable", "Warning", "Critical", "Offline"];

  function matchesFilters(bed) {
    if (selectedRoom !== "all" && bed.room !== selectedRoom) return false;
    if (selectedStatus !== "all" && bed.status !== selectedStatus) return false;
    if (searchTerm) {
      const haystack = `${bed.bedId} ${bed.room}`.toLowerCase();
      if (!haystack.includes(searchTerm.toLowerCase())) return false;
    }
    return true;
  }

  function bedCardHtml(bed) {
    const color = UiUtils.statusColor(bed.status);
    return `
      <div class="bed-card" data-bed-id="${UiUtils.escapeHtml(bed.bedId)}">
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
          <div class="bed-updated">Updated ${UiUtils.formatDateTime(bed.lastUpdated)}</div>
        </div>
      </div>`;
  }

  function renderFilters() {
    const rooms = ["all", ...new Set(Array.from(State.beds.values()).map((b) => b.room).filter(Boolean))];
    document.getElementById("bedRoomFilters").innerHTML = rooms.map((room) =>
      `<button class="chip ${room === selectedRoom ? "active" : ""}" data-room="${UiUtils.escapeHtml(room)}">${room === "all" ? "All rooms" : UiUtils.escapeHtml(room)}</button>`
    ).join("");

    document.getElementById("bedStatusFilters").innerHTML = STATUS_FILTERS.map((status) =>
      `<button class="chip ${status === selectedStatus ? "active" : ""}" data-status="${status}">${status === "all" ? "All statuses" : status}</button>`
    ).join("");
  }

  function renderDetail() {
    const panel = document.getElementById("bedDetailPanel");
    const bed = selectedBedId ? State.beds.get(selectedBedId) : null;
    if (!bed) {
      panel.style.display = "none";
      return;
    }

    panel.style.display = "block";
    panel.innerHTML = `
      <h3>${UiUtils.escapeHtml(bed.bedId)}</h3>
      <div class="sub">${UiUtils.escapeHtml(bed.room)} · ${UiUtils.escapeHtml((bed.status || "").toUpperCase())}</div>
      <div class="metric-row">
        <div class="metric"><b>${UiUtils.formatMetric(bed.spo2, "%")}</b><span>SpO2</span></div>
        <div class="metric"><b>${UiUtils.formatMetric(bed.heartRate, "")}</b><span>Heart Rate</span></div>
        <div class="metric"><b>${UiUtils.formatMetric(bed.flowRate, "%")}</b><span>Flow Rate</span></div>
        <div class="metric"><b>${UiUtils.formatMetric(bed.dripRate, "%")}</b><span>Drip Rate</span></div>
      </div>
      ${UiUtils.signalRowHtml(bed)}
      ${bed.alertMessage ? `<div class="bed-message">${UiUtils.escapeHtml(bed.alertMessage)}</div>` : ""}
      <form id="editBedForm">
        <label>Room</label>
        <input type="text" id="editBedRoom" value="${UiUtils.escapeHtml(bed.room)}">
        <button type="submit" class="btn primary">Save</button>
      </form>`;

    document.getElementById("editBedForm").addEventListener("submit", async (e) => {
      e.preventDefault();
      const room = document.getElementById("editBedRoom").value.trim();
      await Api.updateBed(bed.bedId, { room });
    });
  }

  function render() {
    renderFilters();
    const beds = Array.from(State.beds.values()).filter(matchesFilters).sort((a, b) => a.bedId.localeCompare(b.bedId));
    const grid = document.getElementById("bedsGrid");
    grid.innerHTML = beds.length === 0
      ? `<div class="empty-state">No beds match the current filters.</div>`
      : beds.map(bedCardHtml).join("");

    grid.querySelectorAll(".bed-card").forEach((card) => {
      card.addEventListener("click", () => {
        selectedBedId = card.getAttribute("data-bed-id");
        renderDetail();
      });
    });

    renderDetail();
  }

  function init() {
    State.on("beds-changed", render);

    document.getElementById("bedSearch").addEventListener("input", (e) => {
      searchTerm = e.target.value;
      render();
    });

    document.getElementById("bedRoomFilters").addEventListener("click", (e) => {
      const room = e.target.getAttribute("data-room");
      if (room) { selectedRoom = room; render(); }
    });

    document.getElementById("bedStatusFilters").addEventListener("click", (e) => {
      const status = e.target.getAttribute("data-status");
      if (status) { selectedStatus = status; render(); }
    });

    document.getElementById("addBedBtn").addEventListener("click", () => {
      document.getElementById("addBedModal").classList.remove("hidden");
    });

    document.getElementById("addBedForm").addEventListener("submit", async (e) => {
      e.preventDefault();
      const bedId = document.getElementById("newBedId").value.trim();
      const room = document.getElementById("newBedRoom").value.trim();
      if (!bedId) return;
      await Api.createBed(bedId, room);
      document.getElementById("addBedModal").classList.add("hidden");
      document.getElementById("addBedForm").reset();
    });

    render();
  }

  return { init, render };
})();
