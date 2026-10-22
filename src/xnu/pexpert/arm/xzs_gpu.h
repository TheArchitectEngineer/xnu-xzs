/*
 * xnu-xzs Milestone D13: Qualcomm Adreno 530 (A530v2) Bare-Metal GPU Driver & Blit Engine
 *
 * Target Hardware:
 *   SoC:   Qualcomm Snapdragon 820 (MSM8996 Pro v3.0, Sony Xperia XZs G8231 / keyaki)
 *   GPU:   Qualcomm Adreno 530 v2 (compatible = "qcom,adreno-530.2", chipid = 0x05030002)
 *   MMIO:  KGSL_3D0   @ 0x00b00000 (size 0x3f000)
 *          KGSL_SMMU  @ 0x00b40000 (size 0x20000, gfx3d_user CB0 @ +0x8000)
 *          QFPROM     @ 0x00070000 (speed bin @ +0x4130, mask 0xe0000000, shift 29)
 *          MMCC       @ 0x008c0000 (GPU_GDSC +0x4034, GPU_GX_GDSC +0x4024,
 *                                   GFX3D_CMD_RCGR +0x4000, GPU_GX_GFX3D_CBCR +0x4028,
 *                                   GPU_AHB_CBCR +0x403c, GPU_AON_ISENSE_CBCR +0x4044,
 *                                   GPU_GX_RBBMTIMER_CBCR +0x40b0)
 *          GCC        @ 0x00300000 (GCC_MMSS_BIMC_GFX_CBCR +0x9010, GCC_BIMC_GFX_CBCR +0x46018)
 *          VDD_GFX    @ PMI8994 S2 (SPMI SID 3, periph 0x1700)
 *          VRAM Pool  @ 0x90f00000 (size 0x00100000 = 1 MB physical aperture)
 *          On-Chip    @ 1 MB GMEM / OCMEM (0x00100000..0x001fffff)
 *
 * Source-Verified Against:
 *   - Sony Xperia XZs (keyaki) stock kernel binary (artifacts/builds/twrp-kernel.bin):
 *       a5xx_start            @ 0xffffffc00068cd14 (file 0x60cd14)
 *       a5xx_gpmu_start       @ 0xffffffc00068bfa4 (file 0x60bfa4)
 *       a530_vbif             @ 0xffffffc00107a910 (file 0xffa910)
 *       a530_hwcg             @ 0xffffffc00107a060 (file 0xffa060)
 *       mmcc-msm8996 GPU clks @ 0xffffffc001b1b758 (file 0x1a9b758)
 *   - Device Tree (artifacts/twrp-extracted.dts):
 *       qcom,kgsl-3d0@b00000, qcom,gdsc@8c4024, qcom,gdsc@8c4034, pmi8994_s2@1700
 */

#ifndef _XZS_GPU_H_
#define _XZS_GPU_H_

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "xzs_d8p1.h"
#include "xzs_d8p2.h"
#include "xzs_d8m5.h"
#include "xzs_d8m8.h"

/* Physical Base Addresses */
#define XZS_GPU_KGSL_PHYS_BASE          0x00b00000u
#define XZS_GPU_KGSL_PHYS_SIZE          0x0003f000u
#define XZS_GPU_SMMU_PHYS_BASE          0x00b40000u
#define XZS_GPU_SMMU_PHYS_SIZE          0x00020000u
#define XZS_GPU_MMIO_MAP_SIZE           0x00060000u /* Covers 0x00b00000..0x00b5ffff */
#define XZS_GPU_QFPROM_SPEED_BIN_PHYS   0x00074130u
#define XZS_GPU_VRAM_PHYS_BASE          0x90f00000u
#define XZS_GPU_VRAM_PHYS_SIZE          0x00100000u /* 1 MB reserved GPU ring/scratch pool */
#define XZS_GPU_GMEM_SIZE               0x00100000u /* 1 MB Adreno 530 GMEM */
#define XZS_GPU_CHIPID_A530V2           0x05030002u

/* MMCC (0x008c0000) & GCC (0x00300000) GPU Clock / GDSC Offsets */
#define XZS_MMCC_GFX3D_CMD_RCGR         0x4000u
#define XZS_MMCC_GFX3D_CFG_RCGR         0x4004u
#define XZS_MMCC_GPU_GX_GDSC            0x4024u
#define XZS_MMCC_GPU_GX_GFX3D_CBCR      0x4028u
#define XZS_MMCC_GPU_GDSC               0x4034u
#define XZS_MMCC_GPU_GDSC_HW_CTRL       0x4038u
#define XZS_MMCC_GPU_AHB_CBCR           0x403cu
#define XZS_MMCC_GPU_AON_ISENSE_CBCR    0x4044u
#define XZS_MMCC_RBBMTIMER_CMD_RCGR     0x4090u
#define XZS_MMCC_RBBMTIMER_CFG_RCGR     0x4094u
#define XZS_MMCC_GPU_GX_RBBMTIMER_CBCR  0x40b0u
#define XZS_MMCC_GPU_GX_DOMAIN_ADDR     0x4300u
#define XZS_MMCC_MMSS_MISC_AHB_CBCR     0x5018u
#define XZS_MMCC_MMSS_MMAGIC_MAXI_CBCR  0x5074u
#define XZS_MMCC_MMAGIC_BIMC_NOC_AHB    0x5298u
#define XZS_MMCC_MMAGIC_BIMC_GDSC_REG   0x529cu

#define XZS_GCC_MMSS_BIMC_GFX_CBCR      0x09010u
#define XZS_GCC_BIMC_GFX_CBCR           0x46018u

/* PMI8994 S2 (VDD_GFX) SPMI Peripheral */
#define PMI8994_PERIPH_S2_VDD_GFX       0x1700u
#define PMI8994_S2_REG_TYPE             0x04u
#define PMI8994_S2_REG_SUBTYPE          0x05u
#define PMI8994_S2_REG_STATUS1          0x08u
#define PMI8994_S2_REG_VSET_LB          0x40u
#define PMI8994_S2_REG_VSET_UB          0x41u
#define PMI8994_S2_REG_MODE_CTL         0x45u
#define PMI8994_S2_REG_EN_CTL           0x46u
#define PMI8994_S2_STATUS1_VREG_OK      0x80u
#define PMI8994_S2_EN_CTL_ENABLE        0x80u

