/**
 * Copyright (c) 2023 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#define CFG_TUD_ENABLED         (1) // Enable Device Stack
#define CFG_TUSB_OS         OPT_OS_FREERTOS

// Legacy RHPORT configuration
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_HIGH_SPEED)   // Usb Device Mode + Full Speed (12Mbps)
#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT        (0)
#endif
// end legacy RHPORT

//--------------------------------------------------------------------
// DEVICE CONFIGURATION
//--------------------------------------------------------------------

#define CFG_TUD_ENDPOINT0_SIZE    64

//------------- CLASS -------------//

// Vendor specific class configuration
#define CFG_TUD_VENDOR           1
#define CFG_TUD_VENDOR_EP_BUFSIZE  64          // 64 is the max packet size for full speed
#define CFG_TUD_VENDOR_RX_BUFSIZE  (4096 + 64)  // 4kb + 64 bytes for header/command headroom
#define CFG_TUD_VENDOR_TX_BUFSIZE  (4096 + 64)

// DFU RT does not required for this project
#define CFG_TUD_DFU_RT           0

// Used by pico_stdio_usb
#define CFG_TUD_CDC_EP_BUFSIZE  64          // 64 is the max packet size for full speed
#define CFG_TUD_CDC_RX_BUFSIZE (2048 + 64)  // 2kb + 64 bytes for header/command headroom
#define CFG_TUD_CDC_TX_BUFSIZE (2048 + 64)  // 2kb + 64 bytes for header/command headroom
#define CFG_TUD_CDC              1

// MSC class is not needed
#define CFG_TUD_MSC              0

// HID class is not needed
#define CFG_TUD_HID              0

// MIDI class is not needed
#define CFG_TUD_MIDI             0

// Audio class is not needed
#define CFG_TUD_AUDIO            0

// Video class is not needed
#define CFG_TUD_VIDEO            0

// BTH class is not needed
#define CFG_TUD_BTH              0

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */