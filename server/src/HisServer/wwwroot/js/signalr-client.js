// Opens the SignalR connection to /hubs/monitoring and fans events out
// through State's pub/sub bus (dashboard/beds/alerts/devices modules
// subscribe rather than knowing about SignalR directly).
const MonitoringHub = (() => {
  const connection = new signalR.HubConnectionBuilder()
    .withUrl("/hubs/monitoring")
    .withAutomaticReconnect()
    .build();

  connection.on("BedUpdated", (bed) => State.upsertBed(bed));
  /* A removed bed must vanish from every open console at once. Leaving it on
   * screen is worse than a stale reading: it is a bed somebody might go and
   * look for a patient in. */
  connection.on("BedRemoved", (bedId) => State.removeBed(bedId));
  connection.on("AlertCreated", (alert) => State.emit("alert-created", alert));
  connection.on("AlertAcknowledged", (alertId, acknowledgedAt) =>
    State.emit("alert-acknowledged", { alertId, acknowledgedAt }));
  connection.on("DeviceUpdated", (device) => State.upsertDevice(device));

  /* Tiến độ nạp firmware. Sự kiện riêng chứ không gắn vào DeviceUpdated: trong
   * lúc nạp nó về vài giây một lần suốt vài phút, và đẩy cả bản ghi thiết bị
   * với nhịp đó sẽ vẽ lại trang Devices liên tục. */
  connection.on("OtaStatusChanged", (status) => State.emit("ota-status", status));
  /* A device that just joined the Zigbee network. Same payload as
   * DeviceUpdated, separate event so the Devices tab can say so out loud
   * instead of quietly adding a row nobody notices. */
  connection.on("FaultReported", (report) => State.emit("fault-reported", report));
  connection.on("DeviceDiscovered", (device) => {
    State.upsertDevice(device);
    State.emit("device-discovered", device);
  });

  connection.onreconnected(() => State.emit("connection-status", "connected"));
  connection.onreconnecting(() => State.emit("connection-status", "reconnecting"));
  connection.onclose(() => State.emit("connection-status", "disconnected"));

  async function start() {
    try {
      await connection.start();
      State.emit("connection-status", "connected");
    } catch (err) {
      console.error("SignalR connection failed:", err);
      State.emit("connection-status", "disconnected");
      setTimeout(start, 3000);
    }
  }

  return { start };
})();
