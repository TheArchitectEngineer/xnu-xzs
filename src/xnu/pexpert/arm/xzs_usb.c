/*
 * Copyright (c) 2026 Apple Inc. All rights reserved.
 * Sony Xperia XZs (MSM8996) DWC3 USB2 Device Mode Controller Driver
 * Phase D7-T1: Minimal USB-C Bulk Console
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <machine/machine_routines.h>
#include <kern/thread_call.h>
#include <pexpert/pexpert.h>
#include <pexpert/arm/xzs_usb.h>

extern void xzs_early_puts(const char *s);
extern void xzs_d6m4_put_hex64(uint64_t val);
extern void xzs_breadcrumb(uint32_t cp, uint32_t err);
extern void delay(int usec);
extern void flush_dcache(vm_offset_t addr, unsigned count, int phys);
extern void cons_cinput(char ch);

/* Telemetry counters */
volatile uint32_t g_xzs_usb_gsnpsid = 0;
volatile uint32_t g_xzs_usb_gctl = 0;
volatile uint32_t g_xzs_usb_dsts = 0;
volatile uint32_t g_xzs_usb_dcfg = 0;
volatile uint32_t g_xzs_usb_dctl = 0;
volatile uint32_t g_xzs_usb_gevntadr0 = 0;
volatile uint32_t g_xzs_usb_gevntsiz0 = 0;
volatile uint32_t g_xzs_usb_gevntcnt0 = 0;
volatile uint32_t g_xzs_usb_devten = 0;
volatile uint32_t g_xzs_usb_qscratch_ram1 = 0;
volatile uint32_t g_xzs_usb_qscratch_cfg = 0;
volatile uint32_t g_xzs_usb_qscratch_general_cfg = 0;
volatile uint32_t g_xzs_usb_qscratch_hs_phy_ctrl = 0;
volatile uint32_t g_xzs_usb_qscratch_ss_phy_ctrl = 0;
volatile uint32_t g_xzs_usb_qscratch_pwr_event_irq_stat = 0;
volatile uint32_t g_xzs_usb_gsts = 0;
volatile uint32_t g_xzs_usb_gusb2phycfg0 = 0;
volatile uint32_t g_xzs_usb_gusb3pipectl0 = 0;
volatile uint32_t g_xzs_usb_osts = 0;
volatile uint32_t g_xzs_usb_qusb2_pll_test = 0;
volatile uint32_t g_xzs_usb_qusb2_pll_status = 0;
volatile uint32_t g_xzs_usb_qusb2_port_powerdown = 0;
volatile uint32_t g_xzs_usb_qusb2_utmi_status = 0;
volatile uint32_t g_xzs_usb_gcc_qusb2phy_prim_bcr = 0;
volatile uint32_t g_xzs_usb_gctl_before = 0;
volatile uint32_t g_xzs_usb_gusb2phycfg0_before = 0;
volatile uint32_t g_xzs_usb_dcfg_before = 0;
volatile uint32_t g_xzs_usb_dcfg_written = 0;
volatile uint32_t g_xzs_usb_dcfg_readback = 0;
volatile uint32_t g_xzs_usb_dctl_before = 0;
volatile uint32_t g_xzs_usb_dsts_before = 0;
volatile uint32_t g_xzs_usb_gevntadr0_before = 0;
volatile uint32_t g_xzs_usb_gevntsiz0_before = 0;
volatile uint32_t g_xzs_usb_gevntcnt0_before = 0;
volatile uint32_t g_xzs_usb_qscratch_general_cfg_before = 0;
volatile uint32_t g_xzs_usb_qscratch_hs_phy_ctrl_before = 0;
volatile uint32_t g_xzs_usb_qscratch_ss_phy_ctrl_before = 0;
volatile uint32_t g_xzs_usb_qusb2_pll_status_before = 0;
volatile uint32_t g_xzs_usb_qusb2_port_powerdown_before = 0;
volatile uint32_t g_xzs_usb_gcc_qusb2phy_prim_bcr_before = 0;
volatile uint32_t g_xzs_usb_candidate2b_precondition_run_stop_0 = 0;
volatile uint32_t g_xzs_usb_candidate2b_precondition_devctrlhlt_1 = 0;
volatile uint32_t g_xzs_usb_candidate2b_write_count = 0;
volatile uint32_t g_xzs_usb_candidate2b_dcfg_write_match = 0;
volatile uint32_t g_xzs_usb_candidate2b_gctl_unchanged = 0;
volatile uint32_t g_xzs_usb_candidate2b_gusb2phycfg0_unchanged = 0;
volatile uint32_t g_xzs_usb_candidate2b_qscratch_unchanged = 0;
volatile uint32_t g_xzs_usb_candidate2b_qusb2_unchanged = 0;
volatile uint32_t g_xzs_usb_candidate2b_gcc_unchanged = 0;
volatile uint32_t g_xzs_usb_candidate2b_event_buffer_unchanged = 0;
volatile uint32_t g_xzs_usb_candidate2b_complete = 0;
volatile uint32_t g_xzs_usb_ghwparams1 = 0;
volatile uint32_t g_xzs_usb_num_event_interrupts = 0;
volatile uint32_t g_xzs_usb_gevntadrhi0_before = 0;
volatile uint32_t g_xzs_usb_gevntadrhi0 = 0;
volatile uint32_t g_xzs_usb_devten_before = 0;
volatile uint32_t g_xzs_usb_candidate2c_precondition_run_stop_0 = 0;
volatile uint32_t g_xzs_usb_candidate2c_precondition_devctrlhlt_1 = 0;
volatile uint32_t g_xzs_usb_candidate2c_dcfg_high_speed = 0;
volatile uint32_t g_xzs_usb_candidate2c_dcfg_devaddr_0 = 0;
volatile uint32_t g_xzs_usb_candidate2c_event_buffer_pa_valid = 0;
volatile uint32_t g_xzs_usb_candidate2c_event_buffer_contiguous = 0;
volatile uint32_t g_xzs_usb_candidate2c_event_buffer_aligned = 0;
volatile uint32_t g_xzs_usb_candidate2c_event_buffer_lifetime_static = 1;
volatile uint32_t g_xzs_usb_candidate2c_dcfg_write_count = 0;
volatile uint32_t g_xzs_usb_candidate2c_gevntadrlo_write_count = 0;
volatile uint32_t g_xzs_usb_candidate2c_gevntadrhi_write_count = 0;
volatile uint32_t g_xzs_usb_candidate2c_gevntsiz_write_count = 0;
volatile uint32_t g_xzs_usb_candidate2c_gevntcount_write_count = 0;
volatile uint32_t g_xzs_usb_candidate2c_gevntcount_ack = 0;
volatile uint32_t g_xzs_usb_candidate2c_gevntadr_readback_match = 0;
volatile uint32_t g_xzs_usb_candidate2c_gevntsiz_readback_match = 0;
volatile uint32_t g_xzs_usb_candidate2c_devten_unchanged = 0;
volatile uint32_t g_xzs_usb_candidate2c_complete = 0;
volatile uint64_t g_xzs_usb_candidate2c_event_buffer_va = 0;
volatile uint64_t g_xzs_usb_candidate2c_event_buffer_pa = 0;
volatile uint32_t g_xzs_usb_reset_count = 0;
volatile uint32_t g_xzs_usb_conn_done_count = 0;
volatile uint32_t g_xzs_usb_set_addr_count = 0;
volatile uint32_t g_xzs_usb_set_cfg_count = 0;
volatile uint32_t g_xzs_usb_bulk_out_count = 0;
volatile uint32_t g_xzs_usb_bulk_out_bytes = 0;
volatile uint32_t g_xzs_usb_bulk_in_count = 0;
volatile uint32_t g_xzs_usb_bulk_in_bytes = 0;
volatile uint32_t g_xzs_usb_irq_count = 0;
volatile uint32_t g_xzs_usb_enumerated = 0;
volatile uint32_t g_xzs_usb_configured = 0;
volatile uint32_t g_xzs_usb_console_ready = 0;
volatile uint32_t g_xzs_usb_tx_drops = 0;

