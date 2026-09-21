/*
 * Copyright (c) 2026 Apple Inc. All rights reserved.
 * Sony Xperia XZs (MSM8996) DWC3 USB2 Device Mode Controller Driver
 * Phase D7-T1: Minimal USB-C Bulk Console
 */

#ifndef _PEXPERT_ARM_XZS_USB_H_
#define _PEXPERT_ARM_XZS_USB_H_

#include <stdint.h>
#include <stdbool.h>
#include <pexpert/pexpert.h>

#define XZS_USB_DWC3_PHYS_BASE        0x06A00000ULL
#define XZS_USB_DWC3_MMIO_SIZE        0x00010000ULL   /* 64 KB */

#define XZS_USB_QCOM_GLUE_PHYS_BASE   0x06AF8800ULL
#define XZS_USB_QCOM_GLUE_MMIO_SIZE   0x00001000ULL

#define XZS_USB_QUSB2_PHY_PHYS_BASE   0x07411000ULL
#define XZS_USB_QUSB2_PHY_MMIO_SIZE   0x00001000ULL

/* Unambiguous IRQ definition */
#define USB_DT_SPI                    131
#define USB_ARCH_GIC_INTID            163   /* 32 + 131 */
#define USB_IRQ_TRIGGER               LEVEL_HIGH

/* Hardware IDs: Using pid.codes test VID/PID for private non-commercial development */
#define XZS_USB_VID                   0x1209
#define XZS_USB_PID                   0x000A

/* DWC3 Register Offsets from 0x06A00000 */
#define DWC3_GCTL                     0xC110
#define DWC3_GSTS                     0xC118
#define DWC3_GSNPSID                  0xC120
#define DWC3_GUSB2PHYCFG0             0xC200
#define DWC3_GUSB3PIPECTL0            0xC2C0
#define DWC3_GEVNTADR0                0xC400
#define DWC3_GEVNTADR_HI0             0xC404
#define DWC3_GEVNTSIZ0                0xC408
#define DWC3_GEVNTCNT0                0xC40C
#define DWC3_DCFG                     0xC700
#define DWC3_DCTL                     0xC704
#define DWC3_DEVTEN                   0xC708
#define DWC3_DSTS                     0xC70C
#define DWC3_DALEPENA                 0xC714

#define DWC3_DEPCMDPAR2(n)            (0xC800 + ((n) * 0x10))
#define DWC3_DEPCMDPAR1(n)            (0xC804 + ((n) * 0x10))
#define DWC3_DEPCMDPAR0(n)            (0xC808 + ((n) * 0x10))
#define DWC3_DEPCMD(n)                (0xC80C + ((n) * 0x10))

/* DWC3 Command Types */
#define DEPCMD_SETEPCONFIG            0x01
#define DEPCMD_SETTRANSXFR            0x02
#define DEPCMD_GETEPSTATE             0x03
#define DEPCMD_SETSTALL               0x04
#define DEPCMD_CLEARSTALL             0x05
#define DEPCMD_STARTTRANSFER          0x06
#define DEPCMD_UPDATETRANSFER         0x07
#define DEPCMD_ENDTRANSFER            0x08
#define DEPCMD_STARTNEWCFG            0x09

#define DEPCMD_CMDACT                 (1u << 10)
#define DEPCMD_CMDIOC                 (1u << 8)

/* Endpoint Numbers */
#define DWC3_PHYS_EP_CTRL_OUT         0
#define DWC3_PHYS_EP_CTRL_IN          1
#define DWC3_PHYS_EP_BULK_OUT         2   /* Logical 0x01 */
#define DWC3_PHYS_EP_BULK_IN          3   /* Logical 0x81 */

/* TRB Definition (16 bytes, aligned) */
struct dwc3_trb {
	uint32_t bpl;
	uint32_t bph;
	uint32_t size;
	uint32_t ctrl;
} __attribute__((aligned(64)));

#define DWC3_TRB_CTRL_HWO             (1u << 0)
#define DWC3_TRB_CTRL_LST             (1u << 1)
#define DWC3_TRB_CTRL_CHN             (1u << 2)
#define DWC3_TRB_CTRL_CSP             (1u << 3)
#define DWC3_TRB_CTRL_TRBCTL_NORMAL   (1u << 4)
#define DWC3_TRB_CTRL_TRBCTL_CTRL_SETUP (2u << 4)
#define DWC3_TRB_CTRL_TRBCTL_CTRL_STATUS2 (3u << 4)
#define DWC3_TRB_CTRL_TRBCTL_CTRL_STATUS3 (4u << 4)
#define DWC3_TRB_CTRL_TRBCTL_CTRL_DATA  (5u << 4)
#define DWC3_TRB_CTRL_ISP_IMI         (1u << 10)
#define DWC3_TRB_CTRL_IOC             (1u << 11)

/* USB Standard Setup Packet */
struct usb_setup_packet {
	uint8_t  bmRequestType;
	uint8_t  bRequest;
	uint16_t wValue;
	uint16_t wIndex;
	uint16_t wLength;
} __attribute__((packed));

/* USB Standard Request Codes */
#define USB_REQ_GET_STATUS            0x00
#define USB_REQ_CLEAR_FEATURE         0x01
#define USB_REQ_SET_FEATURE           0x03
#define USB_REQ_SET_ADDRESS           0x05
#define USB_REQ_GET_DESCRIPTOR        0x06
#define USB_REQ_SET_DESCRIPTOR        0x07
#define USB_REQ_GET_CONFIGURATION     0x08
#define USB_REQ_SET_CONFIGURATION     0x09
#define USB_REQ_GET_INTERFACE         0x0A
#define USB_REQ_SET_INTERFACE         0x0B

/* Descriptor Types */
#define USB_DT_DEVICE                 0x01
#define USB_DT_CONFIG                 0x02
#define USB_DT_STRING                 0x03
#define USB_DT_INTERFACE              0x04
#define USB_DT_ENDPOINT               0x05
#define USB_DT_DEVICE_QUALIFIER       0x06

/* Public Driver Interface */
int  xzs_usb_init(void);
void xzs_usb_poll_events(void);
void xzs_usb_irq_handler(void);
void xzs_usb_console_putc(char c);
int  xzs_usb_send_bulk_in(const uint8_t *data, uint32_t len);
boolean_t xzs_usb_is_enumerated(void);
boolean_t xzs_usb_is_console_ready(void);
uint32_t  dwc3_read32_pub(uint32_t offset);

/* Telemetry Exports */
extern volatile uint32_t g_xzs_usb_gsnpsid;
extern volatile uint32_t g_xzs_usb_dsts;
extern volatile uint32_t g_xzs_usb_reset_count;
extern volatile uint32_t g_xzs_usb_conn_done_count;
extern volatile uint32_t g_xzs_usb_set_addr_count;
extern volatile uint32_t g_xzs_usb_set_cfg_count;
extern volatile uint32_t g_xzs_usb_bulk_out_count;
extern volatile uint32_t g_xzs_usb_bulk_out_bytes;
extern volatile uint32_t g_xzs_usb_bulk_in_count;
extern volatile uint32_t g_xzs_usb_bulk_in_bytes;
extern volatile uint32_t g_xzs_usb_irq_count;
extern volatile uint32_t g_xzs_usb_enumerated;
extern volatile uint32_t g_xzs_usb_configured;
extern volatile uint32_t g_xzs_usb_console_ready;

#endif /* _PEXPERT_ARM_XZS_USB_H_ */
