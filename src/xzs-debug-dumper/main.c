#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "uart.h"
#include "usb.h"

static inline uint64_t read_cntfrq(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(val));
    return val;
}

static inline uint64_t read_cntvct(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(val));
    return val;
}

static void pet_watchdog(void) {
    volatile uint32_t *wdt_rst = (volatile uint32_t *)0x09830038UL;
    volatile uint32_t *wdt_en  = (volatile uint32_t *)0x09830040UL;
    *wdt_en = 0;
    *wdt_rst = 1;
}

static void delay_ms(uint32_t ms) {
    uint64_t frq = read_cntfrq();
    uint64_t cycles = (frq * ms) / 1000;
    uint64_t start = read_cntvct();
    while ((read_cntvct() - start) < cycles) {
        pet_watchdog();
        __asm__ volatile("nop");
    }
}

static void u64_to_hex(uint64_t val, char *buf) {
    const char hex[] = "0123456789abcdef";
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 60; i >= 0; i -= 4) {
        buf[2 + (60 - i) / 4] = hex[(val >> i) & 0xf];
    }
    buf[18] = '\0';
}

static void u32_to_hex(uint32_t val, char *buf) {
    const char hex[] = "0123456789abcdef";
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 28; i >= 0; i -= 4) {
        buf[2 + (28 - i) / 4] = hex[(val >> i) & 0xf];
    }
    buf[10] = '\0';
}

static void u64_to_dec(uint64_t val, char *buf) {
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    char tmp[24];
    int idx = 0;
    while (val > 0) {
        tmp[idx++] = '0' + (val % 10);
        val /= 10;
    }
    for (int i = 0; i < idx; i++) {
        buf[i] = tmp[idx - 1 - i];
    }
    buf[idx] = '\0';
}

void dumper_on_exception(uint64_t esr, uint64_t elr, uint64_t far, uint64_t spsr) {
    pet_watchdog();
    uart_puts("\n[XZS-DUMPER EXCEPTION]\n");
    uart_puts("  ESR_EL1:  0x"); uart_puthex64(esr); uart_puts("\n");
    uart_puts("  ELR_EL1:  0x"); uart_puthex64(elr); uart_puts("\n");
    uart_puts("  FAR_EL1:  0x"); uart_puthex64(far); uart_puts("\n");
    uart_puts("  SPSR_EL1: 0x"); uart_puthex64(spsr); uart_puts("\n");

    while (1) {
        pet_watchdog();
        usb_poll();
        delay_ms(50);
    }
}