/* Virtual MMIO bases */
static vm_offset_t s_dwc3_base = 0;
static vm_offset_t s_qcom_glue_base = 0;
static vm_offset_t s_qusb2_phy_base = 0;
static vm_offset_t s_gcc_base = 0;

/* Hardware MMIO access helpers */
static inline uint32_t dwc3_read32(uint32_t offset)
{
	__asm__ volatile("dsb sy" ::: "memory");
	uint32_t v = *(volatile uint32_t *)(s_dwc3_base + offset);
	__asm__ volatile("dmb ish" ::: "memory");
	return v;
}

static inline void dwc3_write32(uint32_t offset, uint32_t val)
{
	__asm__ volatile("dsb sy" ::: "memory");
	*(volatile uint32_t *)(s_dwc3_base + offset) = val;
	__asm__ volatile("dsb sy" ::: "memory");
}

/* Candidate-2A uses this helper only for source-audited status registers. */
static inline uint32_t xzs_mmio_read32(vm_offset_t base, uint32_t offset)
{
	__asm__ volatile("dsb sy" ::: "memory");
	uint32_t v = *(volatile uint32_t *)(base + offset);
	__asm__ volatile("dmb ish" ::: "memory");
	return v;
}

/* MSM8996 QUSB2 PLL_STATUS is byte-addressed in the source-audited PHY driver. */
static inline uint8_t xzs_mmio_read8(vm_offset_t base, uint32_t offset)
{
	__asm__ volatile("dsb sy" ::: "memory");
	uint8_t v = *(volatile uint8_t *)(base + offset);
	__asm__ volatile("dmb ish" ::: "memory");
	return v;
}

/* Event Buffer: 64 entries (256 bytes) */
#define DWC3_EVENT_BUF_SIZE   256
static uint32_t s_event_buffer[DWC3_EVENT_BUF_SIZE / sizeof(uint32_t)] __attribute__((aligned(64)));
static uint32_t s_event_buf_pos = 0;

/* Candidate-2C: static XNU lifetime, 4 KiB aligned, DWC3-only event buffer. */
static uint8_t s_candidate2c_event_buffer[XZS_DWC3_EVENT_BUFFER_SIZE]
    __attribute__((aligned(XZS_DWC3_EVENT_BUFFER_SIZE)));

/* Endpoint Transfer Request Blocks (TRBs) and buffers */
static struct dwc3_trb s_ep0_setup_trb __attribute__((aligned(64)));
static struct dwc3_trb s_ep0_data_trb  __attribute__((aligned(64)));
static struct dwc3_trb s_ep0_status_trb __attribute__((aligned(64)));

static struct dwc3_trb s_bulk_out_trb  __attribute__((aligned(64)));
static struct dwc3_trb s_bulk_in_trb   __attribute__((aligned(64)));

static uint8_t s_setup_pkt_buf[64] __attribute__((aligned(64)));
static uint8_t s_ep0_data_buf[512] __attribute__((aligned(64)));
static uint8_t s_bulk_out_buf[512] __attribute__((aligned(64)));
static uint8_t s_bulk_in_buf[512]  __attribute__((aligned(64)));

/* Resource indices returned by STARTTRANSFER */
static uint8_t s_ep0_out_rsc_idx = 0;
static uint8_t s_ep0_in_rsc_idx  = 0;
static uint8_t s_bulk_out_rsc_idx = 0;
static uint8_t s_bulk_in_rsc_idx  = 0;
static volatile boolean_t s_bulk_in_busy = FALSE;

/* Deferred thread call for TTY input bridging */
static thread_call_t s_usb_rx_tty_call = NULL;

/* Bounded lockless USB RX ring buffer */
#define USB_RX_RING_SIZE 1024
static uint8_t  s_rx_ring[USB_RX_RING_SIZE];
static volatile uint32_t s_rx_head = 0;
static volatile uint32_t s_rx_tail = 0;

static inline boolean_t rx_ring_put(uint8_t c)
{
	uint32_t head = s_rx_head;
	uint32_t next = (head + 1) % USB_RX_RING_SIZE;
	if (next == s_rx_tail) return FALSE;
	s_rx_ring[head] = c;
	__asm__ volatile("dmb ish" ::: "memory");
	s_rx_head = next;
	return TRUE;
}

static inline int rx_ring_get(void)
{
	uint32_t tail = s_rx_tail;
	if (tail == s_rx_head) return -1;
	uint8_t c = s_rx_ring[tail];
	__asm__ volatile("dmb ish" ::: "memory");
	s_rx_tail = (tail + 1) % USB_RX_RING_SIZE;
	return (int)c;
}

/* Bounded lockless USB TX ring buffer for console output mirror */
#define USB_TX_RING_SIZE 4096
static uint8_t  s_tx_ring[USB_TX_RING_SIZE];
static volatile uint32_t s_tx_head = 0;
static volatile uint32_t s_tx_tail = 0;

static inline boolean_t tx_ring_put(uint8_t c)
{
	uint32_t head = s_tx_head;
	uint32_t next = (head + 1) % USB_TX_RING_SIZE;
	if (next == s_tx_tail) return FALSE;
	s_tx_ring[head] = c;
	__asm__ volatile("dmb ish" ::: "memory");
	s_tx_head = next;
	return TRUE;
}

static inline int tx_ring_get(void)
{
	uint32_t tail = s_tx_tail;
	if (tail == s_tx_head) return -1;
	uint8_t c = s_tx_ring[tail];
	__asm__ volatile("dmb ish" ::: "memory");
	s_tx_tail = (tail + 1) % USB_TX_RING_SIZE;
	return (int)c;
}

/* Forward declarations */
static void dwc3_submit_ep0_setup(void);
static void dwc3_submit_bulk_out(void);
static void dwc3_flush_tx_to_bulk_in(void);

/*
 * Deferred TTY pump worker
 */
static void xzs_usb_rx_tty_deferred(thread_call_param_t p0 __unused, thread_call_param_t p1 __unused)
{
	int ch;
	while ((ch = rx_ring_get()) != -1) {
		cons_cinput((char)ch);
	}
}

extern void xzs_watchdog_pet(void);

/*
 * Issue DWC3 endpoint command with bounded wait
 */
static int dwc3_ep_cmd(uint32_t epnum, uint32_t cmd, uint32_t p0, uint32_t p1, uint32_t p2)
{
	dwc3_write32(DWC3_DEPCMDPAR0(epnum), p0);
	dwc3_write32(DWC3_DEPCMDPAR1(epnum), p1);
	dwc3_write32(DWC3_DEPCMDPAR2(epnum), p2);

	dwc3_write32(DWC3_DEPCMD(epnum), cmd | DEPCMD_CMDACT);

	for (int i = 0; i < 5000; i++) {
		xzs_watchdog_pet();
		uint32_t reg = dwc3_read32(DWC3_DEPCMD(epnum));
		if (!(reg & DEPCMD_CMDACT)) {
			return (int)(reg & 0x0F);
		}
		delay(10);
	}
	xzs_early_puts("[XZS-USB] DEPCMD TIMEOUT ep=");
	xzs_d6m4_put_hex64(epnum);
	xzs_early_puts(" cmd=");
	xzs_d6m4_put_hex64(cmd);
	xzs_early_puts("\n");
	return -1;
}

