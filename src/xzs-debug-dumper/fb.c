#include "fb.h"
#include "font.h"
#include "uart.h"

static uintptr_t g_fb_base = 0;
static uint32_t g_fb_width = 0;
static uint32_t g_fb_height = 0;
static uint32_t g_fb_stride = 0;
static uint32_t g_fb_bpp = 0;
static bool g_fb_found = false;

bool fb_init(void) {
    uart_puts("[FB] Using Sony cont_splash_mem at 0x83401000...\n");

    /* Sony Xperia XZs cont_splash_mem node from DTB */
    g_fb_base = 0x83401000UL;
    g_fb_width = 1080;
    g_fb_height = 1920;
    g_fb_stride = 1080;
    g_fb_bpp = 32;
    g_fb_found = true;

    uart_puts("[FB] Framebuffer configured at 0x83401000 (1080x1920x32)\n");
    return true;
}

bool fb_is_available(void) {
    return g_fb_found && g_fb_base != 0;
}

uintptr_t fb_get_base(void) {
    return g_fb_base;
}

uint32_t fb_get_width(void) {
    return g_fb_width;
}

uint32_t fb_get_height(void) {
    return g_fb_height;
}

uint32_t fb_get_bpp(void) {
    return g_fb_bpp;
}

static inline void put_pixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= (int)g_fb_width || y < 0 || y >= (int)g_fb_height) return;

    if (g_fb_bpp == 32) {
        volatile uint32_t *dst = (volatile uint32_t *)(g_fb_base + (y * g_fb_stride + x) * 4);
        *dst = color;
    } else if (g_fb_bpp == 24) {
        volatile uint8_t *dst = (volatile uint8_t *)(g_fb_base + (y * g_fb_stride + x) * 3);
        dst[0] = (uint8_t)(color & 0xff);         /* Blue */
        dst[1] = (uint8_t)((color >> 8) & 0xff);  /* Green */
        dst[2] = (uint8_t)((color >> 16) & 0xff); /* Red */
    } else if (g_fb_bpp == 16) {
        volatile uint16_t *dst = (volatile uint16_t *)(g_fb_base + (y * g_fb_stride + x) * 2);
        uint16_t r = (color >> 19) & 0x1f;
        uint16_t g = (color >> 10) & 0x3f;
        uint16_t b = (color >> 3) & 0x1f;
        *dst = (r << 11) | (g << 5) | b;
    }
}

void fb_clear(uint32_t color) {
    if (!fb_is_available()) return;
    for (int y = 0; y < (int)g_fb_height; y++) {
        for (int x = 0; x < (int)g_fb_width; x++) {
            put_pixel(x, y, color);
        }
    }
}

void fb_fill_rect(int x, int y, int w, int h, uint32_t color) {
    if (!fb_is_available()) return;
    for (int cy = y; cy < y + h; cy++) {
        for (int cx = x; cx < x + w; cx++) {
            put_pixel(cx, cy, color);
        }
    }
}

void fb_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg, int scale) {
    if (!fb_is_available()) return;
    if (c < 32 || c > 127) c = ' ';
    const uint8_t *glyph = font_8x16[(int)(c - 32)];

    for (int row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            uint32_t color = (bits & (0x80 >> col)) ? fg : bg;
            if (color != 0xff000000) { /* If bg is not completely transparent */
                if (scale == 1) {
                    put_pixel(x + col, y + row, color);
                } else {
                    for (int dy = 0; dy < scale; dy++) {
                        for (int dx = 0; dx < scale; dx++) {
                            put_pixel(x + col * scale + dx, y + row * scale + dy, color);
                        }
                    }
                }
            }
        }
    }
}

void fb_draw_string(int x, int y, const char *str, uint32_t fg, uint32_t bg, int scale) {
    if (!fb_is_available() || !str) return;
    int cur_x = x;
    int cur_y = y;

    while (*str) {
        if (*str == '\n') {
            cur_x = x;
            cur_y += 18 * scale;
        } else {
            fb_draw_char(cur_x, cur_y, *str, fg, bg, scale);
            cur_x += 8 * scale;
        }
        str++;
    }
}
