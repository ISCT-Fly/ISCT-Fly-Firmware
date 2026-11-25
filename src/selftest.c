/* Self-test sequencing, result bookkeeping and the tests that need no driver of their own. */
#include "app.h"
#include <stdarg.h>
#include <stdio.h>

static int n_pass, n_warn, n_fail;

void report(res_t r, const char *item, const char *fmt, ...)
{
    static const char *const tag[] = {"PASS", "WARN", "FAIL", "INFO"};
    char msg[200];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    P("[%s] %-14s %s\n", tag[r], item, msg);
    if (r == R_PASS) n_pass++;
    if (r == R_WARN) n_warn++;
    if (r == R_FAIL) n_fail++;
}

int selftest_fail_count(void)
{
    return n_fail;
}

void selftest_summary(void)
{
    P("---- %d passed, %d warnings, %d failed ----\n", n_pass, n_warn, n_fail);
    P("%s\n", n_fail ? "RESULT: FAIL (red LED)" : "RESULT: PASS (green LED)");
    P("Not covered automatically: servo outputs, motor driver, encoder, ToF ranging, SD write.\n");
    P("Use the commands (type 'help') to exercise them by hand.\n");
}

void selftest_run(void)
{
    n_pass = n_warn = n_fail = 0;
    led_set(LED_B);
    P("\n==== %s v%s - self-test ====\n", FW_NAME, FW_VERSION);
    test_clock();
    test_power();
    test_i2c_bus();
    test_baro();
    test_tof();
    test_imu();
    test_rf();
    test_sd();
    test_encoder();
    test_usb();
    selftest_summary();
    led_set(n_fail ? LED_R : LED_G);
}

/* ---------------------------------------------------------------- clock */

void test_clock(void)
{
    static uint32_t csr;
    static bool have_csr;

    if (!have_csr) {
        csr = RCC->CSR;
        __HAL_RCC_CLEAR_RESET_FLAGS();
        have_csr = true;
    }
    if (g_hse_ok) {
        report(R_PASS, "HSE crystal", "8 MHz crystal running, SYSCLK %lu MHz", (unsigned long)(SystemCoreClock / 1000000U));
    } else {
        report(R_FAIL, "HSE crystal", "8 MHz crystal Y1 did not start; running on internal RC (USB may not enumerate)");
    }
    report(R_INFO, "Reset cause", "%s%s%s%s%s%s", (csr & RCC_CSR_PORRSTF) ? "power-on " : "",
           (csr & RCC_CSR_BORRSTF) && !(csr & RCC_CSR_PORRSTF) ? "BROWN-OUT " : "",
           (csr & RCC_CSR_SFTRSTF) ? "software " : "", (csr & RCC_CSR_IWDGRSTF) ? "watchdog " : "",
           (csr & RCC_CSR_LPWRRSTF) ? "low-power " : "", (csr & RCC_CSR_PINRSTF) ? "(NRST pin)" : "");
}

/* ---------------------------------------------------------------- supply rails */

void test_power(void)
{
    float vdda = adc_vdda();
    float vsys = adc_vsys(vdda);
    float temp = adc_mcu_temp(vdda);
    bool vbus = HAL_GPIO_ReadPin(VBUS_DET_PORT, VBUS_DET_PIN) == GPIO_PIN_SET;
    bool chg_low = HAL_GPIO_ReadPin(CHG_DET_PORT, CHG_DET_PIN) == GPIO_PIN_RESET;

    /* window covers the regulator tolerance plus the ~1 % of the internal reference */
    report(vdda > 3.15f && vdda < 3.45f ? R_PASS : R_FAIL, "3V3 rail", "VDDA = %.3f V (TPS63001), MCU %.0f C",
           (double)vdda, (double)temp);
    if (vsys < 0.5f) {
        report(R_WARN, "VSYS / battery", "%.2f V: power switch off or no battery (running from USB only)",
               (double)vsys);
    } else if (vsys < 3.0f || vsys > 4.35f) {
        report(R_WARN, "VSYS / battery", "%.2f V is outside the 1S range 3.0-4.2 V (divider R10/R11?)", (double)vsys);
    } else {
        report(R_PASS, "VSYS / battery", "%.2f V", (double)vsys);
    }
    if (vbus) {
        report(R_INFO, "USB power", "VBUS present, charger: %s", chg_low ? "charging" : "not charging / full");
    } else {
        report(R_INFO, "USB power", "VBUS not present (CHG_DET not meaningful)");
    }
}

/* ---------------------------------------------------------------- USB */

void test_usb(void)
{
    bool vbus = HAL_GPIO_ReadPin(VBUS_DET_PORT, VBUS_DET_PIN) == GPIO_PIN_SET;

    /* Enumeration takes a second or so after power-up: give it until t = 4 s. */
    while (vbus && !usb_configured() && HAL_GetTick() < 4000u) {
        HAL_Delay(10);
    }
    if (usb_configured()) {
        report(R_PASS, "USB CDC", "enumerated by the host");
        report(vbus ? R_PASS : R_FAIL, "VBUS_DET", "PA9 = %d while USB is connected (divider R3/R4)", vbus);
    } else if (vbus) {
        report(R_WARN, "USB CDC", "VBUS present but not enumerated (charger, power-only cable, or D+/D- fault)");
    } else {
        report(R_INFO, "USB CDC", "no VBUS on PA9, USB not connected");
    }
}

/* ---------------------------------------------------------------- encoder */

void test_encoder(void)
{
    int a = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_6) == GPIO_PIN_SET;
    int b = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_7) == GPIO_PIN_SET;
    report(R_INFO, "Encoder", "A=%d B=%d count=%d (turn the shaft and use 'enc')", a, b, enc_count());
}
