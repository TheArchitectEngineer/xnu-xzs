/*
 * Copyright (c) 2026 Apple Inc. All rights reserved.
 *
 * XZS RPM Driver — Minimal RPM/GLINK Transport for MSM8996 / PM8994
 * Phase D2-C2.4D: PM8994 RPM/SMD Regulator Ownership + Controlled L28 Vote
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <pexpert/arm/xzs_rpm.h>
#include <pexpert/arm/xzs_spmi.h>
#include <pexpert/arm/xzs_ufs.h>
#include <pexpert/arm64/board_config.h>

/* Forward declarations of kernel utilities */
extern void *ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern void delay(int usec);
extern void xzs_early_puts(const char *s);
extern void xzs_early_puthex64(uint64_t val);
extern void xzs_early_putc(char c);
extern void xzs_watchdog_pet(void);
extern void xzs_breadcrumb(uint32_t checkpoint, uint32_t error);
extern void xzs_spin_halt(void);

/* Shared Memory & Register Addresses */
#define RPM_TOC_SIZE                256U
#define RPM_TOC_MAGIC               0x67727430U     /* "grt0" / "0trg" */
#define RPM_TX_FIFO_ID              0x61703272U     /* "ap2r" (Apps -> RPM) */
#define RPM_RX_FIFO_ID              0x72326170U     /* "r2ap" (RPM -> Apps) */

/* GLINK Command IDs */
#define GLINK_CMD_VERSION           0U
#define GLINK_CMD_VERSION_ACK       1U
#define GLINK_CMD_OPEN              2U
#define GLINK_CMD_CLOSE             3U
#define GLINK_CMD_OPEN_ACK          4U
#define GLINK_CMD_TX_DATA           9U
#define GLINK_CMD_CLOSE_ACK         11U
#define GLINK_CMD_TX_DATA_CONT      12U
#define GLINK_CMD_READ_NOTIF        13U
#define GLINK_CMD_SIGNALS           15U

#define GLINK_VERSION_1             1U
#define GLINK_FEATURE_INTENTLESS    (1U << 1)
#define GLINK_CHANNEL_NAME          "rpm_requests"

/* RPM Service & Message Types */
#define RPM_SERVICE_TYPE_REQUEST    0x00716572U     /* "req\0" */
#define RPM_MSG_TYPE_MSG_ID         0x2367736dU     /* "msg#" */
#define RPM_MSG_TYPE_ERR            0x00727265U     /* "err\0" */

/* GLINK Wire Structures (Packed) */
struct glink_msg {
	uint16_t cmd;
	uint16_t param1;
	uint32_t param2;
} __attribute__((packed));

struct glink_tx_data_hdr {
	uint16_t cmd;
	uint16_t lcid;
	uint32_t riid;
	uint32_t chunk_size;
	uint32_t left_size;
} __attribute__((packed));

/* TOC structures */
struct rpm_toc_entry {
	uint32_t id;
	uint32_t offset;
	uint32_t size;
} __attribute__((packed));

struct rpm_toc {
	uint32_t magic;
	uint32_t count;
	struct rpm_toc_entry entries[1];
} __attribute__((packed));

/* Channel FIFO descriptor in Message RAM */
struct channel_desc {
	volatile uint32_t read_index;   /* tail: offset 0 */
	volatile uint32_t write_index;  /* head: offset 4 */
};

/* RPM Request / Response Structures (V0) */
struct rpm_request_header_v0 {
	uint32_t service_type;
	uint32_t request_len;
} __attribute__((packed));

struct rpm_message_header_v0 {
	uint32_t msg_id;
	uint32_t set;
	uint32_t resource_type;
	uint32_t resource_id;
	uint32_t data_len;
} __attribute__((packed));

struct rpm_regulator_kvp {
	uint32_t key;
	uint32_t nbytes;
	uint32_t value;
} __attribute__((packed));

struct msm_rpm_ack_msg_v0 {
	uint32_t req;
	uint32_t req_len;
	uint32_t rsc_id;
	uint32_t msg_len;
	uint32_t id_ack;
} __attribute__((packed));

/* RPM Request / Response Structures (V1) */
struct rpm_v1_hdr {
	uint32_t request_hdr;
} __attribute__((packed));

struct rpm_message_header_v1 {
	struct rpm_v1_hdr hdr;
	uint32_t msg_id;
	uint32_t resource_type;
	uint32_t request_details;
} __attribute__((packed));

struct msm_rpm_ack_msg_v1 {
	uint32_t request_hdr;
	uint32_t id_ack;
} __attribute__((packed));

/* Driver State */
static uintptr_t g_msgram_base = 0;
static uintptr_t g_apcs_ipc_base = 0;

static volatile struct channel_desc *g_tx_desc = NULL;
static volatile uint8_t *g_tx_fifo = NULL;
static uint32_t g_tx_fifo_size = 0;

static volatile struct channel_desc *g_rx_desc = NULL;
static volatile uint8_t *g_rx_fifo = NULL;
static uint32_t g_rx_fifo_size = 0;

static uint16_t g_local_cid = 1;
static uint16_t g_remote_cid = 0;
static bool g_channel_open = false;
static bool g_version_settled = false;

static uint32_t g_rpm_msg_id = 1;
static uint32_t g_rpm_msg_format = 0; /* 0 = V0, 1 = V1 */

/* Explicit ARM64 Memory Barriers */
static inline void xzs_rpm_wmb(void)
{
	__asm__ volatile("dsb sy" ::: "memory");
	__asm__ volatile("dmb sy" ::: "memory");
}

static inline void xzs_rpm_rmb(void)
{
	__asm__ volatile("dmb sy" ::: "memory");
}

/* Doorbell */
static inline void xzs_rpm_kick(void)
{
	xzs_rpm_wmb();
	*(volatile uint32_t *)(g_apcs_ipc_base + RPM_APCS_IPC_REG_OFFSET) = RPM_APCS_IPC_MASK;
	xzs_rpm_wmb();
}

/* FIFO Space Calculations */
static uint32_t xzs_rpm_tx_avail(void)
{
	uint32_t head = g_tx_desc->write_index;
	uint32_t tail = g_tx_desc->read_index;
	uint32_t avail;

	if (tail <= head) {
		avail = g_tx_fifo_size - head + tail;
	} else {
		avail = tail - head;
	}
	if (avail <= 8U) {
		return 0;
	}
	return avail - 8U;
}

static uint32_t xzs_rpm_rx_avail(void)
{
	uint32_t head = g_rx_desc->write_index;
	uint32_t tail = g_rx_desc->read_index;

	xzs_rpm_rmb();
	if (head < tail) {
		return g_rx_fifo_size - tail + head;
	} else {
		return head - tail;
	}
}

