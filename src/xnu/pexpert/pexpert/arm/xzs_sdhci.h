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
 * SDHCI Power Control Bits (relative to HC offset 0x29)
 */
#define SDHCI_POWER_ON                  0x01U
#define SDHCI_POWER_180                 0x0AU
#define SDHCI_POWER_300                 0x0CU
#define SDHCI_POWER_330                 0x0EU
#define ABOOT_POWER_FIRST_WRITE         SDHCI_POWER_180
#define ABOOT_POWER_SECOND_WRITE        (SDHCI_POWER_180 | SDHCI_POWER_ON)
#define ABOOT_POWER_FINAL_VAL           0x0BU

/*
 * SDHCI Clock Control Bits (relative to HC offset 0x2C)
 */
#define SDHCI_CLOCK_INT_EN              0x0001U
#define SDHCI_CLOCK_INT_STABLE          0x0002U
#define SDHCI_CLOCK_CARD_EN             0x0004U
#define ABOOT_CLOCK_FIRST_WRITE         SDHCI_CLOCK_INT_EN
#define ABOOT_CLOCK_FINAL_VAL           (SDHCI_CLOCK_INT_EN | SDHCI_CLOCK_INT_STABLE | SDHCI_CLOCK_CARD_EN)

/*
 * SDHCI Timeout & Host Control Constants
 */
#define ABOOT_TIMEOUT_VAL               0x0FU
#define SDHCI_CTRL_4BITBUS              0x02U
#define SDHCI_CTRL_ADMA32               0x10U
#define SDHCI_CTRL_8BITBUS              0x20U
#define SDHCI_CTRL_1BIT_INIT            0x00U

/*
 * SDHCI Present State Bits
 */
#define SDHCI_CMD_INHIBIT               (1U << 0)
#define SDHCI_DATA_INHIBIT              (1U << 1)

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
 * SDHCI Command & Argument Registers (relative to HC Base: 0x07464900)
 */
#define SDHCI_ARGUMENT                  0x08U
#define SDHCI_TRANSFER_MODE             0x0CU
#define SDHCI_COMMAND                   0x0EU

#define SDHCI_CMD_RESP_MASK             0x03U
#define SDHCI_CMD_RESP_NONE             0x00U
#define SDHCI_CMD_RESP_136              0x01U
#define SDHCI_CMD_RESP_48               0x02U
#define SDHCI_CMD_RESP_48_BUSY          0x03U
#define SDHCI_CMD_CRC                   (1U << 3)
#define SDHCI_CMD_INDEX                 (1U << 4)
#define SDHCI_CMD_DATA                  (1U << 5)
#define SDHCI_MAKE_CMD(c, f)            ((uint16_t)(((c) << 8) | (f)))

/*
 * SDHCI Transfer Mode Bits (relative to HC offset 0x0C)
 */
#define SDHCI_TRNS_DMA                  0x01U
#define SDHCI_TRNS_BLK_CNT_EN           0x02U
#define SDHCI_TRNS_AUTO_CMD12           0x04U
#define SDHCI_TRNS_AUTO_CMD23           0x08U
#define SDHCI_TRNS_READ                 0x10U
#define SDHCI_TRNS_MULTI                0x20U

/*
 * SDHCI Interrupt Registers & Status Bits (relative to HC Base: 0x07464900)
 */
#define SDHCI_INT_STATUS                0x30U
#define SDHCI_INT_ENABLE                0x34U
#define SDHCI_SIGNAL_ENABLE             0x38U