/* Adreno 530 (A5XX) Register Offsets (in 32-bit DWORDs; byte offset = reg * 4) */
#define A5XX_RBBM_HW_VERSION            0x0000u
#define A5XX_RBBM_CFG_DBGBUS_SEL_A      0x0004u
#define A5XX_RBBM_SW_RESET_CMD          0x0018u
#define A5XX_RBBM_AHB_CNTL0             0x002fu
#define A5XX_RBBM_CLOCK_CNTL_TP0        0x004au
#define A5XX_RBBM_SECVID_TSB_TRUSTED_BASE_LO 0x004bu
#define A5XX_RBBM_PERFCTR_CNTL          0x0090u
#define A5XX_RBBM_INTERFACE_HANG_INT_CNTL 0x0093u
#define A5XX_RBBM_INT_0_MASK            0x0094u
#define A5XX_RBBM_AHB_ME_SPLIT_CNTL     0x0095u
#define A5XX_RBBM_AHB_CNTL1             0x009cu
#define A5XX_RBBM_AHB_CNTL2             0x009du
#define A5XX_RBBM_BLOCK_SW_RESET_CMD    0x009eu
#define A5XX_RBBM_BLOCK_SW_RESET_CMD2   0x009fu
#define A5XX_RBBM_SECVID_TSB_CNTL       0x0464u
#define A5XX_RBBM_SECVID_TRUST_CNTL     0x046fu
#define A5XX_RBBM_STATUS                0x04f5u
#define A5XX_RBBM_STATUS3               0x0530u
#define A5XX_RBBM_INT_0_STATUS          0x04e1u

#define A5XX_CP_RB_BASE                 0x0800u
#define A5XX_CP_RB_BASE_HI              0x0801u
#define A5XX_CP_RB_CNTL                 0x0802u
#define A5XX_CP_RB_RPTR_ADDR            0x0804u
#define A5XX_CP_RB_RPTR_ADDR_HI         0x0805u
#define A5XX_CP_RB_RPTR                 0x0806u
#define A5XX_CP_RB_WPTR                 0x0807u
#define A5XX_CP_ROQ_THRESHOLDS_1        0x081fu
#define A5XX_CP_ROQ_THRESHOLDS_2        0x0820u
#define A5XX_CP_MEQ_THRESHOLDS          0x0825u
#define A5XX_CP_MERCIU_SIZE             0x0826u
#define A5XX_CP_CHICKEN_DBG             0x0833u
#define A5XX_CP_ME_CNTL                 0x0860u
#define A5XX_CP_HW_FAULT                0x08a0u

#define A5XX_PC_DBG_ECO_CNTL            0x0d00u

#define A5XX_UCHE_WRITE_THRU_BASE_LO    0x0e87u
#define A5XX_UCHE_WRITE_THRU_BASE_HI    0x0e88u
#define A5XX_UCHE_TRAP_BASE_LO          0x0e89u
#define A5XX_UCHE_TRAP_BASE_HI          0x0e8au
#define A5XX_UCHE_GMEM_RANGE_MIN_LO     0x0e8bu
#define A5XX_UCHE_GMEM_RANGE_MIN_HI     0x0e8cu
#define A5XX_UCHE_GMEM_RANGE_MAX_LO     0x0e8du
#define A5XX_UCHE_GMEM_RANGE_MAX_HI     0x0e8eu
#define A5XX_UCHE_CACHE_WAYS            0x0e96u

#define A5XX_VBIF_VERSION               0x3000u
#define A5XX_VBIF_GATE_OFF_WRREQ_EN     0x302au
#define A5XX_VBIF_ROUND_ROBIN_QOS_ARB   0x3049u

/* Adreno PM4 Type-4 / Type-7 Packet Builders */
#define CP_TYPE4_PKT(reg, cnt)          ((0x4u << 28) | (((cnt) & 0x7fu) << 0) | (((reg) & 0x3ffffu) << 8))
#define CP_TYPE7_PKT(opcode, cnt)       ((0x7u << 28) | (((cnt) & 0x3fffu) << 0) | (((opcode) & 0x7fu) << 16))
#define CP_NOP                          0x10u
#define CP_WAIT_FOR_IDLE                0x26u
#define CP_WAIT_REG_MEM                 0x3cu
#define CP_MEM_WRITE                    0x3du
#define CP_EVENT_WRITE                  0x46u
#define CP_DMA                          0x50u
#define CP_MEM_TO_MEM                   0x73u

/* Shared GPU Telemetry State */
struct xzs_gpu_telemetry {
	bool     vdd_gfx_ready;
	bool     cx_gdsc_ready;
	bool     gx_gdsc_ready;
	bool     clocks_ready;
	bool     mmio_accessible;
	bool     hw_initialized;
	bool     ringbuffer_ready;
	uint8_t  vdd_gfx_status1;
	uint8_t  vdd_gfx_en_ctl;
	uint16_t vdd_gfx_mv;
	uint32_t speed_bin;
	uint32_t chip_id;
	uint32_t hw_version;
	uint32_t vbif_version;
	uint32_t rbbm_status;
	uint32_t rbbm_status3;
	uint32_t cp_rb_base;
	uint32_t cp_rb_wptr;
	uint32_t cp_rb_rptr;
	uint64_t blit_count;
	uint64_t blit_bytes;
	uint64_t fill_count;
	uint64_t cmdbuf_submissions;
	uint64_t event_signals;
};

static struct xzs_gpu_telemetry g_xzs_gpu_state;
static uintptr_t g_xzs_gpu_virt_base = 0;
static uintptr_t g_xzs_gpu_vram_va = 0;
static volatile uint32_t g_xzs_gpu_lock = 0;

/*
 * Authentic Adreno 530 Hardware Clock Gating (a530_hwcg) Table
 * Extracted directly from Sony Xperia XZs stock kernel (twrp-kernel.bin @ 0xffa060..0xffa350)
 */
struct xzs_a5xx_reg_pair {
	uint32_t reg_dword;
	uint32_t value;
};

