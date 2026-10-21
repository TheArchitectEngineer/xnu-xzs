/*
 * Xperia XZs Framebuffer Text Console (xzs_fbcon)
 * Milestone D8-M10: Early Boot Framebuffer Console & Interactive Shell Display
 *
 * Canonical Framebuffer Geometry:
 *   Resolution:   1080 x 1920
 *   Physical PA:  0x98000000
 *   Stride:       4352 bytes (0x1100, 1088 uint32 words per row)
 *   Color Format: 32-bit XRGB8888 (Unpack 0x03010002: B0=R, B1=G, B2=B, B3=A)
 *   Font:         Standard 8x16 ISO font (256 glyphs, iso_font from osfmk)
 *   Text Grid:    135 columns x 120 rows (zero padding, exact fill)
 *   Scrollback:   256 logical lines
 *   Early Ring:   64 KiB static ring buffer
 */

#ifndef _XZS_FBCON_H_
#define _XZS_FBCON_H_

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define XZS_FB_PA               0x98000000u
#define XZS_FB_WIDTH            1080u
#define XZS_FB_HEIGHT           1920u
#define XZS_FB_STRIDE           4352u  /* 0x1100 bytes */
#define XZS_FB_WORDS_PER_ROW    (XZS_FB_STRIDE / 4u) /* 1088 uint32 words */
#define XZS_FB_BPP              4u
#define XZS_FB_SIZE             (XZS_FB_STRIDE * XZS_FB_HEIGHT) /* 8,355,840 bytes */

#define XZS_FONT_WIDTH          8u
#define XZS_FONT_HEIGHT         16u
#define XZS_FBCON_COLS          (XZS_FB_WIDTH / XZS_FONT_WIDTH)   /* 135 columns */
#define XZS_FBCON_TOTAL_ROWS    (XZS_FB_HEIGHT / XZS_FONT_HEIGHT) /* 120 rows total */
#define XZS_FBCON_ROWS          39u                               /* Console viewport: rows 0..38 */
#define XZS_FBCON_DIVIDER_ROW   39u                               /* Fixed divider at row 39 */
#define XZS_FBCON_BARS_START    40u                               /* Hardware test bars: rows 40..119 */
#define XZS_SCROLLBACK_LINES    256u
#define XZS_EARLY_RING_CAP      65536u

/* Colors in Little-Endian uint32: byte 0=R, byte 1=G, byte 2=B, byte 3=A */
#define XZS_COLOR_BG            0x00050505u /* Near-black */
#define XZS_COLOR_FG            0x00ffffffu /* Light gray / White */
#define XZS_COLOR_GREEN         0x0000ff55u /* Shell prompt / Success */
#define XZS_COLOR_YELLOW        0x0000ffffu /* Warning */
#define XZS_COLOR_RED           0x000000ffu /* Error */
#define XZS_COLOR_CYAN          0x00ffff00u /* Info tags */
#define XZS_COLOR_DIM           0x00808080u /* Dim text */
#define XZS_COLOR_BLUE          0x00ff0000u /* Blue */

/* External ISO font from osfmk/console/iso_font.c */
extern unsigned char iso_font[256 * 16];
extern int snprintf(char *, size_t, const char *, ...);
extern void xzs_d8m8_kick_frame(void);
extern void xzs_d8m8_kick_frame_ex(bool wait_for_idle);

/* Boot Timing Telemetry Timestamps (19.2 MHz ARM Generic Timer) */
extern uint64_t g_xzs_boot_t0; /* T0 = first reliable XNU entry */
extern uint64_t g_xzs_boot_t1; /* T1 = arm/platform init complete */
extern uint64_t g_xzs_boot_t2; /* T2 = Mach scheduler ready */
extern uint64_t g_xzs_boot_t3; /* T3 = BSD_INIT */
extern uint64_t g_xzs_boot_t4; /* T4 = VFS/root ready */
extern uint64_t g_xzs_boot_t5; /* T5 = display/fbcon ready */
extern uint64_t g_xzs_boot_t6; /* T6 = PID1 entered */
extern uint64_t g_xzs_boot_t7; /* T7 = shell prompt ready */

/* Framebuffer Console Public API */
void xzs_fbcon_attach(uintptr_t fb_va);
void xzs_fbcon_detach(void);
void xzs_fbcon_write(const char *buf, int len);
void xzs_fbcon_putc(char c);
void xzs_fbcon_early_record(const char *buf, int len);
bool xzs_fbcon_is_active(void);
void xzs_fbcon_clear(uint32_t bg);
void xzs_print_boot_timing(void);
void xzs_record_boot_t7_and_print(void);
void xzs_d8m10_auto_display_init(bool show_diagnostic_bars);

#ifdef XZS_FBCON_IMPL