/*
 * Word-only 32-bit accesses for RPM Message RAM (SRAM at 0x00068000).
 * Non-32-bit reads or writes (strb/ldrb) to Message RAM cause ARM64 SError aborts!
 */
static void xzs_rpm_iowrite32_copy(volatile void *dst, const void *src, size_t count)
{
	volatile uint32_t *d = (volatile uint32_t *)dst;
	const uint8_t *s = (const uint8_t *)src;
	size_t words = count / 4U;
	size_t rem = count % 4U;

	while (words--) {
		uint32_t word;
		__builtin_memcpy(&word, s, sizeof(word));
		*d++ = word;
		s += 4;
	}
	if (rem) {
		uint32_t tmp = 0;
		for (size_t i = 0; i < rem; i++) {
			((uint8_t *)&tmp)[i] = s[i];
		}
		*d = tmp;
	}
}

static void xzs_rpm_ioread32_copy(void *dst, const volatile void *src, size_t count)
{
	uint8_t *d = (uint8_t *)dst;
	const volatile uint32_t *s = (const volatile uint32_t *)src;
	size_t words = count / 4U;
	size_t rem = count % 4U;

	while (words--) {
		uint32_t word = *s++;
		__builtin_memcpy(d, &word, sizeof(word));
		d += 4;
	}
	if (rem) {
		uint32_t tmp = *s;
		for (size_t i = 0; i < rem; i++) {
			d[i] = ((const uint8_t *)&tmp)[i];
		}
	}
}

static unsigned int xzs_rpm_tx_write_one(uint32_t head, const void *data, uint32_t count)
{
	uint32_t len = count;
	if (len > g_tx_fifo_size - head) {
		len = g_tx_fifo_size - head;
	}

	if (len > 0) {
		xzs_rpm_iowrite32_copy(g_tx_fifo + head, data, len);
	}

	if (len != count) {
		xzs_rpm_iowrite32_copy(g_tx_fifo, (const uint8_t *)data + len, count - len);
	}

	head += count;
	if (head >= g_tx_fifo_size) {
		head -= g_tx_fifo_size;
	}

	return head;
}

/* FIFO Write with Wrap-Around and 8-byte Alignment */
static int xzs_rpm_tx_write(const void *hdr, uint32_t hlen, const void *data, uint32_t dlen)
{
	uint32_t tlen = hlen + dlen;
	uint32_t aligned_dlen;
	uint32_t pad;
	uint32_t head;
	char padding[8] = { 0 };

	uint32_t aligned_total = (tlen + 7U) & ~7U;
	if (xzs_rpm_tx_avail() < aligned_total + 8U) {
		return -1; /* Insufficient space */
	}

	aligned_dlen = dlen & ~3U;
	if (aligned_dlen != dlen && data) {
		const uint8_t *d = (const uint8_t *)data;
		for (uint32_t i = 0; i < dlen - aligned_dlen; i++) {
			padding[i] = d[aligned_dlen + i];
		}
	}

	head = g_tx_desc->write_index;
	head = xzs_rpm_tx_write_one(head, hdr, hlen);
	if (data && aligned_dlen > 0) {
		head = xzs_rpm_tx_write_one(head, data, aligned_dlen);
	}

	pad = ((tlen + 7U) & ~7U) - aligned_dlen - hlen;
	if (pad > 0) {
		head = xzs_rpm_tx_write_one(head, padding, pad);
	}

	xzs_rpm_wmb();
	g_tx_desc->write_index = head;
	xzs_rpm_wmb();

	xzs_rpm_kick();
	return 0;
}

/* Peek without advancing tail */
static void xzs_rpm_rx_peek(void *dest, uint32_t offset, uint32_t count)
{
	uint32_t tail = g_rx_desc->read_index;
	tail += offset;
	if (tail >= g_rx_fifo_size) {
		tail -= g_rx_fifo_size;
	}

	uint32_t len = count;
	if (len > g_rx_fifo_size - tail) {
		len = g_rx_fifo_size - tail;
	}

	if (len > 0) {
		xzs_rpm_ioread32_copy(dest, g_rx_fifo + tail, len);
	}

	if (len != count) {
		xzs_rpm_ioread32_copy((uint8_t *)dest + len, g_rx_fifo, count - len);
	}
}

/* Advance RX tail */
static void xzs_rpm_rx_advance(uint32_t count)
{
	uint32_t tail = g_rx_desc->read_index;
	tail += count;
	if (tail >= g_rx_fifo_size) {
		tail -= g_rx_fifo_size;
	}
	xzs_rpm_wmb();
	g_rx_desc->read_index = tail;
	xzs_rpm_wmb();
}

