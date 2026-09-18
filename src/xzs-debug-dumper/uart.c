#include "uart.h"

#define BLSP2_UART2_BASE    0x075b0000UL
#define MSM_UART_SR         0x0008
#define MSM_UART_SR_TX_READY (1 << 2)
#define MSM_UART_SR_TX_EMPTY (1 << 3)
#define MSM_UART_IMR        0x0014
#define UARTDM_NCF_TX       0x0040
#define UARTDM_TF           0x0070

static inline void mmio_write32(uintptr_t addr, uint32_t val) {
    *(volatile uint32_t *)addr = val;
}

static inline uint32_t mmio_read32(uintptr_t addr) {
    return *(volatile uint32_t *)addr;
}

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

void uart_putc(char c) {
    uint32_t timeout = 50000;

    if (c == '\n') {
        uart_putc('\r');
    }

    mmio_write32(BLSP2_UART2_BASE + MSM_UART_IMR, 0);

    while (!(mmio_read32(BLSP2_UART2_BASE + MSM_UART_SR) & MSM_UART_SR_TX_EMPTY)) {
        if (--timeout == 0) return;
    }

    mmio_write32(BLSP2_UART2_BASE + UARTDM_NCF_TX, 1);
    (void)mmio_read32(BLSP2_UART2_BASE + UARTDM_NCF_TX);

    timeout = 50000;
    while (!(mmio_read32(BLSP2_UART2_BASE + MSM_UART_SR) & MSM_UART_SR_TX_READY)) {
        if (--timeout == 0) return;
    }

    mmio_write32(BLSP2_UART2_BASE + UARTDM_TF, (uint32_t)(uint8_t)c);
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
