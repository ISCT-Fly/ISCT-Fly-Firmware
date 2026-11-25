/* microSD test over SDIO in 1-bit mode (PC8 D0, PC12 CK, PD2 CMD; D1..D3 are not routed).
 * Reads block 0 at three clock speeds so a wiring fault can be told apart from a
 * signal-quality problem.  Read-only unless "sdw" is typed. */
#include "app.h"
#include <stdio.h>
#include <string.h>

static SD_HandleTypeDef hsd;
static uint8_t blk[512] __attribute__((aligned(4)));
static uint8_t blk2[512] __attribute__((aligned(4)));

#define SD_DIV_400K 0x76u /* 48 MHz / (118 + 2) */
#define SD_DIV_8M   4u    /* 48 MHz / (4 + 2)   */
#define SD_DIV_24M  0u    /* 48 MHz / (0 + 2)   */

static const char *sd_err(uint32_t e)
{
    static char s[96];
    size_t o = 0;
    s[0] = '\0';
    static const struct { uint32_t bit; const char *name; } tab[] = {
        {HAL_SD_ERROR_CMD_RSP_TIMEOUT, "CMD_TIMEOUT"}, {HAL_SD_ERROR_CMD_CRC_FAIL, "CMD_CRC"},
        {HAL_SD_ERROR_DATA_TIMEOUT, "DATA_TIMEOUT"},   {HAL_SD_ERROR_DATA_CRC_FAIL, "DATA_CRC"},
        {HAL_SD_ERROR_RX_OVERRUN, "RX_OVERRUN"},       {HAL_SD_ERROR_TX_UNDERRUN, "TX_UNDERRUN"},
        {HAL_SD_ERROR_TIMEOUT, "TIMEOUT"},             {HAL_SD_ERROR_INVALID_VOLTRANGE, "VOLT_RANGE"},
    };
    for (size_t i = 0; i < sizeof(tab) / sizeof(tab[0]); i++) {
        if ((e & tab[i].bit) && o < sizeof(s) - 16) {
            o += (size_t)snprintf(s + o, sizeof(s) - o, "%s ", tab[i].name);
        }
    }
    snprintf(s + o, sizeof(s) - o, "(0x%08lX)", (unsigned long)e);
    return s;
}

static void sd_pin(GPIO_TypeDef *port, uint32_t pin, uint32_t mode, uint32_t pull)
{
    GPIO_InitTypeDef g = {0};
    g.Pin = pin;
    g.Mode = mode;
    g.Pull = pull;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF12_SDIO;
    HAL_GPIO_Init(port, &g);
}

static bool sd_open(void)
{
    if (hsd.State != HAL_SD_STATE_RESET) {
        HAL_SD_DeInit(&hsd);
    }
    __HAL_RCC_SDIO_CLK_ENABLE();
    __HAL_RCC_SDIO_FORCE_RESET();
    __HAL_RCC_SDIO_RELEASE_RESET();
    sd_pin(GPIOC, GPIO_PIN_8, GPIO_MODE_AF_PP, GPIO_PULLUP);
    sd_pin(GPIOC, GPIO_PIN_12, GPIO_MODE_AF_PP, GPIO_NOPULL);
    sd_pin(GPIOD, GPIO_PIN_2, GPIO_MODE_AF_PP, GPIO_PULLUP);

    hsd.Instance = SDIO;
    hsd.Init.ClockEdge = SDIO_CLOCK_EDGE_RISING;
    hsd.Init.ClockBypass = SDIO_CLOCK_BYPASS_DISABLE;
    hsd.Init.ClockPowerSave = SDIO_CLOCK_POWER_SAVE_DISABLE;
    hsd.Init.BusWide = SDIO_BUS_WIDE_1B;
    hsd.Init.HardwareFlowControl = SDIO_HARDWARE_FLOW_CONTROL_DISABLE;
    hsd.Init.ClockDiv = SD_DIV_400K;
    return HAL_SD_Init(&hsd) == HAL_OK; /* identification runs at 400 kHz and leaves the clock there */
}

static void sd_set_clock(uint32_t div)
{
    SDIO_InitTypeDef init = hsd.Init;
    init.ClockDiv = div;
    SDIO_Init(hsd.Instance, init);
    HAL_Delay(2);
}

static bool sd_wait_ready(uint32_t timeout_ms)
{
    uint32_t t0 = HAL_GetTick();
    while (HAL_SD_GetCardState(&hsd) != HAL_SD_CARD_TRANSFER) {
        if (HAL_GetTick() - t0 > timeout_ms) {
            return false;
        }
    }
    return true;
}

/* Read block 0 `reps` times at the given clock.  Returns the number of good reads;
 * the SDIO peripheral checks the CRC16 of every block. */
