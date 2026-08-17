/* "Ca trực của tôi" - the shift view.
 *
 * Answers the question a nurse actually has at handover: which beds am I
 * responsible for right now, who is in them, and is any of them in trouble.
 * The roster does not restrict anything - every bed stays reachable from the
 * ward tabs - so this page is a shortcut, not a wall. */
const ProfileTab = (() => {
  const REFRESH_MS = 10000;

  let data = null;
  let error = null;
  let timer = null;

  async function load() {
    try {
      data = await Api.getMyAssignment();
      error = null;
    } catch (err) {
      error = err.message;
    }
    render();
  }

  function statusChip(status) {
    const color = UiUtils.statusColor(status);
    return `<span class="status-chip" style="background:${color};">${UiUtils.escapeHtml((status || "").toUpperCase())}</span>`;
  }

  function bedRow(bed) {
    const patient = bed.patientName
      ? `${UiUtils.escapeHtml(bed.patientName)}${bed.patientCode ? ` <span class="muted">· ${UiUtils.escapeHtml(bed.patientCode)}</span>` : ""}`
      : `<span class="muted">Giường trống</span>`;

    return `
      <tr data-bed-id="${UiUtils.escapeHtml(bed.bedId)}" class="profile-bed-row">
        <td><b>${UiUtils.escapeHtml(bed.bedId)}</b><div class="muted">${UiUtils.escapeHtml(bed.room)}</div></td>
        <td>${patient}</td>
        <td>${statusChip(bed.status)}</td>
        <td>${bed.alertMessage ? UiUtils.escapeHtml(bed.alertMessage.split(" · ")[0]) : "<span class='muted'>—</span>"}</td>
        <td class="muted">${bed.lastUpdated ? UiUtils.formatDateTime(bed.lastUpdated) : "—"}</td>
      </tr>`;
  }

  function render() {
    const host = document.getElementById("tab-profile");
    if (!host) return;

    if (error) {
      host.innerHTML = `<div class="empty-state">Không tải được thông tin ca trực: ${UiUtils.escapeHtml(error)}</div>`;
      return;
    }
    if (!data) {
      host.innerHTML = `<div class="empty-state">Đang tải…</div>`;
      return;
    }

    const u = data.user;
    const s = data.summary;
    const scope = [
      ...data.rooms.map((r) => `<span class="chip active">Phòng ${UiUtils.escapeHtml(r)}</span>`),
      ...data.individualBeds.map((b) => `<span class="chip active">${UiUtils.escapeHtml(b)}</span>`)
    ].join(" ");

    /* An empty roster is a normal state (nobody has been assigned yet), not an
     * error - say so plainly and point at who can fix it, rather than showing
     * an empty table that reads like a bug. */
    const scopeHtml = scope || `<span class="muted">Chưa được phân công phòng/giường nào —
      quản trị viên gán trong tab Người dùng.</span>`;

    host.innerHTML = `
      <div class="panel-card">
        <div class="panel-card-head">
          <h3>${UiUtils.escapeHtml(u.fullName || u.username)}</h3>
          <span class="chip active">${UiUtils.escapeHtml(Session.ROLE_LABEL[u.role] || u.role)}</span>
        </div>
        <div class="kv"><span>Tài khoản</span><b>${UiUtils.escapeHtml(u.username)}</b></div>
        <div class="kv"><span>Đăng nhập lần cuối</span><b>${u.lastLoginAt ? UiUtils.formatDateTime(u.lastLoginAt) : "—"}</b></div>
        <div class="kv"><span>Phạm vi phụ trách</span><div>${scopeHtml}</div></div>
        <button type="button" class="btn" id="changePasswordBtn" style="margin-top:12px;">Đổi mật khẩu</button>
      </div>

      <div class="stat-row" style="margin-top:18px;">
        <div class="stat-tile"><div class="value">${s.total}</div><div class="label">Giường phụ trách</div></div>
        <div class="stat-tile"><div class="value">${s.occupied}</div><div class="label">Đang có bệnh nhân</div></div>
        <div class="stat-tile critical"><div class="value">${s.critical}</div><div class="label">Nguy kịch</div></div>
        <div class="stat-tile warning"><div class="value">${s.warning}</div><div class="label">Cảnh báo</div></div>
      </div>

      <div class="section-title">Giường của tôi</div>
      ${data.beds.length === 0
        ? `<div class="empty-state">Chưa có giường nào trong phạm vi phụ trách.</div>`
        : `<table class="data-table">
             <thead><tr><th>Giường</th><th>Bệnh nhân</th><th>Trạng thái</th><th>Cảnh báo</th><th>Cập nhật</th></tr></thead>
             <tbody>${data.beds.map(bedRow).join("")}</tbody>
           </table>`}`;

    document.getElementById("changePasswordBtn")
      ?.addEventListener("click", () => Session.promptChangePassword());

    // Clicking a row opens the same bed detail the ward tabs use.
    host.querySelectorAll(".profile-bed-row").forEach((row) => {
      row.style.cursor = "pointer";
      row.addEventListener("click", () => {
        // The detail panel lives inside the Beds tab, so switch there first -
        // same two steps the dashboard cards and the critical banner use.
        document.querySelector('.nav-btn[data-tab="beds"]').click();
        BedsTab.openBed(row.getAttribute("data-bed-id"));
      });
    });
  }

  /* Only polled while the tab is actually open: the shift roster changes when
   * an administrator edits it, not every second, and the bed states on it are
   * already pushed live to the tabs that show vitals. */
  function activate() {
    load();
    stop();
    timer = setInterval(load, REFRESH_MS);
  }

  function stop() {
    if (timer) { clearInterval(timer); timer = null; }
  }

  return { activate, stop, render };
})();
