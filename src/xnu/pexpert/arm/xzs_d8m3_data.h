/*
 * Sony Xperia XZs (Keyaki / MSM8996 v3.0)
 * D8-M3: Guarded Real DSI PLL & PHY Bring-Up State Machine & HAL
 *
 * Generated from audited golden Linux display trace (D8-A1) and
 * dry-run state machine specification (D8-A3).
 */

#ifndef _XZS_D8M3_DATA_H_
#define _XZS_D8M3_DATA_H_

#include <stdint.h>

#define M3_WRITE_ALLOWLIST_COUNT 66
#define M3_SNAPSHOT_REGS_COUNT 35
#define M3_TOTAL_WRITES_COUNT 68

struct m3_reg_write {
    uint32_t addr;
    uint32_t val;
    const char *chk;
    const char *desc;
};

/* Strictly verified 66-address MMIO allowlist */
static const uint32_t s_m3_write_allowlist[66] = {
    0x008c2000u, 0x008c2004u, 0x008c2120u, 0x008c2124u,
    0x008c2160u, 0x008c2164u, 0x008c2314u, 0x008c233cu,
    0x008c2344u, 0x0099412cu, 0x00994410u, 0x00994414u,
    0x0099441cu, 0x00994420u, 0x00994440u, 0x00994448u,
    0x0099444cu, 0x009944c0u, 0x00994540u, 0x00994564u,
    0x009945c0u, 0x009945e4u, 0x00994640u, 0x00994664u,
    0x009946e4u, 0x00994764u, 0x00994800u, 0x00994804u,
    0x00994810u, 0x00994828u, 0x0099482cu, 0x00994830u,
    0x0099483cu, 0x00994840u, 0x00994844u, 0x00994848u,
    0x0099484cu, 0x0099485cu, 0x0099486cu, 0x00994870u,
    0x00994874u, 0x00994878u, 0x0099487cu, 0x00994880u,
    0x00994884u, 0x00994888u, 0x00994890u, 0x00994894u,
    0x00994898u, 0x0099489cu, 0x009948a0u, 0x009948a4u,
    0x009948a8u, 0x009948acu, 0x009948b4u, 0x009948b8u,
    0x009948bcu, 0x009948c0u, 0x009948c4u, 0x009948e8u,
    0x009948f0u, 0x009948f4u, 0x009948f8u, 0x009948fcu,
    0x00994900u, 0x00994904u
};

/* 35 snapshot registers for diffing against golden Linux */
static const uint32_t s_m3_snapshot_regs[35] = {
    0x008c2004u, 0x008c2124u, 0x008c2164u, 0x008c2314u,
    0x008c233cu, 0x008c2344u, 0x00994000u, 0x0099400cu,
    0x00994014u, 0x00994018u, 0x009940f0u, 0x00994110u,
    0x009941b4u, 0x009941b8u, 0x009941f4u, 0x009942a0u,
    0x00994410u, 0x00994440u, 0x00994448u, 0x0099444cu,
    0x00994564u, 0x009945e4u, 0x00994664u, 0x009946e4u,
    0x00994764u, 0x00994800u, 0x0099483cu, 0x00994840u,
    0x00994850u, 0x0099485cu, 0x0099486cu, 0x00994870u,
    0x00994874u, 0x00994878u, 0x009948ccu
};