/* Boot Timing Globals */
uint64_t g_xzs_boot_t0 = 0;
uint64_t g_xzs_boot_t1 = 0;
uint64_t g_xzs_boot_t2 = 0;
uint64_t g_xzs_boot_t3 = 0;
uint64_t g_xzs_boot_t4 = 0;
uint64_t g_xzs_boot_t5 = 0;
uint64_t g_xzs_boot_t6 = 0;
uint64_t g_xzs_boot_t7 = 0;

/* Framebuffer Console State */
static uintptr_t g_fbcon_va = 0;
static volatile bool g_fbcon_attached = false;
static uint32_t  g_fbcon_col = 0;
static uint32_t  g_fbcon_row = 0;
static uint32_t  g_fbcon_fg_color = XZS_COLOR_FG;
static uint32_t  g_fbcon_bg_color = XZS_COLOR_BG;
static bool      g_fbcon_cursor_visible = true;

/* Early Log Ring Buffer (Section 12) */
static char      g_fbcon_early_ring[XZS_EARLY_RING_CAP];
static uint32_t  g_fbcon_early_bytes_captured = 0;
static uint32_t  g_fbcon_early_dropped_bytes = 0;
static bool      g_fbcon_early_wrapped = false;

/* Logical Scrollback Ring Buffer (Section 23 & 24) */
static char      g_fbcon_scrollback[XZS_SCROLLBACK_LINES][XZS_FBCON_COLS + 1];
static uint32_t  g_fbcon_scrollback_head = 0;
static uint32_t  g_fbcon_scrollback_total = 0;

/* Active Screen Buffer in Cacheable RAM (Zero Uncached Reads during Scroll) */
static char      g_fbcon_screen[XZS_FBCON_ROWS][XZS_FBCON_COLS];
static uint32_t  g_fbcon_screen_fg[XZS_FBCON_ROWS][XZS_FBCON_COLS];
static uint8_t   g_fbcon_row_len[XZS_FBCON_ROWS];

/* SMP-Safe Synchronization (Section 13) */
static volatile bool g_fbcon_lock = false;

static inline uint64_t
xzs_fbcon_lock_irq(void)
{
	uint64_t daif = 0;
	__asm__ volatile("mrs %0, DAIF; msr DAIFSet, #0xf" : "=r"(daif));
	for (int i = 0; i < 100000; i++) {
		if (!__atomic_test_and_set(&g_fbcon_lock, __ATOMIC_ACQUIRE)) {
			return daif;
		}
	}
	__asm__ volatile("msr DAIF, %0" :: "r"(daif));
	return 0xffffffffffffffffULL;
}

static inline void
xzs_fbcon_unlock_irq(uint64_t daif)
{
	__atomic_clear(&g_fbcon_lock, __ATOMIC_RELEASE);
	__asm__ volatile("msr DAIF, %0" :: "r"(daif));
}

bool
xzs_fbcon_is_active(void)
{
	return g_fbcon_attached && (g_fbcon_va != 0);
}

void
xzs_fbcon_detach(void)
{
	g_fbcon_attached = false;
}

void
xzs_fbcon_early_record(const char *buf, int len)
{
	if (buf == NULL || len <= 0) {
		return;
	}
	uint64_t daif = xzs_fbcon_lock_irq();
	if (daif == 0xffffffffffffffffULL) {
		return;
	}
	for (int i = 0; i < len; i++) {
		if (g_fbcon_early_bytes_captured < XZS_EARLY_RING_CAP) {
			g_fbcon_early_ring[g_fbcon_early_bytes_captured++] = buf[i];
		} else {
			g_fbcon_early_dropped_bytes++;
			g_fbcon_early_wrapped = true;
		}
	}
	xzs_fbcon_unlock_irq(daif);
}

/* Fast Direct 32-bit Glyph Render Primitive (Section 18) */
static inline void
xzs_fbcon_draw_glyph_raw(uint32_t col, uint32_t row, char ch, uint32_t fg, uint32_t bg)
{
	if (g_fbcon_va == 0 || col >= XZS_FBCON_COLS || row >= XZS_FBCON_TOTAL_ROWS) {
		return;
	}
	volatile uint32_t *line_ptr = (volatile uint32_t *)g_fbcon_va +
	                              (row * XZS_FONT_HEIGHT) * XZS_FB_WORDS_PER_ROW +
	                              (col * XZS_FONT_WIDTH);

	if (ch == ' ') {
		for (uint32_t y = 0; y < 16; y++) {
			line_ptr[0] = bg; line_ptr[1] = bg;
			line_ptr[2] = bg; line_ptr[3] = bg;
			line_ptr[4] = bg; line_ptr[5] = bg;
			line_ptr[6] = bg; line_ptr[7] = bg;
			line_ptr += XZS_FB_WORDS_PER_ROW;
		}
		return;
	}

	const unsigned char *glyph = &iso_font[(unsigned char)ch * 16];

	for (uint32_t y = 0; y < 16; y++) {
		uint8_t bits = glyph[y];
		line_ptr[0] = (bits & 0x01) ? fg : bg;
		line_ptr[1] = (bits & 0x02) ? fg : bg;
		line_ptr[2] = (bits & 0x04) ? fg : bg;
		line_ptr[3] = (bits & 0x08) ? fg : bg;
		line_ptr[4] = (bits & 0x10) ? fg : bg;
		line_ptr[5] = (bits & 0x20) ? fg : bg;
		line_ptr[6] = (bits & 0x40) ? fg : bg;
		line_ptr[7] = (bits & 0x80) ? fg : bg;
		line_ptr += XZS_FB_WORDS_PER_ROW;
	}
}

