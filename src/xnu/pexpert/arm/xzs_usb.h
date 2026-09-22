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

/* GCC contains the source-audited primary QUSB2 PHY reset BCR. */
#define XZS_USB_GCC_PHYS_BASE          0x00300000ULL
#define XZS_USB_GCC_MMIO_SIZE          0x00090000ULL

/* Qualcomm MSM8996 USB wrapper (QSCRATCH) read-only snapshot registers. */
#define QSCRATCH_RAM1                  0x000
#define QSCRATCH_GENERAL_CFG           0x008
#define QSCRATCH_HS_PHY_CTRL           0x010
#define QSCRATCH_SS_PHY_CTRL           0x030
#define QSCRATCH_PWR_EVENT_IRQ_STAT    0x058

#define QSCRATCH_UTMI_OTG_VBUS_VALID   (1u << 20)
#define QSCRATCH_SW_SESSVLD_SEL        (1u << 28)

/* MSM8996 QUSB2 PHY read-only snapshot registers. */
#define QUSB2PHY_PLL_TEST              0x004
#define QUSB2PHY_PLL_STATUS            0x038
#define QUSB2PHY_PORT_POWERDOWN        0x0B4
#define QUSB2PHY_PORT_UTMI_STATUS      0x0F4

#define QUSB2PHY_PLL_TEST_CLK_REF_SEL  (1u << 7)
#define QUSB2PHY_PLL_STATUS_LOCKED     (1u << 5)
#define QUSB2PHY_PORT_POWER_DOWN       (1u << 0)

/* GCC QUSB2 primary PHY block-control reset (read-only in Candidate-2A). */
#define GCC_QUSB2PHY_PRIM_BCR          0x12038

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
#define DWC3_GHWPARAMS1               0xC144
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
#define DWC3_DGCMD                    0xC714
#define DWC3_DALEPENA                 0xC720
#define DWC3_OSTS                     0xCC10

/* Candidate-2B DCFG-only halted-state normalization masks. */
#define DWC3_DCFG_SPEED_MASK          0x00000007u
#define DWC3_DCFG_DEVADDR_MASK        0x000003F8u
#define DWC3_DCTL_RUN_STOP            (1u << 31)
#define DWC3_DSTS_DEVCTRLHLT          (1u << 22)
#define DWC3_DSTS_USBLNKST_MASK       (0x0fu << 18)
#define DWC3_DSTS_CONNECTSPD_MASK     0x00000007u

#define DWC3_DEVTEN_DISCONNECT        (1u << 0)
#define DWC3_DEVTEN_USBRESET          (1u << 1)
#define DWC3_DEVTEN_CONNECTDONE       (1u << 2)
#define DWC3_DEVTEN_T1Y_MASK          (DWC3_DEVTEN_DISCONNECT | \
                                      DWC3_DEVTEN_USBRESET | \
                                      DWC3_DEVTEN_CONNECTDONE)

/* Candidate-2C DWC3 2.70a event-buffer ownership definitions. */
#define DWC3_NUM_EVENT_INTERRUPTS(n)  (((n) >> 15) & 0x3fu)
#define DWC3_GEVNTSIZ_SIZE_MASK       0x0000FFFFu
#define DWC3_GEVNTSIZ_INTMASK         (1u << 31)
#define DWC3_GEVNTCOUNT_PENDING_MASK  0x0000FFFCu
#define XZS_DWC3_EVENT_BUFFER_SIZE    4096u

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
#define DEPCMD_PARAM(n)               ((uint32_t)(n) << 16)
#define DEPCMD_STATUS(n)              (((n) >> 12) & 0x0fu)
#define DEPCMD_RESOURCE_INDEX(n)      (((n) >> 16) & 0x7fu)

#define DWC3_DEPEVT_XFERCOMPLETE      0x01u
#define DWC3_DEPEVT_XFERNOTREADY      0x03u
#define DWC3_DEPEVT_STATUS_PHASE(n)   (((n) >> 12) & 0x03u)
#define DWC3_DEPEVT_STATUS_CONTROL_STATUS 0x02u

