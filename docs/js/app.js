// FlapFC web test bench - page logic.
// Receives the firmware's text report and JSON telemetry, keeps the dashboard up to date and
// sends the live motor / servo commands.  See src/telemetry.c for the wire format.

import { SerialLink, DemoLink, serialSupported } from './link.js';
import { Ahrs } from './ahrs.js';
import { drawBoard } from './board3d.js';
import { StripChart, ChannelBars } from './charts.js';

const $ = (id) => document.getElementById(id);
const MOTOR_RESEND_MS = 150;   // the firmware stops the motor after 600 ms without a set-point
const SERVO_SEND_MS = 40;

// ---------------------------------------------------------------- static description of the board

const MODULES = [
  { id: 'hse', name: '晶振', match: /^HSE/ },
  { id: 'pwr', name: '电源', match: /^(3V3|VSYS)/ },
  { id: 'i2c', name: 'I2C 总线', match: /^I2C idle/ },
  { id: 'imu', name: 'IMU', match: /^(BMI088|ACC_INT|GYRO_INT)/ },
  { id: 'baro', name: '气压计', match: /^(BMP388|BARO_INT)/ },
  { id: 'tof', name: 'ToF', match: /^(VL53L1X|TOF_)/ },
  { id: 'rf', name: '无线', match: /^(nRF24|RF_IRQ)/ },
  { id: 'sd', name: 'SD 卡', match: /^SD / },
  { id: 'usb', name: 'USB', match: /^(USB CDC|VBUS_DET)/ },
];

// index = position in the firmware's "iq" array
const IRQS = [
  { i: 3, name: '加速度计 INT1', pin: 'PB12', range: [30, 130], expect: '约 100 Hz' },
  { i: 2, name: '陀螺仪 INT3', pin: 'PB7', range: [700, 1300], expect: '约 1000 Hz' },
  { i: 4, name: '气压计 INT', pin: 'PC15', range: [35, 65], expect: '约 50 Hz' },
  { i: 0, name: 'ToF GPIO1', pin: 'PB0', range: null, expect: '0（未启用测距）' },
  { i: 1, name: '无线 IRQ', pin: 'PC4', range: null, expect: '0（只在收发时有脉冲）' },
];

const SERVO_PINS = ['PA0', 'PA1', 'PA2'];
const AXES = [
  { name: 'X 轴', color: '--series-1' },
  { name: 'Y 轴', color: '--series-2' },
  { name: 'Z 轴', color: '--series-3' },
];

const RANK = { unknown: 0, good: 1, warning: 2, critical: 3 };
const ICON = { unknown: '–', good: '✓', warning: '!', critical: '✕' };
const STATE_TEXT = { unknown: '未测', good: '正常', warning: '警告', critical: '异常' };
const TAG = {
  PASS: { s: 'good', text: '通过' },
  WARN: { s: 'warning', text: '警告' },
  FAIL: { s: 'critical', text: '失败' },
  INFO: { s: 'unknown', text: '信息' },
};

// ---------------------------------------------------------------- state

let link = null;
let mode = 'off';             // 'off' | 'serial' | 'demo'
let latest = null;            // last telemetry frame
let lastT = null;             // its device time [s]
let lastFrameAt = 0;          // performance.now() of its arrival
let history = [];             // recent frames for rates: { t, iq, en }
let pressureRef = null;
let pressureSeed = [];
let testing = false;
let testStarted = 0;
let sawReport = false;
const results = new Map();    // item -> { tag, msg }
const ahrs = new Ahrs();

let armed = false;
let armedAt = 0;
let motorTimer = null;
const servoOn = [false, false, false];
const servoDue = [0, 0, 0];

// ---------------------------------------------------------------- charts