/* Underscore Visible Cursor (Section 22) */
static inline void
xzs_fbcon_draw_cursor_raw(uint32_t col, uint32_t row, bool visible)
{
	if (g_fbcon_va == 0 || col >= XZS_FBCON_COLS || row >= XZS_FBCON_ROWS) {
		return;
	}
	uint32_t color = visible ? g_fbcon_fg_color : g_fbcon_bg_color;
	volatile uint32_t *p = (volatile uint32_t *)g_fbcon_va +
	                       (row * XZS_FONT_HEIGHT + 14) * XZS_FB_WORDS_PER_ROW +
	                       (col * XZS_FONT_WIDTH);

	for (uint32_t y = 0; y < 2; y++) {
		p[0] = color; p[1] = color; p[2] = color; p[3] = color;
		p[4] = color; p[5] = color; p[6] = color; p[7] = color;
		p += XZS_FB_WORDS_PER_ROW;
	}
}

extern void xzs_watchdog_pet(void);

/* Clear Console Viewport (Rows 0..38, Lines 0..623) */
void
xzs_fbcon_clear(uint32_t bg)
{
	if (g_fbcon_va == 0) return;
	xzs_watchdog_pet();
	volatile uint32_t *dst = (volatile uint32_t *)g_fbcon_va;
	uint32_t total_words = XZS_FB_WORDS_PER_ROW * (XZS_FBCON_ROWS * XZS_FONT_HEIGHT);
	for (uint32_t i = 0; i < total_words; i++) {
		if ((i & 0x7ffff) == 0) {
			xzs_watchdog_pet();
		}
		dst[i] = bg;
	}
	memset(g_fbcon_screen, ' ', sizeof(g_fbcon_screen));
	memset(g_fbcon_row_len, 0, sizeof(g_fbcon_row_len));
	g_fbcon_col = 0;
	g_fbcon_row = 0;
	__asm__ volatile("dmb oshst" ::: "memory");
	xzs_watchdog_pet();
}

/* Render permanent 8 vertical color bars + center badge onto rows 40..119 (lines 640..1919) */
static void
xzs_fbcon_draw_test_bars(uintptr_t fb_va)
{
	if (fb_va == 0) return;
	static const uint32_t s_bar_colors[8] = {
		0x00ffffffu, /* White */
		0x0000ffffu, /* Yellow */
		0x00ffff00u, /* Cyan */
		0x0000ff00u, /* Green */
		0x00ff00ffu, /* Magenta */
		0x000000ffu, /* Red */
		0x00ff0000u, /* Blue */
		0x00101010u  /* Dark Slate */
	};

	for (uint32_t y = 640; y < XZS_FB_HEIGHT; y++) {
		if ((y & 63) == 0) {
			xzs_watchdog_pet();
		}
		uint32_t *line_ptr = (uint32_t *)(fb_va + y * XZS_FB_STRIDE);
		for (uint32_t x = 0; x < XZS_FB_WIDTH; x++) {
			/* Outer border: 30px white on bottom/left/right */
			if (y >= (XZS_FB_HEIGHT - 30) || x < 30 || x >= (XZS_FB_WIDTH - 30)) {
				line_ptr[x] = 0x00ffffffu;
			}
			/* Center contrasting test badge: lines 760..1160, cols 240..840 */
			else if (y >= 760 && y < 1160 && x >= 240 && x < 840) {
				uint32_t cell_x = (x - 240) / 40;
				uint32_t cell_y = (y - 760) / 40;
				line_ptr[x] = ((cell_x + cell_y) & 1) ? 0x00ffffffu : 0x00000000u;
			}
			/* 8 vertical color bars */
			else {
				uint32_t bar_idx = (x * 8) / XZS_FB_WIDTH;
				if (bar_idx > 7) bar_idx = 7;
				line_ptr[x] = s_bar_colors[bar_idx];
			}
		}
	}
	__asm__ volatile("dmb oshst" ::: "memory");
}

