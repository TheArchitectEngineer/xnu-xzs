/*
 * Built-in IOMobileFramebuffer for SWEET: the ABL's continuous-splash
 * framebuffer (1080x2400 a8r8g8b8 in RAM, the panel scans it out by
 * itself) as the main display, driven by CoreAnimation's CPU renderer.
 *
 * IOMobileFramebuffer.framework (iOS 16.7) counts /arm-io children with
 * device_type "display-subsystem", waits for that many services matching
 * IOServiceMatching("IOMobileFramebuffer"), opens each with type 0 and
 * reads canvas-width/canvas-height from the service's provider. The class
 * here (AppleCLCD, an IOMobileFramebuffer) matches the /arm-io/disp0 nub.
 *
 * User client selectors used by CoreAnimation:
 *   3  GetLayerDefaultSurface [layer, ?] -> [IOSurface port name]
 *   4  SwapBegin -> [swap id]
 *   5  SwapEnd, struct 0x46c: +0x50 swap id, +0x54 IOSurface id per layer
 *      (4 layers), +0x64 rect a / +0xc4 rect b per layer (int x,y,w,h),
 *      +0x104 enabled layer mask
 *   6  SwapWait, 7 GetID, 8 GetDisplaySize -> [w, h],
 *   12 RequestPowerChange [state]: 0 = display off (backboardd, power
 *      button / auto-lock), else on. Off fades the panel out over a few
 *      vsyncs (halving each pixel) and keeps it black until on again;
 *      the panel itself stays powered (AMOLED black pixels are off).
 *   18 IsMainDisplay,
 *   28 GetDotPitch, 72 Enable/DisableNotifications [func, refcon, type, 0]
 * Everything else is accepted and logged once.
 *
 * SwapEnd copies the enabled layers' surfaces into the panel memory (no
 * scaling, CPU copy). Vsync (notification type 5) is a 60 Hz timer that
 * sends (count, mach_absolute_time, 0) as 32-bit halves to clients that
 * enabled it, through the port they registered (IOConnectSetNotificationPort).
 *
 * The panel memory itself is published as an IOSurface and returned as the
 * default framebuffer surface. The kernel text console keeps drawing over
 * the GUI by default: pausing it (boot-arg sweetfb_noconsole=1, on the
 * first swap) switches the console to serial, which also hides panics.
 */
#include "XZSSurface.h"

#if defined(SWEET) || defined(XZS)
extern "C" {
uintptr_t xzs_fb_kernel_va(void);
uint32_t xzs_fb_kick_count(void);
void xzs_fb_present_kick(void);
void xzs_fb_set_backlight(bool on);
void xzs_fbcon_detach(void);
bool xzs_fbcon_is_active(void);
void xzs_draw_du_ma_may_banner(uintptr_t fb_va, uint32_t width, uint32_t stride, uint32_t top_y, bool clear_full_screen);
void xzs_watchdog_pet(void);
bool g_xzs_springboard_surface_ready = false;
bool g_xzs_gui_presented = false;
bool g_xzs_power_key_used = false;
}
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/system.h>
#include <pexpert/pexpert.h>
#include <kern/clock.h>
#include <kern/sched_prim.h>
#include <sys/proc.h>

enum {
	kSweetFBGetLayerDefaultSurface  = 3,
	kSweetFBSwapBegin               = 4,
	kSweetFBSwapEnd                 = 5,
	kSweetFBSwapWait                = 6,
	kSweetFBGetID                   = 7,
	kSweetFBGetDisplaySize          = 8,
	kSweetFBRequestPowerChange      = 12,
	kSweetFBIsMainDisplay           = 18,
	kSweetFBGetDotPitch             = 28,
	kSweetFBNotifications           = 72,
};

#define SWEET_FB_SWAP_REC_SIZE          0x46c
#define SWEET_FB_LAYERS                 4
#define SWEET_FB_NOTIFY_TYPES           8
#define SWEET_FB_NOTIFY_VSYNC           5
#define SWEET_FB_VSYNC_US               16667
#define SWEET_FB_DOT_PITCH              458     /* ppi, as the iPhone X panel */
/*
 * The iOS userland only has status bar / layout data for real iPhone
 * screens (UIKit: "No status bar visual provider for screen", the red
 * rdar://45025538 bar for 360x800 points). The display is therefore
 * presented as the iPhone X panel, 1125x2436 (375x812 points @3x), and the
 * centered 1080x2400 part of it is shown on the 1080x2400 panel.
 * Boot-arg sweetfb_native=1 presents the real panel size instead.
 */
#define SWEET_FB_LOGICAL_W              1125
#define SWEET_FB_LOGICAL_H              2436
#define SWEET_FB_FORMAT_BGRA            0x42475241

class IOMobileFramebuffer : public IOService {
	OSDeclareDefaultStructors(IOMobileFramebuffer);
};

OSDefineMetaClassAndStructors(IOMobileFramebuffer, IOService);

class IOMobileFramebufferUserClient;

#define XZS_FB_WORKER_THREADS           3

struct XZSFBSliceJob {
	uintptr_t src_base;
	uint32_t  src_bpr;
	uint32_t  src_w;
	uint32_t  src_h;
	uintptr_t dst_base;
	uint32_t  dst_bpr;
	int32_t   px0, px1;
	int32_t   py0, py1;
	int32_t   sx, sy;
	uint32_t  step_x, step_y;
	bool      use_1to1_map;
	bool      use_wallpaper;
	volatile uint32_t gen;
	volatile uint32_t done_count;
};

class AppleCLCD : public IOMobileFramebuffer {
	OSDeclareDefaultStructors(AppleCLCD);

	IOWorkLoop         *fWorkLoop;
	IOTimerEventSource *fVsync;
	uint64_t            fVsyncCount;

