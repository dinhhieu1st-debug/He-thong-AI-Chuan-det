/* User administration: accounts, roles, and the duty roster.
 *
 * Admin-only. The tab itself is removed for everyone else by Session.applyTo(),
 * and every call behind it is refused by the server as well - the removal is
 * so nobody is offered a control that would only 403. */
const UsersTab = (() => {
  let users = [];
  let selected = null;         // { user, assignments }
  let error = null;
  let searchTerm = "";

  /* Rooms/beds for the assignment picker, loaded once alongside the user
   * list. A room is not its own row anywhere in this system - it only
   * exists as a value on a bed - so the room list here is built from the
   * same directory the admin's bed tab uses, not typed by hand a second
   * time where a typo would silently create an assignment nobody's roster
   * ever matches. */
  let bedDirectory = [];

  async function loadBedDirectory() {
    try {
      bedDirectory = await Api.getBedDirectory();
    } catch {
      bedDirectory = [];
    }
  }

  function matchesFilters(u) {
    if (!searchTerm) return true;
    const haystack = `${u.username} ${u.fullName || ""}`.toLowerCase();
    return haystack.includes(searchTerm.toLowerCase());
  }

  async function load() {
    try {
      users = await Api.getUsers();
      error = null;
    } catch (err) {
      error = err.message;
    }
    await loadBedDirectory();
    render();
  }

  function assignValueOptionsHtml(scope) {
    const values = scope === "ROOM"
      ? Array.from(new Set(bedDirectory.map((b) => b.room).filter(Boolean))).sort()
      : bedDirectory.map((b) => b.bedId).sort();

    if (values.length === 0) {
      return `<option value="">No ${scope === "ROOM" ? "rooms" : "beds"} in the directory yet</option>`;
    }
    return values.map((v) => `<option value="${UiUtils.escapeHtml(v)}">${UiUtils.escapeHtml(v)}</option>`).join("");
  }

  async function select(userId) {
    selected = await Api.getUser(userId);
    render();
  }

  function rolePill(role) {
    const label = Session.ROLE_LABEL[role] || role;
    const cls = role === "ADMIN" ? "admin" : role === "TECHNICIAN" ? "technician" : "";
    return `<span class="role-pill ${cls}">${UiUtils.escapeHtml(label)}</span>`;
  }

  function userRow(u) {
    return `
      <tr data-user-id="${u.userId}" class="user-row" style="cursor:pointer;">
        <td><b>${UiUtils.escapeHtml(u.username)}</b>${u.isActive ? "" : ` <span class="muted">(disabled)</span>`}</td>
        <td>${UiUtils.escapeHtml(u.fullName || "—")}</td>
        <td>${rolePill(u.role)}</td>
        <td class="muted">${u.lastLoginAt ? UiUtils.formatDateTime(u.lastLoginAt) : "never signed in"}</td>
        <td>${u.mustChangePassword ? `<span class="muted">must change password</span>` : ""}</td>
      </tr>`;
  }

  function detailHtml() {
    if (!selected) {
      return `<div class="empty-state">Select an account to view and assign.</div>`;
    }
    const u = selected.user;
    const rows = selected.assignments.map((a) => `
      <tr>
        <td>${a.scopeType === "ROOM" ? "Room" : "Bed"}</td>
        <td><b>${UiUtils.escapeHtml(a.scopeValue)}</b></td>
        <td style="text-align:right;">
          <button class="btn danger" data-remove-assignment="${a.assignmentId}">Remove</button>
        </td>
      </tr>`).join("");
    const dutyAssignmentsHtml = u.role === "NURSE" ? `
      <div class="section-title">Duty assignments</div>
      <div class="toolbar">
        <select class="search-input" id="assignScope" style="max-width:140px;">
          <option value="ROOM">Room</option>
          <option value="BED">Bed</option>
        </select>
        <select class="search-input" id="assignValue" style="max-width:220px;">
          ${assignValueOptionsHtml("ROOM")}
        </select>
        <button class="btn primary" id="addAssignBtn">Add assignment</button>
      </div>
      ${selected.assignments.length === 0
        ? `<div class="empty-state">No rooms or beds assigned yet.</div>`
        : `<table class="data-table">
             <thead><tr><th>Scope</th><th>Value</th><th></th></tr></thead>
             <tbody>${rows}</tbody>
           </table>`}` : "";

    return `
      <div class="panel-card">
        <div class="panel-card-head">
          <h3>${UiUtils.escapeHtml(u.fullName || u.username)}</h3>
          ${rolePill(u.role)}
        </div>
        <div class="kv"><span>Account</span><b>${UiUtils.escapeHtml(u.username)}</b></div>
        <div class="kv"><span>Status</span><b>${u.isActive ? "Active" : "Disabled"}</b></div>
        <div class="kv"><span>Created</span><b>${UiUtils.formatDateTime(u.createdAt)}</b></div>

        <div style="display:flex; gap:8px; margin-top:12px; flex-wrap:wrap;">
          <select class="search-input" id="userRoleSelect" style="max-width:180px;">
            <option value="NURSE"${u.role === "NURSE" ? " selected" : ""}>Nurse</option>
            <option value="TECHNICIAN"${u.role === "TECHNICIAN" ? " selected" : ""}>Technician</option>
            <option value="ADMIN"${u.role === "ADMIN" ? " selected" : ""}>Administrator</option>
          </select>
          <button class="btn" id="saveRoleBtn">Save role</button>
          <button class="btn" id="toggleActiveBtn">${u.isActive ? "Disable account" : "Enable"}</button>
          <button class="btn" id="resetPwBtn">Reset password</button>
          <button class="btn danger" id="deleteUserBtn">Delete</button>
        </div>
      </div>

      ${dutyAssignmentsHtml}`;
  }

  function render() {
    const host = document.getElementById("tab-users");
    if (!host) return;

    if (error) {
      host.innerHTML = `<div class="empty-state">Could not load users: ${UiUtils.escapeHtml(error)}</div>`;
      return;
    }

    const visible = users.filter(matchesFilters);

    host.innerHTML = `
      <div class="toolbar">
        <input class="search-input" id="userSearch" placeholder="Search account or name..." value="${UiUtils.escapeHtml(searchTerm)}">
        <span class="muted">Showing ${visible.length} of ${users.length}</span>
        <button class="btn primary" id="addUserBtn">+ Add account</button>
      </div>
      <div class="split">
        <div class="main-col">
          <table class="data-table">
            <thead><tr><th>Account</th><th>Full name</th><th>Role</th><th>Last sign-in</th><th></th></tr></thead>
            <tbody>${visible.length === 0
              ? `<tr><td colspan="5" class="empty-state">No accounts match the current search.</td></tr>`
              : visible.map(userRow).join("")}</tbody>
          </table>
        </div>
        <div style="flex:1; min-width:320px;">${detailHtml()}</div>
      </div>`;

    bind();
  }

  function bind() {
    document.querySelectorAll(".user-row").forEach((row) => {
      row.addEventListener("click", () => select(parseInt(row.getAttribute("data-user-id"), 10)));
    });

    document.getElementById("addUserBtn")?.addEventListener("click", promptCreate);

    document.getElementById("userSearch")?.addEventListener("input", (e) => {
      searchTerm = e.target.value;
      render();
      const input = document.getElementById("userSearch");
      if (input) { input.focus(); input.setSelectionRange(input.value.length, input.value.length); }
    });

    if (!selected) return;
    const userId = selected.user.userId;

    document.getElementById("saveRoleBtn")?.addEventListener("click", async () => {
      const role = document.getElementById("userRoleSelect").value;
      await guard(() => Api.updateUser(userId, { role }), "Role updated");
    });

    document.getElementById("toggleActiveBtn")?.addEventListener("click", async () => {
      await guard(() => Api.updateUser(userId, { isActive: !selected.user.isActive }),
                  "Account status updated");
    });

    document.getElementById("resetPwBtn")?.addEventListener("click", async () => {
      /* The administrator types the new password and passes it on out of band.
       * It is marked "must change", so the owner replaces it at next login and
       * the administrator's copy stops being valid. */
      const next = await UiUtils.prompt("New password (at least 8 characters):",
        { title: "Reset password", inputType: "password", placeholder: "New password", confirmLabel: "Reset" });
      if (!next) return;
      await guard(() => Api.resetUserPassword(userId, next),
                  "Password reset. The user must change it at next sign-in.");
    });

    document.getElementById("deleteUserBtn")?.addEventListener("click", async () => {
      if (!(await UiUtils.confirm(`Delete account '${selected.user.username}'? This cannot be undone.`,
            { title: "Delete account?", confirmLabel: "Delete", danger: true }))) return;
      await guard(async () => {
        await Api.deleteUser(userId);
        selected = null;
      }, "Account deleted");
    });

    document.getElementById("assignScope")?.addEventListener("change", (e) => {
      document.getElementById("assignValue").innerHTML = assignValueOptionsHtml(e.target.value);
    });

    document.getElementById("addAssignBtn")?.addEventListener("click", async () => {
      const scope = document.getElementById("assignScope").value;
      const value = document.getElementById("assignValue").value;
      if (!value) return;
      await guard(() => Api.addAssignment(userId, scope, value), "Assignment added");
    });

    document.querySelectorAll("[data-remove-assignment]").forEach((btn) => {
      btn.addEventListener("click", async () => {
        await guard(() => Api.removeAssignment(btn.getAttribute("data-remove-assignment")),
                    "Assignment removed");
      });
    });
  }

  /* Every mutation refreshes both lists afterwards, so the screen can never
   * show a stale role next to a changed one. */
  async function guard(action, successMessage) {
    try {
      await action();
      UiUtils.toast(successMessage);
      const keep = selected?.user.userId;
      await load();
      if (keep && users.some((u) => u.userId === keep)) await select(keep);
    } catch (err) {
      UiUtils.toast(err.message, true);
    }
  }

  function promptCreate() {
    const backdrop = document.createElement("div");
    backdrop.className = "modal-backdrop";
    backdrop.innerHTML = `
      <form class="modal-card" id="newUserForm">
        <h3>Add account</h3>
        <label for="nuUsername">Username</label>
        <input id="nuUsername" required minlength="3" autocomplete="off">
        <label for="nuFullName">Full name</label>
        <input id="nuFullName" autocomplete="off">
        <label for="nuRole">Role</label>
        <select id="nuRole">
          <option value="NURSE">Nurse</option>
          <option value="TECHNICIAN">Technician</option>
          <option value="ADMIN">Administrator</option>
        </select>
        <label for="nuPassword">Initial password (at least 8 characters)</label>
        <input type="password" id="nuPassword" required minlength="8" autocomplete="new-password">
        <div class="form-error" id="nuError"></div>
        <div class="modal-actions">
          <button type="button" class="btn" id="nuCancel">Cancel</button>
          <button type="submit" class="btn primary">Create</button>
        </div>
      </form>`;
    document.body.appendChild(backdrop);

    backdrop.querySelector("#nuCancel").addEventListener("click", () => backdrop.remove());
    backdrop.querySelector("#newUserForm").addEventListener("submit", async (e) => {
      e.preventDefault();
      const errorBox = backdrop.querySelector("#nuError");
      try {
        await Api.createUser({
          username: backdrop.querySelector("#nuUsername").value.trim(),
          fullName: backdrop.querySelector("#nuFullName").value.trim(),
          role: backdrop.querySelector("#nuRole").value,
          password: backdrop.querySelector("#nuPassword").value
        });
        backdrop.remove();
        UiUtils.toast("Account created. The user must change the password at first sign-in.");
        await load();
      } catch (err) {
        errorBox.textContent = err.message;
        errorBox.style.display = "block";
      }
    });
  }

  function activate() {
    load();
  }

  return { activate, render };
})();