/* Fast Hardware Framebuffer Line Scroll with Zero Uncached Reads (Section 25) */
static void
xzs_fbcon_scroll_up(void)
{
	if (g_fbcon_va == 0) return;

	/* Hide cursor before scroll */
	xzs_fbcon_draw_cursor_raw(g_fbcon_col, g_fbcon_row, false);
	xzs_watchdog_pet();

	/* Shift screen text and row lengths up by 1 row in cacheable RAM */
	uint8_t old_lens[XZS_FBCON_ROWS];
	for (uint32_t i = 0; i < XZS_FBCON_ROWS; i++) {
		old_lens[i] = g_fbcon_row_len[i];
	}

	char *dst_s = (char *)g_fbcon_screen;
	const char *src_s = (const char *)g_fbcon_screen + XZS_FBCON_COLS;
	uint32_t len_s = (XZS_FBCON_ROWS - 1) * XZS_FBCON_COLS;
	for (uint32_t i = 0; i < len_s; i++) {
		dst_s[i] = src_s[i];
	}

	uint32_t *dst_fg = (uint32_t *)g_fbcon_screen_fg;
	const uint32_t *src_fg = (const uint32_t *)g_fbcon_screen_fg + XZS_FBCON_COLS;
	uint32_t len_fg = (XZS_FBCON_ROWS - 1) * XZS_FBCON_COLS;
	for (uint32_t i = 0; i < len_fg; i++) {
		dst_fg[i] = src_fg[i];
	}

	uint8_t *dst_l = (uint8_t *)g_fbcon_row_len;
	const uint8_t *src_l = (const uint8_t *)g_fbcon_row_len + 1;
	for (uint32_t i = 0; i < XZS_FBCON_ROWS - 1; i++) {
		dst_l[i] = src_l[i];
	}

	char *last_row = (char *)g_fbcon_screen + (XZS_FBCON_ROWS - 1) * XZS_FBCON_COLS;
	for (uint32_t i = 0; i < XZS_FBCON_COLS; i++) {
		last_row[i] = ' ';
	}
	g_fbcon_row_len[XZS_FBCON_ROWS - 1] = 0;

	uint32_t active_rows = g_fbcon_row < XZS_FBCON_ROWS ? (g_fbcon_row + 1) : XZS_FBCON_ROWS;
	for (uint32_t r = 0; r < active_rows; r++) {
		if ((r & 7u) == 0) {
			xzs_watchdog_pet();
		}
		uint32_t max_col = g_fbcon_row_len[r] > old_lens[r] ? g_fbcon_row_len[r] : old_lens[r];
		if (max_col == 0) continue;
		if (max_col > XZS_FBCON_COLS) max_col = XZS_FBCON_COLS;

		for (uint32_t c = 0; c < max_col; c++) {
			char ch = g_fbcon_screen[r][c];
			uint32_t fg = g_fbcon_screen_fg[r][c];
			if (c >= g_fbcon_row_len[r]) ch = ' ';
			xzs_fbcon_draw_glyph_raw(c, r, ch, fg, g_fbcon_bg_color);
		}
	}

	__asm__ volatile("dmb oshst" ::: "memory");
	xzs_watchdog_pet();
}