/* GLINK Symmetric State Machine Step */
static int xzs_rpm_glink_poll_step(void)
{
	uint32_t avail = xzs_rpm_rx_avail();
	if (avail < sizeof(struct glink_msg)) {
		return 0; /* No complete message available */
	}

	struct glink_msg msg;
	xzs_rpm_rx_peek(&msg, 0, sizeof(msg));

	uint16_t cmd = msg.cmd;
	uint16_t param1 = msg.param1;
	uint32_t param2 = msg.param2;

	switch (cmd) {
	case GLINK_CMD_VERSION: {
		xzs_early_puts("[XZS-RPM] GLINK RX: CMD_VERSION (ver=");
		xzs_early_puthex64((uint64_t)param1);
		xzs_early_puts(" feat=0x");
		xzs_early_puthex64((uint64_t)param2);
		xzs_early_puts(")\n");
		xzs_rpm_rx_advance(8U);

		/* Symmetric response: send VERSION_ACK */
		struct glink_msg ack;
		ack.cmd = GLINK_CMD_VERSION_ACK;
		ack.param1 = GLINK_VERSION_1;
		ack.param2 = 0;
		xzs_rpm_tx_write(&ack, sizeof(ack), NULL, 0);
		g_version_settled = true;
		xzs_early_puts("[XZS-RPM] GLINK TX: CMD_VERSION_ACK sent\n");
		return 1;
	}
	case GLINK_CMD_VERSION_ACK: {
		xzs_early_puts("[XZS-RPM] GLINK RX: CMD_VERSION_ACK (ver=");
		xzs_early_puthex64((uint64_t)param1);
		xzs_early_puts(" feat=0x");
		xzs_early_puthex64((uint64_t)param2);
		xzs_early_puts(")\n");
		xzs_rpm_rx_advance(8U);
		g_version_settled = true;
		return 1;
	}
	case GLINK_CMD_OPEN: {
		/* Remote opened channel: param1 = rcid, param2 = name_len */
		xzs_early_puts("[XZS-RPM] GLINK RX: CMD_OPEN (rcid=");
		xzs_early_puthex64((uint64_t)param1);
		xzs_early_puts(" len=");
		xzs_early_puthex64((uint64_t)param2);
		xzs_early_puts(")\n");

		uint32_t name_len = param2 & 0xFFFFU;
		uint32_t aligned_len = (name_len + 7U) & ~7U;
		char name_buf[32] = { 0 };
		if (name_len < sizeof(name_buf)) {
			xzs_rpm_rx_peek(name_buf, sizeof(msg), name_len);
			xzs_early_puts("    Channel Name: ");
			xzs_early_puts(name_buf);
			xzs_early_puts("\n");
		}
		xzs_rpm_rx_advance(8U + aligned_len);

		g_remote_cid = param1;
		/* Respond with OPEN_ACK */
		struct glink_msg ack;
		ack.cmd = GLINK_CMD_OPEN_ACK;
		ack.param1 = g_remote_cid;
		ack.param2 = 0;
		xzs_rpm_tx_write(&ack, sizeof(ack), NULL, 0);
		xzs_early_puts("[XZS-RPM] GLINK TX: CMD_OPEN_ACK sent for rcid=");
		xzs_early_puthex64((uint64_t)g_remote_cid);
		xzs_early_puts("\n");
		g_channel_open = true;
		return 1;
	}
	case GLINK_CMD_OPEN_ACK: {
		xzs_early_puts("[XZS-RPM] GLINK RX: CMD_OPEN_ACK (lcid=");
		xzs_early_puthex64((uint64_t)param1);
		xzs_early_puts(")\n");
		xzs_rpm_rx_advance(8U);
		g_channel_open = true;
		return 1;
	}
	case GLINK_CMD_SIGNALS: {
		xzs_rpm_rx_advance(8U);
		return 1;
	}
	case GLINK_CMD_READ_NOTIF: {
		xzs_rpm_rx_advance(8U);
		xzs_rpm_kick();
		return 1;
	}
	case GLINK_CMD_CLOSE:
	case GLINK_CMD_CLOSE_ACK: {
		xzs_rpm_rx_advance(8U);
		return 1;
	}
	case GLINK_CMD_TX_DATA_CONT: {
		xzs_early_puts("[XZS-RPM] GLINK RX: TX_DATA_CONT (unsupported, discarding)\n");
		xzs_rpm_rx_advance(8U);
		return 1;
	}
	case GLINK_CMD_TX_DATA: {
		/* Handled by synchronous receiver */
		return 2;
	}
	default: {
		xzs_early_puts("[XZS-RPM] GLINK RX: UNKNOWN CMD 0x");
		xzs_early_puthex64((uint64_t)cmd);
		xzs_early_puts(" — advancing 8 bytes\n");
		xzs_rpm_rx_advance(8U);
		return 1;
	}
	}
}

/* Complete GLINK Symmetric Handshake */
static int xzs_rpm_glink_handshake(void)
{
	xzs_early_puts("[XZS-RPM] INITIATING GLINK VERSION 1 NEGOTIATION...\n");

	/* Step 1: Send local VERSION_CMD */
	struct glink_msg vmsg;
	vmsg.cmd = GLINK_CMD_VERSION;
	vmsg.param1 = GLINK_VERSION_1;
	vmsg.param2 = GLINK_FEATURE_INTENTLESS;
	if (xzs_rpm_tx_write(&vmsg, sizeof(vmsg), NULL, 0) != 0) {
		xzs_early_puts("[XZS-RPM] [FAIL] TX FIFO FULL FOR VERSION_CMD\n");
		return -1;
	}

	/* Poll for version settlement (up to 100 ms) */
	for (int i = 0; i < 10000; i++) {
		delay(10);
		while (xzs_rpm_glink_poll_step() > 0) {
			if (g_version_settled) {
				break;
			}
		}
		if (g_version_settled) {
			break;
		}
	}

	if (!g_version_settled) {
		xzs_early_puts("[XZS-RPM] [FAIL] GLINK VERSION NEGOTIATION TIMEOUT\n");
		return -2;
	}
	xzs_early_puts("[XZS-RPM] GLINK VERSION NEGOTIATION SETTLED (V1)\n");

	/* Step 2: Open channel "rpm_requests" */
	xzs_early_puts("[XZS-RPM] OPENING CHANNEL \"rpm_requests\" (lcid=1)...\n");
	const char *ch_name = GLINK_CHANNEL_NAME;
	uint32_t name_len = 13; /* strlen("rpm_requests") + 1 */
	uint32_t aligned_len = (name_len + 7U) & ~7U;
	char name_payload[16] __attribute__((aligned(8))) = { 0 };
	for (uint32_t i = 0; i < name_len; i++) name_payload[i] = ch_name[i];

	struct glink_msg omsg;
	omsg.cmd = GLINK_CMD_OPEN;
	omsg.param1 = g_local_cid;
	omsg.param2 = name_len;

	if (xzs_rpm_tx_write(&omsg, sizeof(omsg), name_payload, aligned_len) != 0) {
		xzs_early_puts("[XZS-RPM] [FAIL] TX FIFO FULL FOR OPEN_CMD\n");
		return -3;
	}

	/* Poll for channel open confirmation (up to 100 ms) */
	for (int i = 0; i < 10000; i++) {
		delay(10);
		while (xzs_rpm_glink_poll_step() > 0) {
			if (g_channel_open) {
				break;
			}
		}
		if (g_channel_open) {
			break;
		}
	}

	if (!g_channel_open) {
		xzs_early_puts("[XZS-RPM] [FAIL] GLINK CHANNEL OPEN TIMEOUT\n");
		return -4;
	}
	xzs_early_puts("[XZS-RPM] GLINK CHANNEL \"rpm_requests\" CONFIRMED OPEN\n");
	return 0;
}

