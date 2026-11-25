// Attitude from gyro + accelerometer (Mahony complementary filter, no magnetometer).
// The quaternion q = [w, x, y, z] rotates sensor-frame vectors into a world frame with Z up.
// The accelerometer convention is "reads +1 g on the axis that points up when at rest".

const D2R = Math.PI / 180;

export class Ahrs {
  constructor(kp = 1.2) {
    this.kp = kp;
    this.reset();
  }

  reset() {
    this.q = [1, 0, 0, 0];
    this.started = false;
    this.yawZero = 0;
  }

  /** gyro in deg/s, accel in g, dt in seconds */
  update(gyro, accel, dt) {
    let [q0, q1, q2, q3] = this.q;
    let gx = gyro[0] * D2R, gy = gyro[1] * D2R, gz = gyro[2] * D2R;
    const norm = Math.hypot(accel[0], accel[1], accel[2]);

    // Only trust the accelerometer as a gravity reference when it reads roughly 1 g.
    if (norm > 0.6 && norm < 1.4) {
      const ax = accel[0] / norm, ay = accel[1] / norm, az = accel[2] / norm;
      if (!this.started) {
        // start level-correct instead of waiting for the filter to converge
        const roll = Math.atan2(ay, az);
        const pitch = Math.atan2(-ax, Math.hypot(ay, az));
        const cr = Math.cos(roll / 2), sr = Math.sin(roll / 2);
        const cp = Math.cos(pitch / 2), sp = Math.sin(pitch / 2);
        this.q = [cr * cp, sr * cp, cr * sp, -sr * sp];
        this.started = true;
        return;
      }
      // estimated "up" in the sensor frame, and its error against the measured one
      const vx = 2 * (q1 * q3 - q0 * q2);
      const vy = 2 * (q0 * q1 + q2 * q3);
      const vz = q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3;
      gx += this.kp * (ay * vz - az * vy);
      gy += this.kp * (az * vx - ax * vz);
      gz += this.kp * (ax * vy - ay * vx);
    }
    if (!this.started) return;

    const h = 0.5 * dt;
    const n0 = q0 + (-q1 * gx - q2 * gy - q3 * gz) * h;
    const n1 = q1 + (q0 * gx + q2 * gz - q3 * gy) * h;
    const n2 = q2 + (q0 * gy - q1 * gz + q3 * gx) * h;
    const n3 = q3 + (q0 * gz + q1 * gy - q2 * gx) * h;
    const n = Math.hypot(n0, n1, n2, n3) || 1;
    this.q = [n0 / n, n1 / n, n2 / n, n3 / n];
  }

  /** roll / pitch / yaw in degrees (Z-Y-X order) */
  euler() {
    const [q0, q1, q2, q3] = this.q;
    const roll = Math.atan2(2 * (q0 * q1 + q2 * q3), 1 - 2 * (q1 * q1 + q2 * q2));
    const pitch = Math.asin(Math.max(-1, Math.min(1, 2 * (q0 * q2 - q3 * q1))));
    const yaw = Math.atan2(2 * (q0 * q3 + q1 * q2), 1 - 2 * (q2 * q2 + q3 * q3));
    let y = (yaw - this.yawZero) / D2R;
    y = ((y + 180) % 360 + 360) % 360 - 180;
    return { roll: roll / D2R, pitch: pitch / D2R, yaw: y };
  }

  zeroYaw() {
    const [q0, q1, q2, q3] = this.q;
    this.yawZero = Math.atan2(2 * (q0 * q3 + q1 * q2), 1 - 2 * (q2 * q2 + q3 * q3));
  }

  /** the quaternion with the yaw offset removed, for drawing */
  drawQuat() {
    const c = Math.cos(-this.yawZero / 2), s = Math.sin(-this.yawZero / 2);
    const [q0, q1, q2, q3] = this.q;
    // rotation about world Z applied on the left: [c,0,0,s] * q
    return [c * q0 - s * q3, c * q1 - s * q2, c * q2 + s * q1, c * q3 + s * q0];
  }
}
