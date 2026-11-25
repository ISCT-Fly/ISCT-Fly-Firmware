// Canvas charts for the test bench: a scrolling strip chart and a per-channel bar chart.
// Chart chrome (grid, axes, text) is read from the CSS custom properties in style.css so
// light and dark mode stay in one place.

const varCache = new Map();
export function cssVar(name) {
  let v = varCache.get(name);
  if (v === undefined) {
    v = getComputedStyle(document.documentElement).getPropertyValue(name).trim();
    varCache.set(name, v);
  }
  return v;
}
matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => varCache.clear());

/** Match the canvas bitmap to its CSS box and device pixel ratio; returns a context in CSS pixels. */
export function fitCanvas(canvas) {
  const dpr = window.devicePixelRatio || 1;
  const w = canvas.clientWidth, h = canvas.clientHeight;
  if (canvas.width !== Math.round(w * dpr) || canvas.height !== Math.round(h * dpr)) {
    canvas.width = Math.round(w * dpr);
    canvas.height = Math.round(h * dpr);
  }
  const ctx = canvas.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  return { ctx, w, h };
}

function niceStep(span, target = 4) {
  const raw = span / target;
  const mag = Math.pow(10, Math.floor(Math.log10(raw)));
  const f = raw / mag;
  return (f <= 1 ? 1 : f <= 2 ? 2 : f <= 5 ? 5 : 10) * mag;
}

function fmtTick(v, step) {
  const d = Math.max(0, Math.min(4, -Math.floor(Math.log10(step) + 1e-9)));
  return v.toFixed(d).replace(/^-0(\.0+)?$/, '0');
}

const FONT = '11px system-ui, -apple-system, "Segoe UI", sans-serif';
const PAD = { l: 46, r: 12, t: 8, b: 22 };

function setTip(tip, chartEl, x, buildRows) {
  tip.replaceChildren();
  buildRows(tip);
  tip.hidden = false;
  const tw = tip.offsetWidth, cw = chartEl.clientWidth;
  tip.style.left = Math.max(4, Math.min(cw - tw - 4, x + 12 + tw > cw ? x - tw - 12 : x + 12)) + 'px';
}

function tipRow(tip, color, value, name) {
  const r = document.createElement('div');
  r.className = 'r';
  if (color) {
    const k = document.createElement('span');
    k.className = 'key';
    k.style.background = color;
    r.append(k);
  }
  const s = document.createElement('strong');
  s.textContent = value;
  const n = document.createElement('span');
  n.textContent = name;
  r.append(s, n);
  tip.append(r);
}

// ---------------------------------------------------------------- strip chart

export class StripChart {
  /**
   * series: [{ name, color: '--series-1' }]   windowSec: visible time span
   * symmetric: keep zero in the middle        minSpan: smallest full y-range   digits: tooltip decimals
   */
  constructor(canvas, { series, windowSec = 10, symmetric = false, minSpan = 1, digits = 2, unit = '' }) {
    this.canvas = canvas;
    this.box = canvas.parentElement;
    this.tip = this.box.querySelector('.tip');
    this.series = series;
    this.windowSec = windowSec;
    this.symmetric = symmetric;
    this.minSpan = minSpan;
    this.digits = digits;
    this.unit = unit;
    this.t = [];
    this.v = series.map(() => []);
    this.range = null;
    this.hoverX = null;
    canvas.addEventListener('pointermove', (e) => { this.hoverX = e.offsetX; });
    canvas.addEventListener('pointerleave', () => { this.hoverX = null; this.tip.hidden = true; });
  }

  clear() {
    this.t.length = 0;
    this.v.forEach((a) => (a.length = 0));
    this.range = null;
  }

  /** t in seconds, values aligned with the series list (null = no sample) */
  push(t, values) {
    if (this.t.length && t < this.t[this.t.length - 1]) this.clear(); // device restarted
    this.t.push(t);
    values.forEach((x, i) => this.v[i].push(x));
    const cut = t - this.windowSec - 1;
    let drop = 0;
    while (drop < this.t.length && this.t[drop] < cut) drop++;
    if (drop > 64) {
      this.t.splice(0, drop);
      this.v.forEach((a) => a.splice(0, drop));
    }
  }

