/* Interactive commands for the parts that need a human: actuators, live sensor readings. */
#include "app.h"
#include <stdlib.h>
#include <string.h>

#define MOTOR_MAX_MS 10000

static void cmd_help(void)
{
    P("Self-test\n");
    P("  t                 run the whole self-test again\n");
    P("  t <name>          one test: clock power i2c baro tof imu rf sd enc usb\n");
    P("  last              print the last full report again\n");
    P("Live readings (any key stops)\n");
    P("  imu | baro | bat | enc | irq\n");
    P("Radio / SD\n");
    P("  rfscan            2.4 GHz activity per channel (antenna check)\n");
    P("  sdw               SD write check: block 1 rewritten with its own data\n");
    P("Actuators (nothing moves unless you type these)\n");
    P("  servo <1|2|3|all> <500..2500 | off>   pulse width in us\n");
    P("  sweep             all servos 1500 -> 1000 -> 2000 -> 1500, then off\n");
    P("  motor <1..100> [ms]   duty in %%, default 1000 ms, max %d ms, any key stops\n", MOTOR_MAX_MS);
    P("  led               walk green / blue / red\n");
    P("System\n");
    P("  dfu               reboot into the USB bootloader for flashing\n");
    P("  reboot\n");
}

static void wait_ms_or_key(uint32_t ms, bool *stopped)
{
    uint32_t t0 = HAL_GetTick();
    while (HAL_GetTick() - t0 < ms) {
        if (con_key_pressed()) {
            *stopped = true;
            return;
        }
        cdc_poll();
    }
}

static void cmd_test(const char *name)
{
    static const struct { const char *name; void (*fn)(void); } tests[] = {
        {"clock", test_clock}, {"power", test_power}, {"i2c", test_i2c_bus}, {"baro", test_baro},
        {"tof", test_tof},     {"imu", test_imu},     {"rf", test_rf},       {"sd", test_sd},
        {"enc", test_encoder}, {"usb", test_usb},
    };
    if (name == NULL) {
        con_capture(true);
        selftest_run();
        con_capture(false);
        return;
    }
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        if (strcmp(name, tests[i].name) == 0) {
            tests[i].fn();
            return;
        }
    }
    P("unknown test '%s'\n", name);
}

static void cmd_servo(const char *which, const char *value)
{
    if (which == NULL || value == NULL) {
        P("usage: servo <1|2|3|all> <500..2500 | off>\n");
        return;
    }
    int us = strcmp(value, "off") == 0 ? 0 : atoi(value);
    if (us != 0 && (us < 500 || us > 2500)) {
        P("pulse width must be 500..2500 us, or 'off'\n");
        return;
    }
    int first = 0, last = SERVO_COUNT - 1;
    if (strcmp(which, "all") != 0) {
        int n = atoi(which);
        if (n < 1 || n > SERVO_COUNT) {
            P("servo number must be 1..%d or 'all'\n", SERVO_COUNT);
            return;
        }
        first = last = n - 1;
    }
    for (int i = first; i <= last; i++) {
        servo_set_us(i, (uint16_t)us);
    }
    if (us) {
        P("servo %s -> %d us (PA0/PA1/PA2, 50 Hz). 'servo all off' stops the pulses.\n", which, us);
    } else {
        P("servo %s off\n", which);
    }
}

static void cmd_sweep(void)
{
    static const uint16_t steps[] = {1500, 1000, 2000, 1500};
    bool stopped = false;

    P("sweeping all servos, any key stops\n");
    for (size_t s = 0; s < sizeof(steps) / sizeof(steps[0]) && !stopped; s++) {
        for (int i = 0; i < SERVO_COUNT; i++) {
            servo_set_us(i, steps[s]);
        }
        P("  %u us\n", steps[s]);
        wait_ms_or_key(800, &stopped);
    }
    for (int i = 0; i < SERVO_COUNT; i++) {
        servo_set_us(i, 0);
    }
    P("servos off\n");
}

static void cmd_motor(const char *duty, const char *dur)
{
    if (duty == NULL) {
        P("usage: motor <1..100> [ms]\n");
        return;
    }
    int pct = atoi(duty);
    int ms = dur ? atoi(dur) : 1000;
    if (pct < 1 || pct > 100 || ms < 1 || ms > MOTOR_MAX_MS) {
        P("duty must be 1..100 %%, time 1..%d ms\n", MOTOR_MAX_MS);
        return;
    }
    bool stopped = false;
    con_key_pressed(); /* drop anything typed ahead so it cannot cancel or mask a stop */
    P("MOTOR ON %d %% for %d ms (PA8, 20 kHz) - any key stops\n", pct, ms);
    motor_set_permille((uint16_t)(pct * 10));
    wait_ms_or_key((uint32_t)ms, &stopped);
    motor_set_permille(0);
    P("motor off%s, VSYS now %.2f V\n", stopped ? " (stopped by key)" : "", (double)adc_vsys(adc_vdda()));
}

