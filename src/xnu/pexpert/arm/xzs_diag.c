/*
 * One diagnostic backend for the bring-up shell.
 * Each line is emitted once. xzs_bringup_console_write mirrors that
 * line to the USB console and to the existing pstore path.
 * Display, DSI, PHY, and clock blocks are not read here: the reference
 * tree marks MDSS disabled behind a power domain, and this port has no
 * proof that those registers are safe to touch.
 */

#include <stdint.h>
#include <pexpert/pexpert.h>
#include <pexpert/arm64/boot.h>

extern void xzs_bringup_console_write(const void *buf, int len);
extern volatile uint32_t g_xzs_usb_irq_count;
extern volatile uint32_t g_xzs_uart_rx_irq_count;

#define XZS_DIAG_RETAIN (8 * 1024)

static char s_retain[XZS_DIAG_RETAIN];
static uint32_t s_retain_len;

static void xzs_display_dump_state(void);
static void xzs_dsi_dump_state(void);
static void xzs_clock_dump_state(void);
static void xzs_irq_dump_state(void);
static void xzs_fb_dump_state(void);
static void xzs_memory_dump_state(void);
void xzs_diag_dispatch(uint64_t which);

static void
xzs_diag_emit(const char *s)
{
	int n = 0;

	if (s == NULL) {
		return;
	}
	while (s[n] != '\0') {
		s_retain[s_retain_len % XZS_DIAG_RETAIN] = s[n];
		s_retain_len++;
		n++;
	}
	xzs_bringup_console_write(s, n);
}

static void
xzs_diag_hex_line(const char *label, uint64_t value)
{
	char line[80];
	static const char hex[] = "0123456789abcdef";
	int i = 0;
	int h;

	while (label[i] != '\0' && i < 48) {
		line[i] = label[i];
		i++;
	}
	line[i++] = '0';
	line[i++] = 'x';
	for (h = 15; h >= 0; h--) {
		line[i++] = hex[(value >> (h * 4)) & 0xf];
	}
	line[i++] = '\n';
	line[i] = '\0';
	xzs_diag_emit(line);
}

static const boot_args *
xzs_diag_boot_args(void)
{
	if (PE_state.bootArgs == NULL) {
		return NULL;
	}
	return (const boot_args *)PE_state.bootArgs;
}

/*
 * Linux persistent_ram header. TWRP reads 0xa7f00000 as the first
 * record and 0xa7fbe000 as the console record when its zones are
 * 4 KiB dumps plus a 256 KiB console. Both addresses are inside the
 * reserved 1 MiB region and outside the 88 MiB kernel window.
 */
#define XZS_PSTORE_SIG 0x43474244u
#define XZS_PSTORE_CONSOLE 0xa7fbe000UL
#define XZS_PSTORE_RECORD0 0xa7f00000UL

extern uint64_t g_xzs_ttbr0;

static void
xzs_persist_hdr(volatile uint32_t *hdr, uint32_t *sig, uint32_t *size)
{
	if (sig != NULL) {
		*sig = hdr[0];
	}
	if (size != NULL) {
		*size = hdr[2];
	}
}

static void
xzs_persist_with_ttbr0(void (*fn)(void *), void *arg)
{
	uint64_t saved = 0;

	__asm__ volatile("mrs %0, TTBR0_EL1" : "=r"(saved));
	if (g_xzs_ttbr0 != 0) {
		__asm__ volatile("msr TTBR0_EL1, %0; isb sy" :: "r"(g_xzs_ttbr0) : "memory");
	}
	fn(arg);
	__asm__ volatile("dsb sy" ::: "memory");
	if (g_xzs_ttbr0 != 0) {
		__asm__ volatile("msr TTBR0_EL1, %0; isb sy" :: "r"(saved) : "memory");
	}
}