	static void vsyncFired(OSObject *owner, IOTimerEventSource *sender);
	static void presenterMain(void *arg, wait_result_t wr);
	static void helperMain(void *arg, wait_result_t wr);

public:
	uint64_t      fPhys;
	uint32_t      fWidth, fHeight, fRowBytes;
	uint32_t      fLogicalW, fLogicalH;     /* size reported to userland */
	int32_t       fOffX, fOffY;             /* logical origin of the panel */
	uintptr_t     fPixels;          /* kernel mapping of the panel memory */
	IOMemoryMap  *fMap;
	SweetSurface *fSurface;         /* the panel memory as an IOSurface */
	IOLock       *fLock;            /* swaps and client list */
	OSArray      *fClients;
	uint32_t      fSwapID;
	bool          fConsolePaused;
	uint8_t       fPending[SWEET_FB_SWAP_REC_SIZE];     /* latest swap, not yet shown */
	bool          fDirty;
	bool          fPresentReady;
	uint32_t      fFlushTicks;    /* vsync ticks between panel copies */
	bool          fPowerOff;      /* RequestPowerChange(0) */
	uint32_t      fFadeStep;      /* fade-out steps done while off */
	volatile uint32_t fNextWorkerID;
	XZSFBSliceJob fJob;

	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void addClient(IOMobileFramebufferUserClient *client);
	void removeClient(IOMobileFramebufferUserClient *client);
	void swap(const uint8_t *rec);
	void present(const uint8_t *rec);
	void renderSlice(uint32_t worker_id, uint32_t total_workers);
};

OSDefineMetaClassAndStructors(AppleCLCD, IOMobileFramebuffer);

class IOMobileFramebufferUserClient : public IOUserClient {
	OSDeclareDefaultStructors(IOMobileFramebufferUserClient);

public:
	AppleCLCD          *fFB;
	task_t              fTask;
	mach_port_t         fPort;
	io_user_reference_t fFunc[SWEET_FB_NOTIFY_TYPES];
	io_user_reference_t fRefcon[SWEET_FB_NOTIFY_TYPES];

	bool initWithTask(task_t owningTask, void *securityID, UInt32 type) APPLE_KEXT_OVERRIDE;
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	IOReturn clientClose(void) APPLE_KEXT_OVERRIDE;
	IOReturn registerNotificationPort(mach_port_t port, UInt32 type,
	    io_user_reference_t refCon) APPLE_KEXT_OVERRIDE;
	IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args,
	    IOExternalMethodDispatch *dispatch, OSObject *target,
	    void *reference) APPLE_KEXT_OVERRIDE;
	void notify(uint32_t type, const uint64_t *values, uint32_t count);
};

OSDefineMetaClassAndStructors(IOMobileFramebufferUserClient, IOUserClient);

static void
sweet_draw_apple_logo(uintptr_t base, uint32_t width, uint32_t height, uint32_t rowBytes)
{
	if (base == 0 || width == 0 || height == 0 || rowBytes < width * 4) {
		return;
	}
	xzs_draw_du_ma_may_banner(base, width, rowBytes, (height > 128u) ? (height - 128u) / 2u : 0u, true);
}

#define XZS_WP_W 270u
#define XZS_WP_H 480u
static uint32_t s_ios16_wallpaper[XZS_WP_W * XZS_WP_H];
static bool s_ios16_wallpaper_ready = false;

static void
xzs_init_ios16_wallpaper(void)
{
	if (s_ios16_wallpaper_ready) {
		return;
	}
	for (uint32_t wy = 0; wy < XZS_WP_H; wy++) {
		int32_t v = (int32_t)((wy * 1000u) / XZS_WP_H); /* 0..1000 */
		for (uint32_t wx = 0; wx < XZS_WP_W; wx++) {
			int32_t u = (int32_t)((wx * 1000u) / XZS_WP_W); /* 0..1000 */

			/* Base vertical sky-to-ocean gradient */
			int32_t r = 16 + (v * 14) / 1000;
			int32_t g = 54 + (v < 500 ? (v * 130) / 500 : ((1000 - v) * 130) / 500);
			int32_t b = 142 + (v < 500 ? (v * 82) / 500 : ((1000 - v) * 62) / 500);

			/* Wave 1: iOS 16 upper Cyan-Azure diagonal arch */
			int32_t du1 = u - 480;
			int32_t v1 = 300 - (u * 130) / 1000 + (du1 * du1) / 3200;
			int32_t d1 = v > v1 ? v - v1 : v1 - v;
			if (d1 < 250) {
				int32_t w1 = 250 - d1;
				w1 = (w1 * w1) / 250; /* smooth quadratic bell 0..250 */
				r = (r * (250 - w1) + 42 * w1) / 250;
				g = (g * (250 - w1) + 204 * w1) / 250;
				b = (b * (250 - w1) + 252 * w1) / 250;
			}

			/* Wave 2: iOS 16 middle Indigo-Violet silk ribbon */
			int32_t du2 = u - 420;
			int32_t v2 = 590 - (u * 115) / 1000 - (du2 * du2) / 3600;
			int32_t d2 = v > v2 ? v - v2 : v2 - v;
			if (d2 < 260) {
				int32_t w2 = 260 - d2;
				w2 = (w2 * w2) / 260;
				r = (r * (260 - w2) + 104 * w2) / 260;
				g = (g * (260 - w2) + 68 * w2) / 260;
				b = (b * (260 - w2) + 232 * w2) / 260;
			}

			/* Wave 3: iOS 16 lower Violet-Rose-Cyan accent wave */
			int32_t du3 = u - 600;
			int32_t v3 = 820 - (u * 90) / 1000 + (du3 * du3) / 4000;
			int32_t d3 = v > v3 ? v - v3 : v3 - v;
			if (d3 < 220) {
				int32_t w3 = 220 - d3;
				w3 = (w3 * w3) / 220;
				r = (r * (220 - w3) + 148 * w3) / 220;
				g = (g * (220 - w3) + 76 * w3) / 220;
				b = (b * (220 - w3) + 210 * w3) / 220;
			}

			if (r < 0) r = 0; else if (r > 255) r = 255;
			if (g < 0) g = 0; else if (g > 255) g = 255;
			if (b < 0) b = 0; else if (b > 255) b = 255;

			/* Packed in XZS MDP5 byte order (B0=R, B1=G, B2=B, B3=A) */
			s_ios16_wallpaper[wy * XZS_WP_W + wx] =
			    0xff000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
		}
	}
	s_ios16_wallpaper_ready = true;
}

