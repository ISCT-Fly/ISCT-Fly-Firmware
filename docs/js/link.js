// Transport to the board.  Two interchangeable implementations with the same interface:
//   open()            -> Promise
//   send(line)        -> queue one command line
//   close()           -> Promise
//   onLine(text)      -> called for every received line
//   onClose(reason)   -> called once when the link ends
//
// SerialLink talks to the firmware's USB virtual COM port through the Web Serial API.
// DemoLink imitates the firmware in the browser so the page can be tried without a board.

export const serialSupported = 'serial' in navigator;

export class SerialLink {
  constructor() {
    this.onLine = () => {};
    this.onClose = () => {};
    this.port = null;
    this.reader = null;
    this.writer = null;
    this.closing = false;
  }

  async open() {
    // ST's USB vendor ID; the firmware enumerates as 0483:5740
    this.port = await navigator.serial.requestPort({ filters: [{ usbVendorId: 0x0483 }] });
    await this.port.open({ baudRate: 115200 });
    try {
      await this.port.setSignals({ dataTerminalReady: true, requestToSend: true });
    } catch (e) { /* not every platform lets us set the lines; the firmware copes */ }
    this.writer = this.port.writable.getWriter();
    this.readLoop();
  }

  async readLoop() {
    const decoder = new TextDecoder();
    let pending = '';
    let reason = '已断开';
    try {
      this.reader = this.port.readable.getReader();
      for (;;) {
        const { value, done } = await this.reader.read();
        if (done) break;
        pending += decoder.decode(value, { stream: true });
        let nl;
        while ((nl = pending.indexOf('\n')) >= 0) {
          const line = pending.slice(0, nl).replace(/\r$/, '');
          pending = pending.slice(nl + 1);
          this.onLine(line);
        }
        if (pending.length > 8192) pending = ''; // garbage without line ends: do not grow forever
      }
    } catch (e) {
      if (!this.closing) reason = '连接中断：' + e.message;
    } finally {
      try { this.reader && this.reader.releaseLock(); } catch (e) { /* already released */ }
      if (!this.closing) {
        await this.close(true);
      }
      this.onClose(reason);
    }
  }

  send(line) {
    if (!this.writer) return;
    this.writer.write(new TextEncoder().encode(line + '\r')).catch(() => {});
  }

  async close(fromReader = false) {
    this.closing = true;
    try { if (!fromReader && this.reader) await this.reader.cancel(); } catch (e) { /* ignore */ }
    try { if (this.writer) { await this.writer.close(); } } catch (e) { /* ignore */ }
    this.writer = null;
    try { if (this.port) await this.port.close(); } catch (e) { /* ignore */ }
    this.port = null;
  }
}

// ---------------------------------------------------------------- demo

const DEMO_REPORT = [
  '',
  '==== FlapFC board test v1.1 - self-test ====',
  '[PASS] HSE crystal    8 MHz crystal running, SYSCLK 168 MHz',
  '[INFO] Reset cause    power-on (NRST pin)',
  '[PASS] 3V3 rail       VDDA = 3.301 V (TPS63001), MCU 31 C',
  '[PASS] VSYS / battery 3.92 V',
  '[INFO] USB power      VBUS present, charger: charging',
  '[PASS] I2C idle       SCL=1 SDA=1',
  '[INFO] I2C scan       0x29 0x76 ',
  '[PASS] BMP388         id=0x50  P=1009.6 hPa  T=27.4 C',
  '[PASS] BARO_INT       25 pulses in 0.5 s (expect ~25) on PC15',
  '[PASS] VL53L1X        model id 0xEACC (expect 0xEACC), boot=1',
  '[PASS] TOF_XSHUT      low: sensor off, high: sensor back',
  '[PASS] TOF_INT        PB0 idle level 1 (expect 1, 10k pull-up); ranging not exercised',
  '[PASS] BMI088 accel   id=0x1E  a=[+0.012 -0.008 +1.003] g  |a|=1.003 g  T=28.5 C',
  '[PASS] BMI088 gyro    id=0x0F  w=[+0.06 -0.12 +0.03] dps',
  '[PASS] ACC_INT        50 pulses in 0.5 s (expect ~50) on PB12',
  '[PASS] GYRO_INT       498 pulses in 0.5 s (expect ~500) on PB7',
  '[PASS] nRF24 SPI      status=0x0E, register read/write ok, nRF24L01+',
  '[PASS] nRF24 TX       packet sent, status 0x1E (MAX_RT, nobody listening - expected)',
  '[PASS] RF_IRQ         PC4 idle=1, after TX=0, after clear=1, edges=1',
  '[PASS] nRF24 RX scan  carrier seen on 37 channel-samples, strongest at 2437 MHz',
  '[PASS] SD card        SDHC/SDXC v2.x, 121942 MB, manufacturer 0x03, name SN128',
  '[PASS] SD read 400k   4/4 blocks ok',
  '[PASS] SD read 8M     8/8 blocks ok',
  '[WARN] SD read 24M    6/8 blocks ok: RX_OVERRUN (0x00000020)',
  '[INFO] SD block 0     boot signature 0x55AA present',
  '[INFO] Encoder        A=1 B=0 count=0 (turn the shaft and use \'enc\')',
  '[PASS] USB CDC        enumerated by the host',
  '[PASS] VBUS_DET       PA9 = 1 while USB is connected (divider R3/R4)',
  '---- 24 passed, 1 warnings, 0 failed ----',
  'RESULT: PASS (green LED)',
];