/* Character Dispatcher with Control Characters (Section 20) */
static void
xzs_fbcon_putc_unlocked(char c)
{
	if (g_fbcon_va == 0) {
		return;
	}

	if (c == '\r') {
		g_fbcon_col = 0;
		return;
	}

	if (c == '\n') {
		g_fbcon_col = 0;
		if (g_fbcon_row + 1 < XZS_FBCON_ROWS) {
			g_fbcon_row++;
		} else {
			xzs_fbcon_scroll_up();
		}
		g_fbcon_scrollback_head = (g_fbcon_scrollback_head + 1) % XZS_SCROLLBACK_LINES;
		g_fbcon_scrollback_total++;
		memset(g_fbcon_scrollback[g_fbcon_scrollback_head], 0, XZS_FBCON_COLS + 1);
		return;
	}

	if (c == '\b') {
		if (g_fbcon_col > 0) {
			g_fbcon_col--;
			xzs_fbcon_draw_glyph_raw(g_fbcon_col, g_fbcon_row, ' ', g_fbcon_fg_color, g_fbcon_bg_color);
			g_fbcon_screen[g_fbcon_row][g_fbcon_col] = ' ';
			g_fbcon_scrollback[g_fbcon_scrollback_head][g_fbcon_col] = ' ';
		}
		return;
	}

	if (c == '\t') {
		uint32_t next_col = (g_fbcon_col + 8) & ~7u;
		if (next_col >= XZS_FBCON_COLS) {
			next_col = XZS_FBCON_COLS - 1;
		}
		while (g_fbcon_col < next_col) {
			xzs_fbcon_draw_glyph_raw(g_fbcon_col, g_fbcon_row, ' ', g_fbcon_fg_color, g_fbcon_bg_color);
			g_fbcon_screen[g_fbcon_row][g_fbcon_col] = ' ';
			g_fbcon_scrollback[g_fbcon_scrollback_head][g_fbcon_col] = ' ';
			g_fbcon_col++;
		}
		if (g_fbcon_col > g_fbcon_row_len[g_fbcon_row]) {
			g_fbcon_row_len[g_fbcon_row] = (uint8_t)g_fbcon_col;
		}
		return;
	}

	/* Printable ASCII (0x20 .. 0x7E) */
	if ((unsigned char)c >= 0x20) {
		if (g_fbcon_col >= XZS_FBCON_COLS) {
			g_fbcon_col = 0;
			if (g_fbcon_row + 1 < XZS_FBCON_ROWS) {
				g_fbcon_row++;
			} else {
				xzs_fbcon_scroll_up();
			}
			g_fbcon_scrollback_head = (g_fbcon_scrollback_head + 1) % XZS_SCROLLBACK_LINES;
			g_fbcon_scrollback_total++;
			memset(g_fbcon_scrollback[g_fbcon_scrollback_head], 0, XZS_FBCON_COLS + 1);
		}

		/* Determine contextual color */
		uint32_t fg = g_fbcon_fg_color;

		xzs_fbcon_draw_glyph_raw(g_fbcon_col, g_fbcon_row, c, fg, g_fbcon_bg_color);
		g_fbcon_screen[g_fbcon_row][g_fbcon_col] = c;
		g_fbcon_screen_fg[g_fbcon_row][g_fbcon_col] = fg;
		if (g_fbcon_col + 1 > g_fbcon_row_len[g_fbcon_row]) {
			g_fbcon_row_len[g_fbcon_row] = (uint8_t)(g_fbcon_col + 1);
		}
		g_fbcon_scrollback[g_fbcon_scrollback_head][g_fbcon_col] = c;
		g_fbcon_col++;
	}
}

void
xzs_fbcon_putc(char c)
{
	if (!g_fbcon_attached || g_fbcon_va == 0) {
		return;
	}
	uint64_t daif = xzs_fbcon_lock_irq();
	if (daif == 0xffffffffffffffffULL) {
		return;
	}
	xzs_fbcon_draw_cursor_raw(g_fbcon_col, g_fbcon_row, false);
	xzs_fbcon_putc_unlocked(c);
	xzs_fbcon_draw_cursor_raw(g_fbcon_col, g_fbcon_row, true);
	bool sync_now = (c == ' ' && g_fbcon_col == 5 && g_fbcon_row < XZS_FBCON_ROWS &&
	                 g_fbcon_screen[g_fbcon_row][3] == '#');
	__asm__ volatile("dmb oshst" ::: "memory");
	xzs_fbcon_unlock_irq(daif);
	xzs_d8m8_kick_frame_ex(sync_now);
}

void
xzs_fbcon_write(const char *buf, int len)
{
	if (buf == NULL || len <= 0 || !g_fbcon_attached || g_fbcon_va == 0) {
		return;
	}
	uint64_t daif = xzs_fbcon_lock_irq();
	if (daif == 0xffffffffffffffffULL) {
		return;
	}
	xzs_fbcon_draw_cursor_raw(g_fbcon_col, g_fbcon_row, false);
	for (int i = 0; i < len; i++) {
		xzs_fbcon_putc_unlocked(buf[i]);
	}
	xzs_fbcon_draw_cursor_raw(g_fbcon_col, g_fbcon_row, true);
	bool sync_now = (g_fbcon_col == 5 && g_fbcon_row < XZS_FBCON_ROWS &&
	                 g_fbcon_screen[g_fbcon_row][3] == '#' &&
	                 g_fbcon_screen[g_fbcon_row][4] == ' ');
	__asm__ volatile("dmb oshst" ::: "memory");
	xzs_fbcon_unlock_irq(daif);
	xzs_d8m8_kick_frame_ex(sync_now);
}

/* Print String Utility for Banner / Markers */
static void
xzs_fbcon_puts_raw(const char *str, uint32_t fg, uint32_t bg)
{
	if (str == NULL) return;
	uint32_t saved_fg = g_fbcon_fg_color;
	uint32_t saved_bg = g_fbcon_bg_color;
	g_fbcon_fg_color = fg;
	g_fbcon_bg_color = bg;
	while (*str != '\0') {
		xzs_fbcon_putc_unlocked(*str++);
	}
	g_fbcon_fg_color = saved_fg;
	g_fbcon_bg_color = saved_bg;
}