static void
xzs_persist_reset_fn(void *arg)
{
	volatile uint32_t *console = (volatile uint32_t *)XZS_PSTORE_CONSOLE;
	volatile uint32_t *record0 = (volatile uint32_t *)XZS_PSTORE_RECORD0;

	(void)arg;
	console[0] = XZS_PSTORE_SIG;
	console[1] = 0;
	console[2] = 0;
	record0[0] = XZS_PSTORE_SIG;
	record0[1] = 0;
	record0[2] = 0;
}

static uint32_t s_console_sig;
static uint32_t s_console_size;
static uint32_t s_record0_sig;
static uint32_t s_record0_size;
static int s_persist_armed;

static void
xzs_persist_read_fn(void *arg)
{
	(void)arg;
	xzs_persist_hdr((volatile uint32_t *)XZS_PSTORE_CONSOLE,
	    &s_console_sig, &s_console_size);
	xzs_persist_hdr((volatile uint32_t *)XZS_PSTORE_RECORD0,
	    &s_record0_sig, &s_record0_size);
}

static void
xzs_persist_marker(void)
{
	if (s_persist_armed) {
		return;
	}
	s_persist_armed = 1;
	xzs_persist_with_ttbr0(xzs_persist_reset_fn, NULL);
	xzs_diag_emit("[XZS-PSTORE] MAGIC=XZSP\n");
	xzs_diag_emit("[XZS-PSTORE] VERSION=1\n");
	xzs_diag_emit("[XZS-PSTORE] TEST=1122334455667788\n");
	xzs_diag_emit("[XZS-PSTORE] CHECKPOINT=PERSIST_TEST\n");
	xzs_persist_with_ttbr0(xzs_persist_read_fn, NULL);
	xzs_diag_hex_line("[XZS-PSTORE] CONSOLE_SIG=", s_console_sig);
	xzs_diag_hex_line("[XZS-PSTORE] CONSOLE_SIZE=", s_console_size);
	xzs_diag_hex_line("[XZS-PSTORE] RECORD0_SIG=", s_record0_sig);
	xzs_diag_hex_line("[XZS-PSTORE] RECORD0_SIZE=", s_record0_size);
}

/*
 * MMCC lives at 0x008c0000, inside the already-mapped device window
 * 0x00000000-0x01ffffff. GDSC and branch registers are the clock
 * controller, not the MDSS slave behind MDSS_GDSC. MDSS, MDP, DSI,
 * PHY, and PLL addresses are not read.
 */
#define XZS_MMCC_BASE 0x008c0000UL
#define XZS_MMCC_MDSS_GDSC 0x2304u
#define XZS_MMCC_MMAGIC_MDSS_GDSC 0x247cu
#define XZS_MMCC_MDSS_AHB 0x2308u
#define XZS_MMCC_MDSS_AXI 0x2310u
#define XZS_MMCC_MDSS_PCLK0 0x2314u
#define XZS_MMCC_MDSS_MDP 0x231cu
#define XZS_MMCC_MDSS_BYTE0 0x233cu
#define XZS_MMCC_MDSS_ESC0 0x2344u
#define XZS_MMCC_CFG_AHB 0x5054u

static uint32_t
xzs_mmcc_read32(uint32_t offset)
{
	uint64_t saved = 0;
	uint32_t value;

	__asm__ volatile("mrs %0, TTBR0_EL1" : "=r"(saved));
	if (g_xzs_ttbr0 != 0) {
		__asm__ volatile("msr TTBR0_EL1, %0; isb sy" :: "r"(g_xzs_ttbr0) : "memory");
	}
	value = *(volatile uint32_t *)(XZS_MMCC_BASE + offset);
	__asm__ volatile("dsb sy" ::: "memory");
	if (g_xzs_ttbr0 != 0) {
		__asm__ volatile("msr TTBR0_EL1, %0; isb sy" :: "r"(saved) : "memory");
	}
	return value;
}