/* 68 sequential MMIO writes */
static const struct m3_reg_write s_m3_writes[68] = {
    { 0x00994564u, 0x0000001du, "D8M3-A0", "Data lane 0 LDO bias" },
    { 0x009945e4u, 0x0000001du, "D8M3-A0", "Data lane 1 LDO bias" },
    { 0x00994664u, 0x0000001du, "D8M3-A0", "Data lane 2 LDO bias" },
    { 0x009946e4u, 0x0000001du, "D8M3-A0", "Data lane 3 LDO bias" },
    { 0x00994764u, 0x0000001du, "D8M3-A0", "Clock lane LDO bias" },
    { 0x0099412cu, 0x00000001u, "D8M3-A0", "Assert PHY soft reset" },
    { 0x0099412cu, 0x00000000u, "D8M3-A0", "Deassert PHY soft reset" },
    { 0x0099444cu, 0x00000001u, "D8M3-20", "Post-N1 divider = 2" },
    { 0x00994448u, 0x00000000u, "D8M3-30", "Clear ext clk override" },
    { 0x00994800u, 0x00000004u, "D8M3-30", "Sysclk enable & reset" },
    { 0x00994804u, 0x00000004u, "D8M3-30", "Enable Tx clock channels" },
    { 0x00994810u, 0x00000007u, "D8M3-30", "Reset SM control 3" },
    { 0x00994828u, 0x00000000u, "D8M3-30", "Reset SM counter initial" },
    { 0x0099482cu, 0x00000030u, "D8M3-30", "PLL backup control (0x30)" },
    { 0x00994830u, 0x00000020u, "D8M3-30", "Kvco divider reference 1 (0x20)" },
    { 0x0099483cu, 0x00000005u, "D8M3-30", "Lock cmp low byte / kvco ref 2 (0x05)" },
    { 0x00994840u, 0x0000005fu, "D8M3-30", "Lock cmp high byte / kvco div ref 1 (0x5f)" },
    { 0x00994844u, 0x00000000u, "D8M3-30", "Lock cmp upper byte / kvco div ref 2" },
    { 0x00994848u, 0x0000003cu, "D8M3-30", "Kvco count 1 (60 = 0x3c)" },
    { 0x0099484cu, 0x00000000u, "D8M3-30", "Kvco count 2 (0x00)" },
    { 0x0099486cu, 0x0000005fu, "D8M3-30", "VCO divider ref 1 (0x5f)" },
    { 0x00994870u, 0x00000000u, "D8M3-30", "VCO divider ref 2 (0x00)" },
    { 0x00994874u, 0x00000081u, "D8M3-30", "VCO count 1 (0x381 & 0xff = 0x81)" },
    { 0x00994878u, 0x00000003u, "D8M3-30", "VCO count 2 (0x381 >> 8 = 0x03)" },
    { 0x0099487cu, 0x00000058u, "D8M3-30", "Lock cmp byte 0 (0x958 & 0xff)" },
    { 0x00994880u, 0x00000009u, "D8M3-30", "Lock cmp byte 1 (0x958 >> 8)" },
    { 0x00994884u, 0x00000000u, "D8M3-30", "Lock cmp byte 2 ((0x958 >> 16) & 3)" },
    { 0x00994888u, 0x00000002u, "D8M3-30", "PLL lock cmp config (0x488 = 2)" },
    { 0x00994890u, 0x0000005du, "D8M3-30", "Integer decimation start (93)" },
    { 0x00994894u, 0x00000000u, "D8M3-30", "SSC disable (SSC_EN_CENTER = 0)" },
    { 0x00994898u, 0x00000000u, "D8M3-30", "SSC adj period 1" },
    { 0x0099489cu, 0x00000000u, "D8M3-30", "SSC adj period 2" },
    { 0x009948a0u, 0x00000000u, "D8M3-30", "SSC period 1" },
    { 0x009948a4u, 0x00000000u, "D8M3-30", "SSC period 2" },
    { 0x009948a8u, 0x00000000u, "D8M3-30", "SSC step size 1" },
    { 0x009948acu, 0x00000000u, "D8M3-30", "SSC step size 2" },
    { 0x009948b4u, 0x000000cbu, "D8M3-30", "Frac start byte 0 (0x710cb & 0xff)" },
    { 0x009948b8u, 0x00000010u, "D8M3-30", "Frac start byte 1 (0x710cb >> 8)" },
    { 0x009948bcu, 0x00000007u, "D8M3-30", "Frac start byte 2 (0x710cb >> 16)" },
    { 0x009948c0u, 0x00000001u, "D8M3-30", "Calibration config 4" },
    { 0x009948c4u, 0x00000012u, "D8M3-30", "Calibration config 5" },
    { 0x009948e8u, 0x00000010u, "D8M3-30", "Calibration config 6" },
    { 0x009948f0u, 0x00000009u, "D8M3-30", "Calibration config 7" },
    { 0x009948f4u, 0x00000000u, "D8M3-30", "Calibration config 8" },
    { 0x009948f8u, 0x00000000u, "D8M3-30", "Calibration config 9" },
    { 0x009948fcu, 0x00000024u, "D8M3-30", "Calibration config 10" },
    { 0x00994900u, 0x0000001bu, "D8M3-30", "Calibration config 11" },
    { 0x00994904u, 0x00000003u, "D8M3-30", "Efuse config 1 (0x03)" },
    { 0x00994410u, 0x0000001eu, "D8M3-30", "PHY common control 0" },
    { 0x00994414u, 0x00000001u, "D8M3-30", "PHY common control 1" },
    { 0x0099441cu, 0x000000ffu, "D8M3-30", "PHY common control 2" },
    { 0x00994420u, 0x00000000u, "D8M3-30", "PHY common control 3" },
    { 0x0099485cu, 0x00000010u, "D8M3-40", "Common PLL output buffer enable" },
    { 0x00994448u, 0x00000001u, "D8M3-40", "PLL clock generation kick" },
    { 0x008c2124u, 0x00000100u, "D8M3-70", "Select DSI0 PLL Byte Mux" },
    { 0x008c2120u, 0x00000001u, "D8M3-70", "Trigger Byte0 update" },
    { 0x008c233cu, 0x00000001u, "D8M3-70", "Enable mdss_byte0_clk" },
    { 0x008c2004u, 0x00000200u, "D8M3-80", "Select DSI0 PLL Pixel Mux" },
    { 0x008c2000u, 0x00000001u, "D8M3-80", "Trigger Pclk0 update" },
    { 0x008c2314u, 0x00000001u, "D8M3-80", "Enable mdss_pclk0_clk" },
    { 0x008c2164u, 0x00000000u, "D8M3-90", "Select XO as Esc0 source" },
    { 0x008c2160u, 0x00000001u, "D8M3-90", "Trigger Esc0 update" },
    { 0x008c2344u, 0x00000001u, "D8M3-90", "Enable mdss_esc0_clk" },
    { 0x00994440u, 0x000006ffu, "D8M3-B0", "Data lane 0 drive strength" },
    { 0x009944c0u, 0x000006ffu, "D8M3-B0", "Data lane 1 drive strength" },
    { 0x00994540u, 0x000006ffu, "D8M3-B0", "Data lane 2 drive strength" },
    { 0x009945c0u, 0x000006ffu, "D8M3-B0", "Data lane 3 drive strength" },
    { 0x00994640u, 0x000000ffu, "D8M3-B0", "Clock lane drive strength" }
};

#endif /* _XZS_D8M3_H_ */