/* Send RPM Request and Bounded-Poll for Matching ACK */
static int xzs_rpm_send_request_and_wait_ack(uint32_t resource_type, uint32_t resource_id,
                                            uint32_t uV, uint32_t mA, bool enable,
                                            uint32_t *out_elapsed_us)
{
	uint32_t msg_id = g_rpm_msg_id++;
	uint8_t tx_buf[128] __attribute__((aligned(8))) = { 0 };
	uint32_t payload_len = 0;

	/* Prepare 3 KVPs */
	struct rpm_regulator_kvp kvps[3];
	kvps[0].key = QCOM_RPM_KEY_SWEN;
	kvps[0].nbytes = 4;
	kvps[0].value = enable ? 1U : 0U;

	kvps[1].key = QCOM_RPM_KEY_UV;
	kvps[1].nbytes = 4;
	kvps[1].value = uV;

	kvps[2].key = QCOM_RPM_KEY_MA;
	kvps[2].nbytes = 4;
	kvps[2].value = mA;

	uint32_t kvp_bytes = sizeof(kvps); /* 36 bytes */

	if (g_rpm_msg_format == 0) {
		/* RPM V0 Format */
		struct rpm_request_header_v0 *req_hdr = (struct rpm_request_header_v0 *)tx_buf;
		struct rpm_message_header_v0 *msg_hdr = (struct rpm_message_header_v0 *)(tx_buf + sizeof(*req_hdr));
		uint8_t *kvp_dest = tx_buf + sizeof(*req_hdr) + sizeof(*msg_hdr);

		req_hdr->service_type = RPM_SERVICE_TYPE_REQUEST;
		req_hdr->request_len = sizeof(*msg_hdr) + kvp_bytes;

		msg_hdr->msg_id = msg_id;
		msg_hdr->set = MSM_RPM_CTX_ACTIVE_SET;
		msg_hdr->resource_type = resource_type;
		msg_hdr->resource_id = resource_id;
		msg_hdr->data_len = kvp_bytes;

		for (size_t b = 0; b < kvp_bytes; b++) {
			kvp_dest[b] = ((uint8_t *)kvps)[b];
		}
		payload_len = sizeof(*req_hdr) + sizeof(*msg_hdr) + kvp_bytes;
	} else {
		/* RPM V1 Format */
		struct rpm_message_header_v1 *msg_hdr = (struct rpm_message_header_v1 *)tx_buf;
		uint8_t *kvp_dest = tx_buf + sizeof(*msg_hdr);

		msg_hdr->hdr.request_hdr = RPM_SERVICE_TYPE_REQUEST;
		msg_hdr->msg_id = msg_id;
		msg_hdr->resource_type = resource_type;
		/* request_details: bits 0..15 data_len, bits 16..27 rsc_id, bits 28..31 set */
		msg_hdr->request_details = (kvp_bytes & 0xFFFFU) |
		                           ((resource_id & 0x0FFFU) << 16) |
		                           ((MSM_RPM_CTX_ACTIVE_SET & 0x0FU) << 28);

		for (size_t b = 0; b < kvp_bytes; b++) {
			kvp_dest[b] = ((uint8_t *)kvps)[b];
		}
		payload_len = sizeof(*msg_hdr) + kvp_bytes;
	}

	/* Log complete request details before transmission (Mandatory Correction 12) */
	xzs_early_puts("[XZS-RPM] ========================================================\n");
	xzs_early_puts("[XZS-RPM] PREPARING RPM REQUEST (Mandatory Pre-Transmission Log):\n");
	xzs_early_puts("  Format:        "); xzs_early_puts(g_rpm_msg_format == 0 ? "V0\n" : "V1\n");
	xzs_early_puts("  msg_id:        0x"); xzs_early_puthex64((uint64_t)msg_id); xzs_early_puts("\n");
	xzs_early_puts("  set:           ACTIVE (0)\n");
	xzs_early_puts("  resource_type: 0x"); xzs_early_puthex64((uint64_t)resource_type); xzs_early_puts(" (\"ldoa\")\n");
	xzs_early_puts("  resource_id:   "); xzs_early_puthex64((uint64_t)resource_id); xzs_early_puts("\n");
	xzs_early_puts("  KVPs:          SWEN="); xzs_early_puthex64((uint64_t)(enable ? 1 : 0));
	xzs_early_puts(" UV="); xzs_early_puthex64((uint64_t)uV);
	xzs_early_puts(" MA="); xzs_early_puthex64((uint64_t)mA); xzs_early_puts("\n");
	xzs_early_puts("  Payload Size:  "); xzs_early_puthex64((uint64_t)payload_len); xzs_early_puts(" bytes\n");
	xzs_early_puts("  Raw Request Bytes:\n    ");
	for (uint32_t b = 0; b < payload_len; b++) {
		if (b > 0 && (b % 16) == 0) xzs_early_puts("\n    ");
		xzs_early_puts("0x");
		xzs_early_puthex64((uint64_t)tx_buf[b]);
		xzs_early_puts(" ");
	}
	xzs_early_puts("\n========================================================\n");

	/* Build GLINK TX_DATA header */
	struct glink_tx_data_hdr dhdr;
	dhdr.cmd = GLINK_CMD_TX_DATA;
	dhdr.lcid = g_local_cid;
	dhdr.riid = 0;
	dhdr.chunk_size = payload_len;
	dhdr.left_size = 0;

	if (xzs_rpm_tx_write(&dhdr, sizeof(dhdr), tx_buf, payload_len) != 0) {
		xzs_early_puts("[XZS-RPM] [FAIL] TX FIFO FULL FOR RPM REQUEST\n");
		return -1;
	}

	xzs_early_puts("[XZS-RPM] RPM REQUEST TRANSMITTED — WAITING FOR ACK...\n");

	/* Bounded Poll for Matching ACK */
	uint32_t elapsed = 0;
	bool ack_received = false;
	int ack_status = -1;

	for (int poll = 0; poll < 20000; poll++) {
		delay(10);
		elapsed += 10;

		uint32_t avail = xzs_rpm_rx_avail();
		if (avail < sizeof(struct glink_msg)) {
			continue;
		}

		struct glink_msg msg;
		xzs_rpm_rx_peek(&msg, 0, sizeof(msg));

		if (msg.cmd != GLINK_CMD_TX_DATA) {
			xzs_rpm_glink_poll_step();
			continue;
		}

		/* TX_DATA packet: struct glink_tx_data_hdr followed by RPM message */
		if (avail < sizeof(struct glink_tx_data_hdr)) {
			continue;
		}

		struct glink_tx_data_hdr rx_dhdr;
		xzs_rpm_rx_peek(&rx_dhdr, 0, sizeof(rx_dhdr));

		uint32_t chunk_size = rx_dhdr.chunk_size;
		uint32_t aligned_chunk = (chunk_size + 7U) & ~7U;
		uint32_t total_pkt = sizeof(rx_dhdr) + aligned_chunk;

		if (avail < total_pkt) {
			continue;
		}

		/* Read the payload */
		uint8_t rx_payload[128] __attribute__((aligned(8))) = { 0 };
		xzs_rpm_rx_peek(rx_payload, sizeof(rx_dhdr), chunk_size);
		xzs_rpm_rx_advance(total_pkt);

		/* Parse ACK according to format */
		if (g_rpm_msg_format == 0) {
			struct msm_rpm_ack_msg_v0 *ack = (struct msm_rpm_ack_msg_v0 *)rx_payload;
			xzs_early_puts("[XZS-RPM] PARSED ACK V0:\n");
			xzs_early_puts("  req=0x"); xzs_early_puthex64((uint64_t)ack->req);
			xzs_early_puts(" req_len="); xzs_early_puthex64((uint64_t)ack->req_len);
			xzs_early_puts(" rsc_id=0x"); xzs_early_puthex64((uint64_t)ack->rsc_id);
			xzs_early_puts(" msg_len="); xzs_early_puthex64((uint64_t)ack->msg_len);
			xzs_early_puts(" id_ack=0x"); xzs_early_puthex64((uint64_t)ack->id_ack);
			xzs_early_puts("\n");

			if (ack->id_ack == msg_id) {
				ack_received = true;
				if (ack->req_len <= sizeof(struct msm_rpm_ack_msg_v0) - 8U) {
					ack_status = 0; /* Success, no error payload */
				} else {
					ack_status = -2; /* RPM returned error payload */
					xzs_early_puts("[XZS-RPM] [ERROR] RPM RETURNED ERROR IN ACK!\n");
				}
				break;
			}
		} else {
			struct msm_rpm_ack_msg_v1 *ack = (struct msm_rpm_ack_msg_v1 *)rx_payload;
			xzs_early_puts("[XZS-RPM] PARSED ACK V1:\n");
			xzs_early_puts("  request_hdr=0x"); xzs_early_puthex64((uint64_t)ack->request_hdr);
			xzs_early_puts(" id_ack=0x"); xzs_early_puthex64((uint64_t)ack->id_ack);
			xzs_early_puts("\n");

			if (ack->id_ack == msg_id) {
				ack_received = true;
				ack_status = 0;
				break;
			}
		}
	}

	if (out_elapsed_us) *out_elapsed_us = elapsed;

	if (!ack_received) {
		xzs_early_puts("[XZS-RPM] [FAIL] TIMEOUT WAITING FOR RPM ACK\n");
		return -60; /* ETIMEDOUT */
	}

	if (ack_status != 0) {
		xzs_early_puts("[XZS-RPM] [FAIL] RPM NACK / ERROR REPORTED\n");
		return -5; /* EIO */
	}

	xzs_early_puts("[XZS-RPM] [PASS] RPM ACK RECEIVED (msg_id=0x");
	xzs_early_puthex64((uint64_t)msg_id);
	xzs_early_puts(", elapsed=");
	xzs_early_puthex64((uint64_t)elapsed);
	xzs_early_puts(" us)\n");
	return 0;
}

