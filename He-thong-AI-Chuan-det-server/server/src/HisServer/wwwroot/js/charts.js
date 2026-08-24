// Minimal time-series chart renderer, drawn as inline SVG.
//
// Deliberately hand-written instead of pulling in Chart.js/uPlot/d3: this app
// has no bundler and no build step (plain <script> tags), and everything must
// keep working on a hospital machine with no internet, so a CDN <script> is
// not an option either. What the bed view needs is one small line chart per
// metric - a few hundred points, no zoom/pan, no animation - which is far less
// than any charting library exists to provide.
//
// Loaded as a global `Charts` object, same convention as UiUtils/Api.
const Charts = (() => {
  const NS = "http://www.w3.org/2000/svg";

  // Viewbox units. The SVG scales to whatever width the CSS gives it, so
  // these are an aspect ratio and a coordinate space, not pixels.
  const VB_W = 600;
  const VB_H = 160;
  const PAD_L = 46;   // room for the y-axis labels
  const PAD_R = 10;
  const PAD_T = 12;
  const PAD_B = 20;   // room for the time labels

  const PLOT_W = VB_W - PAD_L - PAD_R;
  const PLOT_H = VB_H - PAD_T - PAD_B;

  function escapeXml(value) {
    return String(value).replace(/[<>&"']/g, (c) =>
      ({ "<": "&lt;", ">": "&gt;", "&": "&amp;", '"': "&quot;", "'": "&apos;" })[c]);
  }

  function formatClock(date) {
    return date.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
  }

  /* Picks a "nice" y-range around the data.
   *
   * Two things this must get right, both of which a naive min/max gets wrong
   * for vitals:
   *  - A dead-flat series (SpO2 pinned at 98) would give min == max and a
   *    zero-height scale, so a flat line needs a synthetic band around it.
   *  - Autoscaling tightly to the data exaggerates clinically meaningless
   *    wobble: 1 bpm of jitter would fill the whole chart height and look
   *    alarming. `minSpan` forces a floor on the visible range per metric. */
  function niceRange(values, minSpan) {
    const finite = values.filter((v) => v != null && Number.isFinite(v));
    if (finite.length === 0) return null;

    let lo = Math.min(...finite);
    let hi = Math.max(...finite);

    const span = hi - lo;
    if (span < minSpan) {
      const mid = (lo + hi) / 2;
      lo = mid - minSpan / 2;
      hi = mid + minSpan / 2;
    } else {
      const margin = span * 0.12;
      lo -= margin;
      hi += margin;
    }

    if (lo > 0 && lo < minSpan) lo = 0;   // don't float a near-zero series off the axis
    return { lo, hi };
  }

  /* Builds the path data, breaking it into separate sub-paths wherever the
   * series has a null.
   *
   * This is the whole reason nulls are preserved end-to-end from the DB: a
   * detached sensor must show as a GAP. Interpolating across it would draw a
   * confident straight line through a period where nothing was measured, and
   * plotting it as 0 would look like a flatlining patient. */
  function buildPath(points, xOf, yOf) {
    let d = "";
    let penDown = false;

    points.forEach((p, i) => {
      if (p.v == null || !Number.isFinite(p.v)) {
        penDown = false;
        return;
      }
      const cmd = penDown ? "L" : "M";
      d += `${cmd}${xOf(i).toFixed(1)},${yOf(p.v).toFixed(1)} `;
      penDown = true;
    });

    return d.trim();
  }

  /* Marks stretches where an alarm flag was set, so the doctor can see at a
   * glance WHEN something went wrong rather than reading it off the line. */
  function alarmBandsSvg(points, xOf) {
    const bands = [];
    let start = null;

    points.forEach((p, i) => {
      if (p.alarm && start === null) {
        start = i;
      } else if (!p.alarm && start !== null) {
        bands.push([start, i - 1]);
        start = null;
      }
    });
    if (start !== null) bands.push([start, points.length - 1]);

    return bands.map(([a, b]) => {
      const x = xOf(a);
      // A single-sample alarm would be a zero-width rect and invisible, so
      // give every band a minimum width.
      const w = Math.max(xOf(b) - x, 2);
      return `<rect x="${x.toFixed(1)}" y="${PAD_T}" width="${w.toFixed(1)}" height="${PLOT_H}" class="chart-alarm-band"/>`;
    }).join("");
  }

  /**
   * Renders one metric as an SVG string.
   *
   * @param {Object}   opts
   * @param {Array}    opts.points   [{ t: Date, v: number|null, alarm?: boolean }]
   * @param {string}   opts.label    metric name shown above the plot
   * @param {string}   opts.unit     appended to the current-value readout
   * @param {string}   opts.color    stroke colour
   * @param {number}   opts.minSpan  smallest y-range to show (see niceRange)
   * @param {number}   opts.decimals digits in the readouts
   * @param {number[]} opts.yRange   optional fixed [lo, hi] y-axis; disables
   *                                 autoscaling so the scale never moves
   * @param {string}   opts.severity "ok" | "warning" | "critical" — when the
   *                                 latest value breaches this metric's
   *                                 clinical limits the trace, the readout and
   *                                 the card all switch to the alarm colour,
   *                                 so a bed in trouble is obvious from across
   *                                 the room rather than only on close reading
   */
  function metricChart(opts) {
    const { points, label, unit = "", color = "#2470c8", minSpan = 5, decimals = 0,
            yRange = null, severity = "ok" } = opts;

    // Severity wins over the metric's own colour: a red trace must mean
    // "this reading is dangerous", never "this happens to be the heart-rate
    // chart", otherwise the colour carries no information.
    const SEVERITY_COLOR = { warning: "#e69119", critical: "#dc3c3c" };
    const strokeColor = SEVERITY_COLOR[severity] || color;

    const sevClass = severity === "ok" ? "" : ` sev-${severity}`;

    const withData = points.filter((p) => p.v != null && Number.isFinite(p.v));
    if (points.length === 0 || withData.length === 0) {
      return `
        <div class="chart-card${sevClass}">
          <div class="chart-head"><span class="chart-label">${escapeXml(label)}</span></div>
          <div class="chart-empty">No data in this period</div>
        </div>`;
    }

    // A fixed yRange wins over autoscaling: for a vital sign a scale that never
    // moves is easier to read at a glance across beds and across time, and it
    // stops a clinically meaningless 1-2 unit wobble from being magnified to
    // fill the whole plot height.
    const range = yRange
      ? { lo: yRange[0], hi: yRange[1] }
      : niceRange(points.map((p) => p.v), minSpan);

    const xOf = (i) => PAD_L + (points.length === 1 ? PLOT_W / 2 : (i / (points.length - 1)) * PLOT_W);
    // Clamp into the plot box: with a FIXED range a reading can legitimately
    // fall outside it (e.g. HR 160 on a 1-150 axis), and an unclamped y would
    // draw the line outside the chart and over the neighbouring text.
    const yOf = (v) => {
      const y = PAD_T + PLOT_H - ((v - range.lo) / (range.hi - range.lo)) * PLOT_H;
      return Math.min(PAD_T + PLOT_H, Math.max(PAD_T, y));
    };

    const path = buildPath(points, xOf, yOf);
    const bands = alarmBandsSvg(points, xOf);

    const latest = withData[withData.length - 1];
    const values = withData.map((p) => p.v);
    const vMin = Math.min(...values);
    const vMax = Math.max(...values);
    const vAvg = values.reduce((a, b) => a + b, 0) / values.length;

    // Gridlines at the range ends plus the midpoint - enough to read the
    // scale without turning a 160px-tall chart into graph paper.
    const ticks = [range.hi, (range.hi + range.lo) / 2, range.lo];
    const grid = ticks.map((t) => {
      const y = yOf(t);
      return `<line x1="${PAD_L}" y1="${y.toFixed(1)}" x2="${VB_W - PAD_R}" y2="${y.toFixed(1)}" class="chart-grid"/>
              <text x="${PAD_L - 6}" y="${(y + 3).toFixed(1)}" class="chart-axis" text-anchor="end">${t.toFixed(decimals)}</text>`;
    }).join("");

    const tStart = formatClock(points[0].t);
    const tEnd = formatClock(points[points.length - 1].t);

    const gapCount = points.length - withData.length;
    const gapNote = gapCount > 0
      ? `<span class="chart-gap-note" title="Samples with no trustworthy reading - sensor detached or not installed. Shown as gaps rather than zeros.">${gapCount} gap${gapCount === 1 ? "" : "s"}</span>`
      : "";

    return `
      <div class="chart-card${sevClass}">
        <div class="chart-head">
          <span class="chart-label">${escapeXml(label)}</span>
          <span class="chart-now" style="color:${strokeColor};">${latest.v.toFixed(decimals)}${escapeXml(unit)}</span>
        </div>
        <svg class="chart-svg" viewBox="0 0 ${VB_W} ${VB_H}" preserveAspectRatio="none" role="img"
             aria-label="${escapeXml(label)} over time">
          <rect class="chart-plot-bg" x="${PAD_L}" y="${PAD_T}" width="${PLOT_W}" height="${PLOT_H}"/>
          ${bands}
          ${grid}
          <path d="${path}" fill="none" stroke="${strokeColor}" stroke-width="2"
                stroke-linejoin="round" stroke-linecap="round" vector-effect="non-scaling-stroke"/>
          <circle cx="${xOf(points.indexOf(latest)).toFixed(1)}" cy="${yOf(latest.v).toFixed(1)}" r="3" fill="${strokeColor}"/>
          <text x="${PAD_L}" y="${VB_H - 5}" class="chart-axis">${escapeXml(tStart)}</text>
          <text x="${VB_W - PAD_R}" y="${VB_H - 5}" class="chart-axis" text-anchor="end">${escapeXml(tEnd)}</text>
        </svg>
        <div class="chart-foot">
          <span>min <b>${vMin.toFixed(decimals)}</b></span>
          <span>avg <b>${vAvg.toFixed(decimals)}</b></span>
          <span>max <b>${vMax.toFixed(decimals)}</b></span>
          <span>${withData.length} pts</span>
          ${gapNote}
        </div>
      </div>`;
  }

  return { metricChart };
})();
