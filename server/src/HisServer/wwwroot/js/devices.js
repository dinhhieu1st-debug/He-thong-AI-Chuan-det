const DevicesTab = (() => {
  let searchTerm = "";
  let typeFilter = "all";
  let selectedDeviceId = null;

  /* SENSOR_FAULT sits between healthy and dead on purpose, and gets its own
   * colour: the unit is alive and talking, but a probe or a cable is gone.
   * That is a different van-load of spare parts from a device that has stopped
   * answering entirely. */
  const DEVICE_STATUS_COLOR = {
    ONLINE: "#1ea050",
    SENSORFAULT: "#e08b00",
    PENDING: "#e69119",
    WARNING: "#e69119",
    OFFLINE: "#8a97a8"
  };

  const DEVICE_STATUS_LABEL = {
    ONLINE: "ONLINE",
    SENSORFAULT: "SENSOR FAULT",
    PENDING: "PENDING",
    WARNING: "WARNING",
    OFFLINE: "OFFLINE"
  };

  function statusKey(device) {
    return (device.status || "").toUpperCase().replace("_", "");
  }

  /* Signal strength means nothing to most readers as a raw 0-255, and the
   * decision it drives is coarse: is this bed's radio link fine, marginal, or
   * the reason it keeps dropping out. */
  function linkLabel(linkQuality) {
    if (linkQuality == null) return "--";
    const quality = linkQuality >= 120 ? "good" : linkQuality >= 60 ? "fair" : "weak";
    return `${linkQuality}/255 (${quality})`;
  }

  function matchesFilters(device) {
    if (typeFilter !== "all" && device.deviceType.toLowerCase() !== typeFilter) return false;
    if (searchTerm) {
      const haystack = `${device.deviceId} ${device.assignedBedId || ""} ${device.room || ""}`.toLowerCase();
      if (!haystack.includes(searchTerm.toLowerCase())) return false;
    }
    return true;
  }

  function deviceCardHtml(device) {
    const key = statusKey(device);
    const color = DEVICE_STATUS_COLOR[key] || "#8a97a8";
    return `
      <div class="device-card" data-device-id="${UiUtils.escapeHtml(device.deviceId)}">
        <div class="row-top">
          <div><div class="device-id">${UiUtils.escapeHtml(device.deviceId)}</div><div class="device-type">${UiUtils.escapeHtml(device.deviceType.toUpperCase())}</div></div>
          <span class="status-chip" style="background:${color};">${DEVICE_STATUS_LABEL[key] || key}</span>
        </div>
        <div class="meta-row">
          <span>Bed: ${UiUtils.escapeHtml(device.assignedBedId || "--")}</span>
          <span>Room: ${UiUtils.escapeHtml(device.room || "--")}</span>
        </div>
        <div class="meta-row">
          <span>Signal: ${linkLabel(device.linkQuality)}</span>
          <span>Last data: ${device.lastDataAt ? UiUtils.formatDateTime(device.lastDataAt) : "--"}</span>
        </div>
        ${device.channelsLost
          ? `<div class="meta-row device-fault-line">No signal from: ${UiUtils.escapeHtml(device.channelsLost)}</div>`
          : ""}
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
      <div class="bed-updated">Last data: ${device.lastDataAt ? UiUtils.formatDateTime(device.lastDataAt) : "--"}</div>
      <div class="bed-updated">Signal: ${linkLabel(device.linkQuality)}</div>
      ${device.channelsLost
        ? `<div class="bed-updated device-fault-line">No signal from: ${UiUtils.escapeHtml(device.channelsLost)}</div>`
        : ""}
      <div class="section-title" style="margin-top:14px;">Device log</div>
      <div id="deviceEvents"><span class="muted">Loading…</span></div>
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

    loadDeviceEvents(device.deviceId);

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

  /* Devices that announced themselves over Zigbee but have no bed yet.
   *
   * They are pulled to the top rather than left to sort in among the rest:
   * an unassigned device forwards no vitals, so it is the one thing on this
   * screen that needs someone to act. */
  function pendingBannerHtml() {
    /* Only bedside sensors. A gateway serves a ROOM and legitimately has no
     * bed, so listing gateways here would put three rows that need no action
     * next to the one that does - and a list where most entries are noise
     * stops being read. */
    const pending = Array.from(State.devices.values())
      .filter((d) => !d.assignedBedId && (d.deviceType || "").toUpperCase() === "XG26")
      .sort((a, b) => (b.lastSeenAt || "").localeCompare(a.lastSeenAt || ""));

    if (pending.length === 0) return "";

    const beds = Array.from(State.beds.values())
      .sort((a, b) => a.bedId.localeCompare(b.bedId));

    const rows = pending.map((d) => `
      <tr>
        <td><b>${UiUtils.escapeHtml(d.deviceId)}</b></td>
        <td class="muted">${d.lastSeenAt ? UiUtils.formatDateTime(d.lastSeenAt) : "—"}</td>
        <td>
          <select class="search-input" data-assign-bed="${UiUtils.escapeHtml(d.deviceId)}" style="max-width:170px;">
            <option value="">Choose a bed…</option>
            ${beds.map((b) => `<option value="${UiUtils.escapeHtml(b.bedId)}">${UiUtils.escapeHtml(b.bedId)} · ${UiUtils.escapeHtml(b.room)}</option>`).join("")}
          </select>
        </td>
        <td><button class="btn primary" data-assign-device="${UiUtils.escapeHtml(d.deviceId)}">Assign</button></td>
      </tr>`).join("");

    return `
      <div class="pending-devices">
        <div class="section-title" style="margin-top:0;">
          New devices waiting to be assigned (${pending.length})
        </div>
        <table class="data-table">
          <thead><tr><th>Device</th><th>Seen</th><th>Bed</th><th></th></tr></thead>
          <tbody>${rows}</tbody>
        </table>
      </div>`;
  }

  async function assignDevice(deviceId) {
    const select = document.querySelector(`[data-assign-bed="${CSS.escape(deviceId)}"]`);
    const bedId = select ? select.value : "";
    if (!bedId) {
      UiUtils.toast("Choose a bed first", true);
      return;
    }

    const device = State.devices.get(deviceId);
    const bed = State.beds.get(bedId);
    try {
      await Api.updateDevice(deviceId, {
        deviceId,
        deviceType: device.deviceType,
        assignedBedId: bedId,
        // The room comes from the bed, not from a second thing to type: a
        // device assigned to BED-101 is in whatever room BED-101 is in.
        room: bed ? bed.room : device.room,
        status: "Online",
        batteryPercent: device.batteryPercent,
        rssi: device.rssi,
        eui64: device.eui64 || deviceId
      });
      UiUtils.toast(`${deviceId} assigned to ${bedId}`);
      State.setDevices(await Api.getDevices());
    } catch (err) {
      UiUtils.toast(err.message, true);
    }
  }

  /* The technician's log. Deliberately not the System Log, which carries SpO2
   * and heart rate in every row - this one records what happened to the
   * equipment and nothing about the patient. */
  async function loadDeviceEvents(deviceId) {
    const host = document.getElementById("deviceEvents");
    if (!host) return;
    try {
      const events = await Api.getDeviceEvents(deviceId);
      host.innerHTML = events.length === 0
        ? `<span class="muted">No events recorded yet.</span>`
        : events.map((e) => `
            <div class="device-event">
              <span class="device-event-type">${UiUtils.escapeHtml(e.eventType.replace("_", " "))}</span>
              <span class="muted">${UiUtils.formatDateTime(e.occurredAt)}</span>
              ${e.detail ? `<div class="muted">${UiUtils.escapeHtml(e.detail)}</div>` : ""}
            </div>`).join("");
    } catch (err) {
      host.innerHTML = `<span class="muted">Could not load the device log.</span>`;
    }
  }

  function render() {
    const devices = Array.from(State.devices.values()).filter(matchesFilters).sort((a, b) => a.deviceId.localeCompare(b.deviceId));
    const grid = document.getElementById("devicesGrid");
    const banner = document.getElementById("pendingDevices");
    if (banner) banner.innerHTML = pendingBannerHtml();

    grid.innerHTML = devices.length === 0
      ? `<div class="empty-state">No devices match the current filters.</div>`
      : devices.map(deviceCardHtml).join("");

    document.querySelectorAll("[data-assign-device]").forEach((btn) => {
      btn.addEventListener("click", () => assignDevice(btn.getAttribute("data-assign-device")));
    });

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
    State.on("device-discovered", (device) => {
      UiUtils.toast(`New device joined: ${device.deviceId}`);
    });

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