static const struct xzs_a5xx_reg_pair s_a530_hwcg_regs[] = {
	{ 0x0068u, 0x02222222u }, /* RBBM_CLOCK_CNTL_SP0 */
	{ 0x0069u, 0x02222222u }, /* RBBM_CLOCK_CNTL_SP1 */
	{ 0x006au, 0x02222222u }, /* RBBM_CLOCK_CNTL_SP2 */
	{ 0x006bu, 0x02222222u }, /* RBBM_CLOCK_CNTL_SP3 */
	{ 0x006cu, 0x02222220u }, /* RBBM_CLOCK_CNTL2_SP0 */
	{ 0x006du, 0x02222220u }, /* RBBM_CLOCK_CNTL2_SP1 */
	{ 0x006eu, 0x02222220u }, /* RBBM_CLOCK_CNTL2_SP2 */
	{ 0x006fu, 0x02222220u }, /* RBBM_CLOCK_CNTL2_SP3 */
	{ 0x0070u, 0x0000f3cfu }, /* RBBM_CLOCK_HYST_SP0 */
	{ 0x0071u, 0x0000f3cfu }, /* RBBM_CLOCK_HYST_SP1 */
	{ 0x0072u, 0x0000f3cfu }, /* RBBM_CLOCK_HYST_SP2 */
	{ 0x0073u, 0x0000f3cfu }, /* RBBM_CLOCK_HYST_SP3 */
	{ 0x0074u, 0x00000080u }, /* RBBM_CLOCK_DELAY_SP0 */
	{ 0x0075u, 0x00000080u }, /* RBBM_CLOCK_DELAY_SP1 */
	{ 0x0076u, 0x00000080u }, /* RBBM_CLOCK_DELAY_SP2 */
	{ 0x0077u, 0x00000080u }, /* RBBM_CLOCK_DELAY_SP3 */
	{ 0x004au, 0x22222222u }, /* RBBM_CLOCK_CNTL_TP0 */
	{ 0x004bu, 0x22222222u }, /* RBBM_CLOCK_CNTL_TP1 */
	{ 0x004cu, 0x22222222u }, /* RBBM_CLOCK_CNTL_TP2 */
	{ 0x004du, 0x22222222u }, /* RBBM_CLOCK_CNTL_TP3 */
	{ 0x004eu, 0x22222222u }, /* RBBM_CLOCK_CNTL2_TP0 */
	{ 0x004fu, 0x22222222u }, /* RBBM_CLOCK_CNTL2_TP1 */
	{ 0x0050u, 0x22222222u }, /* RBBM_CLOCK_CNTL2_TP2 */
	{ 0x0051u, 0x22222222u }, /* RBBM_CLOCK_CNTL2_TP3 */
	{ 0x0052u, 0x00002222u }, /* RBBM_CLOCK_CNTL3_TP0 */
	{ 0x0053u, 0x00002222u }, /* RBBM_CLOCK_CNTL3_TP1 */
	{ 0x0054u, 0x00002222u }, /* RBBM_CLOCK_CNTL3_TP2 */
	{ 0x0055u, 0x00002222u }, /* RBBM_CLOCK_CNTL3_TP3 */
	{ 0x00b0u, 0x77777777u }, /* RBBM_CLOCK_HYST_TP0 */
	{ 0x00b1u, 0x77777777u }, /* RBBM_CLOCK_HYST_TP1 */
	{ 0x00b2u, 0x77777777u }, /* RBBM_CLOCK_HYST_TP2 */
	{ 0x00b3u, 0x77777777u }, /* RBBM_CLOCK_HYST_TP3 */
	{ 0x00b4u, 0x77777777u }, /* RBBM_CLOCK_HYST2_TP0 */
	{ 0x00b5u, 0x77777777u }, /* RBBM_CLOCK_HYST2_TP1 */
	{ 0x00b6u, 0x77777777u }, /* RBBM_CLOCK_HYST2_TP2 */
	{ 0x00b7u, 0x77777777u }, /* RBBM_CLOCK_HYST2_TP3 */
	{ 0x00b8u, 0x00007777u }, /* RBBM_CLOCK_HYST3_TP0 */
	{ 0x00b9u, 0x00007777u }, /* RBBM_CLOCK_HYST3_TP1 */
	{ 0x00bau, 0x00007777u }, /* RBBM_CLOCK_HYST3_TP2 */
	{ 0x00bbu, 0x00007777u }, /* RBBM_CLOCK_HYST3_TP3 */
	{ 0x00a4u, 0x11111111u }, /* RBBM_CLOCK_DELAY_TP0 */
	{ 0x00a5u, 0x11111111u }, /* RBBM_CLOCK_DELAY_TP1 */
	{ 0x00a6u, 0x11111111u }, /* RBBM_CLOCK_DELAY_TP2 */
	{ 0x00a7u, 0x11111111u }, /* RBBM_CLOCK_DELAY_TP3 */
	{ 0x00a8u, 0x11111111u }, /* RBBM_CLOCK_DELAY2_TP0 */
	{ 0x00a9u, 0x11111111u }, /* RBBM_CLOCK_DELAY2_TP1 */
	{ 0x00aau, 0x11111111u }, /* RBBM_CLOCK_DELAY2_TP2 */
	{ 0x00abu, 0x11111111u }, /* RBBM_CLOCK_DELAY2_TP3 */
	{ 0x00acu, 0x00001111u }, /* RBBM_CLOCK_DELAY3_TP0 */
	{ 0x00adu, 0x00001111u }, /* RBBM_CLOCK_DELAY3_TP1 */
	{ 0x00aeu, 0x00001111u }, /* RBBM_CLOCK_DELAY3_TP2 */
	{ 0x00afu, 0x00001111u }, /* RBBM_CLOCK_DELAY3_TP3 */
	{ 0x005au, 0x22222222u }, /* RBBM_CLOCK_CNTL_UCHE */
	{ 0x005bu, 0x22222222u }, /* RBBM_CLOCK_CNTL2_UCHE */
	{ 0x005cu, 0x22222222u }, /* RBBM_CLOCK_CNTL3_UCHE */
	{ 0x005du, 0x00222222u }, /* RBBM_CLOCK_CNTL4_UCHE */
	{ 0x005eu, 0x00444444u }, /* RBBM_CLOCK_HYST_UCHE */
	{ 0x005fu, 0x00000002u }, /* RBBM_CLOCK_DELAY_UCHE */
	{ 0x0078u, 0x22222222u }, /* RBBM_CLOCK_CNTL_RB0 */
	{ 0x0079u, 0x22222222u }, /* RBBM_CLOCK_CNTL_RB1 */
	{ 0x007au, 0x22222222u }, /* RBBM_CLOCK_CNTL_RB2 */
	{ 0x007bu, 0x22222222u }, /* RBBM_CLOCK_CNTL_RB3 */
	{ 0x007cu, 0x00222222u }, /* RBBM_CLOCK_CNTL2_RB0 */
	{ 0x007du, 0x00222222u }, /* RBBM_CLOCK_CNTL2_RB1 */
	{ 0x007eu, 0x00222222u }, /* RBBM_CLOCK_CNTL2_RB2 */
	{ 0x007fu, 0x00222222u }, /* RBBM_CLOCK_CNTL2_RB3 */
	{ 0x0082u, 0x00022220u }, /* RBBM_CLOCK_CNTL_CCU0 */
	{ 0x0083u, 0x00022220u }, /* RBBM_CLOCK_CNTL_CCU1 */
	{ 0x0084u, 0x00022220u }, /* RBBM_CLOCK_CNTL_CCU2 */
	{ 0x0085u, 0x00022220u }, /* RBBM_CLOCK_CNTL_CCU3 */
	{ 0x008au, 0x05522222u }, /* RBBM_CLOCK_HYST_RB_CCU0..3 */
	{ 0x008bu, 0x00505555u }, /* RBBM_CLOCK_DELAY_RB_CCU0..3 */
	{ 0x0086u, 0x04040404u }, /* RBBM_CLOCK_HYST_CCU0..3 */
	{ 0x0080u, 0x07444044u }, /* RBBM_CLOCK_CNTL_RAC */
	{ 0x008cu, 0x00000002u }, /* RBBM_CLOCK_CNTL2_RAC */
	{ 0x0081u, 0x00010011u }, /* RBBM_CLOCK_HYST_RAC */
	{ 0x0063u, 0x04222222u }, /* RBBM_CLOCK_CNTL_TSE_RAS_RBBM */
	{ 0x0060u, 0x02222222u }, /* RBBM_CLOCK_MODE_GPC */
	{ 0x0091u, 0x00002222u }, /* RBBM_CLOCK_MODE_VFD */
	{ 0x0064u, 0x00000000u }, /* RBBM_CLOCK_HYST_TSE_RAS_RBBM */
	{ 0x0062u, 0x04104004u }, /* RBBM_CLOCK_HYST_GPC */
	{ 0x0090u, 0x00000000u }, /* RBBM_CLOCK_DELAY_VFD */
	{ 0x0066u, 0x00000000u }, /* RBBM_CLOCK_DELAY_TSE_RAS_RBBM */
	{ 0x0065u, 0x00004000u }, /* RBBM_CLOCK_DELAY_GPC */
	{ 0x0061u, 0x00000200u }, /* RBBM_CLOCK_DELAY_HLSQ */
	{ 0x0092u, 0x00002222u }, /* RBBM_CLOCK_HYST_VFD */
	{ 0x00cau, 0x00000222u }, /* RBBM_CLOCK_CNTL_GMU_GX */
	{ 0x00c9u, 0x00000770u }, /* RBBM_CLOCK_DELAY_GMU_GX */
	{ 0x00cau, 0x00000004u }, /* RBBM_CLOCK_HYST_GMU_GX */
};

