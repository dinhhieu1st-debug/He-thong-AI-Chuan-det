/* Bed directory — the administrator's view of the ward's structure.
 *
 * Which beds exist, in which room, with which device, and whether they are
 * occupied. No vitals and no patient names: an administrator builds and
 * maintains the ward, they do not monitor patients in it. "Occupied yes/no" is
 * the least that still makes the page useful for spotting a bed that was never
 * set up. */
const DirectoryTab = (() => {
  let beds = [];
  let error = null;
  let searchTerm = "";

  function matchesFilters(b) {
    if (!searchTerm) return true;
    const haystack = `${b.bedId} ${b.room || ""} ${b.deviceId || ""}`.toLowerCase();
    return haystack.includes(searchTerm.toLowerCase());
  }

  async function load() {
    try {
      beds = await Api.getBedDirectory();
      error = null;
    } catch (err) {
      error = err.message;
    }
    render();
  }

  function rowHtml(b) {
    return `
      <tr>
        <td><b>${UiUtils.escapeHtml(b.bedId)}</b></td>
        <td>${UiUtils.escapeHtml(b.room || "—")}</td>
        <td>${b.deviceId
              ? UiUtils.escapeHtml(b.deviceId)
              : `<span class="muted">no device assigned</span>`}</td>
        <td>${b.hasPatient
              ? "Occupied"
              : `<span class="muted">Empty</span>`}</td>
        <td style="text-align:right;">
          <button class="btn" data-edit-room="${UiUtils.escapeHtml(b.bedId)}"
                  data-room="${UiUtils.escapeHtml(b.room || "")}">Change room</button>
          <button class="btn" data-delete-bed="${UiUtils.escapeHtml(b.bedId)}">Delete</button>
        </td>
      </tr>`;
  }

  function render() {
    const host = document.getElementById("tab-directory");
    if (!host) return;

    if (error) {
      host.innerHTML = `<div class="empty-state">Could not load the bed directory: ${UiUtils.escapeHtml(error)}</div>`;
      return;
    }

    const rooms = new Set(beds.map((b) => b.room).filter(Boolean));

    host.innerHTML = `
      <div class="stat-row">
        <div class="stat-tile"><div class="value">${beds.length}</div><div class="label">Beds</div></div>
        <div class="stat-tile"><div class="value">${rooms.size}</div><div class="label">Rooms</div></div>
        <div class="stat-tile"><div class="value">${beds.filter((b) => b.deviceId).length}</div><div class="label">With a device</div></div>
        <div class="stat-tile"><div class="value">${beds.filter((b) => b.hasPatient).length}</div><div class="label">Occupied</div></div>
      </div>

      <div class="toolbar">
        <input class="search-input" id="directorySearch" placeholder="Search bed, room or device..." value="${UiUtils.escapeHtml(searchTerm)}">
      </div>
      <div class="toolbar">
        <input class="search-input" id="newBedId" placeholder="New bed id, e.g. BED-104" style="max-width:220px;">
        <select class="search-input" id="newBedRoomSelect" style="max-width:180px;">
          ${Array.from(rooms).sort().map((r) =>
            `<option value="${UiUtils.escapeHtml(r)}">${UiUtils.escapeHtml(r)}</option>`).join("")}
          <option value="__new__">+ New room…</option>
        </select>
        <input class="search-input" id="newBedRoomNew" placeholder="New room name" style="max-width:160px; display:none;">
        <button class="btn primary" id="createBedBtn">Add bed</button>
      </div>

      ${(() => {
        const visible = beds.filter(matchesFilters);
        if (beds.length === 0) return `<div class="empty-state">No beds yet. Add the first one above.</div>`;
        if (visible.length === 0) return `<div class="empty-state">No beds match the current search.</div>`;
        return `<table class="data-table">
             <thead><tr><th>Bed</th><th>Room</th><th>Device</th><th>Occupancy</th><th></th></tr></thead>
             <tbody>${visible.map(rowHtml).join("")}</tbody>
           </table>`;
      })()}`;

    document.getElementById("directorySearch")?.addEventListener("input", (e) => {
      searchTerm = e.target.value;
      render();
      const input = document.getElementById("directorySearch");
      if (input) { input.focus(); input.setSelectionRange(input.value.length, input.value.length); }
    });

    /* Pick an existing room from the dropdown, or "+ New room..." to type
     * one - a room is not its own row anywhere in this system, it only
     * exists as a value on a bed, so this select IS the room list, built
     * from what beds already have rather than a second thing to keep in
     * sync. Retyping "ICU-1" by hand for every new bed is exactly what let
     * "ICU-1" and "Icu-1" both exist as two different rooms before. */
    const roomSelect = document.getElementById("newBedRoomSelect");
    const roomCustom = document.getElementById("newBedRoomNew");
    function syncRoomCustomVisibility() {
      const isNew = roomSelect.value === "__new__";
      roomCustom.style.display = isNew ? "" : "none";
      if (isNew) roomCustom.focus();
    }
    roomSelect?.addEventListener("change", syncRoomCustomVisibility);
    syncRoomCustomVisibility();

    document.getElementById("createBedBtn")?.addEventListener("click", async () => {
      const bedId = document.getElementById("newBedId").value.trim();
      const room = roomSelect.value === "__new__" ? roomCustom.value.trim() : roomSelect.value;
      if (!bedId) {
        UiUtils.toast("Enter a bed id", true);
        return;
      }
      if (!room) {
        UiUtils.toast("Choose or type a room", true);
        return;
      }
      await act(() => Api.createBed(bedId, room), `${bedId} created`);
    });

    host.querySelectorAll("[data-delete-bed]").forEach((btn) => {
      btn.addEventListener("click", async () => {
        const bedId = btn.getAttribute("data-delete-bed");

        /* The server refuses a bed that still has a patient or a device, so
         * this dialog is about the one case it does allow: an empty bed being
         * retired. It says what survives, because "delete" next to a hospital
         * bed reasonably makes people wonder whether the records go too. */
        if (!(await UiUtils.confirm(`Remove bed ${bedId} from the ward directory?\n\n`
                   + `Past vitals and alerts recorded for this bed are kept — `
                   + `they are the clinical record and are not deleted.\n\n`
                   + `A bed that still has a patient admitted, or a device `
                   + `assigned to it, cannot be removed.`,
                   { title: "Remove bed?", confirmLabel: "Remove", danger: true }))) return;

        await act(() => Api.deleteBed(bedId), `${bedId} removed`);
      });
    });

    host.querySelectorAll("[data-edit-room]").forEach((btn) => {
      btn.addEventListener("click", async () => {
        const bedId = btn.getAttribute("data-edit-room");
        const room = await UiUtils.prompt(`Room for ${bedId}:`,
          { title: "Change room", defaultValue: btn.getAttribute("data-room") || "", placeholder: "Room", confirmLabel: "Save" });
        if (room === null) return;
        await act(() => Api.updateBed(bedId, { room: room.trim() }), `${bedId} updated`);
      });
    });
  }

  async function act(action, message) {
    try {
      await action();
      UiUtils.toast(message);
      await load();
    } catch (err) {
      UiUtils.toast(err.message, true);
    }
  }

  function activate() {
    load();
  }

  return { activate, render };
})();
