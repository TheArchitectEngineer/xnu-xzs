#include "fb.h"
#include "uart.h"

#define XZS_FB_BASE 0x98000000ULL

uint64_t fb_init_and_paint(uint32_t color) {
    (void)color;
    return XZS_FB_BASE;
}

void fb_set_color(uint32_t color) {
    (void)color;
}
