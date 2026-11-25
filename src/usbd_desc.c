/* USB descriptors: a plain CDC ACM virtual COM port (Windows 10/11 loads usbser.sys by class). */
#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_conf.h"

#define USBD_VID     0x0483U /* STMicroelectronics */
#define USBD_PID     0x5740U /* ST virtual COM port */
#define USBD_LANGID  0x0409U

static uint8_t dev_desc[USB_LEN_DEV_DESC] __attribute__((aligned(4))) = {
    0x12, USB_DESC_TYPE_DEVICE,
    0x00, 0x02,                         /* bcdUSB 2.00 */
    0x02, 0x02, 0x00,                   /* CDC */
    USB_MAX_EP0_SIZE,
    LOBYTE(USBD_VID), HIBYTE(USBD_VID),
    LOBYTE(USBD_PID), HIBYTE(USBD_PID),
    0x00, 0x02,                         /* bcdDevice 2.00 */
    USBD_IDX_MFC_STR, USBD_IDX_PRODUCT_STR, USBD_IDX_SERIAL_STR,
    USBD_MAX_NUM_CONFIGURATION,
};

static uint8_t lang_desc[USB_LEN_LANGID_STR_DESC] __attribute__((aligned(4))) = {
    USB_LEN_LANGID_STR_DESC, USB_DESC_TYPE_STRING, LOBYTE(USBD_LANGID), HIBYTE(USBD_LANGID),
};

static uint8_t str_buf[USBD_MAX_STR_DESC_SIZ] __attribute__((aligned(4)));

static uint8_t *get_device(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    *length = sizeof(dev_desc);
    return dev_desc;
}

static uint8_t *get_langid(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    *length = sizeof(lang_desc);
    return lang_desc;
}

static uint8_t *get_string(const char *s, uint16_t *length)
{
    USBD_GetString((uint8_t *)s, str_buf, length);
    return str_buf;
}

static uint8_t *get_manufacturer(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    return get_string("Kero", length);
}

static uint8_t *get_product(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    return get_string("FlapFC test console", length);
}

static uint8_t *get_serial(USBD_SpeedTypeDef speed, uint16_t *length)
{
    static const char hex[] = "0123456789ABCDEF";
    const uint8_t *uid = (const uint8_t *)UID_BASE; /* 96-bit unique device ID */
    char s[25];
    (void)speed;
    for (int i = 0; i < 12; i++) {
        s[2 * i] = hex[uid[i] >> 4];
        s[2 * i + 1] = hex[uid[i] & 0x0F];
    }
    s[24] = '\0';
    return get_string(s, length);
}

static uint8_t *get_config_str(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    return get_string("CDC Config", length);
}

static uint8_t *get_interface_str(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    return get_string("CDC Interface", length);
}

USBD_DescriptorsTypeDef g_usb_desc = {
    get_device, get_langid, get_manufacturer, get_product, get_serial, get_config_str, get_interface_str,
};