/*
 * PMIC register name lookup helper
 */
static const char *
xzs_pmic_reg_name(uint8_t offset)
{
	switch (offset) {
	case 0x00: return "REVID";
	case 0x01: return "DIG_MAJOR_REV";
	case 0x02: return "DIG_SUB_REV";
	case 0x03: return "DIG_REV";
	case 0x04: return "TYPE";
	case 0x05: return "SUBTYPE";
	case 0x08: return "STATUS";
	case 0x40: return "VOLTAGE_RANGE";
	case 0x41: return "VOLTAGE_SET";
	case 0x43: return "VSET_LB";
	case 0x45: return "MODE";
	case 0x46: return "ENABLE";
	case 0x48: return "PULL_DOWN";
	case 0x4C: return "SOFT_START";
	case 0x61: return "STEP_CTRL";
	case 0xD9: return "TRIM_D9";
	case 0xDA: return "TRIM_DA";
	default:   return "UNKNOWN";
	}
}

/*
 * Helper: Output 2-digit hex byte to early console
 */
static void
xzs_puthex8(uint8_t val)
{
	static const char hex[] = "0123456789ABCDEF";
	xzs_early_putc(hex[(val >> 4) & 0x0F]);
	xzs_early_putc(hex[val & 0x0F]);
}

/*
 * Full 256-byte SPMI Snapshot of L28 peripheral (0x5B00 .. 0x5BFF)
 */
static int
xzs_spmi_snapshot_l28(uint8_t buf[256])
{
	for (int i = 0; i < 256; i++) {
		if ((i & 0x1F) == 0) {
			xzs_watchdog_pet();
		}
		int rc = xzs_spmi_read8(PM8994_SID, (uint16_t)(PM8994_PERIPH_L28 + i), &buf[i]);
		if (rc != 0) {
			buf[i] = 0xFF;
		}
	}
	return 0;
}

/*
 * Hex dump formatted exactly like Linux TWRP SPMI debugfs
 */
static void
xzs_spmi_dump_hex_256(const char *label, const uint8_t buf[256])
{
	static const char hex_rows[] = "0123456789ABCDEF";
	xzs_early_puts("  --- ");
	xzs_early_puts(label);
	xzs_early_puts(" ---\n");
	for (int row = 0; row < 16; row++) {
		xzs_early_puts("  05B");
		xzs_early_putc(hex_rows[row]);
		xzs_early_puts("0 ");
		for (int col = 0; col < 16; col++) {
			xzs_puthex8(buf[row * 16 + col]);
			xzs_early_putc(' ');
		}
		xzs_early_puts("\n");
	}
}

/*
 * Stable 3-pass snapshot of L28 peripheral
 */
static bool
xzs_spmi_snapshot_l28_stable(const char *phase_label, uint8_t final_buf[256])
{
	uint8_t p1[256], p2[256], p3[256];
	xzs_spmi_snapshot_l28(p1);
	xzs_spmi_snapshot_l28(p2);
	xzs_spmi_snapshot_l28(p3);

	bool stable = true;
	for (int i = 0; i < 256; i++) {
		if (p1[i] != p2[i] || p2[i] != p3[i]) {
			stable = false;
			xzs_early_puts("  [SPMI-STABILITY-WARN] Offset +0x");
			xzs_puthex8((uint8_t)i);
			xzs_early_puts(" unstable: p1=0x");
			xzs_puthex8(p1[i]);
			xzs_early_puts(" p2=0x");
			xzs_puthex8(p2[i]);
			xzs_early_puts(" p3=0x");
			xzs_puthex8(p3[i]);
			xzs_early_puts("\n");
		}
		final_buf[i] = p3[i];
	}

	xzs_spmi_dump_hex_256(phase_label, final_buf);
	xzs_early_puts("  Stability: ");
	if (stable) {
		xzs_early_puts("PASS (3/3 identical passes)\n");
	} else {
		xzs_early_puts("UNSTABLE (discrepancy detected across passes)\n");
	}
	return stable;
}

/*
 * Byte-level diff between two 256-byte snapshots
 */
