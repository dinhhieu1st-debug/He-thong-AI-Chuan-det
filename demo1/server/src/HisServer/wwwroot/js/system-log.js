const SystemLogTab = (() => {
  let page = 1;
  const pageSize = 80;
  let currentItems = [];
  let totalCount = 0;
  let searchTerm = "";

  function currentFilters() {
    const type = document.getElementById("logTypeFilter").value;
    const from = document.getElementById("logFromDate").value;
    const to = document.getElementById("logToDate").value;
    const params = { page: String(page), pageSize: String(pageSize) };
    if (type) params.type = type;
    if (from) params.from = new Date(from).toISOString();
    if (to) params.to = new Date(to + "T23:59:59").toISOString();
    return params;
  }

  async function load() {
    const result = await Api.getLogs(currentFilters());
    currentItems = result.items;
    totalCount = result.totalCount;
    render();
  }

  function visibleItems() {
    if (!searchTerm) return currentItems;
    const term = searchTerm.toLowerCase();
    return currentItems.filter((entry) => `${entry.bedId} ${entry.room || ""}`.toLowerCase().includes(term));
  }

  function rowHtml(entry) {
    const color = UiUtils.statusColor(entry.level);
    return `
      <tr>
        <td>${UiUtils.formatDateTime(entry.occurredAt)}</td>
        <td>${UiUtils.escapeHtml(entry.category)}</td>
        <td>${UiUtils.escapeHtml(entry.bedId)}</td>
        <td>${UiUtils.escapeHtml(entry.room || "--")}</td>
        <td><span class="status-chip" style="background:${color};">${UiUtils.escapeHtml(entry.level)}</span></td>
        <td>${UiUtils.escapeHtml(entry.message)}</td>
      </tr>`;
  }

  function render() {
    const items = visibleItems();
    document.getElementById("logTableBody").innerHTML = items.length === 0
      ? `<tr><td colspan="6" class="empty-state">No log entries match the current filters.</td></tr>`
      : items.map(rowHtml).join("");

    const totalPages = Math.max(1, Math.ceil(totalCount / pageSize));
    document.getElementById("logPagination").innerHTML = `
      <button class="btn small" id="logPrev" ${page <= 1 ? "disabled" : ""}>Prev</button>
      <span>Page ${page} / ${totalPages} (${totalCount} total)</span>
      <button class="btn small" id="logNext" ${page >= totalPages ? "disabled" : ""}>Next</button>`;

    document.getElementById("logPrev")?.addEventListener("click", () => { page = Math.max(1, page - 1); load(); });
    document.getElementById("logNext")?.addEventListener("click", () => { page += 1; load(); });
  }

  function init() {
    // Default to the last 24h - an unbounded query against months of vitals
    // history is both slow and useless as a first screen; the date inputs
    // stay fully editable for anyone who needs to look further back.
    const to = new Date();
    const from = new Date(to.getTime() - 24 * 60 * 60 * 1000);
    document.getElementById("logFromDate").value = from.toISOString().slice(0, 10);
    document.getElementById("logToDate").value = to.toISOString().slice(0, 10);

    const persisted = UiUtils.loadFilterState("systemLog", { type: "" });
    document.getElementById("logTypeFilter").value = persisted.type;

    document.getElementById("logSearch").addEventListener("input", (e) => {
      searchTerm = e.target.value;
      render();
    });

    document.getElementById("logTypeFilter").addEventListener("change", (e) => {
      UiUtils.saveFilterState("systemLog", { type: e.target.value });
      page = 1; load();
    });
    document.getElementById("logFromDate").addEventListener("change", () => { page = 1; load(); });
    document.getElementById("logToDate").addEventListener("change", () => { page = 1; load(); });
    document.getElementById("logRefreshBtn").addEventListener("click", () => load());

    document.getElementById("logExportBtn").addEventListener("click", () => {
      const params = { ...currentFilters() };
      delete params.page;
      delete params.pageSize;
      window.location.href = Api.exportLogsUrl(params);
    });

    load();
  }

  return { init, render: load };
})();