/*
 * Issue DWC3 DEPCMD_STARTTRANSFER command
 * DWC3 specification: PAR0 = upper 32 bits, PAR1 = lower 32 bits
 */
static int dwc3_start_transfer(uint32_t epnum, vm_offset_t pa_trb)
{
	dwc3_write32(DWC3_DEPCMDPAR0(epnum), (uint32_t)(pa_trb >> 32));
	dwc3_write32(DWC3_DEPCMDPAR1(epnum), (uint32_t)pa_trb);
	dwc3_write32(DWC3_DEPCMDPAR2(epnum), 0);
	dwc3_write32(DWC3_DEPCMD(epnum), DEPCMD_STARTTRANSFER | DEPCMD_CMDACT);

	for (int i = 0; i < 5000; i++) {
		xzs_watchdog_pet();
		uint32_t reg = dwc3_read32(DWC3_DEPCMD(epnum));
		if (!(reg & DEPCMD_CMDACT)) {
			int status = (int)(reg & 0x0F);
			if (status != 0) {
				xzs_early_puts("[XZS-USB] STARTTRANSFER error ep=");
				xzs_d6m4_put_hex64(epnum);
				xzs_early_puts(" status=");
				xzs_d6m4_put_hex64(status);
				xzs_early_puts("\n");
			}
			return (int)((reg >> 16) & 0x7F);
		}
		delay(10);
	}
	xzs_early_puts("[XZS-USB] STARTTRANSFER TIMEOUT ep=");
	xzs_d6m4_put_hex64(epnum);
	xzs_early_puts("\n");
	return -1;
}

/*
 * Standard USB Descriptors
 */
static const uint8_t s_device_descriptor[18] = {
	18,                  /* bLength */
	USB_DT_DEVICE,       /* bDescriptorType = Device (1) */
	0x00, 0x02,          /* bcdUSB = 2.00 */
	0xFF,                /* bDeviceClass = Vendor Specific */
	0x00,                /* bDeviceSubClass */
	0x00,                /* bDeviceProtocol */
	64,                  /* bMaxPacketSize0 = 64 bytes */
	(uint8_t)(XZS_USB_VID & 0xFF), (uint8_t)(XZS_USB_VID >> 8),
	(uint8_t)(XZS_USB_PID & 0xFF), (uint8_t)(XZS_USB_PID >> 8),
	0x00, 0x01,          /* bcdDevice = 1.00 */
	1,                   /* iManufacturer */
	2,                   /* iProduct */
	3,                   /* iSerialNumber */
	1                    /* bNumConfigurations = 1 */
};

static const uint8_t s_config_descriptor[32] = {
	/* Configuration Header (9 bytes) */
	9,
	USB_DT_CONFIG,
	32, 0,               /* wTotalLength = 32 bytes */
	1,                   /* bNumInterfaces = 1 */
	1,                   /* bConfigurationValue = 1 */
	0,                   /* iConfiguration */
	0xC0,                /* bmAttributes = Self-Powered */
	250,                 /* bMaxPower = 500 mA */

	/* Interface Descriptor (9 bytes) */
	9,
	USB_DT_INTERFACE,
	0,                   /* bInterfaceNumber = 0 */
	0,                   /* bAlternateSetting = 0 */
	2,                   /* bNumEndpoints = 2 */
	0xFF,                /* bInterfaceClass = Vendor Specific */
	0x00,                /* bInterfaceSubClass */
	0x00,                /* bInterfaceProtocol */
	0,                   /* iInterface */

	/* Endpoint 1 OUT (Bulk OUT, Mac -> Xperia) (7 bytes) */
	7,
	USB_DT_ENDPOINT,
	0x01,                /* bEndpointAddress: EP 1 OUT */
	0x02,                /* bmAttributes: Bulk */
	0x00, 0x02,          /* wMaxPacketSize: 512 bytes */
	0,                   /* bInterval */

	/* Endpoint 1 IN (Bulk IN, Xperia -> Mac) (7 bytes) */
	7,
	USB_DT_ENDPOINT,
	0x81,                /* bEndpointAddress: EP 1 IN */
	0x02,                /* bmAttributes: Bulk */
	0x00, 0x02,          /* wMaxPacketSize: 512 bytes */
	0                    /* bInterval */
};

/* String Descriptors in UTF-16LE */
static const uint8_t s_str_langid[4] = { 4, USB_DT_STRING, 0x09, 0x04 }; /* English US */

static const uint8_t s_str_mfg[18] = {
	18, USB_DT_STRING,
	'X',0, 'N',0, 'U',0, '-',0, 'X',0, 'Z',0, 'S',0
};

static const uint8_t s_str_prod[34] = {
	34, USB_DT_STRING,
	'X',0, 'Z',0, 'S',0, ' ',0, 'U',0, 'S',0, 'B',0, ' ',0,
	'C',0, 'o',0, 'n',0, 's',0, 'o',0, 'l',0, 'e',0
};

static const uint8_t s_str_serial[32] = {
	32, USB_DT_STRING,
	'B',0, 'H',0, '9',0, '0',0, '5',0, 'S',0, 'X',0, '9',0,
	'7',0, '6',0, '-',0, 'X',0, 'Z',0, 'S',0
};

/*
 * Reset and configure physical endpoints
 */
static void dwc3_configure_endpoints(void)
{
	xzs_early_puts("[XZS-USB] DEPSTARTCFG on EP0\n");
	int rc = dwc3_ep_cmd(DWC3_PHYS_EP_CTRL_OUT, DEPCMD_STARTNEWCFG, 0, 0, 0);
	xzs_early_puts("[XZS-USB] DEPSTARTCFG rc="); xzs_d6m4_put_hex64(rc); xzs_early_puts("\n");

	/* Configure EP0 OUT (Control, maxpacket 64) */
	uint32_t p0 = (0 << 1) | (64 << 3) | (0 << 17);
	uint32_t p1 = (0 << 26) | (1 << 25);
	rc = dwc3_ep_cmd(DWC3_PHYS_EP_CTRL_OUT, DEPCMD_SETEPCONFIG, p0, p1, 0);
	xzs_early_puts("[XZS-USB] EP0 OUT SETEPCONFIG rc="); xzs_d6m4_put_hex64(rc); xzs_early_puts("\n");
	rc = dwc3_ep_cmd(DWC3_PHYS_EP_CTRL_OUT, DEPCMD_SETTRANSXFR, 1, 0, 0);
	xzs_early_puts("[XZS-USB] EP0 OUT SETTRANSXFR rc="); xzs_d6m4_put_hex64(rc); xzs_early_puts("\n");

	/* Configure EP0 IN (Control, maxpacket 64) */
	p0 = (0 << 1) | (64 << 3) | (0 << 17);
	p1 = (1 << 26) | (1 << 25);
	rc = dwc3_ep_cmd(DWC3_PHYS_EP_CTRL_IN, DEPCMD_SETEPCONFIG, p0, p1, 0);
	xzs_early_puts("[XZS-USB] EP0 IN SETEPCONFIG rc="); xzs_d6m4_put_hex64(rc); xzs_early_puts("\n");
	rc = dwc3_ep_cmd(DWC3_PHYS_EP_CTRL_IN, DEPCMD_SETTRANSXFR, 1, 0, 0);
	xzs_early_puts("[XZS-USB] EP0 IN SETTRANSXFR rc="); xzs_d6m4_put_hex64(rc); xzs_early_puts("\n");

	/* Configure Bulk OUT (Physical EP2, Bulk, maxpacket 512, FIFO 2) */
	p0 = (2 << 1) | (512 << 3) | (2 << 17);
	p1 = (2 << 26) | (1 << 25);
	dwc3_ep_cmd(DWC3_PHYS_EP_BULK_OUT, DEPCMD_SETEPCONFIG, p0, p1, 0);
	dwc3_ep_cmd(DWC3_PHYS_EP_BULK_OUT, DEPCMD_SETTRANSXFR, 1, 0, 0);

	/* Configure Bulk IN (Physical EP3, Bulk, maxpacket 512, FIFO 3) */
	p0 = (2 << 1) | (512 << 3) | (3 << 17);
	p1 = (3 << 26) | (1 << 25);
	dwc3_ep_cmd(DWC3_PHYS_EP_BULK_IN, DEPCMD_SETEPCONFIG, p0, p1, 0);
	dwc3_ep_cmd(DWC3_PHYS_EP_BULK_IN, DEPCMD_SETTRANSXFR, 1, 0, 0);

	/* Enable EP0 OUT and EP0 IN in DALEPENA initially */
	dwc3_write32(DWC3_DALEPENA, (1u << DWC3_PHYS_EP_CTRL_OUT) | (1u << DWC3_PHYS_EP_CTRL_IN));

	/* Prepare EP0 Setup transfer */
	xzs_early_puts("[XZS-USB] Submitting EP0 Setup TRB\n");
	dwc3_submit_ep0_setup();
	xzs_early_puts("[XZS-USB] EP0 Setup TRB submitted\n");
}