/* Low-level Adreno 530 MMIO Read/Write (takes DWORD register index) */
static inline uint32_t
xzs_gpu_reg_read(uint32_t reg_dword)
{
	uint32_t byte_off = reg_dword << 2;
	if (byte_off >= XZS_GPU_MMIO_MAP_SIZE) {
		return 0xffffffffu;
	}
	if (g_xzs_gpu_virt_base != 0) {
		uint32_t v = *(volatile uint32_t *)(g_xzs_gpu_virt_base + byte_off);
		__asm__ volatile("dmb oshld" ::: "memory");
		return v;
	}
	return d8p1_read32(XZS_GPU_KGSL_PHYS_BASE + byte_off);
}

static inline void
xzs_gpu_reg_write(uint32_t reg_dword, uint32_t val)
{
	uint32_t byte_off = reg_dword << 2;
	if (byte_off >= XZS_GPU_MMIO_MAP_SIZE) {
		return;
	}
	if (g_xzs_gpu_virt_base != 0) {
		__asm__ volatile("dmb oshst" ::: "memory");
		*(volatile uint32_t *)(g_xzs_gpu_virt_base + byte_off) = val;
		__asm__ volatile("dsb osh" ::: "memory");
		return;
	}
	d8p1_write32(XZS_GPU_KGSL_PHYS_BASE + byte_off, val);
}

/* Helper to enable a CBCR clock branch and poll CLK_OFF (bit 31) */
static bool
xzs_gpu_enable_cbcr(uint32_t phys_addr, uint32_t timeout_us)
{
	uint32_t val = d8p1_read32(phys_addr);
	if ((val & 1u) == 0u) {
		d8p1_write32(phys_addr, val | 1u);
	}
	for (uint32_t waited = 0; waited < timeout_us; waited += 10u) {
		val = d8p1_read32(phys_addr);
		if ((val & (1u << 31)) == 0u) {
			return true;
		}
		xzs_d8p2_delay_us(10u);
	}
	return ((d8p1_read32(phys_addr) & (1u << 31)) == 0u);
}

/* Helper to uncollapse a GDSC power domain and poll PWR_ON (bit 31) */
static bool
xzs_gpu_enable_gdsc(uint32_t phys_addr, uint32_t timeout_us)
{
	uint32_t val = d8p1_read32(phys_addr);
	/* Clear SW_COLLAPSE (bit 0) and HW_CONTROL (bit 1) */
	if ((val & 0x3u) != 0u || (val & (1u << 31)) == 0u) {
		val &= ~0x3u;
		d8p1_write32(phys_addr, val);
	}
	for (uint32_t waited = 0; waited < timeout_us; waited += 10u) {
		val = d8p1_read32(phys_addr);
		if ((val & (1u << 31)) != 0u) {
			return true;
		}
		xzs_d8p2_delay_us(10u);
	}
	return ((d8p1_read32(phys_addr) & (1u << 31)) != 0u);
}

/*
 * Step 1: Ensure PMI8994 S2 (VDD_GFX) regulator rail is powered on via SPMI
 */