const accChart = new StripChart($('ch-acc'), { series: AXES, symmetric: true, minSpan: 2.4, digits: 3, unit: 'g' });
const gyroChart = new StripChart($('ch-gyro'), { series: AXES, symmetric: true, minSpan: 40, digits: 1, unit: '°/s' });
const altChart = new StripChart($('ch-alt'), { series: [{ name: '相对高度', color: '--series-1' }], windowSec: 30, minSpan: 1, digits: 2, unit: 'm' });
const rfBars = new ChannelBars($('ch-rf'));

function buildLegend(el, prefix) {
  AXES.forEach((a, k) => {
    const item = document.createElement('span');
    item.className = 'item';
    const key = document.createElement('span');
    key.className = 'key';
    key.style.background = `var(${a.color})`;
    const name = document.createElement('span');
    name.textContent = a.name;
    const val = document.createElement('span');
    val.className = 'val';
    val.id = `${prefix}-${k}`;
    val.textContent = '–';
    item.append(key, name, val);
    el.append(item);
  });
}
buildLegend($('lg-acc'), 'lv-acc');
buildLegend($('lg-gyro'), 'lv-gyro');

// ---------------------------------------------------------------- small DOM helpers

function setText(id, text) {
  const el = $(id);
  if (el.textContent !== text) el.textContent = text;
}

function setBadge(id, s, text) {
  const el = $(id);
  el.dataset.s = s;
  if (el.textContent !== text) el.textContent = text;
}

function icon(s) {
  const i = document.createElement('span');
  i.className = 'ico';
  i.dataset.s = s;
  i.textContent = ICON[s];
  i.setAttribute('aria-hidden', 'true');
  return i;
}

const logEl = $('log');
function log(text, cls) {
  const atEnd = logEl.scrollTop + logEl.clientHeight >= logEl.scrollHeight - 8;
  const row = document.createElement('div');
  if (cls) row.className = cls;
  row.textContent = text || ' ';
  logEl.append(row);
  while (logEl.childElementCount > 500) logEl.firstElementChild.remove();
  if (atEnd) logEl.scrollTop = logEl.scrollHeight;
}

// ---------------------------------------------------------------- module chips

const chipEls = new Map();
for (const m of MODULES) {
  const li = document.createElement('li');
  li.className = 'chip';
  li.dataset.s = 'unknown';
  const name = document.createElement('span');
  name.textContent = m.name;
  const state = document.createElement('span');
  state.className = 'state';
  li.append(icon('unknown'), name, state);
  $('chips').append(li);
  chipEls.set(m.id, li);
}

function liveStatus(id) {
  if (!latest || mode === 'off') return 'unknown';
  switch (id) {
    case 'imu': return latest.a ? 'good' : 'critical';
    case 'baro': return latest.p != null ? 'good' : 'critical';
    case 'tof': return latest.tof ? 'good' : 'critical';
    case 'pwr': return latest.vd > 3.15 && latest.vd < 3.45 ? 'good' : 'critical';
    case 'usb': return 'good'; // we are talking to it
    default: return 'unknown';
  }
}

function renderChips() {
  for (const m of MODULES) {
    let s = liveStatus(m.id);
    for (const [item, r] of results) {
      if (m.match.test(item) && RANK[TAG[r.tag].s] > RANK[s]) s = TAG[r.tag].s;
    }
    const li = chipEls.get(m.id);
    if (li.dataset.s !== s) {
      li.dataset.s = s;
      li.firstElementChild.dataset.s = s;
      li.firstElementChild.textContent = ICON[s];
    }
    li.lastElementChild.textContent = STATE_TEXT[s];
  }
}

// ---------------------------------------------------------------- self-test report

function renderResults() {
  const body = $('test-table').tBodies[0];
  body.replaceChildren();
  if (!results.size) {
    const tr = body.insertRow();
    tr.className = 'empty';
    const td = tr.insertCell();
    td.colSpan = 3;
    td.textContent = '还没有自检结果';
    return;
  }
  for (const [item, r] of results) {
    const tr = body.insertRow();
    const res = tr.insertCell();
    res.className = 'res';
    res.append(icon(TAG[r.tag].s), TAG[r.tag].text);
    tr.insertCell().textContent = item;
    const d = tr.insertCell();
    d.className = 'detail';
    d.textContent = r.msg;
  }
}