static void
xzs_mmcc_reg(const char *name, uint32_t offset)
{
	uint32_t value;

	xzs_diag_emit("[XZS-D8M1] MMCC_READ_PRE ");
	xzs_diag_emit(name);
	xzs_diag_emit("\n");
	value = xzs_mmcc_read32(offset);
	xzs_diag_emit("[XZS-D8M1] MMCC_READ_POST ");
	xzs_diag_emit(name);
	xzs_diag_emit("\n");
	xzs_diag_hex_line("[XZS-D8M1] mmio ", value);
	xzs_diag_emit("[XZS-D8M1] source=mmio reg=");
	xzs_diag_emit(name);
	xzs_diag_emit(" bit0=");
	xzs_diag_emit((value & 1u) ? "1" : "0");
	xzs_diag_emit(" bit31=");
	xzs_diag_emit((value & 0x80000000u) ? "1\n" : "0\n");
}

static void
xzs_display_dump_state(void)
{
	xzs_diag_emit("[XZS-D8M1] DISPLAY_AUDIT_ENTER\n");
	xzs_diag_emit("[XZS-D8M1] mdss_base=0x00900000 source=reference\n");
	xzs_diag_emit("[XZS-D8M1] mdp_base=0x00901000 source=reference\n");
	xzs_diag_emit("[XZS-D8M1] mdss_regs=not_read class=REQUIRES_POWER_DOMAIN\n");
	xzs_mmcc_reg("mmagic_mdss_gdscr", XZS_MMCC_MMAGIC_MDSS_GDSC);
	xzs_mmcc_reg("mdss_gdscr", XZS_MMCC_MDSS_GDSC);
	xzs_diag_emit("[XZS-D8M1] gdsc_bit31=pwr_on bit0=sw_collapse source=mmio\n");
	xzs_diag_emit("[XZS-D8M1] panel=UNKNOWN backlight=UNKNOWN fb_handoff=NONE\n");
	xzs_diag_emit("[XZS-D8M1] DISPLAY_AUDIT_DONE\n");
}

static void
xzs_dsi_dump_state(void)
{
	xzs_diag_emit("[XZS-D8M1] DSI_AUDIT_ENTER\n");
	xzs_diag_emit("[XZS-D8M1] dsi0_base=0x00994000 source=reference class=REQUIRES_POWER_DOMAIN\n");
	xzs_diag_emit("[XZS-D8M1] phy_base=0x00994400 lane=0x00994500 pll=0x00994800 source=reference\n");
	xzs_diag_emit("[XZS-D8M1] dsi_phy_pll_regs=not_read\n");
	xzs_mmcc_reg("mdss_byte0_cbcr", XZS_MMCC_MDSS_BYTE0);
	xzs_mmcc_reg("mdss_pclk0_cbcr", XZS_MMCC_MDSS_PCLK0);
	xzs_mmcc_reg("mdss_esc0_cbcr", XZS_MMCC_MDSS_ESC0);
	xzs_diag_emit("[XZS-D8M1] branch_bit0=enable source=mmio\n");
	xzs_diag_emit("[XZS-D8M1] DSI_AUDIT_DONE\n");
}

static void
xzs_clock_dump_state(void)
{
	xzs_diag_emit("[XZS-D8M1] CLOCK_AUDIT_ENTER\n");
	xzs_diag_emit("[XZS-D8M1] mmcc_base=0x008c0000 source=reference\n");
	xzs_mmcc_reg("mmss_mmagic_cfg_ahb", XZS_MMCC_CFG_AHB);
	xzs_mmcc_reg("mdss_ahb_cbcr", XZS_MMCC_MDSS_AHB);
	xzs_mmcc_reg("mdss_axi_cbcr", XZS_MMCC_MDSS_AXI);
	xzs_mmcc_reg("mdss_mdp_cbcr", XZS_MMCC_MDSS_MDP);
	xzs_diag_emit("[XZS-D8M1] parents=XO GPLL0 MMPLL0 MMPLL5 DSI0PLL source=reference\n");
	xzs_diag_emit("[XZS-D8M1] CLOCK_AUDIT_DONE\n");
}