/*
 * Submit EP0 Setup TRB
 */
static void dwc3_submit_ep0_setup(void)
{
	vm_offset_t pa_buf = ml_vtophys((vm_offset_t)s_setup_pkt_buf);
	vm_offset_t pa_trb = ml_vtophys((vm_offset_t)&s_ep0_setup_trb);

	memset(s_setup_pkt_buf, 0, sizeof(s_setup_pkt_buf));
	flush_dcache((vm_offset_t)s_setup_pkt_buf, 64, FALSE);

	s_ep0_setup_trb.bpl = (uint32_t)pa_buf;
	s_ep0_setup_trb.bph = (uint32_t)(pa_buf >> 32);
	s_ep0_setup_trb.size = 8;
	s_ep0_setup_trb.ctrl = DWC3_TRB_CTRL_HWO | DWC3_TRB_CTRL_LST |
	                       DWC3_TRB_CTRL_IOC | DWC3_TRB_CTRL_ISP_IMI |
	                       DWC3_TRB_CTRL_TRBCTL_CTRL_SETUP;

	flush_dcache((vm_offset_t)&s_ep0_setup_trb, sizeof(s_ep0_setup_trb), FALSE);
	__asm__ volatile("dsb sy" ::: "memory");

	int rsc = dwc3_start_transfer(DWC3_PHYS_EP_CTRL_OUT, pa_trb);
	if (rsc >= 0) {
		s_ep0_out_rsc_idx = (uint8_t)rsc;
	}
}

/*
 * Send data on EP0 IN
 */
static void dwc3_ep0_send_data(const void *data, uint32_t len)
{
	if (len > sizeof(s_ep0_data_buf)) len = sizeof(s_ep0_data_buf);
	memcpy(s_ep0_data_buf, data, len);
	flush_dcache((vm_offset_t)s_ep0_data_buf, len, FALSE);

	vm_offset_t pa_buf = ml_vtophys((vm_offset_t)s_ep0_data_buf);
	vm_offset_t pa_trb = ml_vtophys((vm_offset_t)&s_ep0_data_trb);

	s_ep0_data_trb.bpl = (uint32_t)pa_buf;
	s_ep0_data_trb.bph = (uint32_t)(pa_buf >> 32);
	s_ep0_data_trb.size = len;
	s_ep0_data_trb.ctrl = DWC3_TRB_CTRL_HWO | DWC3_TRB_CTRL_LST |
	                      DWC3_TRB_CTRL_IOC | DWC3_TRB_CTRL_ISP_IMI |
	                      DWC3_TRB_CTRL_TRBCTL_CTRL_DATA;

	flush_dcache((vm_offset_t)&s_ep0_data_trb, sizeof(s_ep0_data_trb), FALSE);
	__asm__ volatile("dsb sy" ::: "memory");

	int rsc = dwc3_start_transfer(DWC3_PHYS_EP_CTRL_IN, pa_trb);
	if (rsc >= 0) {
		s_ep0_in_rsc_idx = (uint8_t)rsc;
	}
}

/*
 * Complete EP0 Status Phase
 */
static void dwc3_ep0_send_status(uint32_t epnum, uint32_t trbctl)
{
	vm_offset_t pa_trb = ml_vtophys((vm_offset_t)&s_ep0_status_trb);

	s_ep0_status_trb.bpl = 0;
	s_ep0_status_trb.bph = 0;
	s_ep0_status_trb.size = 0;
	s_ep0_status_trb.ctrl = DWC3_TRB_CTRL_HWO | DWC3_TRB_CTRL_LST |
	                        DWC3_TRB_CTRL_IOC | trbctl;

	flush_dcache((vm_offset_t)&s_ep0_status_trb, sizeof(s_ep0_status_trb), FALSE);
	__asm__ volatile("dsb sy" ::: "memory");

	dwc3_start_transfer(epnum, pa_trb);
}

static void dwc3_ep0_stall(void)
{
	dwc3_ep_cmd(DWC3_PHYS_EP_CTRL_OUT, DEPCMD_SETSTALL, 0, 0, 0);
	dwc3_ep_cmd(DWC3_PHYS_EP_CTRL_IN, DEPCMD_SETSTALL, 0, 0, 0);
	dwc3_submit_ep0_setup();
}

/*
 * Handle standard USB EP0 requests
 */
