#ifndef _DUMPER_UART_H_
#define _DUMPER_UART_H_

#include <stdint.h>
#include <stddef.h>

#define XZS_DEBUG_LOG_BASE   0x80060000UL
#define XZS_RAMOOPS_BASE     0xa7f00000UL
#define XZS_DEBUG_LOG_MAGIC  0x585a5344UL /* "XZSD" */
#define XZS_DEBUG_LOG_SIZE   65472

enum xzs_stage {
    XZS_STAGE_NONE    = 0,

    /* Bootshim stages */
    XZS_STAGE_SHIM_A  = 0x100, /* Entry */
    XZS_STAGE_SHIM_B  = 0x101, /* DTB located */
    XZS_STAGE_SHIM_C  = 0x102, /* Mach-O parsed */
    XZS_STAGE_SHIM_D  = 0x103, /* Mach-O flattened / entry extracted */
    XZS_STAGE_SHIM_E  = 0x104, /* ADT constructed */
    XZS_STAGE_SHIM_F  = 0x105, /* Boot args prepared */
    XZS_STAGE_SHIM_G  = 0x106, /* Jumping to XNU */

    /* XNU kernel stages */
    XZS_STAGE_XNU_K0  = 0x200, /* Kernel entry (start_first_cpu) */
    XZS_STAGE_XNU_K1  = 0x201, /* Exception vectors installed */
    XZS_STAGE_XNU_K2  = 0x202, /* Early bootstrap page tables mapped */
    XZS_STAGE_XNU_K3  = 0x203, /* TCR_EL1 configured */
    XZS_STAGE_XNU_K4  = 0x204, /* Before SCTLR.M enable */
    XZS_STAGE_XNU_K5  = 0x205, /* After SCTLR.M enable */
    XZS_STAGE_XNU_K6  = 0x206, /* arm_init entered */
    XZS_STAGE_XNU_K7  = 0x207, /* pmap bootstrap initialized */
};

struct xzs_debug_log {
    uint32_t magic;           /* 0x00: 0x585a5344 ("XZSD") */
    uint32_t version;         /* 0x04: 2 */
    uint32_t write_offset;    /* 0x08 */
    uint32_t boot_count;      /* 0x0c */
    uint32_t exception_count; /* 0x10 */
    uint32_t last_stage;      /* 0x14: enum xzs_stage */
    uint64_t last_elr;        /* 0x18 */
    uint64_t last_esr;        /* 0x20 */
    uint64_t last_far;        /* 0x28 */
    uint64_t last_spsr;       /* 0x30 */
    uint8_t  reserved[8];     /* 0x38 */
    char data[XZS_DEBUG_LOG_SIZE]; /* 0x40 */
};

void uart_putc(char c);
void uart_puts(const char *s);
void uart_puthex64(uint64_t value);
void uart_putdec(uint64_t val);
const char *xzs_stage_to_string(uint32_t stage);

#endif /* _DUMPER_UART_H_ */
