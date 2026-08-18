// Checks the two AI v2 render functions in wwwroot/js/beds.js.
//
//   node tools/dashboard_render_check.js      (run from the repo root)
//
// It pulls the functions OUT OF beds.js and runs them, rather than testing a
// copy - a copied fixture drifts from the file it claims to check, and then
// passes forever while the real page is broken.
//
// The cases are the ones where getting it wrong is dangerous rather than ugly:
// a device too old to report a level must show NO badge instead of a confident
// wrong one, and a load cell still filling its 60-second window must say so
// rather than showing a green "OK" it has not earned.
const fs = require('fs');
const read = (f) => fs.readFileSync(`server/src/HisServer/wwwroot/js/${f}`, 'utf8');

// culpritBadgeHtml lives in ui-utils.js so the dashboard card and the bed card
// share ONE copy. They used to have separate markup, and the badge was added to
// only one of them - the kind of bug that a second copy guarantees eventually.
const sources = { 'ui-utils.js': read('ui-utils.js'), 'beds.js': read('beds.js') };

function extract(name) {
  const src = Object.values(sources).find((s) => s.includes(`function ${name}(`));
  if (!src) throw new Error(`${name} not found in any source`);
  const start = src.indexOf(`function ${name}(`);
  if (start < 0) throw new Error(`${name} not found`);
  let depth = 0, i = src.indexOf('{', start);
  const from = i;
  for (; i < src.length; i++) {
    if (src[i] === '{') depth++;
    else if (src[i] === '}' && --depth === 0) { i++; break; }
  }
  return src.slice(start, i);
}
const beds = sources['beds.js'];
const lineStateTable = beds.slice(beds.indexOf('const LINE_STATE'),
                                  beds.indexOf('};', beds.indexOf('const LINE_STATE')) + 2);

// Both card templates must call the shared helper - checked, because adding the
// badge to one card and not the other is exactly what happened the first time.
for (const file of ['dashboard.js', 'beds.js']) {
  if (!read(file).includes('UiUtils.culpritBadgeHtml(bed)')) {
    console.error(`  [FAIL] ${file} does not render the culprit badge`);
    process.exit(1);
  }
}

const UiUtils = { escapeHtml: (t) => String(t == null ? "" : t).replace(/[&<>"]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c])) };
const ctx = { UiUtils };
const code = `${lineStateTable}\n${extract('culpritBadgeHtml')}\n${extract('lineSectionHtml')}\nmodule.exports={culpritBadgeHtml,lineSectionHtml};`;
const m = { exports: {} };
new Function('module', 'UiUtils', code)(m, UiUtils);
const { culpritBadgeHtml, lineSectionHtml } = m.exports;

const scenarios = [
  { name: "V2-OK      (all normal)",     bed: { alertLevel: 0, lineState: 0, remainingMl: 460, remainingMin: 110 } },
  { name: "V2-LOWBAG  (bag running low)", bed: { alertLevel: 0, lineState: 1, remainingMl: 120, remainingMin: 28 } },
  { name: "V2-LINE    (blocked line)",   bed: { alertLevel: 1, lineState: 2, remainingMl: 300, remainingMin: null, dripAnomaly: true } },
  { name: "V2-PATIENT (patient)",        bed: { alertLevel: 2, lineState: 0, remainingMl: 420, remainingMin: 95, vitalsAnomaly: true } },
  { name: "V2-BOTH    (both)",           bed: { alertLevel: 3, lineState: 3, remainingMl: 90, remainingMin: 6, dripAnomaly: true, vitalsAnomaly: true, aeAlarm: true } },
  { name: "v1 device  (no alertLevel)",  bed: { alertLevel: null, lineState: null } },
];

let fails = 0;
const want = (name, cond, detail) => {
  console.log(`  [${cond ? "PASS" : "FAIL"}] ${name.padEnd(52)} ${detail}`);
  if (!cond) fails++;
};

console.log("\n== Culprit badge ==");
for (const { name, bed } of scenarios) {
  const html = culpritBadgeHtml(bed);
  const text = (html.match(/>([A-Z +]+)<\/span>/) || [,"(none)"])[1];
  console.log(`  ${name}  ->  ${text}`);
}
want("a healthy bed shows no badge", culpritBadgeHtml(scenarios[0].bed) === "", "empty");
want("a v1 device shows no badge (never a wrong one)",
     culpritBadgeHtml(scenarios[5].bed) === "", "empty");
want("level 1 says IV LINE, and is NOT the critical style",
     culpritBadgeHtml(scenarios[2].bed).includes("IV LINE") &&
     culpritBadgeHtml(scenarios[2].bed).includes("culprit-line"), "culprit-line");
want("level 2 says PATIENT", culpritBadgeHtml(scenarios[3].bed).includes("PATIENT"), "culprit-patient");
want("level 3 says LINE + PATIENT",
     culpritBadgeHtml(scenarios[4].bed).includes("LINE + PATIENT"), "culprit-critical");

console.log("\n== Load-cell section ==");
want("no lineState -> 'measuring', NOT a green OK",
     lineSectionHtml({ lineState: null }).includes("Measuring the bag"), "muted line");
want("running low shows the minutes left",
     lineSectionHtml(scenarios[1].bed).includes("about 28 min"), "28 min");
want("blocked line names the evidence",
     lineSectionHtml(scenarios[2].bed).includes("not getting lighter"), "weight evidence");
want("blocked line with no rate estimate does not invent one",
     lineSectionHtml(scenarios[2].bed).includes("rate too low to estimate"), "no fake ETA");
want("free flow is flagged as an alarm",
     lineSectionHtml(scenarios[4].bed).includes("ls-alarm"), "ls-alarm");

console.log(fails === 0 ? "\nALL CHECKS PASSED  (0 failures)\n"
                        : `\nSOME CHECKS FAILED  (${fails})\n`);
process.exit(fails ? 1 : 0);
