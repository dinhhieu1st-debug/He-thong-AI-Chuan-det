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
        <input class="search-input" id="newBedId" placeholder="New bed id, e.g. BED-104" style="max-width:220px;">
        <input class="search-input" id="newBedRoom" placeholder="Room, e.g. ICU-1" style="max-width:180px;">
        <button class="btn primary" id="createBedBtn">Add bed</button>
      </div>

      ${beds.length === 0
        ? `<div class="empty-state">No beds yet. Add the first one above.</div>`
        : `<table class="data-table">
             <thead><tr><th>Bed</th><th>Room</th><th>Device</th><th>Occupancy</th><th></th></tr></thead>
             <tbody>${beds.map(rowHtml).join("")}</tbody>
           </table>`}`;

    document.getElementById("createBedBtn")?.addEventListener("click", async () => {
      const bedId = document.getElementById("newBedId").value.trim();
      const room = document.getElementById("newBedRoom").value.trim();
      if (!bedId) {
        UiUtils.toast("Enter a bed id", true);
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
        if (!confirm(`Remove bed ${bedId} from the ward directory?\n\n`
                   + `Past vitals and alerts recorded for this bed are kept — `
                   + `they are the clinical record and are not deleted.\n\n`
                   + `A bed that still has a patient admitted, or a device `
                   + `assigned to it, cannot be removed.`)) return;

        await act(() => Api.deleteBed(bedId), `${bedId} removed`);
      });
    });

    host.querySelectorAll("[data-edit-room]").forEach((btn) => {
      btn.addEventListener("click", async () => {
        const bedId = btn.getAttribute("data-edit-room");
        const room = prompt(`Room for ${bedId}:`, btn.getAttribute("data-room"));
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
