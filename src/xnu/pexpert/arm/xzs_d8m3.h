/*
 * Sony Xperia XZs (Keyaki / MSM8996 v3.0)
 * D8-M3: Guarded Real DSI PLL & PHY Bring-Up State Machine & HAL
 */

#ifndef _XZS_D8M3_H_
#define _XZS_D8M3_H_

#include "xzs_d8m3_data.h"

static int s_m3_attempted = 0;

static int
display_is_allowed(uint32_t addr)
{
	int lo = 0;
	int hi = M3_WRITE_ALLOWLIST_COUNT - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		if (s_m3_write_allowlist[mid] == addr) {
			return 1;
		}
		if (s_m3_write_allowlist[mid] < addr) {
			lo = mid + 1;
		} else {
			hi = mid - 1;
		}
	}
	return 0;
}

static void
xzs_diag_hex32(uint32_t val)
{
	char str[9];
	static const char hex[] = "0123456789abcdef";
	for (int h = 7; h >= 0; h--) {
		str[7 - h] = hex[(val >> (h * 4)) & 0xf];
	}
	str[8] = '\0';
	xzs_diag_emit(str);
}

static void
xzs_d8m3_print_reg(uint32_t addr, uint32_t val)
{
	char line[40];
	static const char hex[] = "0123456789abcdef";
	int i = 0, h;

	line[i++] = '0';
	line[i++] = 'x';
	for (h = 7; h >= 0; h--) {
		line[i++] = hex[(addr >> (h * 4)) & 0xf];
	}
	line[i++] = ' ';
	line[i++] = '=';
	line[i++] = ' ';
	line[i++] = '0';
	line[i++] = 'x';
	for (h = 7; h >= 0; h--) {
		line[i++] = hex[(val >> (h * 4)) & 0xf];
	}
	line[i++] = '\n';
	line[i] = '\0';
	xzs_diag_emit(line);
}

static int
display_write32(uint32_t phys, uint32_t val, int is_dryrun)
{
	if (!display_is_allowed(phys)) {
		xzs_diag_emit("[D8-M3] ERROR: disallowed MMIO write to 0x");
		xzs_diag_hex32(phys);
		xzs_diag_emit("\n");
		return -1;
	}
	if (is_dryrun) {
		return 0;
	}

	uint64_t saved = 0;
	__asm__ volatile("mrs %0, TTBR0_EL1" : "=r"(saved));
	if (g_xzs_ttbr0 != 0) {
		__asm__ volatile("msr TTBR0_EL1, %0; isb sy" :: "r"(g_xzs_ttbr0) : "memory");
	}
	*(volatile uint32_t *)(uintptr_t)phys = val;
	__asm__ volatile("dsb sy" ::: "memory");
	uint32_t rb = *(volatile uint32_t *)(uintptr_t)phys;
	if (g_xzs_ttbr0 != 0) {
		__asm__ volatile("msr TTBR0_EL1, %0; isb sy" :: "r"(saved) : "memory");
	}

	/* Diagnostic readback check for PLL configuration registers */
	if (phys >= 0x00994800u && phys <= 0x00994904u) {
		uint32_t mask = 0xffu;
		if (phys == 0x0099483cu) mask = 0x1fu;
		else if (phys == 0x0099484cu) mask = 0x01u;
		else if (phys == 0x00994870u) mask = 0x03u;
		else if (phys == 0x00994884u) mask = 0x03u;
		else if (phys == 0x00994888u) mask = 0x1fu;
		else if (phys == 0x00994894u) mask = 0x00u;

		if ((rb & mask) != (val & mask)) {
			xzs_diag_emit("[D8-M3] WRITE_MISMATCH addr=0x");
			xzs_diag_hex32(phys);
			xzs_diag_emit(" wrote=0x");
			xzs_diag_hex32(val);
			xzs_diag_emit(" rb=0x");
			xzs_diag_hex32(rb);
			xzs_diag_emit("\n");
		}
	}
	return 0;
}

static int
display_poll32(uint32_t phys, uint32_t mask, uint32_t expected, uint32_t timeout_us, uint32_t *last_val)
{
	uint32_t val = 0;
	uint32_t elapsed = 0;
	while (elapsed <= timeout_us) {
		val = xzs_phys_read32(phys);
		if ((val & mask) == expected) {
			if (last_val) {
				*last_val = val;
			}
			return 0;
		}
		delay(10);
		elapsed += 10;
	}
	if (last_val) {
		*last_val = val;
	}
	return -1;
}

