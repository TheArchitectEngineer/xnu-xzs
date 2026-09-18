/*
 * Copyright (c) 2026 lechaukha12. All rights reserved.
 *
 * Qualcomm MSM8996 UFS 2.0 Host Controller Bring-up (Phase D2-A)
 * Strictly READ-ONLY definitions and accessors for MMIO sanity probe.
 */

#ifndef _PEXPERT_ARM_XZS_UFS_H
#define _PEXPERT_ARM_XZS_UFS_H

#include <stdint.h>

/*
 * MSM8996 Audited Physical Base Addresses
 * Derived from msm8996.dtsi:2144 (ufshc) and 2194 (ufsphy)
 */
#define XZS_UFS_CONTROLLER_PHYS_BASE    0x00624000UL
#define XZS_UFS_CONTROLLER_MMIO_SIZE    0x2500UL        /* 9.25 KB */

#define XZS_UFS_PHY_PHYS_BASE           0x00627000UL
#define XZS_UFS_PHY_MMIO_SIZE           0x1000UL        /* 4 KB */

#define XZS_UFS_GIC_SPI_IRQ             265             /* INTID 297 (GIC SPI 265) */

/*
 * Standard JEDEC UFSHCI Register Offsets (Core Region: 0x000 - 0x09F)
 * Reference: JEDEC JESD223B, Linux include/ufs/ufshci.h, ARM TF-A include/drivers/ufs.h
 */
#define UFSHCI_REG_CAP                  0x00    /* Controller Capabilities */
#define UFSHCI_REG_VER                  0x08    /* UFSHCI Version */
#define UFSHCI_REG_HCPID                0x10    /* Controller Product ID / Device ID */
#define UFSHCI_REG_HCMID                0x14    /* Controller Manufacturer ID */
#define UFSHCI_REG_AHIT                 0x18    /* Auto-Hibernate Idle Timer */
#define UFSHCI_REG_IS                   0x20    /* Interrupt Status */
#define UFSHCI_REG_IE                   0x24    /* Interrupt Enable */
#define UFSHCI_REG_HCS                  0x30    /* Host Controller Status */
#define UFSHCI_REG_HCE                  0x34    /* Host Controller Enable */
#define UFSHCI_REG_UECPA                0x38    /* UIC Error Code: PHY Adapter */
#define UFSHCI_REG_UECDL                0x3C    /* UIC Error Code: Data Link */
#define UFSHCI_REG_UECN                 0x40    /* UIC Error Code: Network */
#define UFSHCI_REG_UECT                 0x44    /* UIC Error Code: Transport */
#define UFSHCI_REG_UECDME               0x48    /* UIC Error Code: DME */
#define UFSHCI_REG_UTRIACR              0x4C    /* UTP Transfer Req Interrupt Aggregation Control */
#define UFSHCI_REG_UTRLBA               0x50    /* UTP Transfer Req List Base Address Lower 32 */
#define UFSHCI_REG_UTRLBAU              0x54    /* UTP Transfer Req List Base Address Upper 32 */
#define UFSHCI_REG_UTRLDBR              0x58    /* UTP Transfer Req Doorbell */
#define UFSHCI_REG_UTRLCLR              0x5C    /* UTP Transfer Req Clear */
#define UFSHCI_REG_UTRLRSR              0x60    /* UTP Transfer Req Run/Stop */
#define UFSHCI_REG_UTMRLBA              0x70    /* UTP Task Mgmt Req List Base Lower 32 */
#define UFSHCI_REG_UTMRLBAU             0x74    /* UTP Task Mgmt Req List Base Upper 32 */
#define UFSHCI_REG_UTMRLDBR             0x78    /* UTP Task Mgmt Req Doorbell */
#define UFSHCI_REG_UTMRLCLR             0x7C    /* UTP Task Mgmt Req Clear */
#define UFSHCI_REG_UTMRLRSR             0x80    /* UTP Task Mgmt Req Run/Stop */
#define UFSHCI_REG_UICCMD               0x90    /* UIC Command Opcode */
#define UFSHCI_REG_UCMDARG1             0x94    /* UIC Command Argument 1 */
#define UFSHCI_REG_UCMDARG2             0x98    /* UIC Command Argument 2 */
#define UFSHCI_REG_UCMDARG3             0x9C    /* UIC Command Argument 3 */