function parseReportLine(line) {
  if (/^==== .*self-test ====/.test(line)) {
    results.clear();
    setText('test-summary', '');
    renderResults();
    return;
  }
  const m = /^\[(PASS|WARN|FAIL|INFO)\] (.{14}) ?(.*)$/.exec(line);
  if (m) {
    sawReport = true;
    results.set(m[2].trim(), { tag: m[1], msg: m[3] });
    renderResults();
    return;
  }
  const s = /^---- (\d+) passed, (\d+) warnings, (\d+) failed ----/.exec(line);
  if (s) setText('test-summary', `通过 ${s[1]} 项 · 警告 ${s[2]} 项 · 失败 ${s[3]} 项`);
  if (/^RESULT:/.test(line)) testing = false;
}

// ---------------------------------------------------------------- incoming data

function handleLine(line) {
  if (line.startsWith('{')) {
    let msg;
    try { msg = JSON.parse(line); } catch (e) { log(line); return; }
    if (msg.k === 'hello') {
      setText('fw-info', `${msg.board} · 固件 v${msg.fw} · ${msg.mcu}`);
    } else if (msg.k === 'rfscan') {
      rfBars.set(msg.hits, msg.reps);
      setText('rf-title', `各信道检测到信号的次数（每个信道扫描 ${msg.reps} 次）`);
      $('btn-rfscan').disabled = false;
    } else if (typeof msg.t === 'number') {
      onFrame(msg);
    }
    return;
  }
  parseReportLine(line);
  log(line);
}

function resetLive() {
  latest = null;
  lastT = null;
  history = [];
  pressureRef = null;
  pressureSeed = [];
  ahrs.reset();
  accChart.clear();
  gyroChart.clear();
  altChart.clear();
}

function onFrame(m) {
  const t = m.t / 1000;
  if (lastT !== null && t < lastT) resetLive(); // board restarted
  const dt = lastT === null ? 0 : t - lastT;
  lastT = t;
  lastFrameAt = performance.now();
  if (testing && sawReport && performance.now() - testStarted > 500) testing = false; // stream is back

  if (m.a && m.g) {
    if (dt < 0.5) ahrs.update(m.g, m.a, dt);
    accChart.push(t, m.a);
    gyroChart.push(t, m.g);
  } else {
    accChart.push(t, [null, null, null]);
    gyroChart.push(t, [null, null, null]);
  }

  if (m.p != null) {
    if (pressureRef === null) {
      pressureSeed.push(m.p);
      if (pressureSeed.length >= 10) pressureRef = pressureSeed.reduce((a, b) => a + b, 0) / pressureSeed.length;
    }
    m.alt = pressureRef === null ? null : 44330 * (1 - Math.pow(m.p / pressureRef, 0.1903));
    altChart.push(t, [m.alt]);
  } else {
    m.alt = null;
    altChart.push(t, [null]);
  }

  history.push({ t, iq: m.iq, en: m.en });
  while (history.length > 2 && t - history[0].t > 1.2) history.shift();
  latest = m;

  // The firmware disarmed by itself (timeout, another command).  Frames that were already on
  // their way when "arm" was sent still say 0, so give it a moment before believing them.
  if (armed && !m.ar && performance.now() - armedAt > 600) setArmed(false);
}

function rateOf(pick) {
  if (history.length < 2) return null;
  const a = history[0], b = history[history.length - 1];
  const span = b.t - a.t;
  return span > 0.2 ? pick(a, b) / span : null;
}

// ---------------------------------------------------------------- rendering

const fmt = (v, d, unit = '') => (v == null || Number.isNaN(v) ? '–' : v.toFixed(d) + unit);

