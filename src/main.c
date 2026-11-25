/* FlapFC V2 controller board - bring-up / hardware test firmware.
 *
 * Boot: LED walk -> automatic self-test -> command prompt.
 * Console: USB virtual COM port and USART3 (PC10 TX / PC11 RX, 115200 8N1).
 * LEDs after the self-test: green = no failure, red = at least one failure, blue blips = alive. */
#include "app.h"
#include "usbd_core.h"
#include "usbd_desc.h"

#define DFU_MAGIC      0xB007DF00u
#define SYSTEM_MEMORY  0x1FFF0000u

/* Lives in .noinit so it survives the software reset that precedes the jump. */
static volatile uint32_t boot_request __attribute__((section(".noinit")));

/* Called first thing after reset, with the chip still in its reset state: hand over to the
 * ROM bootloader exactly as if BOOT0 had been held. */
static void enter_bootloader(void)
{
    const uint32_t *vec = (const uint32_t *)SYSTEM_MEMORY;
    uint32_t sp = vec[0];
    void (*entry)(void) = (void (*)(void))vec[1];

    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    SYSCFG->MEMRMP = 0x01; /* system flash at address 0 */
    SCB->VTOR = 0;
    __set_MSP(sp);
    entry();
    for (;;) {
    }
}

void request_dfu(void)
{
    HAL_Delay(100); /* let the message out */
    USBD_Stop(&g_usb_dev);
    HAL_Delay(300); /* host notices the disconnect */
    boot_request = DFU_MAGIC;
    NVIC_SystemReset();
}

static void banner(void)
{
    P("\n%s v%s  (STM32F446RET6, built " __DATE__ " " __TIME__ ")\n", FW_NAME, FW_VERSION);
    P("Console: USB CDC + USART3 115200.  Type 'help' for commands.\n");
}

int main(void)
{
    if (boot_request == DFU_MAGIC) {
        boot_request = 0;
        enter_bootloader();
    }
    boot_request = 0;

    board_init();
    con_init();

    /* LED walk: also the visual check of the three LEDs */
    static const uint8_t walk[] = {LED_G, LED_B, LED_R};
    for (int i = 0; i < 3; i++) {
        led_set(walk[i]);
        HAL_Delay(200);
    }
    led_set(0);

    usb_init();
    banner();

    con_capture(true);
    selftest_run();
    con_capture(false);
    con_prompt();

    uint32_t beat = HAL_GetTick();
    for (;;) {
        cdc_poll();
        telem_poll();
        act_poll();

        /* A terminal opened the USB port after the test already ran: show it the report. */
        if (cdc_take_open_event()) {
            HAL_Delay(150);
            banner();
            con_replay();
            con_prompt();
        }

        /* The host went away (port closed, cable pulled): back to the plain interactive state
         * and, above all, no motor left running on its last set-point. */
        if (cdc_take_close_event() || (telem_rate() && !usb_configured())) {
            telem_set_rate(0);
            con_set_echo(true);
            act_safe_stop();
        }

        const char *line = con_readline();
        if (line) {
            cli_execute(line);
            con_prompt();
            beat = HAL_GetTick();
        }

        /* heartbeat: 60 ms blue blip every second, on top of the green/red result */
        uint32_t dt = HAL_GetTick() - beat;
        if (dt >= 1000u) {
            beat += 1000u;
            dt = 0;
        }
        led_write(LED_B, dt < 60u);
    }
}