/*
 * Audited Qualcomm Extension Register Offsets (Region: >= 0x0A0)
 * Reference: Linux drivers/ufs/host/ufs-qcom.h
 */
#define UFS_QCOM_REG_CFG1               0xDC    /* Qualcomm UFS Configuration 1 */
#define UFS_QCOM_REG_CFG2               0xE0    /* Qualcomm UFS Configuration 2 */
#define UFS_QCOM_REG_HW_VER             0xE4    /* Qualcomm Hardware Version */

#define UFS_QCOM_CFG1_QUNIPRO_SEL       (1U << 0)       /* 1 = QUniPro, 0 = Standard UniPro */
#define UFS_QCOM_CFG1_PHY_SOFT_RESET    (1U << 1)       /* 1 = Assert PHY reset, 0 = Deassert */

/*
 * Bit definitions for Host Controller Status (HCS @ 0x30)
 */
#define UFSHCI_HCS_DP                   (1U << 0)       /* Device Present */
#define UFSHCI_HCS_UTRLRDY              (1U << 1)       /* UTP Transfer Request List Ready */
#define UFSHCI_HCS_UTMRLRDY             (1U << 2)       /* UTP Task Management Request List Ready */
#define UFSHCI_HCS_UCRDY                (1U << 3)       /* UIC Command Ready */

/*
 * Bit definitions for Host Controller Enable (HCE @ 0x34)
 */
#define UFSHCI_HCE_ENABLE               (1U << 0)       /* Host Controller Enable */
#define UFSHCI_HCE_DISABLE              (0U)

/*
 * ============================================================================
 * Phase D2-A.1: MSM8996 GCC UFS Prerequisite Register Definitions
 * Reference: Linux drivers/clk/qcom/gcc-msm8996.c & msm8996.dtsi
 * ============================================================================
 */
#define XZS_GCC_PHYS_BASE               0x00300000UL
#define XZS_GCC_MMIO_SIZE               0x90000UL       /* 576 KB */

#define GCC_REG_UFS_BCR                 0x75000UL       /* UFS Block Reset (gcc-msm8996.c:3575) */
#define GCC_REG_UFS_GDSC                0x75004UL       /* UFS Power Domain GDSCR (gcc-msm8996.c:3270) */
#define GCC_REG_UFS_AXI_CBCR            0x75008UL       /* UFS Core AXI Clock CBCR (gcc-msm8996.c:2643) */
#define GCC_REG_UFS_AHB_CBCR            0x7500cUL       /* UFS AHB Interface Clock CBCR (gcc-msm8996.c:2660) */
#define GCC_REG_UFS_AXI_CMD_RCGR        0x75024UL       /* UFS AXI Root Clock Generator CMD (gcc-msm8996.c:1131) */
#define GCC_REG_UFS_AXI_CFG_RCGR        0x75028UL       /* UFS AXI Root Clock Generator CFG (clk-rcg.h) */
#define GCC_REG_SYS_NOC_UFS_AXI_CBCR    0x75038UL       /* Sys NoC UFS AXI Clock CBCR (gcc-msm8996.c:1203) */
#define GCC_REG_AGGRE2_NOC_BCR          0x83000UL       /* Aggre2 NoC Reset BCR (gcc-msm8996.c:3580) */
#define GCC_REG_AGGRE2_UFS_AXI_CBCR     0x83014UL       /* Aggre2 UFS AXI Clock CBCR (gcc-msm8996.c:2938) */
#define GCC_REG_UFS_CLKREF_CBCR         0x88008UL       /* UFS Clock Reference CBCR (msm8996.dtsi:2196, GCC 215) */

/*
 * CBCR Bit Definitions (clk-branch.h:71)
 */