/*
 * Map physical scanline py (0..1919 on 1080x1920 16:9 panel) to logical scanline
 * ly (0..2435 on 1125x2436 19.5:9 canvas) while preserving an exact 1:1 square
 * pixel aspect ratio (step_y = step_x = 25/24) across all active UI content bands
 * (Status Bar, Lock Screen Clock, all 5 Home Screen App Icon rows, and the Dock)
 * and compressing only the empty vertical safe-area gaps.
 */
extern "C" uint32_t
xzs_fb_py_to_ly(uint32_t py)
{
	if (py >= 1920u) {
		py = 1919u;
	}
	uint32_t ly;
	if (py < 108u) {
		/* Band 0: Status bar (108 physical px -> 112 logical px at 1:1 25/24 scale) */
		ly = (py * 25u) / 24u;
	} else if (py < 144u) {
		/* Band 1: Top safe-area gap below status bar (36 physical px -> 150 logical px) */
		ly = 112u + ((py - 108u) * 150u) / 36u;
	} else if (py < 1548u) {
		/* Band 2: Main UI content — Lock Screen Clock & Home Screen Icon Rows 1..5
		 * (1404 physical px -> 1462 logical px at exact 1:1 25/24 scale) */
		ly = 262u + ((py - 144u) * 25u) / 24u;
	} else if (py < 1620u) {
		/* Band 3: Empty margin above Dock (72 physical px -> 388 logical px) */
		ly = 1724u + ((py - 1548u) * 388u) / 72u;
	} else {
		/* Band 4: Bottom Dock & Home Bar (300 physical px -> 312 logical px at 1:1 25/24 scale) */
		ly = 2112u + ((py - 1620u) * 25u) / 24u;
	}
	if (ly >= SWEET_FB_LOGICAL_H) {
		ly = SWEET_FB_LOGICAL_H - 1u;
	}
	return ly;
}

static uint16_t s_py_to_ly_lut[1920];
static uint16_t s_px_to_lx_lut[1080];
static bool s_luts_ready = false;

static void
xzs_init_scale_luts(void)
{
	if (s_luts_ready) {
		return;
	}
	for (uint32_t py = 0; py < 1920u; py++) {
		s_py_to_ly_lut[py] = (uint16_t)xzs_fb_py_to_ly(py);
	}
	for (uint32_t px = 0; px < 1080u; px++) {
		uint32_t lx = (px * SWEET_FB_LOGICAL_W) / 1080u;
		if (lx >= SWEET_FB_LOGICAL_W) {
			lx = SWEET_FB_LOGICAL_W - 1u;
		}
		s_px_to_lx_lut[px] = (uint16_t)lx;
	}
	s_luts_ready = true;
}

