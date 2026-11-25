#ifndef __USBD_DESC_H
#define __USBD_DESC_H

#include "usbd_def.h"
#include "usbd_cdc.h"

extern USBD_DescriptorsTypeDef g_usb_desc;
extern USBD_CDC_ItfTypeDef g_cdc_fops;
extern USBD_HandleTypeDef g_usb_dev;
extern PCD_HandleTypeDef g_hpcd;

#endif /* __USBD_DESC_H */