#define CBCR_CLK_OFF                    (1U << 31)      /* 1 = Halted / Inactive, 0 = Running */
#define CBCR_CLOCK_ENABLE               (1U << 0)       /* 1 = Enabled, 0 = Disabled */

/*
 * GDSCR Bit Definitions (gdsc.h:40)
 */
#define GDSCR_PWR_ON_STATUS             (1U << 31)      /* 1 = Powered ON, 0 = Off / Collapsed */
#define GDSCR_SW_COLLAPSE_REQ           (1U << 0)       /* 1 = Collapse requested, 0 = On requested */

/*
 * BCR Bit Definitions (reset.c)
 */
#define BCR_BLK_ARES                    (1U << 0)       /* 1 = Asserted (in reset), 0 = Released */

/*
 * ============================================================================
 * Phase D2-C: Qualcomm MSM8996 UFS QMP 14nm PHY Register Definitions
 * Reference: Linux drivers/phy/qualcomm/phy-qcom-ufs-qmp-14nm.h,
 *            phy-qcom-qmp-ufs.c, phy-qcom-qmp-pcs-ufs-v2.h
 * ============================================================================
 */
#define QPHY_REG_START_CTRL             0xC00UL   /* QPHY_START_CTRL (PCS + 0x000) */
#define QPHY_REG_PCS_POWER_DOWN_CONTROL 0xC04UL   /* QPHY_PCS_POWER_DOWN_CONTROL (PCS + 0x004) */
#define QPHY_REG_PCS_READY_STATUS       0xD68UL   /* QPHY_PCS_READY_STATUS (PCS + 0x168) */

#define QPHY_START_CTRL_SERDES_START    (1U << 0) /* 1 = Start SerDes */
#define QPHY_PCS_PWRDN_ACTIVE           (1U << 0) /* 1 = Powered Up / Active, 0 = Power-down */
#define QPHY_PCS_READY_BIT              (1U << 0) /* 1 = PCS Ready */
#define QSERDES_COM_REG_RESETSM_CNTRL   0x0B4UL   /* Reset state machine control */
#define QSERDES_COM_REG_LOCK_CMP_EN     0x0C8UL   /* Lock compare enable (config) */
#define QSERDES_COM_REG_C_READY_STATUS  0x190UL   /* Common block PLL ready status (bit 0 = C_READY) */
#define QSERDES_COM_REG_CMN_CONFIG      0x194UL   /* Common block configuration */

#define QSERDES_COM_C_READY_BIT         (1U << 0) /* 1 = Common PLL locked / ready */

/*
 * Function Prototypes
 */
uint32_t xzs_ufs_read32(uint32_t offset);
void xzs_ufs_write32(uint32_t offset, uint32_t val);
uint32_t xzs_ufs_phy_read32(uint32_t offset);
void xzs_ufs_phy_write32(uint32_t offset, uint32_t val);
uint32_t xzs_gcc_read32(uint32_t offset);
void xzs_gcc_write32(uint32_t offset, uint32_t val);
void xzs_ufs_phase_d2_probe(void);
void xzs_ufs_phase_d2a1_probe(void);
void xzs_ufs_phase_d2b1_probe(void);
void xzs_ufs_phase_d2b2_probe(void);
void xzs_ufs_phase_d2c1_probe(void);
void xzs_ufs_phase_d2c2_probe(void);
void xzs_ufs_phase_d2c21_probe(void);
void xzs_ufs_phase_d2c22_probe(void);
void xzs_ufs_phase_d2c23_probe(void);
void xzs_ufs_phase_d2c24a_probe(void);
int xzs_ufs_phy_retest_d2c24c(uint32_t *out_c_ready, uint32_t *out_pcs_ready,
                             int *out_c_ready_us, int *out_pcs_ready_us);
int xzs_ufs_phy_retest_d2c25(uint32_t *out_c_ready, uint32_t *out_pcs_ready_d74,
                            uint32_t *out_pcs_ready_d68, int *out_c_ready_us,
                            int *out_pcs_ready_us);

#endif /* _PEXPERT_ARM_XZS_UFS_H */