#define DWC3_DEVICE_EVENT_DISCONNECT  0x00u
#define DWC3_DEVICE_EVENT_RESET       0x01u
#define DWC3_DEVICE_EVENT_CONNECT_DONE 0x02u

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
extern volatile uint32_t g_xzs_usb_gctl;
extern volatile uint32_t g_xzs_usb_dsts;
extern volatile uint32_t g_xzs_usb_dcfg;
extern volatile uint32_t g_xzs_usb_dctl;
extern volatile uint32_t g_xzs_usb_gevntadr0;
extern volatile uint32_t g_xzs_usb_gevntsiz0;
extern volatile uint32_t g_xzs_usb_gevntcnt0;
extern volatile uint32_t g_xzs_usb_devten;
extern volatile uint32_t g_xzs_usb_qscratch_ram1;
extern volatile uint32_t g_xzs_usb_qscratch_cfg;
extern volatile uint32_t g_xzs_usb_qscratch_general_cfg;
extern volatile uint32_t g_xzs_usb_qscratch_hs_phy_ctrl;
extern volatile uint32_t g_xzs_usb_qscratch_ss_phy_ctrl;
extern volatile uint32_t g_xzs_usb_qscratch_pwr_event_irq_stat;
extern volatile uint32_t g_xzs_usb_gsts;
extern volatile uint32_t g_xzs_usb_gusb2phycfg0;
extern volatile uint32_t g_xzs_usb_gusb3pipectl0;
extern volatile uint32_t g_xzs_usb_osts;
extern volatile uint32_t g_xzs_usb_qusb2_pll_test;
extern volatile uint32_t g_xzs_usb_qusb2_pll_status;
extern volatile uint32_t g_xzs_usb_qusb2_port_powerdown;
extern volatile uint32_t g_xzs_usb_qusb2_utmi_status;
extern volatile uint32_t g_xzs_usb_gcc_qusb2phy_prim_bcr;
extern volatile uint32_t g_xzs_usb_gctl_before;
extern volatile uint32_t g_xzs_usb_gusb2phycfg0_before;
extern volatile uint32_t g_xzs_usb_dcfg_before;
extern volatile uint32_t g_xzs_usb_dcfg_written;
extern volatile uint32_t g_xzs_usb_dcfg_readback;
extern volatile uint32_t g_xzs_usb_dctl_before;
extern volatile uint32_t g_xzs_usb_dsts_before;
extern volatile uint32_t g_xzs_usb_gevntadr0_before;
extern volatile uint32_t g_xzs_usb_gevntsiz0_before;
extern volatile uint32_t g_xzs_usb_gevntcnt0_before;
extern volatile uint32_t g_xzs_usb_qscratch_general_cfg_before;
extern volatile uint32_t g_xzs_usb_qscratch_hs_phy_ctrl_before;
extern volatile uint32_t g_xzs_usb_qscratch_ss_phy_ctrl_before;
extern volatile uint32_t g_xzs_usb_qusb2_pll_status_before;
extern volatile uint32_t g_xzs_usb_qusb2_port_powerdown_before;
extern volatile uint32_t g_xzs_usb_gcc_qusb2phy_prim_bcr_before;
extern volatile uint32_t g_xzs_usb_candidate2b_precondition_run_stop_0;
extern volatile uint32_t g_xzs_usb_candidate2b_precondition_devctrlhlt_1;
extern volatile uint32_t g_xzs_usb_candidate2b_write_count;
extern volatile uint32_t g_xzs_usb_candidate2b_dcfg_write_match;
extern volatile uint32_t g_xzs_usb_candidate2b_gctl_unchanged;
extern volatile uint32_t g_xzs_usb_candidate2b_gusb2phycfg0_unchanged;
extern volatile uint32_t g_xzs_usb_candidate2b_qscratch_unchanged;
extern volatile uint32_t g_xzs_usb_candidate2b_qusb2_unchanged;
extern volatile uint32_t g_xzs_usb_candidate2b_gcc_unchanged;
extern volatile uint32_t g_xzs_usb_candidate2b_event_buffer_unchanged;
extern volatile uint32_t g_xzs_usb_candidate2b_complete;
extern volatile uint32_t g_xzs_usb_ghwparams1;
extern volatile uint32_t g_xzs_usb_num_event_interrupts;
extern volatile uint32_t g_xzs_usb_gevntadrhi0_before;
extern volatile uint32_t g_xzs_usb_gevntadrhi0;
extern volatile uint32_t g_xzs_usb_devten_before;
extern volatile uint32_t g_xzs_usb_candidate2c_precondition_run_stop_0;
extern volatile uint32_t g_xzs_usb_candidate2c_precondition_devctrlhlt_1;
extern volatile uint32_t g_xzs_usb_candidate2c_dcfg_high_speed;
extern volatile uint32_t g_xzs_usb_candidate2c_dcfg_devaddr_0;
extern volatile uint32_t g_xzs_usb_candidate2c_event_buffer_pa_valid;
extern volatile uint32_t g_xzs_usb_candidate2c_event_buffer_contiguous;
extern volatile uint32_t g_xzs_usb_candidate2c_event_buffer_aligned;
extern volatile uint32_t g_xzs_usb_candidate2c_event_buffer_lifetime_static;
extern volatile uint32_t g_xzs_usb_candidate2c_dcfg_write_count;
extern volatile uint32_t g_xzs_usb_candidate2c_gevntadrlo_write_count;
extern volatile uint32_t g_xzs_usb_candidate2c_gevntadrhi_write_count;
extern volatile uint32_t g_xzs_usb_candidate2c_gevntsiz_write_count;
extern volatile uint32_t g_xzs_usb_candidate2c_gevntcount_write_count;
extern volatile uint32_t g_xzs_usb_candidate2c_gevntcount_ack;
extern volatile uint32_t g_xzs_usb_candidate2c_gevntadr_readback_match;
extern volatile uint32_t g_xzs_usb_candidate2c_gevntsiz_readback_match;
extern volatile uint32_t g_xzs_usb_candidate2c_devten_unchanged;
extern volatile uint32_t g_xzs_usb_candidate2c_complete;
extern volatile uint64_t g_xzs_usb_candidate2c_event_buffer_va;
extern volatile uint64_t g_xzs_usb_candidate2c_event_buffer_pa;
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
extern volatile uint32_t g_xzs_usb_t1y_ep0_only_dispatch;
extern volatile uint32_t g_xzs_usb_t1y_bulk_endpoint_write_count;
extern volatile uint32_t g_xzs_usb_t1y_tty_bridge_call_count;
extern volatile uint32_t g_xzs_usb_t1y_devten_before;
extern volatile uint32_t g_xzs_usb_t1y_devten_after;
extern volatile uint32_t g_xzs_usb_t1y_gevntsiz_before;
extern volatile uint32_t g_xzs_usb_t1y_gevntsiz_after;
extern volatile uint32_t g_xzs_usb_t1y_dctl_before;
extern volatile uint32_t g_xzs_usb_t1y_dctl_written;
extern volatile uint32_t g_xzs_usb_t1y_dctl_after;
extern volatile uint32_t g_xzs_usb_t1y_dctl_write_count;
extern volatile uint32_t g_xzs_usb_t1y_first_event;
extern volatile uint32_t g_xzs_usb_t1y_event_dma_working;
extern volatile uint32_t g_xzs_usb_t1y_setup_observed;
extern volatile uint32_t g_xzs_usb_t1y_device_desc_complete;
extern volatile uint32_t g_xzs_usb_t1y_config_desc_complete;
extern volatile uint32_t g_xzs_usb_t1y_set_address_complete;
extern volatile uint32_t g_xzs_usb_t1y_set_configuration_complete;
extern volatile uint32_t g_xzs_usb_t1y_configuration_value;
extern volatile uint32_t g_xzs_usb_t1y_connect_speed;
extern volatile uint32_t g_xzs_usb_t1y_link_state;
extern volatile uint32_t g_xzs_usb_t1y_ep_cmd_failures;
extern volatile uint32_t g_xzs_usb_t1y_event_ring_drops;
extern volatile uint32_t g_xzs_usb_t1y_complete;

#endif /* _PEXPERT_ARM_XZS_USB_H_ */