static void cmd_led(void)
{
    static const struct { uint8_t mask; const char *name; } leds[] = {
        {LED_G, "green (PB1)"}, {LED_B, "blue (PB2)"}, {LED_R, "red (PB10)"},
    };
    for (size_t i = 0; i < 3; i++) {
        P("  %s\n", leds[i].name);
        led_set(leds[i].mask);
        HAL_Delay(600);
    }
    led_set(selftest_fail_count() ? LED_R : LED_G);
}

/* Print one line every 100 ms until a key is pressed. */
static void live(const char *what)
{
    bool stopped = false;
    float a[3], g[3], t, p;

    con_key_pressed();
    P("%s - any key stops\n", what);
    while (!stopped) {
        if (strcmp(what, "imu") == 0) {
            if (!imu_read(a, g, &t)) {
                P("BMI088 not responding\n");
                return;
            }
            P("acc[g] %+6.3f %+6.3f %+6.3f   gyro[dps] %+8.2f %+8.2f %+8.2f   %.1f C\n", (double)a[0], (double)a[1],
              (double)a[2], (double)g[0], (double)g[1], (double)g[2], (double)t);
        } else if (strcmp(what, "baro") == 0) {
            if (!baro_read(&p, &t)) {
                P("BMP388 not responding\n");
                return;
            }
            P("P = %.2f hPa   T = %.2f C\n", (double)(p / 100.0f), (double)t);
        } else if (strcmp(what, "bat") == 0) {
            float vdda = adc_vdda();
            P("VSYS = %.3f V   VDDA = %.3f V   VBUS_DET = %d   CHG_DET = %d\n", (double)adc_vsys(vdda), (double)vdda,
              HAL_GPIO_ReadPin(VBUS_DET_PORT, VBUS_DET_PIN) == GPIO_PIN_SET,
              HAL_GPIO_ReadPin(CHG_DET_PORT, CHG_DET_PIN) == GPIO_PIN_SET);
        } else if (strcmp(what, "enc") == 0) {
            P("count = %6d   A = %d   B = %d\n", enc_count(), HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_6) == GPIO_PIN_SET,
              HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_7) == GPIO_PIN_SET);
        } else { /* irq */
            P("edges: ToF %lu  RF %lu  gyro %lu  accel %lu  baro %lu\n", (unsigned long)g_irq_count[IRQ_TOF],
              (unsigned long)g_irq_count[IRQ_RF], (unsigned long)g_irq_count[IRQ_GYRO],
              (unsigned long)g_irq_count[IRQ_ACC], (unsigned long)g_irq_count[IRQ_BARO]);
        }
        wait_ms_or_key(100, &stopped);
    }
}

void cli_execute(const char *line)
{
    char buf[96];
    char *argv[4] = {0};
    int argc = 0;

    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    for (char *tok = strtok(buf, " \t"); tok && argc < 4; tok = strtok(NULL, " \t")) {
        argv[argc++] = tok;
    }
    if (argc == 0) {
        return;
    }
    const char *c = argv[0];

    if (!strcmp(c, "help") || !strcmp(c, "?")) {
        cmd_help();
    } else if (!strcmp(c, "t") || !strcmp(c, "test")) {
        cmd_test(argv[1]);
    } else if (!strcmp(c, "last")) {
        con_replay();
    } else if (!strcmp(c, "imu") || !strcmp(c, "baro") || !strcmp(c, "bat") || !strcmp(c, "enc") ||
               !strcmp(c, "irq")) {
        live(c);
    } else if (!strcmp(c, "rfscan")) {
        rf_scan(40, true);
    } else if (!strcmp(c, "sdw")) {
        sd_writeback_test();
    } else if (!strcmp(c, "servo")) {
        cmd_servo(argv[1], argv[2]);
    } else if (!strcmp(c, "sweep")) {
        cmd_sweep();
    } else if (!strcmp(c, "motor")) {
        cmd_motor(argv[1], argv[2]);
    } else if (!strcmp(c, "led")) {
        cmd_led();
    } else if (!strcmp(c, "dfu")) {
        P("entering USB bootloader; this port will disappear. Flash with: pio run -e dfu -t upload\n");
        request_dfu();
    } else if (!strcmp(c, "reboot")) {
        HAL_Delay(50);
        NVIC_SystemReset();
    } else {
        P("unknown command '%s' - type 'help'\n", c);
    }
}