bool
AppleCLCD::start(IOService *provider)
{
	PE_Video video;

	if (!IOService::start(provider)) {
		return false;
	}
	PE_current_console(&video);
	fPhys = video.v_baseAddr & ~3ULL;
	fWidth = (uint32_t)video.v_width;
	fHeight = (uint32_t)video.v_height;
	fRowBytes = (uint32_t)video.v_rowBytes;
	if (fPhys == 0 || fWidth == 0 || fHeight == 0 || fRowBytes < fWidth * 4) {
		/* Fallback to unified physical framebuffer memory at 0x98000000 (stride 4352 = 0x1100) */
		fPhys = 0x98000000ULL;
		fWidth = 1080;
		fHeight = 1920;
		fRowBytes = 4352;
		IOLog("XZS FB: using fallback cont_splash_mem at 0x%llx (%ux%u stride %u)\n", fPhys, fWidth, fHeight, fRowBytes);
	}
	if (fPhys == 0 || fWidth == 0 || fHeight == 0 || fRowBytes < fWidth * 4) {
		IOLog("SWEET FB: no boot framebuffer\n");
		return false;
	}
	int native = 0;
	PE_parse_boot_argn("sweetfb_native", &native, sizeof(native));
	fLogicalW = fWidth;
	fLogicalH = fHeight;
	if (!native && fWidth <= SWEET_FB_LOGICAL_W && fHeight <= SWEET_FB_LOGICAL_H) {
		fLogicalW = SWEET_FB_LOGICAL_W;
		fLogicalH = SWEET_FB_LOGICAL_H;
	}
	fOffX = (int32_t)(fLogicalW - fWidth) / 2;
	fOffY = (int32_t)(fLogicalH - fHeight) / 2;
	xzs_init_ios16_wallpaper();
	xzs_init_scale_luts();
	fLock = IOLockAlloc();
	fClients = OSArray::withCapacity(4);
	fSurface = sweet_surface_create_physical(fPhys, fWidth, fHeight, fRowBytes, SWEET_FB_FORMAT_BGRA);
	fPixels = xzs_fb_kernel_va();
	if (fPixels == 0 && fSurface != NULL) {
		fPixels = fSurface->kernelAddress();
	}
	fWorkLoop = IOWorkLoop::workLoop();
	if (fLock == NULL || fClients == NULL || fSurface == NULL || fPixels == 0 || fWorkLoop == NULL) {
		IOLog("SWEET FB: setup failed\n");
		OSSafeReleaseNULL(fSurface);    /* stays published: also held by the table */
		OSSafeReleaseNULL(fWorkLoop);
		return false;
	}
	fVsync = IOTimerEventSource::timerEventSource(this, &AppleCLCD::vsyncFired);
	if (fVsync == NULL || fWorkLoop->addEventSource(fVsync) != kIOReturnSuccess) {
		IOLog("SWEET FB: no vsync timer\n");
		return false;
	}
	uint32_t hz = 60;
	PE_parse_boot_argn("sweetfb_hz", &hz, sizeof(hz));
	fFlushTicks = (hz == 0 || hz >= 60) ? 1 : 60 / hz;

	/* Start parallel row-slice helper threads + dedicated presenter thread (hackios MiniIOMobileFramebuffer architecture) */
	fNextWorkerID = 1;
	for (uint32_t w = 1; w < XZS_FB_WORKER_THREADS; w++) {
		thread_t hth = THREAD_NULL;
		if (kernel_thread_start(&AppleCLCD::helperMain, this, &hth) == KERN_SUCCESS) {
			thread_deallocate(hth);
		}
	}
	thread_t pth = THREAD_NULL;
	if (kernel_thread_start(&AppleCLCD::presenterMain, this, &pth) == KERN_SUCCESS) {
		thread_deallocate(pth);
	}

	fVsync->setTimeoutUS(SWEET_FB_VSYNC_US);

	/* Keep verbose boot console + "ĐU MA MAY" banner active on LCD until SpringBoard presents its first GUI frame */
	int no_console = 0;
	(void)PE_parse_boot_argn("sweetfb_noconsole", &no_console, sizeof(no_console));
	if (no_console != 0) {
		fConsolePaused = true;
		xzs_fbcon_detach();
		sweet_draw_apple_logo(fPixels, fWidth, fHeight, fRowBytes);
		xzs_fb_present_kick();
		IOLog("SWEET FB: presented DU MA MAY banner at 0x%llx (va=0x%lx kicks=%u)\n",
		    fPhys, (unsigned long)fPixels, xzs_fb_kick_count());
	} else {
		fConsolePaused = false;
		IOLog("SWEET FB: keeping verbose boot console + DU MA MAY banner active on LCD until SpringBoard GUI is ready\n");
	}

	setProperty(kIOUserClientClassKey, "IOMobileFramebufferUserClient");
	setProperty("external", false);
	setProperty("DisplayPipePlaneBaseAlignment", 64ULL, 32);
	registerService();
	IOLog("SWEET FB: AppleCLCD %ux%u stride %u at 0x%llx (va=0x%lx), default surface %u, presented as %ux%u (%u Hz, %u threads)\n",
	    fWidth, fHeight, fRowBytes, fPhys, (unsigned long)fPixels, fSurface->id, fLogicalW, fLogicalH, hz, XZS_FB_WORKER_THREADS);
	return true;
}

void
AppleCLCD::vsyncFired(OSObject *owner, IOTimerEventSource *sender)
{
	AppleCLCD *fb = (AppleCLCD *)owner;
	uint64_t now = mach_absolute_time();
	uint64_t count = ++fb->fVsyncCount;
	uint64_t values[6] = {
		(uint32_t)count, (uint32_t)(count >> 32),
		(uint32_t)now, (uint32_t)(now >> 32),
		0, 0,
	};

	IOLockLock(fb->fLock);
	for (unsigned int i = 0; i < fb->fClients->getCount(); i++) {
		IOMobileFramebufferUserClient *client =
		    (IOMobileFramebufferUserClient *)fb->fClients->getObject(i);
		client->notify(SWEET_FB_NOTIFY_VSYNC, values, 6);
	}
	if (!fb->fPowerOff && fb->fDirty && (count % fb->fFlushTicks) == 0) {
		fb->fPresentReady = true;
		IOLockWakeup(fb->fLock, &fb->fPresentReady, true);
	}
	IOLockUnlock(fb->fLock);
	sender->setTimeoutUS(SWEET_FB_VSYNC_US);
}

void
AppleCLCD::presenterMain(void *arg, __unused wait_result_t wr)
{
	AppleCLCD *fb = (AppleCLCD *)arg;
	uint8_t rec[SWEET_FB_SWAP_REC_SIZE];

	for (;;) {
		IOLockLock(fb->fLock);
		while (!fb->fPresentReady) {
			IOLockSleep(fb->fLock, &fb->fPresentReady, THREAD_UNINT);
		}
		memcpy(rec, fb->fPending, SWEET_FB_SWAP_REC_SIZE);
		fb->fPresentReady = false;
		fb->fDirty = false;
		IOLockUnlock(fb->fLock);

		fb->present(rec);
	}
}

void
AppleCLCD::helperMain(void *arg, __unused wait_result_t wr)
{
	AppleCLCD *fb = (AppleCLCD *)arg;
	uint32_t wid = __atomic_fetch_add(&fb->fNextWorkerID, 1u, __ATOMIC_RELAXED);
	uint32_t seen_gen = 0;

	for (;;) {
		assert_wait((event_t)&fb->fJob.gen, THREAD_UNINT);
		uint32_t cur_gen = __atomic_load_n(&fb->fJob.gen, __ATOMIC_ACQUIRE);
		if (cur_gen == seen_gen) {
			thread_block(THREAD_CONTINUE_NULL);
			continue;
		}
		clear_wait(current_thread(), THREAD_AWAKENED);
		seen_gen = cur_gen;

		fb->renderSlice(wid, XZS_FB_WORKER_THREADS);
		__atomic_add_fetch(&fb->fJob.done_count, 1u, __ATOMIC_RELEASE);
		thread_wakeup((event_t)&fb->fJob.done_count);
	}
}

