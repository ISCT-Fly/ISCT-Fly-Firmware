/* Telemetry stream and live actuator control, used by the web test bench (docs/).
 *
 * "stream <hz>" makes the board send one JSON object per line on the USB port:
 *
 *   {"t":12345,"a":[ax,ay,az],"g":[gx,gy,gz],"it":27.1,"p":101325.0,"bt":26.80,"tof":1,
 *    "vs":3.980,"vd":3.301,"mt":31.2,"vb":1,"ch":0,"en":-12,"ea":1,"eb":0,
 *    "iq":[tof,rf,gyro,accel,baro],"m":0,"s":[0,0,0],"ar":0}
 *
 *   t   ms since boot            a   accel [g]            g   gyro [deg/s]      it  IMU temp [C]
 *   p   pressure [Pa]            bt  baro temp [C]        tof 1 = VL53L1X answers with its ID
 *   vs  VSYS [V]                 vd  VDDA [V]             mt  MCU temp [C]
 *   vb  VBUS_DET pin             ch  CHG_DET pin          en  encoder count, ea/eb pin levels
 *   iq  interrupt edge counters  m   motor duty [permille]
 *   s   servo pulse widths [us], 0 = no pulses            ar  1 = motor armed
 *
 * A sensor that does not answer is reported as null.  IMU values are sent every frame; the
 * slower sources (ADC, barometer, ToF presence) are refreshed 10 times per second. */
#include "app.h"
#include <stdarg.h>
#include <stdio.h>

#define SLOW_PERIOD_MS  100u
#define RETRY_PERIOD_MS 2000u /* how often a silent sensor is probed again */

static int rate_hz;
static uint32_t period_ms, next_ms, slow_ms;
static uint32_t imu_retry_ms, baro_retry_ms;
static bool imu_ok, baro_ok, tof_ok;
static float baro_p, baro_t, vsys, vdda, mcu_t;
static unsigned slow_tick;

static bool armed, live_active;
static uint32_t live_ms;

/* ---------------------------------------------------------------- stream */

void telem_set_rate(int hz)
{
    if (hz < 0) {
        hz = 0;
    }
    if (hz > 100) {
        hz = 100;
    }
    rate_hz = hz;
    if (hz) {
        uint32_t now = HAL_GetTick();
        period_ms = 1000u / (uint32_t)hz;
        next_ms = slow_ms = imu_retry_ms = baro_retry_ms = now;
        imu_ok = baro_ok = true; /* optimistic: first frame probes both */
        slow_tick = 0;
    }
}

int telem_rate(void)
{
    return rate_hz;
}

static void add(char *buf, size_t size, size_t *len, const char *fmt, ...)
{
    va_list ap;
    if (*len >= size) {
        return;
    }
    va_start(ap, fmt);
    int n = vsnprintf(buf + *len, size - *len, fmt, ap);
    va_end(ap);
    if (n > 0) {
        *len += (size_t)n;
        if (*len >= size) {
            *len = size - 1;
        }
    }
}

static void refresh_slow(uint32_t now)
{
    vdda = adc_vdda();
    vsys = adc_vsys(vdda);
    mcu_t = adc_mcu_temp(vdda);

    if (baro_ok || (int32_t)(now - baro_retry_ms) >= 0) {
        baro_ok = baro_read(&baro_p, &baro_t);
        if (!baro_ok) {
            baro_retry_ms = now + RETRY_PERIOD_MS;
        }
    }
    if (slow_tick % 10u == 0u) {
        tof_ok = tof_online();
    }
    slow_tick++;
}

