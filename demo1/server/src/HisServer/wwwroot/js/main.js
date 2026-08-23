/* Toggle wired outside main()'s async body and run immediately (not awaited
 * behind Session.load()) so it works even before login - the button is
 * visible on every page including login.html's equivalent, and a nurse
 * should not have to sign in just to fix a blinding white screen. */
(function initThemeToggle() {
  const btn = document.getElementById("themeToggleBtn");
  if (!btn) return;

  function currentTheme() {
    const saved = localStorage.getItem("theme");
    if (saved === "dark" || saved === "light") return saved;
    return window.matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light";
  }

  btn.addEventListener("click", () => {
    const next = currentTheme() === "dark" ? "light" : "dark";
    localStorage.setItem("theme", next);
    document.documentElement.setAttribute("data-theme", next);
  });
})();

(async function main() {
  const titles = {
    dashboard: "Dashboard",
    beds: "Beds & Rooms",
    alerts: "Alerts",
    devices: "Devices",
    "system-log": "System Log",
    profile: "My shift",
    users: "Users",
    faults: "Fault reports",
    directory: "Bed directory"
  };

  /* Where each role lands. Everyone used to arrive at the Dashboard; for a
   * technician or an administrator that tab no longer exists, so without this
   * they would open the console to a blank page. Ordered by capability, not by
   * role name, so a new role inherits a sensible landing tab for free. */
  const LANDING = [
    ["ward.view", "dashboard"],
    ["devices.manage", "devices"],
    ["users.manage", "users"],
    ["logs.view", "system-log"]
  ];

  /* Tabs that fetch on demand instead of riding the live SignalR feed. They
   * are told when they become visible so they do not poll in the background
   * for a screen nobody is looking at. */
  const ON_DEMAND = {
    profile: ProfileTab,
    users: UsersTab,
    faults: FaultsTab,
    directory: DirectoryTab
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

  /* An administrator-issued password is not one the owner has chosen. This
   * has to happen before ANYTHING else loads - the ward grid, alerts and the
   * full-screen Critical banner all used to initialise first and only THEN
   * get covered by a semi-transparent dialog, so a nurse forced through this
   * step could already see live patient data (and a critical alarm) behind
   * it. Awaiting it here means nothing clinical exists in the DOM yet. */
  if (me.mustChangePassword) {
    await Session.promptChangePassword({ forced: true });
  }

  Session.renderIdentity();
  Session.applyTo(document);
  document.getElementById("logoutBtn")?.addEventListener("click", () => Session.logout());
  NotificationHistory.init();

  /* Bed data comes from one of two endpoints depending on the role: the full
   * one with vitals for the ward, or the directory (identifiers and rooms
   * only) for the technician assigning devices. Asking for the wrong one is a
   * 403, not a silent empty screen. */
  try {
    if (Session.can(Session.CAP.viewWard)) {
      State.setBeds(await Api.getBeds());
    } else if (Session.can(Session.CAP.viewBedDirectory)) {
      const directory = await Api.getBedDirectory();
      State.setBeds(directory.map((b) => ({ bedId: b.bedId, room: b.room, status: "Unknown" })));
    }
  } catch (err) {
    console.error("Failed to load initial bed data:", err);
    // A silent console.error here used to leave the whole ward view blank
    // with no clue why - the tab modules below still init() and render an
    // empty grid, which reads exactly like "no beds exist" instead of "the
    // request failed". Surface it, and say what to do about it.
    UiUtils.toast("Could not load bed data — check your connection and reload the page.", true);
  }

  /* Ward modules only exist for roles that can see the ward - their DOM is
   * removed for everyone else, and initialising them would throw on the first
   * missing element. */
  if (Session.can(Session.CAP.viewWard)) {
    DashboardTab.init();
    BedsTab.init();
    AlertsTab.init();
  }
  if (Session.can(Session.CAP.handleFaults)) FaultsTab.init();
  // These two tabs are removed entirely for roles without the capability, so
  // their modules must not be initialised either - their DOM is gone.
  if (Session.can(Session.CAP.manageDevices)) DevicesTab.init();
  if (Session.can(Session.CAP.viewLogs)) SystemLogTab.init();
  // Last, so the tab modules exist by the time it can open a bed. Clinical
  // alarms are for clinical roles: a technician has no way to act on one.
  if (Session.can(Session.CAP.viewWard)) CriticalAlarm.init();

  // Land on the first tab this role can actually use.
  const landing = LANDING.find(([cap]) => Session.can(cap));
  if (landing) switchTab(landing[1]);

  await MonitoringHub.start();
})();