/* Persistent DRAM Debug Log structure at 0x80060000 */
struct xzs_dram_log_header {
	uint32_t magic;           /* 0x00: "XZSD" (0x585a5344) */
	uint32_t version;         /* 0x04 */
	uint32_t write_offset;    /* 0x08 */
	uint32_t boot_count;      /* 0x0c */
	uint32_t exception_count; /* 0x10 */
	uint32_t last_stage;      /* 0x14 */
	uint64_t last_elr;        /* 0x18 */
	uint64_t last_esr;        /* 0x20 */
	uint64_t last_far;        /* 0x28 */
	uint64_t last_spsr;       /* 0x30 */
	uint8_t  reserved[8];     /* 0x38 */
	char     data[65472];     /* 0x40 */
};

extern volatile struct xzs_dram_log_header *g_dram_log;

/* Replay Optimization (Section 26): Parse early log, render visible viewport in 1 pass */
static void
xzs_fbcon_replay_optimized(const char *log_data, uint32_t log_len)
{
	if (log_data == NULL || log_len == 0) {
		return;
	}

	#define XZS_REPLAY_RING_LINES 128u
	static char s_replay_lines[XZS_REPLAY_RING_LINES][XZS_FBCON_COLS + 1];
	static uint8_t s_replay_lens[XZS_REPLAY_RING_LINES];
	uint32_t total_lines_seen = 0;
	uint32_t cur_col = 0;

	memset(s_replay_lines, 0, sizeof(s_replay_lines));
	memset(s_replay_lens, 0, sizeof(s_replay_lens));

	for (uint32_t i = 0; i < log_len; i++) {
		char c = log_data[i];
		if (c == '\0') break;

		if (c == '\r') {
			cur_col = 0;
			continue;
		}
		if (c == '\n') {
			uint32_t cur_slot = total_lines_seen % XZS_REPLAY_RING_LINES;
			s_replay_lens[cur_slot] = (uint8_t)cur_col;
			s_replay_lines[cur_slot][cur_col] = '\0';

			total_lines_seen++;
			uint32_t next_slot = total_lines_seen % XZS_REPLAY_RING_LINES;
			memset(s_replay_lines[next_slot], 0, sizeof(s_replay_lines[0]));
			s_replay_lens[next_slot] = 0;
			cur_col = 0;
			continue;
		}
		if (c == '\t') {
			uint32_t next = (cur_col + 8) & ~7u;
			if (next >= XZS_FBCON_COLS) next = XZS_FBCON_COLS;
			uint32_t cur_slot = total_lines_seen % XZS_REPLAY_RING_LINES;
			while (cur_col < next) {
				s_replay_lines[cur_slot][cur_col++] = ' ';
			}
			continue;
		}
		if ((unsigned char)c >= 0x20) {
			if (cur_col >= XZS_FBCON_COLS) {
				uint32_t cur_slot = total_lines_seen % XZS_REPLAY_RING_LINES;
				s_replay_lens[cur_slot] = (uint8_t)cur_col;
				s_replay_lines[cur_slot][cur_col] = '\0';

				total_lines_seen++;
				uint32_t next_slot = total_lines_seen % XZS_REPLAY_RING_LINES;
				memset(s_replay_lines[next_slot], 0, sizeof(s_replay_lines[0]));
				s_replay_lens[next_slot] = 0;
				cur_col = 0;
			}
			uint32_t cur_slot = total_lines_seen % XZS_REPLAY_RING_LINES;
			s_replay_lines[cur_slot][cur_col++] = c;
		}
	}

	/* Finalize last line if any characters written */
	uint32_t last_slot = total_lines_seen % XZS_REPLAY_RING_LINES;
	s_replay_lens[last_slot] = (uint8_t)cur_col;
	s_replay_lines[last_slot][cur_col] = '\0';

	/* Number of valid lines available in ring buffer */
	uint32_t available_rows = 28; /* Fits into console viewport (rows 3..30), leaving space for prompt */
	uint32_t num_lines_to_draw = total_lines_seen + 1;
	if (num_lines_to_draw > available_rows) {
		num_lines_to_draw = available_rows;
	}
	if (num_lines_to_draw > XZS_REPLAY_RING_LINES) {
		num_lines_to_draw = XZS_REPLAY_RING_LINES;
	}

	uint32_t target_row = 3;
	for (uint32_t idx = 0; idx < num_lines_to_draw && target_row < XZS_FBCON_ROWS - 2; idx++, target_row++) {
		if ((idx % 8) == 0) {
			xzs_watchdog_pet();
		}
		uint32_t back_dist = num_lines_to_draw - 1 - idx;
		uint32_t slot = (total_lines_seen + XZS_REPLAY_RING_LINES - (back_dist % XZS_REPLAY_RING_LINES)) % XZS_REPLAY_RING_LINES;
		const char *line = s_replay_lines[slot];
		uint32_t len = s_replay_lens[slot];
		if (len > XZS_FBCON_COLS) len = XZS_FBCON_COLS;

		for (uint32_t c = 0; c < len; c++) {
			xzs_fbcon_draw_glyph_raw(c, target_row, line[c], XZS_COLOR_FG, XZS_COLOR_BG);
			g_fbcon_screen[target_row][c] = line[c];
			g_fbcon_screen_fg[target_row][c] = XZS_COLOR_FG;
		}
		g_fbcon_row_len[target_row] = (uint8_t)len;
	}

	g_fbcon_row = target_row;
	g_fbcon_col = 0;
	xzs_watchdog_pet();
}

