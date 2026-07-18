// Thin fetch wrappers around the REST API. No build step / bundler — plain
// browser JS, loaded as a global `Api` object.
const Api = (() => {
  async function request(method, path, body) {
    const response = await fetch(path, {
      method,
      headers: body ? { "Content-Type": "application/json" } : undefined,
      body: body ? JSON.stringify(body) : undefined
    });

    if (!response.ok) {
      const text = await response.text().catch(() => "");
      throw new Error(`${method} ${path} failed: ${response.status} ${text}`);
    }

    if (response.status === 204) {
      return null;
    }

    const contentType = response.headers.get("content-type") || "";
    return contentType.includes("application/json") ? response.json() : null;
  }

  return {
    getBeds: () => request("GET", "/api/beds"),
    createBed: (bedId, room) => request("POST", "/api/beds", { bedId, room }),
    updateBed: (bedId, patch) => request("PUT", `/api/beds/${encodeURIComponent(bedId)}`, patch),

    getAlerts: (params) => request("GET", `/api/alerts?${new URLSearchParams(params)}`),
    ackAlert: (id) => request("POST", `/api/alerts/${id}/ack`),

    getDevices: () => request("GET", "/api/devices"),
    createDevice: (device) => request("POST", "/api/devices", device),
    updateDevice: (deviceId, device) => request("PUT", `/api/devices/${encodeURIComponent(deviceId)}`, device),
    deleteDevice: (deviceId) => request("DELETE", `/api/devices/${encodeURIComponent(deviceId)}`),

    getLogs: (params) => request("GET", `/api/logs?${new URLSearchParams(params)}`),
    exportLogsUrl: (params) => `/api/logs/export?${new URLSearchParams(params)}`
  };
})();