static void
xzs_irq_dump_state(void)
{
	xzs_diag_emit("[XZS-DIAG] irq\n");
	xzs_diag_emit("[XZS-D8M1] IRQ_AUDIT_ENTER\n");
	xzs_diag_emit("[XZS-D8M1] usb_irq_class=TARGET\n");
	xzs_diag_hex_line("[XZS-D8M1] usb_irq_count=", (uint64_t)g_xzs_usb_irq_count);
	xzs_diag_emit("[XZS-D8M1] uart_irq_class=TARGET\n");
	xzs_diag_hex_line("[XZS-D8M1] uart_irq_count=", (uint64_t)g_xzs_uart_rx_irq_count);
	xzs_diag_emit("[XZS-D8M1] ref_mdss_spi=83 ref_intid=115 class=CODE_AUDIT\n");
	xzs_diag_emit("[XZS-D8M1] gic_ispend_read=no\n");
	xzs_diag_emit("[XZS-D8M1] IRQ_AUDIT_DONE\n");
}

static void
xzs_fb_dump_state(void)
{
	xzs_diag_emit("[XZS-D8M1] FB_AUDIT_ENTER\n");
	xzs_diag_emit("[XZS-D8M1] handoff_class=TARGET\n");
	xzs_diag_hex_line("[XZS-D8M1] handoff_base=", PE_state.video.v_baseAddr);
	xzs_diag_hex_line("[XZS-D8M1] handoff_rowbytes=", PE_state.video.v_rowBytes);
	xzs_diag_hex_line("[XZS-D8M1] handoff_width=", PE_state.video.v_width);
	xzs_diag_hex_line("[XZS-D8M1] handoff_height=", PE_state.video.v_height);
	xzs_diag_hex_line("[XZS-D8M1] handoff_depth=", PE_state.video.v_depth);
	xzs_diag_hex_line("[XZS-D8M1] handoff_display=", PE_state.video.v_display);
	xzs_diag_emit("[XZS-D8M1] pixel_read=no\n");
	xzs_diag_emit("[XZS-D8M1] FB_AUDIT_DONE\n");
}

static void
xzs_memory_dump_state(void)
{
	const boot_args *args = xzs_diag_boot_args();

	xzs_diag_emit("[XZS-DIAG] mem\n");
	xzs_diag_emit("[XZS-D8M1] MEM_AUDIT_ENTER\n");
	xzs_diag_emit("[XZS-D8M1] class=TARGET\n");
	if (args == NULL) {
		xzs_diag_emit("[XZS-D8M1] boot_args=missing\n");
	} else {
		xzs_diag_hex_line("[XZS-D8M1] phys_base=", args->physBase);
		xzs_diag_hex_line("[XZS-D8M1] virt_base=", args->virtBase);
		xzs_diag_hex_line("[XZS-D8M1] mem_size=", args->memSize);
		xzs_diag_hex_line("[XZS-D8M1] mem_size_actual=", args->memSizeActual);
	}
	xzs_diag_hex_line("[XZS-D8M1] diag_retain_bytes=", (uint64_t)s_retain_len);
	xzs_diag_emit("[XZS-D8M1] MEM_AUDIT_DONE\n");
}

void
xzs_diag_dispatch(uint64_t which)
{
	xzs_persist_marker();
	switch (which) {
	case 1:
		xzs_display_dump_state();
		break;
	case 2:
		xzs_dsi_dump_state();
		break;
	case 3:
		xzs_clock_dump_state();
		break;
	case 4:
		xzs_irq_dump_state();
		break;
	case 5:
		xzs_fb_dump_state();
		break;
	case 6:
		xzs_memory_dump_state();
		break;
	default:
		xzs_diag_emit("[XZS-D8M1] unknown diag\n");
		break;
	}
}