/* Framebuffer Console Attachment Point (Section 14 & 27) */
void
xzs_fbcon_attach(uintptr_t fb_va)
{
	if (fb_va == 0 || g_fbcon_attached) return;

	if (g_xzs_boot_t5 == 0) {
		__asm__ volatile("mrs %0, cntvct_el0" : "=r"(g_xzs_boot_t5));
	}

	extern void xzs_diag_emit(const char *s);
	xzs_diag_emit("[XZS-ATTACH] 1: start\n");

	g_fbcon_va = fb_va;

	/* Clear console viewport (rows 0..38) */
	xzs_fbcon_clear(XZS_COLOR_BG);
	xzs_diag_emit("[XZS-ATTACH] 2: clear done\n");

	/* Render permanent hardware test bars onto lines 640..1919 (rows 40..119) */
	xzs_fbcon_draw_test_bars(g_fbcon_va);
	xzs_diag_emit("[XZS-ATTACH] 2b: test bars rendered\n");

	/* Render Title Banner */
	g_fbcon_row = 0;
	g_fbcon_col = 0;
	xzs_fbcon_puts_raw("=======================================================================================================================================\n", XZS_COLOR_CYAN, XZS_COLOR_BG);
	xzs_fbcon_puts_raw("  Sony Xperia XZs (G8231 / MSM8996) - Native XNU Framebuffer Console [1080x1920, 135x120 text, XRGB8888, 60Hz Autonomous Refresh]     \n", XZS_COLOR_GREEN, XZS_COLOR_BG);
	xzs_fbcon_puts_raw("=======================================================================================================================================\n", XZS_COLOR_CYAN, XZS_COLOR_BG);
	xzs_diag_emit("[XZS-ATTACH] 3: banner done\n");

	/* Determine early log source (Section 10 & 11) - prioritize clean RAM ring */
	const char *log_src = NULL;
	uint32_t    log_len = 0;

	if (g_fbcon_early_bytes_captured > 0) {
		log_src = g_fbcon_early_ring;
		log_len = g_fbcon_early_bytes_captured;
		if (log_len > XZS_EARLY_RING_CAP) log_len = XZS_EARLY_RING_CAP;
	} else if (g_dram_log != NULL && g_dram_log->magic == 0x585a5344u) {
		log_src = (const char *)(uintptr_t)g_dram_log->data;
		log_len = g_dram_log->write_offset;
		if (log_len > 65400u) log_len = 65400u;
	}

	/* Replay early log in one fast pass (Section 26) */
	if (log_src != NULL && log_len > 0) {
		xzs_diag_emit("[XZS-ATTACH] 4: replay start\n");
		xzs_fbcon_replay_optimized(log_src, log_len);
		xzs_diag_emit("[XZS-ATTACH] 4: replay done\n");
	}

	/* Render Attachment Separator Marker (Section 27) */
	g_fbcon_col = 0;
	if (g_fbcon_row + 2 < XZS_FBCON_ROWS) {
		g_fbcon_row++;
	}
	xzs_fbcon_puts_raw("--- [XZS-FBCON] Framebuffer Console Attached - Live Kernel & Shell Display Active ---\n", XZS_COLOR_YELLOW, XZS_COLOR_BG);
	xzs_diag_emit("[XZS-ATTACH] 5: marker done\n");

	/* Render Divider Bar at row 39 */
	for (uint32_t c = 0; c < XZS_FBCON_COLS; c++) {
		xzs_fbcon_draw_glyph_raw(c, XZS_FBCON_DIVIDER_ROW, '=', XZS_COLOR_YELLOW, XZS_COLOR_BG);
	}
	const char *div_text = " [XZS-FBCON] LIVE CONSOLE (ROWS 0-38) | HARDWARE TEST BARS (ROWS 40-119) ";
	uint32_t div_len = (uint32_t)strlen(div_text);
	uint32_t div_start = (XZS_FBCON_COLS > div_len) ? (XZS_FBCON_COLS - div_len) / 2 : 0;
	for (uint32_t c = 0; c < div_len && (div_start + c) < XZS_FBCON_COLS; c++) {
		xzs_fbcon_draw_glyph_raw(div_start + c, XZS_FBCON_DIVIDER_ROW, div_text[c], XZS_COLOR_GREEN, XZS_COLOR_BG);
	}

	/* Position cursor and activate live console */
	xzs_fbcon_draw_cursor_raw(g_fbcon_col, g_fbcon_row, true);

	/* Drain stores to ensure LCD scanout picks up initial frame */
	__asm__ volatile("dmb oshst" ::: "memory");
	xzs_diag_emit("[XZS-ATTACH] 6: attach complete\n");
	g_fbcon_attached = true;

	/* Kick first frame to physical LCD */
	xzs_d8m8_kick_frame();
}

