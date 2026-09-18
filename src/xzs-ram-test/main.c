#include <stdint.h>
#include <stddef.h>

#define XZS_DEBUG_LOG_BASE   0x80060000UL
#define XZS_RAMOOPS_BASE     0xa7f00000UL
#define XZS_DEBUG_LOG_MAGIC  0x585a5344UL /* "XZSD" */

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

static void uart_putc(char c) {
    uint32_t timeout = 50000;
    if (c == '\n') uart_putc('\r');
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

static void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

static void disable_watchdog(void) {
    volatile uint32_t *wdt_en = (volatile uint32_t *)0x09830040UL;
    volatile uint32_t *wdt_rst = (volatile uint32_t *)0x09830038UL;
    *wdt_en = 0;
    *wdt_rst = 1;
}

struct xzs_debug_log {
    uint32_t magic;           /* 0x00: 0x585a5344 ("XZSD") */
    uint32_t version;         /* 0x04: 2 */
    uint32_t write_offset;    /* 0x08 */
    uint32_t boot_count;      /* 0x0c */
    uint32_t exception_count; /* 0x10 */
    uint32_t last_stage;      /* 0x14 */
    uint64_t last_elr;        /* 0x18 */
    uint64_t last_esr;        /* 0x20 */
    uint64_t last_far;        /* 0x28 */
    uint64_t last_spsr;       /* 0x30 */
    uint8_t  reserved[8];     /* 0x38 */
    char data[65472];         /* 0x40 */
};

void test_writer_main(void) {
    disable_watchdog();

    uart_puts("\n============================================================\n");
    uart_puts("XZS RAM PERSISTENCE TEST WRITER\n");
    uart_puts("Writing test canaries to 0x80060000...\n");

    volatile struct xzs_debug_log *dlog = (volatile struct xzs_debug_log *)XZS_DEBUG_LOG_BASE;

    dlog->magic = XZS_DEBUG_LOG_MAGIC;
    dlog->version = 2;
    dlog->write_offset = 32;
    dlog->boot_count = 0x12345678;
    dlog->exception_count = 1;
    dlog->last_stage = 0x106; /* SHIM_G */
    dlog->last_elr = 0x123456789abcdef0ULL;
    dlog->last_esr = 0x96000004ULL;
    dlog->last_far = 0x80060000ULL;
    dlog->last_spsr = 0x60000005ULL;

    const char canary_msg[] = "CANARY_PERSIST_TEST_ACTIVE\n";
    for (int i = 0; canary_msg[i] != '\0'; i++) {
        dlog->data[i] = canary_msg[i];
    }
    dlog->data[sizeof(canary_msg) - 1] = '\0';

    /* Also write canary to Ramoops at 0xa7f00000 */
    volatile uint64_t *ramoops = (volatile uint64_t *)XZS_RAMOOPS_BASE;
    ramoops[0] = 0x585a5344ULL;
    ramoops[1] = 0x123456789abcdef0ULL;

    uart_puts("Canary committed to DRAM:\n");
    uart_puts("  Magic:      0x585a5344\n");
    uart_puts("  Value:      0x123456789abcdef0\n");
    uart_puts("  Boot Count: 0x12345678\n");
    uart_puts("============================================================\n");
    uart_puts("Please force device back to Fastboot, then boot:\n");
    uart_puts("  fastboot boot artifacts/builds/xzs-debug-dumper.img\n");
    uart_puts("============================================================\n\n");

    /* Pet watchdog and spin */
    while (1) {
        disable_watchdog();
        __asm__ volatile("wfe");
    }
}