function buildIrqTable() {
  const body = $('irq-table').tBodies[0];
  for (const q of IRQS) {
    const tr = body.insertRow();
    tr.insertCell().textContent = q.name;
    tr.insertCell().textContent = q.pin;
    const hz = tr.insertCell();
    hz.className = 'num';
    hz.id = `irq-hz-${q.i}`;
    hz.textContent = '–';
    tr.insertCell().textContent = q.expect;
    const st = tr.insertCell();
    st.className = 'res';
    st.id = `irq-st-${q.i}`;
    st.textContent = '–';
  }
}
buildIrqTable();

function renderIrq() {
  for (const q of IRQS) {
    const hz = rateOf((a, b) => b.iq[q.i] - a.iq[q.i]);
    setText(`irq-hz-${q.i}`, hz == null ? '–' : `${hz.toFixed(0)} Hz`);
    let s = 'unknown', text = '–';
    if (hz != null) {
      if (!q.range) { s = 'unknown'; text = hz < 1 ? '空闲' : '有脉冲'; }
      else if (hz >= q.range[0] && hz <= q.range[1]) { s = 'good'; text = '正常'; }
      else if (hz < 1) { s = 'critical'; text = '无脉冲'; }
      else { s = 'warning'; text = '频率异常'; }
    }
    const cell = $(`irq-st-${q.i}`);
    if (cell.dataset.key !== s + text) {
      cell.dataset.key = s + text;
      cell.replaceChildren(icon(s), text);
    }
  }
}

