/*
 * Sony Xperia XZs (Keyaki / MSM8996 v3.0)
 * D8-M4: DSI0 Host Controller Bring-Up State Machine & HAL
 */

#ifndef _XZS_D8M4_H_
#define _XZS_D8M4_H_

#include <stdint.h>

extern void xzs_diag_emit(const char *msg);
extern uint64_t g_xzs_ttbr0;

static inline void
xzs_d8m4_hex32(uint32_t val)
{
	char str[9];
	static const char hex[] = "0123456789abcdef";
	for (int h = 7; h >= 0; h--) {
		str[7 - h] = hex[(val >> (h * 4)) & 0xf];
	}
	str[8] = '\0';
	xzs_diag_emit(str);
}

static inline uint32_t
d8m4_read32(uint32_t phys)
{
	uint64_t saved = 0;
	__asm__ volatile("mrs %0, TTBR0_EL1" : "=r"(saved));
	if (g_xzs_ttbr0 != 0) {
		__asm__ volatile("msr TTBR0_EL1, %0; isb sy" :: "r"(g_xzs_ttbr0) : "memory");
	}
	uint32_t val = *(volatile uint32_t *)(uintptr_t)phys;
	if (g_xzs_ttbr0 != 0) {
		__asm__ volatile("msr TTBR0_EL1, %0; isb sy" :: "r"(saved) : "memory");
	}
	return val;
}

static inline void
d8m4_write32(uint32_t phys, uint32_t val)
{
	uint64_t saved = 0;
	__asm__ volatile("mrs %0, TTBR0_EL1" : "=r"(saved));
	if (g_xzs_ttbr0 != 0) {
		__asm__ volatile("msr TTBR0_EL1, %0; isb sy" :: "r"(g_xzs_ttbr0) : "memory");
	}
	*(volatile uint32_t *)(uintptr_t)phys = val;
	__asm__ volatile("dsb sy" ::: "memory");
	if (g_xzs_ttbr0 != 0) {
		__asm__ volatile("msr TTBR0_EL1, %0; isb sy" :: "r"(saved) : "memory");
	}
}

static inline void
xzs_d8m4_dump_status(void)
{
	xzs_diag_emit("[D8-M4] DSI0 HOST STATUS:\n");
	static const uint32_t host_regs[] = {
		0x00994000u, /* DSI_HW_VERSION */
		0x0099400cu, /* DSI_CTRL_0 */
		0x00994010u, /* DSI_STATUS */
		0x00994014u, /* DSI_FIFO_STATUS */
		0x00994018u, /* DSI_TIMING_CTRL */
		0x009940f0u, /* DSI_CTRL */
		0x00994110u, /* DSI_COMMAND_MODE_MDP_CTRL */
		0x009941b4u, /* DSI_EOT_PACKET_CTRL */
		0x009941b8u, /* DSI_LANE_STATUS */
		0x009941f4u, /* DSI_LANE_CTRL */
		0x009942a0u  /* DSI_T_CLK_PRE_EXTEND */
	};
	for (size_t i = 0; i < sizeof(host_regs)/sizeof(host_regs[0]); i++) {
		uint32_t addr = host_regs[i];
		uint32_t val = d8m4_read32(addr);
		xzs_diag_emit("  0x");
		xzs_d8m4_hex32(addr);
		xzs_diag_emit(" = 0x");
		xzs_d8m4_hex32(val);
		xzs_diag_emit("\n");
	}
}