  draw() {
    const { ctx, w, h } = fitCanvas(this.canvas);
    ctx.clearRect(0, 0, w, h);
    const pw = w - PAD.l - PAD.r, ph = h - PAD.t - PAD.b;
    if (pw <= 10 || ph <= 10) return;
    const tEnd = this.t.length ? this.t[this.t.length - 1] : 0;
    const tStart = tEnd - this.windowSec;

    // y range: grows at once, shrinks slowly, so the scale does not twitch
    let lo = Infinity, hi = -Infinity;
    for (let i = this.t.length - 1; i >= 0 && this.t[i] >= tStart; i--) {
      for (const s of this.v) {
        const x = s[i];
        if (x !== null && x !== undefined) { if (x < lo) lo = x; if (x > hi) hi = x; }
      }
    }
    if (!isFinite(lo)) { lo = -this.minSpan / 2; hi = this.minSpan / 2; }
    if (this.symmetric) {
      const m = Math.max(Math.abs(lo), Math.abs(hi), this.minSpan / 2) * 1.1;
      lo = -m; hi = m;
    } else {
      const mid = (lo + hi) / 2, half = Math.max((hi - lo) / 2 * 1.15, this.minSpan / 2);
      lo = mid - half; hi = mid + half;
    }
    if (!this.range) this.range = [lo, hi];
    this.range[0] = lo < this.range[0] ? lo : this.range[0] + (lo - this.range[0]) * 0.03;
    this.range[1] = hi > this.range[1] ? hi : this.range[1] + (hi - this.range[1]) * 0.03;
    const [y0, y1] = this.range;
    const X = (t) => PAD.l + ((t - tStart) / this.windowSec) * pw;
    const Y = (v) => PAD.t + (1 - (v - y0) / (y1 - y0)) * ph;

    // grid and ticks
    ctx.font = FONT;
    ctx.textBaseline = 'middle';
    ctx.textAlign = 'right';
    ctx.lineWidth = 1;
    const step = niceStep(y1 - y0);
    for (let v = Math.ceil(y0 / step) * step; v <= y1 + 1e-9; v += step) {
      const y = Math.round(Y(v)) + 0.5;
      ctx.strokeStyle = cssVar(Math.abs(v) < step / 1e6 ? '--axis' : '--grid');
      ctx.beginPath(); ctx.moveTo(PAD.l, y); ctx.lineTo(w - PAD.r, y); ctx.stroke();
      ctx.fillStyle = cssVar('--muted');
      ctx.fillText(fmtTick(v, step), PAD.l - 6, y);
    }
    ctx.textBaseline = 'top';
    for (let s = 0; s <= this.windowSec; s += this.windowSec / 2) {
      ctx.textAlign = s === 0 ? 'right' : s === this.windowSec ? 'left' : 'center';
      ctx.fillStyle = cssVar('--muted');
      ctx.fillText(s === 0 ? '现在' : `−${s} s`, X(tEnd - s), h - PAD.b + 6);
    }

    // lines
    ctx.save();
    ctx.beginPath(); ctx.rect(PAD.l, PAD.t - 1, pw, ph + 2); ctx.clip();
    ctx.lineWidth = 2; ctx.lineJoin = 'round'; ctx.lineCap = 'round';
    let first = 0;
    while (first < this.t.length && this.t[first] < tStart) first++;
    first = Math.max(0, first - 1);
    this.series.forEach((s, k) => {
      const data = this.v[k];
      ctx.strokeStyle = cssVar(s.color);
      ctx.beginPath();
      let pen = false, prevT = 0;
      for (let i = first; i < this.t.length; i++) {
        const x = data[i];
        if (x === null || x === undefined || (pen && this.t[i] - prevT > 0.5)) { pen = false; if (x === null || x === undefined) continue; }
        if (pen) ctx.lineTo(X(this.t[i]), Y(x)); else ctx.moveTo(X(this.t[i]), Y(x));
        pen = true; prevT = this.t[i];
      }
      ctx.stroke();
    });
    ctx.restore();

    this.drawHover(ctx, X, Y, tEnd, tStart, h);
  }

  drawHover(ctx, X, Y, tEnd, tStart, h) {
    if (this.hoverX === null || !this.t.length) return;
    const tw = tStart + ((this.hoverX - PAD.l) / (this.canvas.clientWidth - PAD.l - PAD.r)) * this.windowSec;
    // nearest sample (binary search)
    let a = 0, b = this.t.length - 1;
    while (b - a > 1) { const m = (a + b) >> 1; if (this.t[m] < tw) a = m; else b = m; }
    const i = Math.abs(this.t[a] - tw) < Math.abs(this.t[b] - tw) ? a : b;
    if (this.t[i] < tStart) { this.tip.hidden = true; return; }
    const x = X(this.t[i]);
    ctx.strokeStyle = cssVar('--axis');
    ctx.lineWidth = 1;
    ctx.beginPath(); ctx.moveTo(Math.round(x) + 0.5, PAD.t); ctx.lineTo(Math.round(x) + 0.5, h - PAD.b); ctx.stroke();
    this.series.forEach((s, k) => {
      const v = this.v[k][i];
      if (v === null || v === undefined) return;
      ctx.beginPath(); ctx.arc(x, Y(v), 6, 0, 2 * Math.PI); ctx.fillStyle = cssVar('--surface'); ctx.fill();
      ctx.beginPath(); ctx.arc(x, Y(v), 4, 0, 2 * Math.PI); ctx.fillStyle = cssVar(s.color); ctx.fill();
    });
    setTip(this.tip, this.box, x, (tip) => {
      const t = document.createElement('div');
      t.className = 't';
      t.textContent = `−${(tEnd - this.t[i]).toFixed(2)} s`;
      tip.append(t);
      this.series.forEach((s, k) => {
        const v = this.v[k][i];
        tipRow(tip, cssVar(s.color), v === null || v === undefined ? '–' : v.toFixed(this.digits) + (this.unit ? ' ' + this.unit : ''), s.name);
      });
    });
  }
}