/* Boot Timing Telemetry Output (Section 29 & 30) */
void
xzs_print_boot_timing(void)
{
	if (g_xzs_boot_t7 == 0) {
		__asm__ volatile("mrs %0, cntvct_el0" : "=r"(g_xzs_boot_t7));
	}
	uint64_t frq = 19200000ULL;
	uint64_t t0 = g_xzs_boot_t0 ? g_xzs_boot_t0 : 1;
	uint64_t ms_t1 = g_xzs_boot_t1 > t0 ? ((g_xzs_boot_t1 - t0) * 1000ULL) / frq : 0;
	uint64_t ms_t2 = g_xzs_boot_t2 > t0 ? ((g_xzs_boot_t2 - t0) * 1000ULL) / frq : 0;
	uint64_t ms_t3 = g_xzs_boot_t3 > t0 ? ((g_xzs_boot_t3 - t0) * 1000ULL) / frq : 0;
	uint64_t ms_t4 = g_xzs_boot_t4 > t0 ? ((g_xzs_boot_t4 - t0) * 1000ULL) / frq : 0;
	uint64_t ms_t5 = g_xzs_boot_t5 > t0 ? ((g_xzs_boot_t5 - t0) * 1000ULL) / frq : 0;
	uint64_t ms_t6 = g_xzs_boot_t6 > t0 ? ((g_xzs_boot_t6 - t0) * 1000ULL) / frq : 0;
	uint64_t ms_t7 = g_xzs_boot_t7 > t0 ? ((g_xzs_boot_t7 - t0) * 1000ULL) / frq : 0;

	extern void xzs_bringup_console_write(const void *buf, int len);
	char buf[512];
	int len;

	const char *hdr = "\n=======================================================\n"
	                  "=== [XZS-BOOT] BOOT TIMING TELEMETRY (19.2 MHz ARM) ===\n"
	                  "=======================================================\n";
	xzs_bringup_console_write(hdr, (int)strlen(hdr));

	len = snprintf(buf, sizeof(buf), "[XZS-BOOT] T0 (XNU entry)      :        0 ms\n"
	                                 "[XZS-BOOT] T1 (Platform init)  : %8llu ms\n"
	                                 "[XZS-BOOT] T2 (Mach scheduler) : %8llu ms\n"
	                                 "[XZS-BOOT] T3 (BSD_INIT)       : %8llu ms\n"
	                                 "[XZS-BOOT] T4 (VFS/root ready) : %8llu ms\n"
	                                 "[XZS-BOOT] T5 (display/fbcon)  : %8llu ms\n"
	                                 "[XZS-BOOT] T6 (PID1 entered)   : %8llu ms\n"
	                                 "[XZS-BOOT] T7 (shell ready)    : %8llu ms\n"
	                                 "XNU_TO_DISPLAY_MS=%llu\n"
	                                 "XNU_TO_PID1_MS=%llu\n"
	                                 "XNU_TO_SHELL_MS=%llu\n"
	                                 "=======================================================\n\n",
	               ms_t1, ms_t2, ms_t3, ms_t4, ms_t5, ms_t6, ms_t7,
	               ms_t5, ms_t6, ms_t7);
	if (len > 0) {
		if ((size_t)len >= sizeof(buf)) {
			len = (int)sizeof(buf) - 1;
		}
		xzs_bringup_console_write(buf, len);
	}
	const char *done_msg = "[XZS-TIMING-DONE]\n";
	xzs_bringup_console_write(done_msg, (int)strlen(done_msg));
}

void
xzs_record_boot_t7_and_print(void)
{
	if (g_xzs_boot_t7 == 0) {
		__asm__ volatile("mrs %0, cntvct_el0" : "=r"(g_xzs_boot_t7));
		xzs_print_boot_timing();
	}
}

#endif /* XZS_FBCON_IMPL */

#endif /* _XZS_FBCON_H_ */
