/* User administration: accounts, roles, and the duty roster.
 *
 * Admin-only. The tab itself is removed for everyone else by Session.applyTo(),
 * and every call behind it is refused by the server as well - the removal is
 * so nobody is offered a control that would only 403. */
const UsersTab = (() => {
  let users = [];
  let selected = null;         // { user, assignments }
  let error = null;

  async function load() {
    try {
      users = await Api.getUsers();
      error = null;
    } catch (err) {
      error = err.message;
    }
    render();
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
        <td><b>${UiUtils.escapeHtml(u.username)}</b>${u.isActive ? "" : ` <span class="muted">(đã khoá)</span>`}</td>
        <td>${UiUtils.escapeHtml(u.fullName || "—")}</td>
        <td>${rolePill(u.role)}</td>
        <td class="muted">${u.lastLoginAt ? UiUtils.formatDateTime(u.lastLoginAt) : "chưa đăng nhập"}</td>
        <td>${u.mustChangePassword ? `<span class="muted">chờ đổi mật khẩu</span>` : ""}</td>
      </tr>`;
  }

  function detailHtml() {
    if (!selected) {
      return `<div class="empty-state">Chọn một tài khoản để xem và phân công.</div>`;
    }
    const u = selected.user;
    const rows = selected.assignments.map((a) => `
      <tr>
        <td>${a.scopeType === "ROOM" ? "Phòng" : "Giường"}</td>
        <td><b>${UiUtils.escapeHtml(a.scopeValue)}</b></td>
        <td style="text-align:right;">
          <button class="btn danger" data-remove-assignment="${a.assignmentId}">Bỏ</button>
        </td>
      </tr>`).join("");

    return `
      <div class="panel-card">
        <div class="panel-card-head">
          <h3>${UiUtils.escapeHtml(u.fullName || u.username)}</h3>
          ${rolePill(u.role)}
        </div>
        <div class="kv"><span>Tài khoản</span><b>${UiUtils.escapeHtml(u.username)}</b></div>
        <div class="kv"><span>Trạng thái</span><b>${u.isActive ? "Đang hoạt động" : "Đã khoá"}</b></div>
        <div class="kv"><span>Tạo lúc</span><b>${UiUtils.formatDateTime(u.createdAt)}</b></div>

        <div style="display:flex; gap:8px; margin-top:12px; flex-wrap:wrap;">
          <select class="search-input" id="userRoleSelect" style="max-width:180px;">
            <option value="NURSE"${u.role === "NURSE" ? " selected" : ""}>Điều dưỡng</option>
            <option value="TECHNICIAN"${u.role === "TECHNICIAN" ? " selected" : ""}>Kỹ thuật viên</option>
            <option value="ADMIN"${u.role === "ADMIN" ? " selected" : ""}>Quản trị viên</option>
          </select>
          <button class="btn" id="saveRoleBtn">Lưu vai trò</button>
          <button class="btn" id="toggleActiveBtn">${u.isActive ? "Khoá tài khoản" : "Mở khoá"}</button>
          <button class="btn" id="resetPwBtn">Đặt lại mật khẩu</button>
          <button class="btn danger" id="deleteUserBtn">Xoá</button>
        </div>
      </div>

      <div class="section-title">Phân công phụ trách</div>
      <div class="toolbar">
        <select class="search-input" id="assignScope" style="max-width:140px;">
          <option value="ROOM">Phòng</option>
          <option value="BED">Giường</option>
        </select>
        <input class="search-input" id="assignValue" placeholder="ICU-1 hoặc BED-101" style="max-width:220px;">
        <button class="btn primary" id="addAssignBtn">Thêm phân công</button>
      </div>
      ${selected.assignments.length === 0
        ? `<div class="empty-state">Chưa phân công phòng hoặc giường nào.</div>`
        : `<table class="data-table">
             <thead><tr><th>Phạm vi</th><th>Giá trị</th><th></th></tr></thead>
             <tbody>${rows}</tbody>
           </table>`}`;
  }

  function render() {
    const host = document.getElementById("tab-users");
    if (!host) return;

    if (error) {
      host.innerHTML = `<div class="empty-state">Không tải được danh sách người dùng: ${UiUtils.escapeHtml(error)}</div>`;
      return;
    }

    host.innerHTML = `
      <div class="toolbar">
        <button class="btn primary" id="addUserBtn">+ Thêm tài khoản</button>
      </div>
      <div class="split">
        <div class="main-col">
          <table class="data-table">
            <thead><tr><th>Tài khoản</th><th>Họ tên</th><th>Vai trò</th><th>Đăng nhập lần cuối</th><th></th></tr></thead>
            <tbody>${users.map(userRow).join("")}</tbody>
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

    if (!selected) return;
    const userId = selected.user.userId;

    document.getElementById("saveRoleBtn")?.addEventListener("click", async () => {
      const role = document.getElementById("userRoleSelect").value;
      await guard(() => Api.updateUser(userId, { role }), "Đã cập nhật vai trò");
    });

    document.getElementById("toggleActiveBtn")?.addEventListener("click", async () => {
      await guard(() => Api.updateUser(userId, { isActive: !selected.user.isActive }),
                  "Đã cập nhật trạng thái tài khoản");
    });

    document.getElementById("resetPwBtn")?.addEventListener("click", async () => {
      /* The administrator types the new password and passes it on out of band.
       * It is marked "must change", so the owner replaces it at next login and
       * the administrator's copy stops being valid. */
      const next = prompt("Mật khẩu mới (tối thiểu 8 ký tự):");
      if (!next) return;
      await guard(() => Api.resetUserPassword(userId, next),
                  "Đã đặt lại mật khẩu. Người dùng phải đổi khi đăng nhập.");
    });

    document.getElementById("deleteUserBtn")?.addEventListener("click", async () => {
      if (!confirm(`Xoá tài khoản '${selected.user.username}'? Thao tác này không hoàn tác được.`)) return;
      await guard(async () => {
        await Api.deleteUser(userId);
        selected = null;
      }, "Đã xoá tài khoản");
    });

    document.getElementById("addAssignBtn")?.addEventListener("click", async () => {
      const scope = document.getElementById("assignScope").value;
      const value = document.getElementById("assignValue").value.trim();
      if (!value) return;
      await guard(() => Api.addAssignment(userId, scope, value), "Đã thêm phân công");
    });

    document.querySelectorAll("[data-remove-assignment]").forEach((btn) => {
      btn.addEventListener("click", async () => {
        await guard(() => Api.removeAssignment(btn.getAttribute("data-remove-assignment")),
                    "Đã bỏ phân công");
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
        <h3>Thêm tài khoản</h3>
        <label for="nuUsername">Tên đăng nhập</label>
        <input id="nuUsername" required minlength="3" autocomplete="off">
        <label for="nuFullName">Họ và tên</label>
        <input id="nuFullName" autocomplete="off">
        <label for="nuRole">Vai trò</label>
        <select id="nuRole">
          <option value="NURSE">Điều dưỡng</option>
          <option value="TECHNICIAN">Kỹ thuật viên</option>
          <option value="ADMIN">Quản trị viên</option>
        </select>
        <label for="nuPassword">Mật khẩu ban đầu (tối thiểu 8 ký tự)</label>
        <input type="password" id="nuPassword" required minlength="8" autocomplete="new-password">
        <div class="form-error" id="nuError"></div>
        <div class="modal-actions">
          <button type="button" class="btn" id="nuCancel">Huỷ</button>
          <button type="submit" class="btn primary">Tạo</button>
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
        UiUtils.toast("Đã tạo tài khoản. Người dùng phải đổi mật khẩu khi đăng nhập.");
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
