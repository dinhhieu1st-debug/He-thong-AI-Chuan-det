// Thin fetch wrappers around the REST API. No build step / bundler — plain
// browser JS, loaded as a global `Api` object.
const Api = (() => {
  async function request(method, path, body) {
    const response = await fetch(path, {
      method,
      headers: body ? { "Content-Type": "application/json" } : undefined,
      body: body ? JSON.stringify(body) : undefined
    });

    /* The session cookie lasts one shift. When it expires mid-use every call
     * starts failing, and without this the UI would show a wall of parse
     * errors instead of saying the obvious thing: you are signed out. */
    if (response.status === 401) {
      location.href = "/login.html";
      throw new Error("Phiên đăng nhập đã hết hạn");
    }

    if (response.status === 403) {
      throw new Error("Bạn không có quyền thực hiện thao tác này");
    }

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
    setTargetFlow: (bedId, targetFlowMlH) =>
      request("PUT", `/api/beds/${encodeURIComponent(bedId)}/target-flow`, { targetFlowMlH }),
    setTargetDrops: (bedId, targetDropsPerMin) =>
      request("PUT", `/api/beds/${encodeURIComponent(bedId)}/target-drops`, { targetDropsPerMin }),
    resetTare: (bedId) => request("POST", `/api/beds/${encodeURIComponent(bedId)}/tare`),
    recalibrateHr: (bedId) => request("POST", `/api/beds/${encodeURIComponent(bedId)}/recalibrate-hr`),
    getBedHistory: (bedId, minutes) =>
      request("GET", `/api/beds/${encodeURIComponent(bedId)}/history?minutes=${encodeURIComponent(minutes)}`),

    getAlerts: (params) => request("GET", `/api/alerts?${new URLSearchParams(params)}`),
    ackAlert: (id) => request("POST", `/api/alerts/${id}/ack`),

    getDevices: () => request("GET", "/api/devices"),
    createDevice: (device) => request("POST", "/api/devices", device),
    updateDevice: (deviceId, device) => request("PUT", `/api/devices/${encodeURIComponent(deviceId)}`, device),
    deleteDevice: (deviceId) => request("DELETE", `/api/devices/${encodeURIComponent(deviceId)}`),

    getLogs: (params) => request("GET", `/api/logs?${new URLSearchParams(params)}`),

    // Session + roster
    getMyAssignment: () => request("GET", "/api/me/assignment"),
    changePassword: (currentPassword, newPassword) =>
      request("POST", "/api/auth/change-password", { currentPassword, newPassword }),

    // Patient in a bed (admit / discharge)
    setPatient: (bedId, patient) =>
      request("PUT", `/api/beds/${encodeURIComponent(bedId)}/patient`, patient),

    // User administration
    getUsers: () => request("GET", "/api/users"),
    getUser: (userId) => request("GET", `/api/users/${userId}`),
    createUser: (user) => request("POST", "/api/users", user),
    updateUser: (userId, patch) => request("PUT", `/api/users/${userId}`, patch),
    resetUserPassword: (userId, newPassword) =>
      request("POST", `/api/users/${userId}/reset-password`, { newPassword }),
    deleteUser: (userId) => request("DELETE", `/api/users/${userId}`),
    addAssignment: (userId, scopeType, scopeValue) =>
      request("POST", `/api/users/${userId}/assignments`, { scopeType, scopeValue }),
    removeAssignment: (assignmentId) =>
      request("DELETE", `/api/users/assignments/${assignmentId}`),
    exportLogsUrl: (params) => `/api/logs/export?${new URLSearchParams(params)}`
  };
})();