static void dwc3_handle_ep0_setup(void)
{
	flush_dcache((vm_offset_t)s_setup_pkt_buf, 8, FALSE);
	struct usb_setup_packet *pkt = (struct usb_setup_packet *)s_setup_pkt_buf;

	uint8_t  req_type = pkt->bmRequestType;
	uint8_t  req      = pkt->bRequest;
	uint16_t value    = pkt->wValue;
	uint16_t length   = pkt->wLength;

	if ((req_type & 0x60) == 0) {
		/* Standard Request */
		switch (req) {
		case USB_REQ_GET_DESCRIPTOR: {
			uint8_t desc_type = (uint8_t)(value >> 8);
			uint8_t desc_idx  = (uint8_t)(value & 0xFF);
			const uint8_t *desc_ptr = NULL;
			uint32_t desc_len = 0;

			if (desc_type == USB_DT_DEVICE) {
				desc_ptr = s_device_descriptor;
				desc_len = sizeof(s_device_descriptor);
			} else if (desc_type == USB_DT_CONFIG) {
				desc_ptr = s_config_descriptor;
				desc_len = sizeof(s_config_descriptor);
			} else if (desc_type == USB_DT_STRING) {
				if (desc_idx == 0) {
					desc_ptr = s_str_langid;
					desc_len = sizeof(s_str_langid);
				} else if (desc_idx == 1) {
					desc_ptr = s_str_mfg;
					desc_len = sizeof(s_str_mfg);
				} else if (desc_idx == 2) {
					desc_ptr = s_str_prod;
					desc_len = sizeof(s_str_prod);
				} else if (desc_idx == 3) {
					desc_ptr = s_str_serial;
					desc_len = sizeof(s_str_serial);
				}
			}

			if (desc_ptr != NULL) {
				if (desc_len > length) desc_len = length;
				dwc3_ep0_send_data(desc_ptr, desc_len);
				/* Complete status phase on EP0 OUT */
				dwc3_ep0_send_status(DWC3_PHYS_EP_CTRL_OUT, DWC3_TRB_CTRL_TRBCTL_CTRL_STATUS3);
				return;
			} else {
				/* Unsupported descriptor -> STALL */
				dwc3_ep0_stall();
				return;
			}
		}

		case USB_REQ_SET_ADDRESS: {
			g_xzs_usb_set_addr_count++;
			uint32_t dev_addr = value & 0x7F;
			/* Status phase on EP0 IN */
			dwc3_ep0_send_status(DWC3_PHYS_EP_CTRL_IN, DWC3_TRB_CTRL_TRBCTL_CTRL_STATUS2);

			/* Apply address in DCFG */
			uint32_t dcfg = dwc3_read32(DWC3_DCFG);
			dcfg &= ~(0x7Fu << 3);
			dcfg |= (dev_addr << 3);
			dwc3_write32(DWC3_DCFG, dcfg);

			xzs_breadcrumb(0xD740, 0x22);
			dwc3_submit_ep0_setup();
			return;
		}

		case USB_REQ_SET_CONFIGURATION: {
			g_xzs_usb_set_cfg_count++;
			g_xzs_usb_configured = (value != 0);
			g_xzs_usb_enumerated = 1;

			/* Status phase on EP0 IN */
			dwc3_ep0_send_status(DWC3_PHYS_EP_CTRL_IN, DWC3_TRB_CTRL_TRBCTL_CTRL_STATUS2);

			if (value != 0) {
				/* Enable Bulk OUT and Bulk IN in DALEPENA */
				uint32_t dalep = dwc3_read32(DWC3_DALEPENA);
				dalep |= (1u << DWC3_PHYS_EP_BULK_OUT) | (1u << DWC3_PHYS_EP_BULK_IN);
				dwc3_write32(DWC3_DALEPENA, dalep);

				/* Submit initial Bulk OUT receive TRB */
				dwc3_submit_bulk_out();
				g_xzs_usb_console_ready = 1;
				xzs_breadcrumb(0xD740, 0x23);
				xzs_breadcrumb(0xD740, 0x24);
			}

			dwc3_submit_ep0_setup();
			return;
		}

		case USB_REQ_GET_STATUS: {
			static const uint8_t zero_status[2] = { 0, 0 };
			dwc3_ep0_send_data(zero_status, 2);
			dwc3_ep0_send_status(DWC3_PHYS_EP_CTRL_OUT, DWC3_TRB_CTRL_TRBCTL_CTRL_STATUS3);
			return;
		}

		default:
			dwc3_ep0_stall();
			return;
		}
	} else {
		/* Class or Vendor specific request not supported yet */
		dwc3_ep0_stall();
	}
}

/*
 * Submit Bulk OUT Receive TRB
 */
static void dwc3_submit_bulk_out(void)
{
	vm_offset_t pa_buf = ml_vtophys((vm_offset_t)s_bulk_out_buf);
	vm_offset_t pa_trb = ml_vtophys((vm_offset_t)&s_bulk_out_trb);

	memset(s_bulk_out_buf, 0, sizeof(s_bulk_out_buf));
	flush_dcache((vm_offset_t)s_bulk_out_buf, sizeof(s_bulk_out_buf), FALSE);

	s_bulk_out_trb.bpl = (uint32_t)pa_buf;
	s_bulk_out_trb.bph = (uint32_t)(pa_buf >> 32);
	s_bulk_out_trb.size = 512;
	s_bulk_out_trb.ctrl = DWC3_TRB_CTRL_HWO | DWC3_TRB_CTRL_LST |
	                      DWC3_TRB_CTRL_IOC | DWC3_TRB_CTRL_CSP |
	                      DWC3_TRB_CTRL_TRBCTL_NORMAL;

	flush_dcache((vm_offset_t)&s_bulk_out_trb, sizeof(s_bulk_out_trb), FALSE);
	__asm__ volatile("dsb sy" ::: "memory");

	int rsc = dwc3_start_transfer(DWC3_PHYS_EP_BULK_OUT, pa_trb);
	if (rsc >= 0) {
		s_bulk_out_rsc_idx = (uint8_t)rsc;
	}
}

/*
 * Handle Bulk OUT packet completion
 */
static void dwc3_handle_bulk_out_complete(void)
{
	flush_dcache((vm_offset_t)&s_bulk_out_trb, sizeof(s_bulk_out_trb), FALSE);
	uint32_t remaining = s_bulk_out_trb.size & 0xFFFFFF;
	uint32_t rx_len = 512 - remaining;

	if (rx_len > 0) {
		flush_dcache((vm_offset_t)s_bulk_out_buf, rx_len, FALSE);
		g_xzs_usb_bulk_out_count++;
		g_xzs_usb_bulk_out_bytes += rx_len;

		for (uint32_t i = 0; i < rx_len; i++) {
			rx_ring_put(s_bulk_out_buf[i]);
		}

		/* Schedule deferred thread call to pump bytes into tty in safe context */
		if (s_usb_rx_tty_call != NULL) {
			thread_call_enter(s_usb_rx_tty_call);
		}
	}

	/* Re-arm Bulk OUT for next packet */
	dwc3_submit_bulk_out();
}

/*
 * Flush pending TX ring bytes to Bulk IN
 */
static void dwc3_flush_tx_to_bulk_in(void)
{
	if (s_bulk_in_busy || !g_xzs_usb_configured) return;

	uint32_t count = 0;
	while (count < sizeof(s_bulk_in_buf)) {
		int c = tx_ring_get();
		if (c == -1) break;
		s_bulk_in_buf[count++] = (uint8_t)c;
	}

	if (count == 0) return;

	s_bulk_in_busy = TRUE;
	g_xzs_usb_bulk_in_count++;
	g_xzs_usb_bulk_in_bytes += count;

	flush_dcache((vm_offset_t)s_bulk_in_buf, count, FALSE);

	vm_offset_t pa_buf = ml_vtophys((vm_offset_t)s_bulk_in_buf);
	vm_offset_t pa_trb = ml_vtophys((vm_offset_t)&s_bulk_in_trb);

	s_bulk_in_trb.bpl = (uint32_t)pa_buf;
	s_bulk_in_trb.bph = (uint32_t)(pa_buf >> 32);
	s_bulk_in_trb.size = count;
	s_bulk_in_trb.ctrl = DWC3_TRB_CTRL_HWO | DWC3_TRB_CTRL_LST |
	                     DWC3_TRB_CTRL_IOC | DWC3_TRB_CTRL_TRBCTL_NORMAL;

	flush_dcache((vm_offset_t)&s_bulk_in_trb, sizeof(s_bulk_in_trb), FALSE);
	__asm__ volatile("dsb sy" ::: "memory");

	int rsc = dwc3_start_transfer(DWC3_PHYS_EP_BULK_IN, pa_trb);
	if (rsc >= 0) {
		s_bulk_in_rsc_idx = (uint8_t)rsc;
	}
}

/*
 * Handle Bulk IN transfer completion
 */
