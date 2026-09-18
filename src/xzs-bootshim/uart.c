#include "uart.h"

#define BLSP2_UART2_BASE    0x075b0000UL
#define BLSP1_UART2_BASE    0x07570000UL

#define MSM_UART_SR         0x0008
#define MSM_UART_SR_TX_READY (1 << 2)
#define MSM_UART_SR_TX_EMPTY (1 << 3)

#define MSM_UART_CR         0x0010
#define MSM_UART_IMR        0x0014
#define UARTDM_NCF_TX       0x0040
#define UARTDM_TF           0x0070

#define RAMOOPS_LOG_BASE    0xa7f00000UL
#define LOW_DRAM_LOG_BASE   0x80060000UL
#define LOG_MAGIC           0x585a5342UL /* "XZSB" */

static inline void mmio_write32(uintptr_t addr, uint32_t val) {
    *(volatile uint32_t *)addr = val;
}

static inline uint32_t mmio_read32(uintptr_t addr) {
    return *(volatile uint32_t *)addr;
}

#define PSTORE_DMESG_BASE   0xa7f00000UL
#define PSTORE_CONSOLE_BASE 0xa7fbe000UL
#define PSTORE_SIG          0x43474244UL /* "DBGC" (Linux PERSISTENT_RAM_SIG) */

struct pstore_dmesg_buffer {
    uint32_t sig;     /* 0x00: 0x43474244 ("DBGC") */
    uint32_t start;   /* 0x04: start */
    uint32_t size;    /* 0x08: text length */
    char     data[4096 - 12]; /* 0x0c: 4084 bytes max */
};

struct pstore_console_buffer {
    uint32_t sig;     /* 0x00: 0x43474244 ("DBGC") */
    uint32_t start;   /* 0x04: start */
    uint32_t size;    /* 0x08: text length */
    char     data[256 * 1024 - 12]; /* 0x0c: 256KB console zone */
};

static volatile struct pstore_dmesg_buffer *pstore_dmesg = (volatile struct pstore_dmesg_buffer *)PSTORE_DMESG_BASE;
static volatile struct pstore_console_buffer *pstore_console = (volatile struct pstore_console_buffer *)PSTORE_CONSOLE_BASE;

volatile struct xzs_debug_log *g_dlog = (volatile struct xzs_debug_log *)XZS_DEBUG_LOG_BASE;

const char *xzs_stage_to_string(uint32_t stage) {
    switch (stage) {
        case XZS_STAGE_SHIM_A: return "SHIM_A (Entry)";
        case XZS_STAGE_SHIM_B: return "SHIM_B (DTB Located)";
        case XZS_STAGE_SHIM_C: return "SHIM_C (Mach-O Parsed)";
        case XZS_STAGE_SHIM_D: return "SHIM_D (Mach-O Flattened)";
        case XZS_STAGE_SHIM_E: return "SHIM_E (ADT Constructed)";
        case XZS_STAGE_SHIM_F: return "SHIM_F (Boot Args Ready)";
        case XZS_STAGE_SHIM_G: return "SHIM_G (Jumping to XNU)";
        case XZS_STAGE_XNU_K0: return "XNU_K0 (Kernel Entry)";
        case XZS_STAGE_XNU_K1: return "XNU_K1 (Vectors Installed)";
        case XZS_STAGE_XNU_K2: return "XNU_K2 (Page Tables Prepared)";
        case XZS_STAGE_XNU_K3: return "XNU_K3 (TCR Configured)";
        case XZS_STAGE_XNU_K4: return "XNU_K4 (Before MMU Enable)";
        case XZS_STAGE_XNU_K5: return "XNU_K5 (After MMU Enable)";
        case XZS_STAGE_XNU_K6: return "XNU_K6 (arm_init Entered)";
        case XZS_STAGE_XNU_K7: return "XNU_K7 (pmap Initialized)";
        default: return "UNKNOWN";
    }
}