static void
xzs_d8m3_dump_regs(void)
{
	xzs_diag_emit("[D8-M3] DUMPING 35 SNAPSHOT REGISTERS:\n");
	for (int i = 0; i < M3_SNAPSHOT_REGS_COUNT; i++) {
		uint32_t addr = s_m3_snapshot_regs[i];
		uint32_t val = xzs_phys_read32(addr);
		xzs_d8m3_print_reg(addr, val);
	}
	xzs_diag_emit("[D8-M3] REGISTER DUMP COMPLETE\n");
}

static void
xzs_d8m3_dump_pll(void)
{
	uint32_t p = xzs_phys_read32(0x009948cc);
	uint32_t s = xzs_phys_read32(0x00994850);

	xzs_diag_emit("[D8-M3] DSI0 PLL STATUS:\n");
	xzs_d8m3_print_reg(0x009948cc, p);
	xzs_d8m3_print_reg(0x00994850, s);
	xzs_d8m3_print_reg(0x00994800, xzs_phys_read32(0x00994800));
	xzs_d8m3_print_reg(0x0099483c, xzs_phys_read32(0x0099483c));
	xzs_d8m3_print_reg(0x00994840, xzs_phys_read32(0x00994840));
	xzs_d8m3_print_reg(0x0099485c, xzs_phys_read32(0x0099485c));
	xzs_d8m3_print_reg(0x0099486c, xzs_phys_read32(0x0099486c));
	xzs_d8m3_print_reg(0x00994870, xzs_phys_read32(0x00994870));
	xzs_d8m3_print_reg(0x00994874, xzs_phys_read32(0x00994874));
	xzs_d8m3_print_reg(0x00994878, xzs_phys_read32(0x00994878));
	xzs_d8m3_print_reg(0x0099487c, xzs_phys_read32(0x0099487c));
	xzs_d8m3_print_reg(0x00994880, xzs_phys_read32(0x00994880));
	xzs_d8m3_print_reg(0x00994890, xzs_phys_read32(0x00994890));
	xzs_d8m3_print_reg(0x009948b4, xzs_phys_read32(0x009948b4));
	xzs_d8m3_print_reg(0x00994410, xzs_phys_read32(0x00994410));
	xzs_d8m3_print_reg(0x00994414, xzs_phys_read32(0x00994414));
	xzs_d8m3_print_reg(0x00994448, xzs_phys_read32(0x00994448));
	xzs_d8m3_print_reg(0x0099444c, xzs_phys_read32(0x0099444c));
	xzs_diag_emit("[D8-M3] pll_locked=");
	xzs_diag_emit((p & 0x20) ? "1" : "0");
	xzs_diag_emit(" pll_ready=");
	xzs_diag_emit((p & 0x01) ? "1" : "0");
	xzs_diag_emit(" secondary_ready=");
	xzs_diag_emit((s & 0x01) ? "1" : "0");
	xzs_diag_emit(" secondary_locked=");
	xzs_diag_emit((s & 0x20) ? "1" : "0");
	xzs_diag_emit("\n");
}

static void
xzs_d8m3_dump_phy(void)
{
	xzs_diag_emit("[D8-M3] DSI0 PHY STATUS:\n");
	xzs_d8m3_print_reg(0x00994410, xzs_phys_read32(0x00994410));
	xzs_d8m3_print_reg(0x00994440, xzs_phys_read32(0x00994440));
	xzs_d8m3_print_reg(0x00994448, xzs_phys_read32(0x00994448));
	xzs_d8m3_print_reg(0x0099444c, xzs_phys_read32(0x0099444c));
	xzs_d8m3_print_reg(0x009944c0, xzs_phys_read32(0x009944c0));
	xzs_d8m3_print_reg(0x00994540, xzs_phys_read32(0x00994540));
	xzs_d8m3_print_reg(0x00994564, xzs_phys_read32(0x00994564));
	xzs_d8m3_print_reg(0x009945c0, xzs_phys_read32(0x009945c0));
	xzs_d8m3_print_reg(0x009945e4, xzs_phys_read32(0x009945e4));
	xzs_d8m3_print_reg(0x00994640, xzs_phys_read32(0x00994640));
	xzs_d8m3_print_reg(0x00994664, xzs_phys_read32(0x00994664));
	xzs_d8m3_print_reg(0x009946e4, xzs_phys_read32(0x009946e4));
	xzs_d8m3_print_reg(0x00994764, xzs_phys_read32(0x00994764));
}