// ---------------------------------------------------------------- bar chart (one bar per RF channel)

export class ChannelBars {
  constructor(canvas, { color = '--series-1', baseMHz = 2400 } = {}) {
    this.canvas = canvas;
    this.box = canvas.parentElement;
    this.tip = this.box.querySelector('.tip');
    this.color = color;
    this.baseMHz = baseMHz;
    this.values = [];
    this.reps = 0;
    this.hover = -1;
    canvas.addEventListener('pointermove', (e) => { this.hoverX = e.offsetX; this.draw(); });
    canvas.addEventListener('pointerleave', () => { this.hoverX = null; this.tip.hidden = true; this.draw(); });
    new ResizeObserver(() => this.draw()).observe(canvas);
    // this chart is only redrawn on demand, so repaint it when the colour scheme flips
    matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => this.draw());
  }

  set(values, reps) {
    this.values = values;
    this.reps = reps;
    this.draw();
  }

  draw() {
    const { ctx, w, h } = fitCanvas(this.canvas);
    ctx.clearRect(0, 0, w, h);
    const pw = w - PAD.l - PAD.r, ph = h - PAD.t - PAD.b;
    if (pw <= 10 || ph <= 10) return;
    const n = this.values.length || 126;
    const top = Math.max(4, this.reps, ...this.values);
    const step = Math.max(1, niceStep(top, 3));
    const yMax = Math.ceil(top / step) * step;
    const slot = pw / n;
    const bw = Math.max(1, Math.min(24, slot - 2)); // 2 px of surface between neighbours
    const Y = (v) => PAD.t + (1 - v / yMax) * ph;

    ctx.font = FONT;
    ctx.lineWidth = 1;
    ctx.textAlign = 'right';
    ctx.textBaseline = 'middle';
    for (let v = 0; v <= yMax; v += step) {
      const y = Math.round(Y(v)) + 0.5;
      ctx.strokeStyle = cssVar(v === 0 ? '--axis' : '--grid');
      ctx.beginPath(); ctx.moveTo(PAD.l, y); ctx.lineTo(w - PAD.r, y); ctx.stroke();
      ctx.fillStyle = cssVar('--muted');
      ctx.fillText(String(v), PAD.l - 6, y);
    }
    ctx.textAlign = 'center';
    ctx.textBaseline = 'top';
    for (let ch = 0; ch < n; ch += 20) {
      ctx.fillText(`${this.baseMHz + ch}`, PAD.l + (ch + 0.5) * slot, h - PAD.b + 6);
    }
    ctx.textAlign = 'right';
    ctx.fillText('MHz', w - PAD.r, h - PAD.b + 6);

    const hover = this.hoverX == null ? -1 : Math.floor((this.hoverX - PAD.l) / slot);
    const r = Math.min(4, bw / 2);
    this.values.forEach((v, ch) => {
      if (v <= 0) return;
      const x = PAD.l + ch * slot + (slot - bw) / 2, y = Y(v), yb = Y(0);
      ctx.fillStyle = cssVar(this.color);
      ctx.globalAlpha = hover === -1 || hover === ch ? 1 : 0.55;
      ctx.beginPath();
      ctx.moveTo(x, yb); ctx.lineTo(x, y + r);
      ctx.arcTo(x, y, x + r, y, r); ctx.arcTo(x + bw, y, x + bw, y + r, r);
      ctx.lineTo(x + bw, yb); ctx.closePath();
      ctx.fill();
    });
    ctx.globalAlpha = 1;

    if (hover >= 0 && hover < this.values.length) {
      setTip(this.tip, this.box, PAD.l + (hover + 0.5) * slot, (tip) => {
        const t = document.createElement('div');
        t.className = 't';
        t.textContent = `${this.baseMHz + hover} MHz（信道 ${hover}）`;
        tip.append(t);
        tipRow(tip, null, `${this.values[hover]} / ${this.reps}`, '次检测到信号');
      });
    } else {
      this.tip.hidden = true;
    }
  }
}
