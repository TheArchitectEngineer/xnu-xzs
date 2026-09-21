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
volatile uint32_t g_xzs_usb_dsts = 0;
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

/* Event Buffer: 64 entries (256 bytes) */
#define DWC3_EVENT_BUF_SIZE   256
static uint32_t s_event_buffer[DWC3_EVENT_BUF_SIZE / sizeof(uint32_t)] __attribute__((aligned(64)));
static uint32_t s_event_buf_pos = 0;

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

	dwc3_write32(DWC3_DEPCMDPAR0(DWC3_PHYS_EP_CTRL_OUT), (uint32_t)pa_trb);
	dwc3_write32(DWC3_DEPCMDPAR1(DWC3_PHYS_EP_CTRL_OUT), (uint32_t)(pa_trb >> 32));
	dwc3_write32(DWC3_DEPCMDPAR2(DWC3_PHYS_EP_CTRL_OUT), 0);
	dwc3_write32(DWC3_DEPCMD(DWC3_PHYS_EP_CTRL_OUT), DEPCMD_STARTTRANSFER | DEPCMD_CMDACT);

	for (int i = 0; i < 1000; i++) {
		xzs_watchdog_pet();
		uint32_t reg = dwc3_read32(DWC3_DEPCMD(DWC3_PHYS_EP_CTRL_OUT));
		if (!(reg & DEPCMD_CMDACT)) {
			s_ep0_out_rsc_idx = (uint8_t)((reg >> 16) & 0x7F);
			break;
		}
		delay(10);
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

	dwc3_write32(DWC3_DEPCMDPAR0(DWC3_PHYS_EP_CTRL_IN), (uint32_t)pa_trb);
	dwc3_write32(DWC3_DEPCMDPAR1(DWC3_PHYS_EP_CTRL_IN), (uint32_t)(pa_trb >> 32));
	dwc3_write32(DWC3_DEPCMD(DWC3_PHYS_EP_CTRL_IN), DEPCMD_STARTTRANSFER | DEPCMD_CMDACT);

	uint32_t reg = dwc3_read32(DWC3_DEPCMD(DWC3_PHYS_EP_CTRL_IN));
	s_ep0_in_rsc_idx = (uint8_t)((reg >> 16) & 0x7F);
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

	dwc3_write32(DWC3_DEPCMDPAR0(epnum), (uint32_t)pa_trb);
	dwc3_write32(DWC3_DEPCMDPAR1(epnum), (uint32_t)(pa_trb >> 32));
	dwc3_write32(DWC3_DEPCMD(epnum), DEPCMD_STARTTRANSFER | DEPCMD_CMDACT);
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

	dwc3_write32(DWC3_DEPCMDPAR0(DWC3_PHYS_EP_BULK_OUT), (uint32_t)pa_trb);
	dwc3_write32(DWC3_DEPCMDPAR1(DWC3_PHYS_EP_BULK_OUT), (uint32_t)(pa_trb >> 32));
	dwc3_write32(DWC3_DEPCMD(DWC3_PHYS_EP_BULK_OUT), DEPCMD_STARTTRANSFER | DEPCMD_CMDACT);

	uint32_t reg = dwc3_read32(DWC3_DEPCMD(DWC3_PHYS_EP_BULK_OUT));
	s_bulk_out_rsc_idx = (uint8_t)((reg >> 16) & 0x7F);
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

	dwc3_write32(DWC3_DEPCMDPAR0(DWC3_PHYS_EP_BULK_IN), (uint32_t)pa_trb);
	dwc3_write32(DWC3_DEPCMDPAR1(DWC3_PHYS_EP_BULK_IN), (uint32_t)(pa_trb >> 32));
	dwc3_write32(DWC3_DEPCMD(DWC3_PHYS_EP_BULK_IN), DEPCMD_STARTTRANSFER | DEPCMD_CMDACT);

	uint32_t reg = dwc3_read32(DWC3_DEPCMD(DWC3_PHYS_EP_BULK_IN));
	s_bulk_in_rsc_idx = (uint8_t)((reg >> 16) & 0x7F);
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

/*
 * Primary initialization
 */
int xzs_usb_init(void)
{
	xzs_breadcrumb(0xD740, 0x00);
	xzs_early_puts("[XZS-USB] Initializing MSM8996 DWC3 USB controller\n");

	/* Map MMIO apertures */
	s_dwc3_base = (vm_offset_t)ml_io_map(XZS_USB_DWC3_PHYS_BASE, XZS_USB_DWC3_MMIO_SIZE);
	s_qcom_glue_base = (vm_offset_t)ml_io_map(XZS_USB_QCOM_GLUE_PHYS_BASE, XZS_USB_QCOM_GLUE_MMIO_SIZE);
	s_qusb2_phy_base = (vm_offset_t)ml_io_map(XZS_USB_QUSB2_PHY_PHYS_BASE, XZS_USB_QUSB2_PHY_MMIO_SIZE);

	if (s_dwc3_base == 0) {
		xzs_early_puts("[XZS-USB] ERROR: Failed to map DWC3 MMIO base\n");
		return -1;
	}

	/* Verify Synopsys ID */
	g_xzs_usb_gsnpsid = dwc3_read32(DWC3_GSNPSID);
	xzs_early_puts("DWC3_GSNPSID=0x");
	xzs_d6m4_put_hex64(g_xzs_usb_gsnpsid);
	xzs_early_puts("\n");

	if ((g_xzs_usb_gsnpsid & 0xFFFF0000) != 0x55330000) {
		xzs_early_puts("[XZS-USB] WARNING: Unexpected GSNPSID\n");
	} else {
		xzs_breadcrumb(0xD740, 0x10);
		xzs_early_puts("[XZS-USB] D740/10 DWC3 MMIO alive and identity verified\n");
	}

	/* Allocate deferred thread call for safe TTY injection */
	if (s_usb_rx_tty_call == NULL) {
		s_usb_rx_tty_call = thread_call_allocate(xzs_usb_rx_tty_deferred, NULL);
	}

	/* Configure Device Mode in GCTL: PRTCAPDIR = 2 (device) */
	uint32_t gctl = dwc3_read32(DWC3_GCTL);
	gctl &= ~(3u << 12);
	gctl |= (2u << 12);
	dwc3_write32(DWC3_GCTL, gctl);

	/* Configure High-Speed 480 Mbps in DCFG: DEVSPD = 0 */
	uint32_t dcfg = dwc3_read32(DWC3_DCFG);
	dcfg &= ~0x07;       /* High-Speed */
	dcfg |= (16 << 12);  /* NUMP = 16 */
	dwc3_write32(DWC3_DCFG, dcfg);

	xzs_breadcrumb(0xD740, 0x11);
	xzs_early_puts("[XZS-USB] D740/11 DWC3 device mode configured\n");

	/* Configure Event Buffer 0 */
	vm_offset_t pa_event = ml_vtophys((vm_offset_t)s_event_buffer);
	memset(s_event_buffer, 0, sizeof(s_event_buffer));
	flush_dcache((vm_offset_t)s_event_buffer, sizeof(s_event_buffer), FALSE);

	dwc3_write32(DWC3_GEVNTADR0, (uint32_t)pa_event);
	dwc3_write32(DWC3_GEVNTADR_HI0, (uint32_t)(pa_event >> 32));
	dwc3_write32(DWC3_GEVNTSIZ0, sizeof(s_event_buffer));
	dwc3_write32(DWC3_GEVNTCNT0, 0);

	xzs_early_puts("[XZS-USB] EVENT_BUFFER_PA=0x");
	xzs_d6m4_put_hex64((uint64_t)pa_event);
	xzs_early_puts("\n");

	/* Enable Device Events in DEVTEN: Disconnect, Reset, ConnectDone */
	dwc3_write32(DWC3_DEVTEN, (1u << 0) | (1u << 1) | (1u << 2));

	/* Start Device Controller: RUN_STOP = 1 in DCTL */
	uint32_t dctl = dwc3_read32(DWC3_DCTL);
	dctl |= (1u << 31);
	dwc3_write32(DWC3_DCTL, dctl);
	xzs_early_puts("[XZS-USB] DCTL RUN_STOP=1 written\n");

	delay(2000);

	/* Configure Endpoints */
	xzs_breadcrumb(0xD740, 0x20);
	xzs_early_puts("[XZS-USB] D740/20 Configuring EP0 endpoints\n");
	dwc3_configure_endpoints();

	xzs_early_puts("[XZS-USB] DWC3 device running, waiting for host enumeration\n");
	return 0;
}