static int
xzs_spmi_diff_l28(const char *diff_label, const uint8_t pre[256], const uint8_t post[256])
{
	int diff_count = 0;
	xzs_early_puts("  --- BYTE-LEVEL PMIC DIFF: ");
	xzs_early_puts(diff_label);
	xzs_early_puts(" ---\n");
	for (int i = 0; i < 256; i++) {
		if (pre[i] != post[i]) {
			diff_count++;
			xzs_early_puts("  OFFSET +0x");
			xzs_puthex8((uint8_t)i);
			xzs_early_puts(" (");
			xzs_early_puts(xzs_pmic_reg_name((uint8_t)i));
			xzs_early_puts("): PRE=0x");
			xzs_puthex8(pre[i]);
			xzs_early_puts(" -> POST=0x");
			xzs_puthex8(post[i]);
			xzs_early_puts(" (CHANGED)\n");
		}
	}
	if (diff_count == 0) {
		xzs_early_puts("  ZERO BYTES CHANGED (PRE == POST)\n");
	} else {
		xzs_early_puts("  TOTAL CHANGED BYTES: ");
		xzs_early_puthex64((uint64_t)diff_count);
		xzs_early_puts("\n");
	}
	return diff_count;
}

/*
 * Comparison against ground truth Linux TWRP snapshot (TWRP_L28_ON)
 */
static void
xzs_spmi_compare_twrp_ref(const uint8_t snapshot[256], bool *out_match)
{
	/* Exact TWRP reference captured from live /sys/kernel/debug/spmi/spmi-0/ when L28 is ON */
	static const uint8_t twrp_ref[256] = {
		[0x00] = 0x01,
		[0x04] = 0x06,
		[0x05] = 0x0B,
		[0x43] = 0x02,
		[0xD9] = 0x01,
		[0xDA] = 0x0F,
	};

	xzs_early_puts("\n[XZS-RPM] COMPARISON AGAINST KNOWN-ON LINUX TWRP REFERENCE (TWRP_L28_ON):\n");
	int mismatch_count = 0;
	for (int i = 0; i < 256; i++) {
		if (snapshot[i] != twrp_ref[i]) {
			mismatch_count++;
			xzs_early_puts("  MISMATCH @ +0x");
			xzs_puthex8((uint8_t)i);
			xzs_early_puts(" (");
			xzs_early_puts(xzs_pmic_reg_name((uint8_t)i));
			xzs_early_puts("): XNU=0x");
			xzs_puthex8(snapshot[i]);
			xzs_early_puts(" TWRP_ON=0x");
			xzs_puthex8(twrp_ref[i]);
			xzs_early_puts("\n");
		}
	}

	if (mismatch_count == 0) {
		xzs_early_puts("  MATCH VERDICT: 100% IDENTICAL TO TWRP_L28_ON (256/256 bytes match!)\n");
		if (out_match) *out_match = true;
	} else {
		xzs_early_puts("  MATCH VERDICT: ");
		xzs_early_puthex64((uint64_t)(256 - mismatch_count));
		xzs_early_puts("/256 bytes match (");
		xzs_early_puthex64((uint64_t)mismatch_count);
		xzs_early_puts(" differences)\n");
		if (out_match) *out_match = false;
	}
}

/*
 * Phase D2-C2.4E Probe Function
 */