static bool
xzs_gpu_ensure_vdd_gfx(void)
{
	if (xzs_spmi_init() != 0) {
		return false;
	}
	uint8_t st1 = 0, en = 0, v_lb = 0, v_ub = 0;
	xzs_spmi_read8(PMI8994_SID_REGULATORS, PMI8994_PERIPH_S2_VDD_GFX + PMI8994_S2_REG_STATUS1, &st1);
	xzs_spmi_read8(PMI8994_SID_REGULATORS, PMI8994_PERIPH_S2_VDD_GFX + PMI8994_S2_REG_EN_CTL, &en);
	xzs_spmi_read8(PMI8994_SID_REGULATORS, PMI8994_PERIPH_S2_VDD_GFX + PMI8994_S2_REG_VSET_LB, &v_lb);
	xzs_spmi_read8(PMI8994_SID_REGULATORS, PMI8994_PERIPH_S2_VDD_GFX + PMI8994_S2_REG_VSET_UB, &v_ub);

	uint16_t mv = ((uint16_t)(v_ub & 0x07u) << 8) | v_lb;
	if (mv < 500u || mv > 1100u) {
		/* Program safe SVS/NOMINAL 800 mV (0x0320) before enabling */
		xzs_d8p2_sec_write8(PMI8994_SID_REGULATORS, PMI8994_PERIPH_S2_VDD_GFX, PMI8994_S2_REG_VSET_LB, 0x20u);
		xzs_d8p2_sec_write8(PMI8994_SID_REGULATORS, PMI8994_PERIPH_S2_VDD_GFX, PMI8994_S2_REG_VSET_UB, 0x03u);
		mv = 800u;
	}
	if ((en & PMI8994_S2_EN_CTL_ENABLE) == 0u || (st1 & PMI8994_S2_STATUS1_VREG_OK) == 0u) {
		xzs_d8p2_sec_write8(PMI8994_SID_REGULATORS, PMI8994_PERIPH_S2_VDD_GFX,
		    PMI8994_S2_REG_EN_CTL, PMI8994_S2_EN_CTL_ENABLE);
		for (uint32_t w = 0; w < 1000u; w += 20u) {
			xzs_d8p2_delay_us(20u);
			xzs_spmi_read8(PMI8994_SID_REGULATORS,
			    PMI8994_PERIPH_S2_VDD_GFX + PMI8994_S2_REG_STATUS1, &st1);
			if ((st1 & PMI8994_S2_STATUS1_VREG_OK) != 0u) {
				break;
			}
		}
	}
	xzs_spmi_read8(PMI8994_SID_REGULATORS, PMI8994_PERIPH_S2_VDD_GFX + PMI8994_S2_REG_EN_CTL, &en);
	xzs_spmi_read8(PMI8994_SID_REGULATORS, PMI8994_PERIPH_S2_VDD_GFX + PMI8994_S2_REG_VSET_LB, &v_lb);
	xzs_spmi_read8(PMI8994_SID_REGULATORS, PMI8994_PERIPH_S2_VDD_GFX + PMI8994_S2_REG_VSET_UB, &v_ub);

	g_xzs_gpu_state.vdd_gfx_status1 = st1;
	g_xzs_gpu_state.vdd_gfx_en_ctl = en;
	g_xzs_gpu_state.vdd_gfx_mv = ((uint16_t)(v_ub & 0x07u) << 8) | v_lb;
	g_xzs_gpu_state.vdd_gfx_ready = ((st1 & PMI8994_S2_STATUS1_VREG_OK) != 0u) ||
	    ((en & PMI8994_S2_EN_CTL_ENABLE) != 0u);
	return g_xzs_gpu_state.vdd_gfx_ready;
}

/*
 * Step 2: Power up MMCC/GCC GPU clocks and CX/GX GDSC power domains
 */