function renderNumbers() {
  const m = mode === 'off' ? null : latest;
  const stale = mode !== 'off' && !testing && performance.now() - lastFrameAt > 1500;

  // connection line
  const dot = $('conn-dot');
  if (mode === 'off') { dot.dataset.state = 'off'; setText('conn-text', '未连接'); }
  else if (testing) { dot.dataset.state = mode === 'demo' ? 'demo' : 'on'; setText('conn-text', '正在运行测试，实时数据暂停…'); }
  else if (stale) { dot.dataset.state = 'stale'; setText('conn-text', '已连接，但没有收到数据'); }
  else { dot.dataset.state = mode === 'demo' ? 'demo' : 'on'; setText('conn-text', mode === 'demo' ? '演示数据（未连接板子）' : '已连接'); }

  // IMU
  const e = m && m.a && ahrs.started ? ahrs.euler() : null;
  setText('v-roll', e ? fmt(e.roll, 1, '°') : '–');
  setText('v-pitch', e ? fmt(e.pitch, 1, '°') : '–');
  setText('v-yaw', e ? fmt(e.yaw, 1, '°') : '–');
  setText('v-anorm', m && m.a ? fmt(Math.hypot(...m.a), 3, ' g') : '–');
  setText('v-imut', m ? fmt(m.it, 1, ' °C') : '–');
  for (let k = 0; k < 3; k++) {
    setText(`lv-acc-${k}`, m && m.a ? fmt(m.a[k], 3) : '–');
    setText(`lv-gyro-${k}`, m && m.g ? fmt(m.g[k], 1) : '–');
  }
  if (!m) setBadge('imu-badge', 'unknown', '无数据');
  else if (m.a) setBadge('imu-badge', 'good', '在线');
  else setBadge('imu-badge', 'critical', '无应答');

  // barometer
  setText('v-press', m && m.p != null ? fmt(m.p / 100, 2, ' hPa') : '–');
  setText('v-barot', m ? fmt(m.bt, 1, ' °C') : '–');
  setText('v-alt', m ? fmt(m.alt, 2, ' m') : '–');
  if (!m) setBadge('baro-badge', 'unknown', '无数据');
  else if (m.p != null) setBadge('baro-badge', 'good', '在线');
  else setBadge('baro-badge', 'critical', '无应答');

  // power
  const meter = $('bat-meter');
  if (!m) {
    setText('v-vsys', '–');
    meter.dataset.level = 'none';
    meter.firstElementChild.style.width = '0';
  } else {
    const off = m.vs < 0.5;
    setText('v-vsys', off ? '0 V（开关关闭或未接电池）' : fmt(m.vs, 2, ' V'));
    meter.dataset.level = off ? 'none' : m.vs < 3.3 ? 'critical' : m.vs < 3.5 ? 'warning' : 'normal';
    meter.firstElementChild.style.width = `${Math.max(0, Math.min(100, ((m.vs - 3.0) / 1.2) * 100))}%`;
  }
  setText('v-vdda', m ? fmt(m.vd, 3, ' V') : '–');
  setText('v-mcut', m ? fmt(m.mt, 0, ' °C') : '–');
  setText('v-vbus', m ? (m.vb ? '已插入' : '未插入') : '–');
  setText('v-chg', m ? (m.vb ? (m.ch ? '未充电 / 已充满' : '充电中') : '–') : '–');

  // ToF
  if (!m) { setBadge('tof-badge', 'unknown', '无数据'); setText('tof-text', '–'); }
  else if (m.tof) { setBadge('tof-badge', 'good', '在线'); setText('tof-text', '芯片应答正常，型号 ID 正确（0xEACC）。'); }
  else { setBadge('tof-badge', 'critical', '无应答'); setText('tof-text', 'I2C 地址 0x29 没有应答或型号 ID 不对。'); }

  // encoder
  setText('v-enc', m ? String(m.en) : '–');
  const er = rateOf((a, b) => (((b.en - a.en + 32768) & 0xffff) - 32768));
  setText('v-encrate', m && er != null ? `${er.toFixed(0)} /s` : '–');
  setText('v-enca', m ? (m.ea ? '高' : '低') : '–');
  setText('v-encb', m ? (m.eb ? '高' : '低') : '–');

  renderIrq();

  // actuators: what the board says it is really doing
  setText('motor-actual', m ? `${(m.m / 10).toFixed(1)}%` : '–');
  for (let k = 0; k < 3; k++) setText(`servo-act-${k}`, m ? (m.s[k] ? `${m.s[k]} µs` : '无脉冲') : '–');
  if (!m) setBadge('arm-badge', 'unknown', '未连接');
  else if (m.ar) setBadge('arm-badge', 'warning', '电机已解锁');
  else setBadge('arm-badge', 'good', '电机已上锁');

  renderChips();
}

let lastNumbers = 0;
function tick(now) {
  drawBoard($('view3d'), ahrs.started && mode !== 'off' ? ahrs.drawQuat() : [1, 0, 0, 0]);
  accChart.draw();
  gyroChart.draw();
  altChart.draw();
  if (now - lastNumbers > 100) { lastNumbers = now; renderNumbers(); }
  requestAnimationFrame(tick);
}

// ---------------------------------------------------------------- sending

function send(cmd, show = true) {
  if (!link) return;
  if (show) log('> ' + cmd, 'tx');
  link.send(cmd);
}

function runBlocking(cmd) {
  if (armed) setArmed(false);
  testing = true;
  sawReport = false;
  testStarted = performance.now();
  send(cmd);
  // if the board never answers, do not stay in "testing" forever
  setTimeout(() => { if (testing && performance.now() - testStarted >= 14000) testing = false; }, 14500);
}

// ---------------------------------------------------------------- actuators

function setArmed(on) {
  armed = on;
  if (on) armedAt = performance.now();
  $('btn-arm').textContent = on ? '上锁' : '解锁电机';
  $('btn-arm').classList.toggle('active', on);
  $('motor').disabled = !on;
  $('btn-stop').disabled = !on;
  clearInterval(motorTimer);
  motorTimer = null;
  if (!on) {
    $('motor').value = 0;
    setText('motor-val', '0%');
  } else {
    // keep refreshing the set-point: the firmware treats silence as "host gone"
    motorTimer = setInterval(() => {
      const pct = +$('motor').value;
      if (pct > 0) send(`M ${pct * 10}`, false);
    }, MOTOR_RESEND_MS);
  }
}

