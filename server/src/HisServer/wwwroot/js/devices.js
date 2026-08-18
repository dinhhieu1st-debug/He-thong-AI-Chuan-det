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

  /* Trạng thái cập nhật firmware của từng thiết bị, đến qua SignalR.
   * Giữ ngoài State.devices vì nó cập nhật vài giây một lần trong lúc nạp, còn
   * bản ghi thiết bị thì gần như không đổi. */
  const otaByDevice = new Map();

  State.on("ota-status", (status) => {
    if (!status || !status.deviceId) return;
    otaByDevice.set(status.deviceId, status);
    render();
  });

  const OTA_LABEL = {
    Unknown:  { text: "Chưa kiểm tra",        cls: "ota-idle" },
    UpToDate: { text: "Đang ở bản mới nhất",  cls: "ota-ok" },
    Available:{ text: "Có bản cập nhật",      cls: "ota-avail" },
    Starting: { text: "Đang bắt đầu…",        cls: "ota-busy" },
    Updating: { text: "Đang nạp firmware",    cls: "ota-busy" },
    Done:     { text: "Đã cập nhật xong",     cls: "ota-ok" },
    Failed:   { text: "Cập nhật thất bại",    cls: "ota-fail" },
  };

  function otaSectionHtml(device) {
    const ota = otaByDevice.get(device.deviceId);
    const state = ota?.state || "Unknown";
    const spec = OTA_LABEL[state] || OTA_LABEL.Unknown;
    const id = UiUtils.escapeHtml(device.deviceId);

    /* Trong lúc nạp thì KHÔNG hiện nút Cập nhật.
     *
     * Bấm lần thứ hai sẽ khởi động một lượt truyền ảnh chồng lên lượt đang
     * chạy - đó là cách duy nhất tính năng này có thể để lại một thiết bị đầu
     * giường với nửa cái firmware. Server cũng từ chối, nhưng nút không nên
     * mời người ta bấm ngay từ đầu. */
    const busy = ota?.inFlight === true;

    const bar = busy
      ? `<div class="ota-bar"><div class="ota-bar-fill" style="width:${
           ota.progress != null ? ota.progress : 0}%;"></div></div>
         <div class="ota-sub">${
           ota.progress != null ? ota.progress + "%" : "đang chuẩn bị…"}${
           ota.remainingSeconds != null && ota.remainingSeconds > 0
             ? " · còn khoảng " + Math.round(ota.remainingSeconds / 60) + " phút" : ""}</div>
         <div class="ota-warn">Đang nạp firmware — đừng rút nguồn thiết bị.</div>`
      : "";

    const buttons = busy ? "" : `
      <div class="inline-form ota-actions">
        <button type="button" class="btn" data-ota-check="${id}">Kiểm tra bản mới</button>
        ${state === "Available"
          ? `<button type="button" class="btn primary" data-ota-update="${id}">Cập nhật</button>`
          : ""}
      </div>`;

    return `
      <div class="ota-block">
        <div class="ota-head">
          <span class="ota-label">Firmware</span>
          <span class="ota-state ${spec.cls}">${spec.text}</span>
        </div>
        ${ota?.message ? `<div class="ota-sub">${UiUtils.escapeHtml(ota.message)}</div>` : ""}
        ${bar}
        ${buttons}
      </div>`;
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
        ${otaSectionHtml(device)}
      </div>`;
  }

  /* Which device the detail panel is currently BUILT for.
   *
   * A bedside sensor reports about once a second, and every report used to
   * rebuild this whole panel: the log flashed back to "Loading…" once a
   * second, and anything half-typed in the form below it was wiped. So the
   * panel is built once per selected device, and the handful of fields that
   * really do change are written in place afterwards. */
  let builtDetailId = null;

  function detailVolatileHtml(device) {
    return {
      detailBattery: UiUtils.formatMetric(device.batteryPercent, "%"),
      detailRssi:    UiUtils.formatMetric(device.rssi, " dBm"),
      detailLastSeen:`Last seen: ${UiUtils.formatDateTime(device.lastSeenAt)}`,
      detailLastData:`Last data: ${device.lastDataAt ? UiUtils.formatDateTime(device.lastDataAt) : "--"}`,
      detailSignal:  `Signal: ${linkLabel(device.linkQuality)}`,
    };
  }

  function renderDetail() {
    const panel = document.getElementById("deviceDetailPanel");
    const device = selectedDeviceId ? State.devices.get(selectedDeviceId) : null;
    if (!device) {
      panel.style.display = "none";
      builtDetailId = null;
      return;
    }

    /* Same device as last time: touch only the live numbers and leave the log,
     * the form and the caret exactly where they are. */
    if (builtDetailId === device.deviceId) {
      const fields = detailVolatileHtml(device);
      for (const [id, text] of Object.entries(fields)) {
        const el = document.getElementById(id);
        if (el && el.textContent !== text) el.textContent = text;
      }
      return;
    }

    builtDetailId = device.deviceId;
    panel.style.display = "block";
    panel.innerHTML = `
      <h3>${UiUtils.escapeHtml(device.deviceId)}</h3>
      <div class="sub">${UiUtils.escapeHtml(device.deviceType.toUpperCase())} · ${UiUtils.escapeHtml((device.status || "").toUpperCase())}</div>
      <div class="metric-row">
        <div class="metric"><b id="detailBattery">${UiUtils.formatMetric(device.batteryPercent, "%")}</b><span>Battery</span></div>
        <div class="metric"><b id="detailRssi">${UiUtils.formatMetric(device.rssi, " dBm")}</b><span>RSSI</span></div>
      </div>
      <div class="bed-updated">EUI-64: ${UiUtils.escapeHtml(device.eui64 || "--")}</div>
      <div class="bed-updated" id="detailLastSeen">Last seen: ${UiUtils.formatDateTime(device.lastSeenAt)}</div>
      <div class="bed-updated" id="detailLastData">Last data: ${device.lastDataAt ? UiUtils.formatDateTime(device.lastDataAt) : "--"}</div>
      <div class="bed-updated" id="detailSignal">Signal: ${linkLabel(device.linkQuality)}</div>
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
      /* Deleting used to happen on one click, with nothing said about what it
       * meant. A device that is currently sending data is not a stale entry to
       * tidy up - it is a monitor on a bed - so the warning says which bed, and
       * says out loud that the row comes back by itself rather than leaving the
       * reader to fear they have broken something permanently. */
      const live = device.lastDataAt
        && (Date.now() - new Date(device.lastDataAt).getTime()) < 120000;

      const warning = live
        ? `${device.deviceId} is SENDING DATA right now`
          + `${device.assignedBedId ? ` for ${device.assignedBedId}` : ""}.\n\n`
          + `Removing it only deletes this record — the hardware keeps running, `
          + `and the gateway will re-add it as an unassigned device within a `
          + `minute or two. You would then have to assign it to a bed again.\n\n`
          + `Remove it anyway?`
        : `Remove the record for ${device.deviceId}?\n\n`
          + `If the device is still on the Zigbee network it will reappear as `
          + `unassigned and need assigning to a bed again.`;

      if (!confirm(warning)) return;

      try {
        await Api.deleteDevice(device.deviceId);
        State.removeDevice(device.deviceId);
        selectedDeviceId = null;
        UiUtils.toast(`${device.deviceId} removed`);
        render();
      } catch (err) {
        UiUtils.toast(err.message, true);
      }
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
        <div class="muted" style="margin-bottom:8px;">
          Devices appear here on their own when they join the Zigbee network,
          or when the gateway re-reads its inventory. Assign one to a bed to
          start recording its health.
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

      /* The panel may have been closed, or another device selected, while this
       * request was in flight. Writing the answer now would drop one device's
       * log into another device's panel. */
      if (selectedDeviceId !== deviceId) return;

      const html = events.length === 0
        ? `<span class="muted">No events recorded yet.</span>`
        : events.map((e) => `
            <div class="device-event">
              <span class="device-event-type">${UiUtils.escapeHtml(e.eventType.replace("_", " "))}</span>
              <span class="muted">${UiUtils.formatDateTime(e.occurredAt)}</span>
              ${e.detail ? `<div class="muted">${UiUtils.escapeHtml(e.detail)}</div>` : ""}
            </div>`).join("");

      /* Only touch the DOM when the log actually changed. A periodic refresh
       * that rewrites identical HTML still kills the user's text selection and
       * restarts any CSS transition - which is exactly what "nháy nháy" is. */
      if (host.innerHTML !== html) host.innerHTML = html;
    } catch (err) {
      if (selectedDeviceId !== deviceId) return;
      /* Keep whatever is already on screen; replacing a log the technician is
       * reading with an error because one poll failed is worse than a log that
       * is a few seconds stale. */
      if (!host.querySelector(".device-event")) {
        host.innerHTML = `<span class="muted">Could not load the device log.</span>`;
      }
    }
  }

  /* The log still needs to pick up new events on its own - just not at the rate
   * the sensor reports. Every 15 seconds, and silently. */
  const DEVICE_LOG_REFRESH_MS = 15000;
  setInterval(() => {
    if (selectedDeviceId && builtDetailId === selectedDeviceId) {
      loadDeviceEvents(selectedDeviceId);
    }
  }, DEVICE_LOG_REFRESH_MS);

  function render() {
    const devices = Array.from(State.devices.values()).filter(matchesFilters).sort((a, b) => a.deviceId.localeCompare(b.deviceId));
    const grid = document.getElementById("devicesGrid");
    const banner = document.getElementById("pendingDevices");
    if (banner) banner.innerHTML = pendingBannerHtml();

    grid.innerHTML = devices.length === 0
      ? `<div class="empty-state">No devices match the current filters.</div>`
      : devices.map(deviceCardHtml).join("");

    document.querySelectorAll("[data-ota-check]").forEach((btn) => {
      btn.addEventListener("click", async () => {
        const id = btn.getAttribute("data-ota-check");
        btn.disabled = true;
        try {
          await Api.otaCheck(id);
          UiUtils.toast(`${id}: đã gửi lệnh kiểm tra bản mới`);
        } catch (err) {
          UiUtils.toast(`Không kiểm tra được: ${err.message}`);
          btn.disabled = false;
        }
      });
    });

    document.querySelectorAll("[data-ota-update]").forEach((btn) => {
      btn.addEventListener("click", async () => {
        const id = btn.getAttribute("data-ota-update");
        /* Nạp firmware mất vài phút và không huỷ giữa chừng được, nên hỏi lại
         * một lần. Đây là thao tác duy nhất ở trang này có thể làm một giường
         * ngừng theo dõi. */
        if (!window.confirm(
              `Cập nhật firmware cho ${id}?\n\n` +
              `Quá trình mất vài phút và không dừng giữa chừng được. ` +
              `Đừng rút nguồn thiết bị trong lúc đó.`)) {
          return;
        }
        btn.disabled = true;
        try {
          await Api.otaUpdate(id);
          UiUtils.toast(`${id}: đã bắt đầu cập nhật`);
        } catch (err) {
          UiUtils.toast(`Không bắt đầu được: ${err.message}`);
          btn.disabled = false;
        }
      });
    });

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

    /* Firmware state before the first render: progress only arrives over
     * SignalR, so a page opened while a device is being flashed would show it
     * as never checked - and offer the Update button - until the next push. */
    try {
      for (const status of await Api.otaStatuses()) {
        if (status?.deviceId) otaByDevice.set(status.deviceId, status);
      }
    } catch (err) {
      console.warn("Không lấy được trạng thái firmware ban đầu", err);
    }

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

    document.getElementById("rescanDevicesBtn")?.addEventListener("click", async () => {
      /* Asks the gateway to re-announce everything zigbee2mqtt knows about.
       * The list it returns is the same one a join produces, so a device
       * removed here comes back as unassigned and is assigned to a bed the
       * normal way - no hidden path that only works for re-adds. */
      try {
        const result = await Api.rescanDevices();
        UiUtils.toast(result.gateways > 0
          ? `Rescan sent to ${result.gateways} gateway(s)`
          : "No gateway is connected right now", result.gateways === 0);
        setTimeout(async () => State.setDevices(await Api.getDevices()), 3000);
      } catch (err) {
        UiUtils.toast(err.message, true);
      }
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