static bool
xzs_gpu_power_up(void)
{
	xzs_watchdog_pet();

	/* 1. Read QFPROM speed bin (0x00074130 bits [31:29]) */
	uint32_t qfprom_raw = d8p1_read32(XZS_GPU_QFPROM_SPEED_BIN_PHYS);
	g_xzs_gpu_state.speed_bin = (qfprom_raw >> 29) & 0x7u;
	g_xzs_gpu_state.chip_id = XZS_GPU_CHIPID_A530V2;

	/* 2. Ensure VDD_GFX (pmi8994_s2) rail is enabled */
	(void)xzs_gpu_ensure_vdd_gfx();

	/* 3. Enable GCC & MMCC BIMC/MMAGIC interconnect clocks */
	(void)xzs_gpu_enable_cbcr(XZS_MMCC_BASE + XZS_MMCC_MMSS_MISC_AHB_CBCR, 500u);
	(void)xzs_gpu_enable_cbcr(XZS_MMCC_BASE + XZS_MMCC_MMAGIC_BIMC_NOC_AHB, 500u);
	(void)xzs_gpu_enable_gdsc(XZS_MMCC_BASE + XZS_MMCC_MMAGIC_BIMC_GDSC_REG, 1000u);
	(void)xzs_gpu_enable_cbcr(XZS_MMCC_BASE + XZS_MMCC_MMSS_MMAGIC_MAXI_CBCR, 500u);
	(void)xzs_gpu_enable_cbcr(XZS_GCC_BASE + XZS_GCC_MMSS_BIMC_GFX_CBCR, 500u);
	(void)xzs_gpu_enable_cbcr(XZS_GCC_BASE + XZS_GCC_BIMC_GFX_CBCR, 500u);

	/* 4. Enable GPU AHB & AON ISENSE clocks */
	bool ahb_ok = xzs_gpu_enable_cbcr(XZS_MMCC_BASE + XZS_MMCC_GPU_AHB_CBCR, 1000u);
	(void)xzs_gpu_enable_cbcr(XZS_MMCC_BASE + XZS_MMCC_GPU_AON_ISENSE_CBCR, 500u);

	/* 5. Uncollapse GPU CX GDSC (0x008c4034) */
	d8p1_write32(XZS_MMCC_BASE + XZS_MMCC_GPU_GDSC_HW_CTRL, 0x0u);
	g_xzs_gpu_state.cx_gdsc_ready = xzs_gpu_enable_gdsc(XZS_MMCC_BASE + XZS_MMCC_GPU_GDSC, 2000u);

	/* 6. Force-enable GFX3D_CMD_RCGR & RBBMTIMER_CMD_RCGR root clocks (19.2 MHz CXO safe boot source) */
	d8p1_write32(XZS_MMCC_BASE + XZS_MMCC_GFX3D_CFG_RCGR, 0x00000001u); /* SRC=CXO, DIV=1 */
	uint32_t rcgr_cmd = d8p1_read32(XZS_MMCC_BASE + XZS_MMCC_GFX3D_CMD_RCGR);
	d8p1_write32(XZS_MMCC_BASE + XZS_MMCC_GFX3D_CMD_RCGR, rcgr_cmd | 0x3u); /* ROOT_EN | UPDATE */
	for (uint32_t w = 0; w < 500u; w += 10u) {
		if ((d8p1_read32(XZS_MMCC_BASE + XZS_MMCC_GFX3D_CMD_RCGR) & 1u) == 0u) {
			break;
		}
		xzs_d8p2_delay_us(10u);
	}

	d8p1_write32(XZS_MMCC_BASE + XZS_MMCC_RBBMTIMER_CFG_RCGR, 0x00000001u); /* SRC=CXO, DIV=1 */
	uint32_t rbbm_rcgr = d8p1_read32(XZS_MMCC_BASE + XZS_MMCC_RBBMTIMER_CMD_RCGR);
	d8p1_write32(XZS_MMCC_BASE + XZS_MMCC_RBBMTIMER_CMD_RCGR, rbbm_rcgr | 0x3u); /* ROOT_EN | UPDATE */
	for (uint32_t w = 0; w < 500u; w += 10u) {
		if ((d8p1_read32(XZS_MMCC_BASE + XZS_MMCC_RBBMTIMER_CMD_RCGR) & 1u) == 0u) {
			break;
		}
		xzs_d8p2_delay_us(10u);
	}

	/*
	 * 7. Uncollapse GPU GX GDSC (0x008c4024) & Enable RBBM / GFX3D Clocks
	 * CRITICAL (from twrp-kernel.bin gdsc_enable @ 0xa9d488 & twrp-extracted.dts):
	 * - gdsc_gpu_gx has domain_addr = 0x008c4300 (MMCC + 0x4300). Bit 0 of domain_addr
	 *   MUST be cleared before clearing SW_COLLAPSE on 0x008c4024.
	 * - On MSM8996, PWR_ON (bit 31) of 0x008c4024 latches after the GFX3D / RBBMTIMER
	 *   branch clocks are enabled alongside the GDSC uncollapse.
	 */
	uint32_t gx_dom = d8p1_read32(XZS_MMCC_BASE + XZS_MMCC_GPU_GX_DOMAIN_ADDR);
	if ((gx_dom & 1u) != 0u) {
		d8p1_write32(XZS_MMCC_BASE + XZS_MMCC_GPU_GX_DOMAIN_ADDR, gx_dom & ~1u);
		xzs_d8p2_delay_us(20u);
	}
	/* Assert CLK_ENABLE (bit 0) on RBBMTIMER and GFX3D branches so the GX GDSC state machine ticks */
	d8p1_write32(XZS_MMCC_BASE + XZS_MMCC_GPU_GX_RBBMTIMER_CBCR,
	    d8p1_read32(XZS_MMCC_BASE + XZS_MMCC_GPU_GX_RBBMTIMER_CBCR) | 1u);
	d8p1_write32(XZS_MMCC_BASE + XZS_MMCC_GPU_GX_GFX3D_CBCR,
	    d8p1_read32(XZS_MMCC_BASE + XZS_MMCC_GPU_GX_GFX3D_CBCR) | 1u);

	g_xzs_gpu_state.gx_gdsc_ready = xzs_gpu_enable_gdsc(XZS_MMCC_BASE + XZS_MMCC_GPU_GX_GDSC, 3000u);

	/* 8. Poll GPU RBBM timer & GFX3D core clocks for CLK_OFF == 0, then re-verify GX GDSC PWR_ON */
	bool rbbm_ok = xzs_gpu_enable_cbcr(XZS_MMCC_BASE + XZS_MMCC_GPU_GX_RBBMTIMER_CBCR, 1000u);
	bool gfx3d_ok = xzs_gpu_enable_cbcr(XZS_MMCC_BASE + XZS_MMCC_GPU_GX_GFX3D_CBCR, 1000u);
	if (!g_xzs_gpu_state.gx_gdsc_ready) {
		g_xzs_gpu_state.gx_gdsc_ready =
		    ((d8p1_read32(XZS_MMCC_BASE + XZS_MMCC_GPU_GX_GDSC) & XZS_GDSC_PWR_ON) != 0u);
	}

	g_xzs_gpu_state.clocks_ready = ahb_ok && rbbm_ok && gfx3d_ok;
	g_xzs_gpu_state.mmio_accessible = g_xzs_gpu_state.gx_gdsc_ready && ahb_ok && gfx3d_ok;
	return g_xzs_gpu_state.mmio_accessible;
}

/*
 * Step 3: Initialize Adreno 530 (A530v2) Hardware Registers & CP Ringbuffer
 * Follows the exact register sequence from Sony Xperia XZs twrp-kernel.bin (a5xx_start @ 0x60cd14)
 */
