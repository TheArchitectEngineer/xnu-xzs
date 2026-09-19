/*
 * Copyright (c) 2026 lechaukha12. All rights reserved.
 *
 * Qualcomm MSM8996 SDCC1 / eMMC Host Controller Bring-up (Phase D2-M1)
 * Strictly READ-ONLY definitions and accessors for MMIO identity probe.
 */

#ifndef _PEXPERT_ARM_XZS_SDHCI_H
#define _PEXPERT_ARM_XZS_SDHCI_H

#include <stdint.h>
#include <stdbool.h>

/*
 * MSM8996 SDC1 Audited Physical Base Addresses
 * Derived from live Sony Xperia XZs DT (sdhci@7464900):
 *   reg = <0x7464900 0x500 0x7464000 0x800 0x7464e00 0x19c>
 *   reg-names = "hc_mem", "core_mem", "cmdq_mem"
 */
#define XZS_SDCC1_HC_PHYS_BASE          0x07464900UL
#define XZS_SDCC1_HC_MMIO_SIZE          0x500UL

#define XZS_SDCC1_CORE_PHYS_BASE        0x07464000UL
#define XZS_SDCC1_CORE_MMIO_SIZE        0x800UL

#define XZS_SDCC1_CMDQ_PHYS_BASE        0x07464E00UL
#define XZS_SDCC1_CMDQ_MMIO_SIZE        0x200UL

/* Interrupts: hc_irq = SPI 141 (GIC 173), pwr_irq = SPI 134 (GIC 166) */
#define XZS_SDCC1_HC_IRQ_SPI            141
#define XZS_SDCC1_HC_IRQ_GIC            173
#define XZS_SDCC1_PWR_IRQ_SPI           134
#define XZS_SDCC1_PWR_IRQ_GIC           166

/*
 * GCC Base and SDC1 Clock Offsets
 * Base: 0x00300000UL
 * Audited from stock aboot.img clock table & Linux DT
 */
#define XZS_GCC_PHYS_BASE               0x00300000UL
#define XZS_GCC_MMIO_SIZE               0x90000UL

#define GCC_SDCC1_BCR_OFFSET            0x13000U
#define GCC_SDCC1_APPS_CBCR_OFFSET       0x13004U
#define GCC_SDCC1_AHB_CBCR_OFFSET        0x13008U
#define SDCC1_APPS_CMD_RCGR_OFFSET      0x13010U
#define SDCC1_APPS_CFG_RCGR_OFFSET      0x13014U
#define SDCC1_APPS_M_OFFSET             0x13018U
#define SDCC1_APPS_N_OFFSET             0x1301CU
#define SDCC1_APPS_D_OFFSET             0x13020U

#define GCC_SDCC1_ICE_CORE_CBCR_OFFSET  0x13038U
#define SDCC1_ICE_CORE_CMD_RCGR_OFFSET  0x13040U
#define SDCC1_ICE_CORE_CFG_RCGR_OFFSET  0x13044U

/*
 * Standard SDHCI Host Controller Register Offsets (relative to HC Base: 0x07464900)
 */
#define SDHCI_SDMA_ADDRESS              0x00U
#define SDHCI_ARGUMENT2                 0x00U
#define SDHCI_BLOCK_SIZE                0x04U
#define SDHCI_BLOCK_COUNT               0x06U
#define SDHCI_ARGUMENT                  0x08U
#define SDHCI_TRANSFER_MODE             0x0CU
#define SDHCI_COMMAND                   0x0EU
#define SDHCI_RESPONSE_0                0x10U
#define SDHCI_RESPONSE_1                0x14U
#define SDHCI_RESPONSE_2                0x18U
#define SDHCI_RESPONSE_3                0x1CU
#define SDHCI_BUFFER                    0x20U
#define SDHCI_PRESENT_STATE             0x24U
#define SDHCI_HOST_CONTROL              0x28U
#define SDHCI_POWER_CONTROL             0x29U
#define SDHCI_BLOCK_GAP_CONTROL         0x2AU
#define SDHCI_WAKE_UP_CONTROL           0x2BU
#define SDHCI_CLOCK_CONTROL             0x2CU
#define SDHCI_TIMEOUT_CONTROL           0x2EU
#define SDHCI_SOFTWARE_RESET            0x2FU
#define SDHCI_INT_STATUS                0x30U
#define SDHCI_INT_ENABLE                0x34U
#define SDHCI_SIGNAL_ENABLE             0x38U
#define SDHCI_ACMD12_ERR                0x3CU
#define SDHCI_HOST_CONTROL2             0x3EU
#define SDHCI_CAPABILITIES              0x40U
#define SDHCI_CAPABILITIES_1            0x44U
#define SDHCI_MAX_CURRENT               0x48U
#define SDHCI_FORCE_AUTO_EVENT          0x50U
#define SDHCI_ADMA_ERROR                0x54U
#define SDHCI_ADMA_ADDRESS_LO           0x58U
#define SDHCI_ADMA_ADDRESS_HI           0x5CU
#define SDHCI_HOST_VERSION              0xFEU