static int sd_read_at(uint32_t div, int reps, uint32_t *err)
{
    int good = 0;

    *err = 0;
    sd_set_clock(div);
    for (int i = 0; i < reps; i++) {
        if (HAL_SD_ReadBlocks(&hsd, blk, 0, 1, 500) == HAL_OK && sd_wait_ready(500)) {
            good++;
        } else {
            *err |= hsd.ErrorCode;
            /* a failed transfer can leave the card mid-state: start over */
            if (!sd_open()) {
                *err |= hsd.ErrorCode;
                break;
            }
            sd_set_clock(div);
        }
    }
    return good;
}

void test_sd(void)
{
    HAL_SD_CardInfoTypeDef ci;
    HAL_SD_CardCIDTypeDef cid;
    uint32_t err;

    /* Idle levels with the lines as plain inputs: CMD and D0 have 47k pull-ups on the board. */
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    sd_pin(GPIOC, GPIO_PIN_8, GPIO_MODE_INPUT, GPIO_NOPULL);
    sd_pin(GPIOD, GPIO_PIN_2, GPIO_MODE_INPUT, GPIO_NOPULL);
    HAL_Delay(2);
    int d0 = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_8) == GPIO_PIN_SET;
    int cmd = HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_2) == GPIO_PIN_SET;
    if (!d0 || !cmd) {
        report(R_WARN, "SD lines", "idle D0=%d CMD=%d, expected 1/1 (47k pull-ups R34/R32)", d0, cmd);
    }

    if (!sd_open()) {
        report(R_FAIL, "SD card", "init failed: %s - card inserted? else check CMD (PD2) / CLK (PC12)",
               sd_err(hsd.ErrorCode));
        return;
    }
    HAL_SD_GetCardInfo(&hsd, &ci);
    HAL_SD_GetCardCID(&hsd, &cid);
    uint32_t mb = (uint32_t)(((uint64_t)ci.LogBlockNbr * ci.LogBlockSize) >> 20);
    report(R_PASS, "SD card", "%s v%lu.x, %lu MB, manufacturer 0x%02X, name %c%c%c%c%c",
           ci.CardType == CARD_SDSC ? "SDSC" : "SDHC/SDXC", (unsigned long)ci.CardVersion + 1UL, (unsigned long)mb,
           (unsigned)cid.ManufacturerID, (char)(cid.ProdName1 >> 24), (char)(cid.ProdName1 >> 16),
           (char)(cid.ProdName1 >> 8), (char)cid.ProdName1, (char)cid.ProdName2);

    int g400 = sd_read_at(SD_DIV_400K, 4, &err);
    report(g400 == 4 ? R_PASS : R_FAIL, "SD read 400k", "%d/4 blocks ok%s%s", g400, g400 == 4 ? "" : ": ",
           g400 == 4 ? "" : sd_err(err));
    bool sig = blk[510] == 0x55 && blk[511] == 0xAA;

    int g8 = sd_read_at(SD_DIV_8M, 8, &err);
    report(g8 == 8 ? R_PASS : R_FAIL, "SD read 8M", "%d/8 blocks ok%s%s", g8, g8 == 8 ? "" : ": ",
           g8 == 8 ? "" : sd_err(err));

    /* 24 MHz with a polled FIFO is at the limit of this test code, so a miss here is only a hint. */
    int g24 = sd_read_at(SD_DIV_24M, 8, &err);
    report(g24 == 8 ? R_PASS : R_WARN, "SD read 24M", "%d/8 blocks ok%s%s", g24, g24 == 8 ? "" : ": ",
           g24 == 8 ? "" : sd_err(err));

    if (g400 == 4) {
        report(R_INFO, "SD block 0", "boot signature %s", sig ? "0x55AA present" : "absent (card not formatted?)");
    }
    sd_set_clock(SD_DIV_8M);
}

/* Write path: read block 1, write the same bytes back, read again and compare.
 * Block 1 is rewritten with its own content, so the card data does not change. */
void sd_writeback_test(void)
{
    if (hsd.State != HAL_SD_STATE_READY && !sd_open()) {
        report(R_FAIL, "SD write", "card not ready: %s", sd_err(hsd.ErrorCode));
        return;
    }
    sd_set_clock(SD_DIV_8M);
    if (HAL_SD_ReadBlocks(&hsd, blk, 1, 1, 500) != HAL_OK || !sd_wait_ready(500)) {
        report(R_FAIL, "SD write", "read before write failed: %s", sd_err(hsd.ErrorCode));
        return;
    }
    if (HAL_SD_WriteBlocks(&hsd, blk, 1, 1, 1000) != HAL_OK || !sd_wait_ready(1000)) {
        report(R_FAIL, "SD write", "write failed: %s", sd_err(hsd.ErrorCode));
        return;
    }
    if (HAL_SD_ReadBlocks(&hsd, blk2, 1, 1, 500) != HAL_OK || !sd_wait_ready(500)) {
        report(R_FAIL, "SD write", "read after write failed: %s", sd_err(hsd.ErrorCode));
        return;
    }
    bool same = memcmp(blk, blk2, sizeof(blk)) == 0;
    report(same ? R_PASS : R_FAIL, "SD write", "block 1 rewritten with its own data, read-back %s",
           same ? "matches" : "DIFFERS");
}