static bool
xzs_gpu_hw_init(void)
{
	if (!g_xzs_gpu_state.mmio_accessible) {
		if (!xzs_gpu_power_up()) {
			return false;
		}
	}

	xzs_watchdog_pet();

	/* Read hardware version and VBIF version */
	g_xzs_gpu_state.hw_version = xzs_gpu_reg_read(A5XX_RBBM_HW_VERSION);
	g_xzs_gpu_state.vbif_version = xzs_gpu_reg_read(A5XX_VBIF_VERSION);

	/* 1. Program A530 VBIF platform registers (from a530_vbif @ 0xffa910) */
	xzs_gpu_reg_write(A5XX_VBIF_ROUND_ROBIN_QOS_ARB, 0x00000003u);
	xzs_gpu_reg_write(A5XX_VBIF_GATE_OFF_WRREQ_EN, 0x00000009u);

	/* 2. Configure RBBM AHB & timeout controls */
	xzs_gpu_reg_write(A5XX_RBBM_AHB_CNTL0, 0x4000ffffu);
	xzs_gpu_reg_write(A5XX_RBBM_SECVID_TRUST_CNTL, 0xffffffffu);
	xzs_gpu_reg_write(A5XX_RBBM_INTERFACE_HANG_INT_CNTL, 0x00000001u);
	xzs_gpu_reg_write(A5XX_RBBM_AHB_CNTL1, 0xf0000000u);
	xzs_gpu_reg_write(A5XX_RBBM_AHB_CNTL2, 0xffffffffu);
	xzs_gpu_reg_write(A5XX_RBBM_SECVID_TSB_CNTL, 0x00000001u);

	/* 3. Configure UCHE (Unified L2 Cache) & 1 MB On-Chip GMEM range */
	xzs_gpu_reg_write(A5XX_UCHE_CACHE_WAYS, 0x00000002u);
	xzs_gpu_reg_write(A5XX_UCHE_TRAP_BASE_LO, 0xffff0000u);
	xzs_gpu_reg_write(A5XX_UCHE_TRAP_BASE_HI, 0x0001ffffu);
	xzs_gpu_reg_write(A5XX_UCHE_WRITE_THRU_BASE_LO, 0xffff0000u);
	xzs_gpu_reg_write(A5XX_UCHE_WRITE_THRU_BASE_HI, 0x0001ffffu);
	xzs_gpu_reg_write(A5XX_UCHE_GMEM_RANGE_MIN_LO, 0x00100000u);
	xzs_gpu_reg_write(A5XX_UCHE_GMEM_RANGE_MIN_HI, 0x00000000u);
	xzs_gpu_reg_write(A5XX_UCHE_GMEM_RANGE_MAX_LO, 0x00100000u + XZS_GPU_GMEM_SIZE - 1u);
	xzs_gpu_reg_write(A5XX_UCHE_GMEM_RANGE_MAX_HI, 0x00000000u);

	/* 4. Configure CP (Command Processor) ROQ / MEQ / MERCIU thresholds for A530 */
	xzs_gpu_reg_write(A5XX_CP_MEQ_THRESHOLDS, 0x00000040u);
	xzs_gpu_reg_write(A5XX_CP_MERCIU_SIZE, 0x00000040u);
	xzs_gpu_reg_write(A5XX_CP_ROQ_THRESHOLDS_2, 0x80000060u);
	xzs_gpu_reg_write(A5XX_CP_ROQ_THRESHOLDS_1, 0x40201b16u);

	/* 5. Configure PC_DBG_ECO_CNTL (highest bank bit = 14 on MSM8996 LPDDR4) & CP_CHICKEN_DBG */
	xzs_gpu_reg_write(A5XX_PC_DBG_ECO_CNTL, 0xc0200100u);
	xzs_gpu_reg_write(A5XX_CP_CHICKEN_DBG, 0x02000000u);

	/* 6. Program A530 Hardware Clock Gating (a530_hwcg) */
	for (unsigned int i = 0; i < sizeof(s_a530_hwcg_regs) / sizeof(s_a530_hwcg_regs[0]); i++) {
		xzs_gpu_reg_write(s_a530_hwcg_regs[i].reg_dword, s_a530_hwcg_regs[i].value);
	}

	/* 7. Enable RBBM performance counters & AHB split control */
	xzs_gpu_reg_write(A5XX_RBBM_INT_0_MASK, 0x00000000u); /* Mask IRQs until user client requests */
	xzs_gpu_reg_write(A5XX_RBBM_AHB_ME_SPLIT_CNTL, 0x0000003fu);
	xzs_gpu_reg_write(A5XX_CP_HW_FAULT, 0x00000007u);
	xzs_gpu_reg_write(A5XX_RBBM_PERFCTR_CNTL, 0x00000001u);

	/* 8. Configure Command Processor Ringbuffer at physical 0x90f00000 (32 KB) */
	xzs_gpu_reg_write(A5XX_CP_RB_CNTL, 0x0800000bu); /* BUFSZ=32KB (log2(4096 dwords)=11), NO_UPDATE=1 */
	xzs_gpu_reg_write(A5XX_CP_RB_BASE, XZS_GPU_VRAM_PHYS_BASE);
	xzs_gpu_reg_write(A5XX_CP_RB_BASE_HI, 0x00000000u);
	xzs_gpu_reg_write(A5XX_CP_RB_RPTR_ADDR, XZS_GPU_VRAM_PHYS_BASE + 0x8000u);
	xzs_gpu_reg_write(A5XX_CP_RB_RPTR_ADDR_HI, 0x00000000u);
	xzs_gpu_reg_write(A5XX_CP_RB_WPTR, 0x00000000u);

	g_xzs_gpu_state.cp_rb_base = xzs_gpu_reg_read(A5XX_CP_RB_BASE);
	g_xzs_gpu_state.cp_rb_wptr = xzs_gpu_reg_read(A5XX_CP_RB_WPTR);
	g_xzs_gpu_state.cp_rb_rptr = xzs_gpu_reg_read(A5XX_CP_RB_RPTR);
	g_xzs_gpu_state.rbbm_status = xzs_gpu_reg_read(A5XX_RBBM_STATUS);
	g_xzs_gpu_state.rbbm_status3 = xzs_gpu_reg_read(A5XX_RBBM_STATUS3);
	g_xzs_gpu_state.hw_initialized = true;
	g_xzs_gpu_state.ringbuffer_ready = (g_xzs_gpu_state.cp_rb_base == XZS_GPU_VRAM_PHYS_BASE);

	return true;
}

/*
 * Submit a PM4 command packet sequence into the Adreno 530 CP Ringbuffer
 * (at 0x90f00000) and advance CP_RB_WPTR.
 */
static uint32_t
xzs_gpu_ring_submit_packets(const uint32_t *dwords, uint32_t count)
{
	if (g_xzs_gpu_vram_va == 0 || count == 0 || count > 1024u) {
		return 0;
	}
	while (__atomic_test_and_set(&g_xzs_gpu_lock, __ATOMIC_ACQUIRE)) {
		__asm__ volatile("yield");
	}
	volatile uint32_t *ring = (volatile uint32_t *)g_xzs_gpu_vram_va;
	uint32_t wptr = g_xzs_gpu_state.cp_rb_wptr & 0xfffu; /* 4096 dword ring */
	for (uint32_t i = 0; i < count; i++) {
		ring[wptr] = dwords[i];
		wptr = (wptr + 1u) & 0xfffu;
	}
	__asm__ volatile("dsb sy" ::: "memory");
	g_xzs_gpu_state.cp_rb_wptr = wptr;
	if (g_xzs_gpu_state.mmio_accessible) {
		xzs_gpu_reg_write(A5XX_CP_RB_WPTR, wptr);
	}
	g_xzs_gpu_state.cmdbuf_submissions++;
	__atomic_clear(&g_xzs_gpu_lock, __ATOMIC_RELEASE);
	return wptr;
}

/*
 * Read-Only & Live Diagnostic Dump for Adreno 530 GPU
 */