void dumper_main(uint64_t dtb_phys, uint64_t current_el, uint64_t mpidr) {
    (void)dtb_phys;
    (void)current_el;
    (void)mpidr;

    pet_watchdog();
    uart_puts("\n============================================================\n");
    uart_puts("SONY XPERIA XZS BARE-METAL TELEMETRY LOG DUMPER\n");
    uart_puts("============================================================\n");

    /* 1. Read persistent memory buffers */
    volatile struct xzs_debug_log *dlog = (volatile struct xzs_debug_log *)XZS_DEBUG_LOG_BASE;
    volatile uint32_t *ramoops = (volatile uint32_t *)XZS_RAMOOPS_BASE;

    /* 2. Output to UART */
    if (dlog->magic == XZS_DEBUG_LOG_MAGIC) {
        uart_puts("RAM_PERSISTS_ACROSS_FASTBOOT = YES\n");
        uart_puts("Magic:           0x585a5344 (XZSD VALID)\n");
        uart_puts("Boot Count:      "); uart_putdec((uint64_t)dlog->boot_count); uart_puts("\n");
        uart_puts("Last Stage:      0x"); uart_puthex64((uint64_t)dlog->last_stage);
        uart_puts(" ["); uart_puts(xzs_stage_to_string(dlog->last_stage)); uart_puts("]\n");
        uart_puts("Exception Count: "); uart_putdec((uint64_t)dlog->exception_count); uart_puts("\n");
        if (dlog->exception_count > 0 || dlog->last_esr != 0 || dlog->last_elr != 0) {
            uart_puts("Last ESR_EL1:    0x"); uart_puthex64(dlog->last_esr); uart_puts("\n");
            uart_puts("Last ELR_EL1:    0x"); uart_puthex64(dlog->last_elr); uart_puts("\n");
            uart_puts("Last FAR_EL1:    0x"); uart_puthex64(dlog->last_far); uart_puts("\n");
            uart_puts("Last SPSR_EL1:   0x"); uart_puthex64(dlog->last_spsr); uart_puts("\n");
        }
    } else {
        uart_puts("RAM_PERSISTS_ACROSS_FASTBOOT = NO\n");
        uart_puts("Raw 32-bit word at 0x80060000: 0x");
        uart_puthex64((uint64_t)*(volatile uint32_t *)XZS_DEBUG_LOG_BASE);
        uart_puts("\n");
    }
    uart_puts("============================================================\n");

    /* 3. Initialize USB CDC ACM device */
    usb_init();

    /* 4. Main USB transmission loop with watchdog pet */
    uint32_t loop_ticks = 0;
    while (1) {
        pet_watchdog();
        usb_poll();
        delay_ms(2);

        if (++loop_ticks >= 500) { /* Broadcast every ~1 second */
            loop_ticks = 0;

            usb_puts("\n============================================================\n");
            usb_puts("SONY XPERIA XZS TELEMETRY REPORT\n");
            usb_puts("============================================================\n");

            if (dlog->magic == XZS_DEBUG_LOG_MAGIC) {
                usb_puts("RAM_PERSISTS_ACROSS_FASTBOOT = YES\n");
                usb_puts("Magic:           0x585a5344 (XZSD VALID)\n");
                usb_puts("Boot Count:      ");
                char dbuf[24];
                u64_to_dec((uint64_t)dlog->boot_count, dbuf);
                usb_puts(dbuf);
                usb_puts("\nLast Stage:      0x");
                u32_to_hex(dlog->last_stage, dbuf);
                usb_puts(dbuf);
                usb_puts(" [");
                usb_puts(xzs_stage_to_string(dlog->last_stage));
                usb_puts("]\nException Count: ");
                u64_to_dec((uint64_t)dlog->exception_count, dbuf);
                usb_puts(dbuf);
                usb_puts("\n");

                if (dlog->exception_count > 0 || dlog->last_esr != 0 || dlog->last_elr != 0) {
                    char hbuf[24];
                    usb_puts("Last ESR_EL1:    "); u64_to_hex(dlog->last_esr, hbuf); usb_puts(hbuf); usb_puts("\n");
                    usb_puts("Last ELR_EL1:    "); u64_to_hex(dlog->last_elr, hbuf); usb_puts(hbuf); usb_puts("\n");
                    usb_puts("Last FAR_EL1:    "); u64_to_hex(dlog->last_far, hbuf); usb_puts(hbuf); usb_puts("\n");
                    usb_puts("Last SPSR_EL1:   "); u64_to_hex(dlog->last_spsr, hbuf); usb_puts(hbuf); usb_puts("\n");
                }

                usb_puts("\n--- RAW TELEMETRY LOG BUFFER ---\n");
                uint32_t limit = dlog->write_offset;
                if (limit > 65472) limit = 65472;
                if (limit > 0) {
                    usb_write_bytes((const void *)dlog->data, limit);
                }
                usb_puts("\n--- END OF LOG BUFFER ---\n");
            } else {
                usb_puts("RAM_PERSISTS_ACROSS_FASTBOOT = NO\n");
                usb_puts("Target buffer at 0x80060000 is cleared/uninitialized.\n");
                char hbuf[24];
                u32_to_hex(*(volatile uint32_t *)XZS_DEBUG_LOG_BASE, hbuf);
                usb_puts("Raw word at 0x80060000: "); usb_puts(hbuf); usb_puts("\n");
            }

            volatile uint32_t *pstore_console = (volatile uint32_t *)0xa7fbe000UL;
            if (pstore_console[0] == 0x43474244) {
                usb_puts("\n--- PSTORE CONSOLE BUFFER (0xa7fbe000) ---\n");
                uint32_t csize = pstore_console[2];
                if (csize > 262000) csize = 262000;
                if (csize > 0) {
                    usb_write_bytes((const void *)(0xa7fbe000UL + 12), csize);
                }
                usb_puts("\n--- END OF PSTORE CONSOLE ---\n");
            }

            volatile uint32_t *pstore_dmesg = (volatile uint32_t *)0xa7f00000UL;
            if (pstore_dmesg[0] == 0x43474244) {
                usb_puts("\n--- PSTORE DMESG BUFFER (0xa7f00000) ---\n");
                uint32_t dsize = pstore_dmesg[2];
                if (dsize > 4000) dsize = 4000;
                if (dsize > 0) {
                    usb_write_bytes((const void *)(0xa7f00000UL + 12), dsize);
                }
                usb_puts("\n--- END OF PSTORE DMESG ---\n");
            }

            usb_puts("============================================================\n\n");
        }
    }
}