static inline int
xzs_d8m4_run(int mode)
{
	/* mode: 0 = dryrun (TRACE_ONLY), 1 = basic+lane config, 2 = full host bringup */
	int is_dryrun = (mode == 0);

	xzs_diag_emit("\n========================================\n");
	xzs_diag_emit(is_dryrun ? "[D8-M4] STARTING DSI HOST DRY-RUN\n" :
		(mode == 1 ? "[D8-M4] STARTING DSI HOST BASIC CONFIG (MODE 1)\n" :
		             "[D8-M4] STARTING FULL DSI HOST BRING-UP (MODE 2)\n"));
	xzs_diag_emit("========================================\n");

	/* CHECKPOINT D8M4-10: LOWER_LAYER_VERIFY */
	xzs_diag_emit("[D8-M4] CHECKPOINT D8M4-10 LOWER_LAYER_VERIFY START\n");
	uint32_t pll_stat = d8m4_read32(0x009948ccu);
	if ((pll_stat & 0x21u) != 0x21u) {
		xzs_diag_emit("!!! FAIL: PLL not locked/ready: 0x");
		xzs_d8m4_hex32(pll_stat);
		xzs_diag_emit("\n");
		return -1;
	}
	uint32_t byte0_cbcr = d8m4_read32(0x008c233cu);
	if ((byte0_cbcr & 0x80000001u) != 0x00000001u) {
		xzs_diag_emit("!!! FAIL: BYTE0 clock halted: 0x");
		xzs_d8m4_hex32(byte0_cbcr);
		xzs_diag_emit("\n");
		return -1;
	}
	uint32_t pclk0_cbcr = d8m4_read32(0x008c2314u);
	if ((pclk0_cbcr & 0x80000001u) != 0x00000001u) {
		xzs_diag_emit("!!! FAIL: PCLK0 clock halted: 0x");
		xzs_d8m4_hex32(pclk0_cbcr);
		xzs_diag_emit("\n");
		return -1;
	}
	uint32_t esc0_cbcr = d8m4_read32(0x008c2344u);
	if ((esc0_cbcr & 0x80000001u) != 0x00000001u) {
		xzs_diag_emit("!!! FAIL: ESC0 clock halted: 0x");
		xzs_d8m4_hex32(esc0_cbcr);
		xzs_diag_emit("\n");
		return -1;
	}
	uint32_t dl0_ldo = d8m4_read32(0x00994564u);
	if ((dl0_ldo & 0x1fu) != 0x1du) {
		xzs_diag_emit("!!! FAIL: PHY DL0 LDO not 0x1d: 0x");
		xzs_d8m4_hex32(dl0_ldo);
		xzs_diag_emit("\n");
		return -1;
	}
	xzs_diag_emit("[D8-M4] CHECKPOINT D8M4-10 LOWER_LAYER_VERIFY PASS\n");

	/* CHECKPOINT D8M4-20: TIMING_CONFIG (DSI_TIMING_CTRL = 0x1b, DSI_T_CLK_PRE_EXTEND = 0x2b) */
	xzs_diag_emit("[D8-M4] CHECKPOINT D8M4-20 TIMING_CONFIG START\n");
	if (!is_dryrun) {
		d8m4_write32(0x00994018u, 0x0000001bu);
		d8m4_write32(0x009942a0u, 0x0000002bu);
	}
	uint32_t t_post = is_dryrun ? 0x0000001bu : (d8m4_read32(0x00994018u) & 0x3fu);
	uint32_t t_pre  = is_dryrun ? 0x0000002bu : (d8m4_read32(0x009942a0u) & 0x3fu);
	xzs_diag_emit("  t_clk_post = 0x");
	xzs_d8m4_hex32(t_post);
	xzs_diag_emit(", t_clk_pre = 0x");
	xzs_d8m4_hex32(t_pre);
	xzs_diag_emit("\n");
	if (t_post != 0x1bu || t_pre != 0x2bu) {
		xzs_diag_emit("!!! FAIL: DSI timing configuration mismatch\n");
		return -1;
	}
	xzs_diag_emit("[D8-M4] CHECKPOINT D8M4-20 TIMING_CONFIG PASS\n");

	/* CHECKPOINT D8M4-30: LANE_CONFIG (DSI_LANE_CTRL = 0x03000104) */
	xzs_diag_emit("[D8-M4] CHECKPOINT D8M4-30 LANE_CONFIG START\n");
	if (!is_dryrun) {
		d8m4_write32(0x009941f4u, 0x03000104u);
	}
	uint32_t lane_ctrl = is_dryrun ? 0x03000104u : d8m4_read32(0x009941f4u);
	xzs_diag_emit("  DSI_LANE_CTRL = 0x");
	xzs_d8m4_hex32(lane_ctrl);
	xzs_diag_emit("\n");
	if (lane_ctrl != 0x03000104u) {
		xzs_diag_emit("!!! FAIL: DSI_LANE_CTRL mismatch\n");
		return -1;
	}
	xzs_diag_emit("[D8-M4] CHECKPOINT D8M4-30 LANE_CONFIG PASS\n");

	if (mode == 1) {
		xzs_diag_emit("[D8-M4] RESULT=PASS_STAGE_B_BASIC\n");
		return 0;
	}

	/* CHECKPOINT D8M4-40: COMMAND_MODE_CONFIG */
	xzs_diag_emit("[D8-M4] CHECKPOINT D8M4-40 COMMAND_MODE_CONFIG START\n");
	if (!is_dryrun) {
		d8m4_write32(0x00994110u, 0xa220aa02u);
		d8m4_write32(0x009941b4u, 0x00000024u);
	}
	uint32_t cmd_mdp = is_dryrun ? 0xa220aa02u : d8m4_read32(0x00994110u);
	uint32_t eot_pkt = is_dryrun ? 0x00000024u : d8m4_read32(0x009941b4u);
	xzs_diag_emit("  DSI_COMMAND_MODE_MDP_CTRL = 0x");
	xzs_d8m4_hex32(cmd_mdp);
	xzs_diag_emit(", DSI_EOT_PACKET_CTRL = 0x");
	xzs_d8m4_hex32(eot_pkt);
	xzs_diag_emit("\n");
	if (cmd_mdp != 0xa220aa02u || (eot_pkt & 0xffu) != 0x24u) {
		xzs_diag_emit("!!! FAIL: Command mode configuration mismatch\n");
		return -1;
	}
	xzs_diag_emit("[D8-M4] CHECKPOINT D8M4-40 COMMAND_MODE_CONFIG PASS\n");

	/* CHECKPOINT D8M4-50: HOST_ENABLE (DSI_CTRL = 0x00000001) */
	xzs_diag_emit("[D8-M4] CHECKPOINT D8M4-50 HOST_ENABLE START\n");
	if (!is_dryrun) {
		d8m4_write32(0x009940f0u, 0x00000001u);
	}
	uint32_t dsi_ctrl = is_dryrun ? 0x00000001u : d8m4_read32(0x009940f0u);
	xzs_diag_emit("  DSI_CTRL = 0x");
	xzs_d8m4_hex32(dsi_ctrl);
	xzs_diag_emit("\n");
	if ((dsi_ctrl & 1u) != 1u) {
		xzs_diag_emit("!!! FAIL: DSI_CTRL not enabled\n");
		return -1;
	}
	xzs_diag_emit("[D8-M4] CHECKPOINT D8M4-50 HOST_ENABLE PASS\n");

	/* CHECKPOINT D8M4-60: HOST_ACCEPT */
	xzs_diag_emit("[D8-M4] CHECKPOINT D8M4-60 HOST_ACCEPT START\n");
	uint32_t hw_ver = d8m4_read32(0x00994000u);
	xzs_diag_emit("  DSI_HW_VERSION = 0x");
	xzs_d8m4_hex32(hw_ver);
	xzs_diag_emit("\n");
	if (hw_ver != 0x10040001u) {
		xzs_diag_emit("!!! FAIL: DSI_HW_VERSION unexpected\n");
		return -1;
	}
	uint32_t ctrl0_stat = d8m4_read32(0x0099400cu);
	xzs_diag_emit("  DSI_CTRL_0 = 0x");
	xzs_d8m4_hex32(ctrl0_stat);
	xzs_diag_emit("\n");

	/* Re-verify lower layer stability */
	pll_stat = d8m4_read32(0x009948ccu);
	if ((pll_stat & 0x21u) != 0x21u) {
		xzs_diag_emit("!!! FAIL: PLL unlocked during host enable: 0x");
		xzs_d8m4_hex32(pll_stat);
		xzs_diag_emit("\n");
		return -1;
	}
	xzs_diag_emit("[D8-M4] CHECKPOINT D8M4-60 HOST_ACCEPT PASS\n");

	if (is_dryrun) {
		xzs_diag_emit("[D8-M4] RESULT=PASS_DRYRUN\n");
	} else {
		xzs_diag_emit("[D8-M4] RESULT=PASS_FULL_M4\n");
	}
	return 0;
}

#endif /* _XZS_D8M4_H_ */
