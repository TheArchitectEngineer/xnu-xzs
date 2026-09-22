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

static void
xzs_display_dump_state(void)
{
	xzs_diag_emit("[XZS-D8M1] DISPLAY_AUDIT_ENTER\n");
	xzs_diag_emit("[XZS-D8M1] class=CODE_AUDIT\n");
	xzs_diag_emit("[XZS-D8M1] mmio_read=no\n");
	xzs_diag_emit("[XZS-D8M1] reason=mdss_power_domain_not_proven\n");
	xzs_diag_emit("[XZS-D8M1] ref_mdss_phys=0x00900000\n");
	xzs_diag_emit("[XZS-D8M1] ref_mdp=0x00901000\n");
	xzs_diag_emit("[XZS-D8M1] ref_src=device/reference/msm8996.dtsi\n");
	xzs_diag_emit("[XZS-D8M1] dts_mdss_status=disabled\n");
	xzs_diag_emit("[XZS-D8M1] DISPLAY_AUDIT_DONE\n");
}

static void
xzs_dsi_dump_state(void)
{
	xzs_diag_emit("[XZS-D8M1] DSI_AUDIT_ENTER\n");
	xzs_diag_emit("[XZS-D8M1] class=CODE_AUDIT\n");
	xzs_diag_emit("[XZS-D8M1] mmio_read=no\n");
	xzs_diag_emit("[XZS-D8M1] ref_dsi0=0x00994000\n");
	xzs_diag_emit("[XZS-D8M1] ref_dsi0_phy=0x00994400\n");
	xzs_diag_emit("[XZS-D8M1] ref_dsi0_lane=0x00994500\n");
	xzs_diag_emit("[XZS-D8M1] ref_dsi0_pll=0x00994800\n");
	xzs_diag_emit("[XZS-D8M1] dts_dsi_status=disabled\n");
	xzs_diag_emit("[XZS-D8M1] DSI_AUDIT_DONE\n");
}

static void
xzs_clock_dump_state(void)
{
	xzs_diag_emit("[XZS-D8M1] CLOCK_AUDIT_ENTER\n");
	xzs_diag_emit("[XZS-D8M1] class=CODE_AUDIT\n");
	xzs_diag_emit("[XZS-D8M1] mmio_read=no\n");
	xzs_diag_emit("[XZS-D8M1] ref_clocks=MDSS_AHB MDSS_AXI MDSS_MDP MDSS_VSYNC\n");
	xzs_diag_emit("[XZS-D8M1] ref_clocks_dsi=MDSS_BYTE0 MDSS_PCLK0 MDSS_ESC0\n");
	xzs_diag_emit("[XZS-D8M1] reason=mmcc_not_mapped\n");
	xzs_diag_emit("[XZS-D8M1] CLOCK_AUDIT_DONE\n");
}

static void
xzs_irq_dump_state(void)
{
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
