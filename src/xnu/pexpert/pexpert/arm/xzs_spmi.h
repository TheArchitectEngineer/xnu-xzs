/*
 * Copyright (c) 2026 Apple Inc. All rights reserved.
 * SPMI PMIC Arbiter v2 & PM8994 Definitions for Sony Xperia XZs (MSM8996)
 * Phase D2-C2.4B: PM8994 L25 / SPMI Read-Only Resource Audit
 */

#ifndef _PEXPERT_ARM_XZS_SPMI_H_
#define _PEXPERT_ARM_XZS_SPMI_H_

#include <stdint.h>
#include <stdbool.h>

/*
 * Qualcomm MSM8996 SPMI PMIC Arbiter v2 MMIO Apertures
 * Source-verified from device tree:
 * core:   0x0400F000 (1000)
 * chnls:  0x04400000 (800000) -> wr_base
 * obsrvr: 0x04C00000 (800000) -> rd_base
 */
#define XZS_SPMI_CORE_PHYS_BASE       0x0400F000ULL
#define XZS_SPMI_CORE_MMIO_SIZE       0x1000ULL       /* 4 KB */

#define XZS_SPMI_CHNLS_PHYS_BASE      0x04400000ULL   /* wr_base */
#define XZS_SPMI_CHNLS_MMIO_SIZE      0x800000ULL     /* 8 MB */

#define XZS_SPMI_OBSRVR_PHYS_BASE     0x04C00000ULL   /* rd_base */
#define XZS_SPMI_OBSRVR_MMIO_SIZE     0x800000ULL     /* 8 MB */

#define XZS_SPMI_EE                   0               /* APPS Execution Environment */

/*
 * SPMI Core Registers
 */
#define SPMI_REG_HW_VER               0x0000
#define SPMI_REG_APID_MAP(apid)       (0x0800 + 4 * (apid))
#define SPMI_MAX_APID                 512
#define SPMI_PPID_MASK                0x0FFFU

/*
 * SPMI Channel Registers (Offset within Channel Window)
 * Channel Offset Formula: 0x1000 * EE + 0x8000 * APID
 */
#define SPMI_CHANNEL_WINDOW_SIZE      0x8000ULL
#define SPMI_CHANNEL_OFFSET(ee, apid) ((0x1000ULL * (vm_offset_t)(ee)) + (0x8000ULL * (vm_offset_t)(apid)))

#define PMIC_ARB_REG_CMD              0x00
#define PMIC_ARB_REG_CONFIG           0x04
#define PMIC_ARB_REG_STATUS           0x08
#define PMIC_ARB_REG_WDATA0           0x10
#define PMIC_ARB_REG_WDATA1           0x14
#define PMIC_ARB_REG_RDATA0           0x18
#define PMIC_ARB_REG_RDATA1           0x1C

/*
 * Channel Status Register Bits
 */
#define PMIC_ARB_STATUS_DONE          (1U << 0)
#define PMIC_ARB_STATUS_FAILURE       (1U << 1)
#define PMIC_ARB_STATUS_DENIED        (1U << 2)
#define PMIC_ARB_STATUS_DROPPED       (1U << 3)

/*
 * Command Opcodes
 */
#define PMIC_ARB_OP_EXT_WRITEL        0
#define PMIC_ARB_OP_EXT_READL         1
#define PMIC_ARB_OP_EXT_WRITE         2
#define PMIC_ARB_OP_EXT_READ          13

/*
 * Format Command v2:
 * (opc << 27) | ((addr & 0xFF) << 4) | (bc & 0x7)
 */
#define PMIC_ARB_FMT_CMD_V2(opc, addr, bc) \
	((((uint32_t)(opc) & 0x1FU) << 27) | \
	 (((uint32_t)(addr) & 0xFFU) << 4) | \
	 (((uint32_t)(bc) & 0x07U)))

/*
 * PMIC Common Peripheral Register Offsets (relative to peripheral base)
 */
#define PMIC_REG_DIG_MAJOR_REV        0x01
#define PMIC_REG_TYPE                 0x04
#define PMIC_REG_SUBTYPE              0x05
#define PMIC_REG_STATUS               0x08
#define PMIC_REG_VOLTAGE_RANGE        0x40
#define PMIC_REG_VOLTAGE_SET          0x41
#define PMIC_REG_MODE                 0x45
#define PMIC_REG_ENABLE               0x46
#define PMIC_REG_PULL_DOWN            0x48
#define PMIC_REG_SOFT_START           0x4C
#define PMIC_REG_STEP_CTRL            0x61

#define PMIC_ENABLE_MASK              0x80U
#define PMIC_ENABLE_BIT               0x80U
#define PMIC_MODE_HPM_BIT             0x80U

/*
 * PM8994 Peripheral Addresses (SID = 0)
 */
#define PM8994_SID                    0
#define PM8994_PERIPH_REVID           0x0100
#define PM8994_PERIPH_L12             0x4B00
#define PM8994_PERIPH_L25             0x5800
#define PM8994_PERIPH_L26             0x5900
#define PM8994_PERIPH_L27             0x5A00
#define PM8994_PERIPH_L28             0x5B00

/*
 * Functions
 */
int xzs_spmi_init(void);
uint32_t xzs_spmi_get_raw_apid_map(uint16_t apid);
int xzs_spmi_find_apid(uint8_t sid, uint16_t addr, uint16_t *out_apid);
int xzs_spmi_read8(uint8_t sid, uint16_t addr, uint8_t *val);
int xzs_spmi_read_bulk(uint8_t sid, uint16_t addr, uint8_t *buf, size_t len);
int xzs_spmi_write8(uint8_t sid, uint16_t addr, uint8_t val);
void xzs_spmi_phase_d2c24b_probe(void);
void xzs_spmi_phase_d2c24c_probe(void);

#endif /* _PEXPERT_ARM_XZS_SPMI_H_ */