void
AppleCLCD::renderSlice(uint32_t worker_id, uint32_t total_workers)
{
	const XZSFBSliceJob &job = fJob;
	int32_t total_h = job.py1 - job.py0;
	int32_t slice_y0 = job.py0 + (total_h * (int32_t)worker_id) / (int32_t)total_workers;
	int32_t slice_y1 = (worker_id + 1u == total_workers) ?
	    job.py1 : (job.py0 + (total_h * (int32_t)(worker_id + 1u)) / (int32_t)total_workers);
	if (slice_y0 >= slice_y1) {
		return;
	}

	uint32_t s_w_max = job.src_w > 0 ? job.src_w - 1u : 0u;
	uint32_t acc_y = ((uint32_t)job.sy << 16) + (uint32_t)(slice_y0 - job.py0) * job.step_y;
	bool full_row_lut = job.use_1to1_map && job.px0 == 0 && job.px1 == 1080;

	for (int32_t py = slice_y0; py < slice_y1; py++) {
		uint32_t src_y;
		if (job.use_1to1_map) {
			src_y = s_py_to_ly_lut[(uint32_t)py < 1920u ? (uint32_t)py : 1919u];
		} else {
			src_y = acc_y >> 16;
			acc_y += job.step_y;
		}
		if (src_y >= job.src_h) {
			src_y = job.src_h - 1u;
		}
		const uint32_t *src_row = (const uint32_t *)(job.src_base + (uint64_t)src_y * job.src_bpr);
		uint32_t *dst_row = (uint32_t *)(job.dst_base + (uint64_t)py * job.dst_bpr);
		const uint32_t *wp_row = job.use_wallpaper ?
		    &s_ios16_wallpaper[((uint32_t)py >> 2) * XZS_WP_W] : NULL;
		uint32_t acc_x = (uint32_t)job.sx << 16;

		if (wp_row == NULL && full_row_lut) {
			for (int32_t px = 0; px < 1080; px++) {
				uint32_t p = src_row[s_px_to_lx_lut[(uint32_t)px]];
				dst_row[px] = (p & 0xff00ff00u) | ((p & 0x00ff0000u) >> 16) | ((p & 0x000000ffu) << 16);
			}
			continue;
		}

		for (int32_t px = job.px0; px < job.px1; px++) {
			uint32_t lx;
			if (full_row_lut) {
				lx = s_px_to_lx_lut[(uint32_t)px];
			} else {
				lx = acc_x >> 16;
				acc_x += job.step_x;
			}
			uint32_t p = src_row[lx];
			if (wp_row != NULL) {
				uint32_t rgb = p & 0x00ffffffu;
				if (__builtin_expect(rgb == 0u, 1)) {
					/* Fast path for pure-black wallpaper background pixels */
					uint32_t nl = src_row[lx > 14u ? lx - 14u : 0u];
					uint32_t nr = src_row[lx + 14u <= s_w_max ? lx + 14u : s_w_max];
					if (((nl & nr) & 0x00c0c0c0u) != 0x00c0c0c0u) {
						dst_row[px] = wp_row[(uint32_t)px >> 2];
						continue;
					}
				} else if (__builtin_expect((rgb & 0x00e0e0e0u) == 0u, 0)) {
					/* Smooth edge anti-aliasing blend for dark pixels (max_c < 32) */
					uint32_t nl = src_row[lx > 14u ? lx - 14u : 0u];
					uint32_t nr = src_row[lx + 14u <= s_w_max ? lx + 14u : s_w_max];
					if (((nl & nr) & 0x00c0c0c0u) != 0x00c0c0c0u) {
						uint32_t b = rgb & 0xffu;
						uint32_t g = (rgb >> 8) & 0xffu;
						uint32_t r = (rgb >> 16) & 0xffu;
						uint32_t max_c = r > g ? (r > b ? r : b) : (g > b ? g : b);
						if (max_c < 28u) {
							uint32_t wp = wp_row[(uint32_t)px >> 2];
							uint32_t wf = max_c * 9u;
							uint32_t ww = 256u - wf;
							uint32_t wr = wp & 0xffu;
							uint32_t wg = (wp >> 8) & 0xffu;
							uint32_t wb = (wp >> 16) & 0xffu;
							uint32_t or_c = (wr * ww + r * wf) >> 8;
							uint32_t og_c = (wg * ww + g * wf) >> 8;
							uint32_t ob_c = (wb * ww + b * wf) >> 8;
							dst_row[px] = 0xff000000u | (ob_c << 16) | (og_c << 8) | or_c;
							continue;
						}
					}
				}
			}
			/* Convert iOS BGRA (B0=B, B1=G, B2=R, B3=A) to XZS MDP5 unpack (B0=R, B1=G, B2=B, B3=A) */
			dst_row[px] = (p & 0xff00ff00u) | ((p & 0x00ff0000u) >> 16) | ((p & 0x000000ffu) << 16);
		}
	}
}

void
AppleCLCD::addClient(IOMobileFramebufferUserClient *client)
{
	IOLockLock(fLock);
	fClients->setObject(client);
	IOLockUnlock(fLock);
}

void
AppleCLCD::removeClient(IOMobileFramebufferUserClient *client)
{
	IOLockLock(fLock);
	unsigned int i = fClients->getNextIndexOfObject(client, 0);
	if (i != (unsigned int)-1) {
		fClients->removeObject(i);
	}
	IOLockUnlock(fLock);
}

static uint32_t
get32(const uint8_t *p, uint32_t off)
{
	uint32_t v;
	memcpy(&v, p + off, 4);
	return v;
}

static uint32_t sweet_fb_swaps_logged;
static uint64_t sweet_fb_swap_count;

/* Queue the swap and wake the async presenter thread immediately. */
void
AppleCLCD::swap(const uint8_t *rec)
{
	IOLockLock(fLock);
	memcpy(fPending, rec, SWEET_FB_SWAP_REC_SIZE);
	fDirty = true;
	fPresentReady = true;
	IOLockWakeup(fLock, &fPresentReady, true);
	IOLockUnlock(fLock);
	if ((++sweet_fb_swap_count % 1000) == 0) {
		IOLog("XZS FB: %llu swaps\n", sweet_fb_swap_count);
	}
}

