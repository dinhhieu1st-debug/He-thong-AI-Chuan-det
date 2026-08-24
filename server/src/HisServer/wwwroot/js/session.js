/* Who is signed in, and what they may do.
 *
 * Loaded before every other tab module: the rest of the UI asks Session.can()
 * before drawing a control, so this has to be answered first.
 *
 * Hiding a button here is presentation, NOT security - the server refuses the
 * same actions with 403 whatever the browser shows (see Capabilities.cs). What
 * this prevents is offering a nurse a button that would only fail. The two
 * lists come from the same source: /api/auth/me returns the server's own
 * capability names. */
const Session = (() => {
  let me = null;

  const CAP = {
    viewWard: "ward.view",
    ackAlerts: "alerts.ack",
    controlBed: "bed.control",
    editPatient: "patient.edit",
    manageDevices: "devices.manage",
    viewLogs: "logs.view",
    manageBeds: "beds.manage",
    manageUsers: "users.manage",
    reportFaults: "faults.report",
    handleFaults: "faults.handle",
    viewBedDirectory: "beds.directory"
  };

  const ROLE_LABEL = {
    NURSE: "Nurse",
    TECHNICIAN: "Technician",
    ADMIN: "Administrator"
  };

  async function load() {
    const response = await fetch("/api/auth/me", { credentials: "same-origin" });
    if (!response.ok) {
      location.href = "/login.html";
      return null;
    }
    me = await response.json();
    return me;
  }

  function can(capability) {
    return !!me && me.capabilities.includes(capability);
  }

  /* Applies the current role to the DOM in one pass:
   *   data-cap="devices.manage"  -> element is removed unless the user has it
   * Removed rather than hidden, so a curious user cannot un-hide a control in
   * DevTools and be surprised when it 403s. */
  function applyTo(root = document) {
    root.querySelectorAll("[data-cap]").forEach((el) => {
      const needed = el.getAttribute("data-cap").split(/\s+/).filter(Boolean);
      if (!needed.every(can)) el.remove();
    });
  }

  function renderIdentity() {
    const nameEl = document.getElementById("sessionName");
    const roleEl = document.getElementById("sessionRole");
    const avatarEl = document.getElementById("sessionAvatar");
    if (!me || !nameEl) return;

    const display = me.fullName && me.fullName.trim() ? me.fullName : me.username;
    nameEl.textContent = display;
    roleEl.textContent = ROLE_LABEL[me.role] || me.role;
    avatarEl.textContent = display.trim().slice(0, 2).toUpperCase();
  }

  /* Change-password dialog, also used as the forced first-login step. When it
   * is forced there is no cancel button: an administrator-issued password is
   * known to somebody other than its owner, so the console stays closed until
   * it is replaced.
   *
   * Returns a Promise that resolves once the password has actually been
   * changed - main.js awaits this for the forced case so the ward, its
   * alerts and SignalR never even start loading behind the dialog. Before
   * this, the dashboard (including the full-screen Critical banner) was
   * already initialised by the time the dialog appeared, and the semi-
   * transparent backdrop let a nurse see live patient data while still being
   * forced through the password step. */
  function promptChangePassword({ forced = false } = {}) {
    /* One dialog at a time. Two stacked dialogs look identical, so typing goes
     * into the top one while the one underneath stays behind it - the user
     * appears to change their password and nothing happens. */
    const existing = document.querySelector(".modal-backdrop[data-dialog='password']");
    if (existing) return existing._donePromise || Promise.resolve();

    let resolveDone;
    const done = new Promise((resolve) => { resolveDone = resolve; });

    const backdrop = document.createElement("div");
    backdrop.dataset.dialog = "password";
    backdrop._donePromise = done;
    backdrop.className = "modal-backdrop";
    backdrop.innerHTML = `
      <form class="modal-card" id="pwForm">
        <h3>${forced ? "Set a new password" : "Change password"}</h3>
        ${forced ? `<p class="muted" style="font-size:12.5px; margin:0 0 6px;">
          Your current password was issued by an administrator. Choose your own before using the console.
        </p>` : ""}
        <label for="pwCurrent">Current password</label>
        <input type="password" id="pwCurrent" autocomplete="current-password" required>
        <label for="pwNew">New password (at least 8 characters)</label>
        <input type="password" id="pwNew" autocomplete="new-password" minlength="8" required>
        <label for="pwConfirm">Confirm new password</label>
        <input type="password" id="pwConfirm" autocomplete="new-password" minlength="8" required>
        <div class="form-error" id="pwError"></div>
        <div class="modal-actions">
          ${forced ? "" : `<button type="button" class="btn" id="pwCancel">Cancel</button>`}
          <button type="submit" class="btn primary">Save</button>
        </div>
      </form>`;
    document.body.appendChild(backdrop);

    const errorBox = backdrop.querySelector("#pwError");
    backdrop.querySelector("#pwCancel")?.addEventListener("click", () => {
      backdrop.remove();
      resolveDone();
    });

    backdrop.querySelector("#pwForm").addEventListener("submit", async (e) => {
      e.preventDefault();
      const next = backdrop.querySelector("#pwNew").value;
      if (next !== backdrop.querySelector("#pwConfirm").value) {
        errorBox.textContent = "The two new passwords do not match";
        errorBox.style.display = "block";
        return;
      }
      try {
        await Api.changePassword(backdrop.querySelector("#pwCurrent").value, next);
        if (me) me.mustChangePassword = false;
        backdrop.remove();
        UiUtils.toast("Password changed");
        resolveDone();
      } catch (err) {
        errorBox.textContent = err.message;
        errorBox.style.display = "block";
      }
    });

    return done;
  }

  async function logout() {
    await fetch("/api/auth/logout", { method: "POST", credentials: "same-origin" });
    location.href = "/login.html";
  }

  return {
    CAP,
    ROLE_LABEL,
    load,
    can,
    applyTo,
    renderIdentity,
    promptChangePassword,
    logout,
    get user() { return me; }
  };
})();