static void mem_log_putc(char c) {
    /* 1. Structured debug log at 0x80060000 */
    if (g_dlog->magic == XZS_DEBUG_LOG_MAGIC) {
        if (g_dlog->write_offset < XZS_DEBUG_LOG_SIZE - 1) {
            uint32_t idx = g_dlog->write_offset;
            g_dlog->data[idx] = c;
            g_dlog->write_offset = idx + 1;
            g_dlog->data[idx + 1] = '\0';
        }
    }

    /* 2. Persistent RAM pstore console buffer at 0xa7fbe000 (256KB) */
    if (pstore_console->sig != PSTORE_SIG) {
        pstore_console->sig = PSTORE_SIG;
        pstore_console->start = 0;
        pstore_console->size = 0;
    }
    uint32_t sz = pstore_console->size;
    if (sz < sizeof(pstore_console->data) - 1) {
        pstore_console->data[sz] = c;
        pstore_console->size = sz + 1;
    }

    /* 3. Persistent RAM pstore dmesg zone 0 at 0xa7f00000 (4KB) */
    if (pstore_dmesg->sig != PSTORE_SIG) {
        pstore_dmesg->sig = PSTORE_SIG;
        pstore_dmesg->start = 0;
        pstore_dmesg->size = 0;
    }
    uint32_t dsz = pstore_dmesg->size;
    if (dsz < sizeof(pstore_dmesg->data) - 1) {
        pstore_dmesg->data[dsz] = c;
        pstore_dmesg->size = dsz + 1;
    }
}

static void uart_dm_tx_char(uintptr_t base, char c) {
    uint32_t timeout = 500;

    /* Disable interrupts */
    mmio_write32(base + MSM_UART_IMR, 0);

    /* Check if TX is empty */
    if (!(mmio_read32(base + MSM_UART_SR) & MSM_UART_SR_TX_EMPTY)) {
        return;
    }

    /* Tell UARTDM we want to transmit 1 character */
    mmio_write32(base + UARTDM_NCF_TX, 1);
    (void)mmio_read32(base + UARTDM_NCF_TX);

    /* Wait for TX ready */
    timeout = 500;
    while (!(mmio_read32(base + MSM_UART_SR) & MSM_UART_SR_TX_READY)) {
        if (--timeout == 0) return;
    }

    /* Write char (32-bit packed word) */
    mmio_write32(base + UARTDM_TF, (uint32_t)(uint8_t)c);
}

void uart_init(void) {
    /* 1. Initialize structured log buffer at 0x80060000 */
    if (g_dlog->magic != XZS_DEBUG_LOG_MAGIC) {
        g_dlog->magic = XZS_DEBUG_LOG_MAGIC;
        g_dlog->version = 2;
        g_dlog->boot_count = 1;
        g_dlog->exception_count = 0;
        g_dlog->last_stage = XZS_STAGE_NONE;
        g_dlog->last_elr = 0;
        g_dlog->last_esr = 0;
        g_dlog->last_far = 0;
        g_dlog->last_spsr = 0;
        g_dlog->write_offset = 0;
        g_dlog->data[0] = '\0';
    } else {
        g_dlog->boot_count++;
        g_dlog->write_offset = 0;
        g_dlog->data[0] = '\0';
    }

    /* 2. Initialize persistent RAM pstore console buffer at 0xa7fbe000 (256KB) */
    pstore_console->sig = PSTORE_SIG;
    pstore_console->start = 0;
    pstore_console->size = 0;

    /* 3. Initialize persistent RAM pstore dmesg zone 0 buffer at 0xa7f00000 (4KB) */
    pstore_dmesg->sig = PSTORE_SIG;
    pstore_dmesg->start = 0;
    pstore_dmesg->size = 0;
}

void uart_putc(char c) {
    if (c == '\n') {
        mem_log_putc('\r');
    }

    mem_log_putc(c);
}

void uart_puts(const char *s) {
    while (*s) {
        uart_putc(*s++);
    }
}

void uart_puthex64(uint64_t value) {
    const char hex_chars[] = "0123456789abcdef";
    uart_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        uart_putc(hex_chars[(value >> i) & 0xf]);
    }
}

void uart_puthex(uint64_t val) {
    uart_puthex64(val);
}

void uart_putdec(uint64_t val) {
    if (val == 0) {
        uart_putc('0');
        return;
    }
    char buf[24];
    int idx = 0;
    while (val > 0) {
        buf[idx++] = '0' + (val % 10);
        val /= 10;
    }
    for (int i = idx - 1; i >= 0; i--) {
        uart_putc(buf[i]);
    }
}