void telem_poll(void)
{
    if (rate_hz == 0) {
        return;
    }
    uint32_t now = HAL_GetTick();
    if ((int32_t)(now - next_ms) < 0) {
        return;
    }
    next_ms += period_ms;
    if ((int32_t)(now - next_ms) >= 0) {
        next_ms = now + period_ms; /* fell behind (a blocking command ran): do not burst */
    }
    if ((int32_t)(now - slow_ms) >= 0) {
        slow_ms = now + SLOW_PERIOD_MS;
        refresh_slow(now);
    }

    float a[3] = {0}, g[3] = {0}, imu_t = 0;
    if (imu_ok || (int32_t)(now - imu_retry_ms) >= 0) {
        imu_ok = imu_read(a, g, &imu_t);
        if (!imu_ok) {
            imu_retry_ms = HAL_GetTick() + RETRY_PERIOD_MS;
        }
    }

    char b[448];
    size_t n = 0;
    add(b, sizeof(b), &n, "{\"t\":%lu", (unsigned long)now);
    if (imu_ok) {
        add(b, sizeof(b), &n, ",\"a\":[%.4f,%.4f,%.4f],\"g\":[%.2f,%.2f,%.2f],\"it\":%.1f", (double)a[0], (double)a[1],
            (double)a[2], (double)g[0], (double)g[1], (double)g[2], (double)imu_t);
    } else {
        add(b, sizeof(b), &n, ",\"a\":null,\"g\":null,\"it\":null");
    }
    if (baro_ok) {
        add(b, sizeof(b), &n, ",\"p\":%.1f,\"bt\":%.2f", (double)baro_p, (double)baro_t);
    } else {
        add(b, sizeof(b), &n, ",\"p\":null,\"bt\":null");
    }
    add(b, sizeof(b), &n, ",\"tof\":%d,\"vs\":%.3f,\"vd\":%.3f,\"mt\":%.1f,\"vb\":%d,\"ch\":%d", tof_ok, (double)vsys,
        (double)vdda, (double)mcu_t, HAL_GPIO_ReadPin(VBUS_DET_PORT, VBUS_DET_PIN) == GPIO_PIN_SET,
        HAL_GPIO_ReadPin(CHG_DET_PORT, CHG_DET_PIN) == GPIO_PIN_SET);
    add(b, sizeof(b), &n, ",\"en\":%d,\"ea\":%d,\"eb\":%d", enc_count(),
        HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_6) == GPIO_PIN_SET, HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_7) == GPIO_PIN_SET);
    add(b, sizeof(b), &n, ",\"iq\":[%lu,%lu,%lu,%lu,%lu]", (unsigned long)g_irq_count[IRQ_TOF],
        (unsigned long)g_irq_count[IRQ_RF], (unsigned long)g_irq_count[IRQ_GYRO],
        (unsigned long)g_irq_count[IRQ_ACC], (unsigned long)g_irq_count[IRQ_BARO]);
    add(b, sizeof(b), &n, ",\"m\":%u,\"s\":[%u,%u,%u],\"ar\":%d}\r\n", motor_get_permille(), servo_get_us(0),
        servo_get_us(1), servo_get_us(2), armed);
    cdc_write((const uint8_t *)b, n);
}

/* ---------------------------------------------------------------- live motor control
 *
 * The motor hangs directly on the battery, so a set-point from the host is only honoured
 * while armed, and it expires: if no new set-point arrives within ACT_TIMEOUT_MS (browser
 * tab closed, cable pulled, program crashed) the motor is switched off and disarmed. */

void act_arm(bool on)
{
    if (!on) {
        motor_set_permille(0);
        live_active = false;
    }
    armed = on;
}

bool act_armed(void)
{
    return armed;
}

bool act_motor_live(int permille)
{
    if (!armed) {
        return false;
    }
    if (permille < 0) {
        permille = 0;
    }
    if (permille > 1000) {
        permille = 1000;
    }
    motor_set_permille((uint16_t)permille);
    live_ms = HAL_GetTick();
    live_active = permille > 0;
    return true;
}

void act_poll(void)
{
    if (live_active && HAL_GetTick() - live_ms > ACT_TIMEOUT_MS) {
        act_safe_stop();
        P("motor stopped: no set-point for %u ms (disarmed)\n", ACT_TIMEOUT_MS);
    }
}

void act_safe_stop(void)
{
    motor_set_permille(0);
    live_active = false;
    armed = false;
}
