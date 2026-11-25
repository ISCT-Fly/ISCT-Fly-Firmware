// A small software 3D view of the board: a flat box plus the sensor's X/Y/Z axes, drawn on a
// 2D canvas (painter's algorithm).  No WebGL and no library needed for one box.

import { cssVar, fitCanvas } from './charts.js';

const HALF = [1, 1, 0.07]; // board is 30 x 30 x 1.6 mm; thickness exaggerated to stay visible
const AXIS_LEN = 1.75;
const CAM_AZ = -0.6, CAM_EL = 0.5, CAM_DIST = 7;

function rotate(q, v) {
  const [w, x, y, z] = q;
  const r00 = 1 - 2 * (y * y + z * z), r01 = 2 * (x * y - w * z), r02 = 2 * (x * z + w * y);
  const r10 = 2 * (x * y + w * z), r11 = 1 - 2 * (x * x + z * z), r12 = 2 * (y * z - w * x);
  const r20 = 2 * (x * z - w * y), r21 = 2 * (y * z + w * x), r22 = 1 - 2 * (x * x + y * y);
  return [r00 * v[0] + r01 * v[1] + r02 * v[2], r10 * v[0] + r11 * v[1] + r12 * v[2], r20 * v[0] + r21 * v[1] + r22 * v[2]];
}

const VERTS = [];
for (const sz of [-1, 1]) for (const sy of [-1, 1]) for (const sx of [-1, 1]) VERTS.push([sx * HALF[0], sy * HALF[1], sz * HALF[2]]);
// vertex index = (sz>0)*4 + (sy>0)*2 + (sx>0)
const FACES = [
  { idx: [4, 5, 7, 6], n: [0, 0, 1], role: 'top' },
  { idx: [0, 2, 3, 1], n: [0, 0, -1], role: 'bottom' },
  { idx: [1, 3, 7, 5], n: [1, 0, 0], role: 'side' },
  { idx: [0, 4, 6, 2], n: [-1, 0, 0], role: 'side' },
  { idx: [2, 6, 7, 3], n: [0, 1, 0], role: 'side' },
  { idx: [0, 1, 5, 4], n: [0, -1, 0], role: 'side' },
];

export function drawBoard(canvas, q) {
  const { ctx, w, h } = fitCanvas(canvas);
  ctx.clearRect(0, 0, w, h);
  const scale = Math.min(w, h) * 0.3;
  const ca = Math.cos(CAM_AZ), sa = Math.sin(CAM_AZ), ce = Math.cos(CAM_EL), se = Math.sin(CAM_EL);

  // world point -> { x, y on screen, d = depth (bigger is further away) }
  const project = (p) => {
    const x1 = p[0] * ca + p[1] * sa, y1 = -p[0] * sa + p[1] * ca;
    const d = y1 * ce - p[2] * se;
    const k = CAM_DIST / (CAM_DIST + d);
    return { x: w / 2 + x1 * scale * k, y: h * 0.53 - (y1 * se + p[2] * ce) * scale * k, d };
  };
  const body = (v) => project(rotate(q, v));

  // ground reference: a ring in the world's horizontal plane
  ctx.lineWidth = 1;
  ctx.strokeStyle = cssVar('--grid');
  for (const r of [1.3, 2.1]) {
    ctx.beginPath();
    for (let i = 0; i <= 72; i++) {
      const a = (i / 72) * 2 * Math.PI, p = project([r * Math.cos(a), r * Math.sin(a), 0]);
      if (i) ctx.lineTo(p.x, p.y); else ctx.moveTo(p.x, p.y);
    }
    ctx.stroke();
  }

  const axes = [
    { v: [AXIS_LEN, 0, 0], color: cssVar('--series-1'), name: 'X' },
    { v: [0, AXIS_LEN, 0], color: cssVar('--series-2'), name: 'Y' },
    { v: [0, 0, AXIS_LEN], color: cssVar('--series-3'), name: 'Z' },
  ].map((a) => ({ ...a, tip: body(a.v) }));
  const origin = body([0, 0, 0]);

  const drawAxis = (a) => {
    ctx.strokeStyle = a.color;
    ctx.fillStyle = a.color;
    ctx.lineWidth = 2.5;
    ctx.lineCap = 'round';
    ctx.beginPath();
    ctx.moveTo(origin.x, origin.y);
    ctx.lineTo(a.tip.x, a.tip.y);
    ctx.stroke();
    // arrow head
    const ang = Math.atan2(a.tip.y - origin.y, a.tip.x - origin.x);
    ctx.beginPath();
    ctx.moveTo(a.tip.x + 9 * Math.cos(ang), a.tip.y + 9 * Math.sin(ang));
    ctx.lineTo(a.tip.x + 5 * Math.cos(ang + 2.3), a.tip.y + 5 * Math.sin(ang + 2.3));
    ctx.lineTo(a.tip.x + 5 * Math.cos(ang - 2.3), a.tip.y + 5 * Math.sin(ang - 2.3));
    ctx.closePath();
    ctx.fill();
    // the name is text: ink colour, the coloured line beside it carries identity
    ctx.fillStyle = cssVar('--ink');
    ctx.font = '600 13px system-ui, sans-serif';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(a.name, a.tip.x + 20 * Math.cos(ang), a.tip.y + 20 * Math.sin(ang));
  };

  // axes pointing away from the viewer go behind the board
  axes.filter((a) => a.tip.d > origin.d).forEach(drawAxis);

  const pts = VERTS.map(body);
  const light = [0.35, -0.5, 0.8];
  const faces = FACES.map((f) => {
    const n = rotate(q, f.n);
    const depth = f.idx.reduce((s, i) => s + pts[i].d, 0) / 4;
    const lit = 0.72 + 0.28 * Math.max(0, n[0] * light[0] + n[1] * light[1] + n[2] * light[2]);
    return { ...f, depth, lit };
  }).sort((a, b) => b.depth - a.depth);

  ctx.lineJoin = 'round';
  for (const f of faces) {
    ctx.beginPath();
    f.idx.forEach((i, k) => (k ? ctx.lineTo(pts[i].x, pts[i].y) : ctx.moveTo(pts[i].x, pts[i].y)));
    ctx.closePath();
    ctx.globalAlpha = 1;
    ctx.fillStyle = cssVar(f.role === 'top' ? '--board-top' : '--board-side');
    ctx.fill();
    ctx.globalAlpha = 1 - f.lit; // darken by lighting with a translucent overlay
    ctx.fillStyle = '#000000';
    ctx.fill();
    ctx.globalAlpha = 1;
    ctx.strokeStyle = cssVar('--board-edge');
    ctx.lineWidth = 1;
    ctx.stroke();

    if (f.role === 'top') {
      // the IMU chip and a mark on the +X edge, so the top side and heading are recognisable
      const quad = (cx, cy, hx, hy) => {
        const z = HALF[2] + 0.001;
        const c = [[cx - hx, cy - hy, z], [cx + hx, cy - hy, z], [cx + hx, cy + hy, z], [cx - hx, cy + hy, z]].map(body);
        ctx.beginPath();
        c.forEach((p, k) => (k ? ctx.lineTo(p.x, p.y) : ctx.moveTo(p.x, p.y)));
        ctx.closePath();
      };
      ctx.fillStyle = cssVar('--board-edge');
      quad(0, 0, 0.15, 0.1);
      ctx.fill();
      quad(0.86, 0, 0.06, 0.3);
      ctx.fill();
    }
  }

  axes.filter((a) => a.tip.d <= origin.d).forEach(drawAxis);
}