void
AppleCLCD::present(const uint8_t *rec)
{
	uint32_t mask = get32(rec, 0x104);
	bool any_drawn = false;

	for (uint32_t layer = 0; layer < SWEET_FB_LAYERS; layer++) {
		uint32_t id = get32(rec, 0x54 + 4 * layer);
		const uint8_t *a = rec + 0x64 + 16 * layer;
		const uint8_t *b = rec + 0xc4 + 16 * layer;
		int32_t sx = (int32_t)get32(a, 0), sy = (int32_t)get32(a, 4);
		int32_t sw = (int32_t)get32(a, 8), sh = (int32_t)get32(a, 12);
		int32_t dx = (int32_t)get32(b, 0), dy = (int32_t)get32(b, 4);
		int32_t w = (int32_t)get32(b, 8), h = (int32_t)get32(b, 12);

		if (!(mask & (1u << layer)) || id == 0) {
			continue;
		}
		SweetSurface *s = sweet_surface_copy(id);
		if (sweet_fb_swaps_logged <= 64) {
			sweet_fb_swaps_logged++;
		}
		if (sweet_fb_swaps_logged <= 8) {
			IOLog("SWEET FB: swap %u layer %u surface %u %ux%u fmt 0x%x bpr %u a %d,%d %dx%d b %d,%d %dx%d\n",
			    get32(rec, 0x50), layer, id, s ? s->width : 0, s ? s->height : 0,
			    s ? s->pixelFormat : 0, s ? s->bytesPerRow : 0, sx, sy, sw,
			    sh, dx, dy, w, h);
		}
		if (s == NULL || s == fSurface) {
			OSSafeReleaseNULL(s);
			continue;
		}
		uintptr_t src = s->kernelAddress();
		uint32_t bpe = s->bytesPerElement ? s->bytesPerElement : 4;
		if (w <= 0 || h <= 0) {
			w = (int32_t)s->width;
			h = (int32_t)s->height;
		}
		if (sw <= 0 || sh <= 0) {
			sw = w;
			sh = h;
		}
		if (sx < 0) sx = 0;
		if (sy < 0) sy = 0;
		if (dx < 0) {
			sx -= dx;
			sw += dx;
			w += dx;
			dx = 0;
		}
		if (dy < 0) {
			sy -= dy;
			sh += dy;
			h += dy;
			dy = 0;
		}
		if (sw > (int32_t)s->width - sx) {
			sw = (int32_t)s->width - sx;
		}
		if (sh > (int32_t)s->height - sy) {
			sh = (int32_t)s->height - sy;
		}
		if (w > (int32_t)fLogicalW - dx) {
			w = (int32_t)fLogicalW - dx;
		}
		if (h > (int32_t)fLogicalH - dy) {
			h = (int32_t)fLogicalH - dy;
		}
		/* Scale logical (fLogicalW x fLogicalH) rect onto physical panel (fWidth x fHeight) */
		int32_t px0 = (int32_t)(((int64_t)dx * fWidth) / fLogicalW);
		int32_t py0 = (int32_t)(((int64_t)dy * fHeight) / fLogicalH);
		int32_t px1 = (int32_t)(((int64_t)(dx + w) * fWidth) / fLogicalW);
		int32_t py1 = (int32_t)(((int64_t)(dy + h) * fHeight) / fLogicalH);
		int32_t pw = px1 - px0;
		int32_t ph = py1 - py0;

		uint64_t last = (uint64_t)s->offset + (uint64_t)(sy + sh - 1) * s->bytesPerRow +
		    (uint64_t)(sx + sw) * 4;
		if (pw <= 0 || ph <= 0 || sw <= 0 || sh <= 0 ||
		    s->elementWidth != 1 || s->elementHeight != 1 ||
		    s->memory == NULL || last > s->memory->getLength()) {
			src = 0;
		}
		if (src != 0 && bpe == 4) {
			uint32_t non_black_samples = 0;
			uint32_t black_samples = 0;
			uint32_t total_samples = 0;
			uint32_t max_rgb = 0;
			uint32_t sample_rgb = 0;
			uint32_t step_check_y = g_xzs_gui_presented ?
			    (sh > 16 ? (uint32_t)(sh / 16) : 1u) :
			    (sh > 64 ? (uint32_t)(sh / 64) : 1u);
			int32_t step_check_x = g_xzs_gui_presented ? 32 : 8;
			for (int32_t cy = sy; cy < sy + sh; cy += (int32_t)step_check_y) {
				const uint32_t *crow = (const uint32_t *)(src + s->offset + (uint64_t)cy * s->bytesPerRow);
				for (int32_t cx = sx; cx < sx + sw; cx += step_check_x) {
					uint32_t rgb = crow[cx] & 0x00ffffffu;
					total_samples++;
					if (rgb != 0) {
						non_black_samples++;
						if (sample_rgb == 0) {
							sample_rgb = rgb;
						}
						uint32_t c0 = rgb & 0xffu;
						uint32_t c1 = (rgb >> 8) & 0xffu;
						uint32_t c2 = (rgb >> 16) & 0xffu;
						if (c0 > max_rgb) max_rgb = c0;
						if (c1 > max_rgb) max_rgb = c1;
						if (c2 > max_rgb) max_rgb = c2;
					} else {
						black_samples++;
					}
				}
			}

			/* If fbcon is still active, wait until SpringBoard has created its UI surfaces
			 * and presented a non-black frame before detaching the boot console. */
			if (!fConsolePaused) {
				if (g_xzs_springboard_surface_ready && non_black_samples > 0 && max_rgb >= 0x20u) {
					int keep_console = 0;
					fConsolePaused = true;
					g_xzs_gui_presented = true;
					IOLog("SWEET FB: first visible SpringBoard GUI frame presented (swap %u surface %u %ux%u -> %ux%u non_black=%u max_rgb=0x%02x sample=0x%06x), switching LCD to iOS GUI\n",
					    get32(rec, 0x50), s->id, s->width, s->height, fWidth, fHeight,
					    non_black_samples, max_rgb, sample_rgb);
					if (!PE_parse_boot_argn("sweetfb_console", &keep_console, sizeof(keep_console)) || keep_console == 0) {
						xzs_fbcon_detach();
					}
				} else if (xzs_fbcon_is_active()) {
					if (sweet_fb_swaps_logged <= 64) {
						IOLog("SWEET FB: swap %u keeping boot console active (sb_ready=%d non_black=%u max_rgb=0x%02x)\n",
						    get32(rec, 0x50), g_xzs_springboard_surface_ready ? 1 : 0, non_black_samples, max_rgb);
					}
					s->release();
					continue;
				}
			} else {
				/* Never overwrite the rendered iOS GUI on the physical LCD with an all-black or dimming fade-out frame */
				if (non_black_samples == 0 || max_rgb < 0x20u) {
					if (sweet_fb_swaps_logged <= 64) {
						IOLog("SWEET FB: swap %u skipping dim/black surface %u (non_black=%u max_rgb=0x%02x) to preserve GUI on LCD\n",
						    get32(rec, 0x50), s->id, non_black_samples, max_rgb);
					}
					s->release();
					continue;
				}
				if (g_xzs_springboard_surface_ready && !g_xzs_gui_presented) {
					g_xzs_gui_presented = true;
					IOLog("SWEET FB: first visible SpringBoard GUI frame presented (swap %u surface %u %ux%u -> %ux%u non_black=%u max_rgb=0x%02x sample=0x%06x), switching LCD to iOS GUI\n",
					    get32(rec, 0x50), s->id, s->width, s->height, fWidth, fHeight,
					    non_black_samples, max_rgb, sample_rgb);
				} else if (sweet_fb_swaps_logged <= 8) {
					IOLog("SWEET FB: swap %u presented surface %u (non_black=%u max_rgb=0x%02x sample=0x%06x)\n",
					    get32(rec, 0x50), s->id, non_black_samples, max_rgb, sample_rgb);
				}
			}

			fJob.src_base = src + s->offset;
			fJob.src_bpr  = s->bytesPerRow;
			fJob.src_w    = s->width;
			fJob.src_h    = s->height;
			fJob.dst_base = fPixels;
			fJob.dst_bpr  = fRowBytes;
			fJob.px0      = px0;
			fJob.px1      = px1;
			fJob.py0      = py0;
			fJob.py1      = py1;
			fJob.sx       = sx;
			fJob.sy       = sy;
			fJob.step_x   = ((uint32_t)sw << 16) / (uint32_t)pw;
			fJob.step_y   = ((uint32_t)sh << 16) / (uint32_t)ph;
			fJob.use_1to1_map = (fWidth == 1080u && fHeight == 1920u &&
			    fLogicalW == SWEET_FB_LOGICAL_W && fLogicalH == SWEET_FB_LOGICAL_H &&
			    sy == 0 && sh == (int32_t)SWEET_FB_LOGICAL_H &&
			    py0 == 0 && py1 == 1920);
			fJob.use_wallpaper = s_ios16_wallpaper_ready &&
			    g_xzs_springboard_surface_ready &&
			    total_samples > 0 && (black_samples * 3u > total_samples);

			if (ph >= 192 && XZS_FB_WORKER_THREADS > 1) {
				__atomic_store_n(&fJob.done_count, 0u, __ATOMIC_RELAXED);
				__atomic_add_fetch(&fJob.gen, 1u, __ATOMIC_RELEASE);
				thread_wakeup((event_t)&fJob.gen);

				renderSlice(0u, XZS_FB_WORKER_THREADS);

				while (__atomic_load_n(&fJob.done_count, __ATOMIC_ACQUIRE) < (XZS_FB_WORKER_THREADS - 1u)) {
					assert_wait((event_t)&fJob.done_count, THREAD_UNINT);
					if (__atomic_load_n(&fJob.done_count, __ATOMIC_ACQUIRE) >= (XZS_FB_WORKER_THREADS - 1u)) {
						clear_wait(current_thread(), THREAD_AWAKENED);
						break;
					}
					thread_block(THREAD_CONTINUE_NULL);
				}
			} else {
				renderSlice(0u, 1u);
			}
			any_drawn = true;
		}
		s->release();
	}
	if (any_drawn) {
		xzs_fb_present_kick();
		if (sweet_fb_swaps_logged <= 8) {
			uint32_t center_px = *(volatile uint32_t *)(fPixels + (uint64_t)(fHeight / 2) * fRowBytes + (fWidth / 2) * 4);
			IOLog("SWEET FB: swap %u kicked MDP5 (kicks=%u center_px=0x%08x)\n",
			    get32(rec, 0x50), xzs_fb_kick_count(), center_px);
		}
	}
}