#define SDHCI_INT_RESPONSE              (1U << 0)  /* Command Complete */
#define SDHCI_INT_DATA_END              (1U << 1)  /* Transfer Complete */
#define SDHCI_INT_BUF_WRITE_READY       (1U << 4)  /* Buffer Write Ready */
#define SDHCI_INT_BUF_READ_READY        (1U << 5)  /* Buffer Read Ready */
#define SDHCI_INT_ERROR                 (1U << 15) /* Error Interrupt */
#define SDHCI_INT_TIMEOUT               (1U << 16) /* Command Timeout Error (CTO) */
#define SDHCI_INT_CRC                   (1U << 17) /* Command CRC Error (CCRC) */
#define SDHCI_INT_END_BIT               (1U << 18) /* Command End Bit Error (CEND) */
#define SDHCI_INT_INDEX                 (1U << 19) /* Command Index Error (CINDEX) */
#define SDHCI_INT_DATA_TIMEOUT          (1U << 20) /* Data Timeout Error (DTO) */
#define SDHCI_INT_DATA_CRC              (1U << 21) /* Data CRC Error (DCRC) */
#define SDHCI_INT_DATA_END_BIT          (1U << 22) /* Data End Bit Error (DEBE) */
#define SDHCI_INT_BUS_POWER             (1U << 23) /* Bus Power Error */
#define SDHCI_INT_AUTO_CMD_ERR          (1U << 24) /* Auto CMD Error */
#define SDHCI_INT_ADMA_ERROR            (1U << 25) /* ADMA Error */

#define SDHCI_INT_CMD_ERR_MASK          (SDHCI_INT_TIMEOUT | SDHCI_INT_CRC | SDHCI_INT_END_BIT | SDHCI_INT_INDEX | SDHCI_INT_BUS_POWER)
#define SDHCI_INT_DATA_ERR_MASK         (SDHCI_INT_DATA_TIMEOUT | SDHCI_INT_DATA_CRC | SDHCI_INT_DATA_END_BIT | SDHCI_INT_ADMA_ERROR)
#define SDHCI_INT_ALL_MASK              0xFFFFFFFFU

/*
 * SDHCI Response Registers (relative to HC Base: 0x07464900)
 */
#define SDHCI_RESPONSE_0                0x10U
#define SDHCI_RESPONSE_1                0x14U
#define SDHCI_RESPONSE_2                0x18U
#define SDHCI_RESPONSE_3                0x1CU

/*
 * Function Prototypes
 */
uint32_t xzs_sdhci_hc_read32(uint32_t offset);
uint16_t xzs_sdhci_hc_read16(uint32_t offset);
uint8_t  xzs_sdhci_hc_read8(uint32_t offset);

void xzs_sdhci_hc_write32(uint32_t offset, uint32_t val);
void xzs_sdhci_hc_write16(uint32_t offset, uint16_t val);
void xzs_sdhci_hc_write8(uint32_t offset, uint8_t val);

uint32_t xzs_sdhci_core_read32(uint32_t offset);
void xzs_sdhci_core_write32(uint32_t offset, uint32_t val);

void xzs_sdhci_phase_d2m1_probe(void);
void xzs_sdhci_phase_d2m2_probe(void);
void xzs_sdhci_phase_d2m3_probe(void);
void xzs_sdhci_phase_d2m4a_probe(void);
void xzs_sdhci_phase_d2m4b_probe(void);
void xzs_sdhci_phase_d2m4c_probe(void);
/*
 * eMMC R1 Response Bit Definitions & Error Reject Mask (JEDEC JESD84-B51)
 */
#define R1_OUT_OF_RANGE                 (1U << 31)
#define R1_ADDRESS_ERROR                (1U << 30)
#define R1_BLOCK_LEN_ERROR              (1U << 29)
#define R1_ERASE_SEQ_ERROR              (1U << 28)
#define R1_ERASE_PARAM                  (1U << 27)
#define R1_WP_VIOLATION                 (1U << 26)
#define R1_CARD_IS_LOCKED               (1U << 25)
#define R1_LOCK_UNLOCK_FAILED           (1U << 24)
#define R1_COM_CRC_ERROR                (1U << 23)
#define R1_ILLEGAL_COMMAND              (1U << 22)
#define R1_CARD_ECC_FAILED              (1U << 21)
#define R1_CC_ERROR                     (1U << 20)
#define R1_ERROR                        (1U << 19)
#define R1_CID_CSD_OVERWRITE            (1U << 16)
#define R1_SWITCH_ERROR                 (1U << 7)