function quatFromEuler(roll, pitch, yaw) {
  const cr = Math.cos(roll / 2), sr = Math.sin(roll / 2);
  const cp = Math.cos(pitch / 2), sp = Math.sin(pitch / 2);
  const cy = Math.cos(yaw / 2), sy = Math.sin(yaw / 2);
  return [cr * cp * cy + sr * sp * sy, sr * cp * cy - cr * sp * sy, cr * sp * cy + sr * cp * sy, cr * cp * sy - sr * sp * cy];
}

function quatMul(a, b) {
  return [
    a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3],
    a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2],
    a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1],
    a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0],
  ];
}

export class DemoLink {
  constructor() {
    this.onLine = () => {};
    this.onClose = () => {};
    this.timer = null;
    this.rate = 0;
    this.t0 = performance.now();
    this.armed = false;
    this.motor = 0;
    this.motorAt = 0;
    this.servo = [0, 0, 0];
    this.enc = 0;
    this.irq = [0, 0, 0, 0, 0];
    this.lastT = 0;
    this.busyUntil = 0;
  }

  async open() {
    setTimeout(() => {
      this.emit(['', 'FlapFC board test v1.1  (demo data generated in the browser)']);
      this.emit(DEMO_REPORT);
    }, 150);
  }

  emit(lines) { for (const l of lines) this.onLine(l); }

  pose(t) {
    const d = Math.PI / 180;
    return quatFromEuler(28 * d * Math.sin(0.9 * t), 18 * d * Math.sin(0.6 * t + 1.0), 0.35 * t);
  }

  frame() {
    const now = performance.now();
    if (now < this.busyUntil) return; // a blocking command is "running": the stream pauses
    const t = (now - this.t0) / 1000;
    const dt = Math.max(1e-3, t - this.lastT);
    this.lastT = t;

    // body rates and specific force that are consistent with the simulated pose
    const q = this.pose(t), q2 = this.pose(t + 0.002);
    const dq = quatMul([q[0], -q[1], -q[2], -q[3]], q2);
    const noise = (s) => (Math.random() - 0.5) * s;
    const g = [1, 2, 3].map((i) => (2 * dq[i] / 0.002) * 180 / Math.PI + noise(0.3));
    const a = [
      2 * (q[1] * q[3] - q[0] * q[2]) + noise(0.01),
      2 * (q[0] * q[1] + q[2] * q[3]) + noise(0.01),
      q[0] * q[0] - q[1] * q[1] - q[2] * q[2] + q[3] * q[3] + noise(0.01),
    ];

    if (this.motor > 0 && now - this.motorAt > 600) { // same dead-man rule as the firmware
      this.motor = 0;
      this.armed = false;
      this.onLine('motor stopped: no set-point for 600 ms (disarmed)');
    }
    this.enc = (this.enc + Math.round(this.motor * 1.2 * dt)) % 65536;
    this.irq[2] += Math.round(1000 * dt);
    this.irq[3] += Math.round(100 * dt);
    this.irq[4] += Math.round(50 * dt);

    const vs = 3.92 - 0.0004 * this.motor + noise(0.004);
    const p = 100960 - 12 * 1.5 * Math.sin(0.25 * t) + noise(2.5);
    const msg = {
      t: Math.round(t * 1000),
      a: a.map((v) => +v.toFixed(4)), g: g.map((v) => +v.toFixed(2)), it: 28.5,
      p: +p.toFixed(1), bt: 27.4, tof: 1,
      vs: +vs.toFixed(3), vd: 3.301, mt: 31.2, vb: 1, ch: 0,
      en: this.enc > 32767 ? this.enc - 65536 : this.enc, ea: (this.enc >> 1) & 1, eb: this.enc & 1,
      iq: this.irq.slice(), m: this.motor, s: this.servo.slice(), ar: this.armed ? 1 : 0,
    };
    this.onLine(JSON.stringify(msg));
  }

