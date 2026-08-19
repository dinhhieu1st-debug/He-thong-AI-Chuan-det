// Shared client-side state + a tiny pub/sub bus, so each tab module can react
// to REST-loaded data and SignalR push updates without polling each other.
const State = (() => {
  const beds = new Map();
  const devices = new Map();
  const listeners = {};

  function on(event, handler) {
    (listeners[event] ||= []).push(handler);
  }

  function emit(event, payload) {
    (listeners[event] || []).forEach((handler) => handler(payload));
  }

  function upsertBed(bed) {
    beds.set(bed.bedId, bed);
    emit("beds-changed", beds);
  }

  function setBeds(list) {
    beds.clear();
    list.forEach((bed) => beds.set(bed.bedId, bed));
    emit("beds-changed", beds);
  }

  function removeBed(bedId) {
    beds.delete(bedId);
    emit("beds-changed", beds);
  }

  function upsertDevice(device) {
    devices.set(device.deviceId, device);
    emit("devices-changed", devices);
  }

  function setDevices(list) {
    devices.clear();
    list.forEach((device) => devices.set(device.deviceId, device));
    emit("devices-changed", devices);
  }

  function removeDevice(deviceId) {
    devices.delete(deviceId);
    emit("devices-changed", devices);
  }

  return {
    on, emit,
    get beds() { return beds; },
    get devices() { return devices; },
    upsertBed, setBeds, removeBed,
    upsertDevice, setDevices, removeDevice
  };
})();
