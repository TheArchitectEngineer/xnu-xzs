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

#define XZS_FB_WORKER_THREADS           4

static uint32_t fColumn[1080];
static uint16_t fColumnWeight[1080];
static uint32_t fBlendRow[XZS_FB_WORKER_THREADS][SWEET_FB_LOGICAL_W];

struct XZSFBSliceJob {
	const uint8_t *pixels;
	uint32_t  stride;
	int32_t   sx, sy, sw, sh;
	int32_t   px0, py0, px1, py1;
	int32_t   cx0, cx1;
	bool      filter;
	bool      unscaled;
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

static inline uint32_t
xzs_fb_blend(uint32_t a, uint32_t b, uint32_t w)
{
	uint32_t rb = (((a & 0x00ff00ffu) * (256u - w) + (b & 0x00ff00ffu) * w) >> 8) & 0x00ff00ffu;
	uint32_t ga = (((a >> 8) & 0x00ff00ffu) * (256u - w) + ((b >> 8) & 0x00ff00ffu) * w) & 0xff00ff00u;
	return rb | ga;
}

static inline uint32_t
xzs_bgra_to_mdp5(uint32_t p)
{
	return (p & 0xff00ff00u) | ((p & 0x00ff0000u) >> 16) | ((p & 0x000000ffu) << 16);
}

static inline uint64_t
xzs_bgra_to_mdp5_swar2(uint64_t p2)
{
	return (p2 & 0xff00ff00ff00ff00ULL) |
	       ((p2 & 0x00ff000000ff0000ULL) >> 16) |
	       ((p2 & 0x000000ff000000ffULL) << 16);
}

/*
 * Where the center of the i-th of `out` pixels falls among `in` source pixels,
 * in 256ths of a pixel, clamped to [0, (in - 1) * 256].
 */
static inline int64_t
xzs_sample_at(int64_t i, int64_t in, int64_t out)
{
	int64_t at = ((2 * i + 1) * in - out) * 128 / out;
	return at < 0 ? 0 : (at > (in - 1) * 256 ? (in - 1) * 256 : at);
}

extern "C" uint32_t
xzs_fb_py_to_ly(uint32_t py)
{
	if (py >= 1920u) {
		py = 1919u;
	}
	uint32_t ly = (py * SWEET_FB_LOGICAL_H) / 1920u;
	if (ly >= SWEET_FB_LOGICAL_H) {
		ly = SWEET_FB_LOGICAL_H - 1u;
	}
	return ly;
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
	const uint8_t *pixels = job.pixels;
	uint32_t stride = job.stride;
	int32_t sx = job.sx, sy = job.sy, sw = job.sw, sh = job.sh;
	int32_t py0 = job.py0, py1 = job.py1, cx0 = job.cx0, cx1 = job.cx1;
	bool filter = job.filter;
	uint32_t *blendRow = fBlendRow[worker_id < XZS_FB_WORKER_THREADS ? worker_id : 0];

	int32_t total_h = py1 - py0;
	int32_t y0 = py0 + (total_h * (int32_t)worker_id) / (int32_t)total_workers;
	int32_t y1 = (worker_id + 1u == total_workers) ?
	    py1 : (py0 + (total_h * (int32_t)(worker_id + 1u)) / (int32_t)total_workers);
	if (y0 >= y1 || cx0 >= cx1) {
		return;
	}

	int32_t map_py0 = (sy == 0 && sh == (int32_t)fLogicalH) ? 0 : py0;
	int32_t map_ph  = (sy == 0 && sh == (int32_t)fLogicalH) ? (int32_t)fHeight : (py1 - py0);

	for (int32_t y = y0; y < y1; y++) {
		if (y < 0 || (uint32_t)y >= fHeight) {
			continue;
		}
		uint32_t *to = (uint32_t *)(fPixels + (size_t)y * fRowBytes);
		if (job.unscaled) {
			const uint32_t *src_row = (const uint32_t *)(pixels + (size_t)(sy + y - py0) * stride) + sx;
			int32_t x = cx0;
			if ((x & 1) != 0 && x < cx1) {
				to[x] = xzs_bgra_to_mdp5(src_row[x - cx0]);
				x++;
			}
			for (; x + 1 < cx1; x += 2) {
				uint64_t p2;
				memcpy(&p2, &src_row[x - cx0], sizeof(p2));
				*(uint64_t *)&to[x] = xzs_bgra_to_mdp5_swar2(p2);
			}
			if (x < cx1) {
				to[x] = xzs_bgra_to_mdp5(src_row[x - cx0]);
			}
			continue;
		}

		const uint32_t *from;
		if (filter) {
			int64_t at = xzs_sample_at(y - map_py0, sh, map_ph);
			uint32_t row = (uint32_t)(at >> 8);
			uint32_t weight = (uint32_t)(at & 255);
			const uint32_t *upper = (const uint32_t *)(pixels + (size_t)(sy + row) * stride) + sx;

			if (weight == 0 || row + 1u >= (uint32_t)sh) {
				from = upper;
			} else {
				const uint32_t *lower = (const uint32_t *)(pixels + (size_t)(sy + row + 1u) * stride) + sx;
				uint32_t first = fColumn[cx0];
				uint32_t last = fColumn[cx1 - 1];
				if (last + 1u < (uint32_t)sw) {
					last++;
				}
				for (uint32_t c = first; c <= last; c++) {
					uint32_t a = upper[c], b = lower[c];
					blendRow[c] = (a == b) ? a : xzs_fb_blend(a, b, weight);
				}
				from = blendRow;
			}
		} else {
			uint32_t row = (uint32_t)((int64_t)(y - map_py0) * sh / map_ph);
			if (row >= (uint32_t)sh) {
				row = (uint32_t)sh - 1u;
			}
			from = (const uint32_t *)(pixels + (size_t)(sy + row) * stride) + sx;
		}

		if (filter) {
			int32_t x = cx0;
			if ((x & 1) != 0 && x < cx1) {
				uint32_t c = fColumn[x];
				uint32_t w = fColumnWeight[x];
				uint32_t a = from[c];
				uint32_t p = (w == 0 || a == from[c + 1u]) ? a : xzs_fb_blend(a, from[c + 1u], w);
				to[x] = xzs_bgra_to_mdp5(p);
				x++;
			}
			for (; x + 1 < cx1; x += 2) {
				uint32_t c0 = fColumn[x], w0 = fColumnWeight[x], a0 = from[c0];
				uint32_t p0 = (w0 == 0 || a0 == from[c0 + 1u]) ? a0 : xzs_fb_blend(a0, from[c0 + 1u], w0);
				uint32_t c1 = fColumn[x + 1], w1 = fColumnWeight[x + 1], a1 = from[c1];
				uint32_t p1 = (w1 == 0 || a1 == from[c1 + 1u]) ? a1 : xzs_fb_blend(a1, from[c1 + 1u], w1);
				uint64_t pair = (uint64_t)p0 | ((uint64_t)p1 << 32);
				*(uint64_t *)&to[x] = xzs_bgra_to_mdp5_swar2(pair);
			}
			if (x < cx1) {
				uint32_t c = fColumn[x];
				uint32_t w = fColumnWeight[x];
				uint32_t a = from[c];
				uint32_t p = (w == 0 || a == from[c + 1u]) ? a : xzs_fb_blend(a, from[c + 1u], w);
				to[x] = xzs_bgra_to_mdp5(p);
			}
		} else {
			int32_t x = cx0;
			if ((x & 1) != 0 && x < cx1) {
				to[x] = xzs_bgra_to_mdp5(from[fColumn[x]]);
				x++;
			}
			for (; x + 1 < cx1; x += 2) {
				uint64_t pair = (uint64_t)from[fColumn[x]] | ((uint64_t)from[fColumn[x + 1]] << 32);
				*(uint64_t *)&to[x] = xzs_bgra_to_mdp5_swar2(pair);
			}
			if (x < cx1) {
				to[x] = xzs_bgra_to_mdp5(from[fColumn[x]]);
			}
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

/* Queue the swap for presentation on the next vsync tick. */
void
AppleCLCD::swap(const uint8_t *rec)
{
	IOLockLock(fLock);
	memcpy(fPending, rec, SWEET_FB_SWAP_REC_SIZE);
	fDirty = true;
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
			dx = dy = 0;
			w = (int32_t)fLogicalW;
			h = (int32_t)fLogicalH;
		}
		if (sw <= 0 || sh <= 0) {
			sx = sy = 0;
			sw = (int32_t)s->width;
			sh = (int32_t)s->height;
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

		bool full_surface_coords = (sx == dx && sy == dy && sw == w && sh == h &&
		    s->width == fLogicalW && s->height == fLogicalH);

		/* Scale logical (fLogicalW x fLogicalH) rect onto physical panel (fWidth x fHeight) */
		int32_t px0 = (int32_t)(((int64_t)dx * fWidth) / fLogicalW);
		int32_t py0 = (int32_t)(((int64_t)dy * fHeight) / fLogicalH);
		int32_t px1 = full_surface_coords ?
		    (int32_t)(((int64_t)(dx + w) * fWidth + fLogicalW - 1) / fLogicalW) :
		    (int32_t)(((int64_t)(dx + w) * fWidth) / fLogicalW);
		int32_t py1 = full_surface_coords ?
		    (int32_t)(((int64_t)(dy + h) * fHeight + fLogicalH - 1) / fLogicalH) :
		    (int32_t)(((int64_t)(dy + h) * fHeight) / fLogicalH);
		if (px0 < 0) px0 = 0;
		if (py0 < 0) py0 = 0;
		if ((uint32_t)px1 > fWidth) px1 = (int32_t)fWidth;
		if ((uint32_t)py1 > fHeight) py1 = (int32_t)fHeight;
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
			if (!fConsolePaused) {
				uint32_t non_black_samples = 0;
				uint32_t max_rgb = 0;
				uint32_t sample_rgb = 0;
				uint32_t step_check_y = (sh > 64) ? (uint32_t)(sh / 64) : 1u;
				for (int32_t cy = sy; cy < sy + sh; cy += (int32_t)step_check_y) {
					const uint32_t *crow = (const uint32_t *)(src + s->offset + (uint64_t)cy * s->bytesPerRow);
					for (int32_t cx = sx; cx < sx + sw; cx += 8) {
						uint32_t rgb = crow[cx] & 0x00ffffffu;
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
						}
					}
				}
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
					s->release();
					continue;
				}
			} else if (g_xzs_springboard_surface_ready && !g_xzs_gui_presented) {
				g_xzs_gui_presented = true;
			}

			int32_t job_sx = full_surface_coords ? 0 : sx;
			int32_t job_sy = full_surface_coords ? 0 : sy;
			int32_t job_sw = full_surface_coords ? (int32_t)fLogicalW : sw;
			int32_t job_sh = full_surface_coords ? (int32_t)fLogicalH : sh;
			int32_t map_px0 = full_surface_coords ? 0 : px0;
			int32_t map_pw  = full_surface_coords ? (int32_t)fWidth : pw;
			bool filter = (job_sw <= (int32_t)SWEET_FB_LOGICAL_W);

			for (int32_t x = px0; x < px1; x++) {
				if (filter) {
					int64_t at = xzs_sample_at(x - map_px0, job_sw, map_pw);
					uint32_t col = (uint32_t)(at >> 8);
					fColumn[x] = col;
					fColumnWeight[x] = (uint16_t)(col + 1u < (uint32_t)job_sw ? (at & 255) : 0);
				} else {
					fColumn[x] = (uint32_t)((int64_t)(x - map_px0) * job_sw / map_pw);
				}
			}

			fJob.pixels   = (const uint8_t *)(src + s->offset);
			fJob.stride   = s->bytesPerRow;
			fJob.sx       = job_sx;
			fJob.sy       = job_sy;
			fJob.sw       = job_sw;
			fJob.sh       = job_sh;
			fJob.px0      = px0;
			fJob.py0      = py0;
			fJob.px1      = px1;
			fJob.py1      = py1;
			fJob.cx0      = px0;
			fJob.cx1      = px1;
			fJob.filter   = filter;
			fJob.unscaled = (pw == sw && ph == sh && !full_surface_coords);

			if (ph >= 120 && XZS_FB_WORKER_THREADS > 1) {
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