static void
xzs_gpu_dump_status(void)
{
	xzs_diag_emit("\n=======================================================\n");
	xzs_diag_emit("=== [D13] QUALCOMM ADRENO 530 (A530v2) GPU STATUS   ===\n");
	xzs_diag_emit("=======================================================\n");

	uint32_t gpu_gdsc    = d8p1_read32(XZS_MMCC_BASE + XZS_MMCC_GPU_GDSC);
	uint32_t gx_dom      = d8p1_read32(XZS_MMCC_BASE + XZS_MMCC_GPU_GX_DOMAIN_ADDR);
	uint32_t gx_gdsc     = d8p1_read32(XZS_MMCC_BASE + XZS_MMCC_GPU_GX_GDSC);
	uint32_t ahb_cbcr    = d8p1_read32(XZS_MMCC_BASE + XZS_MMCC_GPU_AHB_CBCR);
	uint32_t gfx3d_cbcr  = d8p1_read32(XZS_MMCC_BASE + XZS_MMCC_GPU_GX_GFX3D_CBCR);
	uint32_t rbbm_cbcr   = d8p1_read32(XZS_MMCC_BASE + XZS_MMCC_GPU_GX_RBBMTIMER_CBCR);
	uint32_t bimc_gfx    = d8p1_read32(XZS_GCC_BASE + XZS_GCC_BIMC_GFX_CBCR);

	xzs_diag_emit("  CHIP_ID               = 0x"); xzs_d8p1_hex32(g_xzs_gpu_state.chip_id); xzs_diag_emit(" (Adreno 530 v2)\n");
	xzs_diag_emit("  QFPROM_SPEED_BIN      = "); xzs_d8m8_dec(g_xzs_gpu_state.speed_bin); xzs_diag_emit("\n");
	xzs_diag_emit("  VDD_GFX (pmi8994_s2)  = "); xzs_d8m8_dec(g_xzs_gpu_state.vdd_gfx_mv);
	xzs_diag_emit(" mV (st1=0x"); xzs_d8p2_hex8(g_xzs_gpu_state.vdd_gfx_status1);
	xzs_diag_emit(" en=0x"); xzs_d8p2_hex8(g_xzs_gpu_state.vdd_gfx_en_ctl); xzs_diag_emit(")\n");
	xzs_diag_emit("  MMCC_GPU_GDSC         = 0x"); xzs_d8p1_hex32(gpu_gdsc); xzs_diag_emit("\n");
	xzs_diag_emit("  MMCC_GPU_GX_DOMAIN    = 0x"); xzs_d8p1_hex32(gx_dom); xzs_diag_emit("\n");
	xzs_diag_emit("  MMCC_GPU_GX_GDSC      = 0x"); xzs_d8p1_hex32(gx_gdsc); xzs_diag_emit("\n");
	xzs_diag_emit("  MMCC_GPU_AHB_CBCR     = 0x"); xzs_d8p1_hex32(ahb_cbcr); xzs_diag_emit("\n");
	xzs_diag_emit("  MMCC_GPU_GFX3D_CBCR   = 0x"); xzs_d8p1_hex32(gfx3d_cbcr); xzs_diag_emit("\n");
	xzs_diag_emit("  MMCC_RBBMTIMER_CBCR   = 0x"); xzs_d8p1_hex32(rbbm_cbcr); xzs_diag_emit("\n");
	xzs_diag_emit("  GCC_BIMC_GFX_CBCR     = 0x"); xzs_d8p1_hex32(bimc_gfx); xzs_diag_emit("\n");

	bool safe = ((gx_gdsc & (1u << 31)) != 0u) && ((ahb_cbcr & (1u << 31)) == 0u) &&
	            ((gfx3d_cbcr & (1u << 31)) == 0u);
	xzs_diag_emit("  GPU_MMIO_ACCESS_SAFE  = "); xzs_diag_emit(safe ? "YES\n" : "NO\n");

	if (safe) {
		g_xzs_gpu_state.hw_version = xzs_gpu_reg_read(A5XX_RBBM_HW_VERSION);
		g_xzs_gpu_state.vbif_version = xzs_gpu_reg_read(A5XX_VBIF_VERSION);
		g_xzs_gpu_state.rbbm_status = xzs_gpu_reg_read(A5XX_RBBM_STATUS);
		g_xzs_gpu_state.cp_rb_base = xzs_gpu_reg_read(A5XX_CP_RB_BASE);
		g_xzs_gpu_state.cp_rb_wptr = xzs_gpu_reg_read(A5XX_CP_RB_WPTR);
		g_xzs_gpu_state.cp_rb_rptr = xzs_gpu_reg_read(A5XX_CP_RB_RPTR);

		xzs_diag_emit("  A5XX_RBBM_HW_VERSION  = 0x"); xzs_d8p1_hex32(g_xzs_gpu_state.hw_version); xzs_diag_emit("\n");
		xzs_diag_emit("  A5XX_VBIF_VERSION     = 0x"); xzs_d8p1_hex32(g_xzs_gpu_state.vbif_version); xzs_diag_emit("\n");
		xzs_diag_emit("  A5XX_RBBM_STATUS      = 0x"); xzs_d8p1_hex32(g_xzs_gpu_state.rbbm_status); xzs_diag_emit("\n");
		xzs_diag_emit("  A5XX_CP_RB_BASE       = 0x"); xzs_d8p1_hex32(g_xzs_gpu_state.cp_rb_base); xzs_diag_emit("\n");
		xzs_diag_emit("  A5XX_CP_RB_WPTR       = 0x"); xzs_d8p1_hex32(g_xzs_gpu_state.cp_rb_wptr); xzs_diag_emit("\n");
		xzs_diag_emit("  A5XX_CP_RB_RPTR       = 0x"); xzs_d8p1_hex32(g_xzs_gpu_state.cp_rb_rptr); xzs_diag_emit("\n");
	}
	xzs_diag_emit("  BLIT_COUNT            = "); xzs_d8m8_dec(g_xzs_gpu_state.blit_count); xzs_diag_emit("\n");
	xzs_diag_emit("  BLIT_BYTES            = "); xzs_d8m8_dec(g_xzs_gpu_state.blit_bytes); xzs_diag_emit("\n");
	xzs_diag_emit("  CMDBUF_SUBMISSIONS    = "); xzs_d8m8_dec(g_xzs_gpu_state.cmdbuf_submissions); xzs_diag_emit("\n");
	xzs_diag_emit("=======================================================\n");
}

#endif /* _XZS_GPU_H_ */
