/*
 * Copyright (c) 2026 Apple Inc. All rights reserved.
 *
 * XZS RPM Driver — Minimal RPM/GLINK Transport for MSM8996 / PM8994
 * Phase D2-C2.4D: PM8994 RPM/SMD Regulator Ownership + Controlled L28 Vote
 */

#ifndef _PEXPERT_ARM_XZS_RPM_H_
#define _PEXPERT_ARM_XZS_RPM_H_

#include <stdint.h>
#include <stdbool.h>

#define RPM_MSGRAM_PHYS_BASE        0x00068000ULL
#define RPM_MSGRAM_SIZE             0x00006000ULL   /* 24 KB */
#define RPM_APCS_IPC_PHYS_BASE      0x09820000ULL
#define RPM_APCS_IPC_REG_OFFSET     0x00000010ULL   /* 0x09820010 */
#define RPM_APCS_IPC_MASK           0x00000001U     /* Bit 0 */

/* RPM Resource Constants */
#define QCOM_SMD_RPM_LDOA           0x616f646cU     /* "ldoa" little-endian */
#define QCOM_SMD_RPM_CLK_BUF_A      0x616b6c63U     /* "clka" little-endian */
#define RPM_LN_BB_CLK_ID            8U
#define QCOM_RPM_KEY_SWEN           0x6e657773U     /* "swen" */
#define QCOM_RPM_KEY_UV             0x00007675U     /* "uv" */
#define QCOM_RPM_KEY_MA             0x0000616dU     /* "ma" */

#define MSM_RPM_CTX_ACTIVE_SET      0U
#define MSM_RPM_CTX_SLEEP_SET       1U

/* Protocol Version */
#define RPM_PROTOCOL_V1_REV         0x31726576U     /* "rev1" */

/* Function Declarations */
void xzs_rpm_phase_d2c24d_probe(void);
void xzs_rpm_phase_d2c24e_probe(void);
void xzs_rpm_phase_d2c24f_probe(void);
void xzs_rpm_phase_d2c25_probe(void);

#endif /* _PEXPERT_ARM_XZS_RPM_H_ */
