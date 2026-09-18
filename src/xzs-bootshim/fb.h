#ifndef FB_H
#define FB_H

#include <stdint.h>
#include <stddef.h>

#define COLOR_RED     0x000000ffU  /* 32-bit BGR/RGB depending on endian */
#define COLOR_GREEN   0x0000ff00U
#define COLOR_BLUE    0x00ff0000U
#define COLOR_YELLOW  0x0000ffffU
#define COLOR_MAGENTA 0x00ff00ffU
#define COLOR_CYAN    0x00ffff00U
#define COLOR_WHITE   0x00ffffffU

/*
 * Scans safe DRAM regions for the active Qualcomm continuous splash framebuffer
 * and paints it with the specified color.
 * Returns the detected framebuffer physical address, or 0 if not found.
 */
uint64_t fb_init_and_paint(uint32_t color);

/*
 * Repaints the previously detected framebuffer with a new color stage.
 */
void fb_set_color(uint32_t color);

#endif /* FB_H */
