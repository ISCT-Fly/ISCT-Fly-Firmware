/* USB CDC (virtual COM port) glue: buffered transmit, receive into the console queue. */
#include "app.h"
#include "usbd_core.h"
#include "usbd_desc.h"
#include <string.h>

USBD_HandleTypeDef g_usb_dev;

#define TXQ_SIZE 4096u /* power of two */
#define TXQ_MASK (TXQ_SIZE - 1u)
#define TX_CHUNK 250u  /* not a multiple of 64: avoids a zero-length packet per chunk */

static uint8_t txq[TXQ_SIZE];
static volatile uint32_t tx_head, tx_tail;
static uint8_t tx_pkt[TX_CHUNK];
static uint8_t rx_pkt[CDC_DATA_FS_MAX_PACKET_SIZE];
static volatile uint8_t tx_busy, tx_stalled, dtr, rx_seen, open_evt, close_evt;
static USBD_CDC_LineCodingTypeDef line_coding = {115200, 0, 0, 8};

static bool host_listening(void)
{
    return g_usb_dev.dev_state == USBD_STATE_CONFIGURED && (dtr || rx_seen);
}

/* Start the next IN transfer.  Runs either inside the OTG interrupt or with it masked. */
static void pump(void)
{
    if (tx_busy) {
        return;
    }
    if (!host_listening()) {
        tx_tail = tx_head; /* nobody is reading: drop instead of blocking the tests */
        return;
    }
    uint32_t n = (tx_head - tx_tail) & TXQ_MASK;
    if (n == 0) {
        return;
    }
    if (n > TX_CHUNK) {
        n = TX_CHUNK;
    }
    for (uint32_t i = 0; i < n; i++) {
        tx_pkt[i] = txq[(tx_tail + i) & TXQ_MASK];
    }
    USBD_CDC_SetTxBuffer(&g_usb_dev, tx_pkt, n);
    if (USBD_CDC_TransmitPacket(&g_usb_dev) == USBD_OK) {
        tx_tail = (tx_tail + n) & TXQ_MASK;
        tx_busy = 1;
    }
}

void cdc_poll(void)
{
    HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
    pump();
    HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
}

void cdc_write(const uint8_t *data, size_t len)
{
    if (g_usb_dev.pClassData == NULL) {
        return; /* not enumerated (yet) */
    }
    for (size_t i = 0; i < len; i++) {
        uint32_t next = (tx_head + 1u) & TXQ_MASK;
        if (next == tx_tail) {
            /* Queue full: give the host a moment, then give up until it reads again. */
            uint32_t t0 = HAL_GetTick();
            while (next == tx_tail && !tx_stalled) {
                cdc_poll();
                if (HAL_GetTick() - t0 > 30u) {
                    tx_stalled = 1;
                }
            }
            if (next == tx_tail) {
                break;
            }
        }
        txq[tx_head] = data[i];
        tx_head = next;
    }
    cdc_poll();
}

bool cdc_take_open_event(void)
{
    if (open_evt) {
        open_evt = 0;
        return true;
    }
    return false;
}

bool cdc_take_close_event(void)
{
    if (close_evt) {
        close_evt = 0;
        return true;
    }
    return false;
}

bool usb_configured(void)
{
    return g_usb_dev.dev_state == USBD_STATE_CONFIGURED;
}

void usb_init(void)
{
    USBD_Init(&g_usb_dev, &g_usb_desc, 0);
    USBD_RegisterClass(&g_usb_dev, &USBD_CDC);
    USBD_CDC_RegisterInterface(&g_usb_dev, &g_cdc_fops);
    USBD_Start(&g_usb_dev);
}

/* ---------------------------------------------------------------- class callbacks (interrupt context) */

static int8_t cdc_itf_init(void)
{
    tx_busy = 0;
    tx_stalled = 0;
    dtr = 0;
    rx_seen = 0;
    USBD_CDC_SetTxBuffer(&g_usb_dev, tx_pkt, 0);
    USBD_CDC_SetRxBuffer(&g_usb_dev, rx_pkt);
    return (int8_t)USBD_OK;
}

static int8_t cdc_itf_deinit(void)
{
    if (dtr || rx_seen) {
        close_evt = 1; /* cable pulled or host went away */
    }
    tx_busy = 0;
    dtr = 0;
    rx_seen = 0;
    return (int8_t)USBD_OK;
}

static int8_t cdc_itf_control(uint8_t cmd, uint8_t *pbuf, uint16_t length)
{
    (void)length;
    switch (cmd) {
    case CDC_SET_LINE_CODING:
        line_coding.bitrate = (uint32_t)pbuf[0] | ((uint32_t)pbuf[1] << 8) | ((uint32_t)pbuf[2] << 16) |
                              ((uint32_t)pbuf[3] << 24);
        line_coding.format = pbuf[4];
        line_coding.paritytype = pbuf[5];
        line_coding.datatype = pbuf[6];
        break;
    case CDC_GET_LINE_CODING:
        pbuf[0] = (uint8_t)line_coding.bitrate;
        pbuf[1] = (uint8_t)(line_coding.bitrate >> 8);
        pbuf[2] = (uint8_t)(line_coding.bitrate >> 16);
        pbuf[3] = (uint8_t)(line_coding.bitrate >> 24);
        pbuf[4] = line_coding.format;
        pbuf[5] = line_coding.paritytype;
        pbuf[6] = line_coding.datatype;
        break;
    case CDC_SET_CONTROL_LINE_STATE: {
        /* For requests without a data stage the library passes the setup packet itself. */
        const USBD_SetupReqTypedef *req = (const USBD_SetupReqTypedef *)(void *)pbuf;
        uint8_t now = (uint8_t)(req->wValue & 0x0001u);
        if (now && !dtr) {
            open_evt = 1;
        }
        if (!now) {
            if (dtr || rx_seen) {
                close_evt = 1;
            }
            rx_seen = 0;
        }
        dtr = now;
        break;
    }
    default:
        break;
    }
    return (int8_t)USBD_OK;
}

static int8_t cdc_itf_receive(uint8_t *buf, uint32_t *len)
{
    for (uint32_t i = 0; i < *len; i++) {
        con_rx_push(buf[i]);
    }
    if (!dtr && !rx_seen) {
        open_evt = 1; /* terminal that does not raise DTR: its first keystroke counts as "open" */
    }
    rx_seen = 1;
    USBD_CDC_SetRxBuffer(&g_usb_dev, rx_pkt);
    USBD_CDC_ReceivePacket(&g_usb_dev);
    return (int8_t)USBD_OK;
}

static int8_t cdc_itf_txcplt(uint8_t *buf, uint32_t *len, uint8_t epnum)
{
    (void)buf;
    (void)len;
    (void)epnum;
    tx_busy = 0;
    tx_stalled = 0;
    pump();
    return (int8_t)USBD_OK;
}

USBD_CDC_ItfTypeDef g_cdc_fops = {
    cdc_itf_init, cdc_itf_deinit, cdc_itf_control, cdc_itf_receive, cdc_itf_txcplt,
};