/*
 * Qualcomm SDC Vendor Specific Registers (relative to CORE Base: 0x07464000)
 */
#define MSM_SDCC_FIFO                   0x000U
#define MSM_SDCC_HC_MODE                0x078U
#define MSM_SDCC_HC_MODE_HC_MODE_EN     (1U << 0)
#define MSM_SDCC_HC_MODE_FF_CLK_SW_RST_DIS (1U << 13)
#define MSM_SDCC_HC_MODE_PREREQ         (MSM_SDCC_HC_MODE_HC_MODE_EN | MSM_SDCC_HC_MODE_FF_CLK_SW_RST_DIS)

#define MSM_SDCC_CORE_PWRCTL_STATUS     0x0DCU
#define MSM_SDCC_CORE_PWRCTL_MASK       0x0E0U
#define MSM_SDCC_CORE_PWRCTL_CLEAR      0x0E4U
#define MSM_SDCC_CORE_PWRCTL_CTL        0x0E8U

#define MSM_SDCC_DLL_CONFIG             0x100U
#define MSM_SDCC_DLL_STATUS             0x108U
#define MSM_SDCC_DLL_CONFIG_2           0x10CU
#define MSM_SDCC_DLL_CONFIG_3           0x110U
#define MSM_SDCC_DLL_STATUS_2           0x114U

/*
 * Qualcomm SDCC Vendor Specific Registers (relative to HC Base: 0x07464900)
 */
#define SDCC1_HC_VENDOR_SPEC            0x10CU
#define SDCC1_HC_VENDOR_SPEC_POR        0x00000a1CU

/*
 * TLMM SDC1 Pad Physical Base
 */
#define XZS_TLMM_SDC1_PHYS_BASE         0x0113C000UL
#define XZS_TLMM_SDC1_MMIO_SIZE         0x1000UL

/*
 * PMIC PM8994 Regulators for eMMC
 */
#define PM8994_PERIPH_L20               0x5300U /* VDD: 2.95V */
#define PM8994_PERIPH_S4                0x1D00U /* VDD_IO: 1.80V always-on */

/*
 * SDHCI Reset Bits (relative to HC offset 0x2F)
 */
#define SDHCI_RESET_ALL                 0x01U
#define SDHCI_RESET_CMD                 0x02U
#define SDHCI_RESET_DATA                0x04U

/*
 * Source-Proven 400 KHz RCG2 Values (XO-derived, P_XO = 19.2 MHz)
 * Math: (19.2 MHz / 12) * (1 / 4) = 400 KHz
 * CFG_RCGR: SRC_DIV = 23 (div 12), SRC_SEL = 0 (XO), MODE = 2 (dual-edge fraction)
 */
#define SDCC1_400K_CFG_RCGR             0x00002017U
#define SDCC1_400K_M                    0x00000001U
#define SDCC1_400K_N                    0xFFFFFFFCU
#define SDCC1_400K_D                    0xFFFFFFFBU

/*
 * Function Prototypes
 */
uint32_t xzs_sdhci_hc_read32(uint32_t offset);
uint16_t xzs_sdhci_hc_read16(uint32_t offset);
uint8_t  xzs_sdhci_hc_read8(uint32_t offset);

uint32_t xzs_sdhci_core_read32(uint32_t offset);

void xzs_sdhci_phase_d2m1_probe(void);
void xzs_sdhci_phase_d2m2_probe(void);

#endif /* _PEXPERT_ARM_XZS_SDHCI_H */