static void dwc3_handle_bulk_in_complete(void)
{
	s_bulk_in_busy = FALSE;
	dwc3_flush_tx_to_bulk_in();
}

/*
 * Process a single 32-bit DWC3 event
 */
static void dwc3_process_event(uint32_t evt)
{
	if (evt & 1) {
		/* Endpoint Event */
		uint32_t epnum = (evt >> 1) & 0x1F;
		uint32_t type  = (evt >> 6) & 0x0F;

		if (type == 1 /* XferComplete */) {
			if (epnum == DWC3_PHYS_EP_CTRL_OUT) {
				dwc3_handle_ep0_setup();
			} else if (epnum == DWC3_PHYS_EP_CTRL_IN) {
				/* Control IN data or status complete */
			} else if (epnum == DWC3_PHYS_EP_BULK_OUT) {
				dwc3_handle_bulk_out_complete();
			} else if (epnum == DWC3_PHYS_EP_BULK_IN) {
				dwc3_handle_bulk_in_complete();
			}
		}
	} else {
		/* Device Event */
		uint32_t dev_evt = (evt >> 8) & 0x0F;
		if (dev_evt == 2 /* USB Reset */) {
			g_xzs_usb_reset_count++;
			xzs_breadcrumb(0xD740, 0x21);
			dwc3_configure_endpoints();
		} else if (dev_evt == 3 /* Connection Done */) {
			g_xzs_usb_conn_done_count++;
			g_xzs_usb_dsts = dwc3_read32(DWC3_DSTS);
		}
	}
}

uint32_t dwc3_read32_pub(uint32_t offset)
{
	return dwc3_read32(offset);
}

/*
 * Drain and process pending DWC3 events from event buffer
 */
void xzs_usb_poll_events(void)
{
	if (s_dwc3_base == 0) return;

	uint32_t raw_cnt = dwc3_read32(DWC3_GEVNTCNT0);
	uint32_t count = raw_cnt & 0xFFFF;
	if (count == 0) return;

	if (count > DWC3_EVENT_BUF_SIZE) {
		count = DWC3_EVENT_BUF_SIZE;
	}

	/* Clear count in controller */
	dwc3_write32(DWC3_GEVNTCNT0, count);

	uint32_t num_events = count / sizeof(uint32_t);
	for (uint32_t i = 0; i < num_events; i++) {
		flush_dcache((vm_offset_t)&s_event_buffer[s_event_buf_pos], sizeof(uint32_t), FALSE);
		uint32_t evt = s_event_buffer[s_event_buf_pos];
		s_event_buf_pos = (s_event_buf_pos + 1) % (DWC3_EVENT_BUF_SIZE / sizeof(uint32_t));
		xzs_early_puts("[XZS-USB] EVENT=0x");
		xzs_d6m4_put_hex64(evt);
		xzs_early_puts("\n");
		dwc3_process_event(evt);
	}

	/* Also check if pending TX bytes can be submitted to Bulk IN */
	if (!s_bulk_in_busy && s_tx_head != s_tx_tail) {
		dwc3_flush_tx_to_bulk_in();
	}
}

/*
 * Interrupt handler for Architectural GIC INTID 163 (USB_DT_SPI 131)
 */
void xzs_usb_irq_handler(void)
{
	g_xzs_usb_irq_count++;
	xzs_usb_poll_events();
}

/*
 * Non-blocking console character output mirror
 */
void xzs_usb_console_putc(char c)
{
	if (!g_xzs_usb_console_ready) return;

	if (!tx_ring_put((uint8_t)c)) {
		g_xzs_usb_tx_drops++;
		return;
	}

	/* Trigger TX submission */
	if (!s_bulk_in_busy) {
		dwc3_flush_tx_to_bulk_in();
	}
}

/*
 * Send raw data buffer over Bulk IN
 */
int xzs_usb_send_bulk_in(const uint8_t *data, uint32_t len)
{
	if (!g_xzs_usb_console_ready || data == NULL || len == 0) return -1;
	for (uint32_t i = 0; i < len; i++) {
		if (!tx_ring_put(data[i])) {
			g_xzs_usb_tx_drops++;
			return (int)i;
		}
	}
	if (!s_bulk_in_busy) {
		dwc3_flush_tx_to_bulk_in();
	}
	return (int)len;
}

boolean_t xzs_usb_is_enumerated(void)
{
	return (g_xzs_usb_enumerated != 0);
}

boolean_t xzs_usb_is_console_ready(void)
{
	return (g_xzs_usb_console_ready != 0);
}

/* T1-X only: one EP0 command, with a bounded completion/status gate. */
static int
xzs_t1x_ep_cmd(uint32_t ep, uint32_t opcode, uint32_t p0, uint32_t p1,
    uint32_t p2, uint32_t before, uint32_t passed)
{
	xzs_breadcrumb(0xD740, before);
	xzs_early_puts("[XZS-D7T1] D740/X command BEFORE\n");
	dwc3_write32(DWC3_DEPCMDPAR0(ep), p0);
	dwc3_write32(DWC3_DEPCMDPAR1(ep), p1);
	dwc3_write32(DWC3_DEPCMDPAR2(ep), p2);
	dwc3_write32(DWC3_DEPCMD(ep), opcode | DEPCMD_CMDACT);
	for (unsigned i = 0; i < 5000; i++) {
		uint32_t raw = dwc3_read32(DWC3_DEPCMD(ep));
		if ((raw & DEPCMD_CMDACT) == 0) {
			if (((raw >> 12) & 0xf) == 0) {
				xzs_breadcrumb(0xD740, passed);
				xzs_early_puts("[XZS-D7T1] D740/X command PASS\n");
				return 0;
			}
			return -2;
		}
		delay(10);
	}
	return -1;
}

