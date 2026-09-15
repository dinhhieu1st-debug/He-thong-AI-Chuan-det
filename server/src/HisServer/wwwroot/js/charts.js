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

  function formatAxisTime(date, includeDate) {
    if (!includeDate) return formatClock(date);
    return `${date.toLocaleDateString([], { day: "2-digit", month: "2-digit" })} ${formatClock(date)}`;
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

  /* Builds path data separated into solid lines (normal signal) and dashed lines (no signal).
   * When there is no signal, the line continues horizontally as a dashed line. */
  function buildPaths(points, xOf, yOf) {
    if (points.length < 2) {
      return { solid: "", dashed: "" };
    }

    let solidD = "";
    let dashedD = "";
    let solidPenDown = false;
    let dashedPenDown = false;

    for (let i = 1; i < points.length; i++) {
      const prev = points[i - 1];
      const curr = points[i];
      const x0 = xOf(prev, i - 1).toFixed(1);
      const y0 = yOf(prev.v).toFixed(1);
      const x1 = xOf(curr, i).toFixed(1);
      const y1 = yOf(curr.v).toFixed(1);

      if (curr.isNoSignal) {
        if (!dashedPenDown) {
          dashedD += `M${x0},${y0} `;
          dashedPenDown = true;
        }
        dashedD += `L${x1},${y1} `;
        solidPenDown = false;
      } else {
        if (!solidPenDown) {
          solidD += `M${x0},${y0} `;
          solidPenDown = true;
        }
        solidD += `L${x1},${y1} `;
        dashedPenDown = false;
      }
    }

    return { solid: solidD.trim(), dashed: dashedD.trim() };
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
      const x = xOf(points[a], a);
      const w = Math.max(xOf(points[b], b) - x, 2);
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
   * @param {number[]} opts.yRange   optional fixed [lo, hi] y-axis
   * @param {Date[]}   opts.xRange   optional fixed [from, to] time window
   * @param {string}   opts.severity "ok" | "warning" | "critical"
   * @param {boolean}  opts.zeroMeansNoSignal whether 0 represents lost signal
   */
  function metricChart(opts) {
    const { points, label, unit = "", color = "#2470c8", minSpan = 5, decimals = 0,
            yRange = null, xRange = null, severity = "ok", zeroMeansNoSignal = false } = opts;

    const isVital = zeroMeansNoSignal || label.includes("Heart") || label.includes("SpO2");
    const isNoSig = (v) => v == null || !Number.isFinite(v) || (isVital && v <= 0);

    const SEVERITY_COLOR = { warning: "#e69119", critical: "#dc3c3c" };
    const strokeColor = SEVERITY_COLOR[severity] || color;
    const sevClass = severity === "ok" ? "" : ` sev-${severity}`;

    const withData = points.filter((p) => !isNoSig(p.v));
    if (points.length === 0 || withData.length === 0) {
      return `
        <div class="chart-card${sevClass}">
          <div class="chart-head"><span class="chart-label">${escapeXml(label)}</span></div>
          <div class="chart-empty">No data in this period</div>
        </div>`;
    }

    // Forward-fill and backfill no-signal points so the line stays as a continuous straight line
    let currentVal = withData[0].v;
    const processedPoints = points.map((p) => {
      if (!isNoSig(p.v)) {
        currentVal = p.v;
        return { ...p, v: p.v, isNoSignal: false };
      }
      return { ...p, v: currentVal, isNoSignal: true };
    });

    const range = yRange
      ? { lo: yRange[0], hi: yRange[1] }
      : niceRange(withData.map((p) => p.v), minSpan);

    const firstTime = xRange ? xRange[0].getTime() : points[0].t.getTime();
    const lastTime = xRange ? xRange[1].getTime() : points[points.length - 1].t.getTime();
    const timeSpan = Math.max(1, lastTime - firstTime);
    const xOf = (p) => PAD_L + Math.max(0, Math.min(1,
      (p.t.getTime() - firstTime) / timeSpan)) * PLOT_W;

    const yOf = (v) => {
      const y = PAD_T + PLOT_H - ((v - range.lo) / (range.hi - range.lo || 1)) * PLOT_H;
      return Math.min(PAD_T + PLOT_H, Math.max(PAD_T, y));
    };

    const paths = buildPaths(processedPoints, xOf, yOf);
    const bands = alarmBandsSvg(processedPoints, xOf);

    const latest = processedPoints[processedPoints.length - 1];
    const values = withData.map((p) => p.v);
    const vMin = Math.min(...values);
    const vMax = Math.max(...values);
    const vAvg = values.reduce((a, b) => a + b, 0) / values.length;

    const ticks = [range.hi, (range.hi + range.lo) / 2, range.lo];
    const grid = ticks.map((t) => {
      const y = yOf(t);
      return `<line x1="${PAD_L}" y1="${y.toFixed(1)}" x2="${VB_W - PAD_R}" y2="${y.toFixed(1)}" class="chart-grid"/>
              <text x="${PAD_L - 6}" y="${(y + 3).toFixed(1)}" class="chart-axis" text-anchor="end">${t.toFixed(decimals)}</text>`;
    }).join("");

    const firstDate = new Date(firstTime);
    const lastDate = new Date(lastTime);
    const crossesDate = firstDate.toDateString() !== lastDate.toDateString();
    const tStart = formatAxisTime(firstDate, crossesDate);
    const tEnd = formatAxisTime(lastDate, crossesDate);

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
          ${paths.solid ? `<path d="${paths.solid}" fill="none" stroke="${strokeColor}" stroke-width="2"
                stroke-linejoin="round" stroke-linecap="round" vector-effect="non-scaling-stroke"/>` : ""}
          ${paths.dashed ? `<path d="${paths.dashed}" fill="none" stroke="${strokeColor}" stroke-width="2"
                stroke-dasharray="6 4" stroke-linejoin="round" stroke-linecap="round"
                vector-effect="non-scaling-stroke" opacity="0.8"/>` : ""}
          <circle cx="${xOf(latest, processedPoints.length - 1).toFixed(1)}" cy="${yOf(latest.v).toFixed(1)}" r="3"
                  fill="${latest.isNoSignal ? '#ffffff' : strokeColor}" stroke="${strokeColor}" stroke-width="2"/>
          <text x="${PAD_L}" y="${VB_H - 5}" class="chart-axis">${escapeXml(tStart)}</text>
          <text x="${VB_W - PAD_R}" y="${VB_H - 5}" class="chart-axis" text-anchor="end">${escapeXml(tEnd)}</text>
        </svg>
        <div class="chart-foot">
          <span>min <b>${vMin.toFixed(decimals)}</b></span>
          <span>avg <b>${vAvg.toFixed(decimals)}</b></span>
          <span>max <b>${vMax.toFixed(decimals)}</b></span>
          <span>${processedPoints.length} pts</span>
        </div>
      </div>`;
  }

  return { metricChart };
})();
