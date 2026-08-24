const DevicesTab = (() => {
  const persisted = UiUtils.loadFilterState("devices", { typeFilter: "all" });
  let searchTerm = "";
  let typeFilter = persisted.typeFilter;
  let selectedDeviceId = null;

  // Needs-attention first: a fault or a dropped link is why someone opened
  // this tab, so it should not be buried under a page of healthy devices.
  const DEVICE_SEVERITY_RANK = { SENSORFAULT: 0, WARNING: 0, OFFLINE: 1, PENDING: 2, ONLINE: 3 };

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
    Unknown:  { text: "Not checked yet",   cls: "ota-idle" },
    UpToDate: { text: "Up to date",        cls: "ota-ok" },
    Available:{ text: "Update available",  cls: "ota-avail" },
    Starting: { text: "Starting…",         cls: "ota-busy" },
    Updating: { text: "Installing",        cls: "ota-busy" },
    Done:     { text: "Updated",           cls: "ota-ok" },
    Failed:   { text: "Update failed",     cls: "ota-fail" },
  };

  function otaSectionHtml(device) {
    const ota = otaByDevice.get(device.deviceId);
    const state = ota?.state || "Unknown";
    const spec = OTA_LABEL[state] || OTA_LABEL.Unknown;
    const id = UiUtils.escapeHtml(device.deviceId);
    const otaMessage = ota?.message || "";
    const needsBootloaderRescue = ota?.installedVersion <= 2
      && /(?:ABORT|0x95)/i.test(otaMessage);

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
           ota.progress != null ? ota.progress + "%" : "preparing…"}${
           ota.remainingSeconds != null && ota.remainingSeconds > 0
             ? " · about " + Math.round(ota.remainingSeconds / 60) + " min left" : ""}</div>
         <div class="ota-warn">Installing firmware — do not disconnect power.</div>`
      : "";

    const buttons = busy ? "" : `
      <div class="inline-form ota-actions">
        <button type="button" class="btn" data-ota-check="${id}">Check for update</button>
        ${state === "Available"
          ? `<button type="button" class="btn primary" data-ota-update="${id}">Update</button>`
          : ""}
      </div>`;

    return `
      <div class="ota-block">
        <div class="ota-head">
          <span class="ota-label">Firmware</span>
          <span class="ota-state ${spec.cls}">${spec.text}</span>
        </div>
        ${otaMessage ? `<div class="ota-sub">${UiUtils.escapeHtml(otaMessage)}</div>` : ""}
        ${needsBootloaderRescue
          ? `<div class="ota-warn">This v2 device has no working storage bootloader. Flash the one-time SmartIV v3 bootloader rescue image over J-Link, then future updates can use OTA.</div>`
          : ""}
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
      // Zigbee LQI (0-255), real - reported by the coordinator on every
      // packet. Battery and RSSI(dBm) used to live here too, but nothing in
      // the firmware or gateway ever sent either one; showing a metric that
      // never has a value is worse than not showing it at all.
      detailLinkQuality: UiUtils.formatMetric(device.linkQuality, "/255"),
      detailLastSeen:`Last seen: ${UiUtils.formatDateTime(device.lastSeenAt)}`,
      detailLastData:`Last data: ${device.lastDataAt ? UiUtils.formatDateTime(device.lastDataAt) : "--"}`,
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
        <div class="metric"><b id="detailLinkQuality">${UiUtils.formatMetric(device.linkQuality, "/255")}</b><span>Link Quality</span></div>
      </div>
      <div class="bed-updated">EUI-64: ${UiUtils.escapeHtml(device.eui64 || "--")}</div>
      <div class="bed-updated" id="detailLastSeen">Last seen: ${UiUtils.formatDateTime(device.lastSeenAt)}</div>
      <div class="bed-updated" id="detailLastData">Last data: ${device.lastDataAt ? UiUtils.formatDateTime(device.lastDataAt) : "--"}</div>
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
        <div class="bed-updated">
          Current: ${DEVICE_STATUS_LABEL[statusKey(device)] || (device.status || "").toUpperCase()}
          — Online, Sensor Fault and Offline are computed automatically from
          live data and cannot be set here.
        </div>
        <select id="editDeviceStatus">
          ${["Pending", "Warning"].map((s) =>
            `<option value="${s}" ${s.toUpperCase() === (device.status || "").toUpperCase() ? "selected" : ""}>${s}</option>`).join("")}
        </select>
        <button type="submit" class="btn primary">Save</button>
        <button type="button" class="btn" id="deleteDeviceBtn">Delete Device</button>
      </form>`;

    loadDeviceEvents(device.deviceId);

    document.getElementById("editDeviceForm").addEventListener("submit", async (e) => {
      e.preventDefault();
      const saveBtn = e.target.querySelector("button[type=submit]");
      saveBtn.disabled = true;
      try {
        await Api.updateDevice(device.deviceId, {
          deviceId: device.deviceId,
          deviceType: device.deviceType,
          assignedBedId: document.getElementById("editDeviceBedId").value.trim() || null,
          room: document.getElementById("editDeviceRoom").value.trim() || null,
          status: document.getElementById("editDeviceStatus").value,
          eui64: device.eui64
        });
        UiUtils.toast(`${device.deviceId} saved`);
        /* The push back over SignalR (DeviceUpdated) lands in State.devices
         * and fires a fresh render(), but renderDetail() only patches the
         * "volatile" fields for a panel already built for this device -
         * room/status just typed here would otherwise sit stale in the
         * header until the panel is closed and reopened. Forcing a rebuild
         * (not calling renderDetail() here, which would still read the OLD
         * `device` captured in this closure - let the incoming SignalR
         * update trigger it with fresh data). */
        builtDetailId = null;
      } catch (err) {
        UiUtils.toast(err.message, true);
      } finally {
        saveBtn.disabled = false;
      }
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

      if (!(await UiUtils.confirm(warning, { title: "Remove device?", confirmLabel: "Remove", danger: true }))) return;

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
        eui64: device.eui64 || deviceId
      });
      UiUtils.toast(`${deviceId} assigned to ${bedId}`);
      State.setDevices(await Api.getDevices());
    } catch (err) {
      UiUtils.toast(err.message, true);
    }
  }

  /* Firmware events read as sentences rather than as enum names, and are
   * marked so they stand out in a log that is mostly joins and faults - "what
   * firmware is this bed on, and when did it change" is the question this page
   * gets asked after an update goes wrong. */
  const FIRMWARE_EVENTS = {
    FirmwareUpdateStarted: "Firmware update started",
    FirmwareUpdated:       "Firmware updated",
    FirmwareUpdateFailed:  "Firmware update failed",
  };

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
              <span class="device-event-type${FIRMWARE_EVENTS[e.eventType] ? " device-event-fw" : ""}">${
                UiUtils.escapeHtml(FIRMWARE_EVENTS[e.eventType] || e.eventType.replace("_", " "))}</span>
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

  /* Redraws the "waiting to be assigned" banner without stealing the dropdown
   * out from under the person using it.
   *
   * The banner used to be rewritten on every render - which is once a second,
   * because the "Seen" column shows a live timestamp. Opening the bed dropdown
   * and rebuilding its <select> a moment later closes it instantly, so it was
   * simply impossible to pick a bed. Two guards:
   *
   *   1. If the focus is inside the banner, someone is working in it. Leave it
   *      completely alone.
   *   2. Otherwise carry any half-made choice across the rewrite, so a
   *      selection made and not yet submitted survives the next tick. */
  function renderPendingBanner() {
    const banner = document.getElementById("pendingDevices");
    if (!banner) return;

    if (banner.contains(document.activeElement)) return;

    const chosen = new Map();
    banner.querySelectorAll("[data-assign-bed]").forEach((sel) => {
      if (sel.value) chosen.set(sel.getAttribute("data-assign-bed"), sel.value);
    });

    const html = pendingBannerHtml();
    if (banner.innerHTML !== html) banner.innerHTML = html;

    chosen.forEach((bedId, deviceId) => {
      const sel = banner.querySelector(`[data-assign-bed="${CSS.escape(deviceId)}"]`);
      if (sel) sel.value = bedId;
    });
  }

  /* --- the firmware library ------------------------------------------------
   *
   * What a technician can see about an image is read out of the FILE, not out
   * of its name: the branch this feature came from had three files named
   * xg24_* that were really xG26 images, and a name is not evidence. */
  const OTA_KNOWN_MFG = { 4169: "SmartIV-Sensor (xG26)", 4098: "xG24" };

  async function renderOtaLibrary() {
    const host = document.getElementById("otaImageList");
    if (!host) return;

    let images;
    try {
      images = await Api.otaImages();
    } catch (err) {
      host.innerHTML = `<span class="muted">Could not read the firmware library.</span>`;
      return;
    }

    if (images.length === 0) {
      host.innerHTML = `<span class="muted">No images yet. `
        + `Upload a .ota file so "Check for update" has something to offer.</span>`;
      return;
    }

    host.innerHTML = `
      <table class="data-table">
        <tr><th>File</th><th>Device</th><th>Version</th><th>Size</th><th></th></tr>
        ${images.map((i) => `
          <tr>
            <td><b>${UiUtils.escapeHtml(i.fileName)}</b>
                <div class="muted">${UiUtils.escapeHtml(i.headerText || "")}</div></td>
            <td>${OTA_KNOWN_MFG[i.manufacturerCode]
                  ? UiUtils.escapeHtml(OTA_KNOWN_MFG[i.manufacturerCode])
                  : `<span class="ota-fail">unknown code: ${i.manufacturerCode}</span>`}</td>
            <td>v${i.fileVersion}</td>
            <td class="muted">${Math.round(i.sizeBytes / 1024)} KB</td>
            <td>${i.isProtected ? `<span class="muted">Required bridge</span> ` : ""}<button class="btn" data-ota-del="${UiUtils.escapeHtml(i.fileName)}" data-ota-protected="${i.isProtected ? "1" : "0"}">Delete</button></td>
          </tr>`).join("")}
      </table>`;

    host.querySelectorAll("[data-ota-del]").forEach((btn) => {
      btn.addEventListener("click", async () => {
        const name = btn.getAttribute("data-ota-del");
        const protectedImage = btn.getAttribute("data-ota-protected") === "1";

        if (protectedImage) {
          if (!(await UiUtils.confirm(
                     "This image is required by an active OTA policy.\n"
                     + "Deleting it may break OTA upgrades for legacy firmware.\n"
                     + "Delete anyway?",
                     { title: "Delete required bridge image?", confirmLabel: "Delete", danger: true }))) return;
        } else if (!(await UiUtils.confirm(`Remove ${name} from the firmware library?\n\n`
                   + `Devices already running this image are unaffected — it just `
                   + `stops being offered to anything from now on.`,
                   { title: "Remove firmware image?", confirmLabel: "Remove", danger: true }))) return;

        try {
          await Api.otaDeleteImage(name, protectedImage);
          renderOtaLibrary();
        } catch (err) { UiUtils.toast(err.message, true); }
      });
    });
  }

  function render() {
    const total = State.devices.size;
    const devices = Array.from(State.devices.values())
      .filter(matchesFilters)
      .sort((a, b) => {
        const ra = DEVICE_SEVERITY_RANK[statusKey(a)] ?? 4, rb = DEVICE_SEVERITY_RANK[statusKey(b)] ?? 4;
        return ra - rb || a.deviceId.localeCompare(b.deviceId);
      });
    const grid = document.getElementById("devicesGrid");
    const banner = document.getElementById("pendingDevices");
    renderPendingBanner();

    grid.innerHTML = devices.length === 0
      ? `<div class="empty-state">No devices match the current filters.</div>`
      : devices.map(deviceCardHtml).join("");

    const countEl = document.getElementById("devicesCount");
    if (countEl) countEl.textContent = devices.length === total ? `${total} devices` : `Showing ${devices.length} of ${total}`;

    document.querySelectorAll("[data-ota-check]").forEach((btn) => {
      btn.addEventListener("click", async () => {
        const id = btn.getAttribute("data-ota-check");
        btn.disabled = true;
        try {
          await Api.otaCheck(id);
          UiUtils.toast(`${id}: checking for a newer firmware`);
        } catch (err) {
          UiUtils.toast(`Could not check: ${err.message}`);
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
        if (!(await UiUtils.confirm(
              `Update the firmware on ${id}?\n\n` +
              `This takes several minutes and cannot be stopped once started. ` +
              `Do not disconnect the device's power while it runs.`,
              { title: "Update firmware?", confirmLabel: "Update", danger: true }))) {
          return;
        }
        btn.disabled = true;
        try {
          await Api.otaUpdate(id);
          UiUtils.toast(`${id}: update started`);
        } catch (err) {
          UiUtils.toast(`Could not start: ${err.message}`);
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
      console.warn("Could not load the initial firmware state", err);
    }

    State.setDevices(devices);
  }

  function applyTypeFilterChip() {
    document.querySelectorAll("#deviceTypeFilters .chip").forEach((chip) => {
      chip.classList.toggle("active", chip.getAttribute("data-filter") === typeFilter);
    });
  }

  function init() {
    State.on("devices-changed", render);
    State.on("device-discovered", (device) => {
      UiUtils.toast(`New device joined: ${device.deviceId}`);
    });

    applyTypeFilterChip();

    document.getElementById("deviceSearch").addEventListener("input", (e) => {
      searchTerm = e.target.value;
      render();
    });

    document.getElementById("deviceTypeFilters").addEventListener("click", (e) => {
      const f = e.target.getAttribute("data-filter");
      if (f) {
        typeFilter = f;
        UiUtils.saveFilterState("devices", { typeFilter });
        applyTypeFilterChip();
        render();
      }
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

    /* Room -> bed, filled from the beds that actually exist. Typing either by
     * hand was how a device ended up assigned to a bed nobody had created,
     * reporting to a dashboard nobody was looking at. */
    function fillRoomChoices() {
      const roomSel = document.getElementById("newDeviceRoom");
      const rooms = [...new Set(Array.from(State.beds.values())
                                     .map((b) => b.room)
                                     .filter(Boolean))].sort();

      roomSel.innerHTML = `<option value="">Choose a room…</option>`
        + rooms.map((r) => `<option value="${UiUtils.escapeHtml(r)}">${UiUtils.escapeHtml(r)}</option>`).join("");
    }

    function fillBedChoices(room) {
      const bedSel = document.getElementById("newDeviceBedId");
      if (!room) {
        bedSel.disabled = true;
        bedSel.innerHTML = `<option value="">Choose a room first…</option>`;
        return;
      }

      /* Beds that already have a device are shown but marked, rather than
       * hidden: "the bed I want is missing from the list" is a worse puzzle
       * than "that bed is taken". */
      const taken = new Set(Array.from(State.devices.values())
                                 .map((d) => d.assignedBedId)
                                 .filter(Boolean));

      const beds = Array.from(State.beds.values())
        .filter((b) => b.room === room)
        .sort((a, b) => a.bedId.localeCompare(b.bedId));

      bedSel.disabled = false;
      bedSel.innerHTML = `<option value="">No bed assigned</option>`
        + beds.map((b) => `<option value="${UiUtils.escapeHtml(b.bedId)}">`
                        + `${UiUtils.escapeHtml(b.bedId)}${taken.has(b.bedId) ? " · already has a device" : ""}`
                        + `</option>`).join("");
    }

    document.getElementById("newDeviceRoom").addEventListener("change", (e) => {
      fillBedChoices(e.target.value);
    });

    document.getElementById("otaFileInput")?.addEventListener("change", (e) => {
      const file = e.target.files && e.target.files[0];
      document.getElementById("otaFileName").textContent = file ? file.name : "No file selected";
    });

    document.getElementById("otaUploadBtn")?.addEventListener("click", async () => {
      const input = document.getElementById("otaFileInput");
      const file = input.files && input.files[0];
      if (!file) { UiUtils.toast("No .ota file selected", true); return; }

      const btn = document.getElementById("otaUploadBtn");
      btn.disabled = true;
      try {
        const image = await Api.otaUploadImage(file);
        /* Echo back what the FILE says, not what it is called - that is the
         * whole point of validating the header server-side. */
        UiUtils.toast(`Uploaded: ${image.headerText || image.fileName} `
                    + `(code ${image.manufacturerCode}, v${image.fileVersion})`);
        input.value = "";
        document.getElementById("otaFileName").textContent = "No file selected";
        renderOtaLibrary();
      } catch (err) {
        UiUtils.toast(err.message, true);
      } finally {
        btn.disabled = false;
      }
    });

    document.getElementById("addDeviceBtn").addEventListener("click", () => {
      fillRoomChoices();
      fillBedChoices("");
      document.getElementById("addDeviceModal").classList.remove("hidden");
    });

    document.getElementById("addDeviceForm").addEventListener("submit", async (e) => {
      e.preventDefault();
      const deviceId = document.getElementById("newDeviceId").value.trim();
      if (!deviceId) return;
      const submitBtn = e.target.querySelector("button[type=submit]");
      if (submitBtn) submitBtn.disabled = true;
      try {
        const device = await Api.createDevice({
          deviceId,
          deviceType: document.getElementById("newDeviceType").value,
          assignedBedId: document.getElementById("newDeviceBedId").value || null,
          room: document.getElementById("newDeviceRoom").value || null,
          status: "Pending",
          eui64: null
        });
        State.upsertDevice(device);
        document.getElementById("addDeviceModal").classList.add("hidden");
        document.getElementById("addDeviceForm").reset();
      } catch (err) {
        /* Most likely cause: deviceId already exists (409) - the form stays
         * open with what was typed so the technician can just fix the id,
         * instead of losing the room/bed/type they already picked. */
        UiUtils.toast(err.message, true);
      } finally {
        if (submitBtn) submitBtn.disabled = false;
      }
    });

    loadInitial();
    renderOtaLibrary();
  }

  /* Public jump-to-device, mirroring BedsTab.openBed - used by global search
   * so it does not need to duplicate the selection/render logic. */
  function openDevice(deviceId) {
    if (!State.devices.has(deviceId)) return;
    selectedDeviceId = deviceId;
    renderDetail();
  }

  return { init, render, openDevice };
})();