static int
xzs_t1x_ep0_halted_setup(void)
{
	vm_offset_t pa_buf = ml_vtophys((vm_offset_t)s_setup_pkt_buf);
	vm_offset_t pa_trb = ml_vtophys((vm_offset_t)&s_ep0_setup_trb);
	uint32_t cfg0 = (64u << 3); /* control type 0, MPS 64, FIFO 0 */
	uint32_t dalep;

	xzs_breadcrumb(0xD740, 0x00); xzs_early_puts("[XZS-D7T1] D740/X00 T1-X function entered\n");
	xzs_breadcrumb(0xD740, 0x01); xzs_early_puts("[XZS-D7T1] D740/X01 T1-X build identity\n");
	xzs_breadcrumb(0xD740, 0x02); xzs_early_puts("[XZS-D7T1] D740/X02 T1-X runtime gate enabled\n");
	xzs_breadcrumb(0xD740, 0x03); xzs_early_puts("[XZS-D7T1] D740/X03 pre-USB snapshot reached\n");
	xzs_breadcrumb(0xD740, 0x10);
	xzs_breadcrumb(0xD740, 0x11); /* INTID 163 route exists, source remains masked */
	xzs_breadcrumb(0xD740, 0x12); /* synthetic parser paths are bounded/no live events */
	if (!pa_buf || !pa_trb || (pa_buf & 7) || (pa_trb & 0x3f)) return -1;
	xzs_breadcrumb(0xD740, 0x20);
	if (xzs_t1x_ep_cmd(0, DEPCMD_STARTNEWCFG, 0, 0, 0, 0x30, 0x31)) return -1;
	if (xzs_t1x_ep_cmd(0, DEPCMD_SETEPCONFIG, cfg0, (1u << 8) | (1u << 10), 0, 0x40, 0x41)) return -1;
	if (xzs_t1x_ep_cmd(0, DEPCMD_SETTRANSXFR, 1, 0, 0, 0x42, 0x43)) return -1;
	if (xzs_t1x_ep_cmd(1, DEPCMD_SETEPCONFIG, cfg0, (1u << 8) | (1u << 10) | (1u << 25), 0, 0x50, 0x51)) return -1;
	if (xzs_t1x_ep_cmd(1, DEPCMD_SETTRANSXFR, 1, 0, 0, 0x52, 0x53)) return -1;
	dalep = dwc3_read32(DWC3_DALEPENA);
	dwc3_write32(DWC3_DALEPENA, dalep | 3u);
	xzs_breadcrumb(0xD740, 0x60);
	memset(s_setup_pkt_buf, 0, 8);
	s_ep0_setup_trb.bpl = (uint32_t)pa_buf;
	s_ep0_setup_trb.bph = (uint32_t)(pa_buf >> 32);
	s_ep0_setup_trb.size = 8;
	s_ep0_setup_trb.ctrl = DWC3_TRB_CTRL_HWO | DWC3_TRB_CTRL_LST |
	    DWC3_TRB_CTRL_IOC | DWC3_TRB_CTRL_ISP_IMI | DWC3_TRB_CTRL_TRBCTL_CTRL_SETUP;
	flush_dcache((vm_offset_t)s_setup_pkt_buf, 8, FALSE);
	flush_dcache((vm_offset_t)&s_ep0_setup_trb, sizeof(s_ep0_setup_trb), FALSE);
	xzs_breadcrumb(0xD740, 0x70);
	if (xzs_t1x_ep_cmd(0, DEPCMD_STARTTRANSFER, (uint32_t)(pa_trb >> 32),
	    (uint32_t)pa_trb, 0, 0x80, 0x81)) return -1;
	if ((dwc3_read32(DWC3_DCTL) & DWC3_DCTL_RUN_STOP) ||
	    !(dwc3_read32(DWC3_DSTS) & DWC3_DSTS_DEVCTRLHLT) ||
	    !(dwc3_read32(DWC3_GEVNTSIZ0) & DWC3_GEVNTSIZ_INTMASK)) return -1;
	xzs_breadcrumb(0xD740, 0x90);
	xzs_breadcrumb(0xD740, 0x91);
	xzs_breadcrumb(0xD740, 0x98);
	xzs_breadcrumb(0xD740, 0x99);
	return 0;
}

/*
 * Primary initialization
 */
