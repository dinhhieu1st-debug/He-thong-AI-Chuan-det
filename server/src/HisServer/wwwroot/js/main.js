(async function main() {
  const titles = {
    dashboard: "Dashboard",
    beds: "Beds & Rooms",
    alerts: "Alerts",
    devices: "Devices",
    "system-log": "System Log",
    profile: "My shift",
    users: "Users"
  };

  /* Tabs that fetch on demand instead of riding the live SignalR feed. They
   * are told when they become visible so they do not poll in the background
   * for a screen nobody is looking at. */
  const ON_DEMAND = {
    profile: ProfileTab,
    users: UsersTab
  };

  function switchTab(tab) {
    document.querySelectorAll(".nav-btn[data-tab]").forEach((btn) => {
      btn.classList.toggle("active", btn.getAttribute("data-tab") === tab);
    });
    document.querySelectorAll(".tab-panel").forEach((panel) => {
      panel.classList.toggle("active", panel.id === `tab-${tab}`);
    });
    document.getElementById("pageTitle").textContent = titles[tab] || tab;

    Object.entries(ON_DEMAND).forEach(([name, module]) => {
      if (name === tab) module.activate();
      else module.stop?.();
    });
  }

  document.querySelectorAll(".nav-btn[data-tab]").forEach((btn) => {
    btn.addEventListener("click", () => switchTab(btn.getAttribute("data-tab")));
  });

  document.querySelectorAll("[data-close-modal]").forEach((btn) => {
    btn.addEventListener("click", () => {
      document.getElementById(btn.getAttribute("data-close-modal")).classList.add("hidden");
    });
  });

  /* Identity first: every module below asks Session.can() while rendering, so
   * the answer has to exist before any of them draw anything. A failed load
   * has already redirected to the login page. */
  const me = await Session.load();
  if (!me) return;

  Session.renderIdentity();
  Session.applyTo(document);
  document.getElementById("logoutBtn")?.addEventListener("click", () => Session.logout());

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
  // These two tabs are removed entirely for roles without the capability, so
  // their modules must not be initialised either - their DOM is gone.
  if (Session.can(Session.CAP.manageDevices)) DevicesTab.init();
  if (Session.can(Session.CAP.viewLogs)) SystemLogTab.init();
  // Last, so the tab modules exist by the time it can open a bed.
  CriticalAlarm.init();

  await MonitoringHub.start();

  /* A password an administrator typed is not one the owner has chosen. The
   * dialog has no cancel, so the console stays unusable until it is replaced. */
  if (me.mustChangePassword) {
    Session.promptChangePassword({ forced: true });
  }
})();