function stopMotor() {
  send('disarm');
  setArmed(false);
}

$('btn-arm').addEventListener('click', () => {
  if (armed) { stopMotor(); return; }
  send('arm');
  setArmed(true);
});
$('btn-stop').addEventListener('click', stopMotor);
$('motor').addEventListener('input', () => {
  const pct = +$('motor').value;
  setText('motor-val', `${pct}%`);
  if (armed) send(`M ${pct * 10}`, false);
});
$('motor-cap').addEventListener('change', () => {
  const cap = +$('motor-cap').value;
  $('motor').max = cap;
  if (+$('motor').value > cap) { $('motor').value = cap; $('motor').dispatchEvent(new Event('input')); }
});
window.addEventListener('keydown', (ev) => { if (ev.key === 'Escape' && armed) stopMotor(); });

function buildServos() {
  const host = $('servo-rows');
  for (let k = 0; k < 3; k++) {
    const row = document.createElement('div');
    row.className = 'servo-row';
    const name = document.createElement('span');
    name.textContent = `舵机 ${k + 1}`;
    name.title = SERVO_PINS[k];
    const slider = document.createElement('input');
    slider.type = 'range';
    slider.min = 500; slider.max = 2500; slider.step = 10; slider.value = 1500;
    slider.id = `servo-${k}`;
    slider.disabled = true;
    slider.setAttribute('aria-label', `舵机 ${k + 1} 脉宽`);
    const us = document.createElement('span');
    us.className = 'us';
    us.id = `servo-us-${k}`;
    us.textContent = '1500 µs';
    const btn = document.createElement('button');
    btn.className = 'btn';
    btn.id = `servo-on-${k}`;
    btn.textContent = '输出：关';
    btn.disabled = true;
    const out = document.createElement('span');
    out.className = 'out';
    out.append(`${SERVO_PINS[k]} 实际输出：`);
    const act = document.createElement('span');
    act.id = `servo-act-${k}`;
    act.textContent = '–';
    out.append(act);
    row.append(name, slider, us, btn, out);
    host.append(row);

    slider.addEventListener('input', () => {
      us.textContent = `${slider.value} µs`;
      if (!servoOn[k]) return;
      const now = performance.now();
      if (now >= servoDue[k]) { servoDue[k] = now + SERVO_SEND_MS; send(`S ${k + 1} ${slider.value}`, false); }
    });
    slider.addEventListener('change', () => { if (servoOn[k]) send(`S ${k + 1} ${slider.value}`, false); });
    btn.addEventListener('click', () => setServo(k, !servoOn[k]));
  }
}
buildServos();

function setServo(k, on) {
  servoOn[k] = on;
  const btn = $(`servo-on-${k}`);
  btn.textContent = on ? '输出：开' : '输出：关';
  btn.classList.toggle('active', on);
  send(`S ${k + 1} ${on ? $(`servo-${k}`).value : 0}`, true);
}

$('btn-servo-center').addEventListener('click', () => {
  for (let k = 0; k < 3; k++) {
    $(`servo-${k}`).value = 1500;
    setText(`servo-us-${k}`, '1500 µs');
    setServo(k, true);
  }
});
$('btn-servo-off').addEventListener('click', () => { for (let k = 0; k < 3; k++) setServo(k, false); });

// ---------------------------------------------------------------- tests, RF scan, console

