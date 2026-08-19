/* Kiểm tra panel OTA ở trang kỹ thuật viên (wwwroot/js/devices.js).
 *
 *   node tools/ota_render_check.js        (chạy từ gốc repo)
 *
 * Rút hàm ra khỏi chính file đang chạy chứ không test một bản sao — bản sao sẽ
 * trôi khỏi file thật rồi pass mãi mãi trong khi trang thật đã hỏng.
 *
 * Các ca ở đây là những chỗ sai thì NGUY HIỂM chứ không chỉ xấu: nút Cập nhật
 * không được xuất hiện trong lúc đang nạp, và lúc nạp phải có dòng nhắc đừng
 * rút nguồn.
 */
const fs = require('fs');
const src = fs.readFileSync('server/src/HisServer/wwwroot/js/devices.js', 'utf8');

function extract(name) {
  const start = src.indexOf(`function ${name}(`);
  if (start < 0) throw new Error(`${name} không tìm thấy`);
  let depth = 0, i = src.indexOf('{', start);
  for (; i < src.length; i++) {
    if (src[i] === '{') depth++;
    else if (src[i] === '}' && --depth === 0) { i++; break; }
  }
  return src.slice(start, i);
}
const labelTable = src.slice(src.indexOf('const OTA_LABEL'),
                             src.indexOf('};', src.indexOf('const OTA_LABEL')) + 2);

const UiUtils = {
  escapeHtml: (t) => String(t == null ? "" : t)
    .replace(/[&<>"]/g, (c) => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c])),
};

const otaByDevice = new Map();
const code = `${labelTable}\n${extract('otaSectionHtml')}\nmodule.exports={otaSectionHtml};`;
const m = { exports: {} };
new Function('module', 'UiUtils', 'otaByDevice', code)(m, UiUtils, otaByDevice);
const { otaSectionHtml } = m.exports;

let fails = 0;
const want = (name, cond, detail) => {
  console.log(`  [${cond ? "PASS" : "FAIL"}] ${name.padEnd(56)} ${detail}`);
  if (!cond) fails++;
};

const dev = { deviceId: "0x64028ffffe641802" };
const set = (s) => { if (s) otaByDevice.set(dev.deviceId, s); else otaByDevice.delete(dev.deviceId); };

console.log("\n== Panel OTA ==");

set(null);
let html = otaSectionHtml(dev);
want("chưa kiểm tra thì vẫn có nút Kiểm tra", html.includes('data-ota-check'), "có nút");
want("nhưng KHÔNG có nút Cập nhật khi chưa biết có bản mới",
     !html.includes('data-ota-update'), "không có nút Cập nhật");

set({ deviceId: dev.deviceId, state: "UpToDate", inFlight: false });
html = otaSectionHtml(dev);
want("đang ở bản mới nhất thì không mời cập nhật",
     !html.includes('data-ota-update'), "không có nút Cập nhật");

set({ deviceId: dev.deviceId, state: "Available", inFlight: false });
html = otaSectionHtml(dev);
want("có bản mới thì hiện nút Cập nhật", html.includes('data-ota-update'), "có nút");

set({ deviceId: dev.deviceId, state: "Updating", inFlight: true, progress: 47, remainingSeconds: 120 });
html = otaSectionHtml(dev);
want("ĐANG NẠP: nút Cập nhật phải biến mất",
     !html.includes('data-ota-update'), "bấm hai lần = nửa firmware");
want("đang nạp: cũng không cho bấm Kiểm tra",
     !html.includes('data-ota-check'), "không có nút nào");
want("đang nạp: có thanh tiến độ đúng phần trăm",
     html.includes('width:47%'), "47%");
want("đang nạp: có nhắc đừng rút nguồn",
     html.includes('do not disconnect power'), "có cảnh báo");

set({ deviceId: dev.deviceId, state: "Starting", inFlight: true, progress: null });
html = otaSectionHtml(dev);
want("vừa bắt đầu, chưa có phần trăm: không bịa ra số",
     html.includes('preparing'), "nói 'preparing'");
want("vừa bắt đầu: nút Cập nhật cũng đã biến mất",
     !html.includes('data-ota-update'), "đã ẩn");

set({ deviceId: dev.deviceId, state: "Failed", inFlight: false, message: "No OTA cluster" });
html = otaSectionHtml(dev);
want("thất bại thì hiện lý do", html.includes('No OTA cluster'), "có lý do");
want("thất bại thì cho thử lại", html.includes('data-ota-check'), "có nút Kiểm tra");

console.log(fails === 0 ? "\nALL CHECKS PASSED  (0 failures)\n"
                        : `\nSOME CHECKS FAILED  (${fails})\n`);
process.exit(fails ? 1 : 0);