  send(line) {
    const argv = line.trim().split(/\s+/);
    const c = argv[0];
    if (c === 'M') {
      if (this.armed) { this.motor = Math.max(0, Math.min(1000, +argv[1] || 0)); this.motorAt = performance.now(); }
      else this.onLine('M ignored: not armed');
      return;
    }
    if (c === 'S') { this.servo[(+argv[1] || 1) - 1] = +argv[2] || 0; return; }
    if (c === 'arm') { this.armed = true; this.onLine('armed: motor accepts M set-points'); return; }
    if (c === 'disarm') { this.armed = false; this.motor = 0; this.onLine('disarmed, motor off'); return; }
    if (c === 'echo') return;
    if (c === 'stream') {
      this.rate = Math.max(0, Math.min(100, +argv[1] || 0));
      clearInterval(this.timer);
      if (this.rate) this.timer = setInterval(() => this.frame(), 1000 / this.rate);
      return;
    }
    if (c === 'info') {
      this.onLine('{"k":"hello","fw":"1.1","name":"FlapFC board test","board":"FlapFC 30x30 (演示)","mcu":"STM32F446RET6","uid":"DEMO","hse":1,"servos":3}');
      return;
    }

    // everything else stops the motor first, exactly like the firmware
    this.motor = 0;
    this.armed = false;
    if (c === 't' || c === 'test') {
      const want = argv[1];
      const groups = { imu: /BMI088|ACC_INT|GYRO_INT/, baro: /BMP388|BARO_INT/, tof: /VL53L1X|TOF_/, rf: /nRF24|RF_IRQ/, sd: /^.{7}SD /, power: /3V3|VSYS|USB power/ };
      const lines = want ? DEMO_REPORT.filter((l) => groups[want] && groups[want].test(l)) : DEMO_REPORT;
      this.busyUntil = performance.now() + (want ? 900 : 2500);
      setTimeout(() => this.emit(lines), want ? 800 : 2400);
    } else if (c === 'last') {
      this.emit(DEMO_REPORT);
    } else if (c === 'rfscan') {
      this.busyUntil = performance.now() + 1500;
      setTimeout(() => {
        const hits = Array.from({ length: 126 }, (_, ch) => {
          const wifi = [12, 37, 62].reduce((s, c0) => s + 30 * Math.exp(-((ch - c0) ** 2) / 60), 0);
          return Math.max(0, Math.min(40, Math.round(wifi + Math.random() * 3 - 1.5)));
        });
        this.onLine(JSON.stringify({ k: 'rfscan', reps: 40, hits }));
      }, 1400);
    } else if (c === 'sdw') {
      this.onLine('[PASS] SD write       block 1 rewritten with its own data, read-back matches');
    } else if (c === 'help' || c === '?') {
      this.emit(['(demo) commands: t, t <name>, last, rfscan, sdw, stream <hz>, arm, disarm, M <0..1000>, S <n> <us>']);
    } else if (c) {
      this.onLine(`unknown command '${c}' - type 'help'`);
    }
  }

  async close() {
    clearInterval(this.timer);
    this.timer = null;
    this.onClose('演示已停止');
  }
}