bool
IOMobileFramebufferUserClient::initWithTask(task_t owningTask, void *securityID, UInt32 type)
{
	if (!IOUserClient::initWithTask(owningTask, securityID, type)) {
		return false;
	}
	fTask = owningTask;
	return true;
}

bool
IOMobileFramebufferUserClient::start(IOService *provider)
{
	fFB = OSDynamicCast(AppleCLCD, provider);
	if (fFB == NULL || !IOUserClient::start(provider)) {
		return false;
	}
	fFB->addClient(this);
	return true;
}

IOReturn
IOMobileFramebufferUserClient::clientClose(void)
{
	if (fFB != NULL) {
		fFB->removeClient(this);
		IOLockLock(fFB->fLock);
		mach_port_t old = fPort;
		fPort = MACH_PORT_NULL;
		IOLockUnlock(fFB->fLock);
		if (old != MACH_PORT_NULL) {
			releaseNotificationPort(old);
		}
	}
	terminate();
	return kIOReturnSuccess;
}

IOReturn
IOMobileFramebufferUserClient::registerNotificationPort(mach_port_t port, __unused UInt32 type,
    __unused io_user_reference_t refCon)
{
	mach_port_t old;
	IOLockLock(fFB->fLock);
	old = fPort;
	fPort = port;
	IOLockUnlock(fFB->fLock);
	if (old != MACH_PORT_NULL) {
		releaseNotificationPort(old);
	}
	return kIOReturnSuccess;
}

