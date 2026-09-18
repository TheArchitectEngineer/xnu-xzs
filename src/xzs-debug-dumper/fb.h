#ifndef FB_H
#define FB_H

#include <stdint.h>
#include <stdbool.h>

bool fb_init(void);
bool fb_is_available(void);
uintptr_t fb_get_base(void);
uint32_t fb_get_width(void);
uint32_t fb_get_height(void);
uint32_t fb_get_bpp(void);

void fb_clear(uint32_t color);
void fb_fill_rect(int x, int y, int w, int h, uint32_t color);
void fb_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg, int scale);
void fb_draw_string(int x, int y, const char *str, uint32_t fg, uint32_t bg, int scale);

#endif /* FB_H */