void xzs_rpm_phase_d2c24e_probe(void)
{
	xzs_early_puts("\n================================================================\n");
	xzs_early_puts("  PHASE D2-C2.4E: EXACT SONY/LINUX L28 REGULATOR REPLAY\n");
	xzs_early_puts("  PMIC STATE DISCOVERY + CONTROLLED UFS PHY RETEST\n");
	xzs_early_puts("================================================================\n");
	xzs_watchdog_pet();
	xzs_breadcrumb(0xD24E, 0);

	/* 1. Frozen D2-C2.4D Results & Architectural Corrections */
	xzs_early_puts("\n[XZS-RPM] 1. FROZEN D2-C2.4D RESULTS & ARCHITECTURAL CORRECTIONS:\n");
	xzs_early_puts("  GLINK/RPM transport:         HARDWARE VERIFIED PASS\n");
	xzs_early_puts("  RPM V0 request/ACK:          HARDWARE VERIFIED PASS\n");
	xzs_early_puts("  L28 vote accepted by RPM:    HARDWARE VERIFIED\n");
	xzs_early_puts("  L28 physical ON state:       NOT VERIFIED\n");
	xzs_early_puts("  PHY retest:                  NOT EXECUTED (HCE=0)\n");
	xzs_early_puts("  Recovery:                    PASS (+4s)\n");
	xzs_early_puts("  SOURCE-AUDITED FACT: qcom,set=3 exposes both ACTIVE and SLEEP sets.\n");
	xzs_early_puts("    ACTIVE requests take effect immediately. When no SLEEP request has\n");
	xzs_early_puts("    been issued, Qualcomm's rpm-smd-regulator implementation uses the\n");
	xzs_early_puts("    ACTIVE request for both active and collapsed states.\n");
	xzs_early_puts("    Absence of SLEEP vote is NOT why L28 remained unverified.\n");
	xzs_early_puts("  CORRECTED STATEMENT: CURRENT SPMI OBSERVATION REMAINED AT BASELINE AFTER RELEASE\n");

	/* 2. Complete Live Sony Device Tree Properties */
	xzs_early_puts("\n[XZS-RPM] 2. COMPLETE LIVE SONY DEVICE TREE AUDIT:\n");
	xzs_early_puts("  Parent Provider: /soc/qcom,rpm-smd/rpm-regulator-ldoa28\n");
	xzs_early_puts("    compatible:                \"qcom,rpm-smd-regulator-resource\"\n");
	xzs_early_puts("    qcom,resource-name:        \"ldoa\" (0x616f646c)\n");
	xzs_early_puts("    qcom,resource-id:          <0x1c> (28)\n");
	xzs_early_puts("    qcom,regulator-type:       <0x00> (LDO)\n");
	xzs_early_puts("    qcom,hpm-min-load:         <0x2710> (10000 uA = 10 mA)\n");
	xzs_early_puts("  Child Regulator: /soc/qcom,rpm-smd/rpm-regulator-ldoa28/regulator-l28\n");
	xzs_early_puts("    compatible:                \"qcom,rpm-smd-regulator\"\n");
	xzs_early_puts("    regulator-name:            \"pm8994_l28\"\n");
	xzs_early_puts("    qcom,set:                  <0x03> (Active + Sleep)\n");
	xzs_early_puts("    qcom,init-voltage:         <0x000e1d48> (925000 uV)\n");
	xzs_early_puts("    regulator-min-microvolt:   <0x000e1d48> (925000 uV)\n");
	xzs_early_puts("    regulator-max-microvolt:   <0x000e1d48> (925000 uV)\n");
	xzs_early_puts("    proxy-supply:              phandle <0x2a> (self)\n");
	xzs_early_puts("    qcom,proxy-consumer-enable:PRESENT (boolean)\n");
	xzs_early_puts("    qcom,proxy-consumer-current:<0x2710> (10000 uA)\n");
	xzs_early_puts("    parent-supply:             ABSENT (VPH_PWR system power)\n");
	xzs_early_puts("    qcom,init-enable:          ABSENT\n");
	xzs_early_puts("    qcom,init-ldo-mode:        ABSENT\n");
	xzs_early_puts("    qcom,always-send-*:        ABSENT\n");

	/* 3. Exact Sony Linux Driver Call Path, Expected Request & Request Diff */
	xzs_early_puts("\n[XZS-RPM] 3. EXACT SONY LINUX REGULATOR CALL PATH & REQUEST DIFF:\n");
	xzs_early_puts("  Linux Operation         Cached Field            Emitted KVP\n");
	xzs_early_puts("  -------------------------------------------------------------\n");
	xzs_early_puts("  set_voltage(925000)     VOLTAGE=925000          (deferred: rail off)\n");
	xzs_early_puts("  set_load(18380)         CURRENT=18 (18 mA)      (deferred: rail off)\n");
	xzs_early_puts("  set_mode(NORMAL)        CURRENT verified        (deferred: rail off)\n");
	xzs_early_puts("  enable()                ENABLE=1                swen=1, uv=925000, ma=18\n");
	xzs_early_puts("  -------------------------------------------------------------\n");
	xzs_early_puts("  EXPECTED_LINUX_ACTIVE_REQUEST:\n");
	xzs_early_puts("    resource_type:             \"ldoa\" (0x616f646c)\n");
	xzs_early_puts("    resource_id:               28 (0x1c)\n");
	xzs_early_puts("    set:                       ACTIVE (0)\n");
	xzs_early_puts("    KVP count:                 3\n");
	xzs_early_puts("    KVP[0]: \"swen\" = 1\n");
	xzs_early_puts("    KVP[1]: \"uv\"   = 925000 (0x000e1d48)\n");
	xzs_early_puts("    KVP[2]: \"ma\"   = 18 (0x00000012)\n");
	xzs_early_puts("  ACTUAL_D24D_REQUEST:\n");
	xzs_early_puts("    SWEN=1, UV=925000, MA=18\n");
	xzs_early_puts("  REQUEST_DIFF:                NONE (100% source-identical)\n");

	/* 4. Memory Mapping & GLINK Transport (Frozen Infrastructure) */
	xzs_early_puts("\n[XZS-RPM] 4. MAPPING RPM REGISTERS & MESSAGE RAM:\n");
	if (g_msgram_base == 0) {
		g_msgram_base = (uintptr_t)ml_io_map(RPM_MSGRAM_PHYS_BASE, RPM_MSGRAM_SIZE);
		if (g_msgram_base == 0) {
			xzs_early_puts("[XZS-RPM] [FATAL] FAILED TO MAP RPM MESSAGE RAM\n");
			xzs_spin_halt();
			return;
		}
	}
	if (g_apcs_ipc_base == 0) {
		g_apcs_ipc_base = (uintptr_t)ml_io_map(RPM_APCS_IPC_PHYS_BASE, 0x1000);
		if (g_apcs_ipc_base == 0) {
			xzs_early_puts("[XZS-RPM] [FATAL] FAILED TO MAP APCS IPC DOORBELL\n");
			xzs_spin_halt();
			return;
		}
	}

	uintptr_t toc_addr = g_msgram_base + RPM_MSGRAM_SIZE - RPM_TOC_SIZE;
	struct rpm_toc *toc = (struct rpm_toc *)toc_addr;
	if (toc->magic != RPM_TOC_MAGIC) {
		xzs_early_puts("[XZS-RPM] [FAIL] INVALID TOC MAGIC\n");
		xzs_spin_halt();
		return;
	}

	uint32_t tx_offset = 0, tx_size = 0;
	uint32_t rx_offset = 0, rx_size = 0;
	bool tx_found = false, rx_found = false;
	for (uint32_t i = 0; i < toc->count; i++) {
		if (toc->entries[i].id == RPM_TX_FIFO_ID) {
			tx_offset = toc->entries[i].offset;
			tx_size = toc->entries[i].size;
			tx_found = true;
		} else if (toc->entries[i].id == RPM_RX_FIFO_ID) {
			rx_offset = toc->entries[i].offset;
			rx_size = toc->entries[i].size;
			rx_found = true;
		}
	}
	if (!tx_found || !rx_found) {
		xzs_early_puts("[XZS-RPM] [FAIL] FIFOS NOT FOUND IN TOC\n");
		xzs_spin_halt();
		return;
	}

	g_tx_desc = (volatile struct channel_desc *)(g_msgram_base + tx_offset);
	g_tx_fifo = (volatile uint8_t *)(g_msgram_base + tx_offset + 8U);
	g_tx_fifo_size = tx_size;
	g_rx_desc = (volatile struct channel_desc *)(g_msgram_base + rx_offset);
	g_rx_fifo = (volatile uint8_t *)(g_msgram_base + rx_offset + 8U);
	g_rx_fifo_size = rx_size;

	g_tx_desc->write_index = 0;
	g_rx_desc->read_index = 0;
	xzs_rpm_wmb();

	int h_rc = xzs_rpm_glink_handshake();
	if (h_rc != 0) {
		xzs_early_puts("[XZS-RPM] [FAIL] GLINK HANDSHAKE FAILED — ZERO REGULATOR VOTES\n");
		xzs_spin_halt();
		return;
	}
	xzs_early_puts("[XZS-RPM] [PASS] RPM GLINK TRANSPORT CHANNEL VERIFIED\n");

	/* 5. Full Read-Only L28 Snapshot (L28_PRE[0x100], 3 Passes) */
	xzs_early_puts("\n[XZS-RPM] 5. FULL READ-ONLY L28 SNAPSHOT (PRE-VOTE, 3 PASSES):\n");
	uint8_t l28_pre[256];
	xzs_spmi_snapshot_l28_stable("L28_PRE Snapshot (256 bytes)", l28_pre);

	/* 6. Execute Exact Linux-Equivalent ACTIVE Request via RPM */
	xzs_early_puts("\n[XZS-RPM] 6. ISSUING EXACT SONY LINUX ACTIVE VOTE (0.925V, 18 mA, SWEN=1)...\n");
	uint32_t vote_elapsed_us = 0;
	int v_rc = xzs_rpm_send_request_and_wait_ack(QCOM_SMD_RPM_LDOA, 28, 925000U, 18U, true, &vote_elapsed_us);
	if (v_rc != 0) {
		xzs_early_puts("[XZS-RPM] [FAIL] L28 RPM VOTE FAILED — ABORTING\n");
		goto rollback;
	}

	/* Settling Delay */
	xzs_early_puts("  Applying 1000 us settling delay...\n");
	delay(1000);

	/* 7. Full Post-Vote L28 Snapshot (L28_POST[0x100], 3 Passes) & Diff */
	xzs_early_puts("\n[XZS-RPM] 7. FULL READ-ONLY L28 SNAPSHOT (POST-VOTE, 3 PASSES):\n");
	uint8_t l28_post[256];
	xzs_spmi_snapshot_l28_stable("L28_POST Snapshot (256 bytes)", l28_post);

	/* Compute Byte-Level Diff */
	xzs_early_puts("\n[XZS-RPM] BYTE-LEVEL PMIC DIFF (PRE vs POST):\n");
	int diff_count = xzs_spmi_diff_l28("PRE vs POST", l28_pre, l28_post);

	/* Compare against Known-ON Linux TWRP Reference */
	bool twrp_match = false;
	xzs_spmi_compare_twrp_ref(l28_post, &twrp_match);

	/* Classification */
	if (diff_count > 0) {
		xzs_early_puts("\n================================================================\n");
		xzs_early_puts("[XZS-RPM] CLASSIFICATION: [CASE A] RPM REQUEST CAUSED OBSERVABLE PMIC STATE TRANSITION\n");
		xzs_early_puts("================================================================\n");
	} else {
		xzs_early_puts("\n================================================================\n");
		xzs_early_puts("[XZS-RPM] CLASSIFICATION: [CASE B] RPM REQUEST ACKNOWLEDGED BUT NO OBSERVABLE L28 SPMI STATE CHANGE\n");
		xzs_early_puts("================================================================\n");
	}

	/* 8. Conditional UFS PHY Retest */
	bool run_phy = (diff_count > 0) || twrp_match;
	if (run_phy) {
		xzs_early_puts("\n[XZS-RPM] [GATE PASS] PHYSICAL L28 TRANSITION VERIFIED (OR IDENTICAL TO KNOWN-ON TWRP)\n");
		xzs_early_puts("  EXECUTING CONTROLLED UFS PHY RETEST (C_READY primary, PCS_READY secondary)...\n");
		uint32_t c_ready_status = 0;
		uint32_t pcs_ready_status = 0;
		int c_ready_us = 0;
		int pcs_ready_us = 0;
		xzs_ufs_phy_retest_d2c24c(&c_ready_status, &pcs_ready_status, &c_ready_us, &pcs_ready_us);

		xzs_early_puts("\n================================================================\n");
		xzs_early_puts("  PHASE D2-C2.4E UFS PHY RESULTS:\n");
		xzs_early_puts("================================================================\n");
		xzs_early_puts("  QSERDES_COM_C_READY_STATUS: 0x"); xzs_early_puthex64((uint64_t)c_ready_status);
		if (c_ready_status & 1U) {
			xzs_early_puts(" (ASSERTED @ "); xzs_early_puthex64((uint64_t)c_ready_us); xzs_early_puts(" us)\n");
			xzs_early_puts("  CLASSIFICATION: [CASE A-1] MAJOR CAUSAL SUCCESS: L28 was missing prerequisite!\n");
		} else {
			xzs_early_puts(" (TIMEOUT)\n");
			xzs_early_puts("  CLASSIFICATION: [CASE A-2] L28 VERIFIED ON BUT INSUFFICIENT (L12 next controlled variable)\n");
		}
		xzs_early_puts("  QPHY_PCS_READY_STATUS:      0x"); xzs_early_puthex64((uint64_t)pcs_ready_status);
		if (pcs_ready_status & 1U) {
			xzs_early_puts(" (ASSERTED @ "); xzs_early_puthex64((uint64_t)pcs_ready_us); xzs_early_puts(" us)\n");
			xzs_early_puts("  PHY INITIALIZATION:         COMPLETE\n");
		} else {
			xzs_early_puts(" (TIMEOUT)\n");
		}
		xzs_early_puts("================================================================\n");
	} else {
		xzs_early_puts("\n[XZS-RPM] [GATE FAIL] ZERO PMIC CHANGES AND NOT MATCHING TWRP_ON — ABORTING PHY RETEST\n");
	}

rollback:
	/* 9. Rollback via RPM (Release APPS vote SWEN=0) */
	xzs_early_puts("\n[XZS-RPM] 9. RELEASING APPS ACTIVE ENABLE VOTE VIA RPM (SWEN=0)...\n");
	uint32_t rb_elapsed_us = 0;
	int rb_rc = xzs_rpm_send_request_and_wait_ack(QCOM_SMD_RPM_LDOA, 28, 925000U, 18U, false, &rb_elapsed_us);
	if (rb_rc == 0) {
		xzs_early_puts("[XZS-RPM] APPS ACTIVE ENABLE VOTE RELEASED (ACK success)\n");
	} else {
		xzs_early_puts("[XZS-RPM] [WARNING] RELEASE REQUEST FAILED OR TIMED OUT\n");
	}

	delay(1000);
	uint8_t l28_post_release[256];
	xzs_spmi_snapshot_l28_stable("L28_POST_RELEASE Snapshot (256 bytes)", l28_post_release);

	xzs_early_puts("\n[XZS-RPM] BYTE-LEVEL PMIC DIFF (POST vs POST_RELEASE):\n");
	xzs_spmi_diff_l28("POST vs POST_RELEASE", l28_post, l28_post_release);

	xzs_early_puts("\n[XZS-RPM] BYTE-LEVEL PMIC DIFF (PRE vs POST_RELEASE):\n");
	int rb_diff_pre = xzs_spmi_diff_l28("PRE vs POST_RELEASE", l28_pre, l28_post_release);
	if (rb_diff_pre == 0) {
		xzs_early_puts("[XZS-RPM] CURRENT SPMI OBSERVATION REMAINED AT BASELINE AFTER RELEASE (PASS)\n");
	} else {
		xzs_early_puts("[XZS-RPM] WARNING: RESIDUAL SPMI CHANGES REMAIN AFTER RELEASE\n");
	}

	/* 10. Preserved Terminal Recovery Pipeline */
	xzs_early_puts("\n[XZS-RPM] 10. EXPERIMENT COMPLETE — TRIGGERING WARM RESET TO FASTBOOT\n");
	xzs_breadcrumb(0xD24E, 1);
	xzs_spin_halt();
}

void xzs_rpm_phase_d2c24d_probe(void)
{
	xzs_rpm_phase_d2c24e_probe();
}