/* Called with the framebuffer lock held. */
void
IOMobileFramebufferUserClient::notify(uint32_t type, const uint64_t *values, uint32_t count)
{
	OSAsyncReference64 ref;

	if (type >= SWEET_FB_NOTIFY_TYPES || fFunc[type] == 0 || fPort == MACH_PORT_NULL) {
		return;
	}
	bzero(ref, sizeof(ref));
	setAsyncReference64(ref, fPort, (mach_vm_address_t)fFunc[type], fRefcon[type], fTask);
	sendAsyncResult64WithOptions(ref, kIOReturnSuccess, (io_user_reference_t *)values, count,
	    kIOUserNotifyOptionCanDrop);
}

static uint64_t sweet_fb_logged;        /* selectors already logged (< 64) */

IOReturn
IOMobileFramebufferUserClient::externalMethod(uint32_t selector, IOExternalMethodArguments *args,
    __unused IOExternalMethodDispatch *dispatch, __unused OSObject *target,
    __unused void *reference)
{
	IOReturn ret = kIOReturnSuccess;
	uint64_t out[2] = { 0, 0 };
	uint32_t outCount = 0;

	switch (selector) {
	case kSweetFBGetLayerDefaultSurface:
		out[0] = sweet_surface_port_in_task(fFB->fSurface, fTask);
		outCount = 1;
		break;
	case kSweetFBSwapBegin:
		IOLockLock(fFB->fLock);
		out[0] = ++fFB->fSwapID;
		IOLockUnlock(fFB->fLock);
		outCount = 1;
		break;
	case kSweetFBSwapEnd: {
		uint8_t rec[SWEET_FB_SWAP_REC_SIZE];
		if (args->structureInputDescriptor != NULL ||
		    args->structureInputSize < SWEET_FB_SWAP_REC_SIZE) {
			ret = kIOReturnBadArgument;
			break;
		}
		memcpy(rec, args->structureInput, sizeof(rec));
		fFB->swap(rec);
		break;
	}
	case kSweetFBGetID:
		out[0] = 0;
		outCount = 1;
		break;
	case kSweetFBGetDisplaySize:
		out[0] = fFB->fLogicalW;
		out[1] = fFB->fLogicalH;
		outCount = 2;
		break;
	case kSweetFBIsMainDisplay:
		out[0] = 1;
		outCount = 1;
		break;
	case kSweetFBGetDotPitch:
		out[0] = SWEET_FB_DOT_PITCH;
		outCount = 1;
		break;
	case kSweetFBNotifications:
		if (args->scalarInputCount >= 3 && args->scalarInput[2] < SWEET_FB_NOTIFY_TYPES) {
			uint32_t type = (uint32_t)args->scalarInput[2];
			IOLockLock(fFB->fLock);
			fFunc[type] = args->scalarInput[0];
			fRefcon[type] = args->scalarInput[1];
			IOLockUnlock(fFB->fLock);
			char name[32];
			proc_selfname(name, sizeof(name));
			IOLog("SWEET FB: %s notifications type %u %s\n", name, type,
			    args->scalarInput[0] ? "on" : "off");
		}
		break;
	case kSweetFBRequestPowerChange:
		if (args->scalarInputCount >= 1) {
			bool off = args->scalarInput[0] == 0;
			int allow_off = 0;
			PE_parse_boot_argn("sweetfb_allow_off", &allow_off, sizeof(allow_off));
			if (off && !allow_off && !g_xzs_power_key_used) {
				IOLog("SWEET FB: ignoring idle display off request (keeping LCD active)\n");
				break;
			}
			IOLockLock(fFB->fLock);
			if (off != fFB->fPowerOff) {
				fFB->fPowerOff = off;
				fFB->fFadeStep = 0;
				if (!off) {
					fFB->fDirty = true;
					fFB->fPresentReady = true;
					IOLockWakeup(fFB->fLock, &fFB->fPresentReady, true);
				}
			}
			IOLockUnlock(fFB->fLock);
			xzs_fb_set_backlight(!off);
			IOLog("SWEET FB: display %s\n", off ? "off" : "on");
		}
		break;
	case kSweetFBSwapWait:
	default:
		break;
	}

	for (uint32_t i = 0; i < args->scalarOutputCount; i++) {
		args->scalarOutput[i] = i < outCount ? out[i] : 0;
	}
	if (args->structureOutputDescriptor == NULL && args->structureOutput != NULL) {
		bzero(args->structureOutput, args->structureOutputSize);
	}
	if (selector >= 64 || !(sweet_fb_logged & (1ULL << selector))) {
		if (selector < 64) {
			sweet_fb_logged |= 1ULL << selector;
		}
		char name[32];
		proc_selfname(name, sizeof(name));
		IOLog("SWEET FB: %s sel %u in %u/%u out %u/%u -> 0x%x\n", name, selector,
		    args->scalarInputCount, args->structureInputSize,
		    args->scalarOutputCount, args->structureOutputSize, ret);
	}
	return ret;
}
#endif /* SWEET || XZS */