static void
xzs_d8m3_run(int mode)
{
	int is_dryrun = (mode == 0);

	xzs_diag_emit("=======================================================\n");
	xzs_diag_emit("=== XZS D8-M3 DSI PLL & PHY BRING-UP STATE MACHINE ===\n");
	xzs_diag_emit("=======================================================\n");

	if (is_dryrun) {
		xzs_diag_emit("[D8-M3] MODE: DRY-RUN (TRACE_ONLY - NO MMIO WRITES)\n");
	} else if (mode == 1) {
		xzs_diag_emit("[D8-M3] MODE: REAL_HW STAGE A ONLY (PLL + CLOCKS)\n");
	} else if (mode == 2) {
		xzs_diag_emit("[D8-M3] MODE: REAL_HW FULL (PLL + CLOCKS + PHY LANES)\n");
	} else {
		xzs_diag_emit("[D8-M3] ERROR: invalid mode\n");
		xzs_diag_emit("[D8-M3] RESULT=INVALID_MODE\n");
		return;
	}

	if (!is_dryrun) {
		if (s_m3_attempted) {
			xzs_diag_emit("[D8-M3] GUARD: execution already attempted on this boot\n");
			xzs_diag_emit("[D8-M3] RESULT=M3_ALREADY_ATTEMPTED\n");
			return;
		}
		s_m3_attempted = 1;
		xzs_diag_emit("[D8-M3] M3_ATTEMPTED=yes\n");
	} else {
		xzs_diag_emit("[D8-M3] M3_ATTEMPTED=no (dry-run)\n");
	}

	/* Checkpoint D8M3-10: Prerequisites & Reference Clock */
	xzs_diag_emit("[D8-M3] CHECKPOINT D8M3-10 REFCLK_READY START\n");
	uint32_t mdss_gdsc = xzs_mmcc_read32(XZS_MMCC_MDSS_GDSC);
	uint32_t mmagic_gdsc = xzs_mmcc_read32(XZS_MMCC_MMAGIC_MDSS_GDSC);
	uint32_t ahb = xzs_mmcc_read32(XZS_MMCC_MDSS_AHB);
	uint32_t axi = xzs_mmcc_read32(XZS_MMCC_MDSS_AXI);
	uint32_t mdp = xzs_mmcc_read32(XZS_MMCC_MDSS_MDP);

	if (!is_dryrun) {
		int prereqs_ok = 1;
		if ((mdss_gdsc & 0x80000000u) == 0) {
			xzs_diag_emit("[D8-M3] PREREQ FAIL: MDSS GDSC is OFF (val=0x");
			xzs_diag_hex32(mdss_gdsc);
			xzs_diag_emit(")\n");
			prereqs_ok = 0;
		}
		if ((mmagic_gdsc & 0x80000000u) == 0) {
			xzs_diag_emit("[D8-M3] PREREQ FAIL: MMAGIC GDSC is OFF (val=0x");
			xzs_diag_hex32(mmagic_gdsc);
			xzs_diag_emit(")\n");
			prereqs_ok = 0;
		}
		if ((ahb & 0x80000001u) != 0x00000001u) {
			xzs_diag_emit("[D8-M3] PREREQ FAIL: MDSS AHB branch is not enabled/unhalted (val=0x");
			xzs_diag_hex32(ahb);
			xzs_diag_emit(")\n");
			prereqs_ok = 0;
		}
		if ((axi & 0x80000001u) != 0x00000001u) {
			xzs_diag_emit("[D8-M3] PREREQ FAIL: MDSS AXI branch is not enabled/unhalted (val=0x");
			xzs_diag_hex32(axi);
			xzs_diag_emit(")\n");
			prereqs_ok = 0;
		}
		if ((mdp & 0x80000001u) != 0x00000001u) {
			xzs_diag_emit("[D8-M3] PREREQ FAIL: MDSS MDP branch is not enabled/unhalted (val=0x");
			xzs_diag_hex32(mdp);
			xzs_diag_emit(")\n");
			prereqs_ok = 0;
		}
		if (!prereqs_ok) {
			xzs_diag_emit("[D8-M3] ERROR: PREREQUISITES_NOT_MET\n");
			xzs_diag_emit("[D8-M3] RESULT=PREREQUISITE_FAILED\n");
			return;
		}
	}
	xzs_diag_emit("[D8-M3] CHECKPOINT D8M3-10 REFCLK_READY PASS\n");

	/* Checkpoint D8M3-20: Writes 0..7 (PHY LDO bias, soft reset, post-N1 divider) */
	xzs_diag_emit("[D8-M3] CHECKPOINT D8M3-20 PLL_CONFIG_BEGIN START\n");
	for (int i = 0; i < 8; i++) {
		if (display_write32(s_m3_writes[i].addr, s_m3_writes[i].val, is_dryrun) != 0) {
			xzs_diag_emit("[D8-M3] ERROR: write failed at step D8M3-20\n");
			xzs_diag_emit("[D8-M3] RESULT=WRITE_FAILED\n");
			return;
		}
		if (!is_dryrun) {
			if (s_m3_writes[i].addr == 0x0099412cu && s_m3_writes[i].val == 1) {
				delay(10); /* 10 us assert */
			} else if (s_m3_writes[i].addr == 0x0099412cu && s_m3_writes[i].val == 0) {
				delay(100); /* 100 us de-assert recovery */
			}
		}
	}
	xzs_diag_emit("[D8-M3] CHECKPOINT D8M3-20 PLL_CONFIG_BEGIN PASS\n");

	/* Checkpoint D8M3-30: Writes 8..51 (Ext clk override, PLL registers, PHY cmn ctrl) */
	xzs_diag_emit("[D8-M3] CHECKPOINT D8M3-30 PLL_CONFIG_DONE START\n");
	for (int i = 8; i < 52; i++) {
		if (!is_dryrun && s_m3_writes[i].addr == 0x00994420u) {
			/* Qualcomm downstream 14nm PHY CMN reset pulse: 0x20 -> delay 10us -> 0x00 */
			display_write32(0x00994420u, 0x20u, is_dryrun);
			delay(10);
		}
		if (display_write32(s_m3_writes[i].addr, s_m3_writes[i].val, is_dryrun) != 0) {
			xzs_diag_emit("[D8-M3] ERROR: write failed at step D8M3-30\n");
			xzs_diag_emit("[D8-M3] RESULT=WRITE_FAILED\n");
			return;
		}
	}
	xzs_diag_emit("[D8-M3] CHECKPOINT D8M3-30 PLL_CONFIG_DONE PASS\n");

	/* Checkpoint D8M3-40: Writes 52..53 (PLL output buffer + clock kick) */
	xzs_diag_emit("[D8-M3] CHECKPOINT D8M3-40 PLL_ENABLE START\n");
	for (int i = 52; i < 54; i++) {
		if (display_write32(s_m3_writes[i].addr, s_m3_writes[i].val, is_dryrun) != 0) {
			xzs_diag_emit("[D8-M3] ERROR: write failed at step D8M3-40\n");
			xzs_diag_emit("[D8-M3] RESULT=WRITE_FAILED\n");
			return;
		}
	}
	if (!is_dryrun) {
		delay(100); /* 100 us PLL startup stabilization */
		xzs_diag_emit("[D8-M3] PRE-POLL SNAPSHOT:\n");
		xzs_d8m3_dump_pll();
	}
	xzs_diag_emit("[D8-M3] CHECKPOINT D8M3-40 PLL_ENABLE PASS\n");

	/* Checkpoint D8M3-50: Poll PLL Lock (0x009948cc bit 5 == 0x20) */
	xzs_diag_emit("[D8-M3] CHECKPOINT D8M3-50 PLL_LOCK START\n");
	if (!is_dryrun) {
		uint32_t last_val = 0;
		if (display_poll32(0x009948cc, 0x20, 0x20, 50000, &last_val) != 0) {
			uint32_t sec = xzs_phys_read32(0x00994850);
			xzs_diag_emit("[D8-M3] ERROR: PLL_LOCK_TIMEOUT primary(0x009948cc)=0x");
			xzs_diag_hex32(last_val);
			xzs_diag_emit(" secondary(0x00994850)=0x");
			xzs_diag_hex32(sec);
			xzs_diag_emit("\n");
			if ((sec & 0x20) != 0) {
				xzs_diag_emit("[D8-M3] ERROR: PLL_STATUS_MISMATCH primary!=lock secondary==lock\n");
				xzs_diag_emit("[D8-M3] RESULT=PLL_STATUS_MISMATCH\n");
			} else {
				xzs_diag_emit("[D8-M3] RESULT=PLL_LOCK_TIMEOUT\n");
			}
			return;
		}
	}
	xzs_diag_emit("[D8-M3] CHECKPOINT D8M3-50 PLL_LOCK PASS\n");

	/* Checkpoint D8M3-60: Poll PLL Ready (0x009948cc bit 0 == 0x01) */
	xzs_diag_emit("[D8-M3] CHECKPOINT D8M3-60 PLL_READY START\n");
	if (!is_dryrun) {
		uint32_t last_val = 0;
		if (display_poll32(0x009948cc, 0x01, 0x01, 50000, &last_val) != 0) {
			uint32_t sec = xzs_phys_read32(0x00994850);
			xzs_diag_emit("[D8-M3] ERROR: PLL_READY_TIMEOUT primary(0x009948cc)=0x");
			xzs_diag_hex32(last_val);
			xzs_diag_emit(" secondary(0x00994850)=0x");
			xzs_diag_hex32(sec);
			xzs_diag_emit("\n");
			if ((sec & 0x01) != 0) {
				xzs_diag_emit("[D8-M3] ERROR: PLL_STATUS_MISMATCH primary!=ready secondary==ready\n");
				xzs_diag_emit("[D8-M3] RESULT=PLL_STATUS_MISMATCH\n");
			} else {
				xzs_diag_emit("[D8-M3] RESULT=PLL_READY_TIMEOUT\n");
			}
			return;
		}
	}
	xzs_diag_emit("[D8-M3] CHECKPOINT D8M3-60 PLL_READY PASS\n");

	/* Checkpoint D8M3-70: Byte Clock Configuration (Writes 54..56) */
	xzs_diag_emit("[D8-M3] CHECKPOINT D8M3-70 BYTECLK_CONFIG START\n");
	/* 1. Configure RCG source */
	if (display_write32(s_m3_writes[54].addr, s_m3_writes[54].val, is_dryrun) != 0) {
		xzs_diag_emit("[D8-M3] ERROR: write failed at step D8M3-70 (CFG)\n");
		xzs_diag_emit("[D8-M3] RESULT=WRITE_FAILED\n");
		return;
	}
	/* 2. Trigger CMD update bit */
	if (display_write32(s_m3_writes[55].addr, s_m3_writes[55].val, is_dryrun) != 0) {
		xzs_diag_emit("[D8-M3] ERROR: write failed at step D8M3-70 (CMD)\n");
		xzs_diag_emit("[D8-M3] RESULT=WRITE_FAILED\n");
		return;
	}
	/* 3. Bounded poll for update completion (bit 0 == 0) */
	if (!is_dryrun) {
		uint32_t cmd_val = 0;
		if (display_poll32(0x008c2120, 0x01, 0x00, 10000, &cmd_val) != 0) {
			xzs_diag_emit("[D8-M3] ERROR: BYTECLK_RCG_UPDATE_TIMEOUT cmd=0x");
			xzs_diag_hex32(cmd_val);
			xzs_diag_emit("\n");
			xzs_diag_emit("[D8-M3] RESULT=BYTECLK_TIMEOUT\n");
			return;
		}
	}
	/* 4. Enable/unhalt CBCR */
	if (display_write32(s_m3_writes[56].addr, s_m3_writes[56].val, is_dryrun) != 0) {
		xzs_diag_emit("[D8-M3] ERROR: write failed at step D8M3-70 (CBCR)\n");
		xzs_diag_emit("[D8-M3] RESULT=WRITE_FAILED\n");
		return;
	}
	if (!is_dryrun) {
		uint32_t cbcr = 0;
		if (display_poll32(0x008c233c, 0x80000000u, 0, 50000, &cbcr) != 0) {
			xzs_diag_emit("[D8-M3] ERROR: BYTECLK_UNHALT_TIMEOUT cbcr=0x");
			xzs_diag_hex32(cbcr);
			xzs_diag_emit("\n");
			xzs_diag_emit("[D8-M3] RESULT=BYTECLK_TIMEOUT\n");
			return;
		}
	}
	xzs_diag_emit("[D8-M3] CHECKPOINT D8M3-70 BYTECLK_CONFIG PASS\n");

	/* Checkpoint D8M3-80: Pixel Clock Configuration (Writes 57..59) */
	xzs_diag_emit("[D8-M3] CHECKPOINT D8M3-80 PCLK_CONFIG START\n");
	/* 1. Configure RCG source */
	if (display_write32(s_m3_writes[57].addr, s_m3_writes[57].val, is_dryrun) != 0) {
		xzs_diag_emit("[D8-M3] ERROR: write failed at step D8M3-80 (CFG)\n");
		xzs_diag_emit("[D8-M3] RESULT=WRITE_FAILED\n");
		return;
	}
	/* 2. Trigger CMD update bit */
	if (display_write32(s_m3_writes[58].addr, s_m3_writes[58].val, is_dryrun) != 0) {
		xzs_diag_emit("[D8-M3] ERROR: write failed at step D8M3-80 (CMD)\n");
		xzs_diag_emit("[D8-M3] RESULT=WRITE_FAILED\n");
		return;
	}
	/* 3. Bounded poll for update completion (bit 0 == 0) */
	if (!is_dryrun) {
		uint32_t cmd_val = 0;
		if (display_poll32(0x008c2000, 0x01, 0x00, 10000, &cmd_val) != 0) {
			xzs_diag_emit("[D8-M3] ERROR: PCLK_RCG_UPDATE_TIMEOUT cmd=0x");
			xzs_diag_hex32(cmd_val);
			xzs_diag_emit("\n");
			xzs_diag_emit("[D8-M3] RESULT=PCLK_TIMEOUT\n");
			return;
		}
	}
	/* 4. Enable/unhalt CBCR */
	if (display_write32(s_m3_writes[59].addr, s_m3_writes[59].val, is_dryrun) != 0) {
		xzs_diag_emit("[D8-M3] ERROR: write failed at step D8M3-80 (CBCR)\n");
		xzs_diag_emit("[D8-M3] RESULT=WRITE_FAILED\n");
		return;
	}
	if (!is_dryrun) {
		uint32_t cbcr = 0;
		if (display_poll32(0x008c2314, 0x80000000u, 0, 50000, &cbcr) != 0) {
			xzs_diag_emit("[D8-M3] ERROR: PCLK_UNHALT_TIMEOUT cbcr=0x");
			xzs_diag_hex32(cbcr);
			xzs_diag_emit("\n");
			xzs_diag_emit("[D8-M3] RESULT=PCLK_TIMEOUT\n");
			return;
		}
	}
	xzs_diag_emit("[D8-M3] CHECKPOINT D8M3-80 PCLK_CONFIG PASS\n");

	/* Checkpoint D8M3-90: Escape Clock Configuration (Writes 60..62) */
	xzs_diag_emit("[D8-M3] CHECKPOINT D8M3-90 ESCCLK_CONFIG START\n");
	/* 1. Configure RCG source */
	if (display_write32(s_m3_writes[60].addr, s_m3_writes[60].val, is_dryrun) != 0) {
		xzs_diag_emit("[D8-M3] ERROR: write failed at step D8M3-90 (CFG)\n");
		xzs_diag_emit("[D8-M3] RESULT=WRITE_FAILED\n");
		return;
	}
	/* 2. Trigger CMD update bit */
	if (display_write32(s_m3_writes[61].addr, s_m3_writes[61].val, is_dryrun) != 0) {
		xzs_diag_emit("[D8-M3] ERROR: write failed at step D8M3-90 (CMD)\n");
		xzs_diag_emit("[D8-M3] RESULT=WRITE_FAILED\n");
		return;
	}
	/* 3. Bounded poll for update completion (bit 0 == 0) */
	if (!is_dryrun) {
		uint32_t cmd_val = 0;
		if (display_poll32(0x008c2160, 0x01, 0x00, 10000, &cmd_val) != 0) {
			xzs_diag_emit("[D8-M3] ERROR: ESCCLK_RCG_UPDATE_TIMEOUT cmd=0x");
			xzs_diag_hex32(cmd_val);
			xzs_diag_emit("\n");
			xzs_diag_emit("[D8-M3] RESULT=ESCCLK_TIMEOUT\n");
			return;
		}
	}
	/* 4. Enable/unhalt CBCR */
	if (display_write32(s_m3_writes[62].addr, s_m3_writes[62].val, is_dryrun) != 0) {
		xzs_diag_emit("[D8-M3] ERROR: write failed at step D8M3-90 (CBCR)\n");
		xzs_diag_emit("[D8-M3] RESULT=WRITE_FAILED\n");
		return;
	}
	if (!is_dryrun) {
		uint32_t cbcr = 0;
		if (display_poll32(0x008c2344, 0x80000000u, 0, 50000, &cbcr) != 0) {
			xzs_diag_emit("[D8-M3] ERROR: ESCCLK_UNHALT_TIMEOUT cbcr=0x");
			xzs_diag_hex32(cbcr);
			xzs_diag_emit("\n");
			xzs_diag_emit("[D8-M3] RESULT=ESCCLK_TIMEOUT\n");
			return;
		}
	}
	xzs_diag_emit("[D8-M3] CHECKPOINT D8M3-90 ESCCLK_CONFIG PASS\n");

	/* If PLL Stage only, complete here */
	if (mode == 1) {
		xzs_diag_emit("[D8-M3] RESULT=PASS_PLL_STAGE\n");
		return;
	}

	/* Stage B: PHY Lanes (Writes 63..67) */
	xzs_diag_emit("[D8-M3] CHECKPOINT D8M3-A0 PHY_COMMON PASS\n");

	xzs_diag_emit("[D8-M3] CHECKPOINT D8M3-B0 PHY_LANES START\n");
	for (int i = 63; i < 68; i++) {
		if (display_write32(s_m3_writes[i].addr, s_m3_writes[i].val, is_dryrun) != 0) {
			xzs_diag_emit("[D8-M3] ERROR: write failed at step D8M3-B0\n");
			xzs_diag_emit("[D8-M3] RESULT=WRITE_FAILED\n");
			return;
		}
	}
	xzs_diag_emit("[D8-M3] CHECKPOINT D8M3-B0 PHY_LANES PASS\n");

	/* Checkpoint D8M3-C0: PHY Acceptance & Readback */
	xzs_diag_emit("[D8-M3] CHECKPOINT D8M3-C0 PHY_ACCEPT START\n");
	if (!is_dryrun) {
		uint32_t dl0 = xzs_phys_read32(0x00994440);
		uint32_t dl1 = xzs_phys_read32(0x009944c0);
		uint32_t dl2 = xzs_phys_read32(0x00994540);
		uint32_t dl3 = xzs_phys_read32(0x009945c0);
		uint32_t clk = xzs_phys_read32(0x00994640);
		uint32_t pll = xzs_phys_read32(0x009948cc);

		if (dl0 != 0x06ff || dl1 != 0x06ff || dl2 != 0x06ff || dl3 != 0x06ff || clk != 0x00ff) {
			xzs_diag_emit("[D8-M3] ERROR: DRIVE_STRENGTH_VERIFY_FAILED\n");
			xzs_diag_emit("[D8-M3] RESULT=DRIVE_STRENGTH_FAILED\n");
			return;
		}
		if ((pll & 0x21) != 0x21) {
			xzs_diag_emit("[D8-M3] ERROR: PLL_UNLOCKED_AFTER_PHY status=0x");
			xzs_diag_hex32(pll);
			xzs_diag_emit("\n");
			xzs_diag_emit("[D8-M3] RESULT=PLL_UNLOCKED_POST_PHY\n");
			return;
		}
	}
	xzs_diag_emit("[D8-M3] CHECKPOINT D8M3-C0 PHY_ACCEPT PASS\n");

	if (is_dryrun) {
		xzs_diag_emit("[D8-M3] RESULT=PASS_DRYRUN\n");
	} else {
		xzs_diag_emit("[D8-M3] RESULT=PASS_FULL_M3\n");
	}
}

#endif /* _XZS_D8M3_H_ */
