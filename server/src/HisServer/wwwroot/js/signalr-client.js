// Opens the SignalR connection to /hubs/monitoring and fans events out
// through State's pub/sub bus (dashboard/beds/alerts/devices modules
// subscribe rather than knowing about SignalR directly).
const MonitoringHub = (() => {
  const connection = new signalR.HubConnectionBuilder()
    .withUrl("/hubs/monitoring")
    .withAutomaticReconnect()
    .build();

  connection.on("BedUpdated", (bed) => State.upsertBed(bed));
  connection.on("AlertCreated", (alert) => State.emit("alert-created", alert));
  connection.on("AlertAcknowledged", (alertId, acknowledgedAt) =>
    State.emit("alert-acknowledged", { alertId, acknowledgedAt }));
  connection.on("DeviceUpdated", (device) => State.upsertDevice(device));
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
