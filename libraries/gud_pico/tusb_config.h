// SPDX-License-Identifier: MIT

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#define CFG_TUSB_RHPORT0_MODE     (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

#define CFG_TUD_HID               0

#define CFG_GUD_VID     0x16d0 // MCS Electronics
#define CFG_GUD_PID     0x10a9 // GUD USB Display

#define CFG_GUD_BULK_OUT_SIZE	64

//#define CFG_TUSB_DEBUG              2

#endif /* _TUSB_CONFIG_H_ */
