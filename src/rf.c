/* nRF24L01+ test on SPI1: register access, a real transmission (CE, IRQ, 16 MHz crystal, PLL)
 * and a receive-power scan of the 2.4 GHz band (antenna path). */
#include "app.h"
#include <string.h>

#define NRF_CONFIG     0x00
#define NRF_EN_AA      0x01
#define NRF_EN_RXADDR  0x02
#define NRF_SETUP_AW   0x03
#define NRF_SETUP_RETR 0x04
#define NRF_RF_CH      0x05
#define NRF_RF_SETUP   0x06
#define NRF_STATUS     0x07
#define NRF_RPD        0x09
#define NRF_RX_ADDR_P0 0x0A
#define NRF_TX_ADDR    0x10

#define NRF_W_REGISTER 0x20
#define NRF_W_TX_PLD   0xA0
#define NRF_FLUSH_TX   0xE1
#define NRF_FLUSH_RX   0xE2
#define NRF_NOP        0xFF

#define ST_TX_DS  0x20
#define ST_MAX_RT 0x10

static uint8_t rf_xfer(const uint8_t *tx, uint8_t *rx, uint16_t n)
{
    HAL_GPIO_WritePin(RF_CSN_PORT, RF_CSN_PIN, GPIO_PIN_RESET);
    HAL_SPI_TransmitReceive(&hspi_rf, (uint8_t *)tx, rx, n, 20);
    HAL_GPIO_WritePin(RF_CSN_PORT, RF_CSN_PIN, GPIO_PIN_SET);
    delay_us(2);
    return rx[0]; /* STATUS is clocked out with the command byte */
}

static uint8_t rf_cmd(uint8_t cmd)
{
    uint8_t rx;
    return rf_xfer(&cmd, &rx, 1);
}

static uint8_t rf_rreg(uint8_t reg)
{
    uint8_t tx[2] = {reg, NRF_NOP}, rx[2];
    rf_xfer(tx, rx, 2);
    return rx[1];
}

static void rf_wreg(uint8_t reg, uint8_t v)
{
    uint8_t tx[2] = {(uint8_t)(NRF_W_REGISTER | reg), v}, rx[2];
    rf_xfer(tx, rx, 2);
}

static void rf_wbuf(uint8_t cmd, const uint8_t *d, uint8_t n)
{
    uint8_t tx[8] = {cmd}, rx[8];
    memcpy(tx + 1, d, n);
    rf_xfer(tx, rx, (uint16_t)(n + 1));
}

static void rf_rbuf(uint8_t reg, uint8_t *d, uint8_t n)
{
    uint8_t tx[8], rx[8];
    memset(tx, NRF_NOP, sizeof(tx));
    tx[0] = reg;
    rf_xfer(tx, rx, (uint16_t)(n + 1));
    memcpy(d, rx + 1, n);
}

static void rf_ce(bool on)
{
    HAL_GPIO_WritePin(RF_CE_PORT, RF_CE_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static bool rf_irq_low(void)
{
    return HAL_GPIO_ReadPin(RF_IRQ_PORT, RF_IRQ_PIN) == GPIO_PIN_RESET;
}

/* Count how often each channel shows a carrier above -64 dBm (RPD bit). */
void rf_scan(int reps, bool print)
{
    static uint8_t hits[126];
    unsigned total = 0, best = 0, best_ch = 0;

    memset(hits, 0, sizeof(hits));
    rf_ce(false);
    rf_wreg(NRF_EN_AA, 0x00);
    rf_wreg(NRF_RF_SETUP, 0x06);
    rf_wreg(NRF_CONFIG, 0x0F); /* power up, receiver */
    HAL_Delay(5);
    for (int r = 0; r < reps; r++) {
        for (uint8_t ch = 0; ch < 126; ch++) {
            rf_wreg(NRF_RF_CH, ch);
            rf_ce(true);
            delay_us(400); /* 130 us settling + 40 us for RPD, with margin */
            rf_ce(false);
            if ((rf_rreg(NRF_RPD) & 1u) && hits[ch] < 255) {
                hits[ch]++;
            }
        }
    }
    rf_wreg(NRF_CONFIG, 0x08); /* power down */

    for (unsigned ch = 0; ch < 126; ch++) {
        total += hits[ch];
        if (hits[ch] > best) {
            best = hits[ch];
            best_ch = ch;
        }
    }
    if (print && !con_echo()) {
        /* machine mode (web test bench): one JSON line instead of the text histogram */
        P("{\"k\":\"rfscan\",\"reps\":%d,\"hits\":[", reps);
        for (int ch = 0; ch < 126; ch++) {
            P(ch ? ",%u" : "%u", hits[ch]);
        }
        P("]}\n");
    } else if (print) {
        char row[127];
        for (int ch = 0; ch < 126; ch++) {
            unsigned h = hits[ch];
            row[ch] = h == 0 ? '.' : (h * 4u <= (unsigned)reps ? ':' : (h * 2u <= (unsigned)reps ? '+' : '#'));
        }
        row[126] = '\0';
        P("  2400 MHz + channel, %d sweeps ('.' none  ':' <25%%  '+' <50%%  '#' more)\n", reps);
        P("  0         10        20        30        40        50        60\n");
        P("  %.63s\n", row);
        P("  63        73        83        93        103       113       123\n");
        P("  %s\n", row + 63);
    }
    if (total > 0) {
        report(R_PASS, "nRF24 RX scan", "carrier seen on %u channel-samples, strongest at %u MHz", total,
               2400u + best_ch);
    } else {
        report(R_WARN, "nRF24 RX scan",
               "nothing above -64 dBm on any channel (quiet room, or antenna/matching problem)");
    }
}

void test_rf(void)
{
    static const uint8_t addr[5] = {0xA5, 0x5A, 0xC3, 0x3C, 0x0F};
    static const uint8_t payload[4] = {'T', 'E', 'S', 'T'};
    uint8_t back[5] = {0};

    rf_ce(false);
    uint8_t status = rf_cmd(NRF_NOP);

    /* 1. SPI link: two complementary patterns through a plain read/write register */
    rf_wreg(NRF_RF_CH, 0x55);
    uint8_t a = rf_rreg(NRF_RF_CH);
    rf_wreg(NRF_RF_CH, 0x2A);
    uint8_t b = rf_rreg(NRF_RF_CH);
    rf_wbuf(NRF_W_REGISTER | NRF_TX_ADDR, addr, 5);
    rf_rbuf(NRF_TX_ADDR, back, 5);
    if (a != 0x55 || b != 0x2A || memcmp(addr, back, 5) != 0) {
        report(R_FAIL, "nRF24 SPI", "wrote 0x55/0x2A, read 0x%02X/0x%02X, status 0x%02X (0x00 = chip silent)", a, b,
               status);
        return;
    }
    rf_wreg(NRF_RF_SETUP, 0x26); /* the 250 kbps bit only exists on the "+" version */
    bool plus = rf_rreg(NRF_RF_SETUP) == 0x26;
    rf_wreg(NRF_RF_SETUP, 0x06); /* 1 Mbps, 0 dBm */
    report(R_PASS, "nRF24 SPI", "status=0x%02X, register read/write ok, %s", status,
           plus ? "nRF24L01+" : "250 kbps bit not writable (not a genuine '+')");

    /* 2. Transmit one packet to nobody.  With auto-ack on, the radio retries and then raises
     *    MAX_RT: that only happens if CE works and the 16 MHz crystal + PLL are running. */
    rf_wreg(NRF_CONFIG, 0x0E); /* power up, transmitter, CRC16, all interrupts on IRQ */
    HAL_Delay(5);
    rf_wreg(NRF_EN_AA, 0x01);
    rf_wreg(NRF_EN_RXADDR, 0x01);
    rf_wreg(NRF_SETUP_AW, 0x03);
    rf_wreg(NRF_SETUP_RETR, 0x11); /* 500 us, 1 retry */
    rf_wreg(NRF_RF_CH, 76);
    rf_wbuf(NRF_W_REGISTER | NRF_RX_ADDR_P0, addr, 5);
    rf_cmd(NRF_FLUSH_TX);
    rf_cmd(NRF_FLUSH_RX);
    rf_wreg(NRF_STATUS, 0x70);
    delay_us(100);
    bool idle_high = !rf_irq_low();
    uint32_t c0 = g_irq_count[IRQ_RF];

    rf_wbuf(NRF_W_TX_PLD, payload, sizeof(payload));
    rf_ce(true);
    delay_us(50);
    rf_ce(false);
    uint32_t t0 = HAL_GetTick();
    while (!rf_irq_low() && HAL_GetTick() - t0 < 50u) {
    }
    HAL_Delay(2);
    uint8_t st = rf_cmd(NRF_NOP);
    bool went_low = rf_irq_low();
    uint32_t edges = g_irq_count[IRQ_RF] - c0;

    rf_wreg(NRF_STATUS, 0x70);
    rf_cmd(NRF_FLUSH_TX);
    delay_us(100);
    bool released = !rf_irq_low();
    rf_wreg(NRF_CONFIG, 0x08); /* power down */

    bool sent = (st & (ST_TX_DS | ST_MAX_RT)) != 0;
    if (!sent) {
        report(R_FAIL, "nRF24 TX", "no TX_DS/MAX_RT after CE pulse (status 0x%02X): check CE (PA3) and crystal Y2",
               st);
    } else {
        report(R_PASS, "nRF24 TX", "packet sent, status 0x%02X (%s)", st,
               (st & ST_TX_DS) ? "acknowledged by another radio" : "MAX_RT, nobody listening - expected");
    }
    if (sent || idle_high) {
        bool ok = idle_high && went_low && edges >= 1 && released;
        report(ok ? R_PASS : R_FAIL, "RF_IRQ", "PC4 idle=%d, after TX=%d, after clear=%d, edges=%lu", idle_high,
               !went_low, released, (unsigned long)edges);
    }

    /* 3. Receiver / antenna */
    rf_scan(15, false);
}