for (const b of document.querySelectorAll('#test-buttons button')) {
  b.addEventListener('click', () => {
    if (b.dataset.cmd) runBlocking(b.dataset.cmd);
    else runBlocking(b.dataset.test ? `t ${b.dataset.test}` : 't');
  });
}
$('btn-rfscan').addEventListener('click', () => {
  $('btn-rfscan').disabled = true;
  setText('rf-title', '扫描中，约需 3 秒…');
  runBlocking('rfscan');
  setTimeout(() => { $('btn-rfscan').disabled = mode === 'off'; }, 8000);
});
$('btn-clear').addEventListener('click', () => logEl.replaceChildren());
$('cmd-form').addEventListener('submit', (ev) => {
  ev.preventDefault();
  const text = $('cmd').value.trim();
  if (!text) return;
  $('cmd').value = '';
  if (/^(M|S|arm|disarm|stream|echo|info)\b/.test(text)) send(text);
  else runBlocking(text);
});
$('btn-yaw0').addEventListener('click', () => ahrs.zeroYaw());
$('rate').addEventListener('change', () => send(`stream ${$('rate').value}`, false));

// ---------------------------------------------------------------- connect / disconnect

function setControls(on) {
  for (const el of document.querySelectorAll('#test-buttons button, #btn-rfscan, #cmd, #btn-send, #btn-arm, #btn-servo-center, #btn-servo-off')) {
    el.disabled = !on;
  }
  for (let k = 0; k < 3; k++) { $(`servo-${k}`).disabled = !on; $(`servo-on-${k}`).disabled = !on; }
  $('btn-connect').textContent = mode === 'serial' ? '断开' : '连接板子';
  $('btn-demo').textContent = mode === 'demo' ? '停止演示' : '演示数据';
  $('btn-demo').classList.toggle('active', mode === 'demo');
}

async function start(newLink, newMode) {
  await stop();
  link = newLink;
  link.onLine = handleLine;
  link.onClose = (reason) => {
    if (link !== newLink) return;
    link = null;
    mode = 'off';
    setArmed(false);
    for (let k = 0; k < 3; k++) { servoOn[k] = false; $(`servo-on-${k}`).textContent = '输出：关'; $(`servo-on-${k}`).classList.remove('active'); }
    setControls(false);
    setText('fw-info', '未连接');
    log(`— ${reason} —`, 'tx');
  };
  try {
    await link.open();
  } catch (e) {
    link = null;
    if (e.name !== 'NotFoundError') log(`无法打开串口：${e.message}`, 'tx'); // NotFoundError = picker cancelled
    return;
  }
  mode = newMode;
  resetLive();
  results.clear();
  renderResults();
  sawReport = false;
  testing = false;
  lastFrameAt = performance.now();
  setControls(true);
  log(newMode === 'demo' ? '— 演示数据 —' : '— 已连接 —', 'tx');

  // The firmware prints its banner and last report when the port opens; then switch it to
  // machine mode and start the stream.  Ask for the report explicitly if it did not come.
  setTimeout(() => {
    if (link !== newLink) return;
    send('echo off', false);
    send('info', false);
    send(`stream ${$('rate').value}`, false);
  }, 500);
  setTimeout(() => { if (link === newLink && !sawReport) send('last', false); }, 1500);
}

async function stop() {
  if (!link) return;
  const old = link;
  old.send('disarm');
  old.send('S 1 0'); old.send('S 2 0'); old.send('S 3 0');
  old.send('stream 0');
  old.send('echo on');
  await new Promise((r) => setTimeout(r, 120));
  await old.close();
}

$('btn-connect').addEventListener('click', () => (mode === 'serial' ? stop() : start(new SerialLink(), 'serial')));
$('btn-demo').addEventListener('click', () => (mode === 'demo' ? stop() : start(new DemoLink(), 'demo')));
window.addEventListener('pagehide', () => { if (link) { link.send('disarm'); link.send('stream 0'); link.send('echo on'); } });
if (serialSupported) {
  navigator.serial.addEventListener('disconnect', () => { if (mode === 'serial' && link) link.close(); });
} else {
  $('no-serial').hidden = false;
  $('btn-connect').disabled = true;
}

setControls(false);
requestAnimationFrame(tick);
