(async function main() {
  const titles = {
    dashboard: "Dashboard",
    beds: "Beds & Rooms",
    alerts: "Alerts",
    devices: "Devices",
    "system-log": "System Log"
  };

  function switchTab(tab) {
    document.querySelectorAll(".nav-btn[data-tab]").forEach((btn) => {
      btn.classList.toggle("active", btn.getAttribute("data-tab") === tab);
    });
    document.querySelectorAll(".tab-panel").forEach((panel) => {
      panel.classList.toggle("active", panel.id === `tab-${tab}`);
    });
    document.getElementById("pageTitle").textContent = titles[tab] || tab;
  }

  document.querySelectorAll(".nav-btn[data-tab]").forEach((btn) => {
    btn.addEventListener("click", () => switchTab(btn.getAttribute("data-tab")));
  });

  document.querySelectorAll("[data-close-modal]").forEach((btn) => {
    btn.addEventListener("click", () => {
      document.getElementById(btn.getAttribute("data-close-modal")).classList.add("hidden");
    });
  });

  // Initial REST load, then open the live SignalR connection.
  try {
    const beds = await Api.getBeds();
    State.setBeds(beds);
  } catch (err) {
    console.error("Failed to load initial bed data:", err);
  }

  DashboardTab.init();
  BedsTab.init();
  AlertsTab.init();
  DevicesTab.init();
  SystemLogTab.init();

  await MonitoringHub.start();
})();