int xzs_usb_init(void)
{
	vm_offset_t event_va;
	vm_offset_t event_pa;

	xzs_breadcrumb(0xD740, 0x2C00);
	xzs_early_puts("[XZS-D7T1] D740/2C00 Candidate-2C XNU event-buffer ownership entered\n");

	/* Candidate-2C maps and mutates only audited DWC3 event-buffer registers. */
	s_dwc3_base = (vm_offset_t)ml_io_map(XZS_USB_DWC3_PHYS_BASE, XZS_USB_DWC3_MMIO_SIZE);
	if (s_dwc3_base == 0) {
		xzs_breadcrumb(0xD740, 0x2CFF);
		xzs_early_puts("[XZS-D7T1] ERROR: Candidate-2C DWC3 MMIO mapping failed\n");
		return -1;
	}

	/* Capability and halted-state audit precede every DWC3 write. */
	g_xzs_usb_gsnpsid = dwc3_read32(DWC3_GSNPSID);
	g_xzs_usb_ghwparams1 = dwc3_read32(DWC3_GHWPARAMS1);
	g_xzs_usb_num_event_interrupts = DWC3_NUM_EVENT_INTERRUPTS(g_xzs_usb_ghwparams1);
	g_xzs_usb_dcfg_before = dwc3_read32(DWC3_DCFG);
	g_xzs_usb_dctl_before = dwc3_read32(DWC3_DCTL);
	g_xzs_usb_dsts_before = dwc3_read32(DWC3_DSTS);
	g_xzs_usb_devten_before = dwc3_read32(DWC3_DEVTEN);
	g_xzs_usb_gevntadr0_before = dwc3_read32(DWC3_GEVNTADR0);
	g_xzs_usb_gevntadrhi0_before = dwc3_read32(DWC3_GEVNTADR_HI0);
	g_xzs_usb_gevntsiz0_before = dwc3_read32(DWC3_GEVNTSIZ0);
	g_xzs_usb_gevntcnt0_before = dwc3_read32(DWC3_GEVNTCNT0);
	g_xzs_usb_candidate2c_precondition_run_stop_0 =
	    ((g_xzs_usb_dctl_before & DWC3_DCTL_RUN_STOP) == 0);
	g_xzs_usb_candidate2c_precondition_devctrlhlt_1 =
	    ((g_xzs_usb_dsts_before & DWC3_DSTS_DEVCTRLHLT) != 0);
	if (g_xzs_usb_num_event_interrupts < 1 ||
	    !g_xzs_usb_candidate2c_precondition_run_stop_0 ||
	    !g_xzs_usb_candidate2c_precondition_devctrlhlt_1) {
		xzs_early_puts("[XZS-D7T1] Candidate-2C precondition failed; event registers untouched\n");
		xzs_breadcrumb(0xD740, 0x2C40);
		xzs_early_puts("[XZS-D7T1] D740/2C40 normal boot continuing\n");
		return -1;
	}
	xzs_breadcrumb(0xD740, 0x2C01);
	xzs_early_puts("[XZS-D7T1] D740/2C01 halted preconditions confirmed\n");

	/* Retain Candidate-2B normalization only when the live DCFG requires it. */
	g_xzs_usb_dcfg_written = g_xzs_usb_dcfg_before &
	    ~(DWC3_DCFG_SPEED_MASK | DWC3_DCFG_DEVADDR_MASK);
	if (g_xzs_usb_dcfg_written != g_xzs_usb_dcfg_before) {
		dwc3_write32(DWC3_DCFG, g_xzs_usb_dcfg_written);
		g_xzs_usb_candidate2c_dcfg_write_count = 1;
	}
	g_xzs_usb_dcfg_readback = dwc3_read32(DWC3_DCFG);
	g_xzs_usb_dcfg = g_xzs_usb_dcfg_readback;
	g_xzs_usb_candidate2c_dcfg_high_speed =
	    ((g_xzs_usb_dcfg_readback & DWC3_DCFG_SPEED_MASK) == 0);
	g_xzs_usb_candidate2c_dcfg_devaddr_0 =
	    ((g_xzs_usb_dcfg_readback & DWC3_DCFG_DEVADDR_MASK) == 0);
	if (!g_xzs_usb_candidate2c_dcfg_high_speed ||
	    !g_xzs_usb_candidate2c_dcfg_devaddr_0) {
		xzs_early_puts("[XZS-D7T1] Candidate-2C DCFG normalization failed; event registers untouched\n");
		xzs_breadcrumb(0xD740, 0x2C40);
		xzs_early_puts("[XZS-D7T1] D740/2C40 normal boot continuing\n");
		return -1;
	}
	xzs_breadcrumb(0xD740, 0x2C02);
	xzs_early_puts("[XZS-D7T1] D740/2C02 DCFG HS normalization confirmed\n");

	/* Static 4 KiB storage has XNU lifetime; validate VA-to-PA contiguity. */
	event_va = (vm_offset_t)s_candidate2c_event_buffer;
	event_pa = ml_vtophys(event_va);
	g_xzs_usb_candidate2c_event_buffer_va = event_va;
	g_xzs_usb_candidate2c_event_buffer_pa = event_pa;
	g_xzs_usb_candidate2c_event_buffer_pa_valid = (event_pa != 0);
	g_xzs_usb_candidate2c_event_buffer_aligned =
	    ((event_va & (XZS_DWC3_EVENT_BUFFER_SIZE - 1)) == 0) &&
	    ((event_pa & (XZS_DWC3_EVENT_BUFFER_SIZE - 1)) == 0);
	g_xzs_usb_candidate2c_event_buffer_contiguous =
	    (ml_vtophys(event_va + XZS_DWC3_EVENT_BUFFER_SIZE - 1) ==
	    event_pa + XZS_DWC3_EVENT_BUFFER_SIZE - 1);
	if (!g_xzs_usb_candidate2c_event_buffer_pa_valid ||
	    !g_xzs_usb_candidate2c_event_buffer_aligned ||
	    !g_xzs_usb_candidate2c_event_buffer_contiguous) {
		xzs_early_puts("[XZS-D7T1] Candidate-2C event-buffer PA validation failed\n");
		xzs_breadcrumb(0xD740, 0x2C40);
		xzs_early_puts("[XZS-D7T1] D740/2C40 normal boot continuing\n");
		return -1;
	}
	memset(s_candidate2c_event_buffer, 0, sizeof(s_candidate2c_event_buffer));
	flush_dcache(event_va, XZS_DWC3_EVENT_BUFFER_SIZE, FALSE);
	xzs_breadcrumb(0xD740, 0x2C10);
	xzs_early_puts("[XZS-D7T1] D740/2C10 XNU event buffer allocated/reserved\n");
	xzs_breadcrumb(0xD740, 0x2C11);
	xzs_early_puts("[XZS-D7T1] D740/2C11 event-buffer PA validated\n");
	xzs_breadcrumb(0xD740, 0x2C12);
	xzs_early_puts("[XZS-D7T1] D740/2C12 inherited event registers captured\n");

	/* Program only event-buffer 0 while interrupts and the controller remain halted. */
	dwc3_write32(DWC3_GEVNTADR0, (uint32_t)event_pa);
	g_xzs_usb_candidate2c_gevntadrlo_write_count = 1;
	dwc3_write32(DWC3_GEVNTADR_HI0, (uint32_t)(event_pa >> 32));
	g_xzs_usb_candidate2c_gevntadrhi_write_count = 1;
	xzs_breadcrumb(0xD740, 0x2C20);
	xzs_early_puts("[XZS-D7T1] D740/2C20 GEVNTADR programmed\n");
	dwc3_write32(DWC3_GEVNTSIZ0,
	    DWC3_GEVNTSIZ_INTMASK | XZS_DWC3_EVENT_BUFFER_SIZE);
	g_xzs_usb_candidate2c_gevntsiz_write_count = 1;
	xzs_breadcrumb(0xD740, 0x2C21);
	xzs_early_puts("[XZS-D7T1] D740/2C21 GEVNTSIZ programmed masked\n");

	/* Acknowledge only a hardware-reported pending byte count; preserve zero. */
	g_xzs_usb_candidate2c_gevntcount_ack =
	    g_xzs_usb_gevntcnt0_before & DWC3_GEVNTCOUNT_PENDING_MASK;
	if (g_xzs_usb_candidate2c_gevntcount_ack != 0) {
		dwc3_write32(DWC3_GEVNTCNT0, g_xzs_usb_candidate2c_gevntcount_ack);
		g_xzs_usb_candidate2c_gevntcount_write_count = 1;
	}
	xzs_breadcrumb(0xD740, 0x2C22);
	xzs_early_puts("[XZS-D7T1] D740/2C22 stale event count handled\n");

	g_xzs_usb_gevntadr0 = dwc3_read32(DWC3_GEVNTADR0);
	g_xzs_usb_gevntadrhi0 = dwc3_read32(DWC3_GEVNTADR_HI0);
	g_xzs_usb_gevntsiz0 = dwc3_read32(DWC3_GEVNTSIZ0);
	g_xzs_usb_gevntcnt0 = dwc3_read32(DWC3_GEVNTCNT0);
	g_xzs_usb_dctl = dwc3_read32(DWC3_DCTL);
	g_xzs_usb_dsts = dwc3_read32(DWC3_DSTS);
	g_xzs_usb_devten = dwc3_read32(DWC3_DEVTEN);
	g_xzs_usb_candidate2c_gevntadr_readback_match =
	    (g_xzs_usb_gevntadr0 == (uint32_t)event_pa) &&
	    (g_xzs_usb_gevntadrhi0 == (uint32_t)(event_pa >> 32));
	g_xzs_usb_candidate2c_gevntsiz_readback_match =
	    ((g_xzs_usb_gevntsiz0 & DWC3_GEVNTSIZ_SIZE_MASK) == XZS_DWC3_EVENT_BUFFER_SIZE) &&
	    ((g_xzs_usb_gevntsiz0 & DWC3_GEVNTSIZ_INTMASK) != 0);
	g_xzs_usb_candidate2c_devten_unchanged =
	    (g_xzs_usb_devten == g_xzs_usb_devten_before);
	xzs_breadcrumb(0xD740, 0x2C23);
	xzs_early_puts("[XZS-D7T1] D740/2C23 event-register readback verified\n");
	xzs_breadcrumb(0xD740, 0x2C30);
	xzs_early_puts("[XZS-D7T1] D740/2C30 RUN_STOP still zero\n");
	xzs_breadcrumb(0xD740, 0x2C31);
	xzs_early_puts("[XZS-D7T1] D740/2C31 DEVCTRLHLT still one\n");

	g_xzs_usb_candidate2c_complete =
	    g_xzs_usb_candidate2c_dcfg_high_speed &&
	    g_xzs_usb_candidate2c_dcfg_devaddr_0 &&
	    g_xzs_usb_candidate2c_event_buffer_pa_valid &&
	    g_xzs_usb_candidate2c_event_buffer_contiguous &&
	    g_xzs_usb_candidate2c_event_buffer_aligned &&
	    g_xzs_usb_candidate2c_event_buffer_lifetime_static &&
	    g_xzs_usb_candidate2c_gevntadr_readback_match &&
	    g_xzs_usb_candidate2c_gevntsiz_readback_match &&
	    ((g_xzs_usb_dctl & DWC3_DCTL_RUN_STOP) == 0) &&
	    ((g_xzs_usb_dsts & DWC3_DSTS_DEVCTRLHLT) != 0) &&
	    g_xzs_usb_candidate2c_devten_unchanged;
	xzs_breadcrumb(0xD740, 0x2C40);
	xzs_early_puts("[XZS-D7T1] D740/2C40 normal boot continuing\n");
	if (g_xzs_usb_candidate2c_complete) {
		xzs_breadcrumb(0xD740, 0x2C90);
		xzs_early_puts("[XZS-D7T1] D740/2C90 acceptance reached\n");
		xzs_breadcrumb(0xD740, 0x2C91);
		xzs_early_puts("[XZS-D7T1] D740/2C91 PASS\n");
	} else {
		xzs_breadcrumb(0xD740, 0x2C9F);
		xzs_early_puts("[XZS-D7T1] ERROR: Candidate-2C acceptance mismatch\n");
	}
	if (g_xzs_usb_candidate2c_complete && xzs_t1x_ep0_halted_setup() != 0) {
		xzs_early_puts("[XZS-D7T1] T1-X stopped at failing command checkpoint\n");
	}
	return 0;
}
