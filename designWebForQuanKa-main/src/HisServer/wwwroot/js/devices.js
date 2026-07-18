const DevicesTab = (() => {
  let searchTerm = "";
  let typeFilter = "all";
  let selectedDeviceId = null;

  const DEVICE_STATUS_COLOR = {
    ONLINE: "#1ea050",
    PENDING: "#e69119",
    WARNING: "#e69119",
    OFFLINE: "#8a97a8"
  };

  function matchesFilters(device) {
    if (typeFilter !== "all" && device.deviceType.toLowerCase() !== typeFilter) return false;
    if (searchTerm) {
      const haystack = `${device.deviceId} ${device.assignedBedId || ""} ${device.room || ""}`.toLowerCase();
      if (!haystack.includes(searchTerm.toLowerCase())) return false;
    }
    return true;
  }

  function deviceCardHtml(device) {
    const color = DEVICE_STATUS_COLOR[(device.status || "").toUpperCase()] || "#8a97a8";
    return `
      <div class="device-card" data-device-id="${UiUtils.escapeHtml(device.deviceId)}">
        <div class="row-top">
          <div><div class="device-id">${UiUtils.escapeHtml(device.deviceId)}</div><div class="device-type">${UiUtils.escapeHtml(device.deviceType.toUpperCase())}</div></div>
          <span class="status-chip" style="background:${color};">${UiUtils.escapeHtml((device.status || "").toUpperCase())}</span>
        </div>
        <div class="meta-row">
          <span>Bed: ${UiUtils.escapeHtml(device.assignedBedId || "--")}</span>
          <span>Room: ${UiUtils.escapeHtml(device.room || "--")}</span>
        </div>
        <div class="meta-row">
          <span>Battery: ${UiUtils.formatMetric(device.batteryPercent, "%")}</span>
          <span>RSSI: ${UiUtils.formatMetric(device.rssi, " dBm")}</span>
        </div>
      </div>`;
  }

  function renderDetail() {
    const panel = document.getElementById("deviceDetailPanel");
    const device = selectedDeviceId ? State.devices.get(selectedDeviceId) : null;
    if (!device) {
      panel.style.display = "none";
      return;
    }

    panel.style.display = "block";
    panel.innerHTML = `
      <h3>${UiUtils.escapeHtml(device.deviceId)}</h3>
      <div class="sub">${UiUtils.escapeHtml(device.deviceType.toUpperCase())} · ${UiUtils.escapeHtml((device.status || "").toUpperCase())}</div>
      <div class="metric-row">
        <div class="metric"><b>${UiUtils.formatMetric(device.batteryPercent, "%")}</b><span>Battery</span></div>
        <div class="metric"><b>${UiUtils.formatMetric(device.rssi, " dBm")}</b><span>RSSI</span></div>
      </div>
      <div class="bed-updated">EUI-64: ${UiUtils.escapeHtml(device.eui64 || "--")}</div>
      <div class="bed-updated">Last seen: ${UiUtils.formatDateTime(device.lastSeenAt)}</div>
      <form id="editDeviceForm">
        <label>Assigned Bed</label>
        <input type="text" id="editDeviceBedId" value="${UiUtils.escapeHtml(device.assignedBedId || "")}">
        <label>Room</label>
        <input type="text" id="editDeviceRoom" value="${UiUtils.escapeHtml(device.room || "")}">
        <label>Status</label>
        <select id="editDeviceStatus">
          ${["Online", "Pending", "Warning", "Offline"].map((s) =>
            `<option value="${s}" ${s.toUpperCase() === (device.status || "").toUpperCase() ? "selected" : ""}>${s}</option>`).join("")}
        </select>
        <button type="submit" class="btn primary">Save</button>
        <button type="button" class="btn" id="deleteDeviceBtn">Delete Device</button>
      </form>`;

    document.getElementById("editDeviceForm").addEventListener("submit", async (e) => {
      e.preventDefault();
      await Api.updateDevice(device.deviceId, {
        deviceId: device.deviceId,
        deviceType: device.deviceType,
        assignedBedId: document.getElementById("editDeviceBedId").value.trim() || null,
        room: document.getElementById("editDeviceRoom").value.trim() || null,
        status: document.getElementById("editDeviceStatus").value,
        batteryPercent: device.batteryPercent,
        rssi: device.rssi,
        eui64: device.eui64
      });
    });

    document.getElementById("deleteDeviceBtn").addEventListener("click", async () => {
      await Api.deleteDevice(device.deviceId);
      State.removeDevice(device.deviceId);
      selectedDeviceId = null;
      render();
    });
  }

  function render() {
    const devices = Array.from(State.devices.values()).filter(matchesFilters).sort((a, b) => a.deviceId.localeCompare(b.deviceId));
    const grid = document.getElementById("devicesGrid");
    grid.innerHTML = devices.length === 0
      ? `<div class="empty-state">No devices match the current filters.</div>`
      : devices.map(deviceCardHtml).join("");

    grid.querySelectorAll(".device-card").forEach((card) => {
      card.addEventListener("click", () => {
        selectedDeviceId = card.getAttribute("data-device-id");
        renderDetail();
      });
    });

    renderDetail();
  }

  async function loadInitial() {
    const devices = await Api.getDevices();
    State.setDevices(devices);
  }

  function init() {
    State.on("devices-changed", render);

    document.getElementById("deviceSearch").addEventListener("input", (e) => {
      searchTerm = e.target.value;
      render();
    });

    document.getElementById("deviceTypeFilters").addEventListener("click", (e) => {
      const f = e.target.getAttribute("data-filter");
      if (f) { typeFilter = f; render(); }
    });

    document.getElementById("addDeviceBtn").addEventListener("click", () => {
      document.getElementById("addDeviceModal").classList.remove("hidden");
    });

    document.getElementById("addDeviceForm").addEventListener("submit", async (e) => {
      e.preventDefault();
      const deviceId = document.getElementById("newDeviceId").value.trim();
      if (!deviceId) return;
      const device = await Api.createDevice({
        deviceId,
        deviceType: document.getElementById("newDeviceType").value,
        assignedBedId: document.getElementById("newDeviceBedId").value.trim() || null,
        room: document.getElementById("newDeviceRoom").value.trim() || null,
        status: "Pending",
        batteryPercent: null,
        rssi: null,
        eui64: null
      });
      State.upsertDevice(device);
      document.getElementById("addDeviceModal").classList.add("hidden");
      document.getElementById("addDeviceForm").reset();
    });

    loadInitial();
  }

  return { init, render };
})();