#define MMC_R1_REJECT_MASK ( \
	R1_OUT_OF_RANGE       | \
	R1_ADDRESS_ERROR      | \
	R1_BLOCK_LEN_ERROR    | \
	R1_ERASE_SEQ_ERROR    | \
	R1_ERASE_PARAM        | \
	R1_WP_VIOLATION       | \
	R1_CARD_IS_LOCKED     | \
	R1_LOCK_UNLOCK_FAILED | \
	R1_COM_CRC_ERROR      | \
	R1_ILLEGAL_COMMAND    | \
	R1_CARD_ECC_FAILED    | \
	R1_CC_ERROR           | \
	R1_ERROR              | \
	R1_CID_CSD_OVERWRITE  | \
	R1_SWITCH_ERROR)

#define MMC_R1_CURRENT_STATE_MASK       0x00001E00U
#define MMC_R1_CURRENT_STATE_SHIFT      9
#define MMC_STATE_STBY                  3

void xzs_sdhci_phase_d2m4c1_probe(void);
void xzs_sdhci_phase_d2m4da_probe(void);
void xzs_sdhci_phase_d2m4db_probe(void);
void xzs_sdhci_phase_d2m4dc_probe(void);
void xzs_sdhci_phase_d2m4dd_probe(void);
void xzs_sdhci_phase_d2m4e_probe(void);
void xzs_sdhci_phase_d2m5_probe(void);

/*
 * Phase D3: GPT Header & Partition Parsing Declarations & Invariants
 */
#define GPT_SIGNATURE_MAGIC             0x5452415020494645ULL /* "EFI PART" in little-endian */
#define GPT_REVISION_1_0                0x00010000U
#define GPT_MIN_HEADER_SIZE             92U
#define GPT_MAX_HEADER_SIZE             512U
#define GPT_MIN_ENTRY_SIZE              128U

int  xzs_emmc_read_sector_pio(uint32_t lba, uint64_t validated_sector_count, uint8_t out[512]);
void xzs_sdhci_phase_d3m1_probe(void);

/*
 * Phase D3-M2A: Primary GPT Partition Entry Array Declarations
 */
#define GPT_PRIMARY_ARRAY_BUFFER_CAPACITY 16384U

void xzs_sdhci_phase_d3m2a_probe(void);

/*
 * Phase D3-M2B: Primary GPT Partition Map Parsing Declarations
 */
#define GPT_MAX_ENTRY_SLOTS             128U

void xzs_sdhci_phase_d3m2b_probe(void);

/*
 * Phase D3-M3: Backup GPT Header & Partition Entry Array Verification
 */
void xzs_sdhci_phase_d3m3_probe(void);

/*
 * Phase D4-M1: Persistent eMMC Runtime Context & Multi-Sector Read Pipeline
 */
typedef struct xzs_emmc_context {
	bool      initialized;

	uint16_t  rca;

	uint64_t  sector_count;
	uint64_t  last_physical_lba;

	uint32_t  sector_size;

	uint8_t   ext_csd_rev;

	uint32_t  initialization_count;
	uint32_t  initialization_reuse_count;
	uint32_t  controller_reset_count;

	uint64_t  successful_sector_reads;
	uint64_t  failed_sector_reads;
} xzs_emmc_context_t;

int  xzs_emmc_init_persistent(void);
int  xzs_emmc_read_sector_sync(uint64_t lba, void *out512);
int  xzs_emmc_read_blocks_sync(uint64_t start_lba, uint32_t count, void *buffer);
void xzs_sdhci_phase_d4m1_probe(void);

/*
 * Phase D4-M2: BSD bdevsw Read-Only Block Device Integration
 */
struct buf;
void xzs_sdhci_phase_d4m2_probe(void);
void xzs_buf_set_dev(struct buf *bp, uint32_t dev);

/*
 * Phase D4: Read-Only BSD Block Storage & Integration
 */
void xzs_sdhci_phase_d4_probe(void);
int  xzs_storage_nub_publish(int bsd_major, int bsd_minor);
int  xzs_storage_nub_find_bsd_name(const char *name, char *out_name, size_t out_name_size, int *out_major, int *out_minor);

#endif /* _PEXPERT_ARM_XZS_SDHCI_H */
