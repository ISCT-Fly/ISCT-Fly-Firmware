/* Shared declarations of the test firmware. */
#ifndef APP_H
#define APP_H

#include "board.h"
#include <stddef.h>

#define FW_NAME    "FlapFC board test"
#define FW_VERSION "1.1"

/* ---- console: USB CDC + USART3, same text on both ---- */
void con_init(void);
void con_rx_push(uint8_t b);        /* called from the UART / USB receive interrupts */
int  con_getc(void);                /* -1 when nothing is waiting */
bool con_key_pressed(void);         /* true (and input flushed) when anything was typed */
void con_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
const char *con_readline(void);     /* non-blocking; returns a line once Enter was received */
void con_capture(bool on);          /* keep a copy of the output for later replay */
void con_set_echo(bool on);         /* off = machine mode: no character echo, no prompt */
bool con_echo(void);
void con_prompt(void);
void con_replay(void);
#define P con_printf

/* ---- USB CDC ---- */
void usb_init(void);
void cdc_write(const uint8_t *data, size_t len);
void cdc_poll(void);
bool cdc_take_open_event(void);     /* true once each time a terminal opens the port */
bool cdc_take_close_event(void);    /* true once each time it closes it again */
bool usb_configured(void);

/* ---- test results ---- */
typedef enum { R_PASS, R_WARN, R_FAIL, R_INFO } res_t;
void report(res_t r, const char *item, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
void selftest_run(void);
void selftest_summary(void);
int  selftest_fail_count(void);

/* ---- individual tests (each prints its own report lines) ---- */
void test_clock(void);
void test_power(void);
void test_i2c_bus(void);
void test_baro(void);
void test_tof(void);
void test_imu(void);
void test_rf(void);
void test_sd(void);
void test_usb(void);
void test_encoder(void);

/* ---- live readings for the interactive commands ---- */
bool imu_read(float acc_g[3], float gyro_dps[3], float *temp_c);
bool baro_read(float *press_pa, float *temp_c);
void rf_scan(int reps, bool print);
void sd_writeback_test(void);

/* ---- telemetry stream and live actuator control (used by the web test bench) ---- */
void telem_set_rate(int hz);        /* 0 = off, otherwise 1..100 JSON lines per second on USB */
int  telem_rate(void);
void telem_poll(void);              /* call from the main loop */
void act_arm(bool on);              /* the motor only accepts live set-points while armed */
bool act_armed(void);
bool act_motor_live(int permille);  /* false when not armed; needs refreshing, see ACT_TIMEOUT_MS */
void act_poll(void);                /* stops the motor when the set-points stop coming */
void act_safe_stop(void);           /* motor off + disarm */
bool tof_online(void);
#define ACT_TIMEOUT_MS 600u

/* ---- command line ---- */
void cli_execute(const char *line);
void request_dfu(void);

#endif /* APP_H */
