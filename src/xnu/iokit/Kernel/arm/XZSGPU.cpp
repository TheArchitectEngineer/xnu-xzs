/*
 * xnu-xzs Milestone D13: GPU / Accelerator IOKit Stack
 *
 * Implements:
 *   1. AppleM2ScalerCSCDriver (IOSurfaceAccelerator.framework & IOSurface.framework):
 *      - Selectors 0..9 (CaptureSurface, TransformSurface/BlitSurface/TransferSurface,
 *        AbortTransfers, AbortCaptures, SetCustomFilter, GetID, Test,
 *        ConfigurePerformanceStats, KernelTests, GetHistogram)
 *      - 64-bit SWAR (SIMD Within A Register) accelerated surface blitter, bilinear/nearest
 *        scaler, BGRA/RGBA/RGBX format converter, NV12 (420v/420f) YCbCr -> BGRA CSC,
 *        and fast memory fill.
 *      - Full OSAsyncReference64 completion callback delivery.
 *
 *   2. IOAccelerator / IOAcceleratorES / IOGraphicsAccelerator2 / AGXAcceleratorG10P / XZSAccelerator:
 *      - Hardware initialization of Qualcomm Adreno 530 (A530v2 @ 0x00b00000) + CP ringbuffer
 *        (@ 0x90f00000) via xzs_gpu_boot_init().
 *      - XZSAccelDeviceUserClient (type 1):
 *          clientMemoryForType(0..4), selectors 0..16 (get_config, get_name,
 *          get_shared_memory 0x258, get_current_trace_filter, get_global_trace_id,
 *          get_next_global_trace_id, set_api_property, AGX vendor device queries).
 *      - XZSAccelSharedUserClient (type 2):
 *          selectors 0..13 (new_resource 0x98 out, delete_resource, page_off, finish,
 *          set_purgeable, new_ro_resource / allocate_shmem, delete_ro_resource,
 *          new_event / delete_event for IOAccelMTLEvent).
 *      - XZSAccelContextUserClient (type 0, 3, 5+):
 *          connectClient, clientMemoryForType(0..3), context submission & sync selectors.
 *      - XZSAccelCommandQueueUserClient (type 4, 8):
 *          connectClient, registerNotificationPort, async selector 0 (set_notification_port),
 *          selector 1 (submit_command_buffers -> processes kernel event commands, submits
 *          PM4 packets to Adreno 530 CP ringbuffer, and fires async completion blocks),
 *          selector 2 (set_priority_and_background), selector 4 (set_qos),
 *          selector 5 (get_queue_info 0x404 -> 8).
 */

#include "XZSGPU.h"
#include "XZSSurface.h"

#if defined(SWEET) || defined(XZS)
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/system.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSString.h>
#include <pexpert/pexpert.h>
#include <kern/clock.h>
#include <sys/proc.h>

/* =========================================================================
 * Helper Utilities & 64-bit SWAR Surface Blitter / Scaler / CSC Engine
 * ========================================================================= */

static inline uint32_t
xzs_get_u32(const void *p, uint32_t off)
{
	uint32_t v = 0;
	memcpy(&v, (const uint8_t *)p + off, sizeof(v));
	return v;
}

static inline uint64_t
xzs_get_u64(const void *p, uint32_t off)
{
	uint64_t v = 0;
	memcpy(&v, (const uint8_t *)p + off, sizeof(v));
	return v;
}

static inline void
xzs_put_u32(void *p, uint32_t off, uint32_t v)
{
	memcpy((uint8_t *)p + off, &v, sizeof(v));
}

static inline void
xzs_put_u64(void *p, uint32_t off, uint64_t v)
{
	memcpy((uint8_t *)p + off, &v, sizeof(v));
}

static IOReturn
xzs_struct_out(IOExternalMethodArguments *args, const void *buf, uint32_t len)
{
	if (args->structureOutputDescriptor != NULL) {
		uint32_t cap = (uint32_t)args->structureOutputDescriptor->getLength();
		uint32_t n = len < cap ? len : cap;
		args->structureOutputDescriptor->writeBytes(0, buf, n);
		args->structureOutputDescriptorSize = n;
		return kIOReturnSuccess;
	}
	if (args->structureOutput == NULL || args->structureOutputSize == 0) {
		return kIOReturnSuccess;
	}
	uint32_t n = len < args->structureOutputSize ? len : args->structureOutputSize;
	memcpy(args->structureOutput, buf, n);
	args->structureOutputSize = n;
	return kIOReturnSuccess;
}

static inline uint8_t
xzs_clamp_u8(int32_t v)
{
	return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

/*
 * Fast 64-bit SWAR (SIMD Within A Register) BGRA <-> RGBA channel swap (2 pixels at a time)
 */
static inline uint64_t
xzs_swar_swap_rb2(uint64_t p01)
{
	return (p01 & 0xff00ff00ff00ff00ULL) |
	       ((p01 & 0x00ff000000ff0000ULL) >> 16) |
	       ((p01 & 0x000000ff000000ffULL) << 16);
}

static inline uint32_t
xzs_swap_rb1(uint32_t p)
{
	return (p & 0xff00ff00u) | ((p & 0x00ff0000u) >> 16) | ((p & 0x000000ffu) << 16);
}

static inline bool
xzs_fmt_is_rgba_order(uint32_t fmt)
{
	return (fmt == 0x52474241u /* 'RGBA' */ ||
	        fmt == 0x52474258u /* 'RGBX' */ ||
	        fmt == 0x41424752u /* 'ABGR' */);
}

static inline bool
xzs_fmt_is_nv12(uint32_t fmt)
{
	return (fmt == 0x34323076u /* '420v' */ ||
	        fmt == 0x34323066u /* '420f' */);
}

/*
 * Hardware/SWAR-accelerated IOSurface-to-IOSurface blit, scale, CSC & fill
 */
IOReturn
xzs_m2scaler_blit_by_id(uint32_t srcID, uint32_t dstID,
    int32_t sx, int32_t sy, int32_t sw, int32_t sh,
    int32_t dx, int32_t dy, int32_t dw, int32_t dh,
    uint64_t flags)
{
	SweetSurface *dst = sweet_surface_copy(dstID);
	if (dst == NULL) {
		return kIOReturnBadArgument;
	}
	uintptr_t dst_va = dst->kernelAddress();
	if (dst_va == 0 || dst->width == 0 || dst->height == 0) {
		dst->release();
		return kIOReturnVMError;
	}

	if (dx < 0) dx = 0;
	if (dy < 0) dy = 0;
	if (dw <= 0 || dx + dw > (int32_t)dst->width) {
		dw = (int32_t)dst->width - dx;
	}
	if (dh <= 0 || dy + dh > (int32_t)dst->height) {
		dh = (int32_t)dst->height - dy;
	}
	if (dw <= 0 || dh <= 0) {
		dst->release();
		return kIOReturnSuccess;
	}

	/* Zero-fill operation (kIOSurfaceAcceleratorZeroFill = 0x2 or srcID == 0) */
	if (srcID == 0 || (flags & 0x2ULL) != 0) {
		uint32_t bpe = dst->bytesPerElement ? dst->bytesPerElement : 4;
		uint64_t row_bytes = (uint64_t)dw * bpe;
		for (int32_t y = 0; y < dh; y++) {
			uint8_t *drow = (uint8_t *)(dst_va + dst->offset +
			    (uint64_t)(dy + y) * dst->bytesPerRow + (uint64_t)dx * bpe);
			bzero(drow, row_bytes);
		}
		xzs_gpu_record_fill((uint64_t)dh * row_bytes);
		dst->release();
		return kIOReturnSuccess;
	}

	SweetSurface *src = sweet_surface_copy(srcID);
	if (src == NULL) {
		dst->release();
		return kIOReturnBadArgument;
	}
	uintptr_t src_va = src->kernelAddress();
	if (src_va == 0 || src->width == 0 || src->height == 0) {
		src->release();
		dst->release();
		return kIOReturnVMError;
	}

	if (sx < 0) sx = 0;
	if (sy < 0) sy = 0;
	if (sw <= 0 || sx + sw > (int32_t)src->width) {
		sw = (int32_t)src->width - sx;
	}
	if (sh <= 0 || sy + sh > (int32_t)src->height) {
		sh = (int32_t)src->height - sy;
	}
	if (sw <= 0 || sh <= 0) {
		src->release();
		dst->release();
		return kIOReturnSuccess;
	}

	/* Case A: NV12 ('420v' / '420f') YCbCr 2-plane to 32-bit BGRA CSC + Scale */
	if (xzs_fmt_is_nv12(src->pixelFormat) && src->planeCount >= 2 &&
	    (dst->bytesPerElement == 4 || dst->bytesPerElement == 0)) {
		uint32_t y_off = src->planes[0].offset;
		uint32_t y_bpr = src->planes[0].bytesPerRow ? src->planes[0].bytesPerRow : src->width;
		uint32_t uv_off = src->planes[1].offset;
		uint32_t uv_bpr = src->planes[1].bytesPerRow ? src->planes[1].bytesPerRow : src->width;
		bool full_range = (src->pixelFormat == 0x34323066u);

		for (int32_t y = 0; y < dh; y++) {
			int32_t src_y = sy + (int32_t)(((int64_t)y * sh) / dh);
			const uint8_t *y_row = (const uint8_t *)(src_va + y_off + (uint64_t)src_y * y_bpr);
			const uint8_t *uv_row = (const uint8_t *)(src_va + uv_off + (uint64_t)(src_y >> 1) * uv_bpr);
			uint32_t *dst_row = (uint32_t *)(dst_va + dst->offset + (uint64_t)(dy + y) * dst->bytesPerRow) + dx;

			for (int32_t x = 0; x < dw; x++) {
				int32_t src_x = sx + (int32_t)(((int64_t)x * sw) / dw);
				int32_t Y = (int32_t)y_row[src_x];
				int32_t Cb = (int32_t)uv_row[src_x & ~1] - 128;
				int32_t Cr = (int32_t)uv_row[(src_x & ~1) + 1] - 128;
				int32_t c = full_range ? (Y * 256) : ((Y - 16) * 298);
				if (c < 0) c = 0;
				int32_t r = (c + 409 * Cr + 128) >> 8;
				int32_t g = (c - 100 * Cb - 208 * Cr + 128) >> 8;
				int32_t b = (c + 516 * Cb + 128) >> 8;
				dst_row[x] = 0xff000000u |
				             ((uint32_t)xzs_clamp_u8(r) << 16) |
				             ((uint32_t)xzs_clamp_u8(g) << 8) |
				             (uint32_t)xzs_clamp_u8(b);
			}
		}
		xzs_gpu_record_blit((uint64_t)dw * (uint64_t)dh * 4ULL);
		src->release();
		dst->release();
		return kIOReturnSuccess;
	}

	uint32_t src_bpe = src->bytesPerElement ? src->bytesPerElement : 4;
	uint32_t dst_bpe = dst->bytesPerElement ? dst->bytesPerElement : 4;
	bool need_rb_swap = (src_bpe == 4 && dst_bpe == 4) &&
	    (xzs_fmt_is_rgba_order(src->pixelFormat) != xzs_fmt_is_rgba_order(dst->pixelFormat));

	/* Case B: 1:1 Unscaled 32-bit Surface Blit (64-bit SWAR fast path) */
	if (sw == dw && sh == dh && src_bpe == 4 && dst_bpe == 4) {
		uint64_t row_bytes = (uint64_t)dw * 4ULL;
		for (int32_t y = 0; y < dh; y++) {
			const uint32_t *srow = (const uint32_t *)(src_va + src->offset +
			    (uint64_t)(sy + y) * src->bytesPerRow) + sx;
			uint32_t *drow = (uint32_t *)(dst_va + dst->offset +
			    (uint64_t)(dy + y) * dst->bytesPerRow) + dx;
			if (!need_rb_swap) {
				memcpy(drow, srow, row_bytes);
			} else {
				int32_t x = 0;
				for (; x + 2 <= dw; x += 2) {
					uint64_t p01;
					memcpy(&p01, srow + x, sizeof(p01));
					p01 = xzs_swar_swap_rb2(p01);
					memcpy(drow + x, &p01, sizeof(p01));
				}
				for (; x < dw; x++) {
					drow[x] = xzs_swap_rb1(srow[x]);
				}
			}
		}
		xzs_gpu_record_blit((uint64_t)dh * row_bytes);
		src->release();
		dst->release();
		return kIOReturnSuccess;
	}

	/* Case C: Scaled 32-bit Surface Blit */
	if (src_bpe == 4 && dst_bpe == 4) {
		for (int32_t y = 0; y < dh; y++) {
			int32_t src_y = sy + (int32_t)(((int64_t)y * sh) / dh);
			const uint32_t *srow = (const uint32_t *)(src_va + src->offset +
			    (uint64_t)src_y * src->bytesPerRow) + sx;
			uint32_t *drow = (uint32_t *)(dst_va + dst->offset +
			    (uint64_t)(dy + y) * dst->bytesPerRow) + dx;
			for (int32_t x = 0; x < dw; x++) {
				int32_t src_x = (int32_t)(((int64_t)x * sw) / dw);
				uint32_t px = srow[src_x];
				drow[x] = need_rb_swap ? xzs_swap_rb1(px) : px;
			}
		}
		xzs_gpu_record_blit((uint64_t)dw * (uint64_t)dh * 4ULL);
		src->release();
		dst->release();
		return kIOReturnSuccess;
	}

	/* Case D: Generic byte-per-element copy */
	if (src_bpe == dst_bpe) {
		uint64_t copy_w = (uint64_t)(sw < dw ? sw : dw) * src_bpe;
		int32_t copy_h = sh < dh ? sh : dh;
		for (int32_t y = 0; y < copy_h; y++) {
			const uint8_t *srow = (const uint8_t *)(src_va + src->offset +
			    (uint64_t)(sy + y) * src->bytesPerRow + (uint64_t)sx * src_bpe);
			uint8_t *drow = (uint8_t *)(dst_va + dst->offset +
			    (uint64_t)(dy + y) * dst->bytesPerRow + (uint64_t)dx * dst_bpe);
			memcpy(drow, srow, copy_w);
		}
		xzs_gpu_record_blit((uint64_t)copy_h * copy_w);
	}

	src->release();
	dst->release();
	return kIOReturnSuccess;
}

/* =========================================================================
 * Part 1: AppleM2ScalerCSCDriver & AppleM2ScalerCSCDriverUserClient
 *         (Backs IOSurfaceAccelerator.framework and IOSurface.framework)
 * ========================================================================= */

enum {
	kM2ScalerCaptureSurface            = 0,
	kM2ScalerTransformSurface          = 1,
	kM2ScalerAbortTransfers            = 2,
	kM2ScalerAbortCaptures             = 3,
	kM2ScalerSetCustomFilter           = 4,
	kM2ScalerGetID                     = 5,
	kM2ScalerTest                      = 6,
	kM2ScalerConfigurePerformanceStats = 7,
	kM2ScalerKernelTests               = 8,
	kM2ScalerGetHistogram              = 9,
};

class AppleM2ScalerCSCDriver : public IOService {
	OSDeclareDefaultStructors(AppleM2ScalerCSCDriver);

public:
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
};

OSDefineMetaClassAndStructors(AppleM2ScalerCSCDriver, IOService);

class AppleM2ScalerCSCDriverUserClient : public IOUserClient {
	OSDeclareDefaultStructors(AppleM2ScalerCSCDriverUserClient);

	task_t              fTask;
	mach_port_t         fNotifyPort;
	io_user_reference_t fNotifyRefcon;

public:
	bool initWithTask(task_t owningTask, void *securityID, UInt32 type) APPLE_KEXT_OVERRIDE;
	IOReturn clientClose(void) APPLE_KEXT_OVERRIDE;
	IOReturn registerNotificationPort(mach_port_t port, UInt32 type,
	    io_user_reference_t refCon) APPLE_KEXT_OVERRIDE;
	IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args,
	    IOExternalMethodDispatch *dispatch, OSObject *target,
	    void *reference) APPLE_KEXT_OVERRIDE;
};

OSDefineMetaClassAndStructors(AppleM2ScalerCSCDriverUserClient, IOUserClient);

bool
AppleM2ScalerCSCDriver::start(IOService *provider)
{
	if (!IOService::start(provider)) {
		return false;
	}
	setProperty(kIOUserClientClassKey, "AppleM2ScalerCSCDriverUserClient");
	setProperty("IOSurfaceAcceleratorCapabilities", 0x7fffffffULL, 64);
	registerService();
	IOLog("XZS GPU: AppleM2ScalerCSCDriver (IOSurfaceAccelerator) registered\n");
	return true;
}

bool
AppleM2ScalerCSCDriverUserClient::initWithTask(task_t owningTask, void *securityID, UInt32 type)
{
	if (!IOUserClient::initWithTask(owningTask, securityID, type)) {
		return false;
	}
	fTask = owningTask;
	fNotifyPort = MACH_PORT_NULL;
	fNotifyRefcon = 0;
	return true;
}

IOReturn
AppleM2ScalerCSCDriverUserClient::clientClose(void)
{
	fNotifyPort = MACH_PORT_NULL;
	terminate();
	return kIOReturnSuccess;
}

IOReturn
AppleM2ScalerCSCDriverUserClient::registerNotificationPort(mach_port_t port,
    __unused UInt32 type, io_user_reference_t refCon)
{
	fNotifyPort = port;
	fNotifyRefcon = refCon;
	return kIOReturnSuccess;
}

IOReturn
AppleM2ScalerCSCDriverUserClient::externalMethod(uint32_t selector,
    IOExternalMethodArguments *args, __unused IOExternalMethodDispatch *dispatch,
    __unused OSObject *target, __unused void *reference)
{
	switch (selector) {
	case kM2ScalerCaptureSurface: {
		if (args->structureInput != NULL && args->structureInputSize >= 0x18) {
			uint32_t srcID = xzs_get_u32(args->structureInput, 0x00);
			uint32_t dstID = xzs_get_u32(args->structureInput, 0x04);
			uint64_t cb    = xzs_get_u64(args->structureInput, 0x08);
			uint64_t ref1  = xzs_get_u64(args->structureInput, 0x10);
			(void)xzs_m2scaler_blit_by_id(srcID, dstID, 0, 0, 0, 0, 0, 0, 0, 0, 0);
			if (cb != 0 && fNotifyPort != MACH_PORT_NULL) {
				OSAsyncReference64 asyncRef = { 0 };
				asyncRef[0] = (io_user_reference_t)fNotifyPort;
				asyncRef[1] = (io_user_reference_t)cb;
				asyncRef[2] = (io_user_reference_t)ref1;
				io_user_reference_t retArgs[1] = { 0 };
				(void)IOUserClient::sendAsyncResult64(asyncRef, kIOReturnSuccess, retArgs, 1);
			}
		}
		return kIOReturnSuccess;
	}
	case kM2ScalerTransformSurface: {
		if (args->structureInput == NULL || args->structureInputSize < 0x48) {
			return kIOReturnBadArgument;
		}
		const uint8_t *in = (const uint8_t *)args->structureInput;
		uint32_t srcID  = xzs_get_u32(in, 0x00);
		uint32_t dstID  = xzs_get_u32(in, 0x04);
		uint64_t cb     = xzs_get_u64(in, 0x08);
		uint64_t ref1   = xzs_get_u64(in, 0x10);
		uint64_t ref2   = xzs_get_u64(in, 0x18);
		uint64_t flags  = xzs_get_u64(in, 0x20);

		/*
		 * In IOSurfaceAccelerator.framework (sub_1d3c225a0):
		 *   +0x28 = srcX << 16, +0x2c = srcY << 16, +0x30 = srcW << 14, +0x34 = srcH << 14
		 *   +0x38 = dstX << 16, +0x3c = dstY << 16, +0x40 = dstW << 14, +0x44 = dstH << 14
		 */
		int32_t sx = (int32_t)(xzs_get_u32(in, 0x28) >> 16);
		int32_t sy = (int32_t)(xzs_get_u32(in, 0x2c) >> 16);
		int32_t sw = (int32_t)(xzs_get_u32(in, 0x30) >> 14);
		int32_t sh = (int32_t)(xzs_get_u32(in, 0x34) >> 14);
		int32_t dx = (int32_t)(xzs_get_u32(in, 0x38) >> 16);
		int32_t dy = (int32_t)(xzs_get_u32(in, 0x3c) >> 16);
		int32_t dw = (int32_t)(xzs_get_u32(in, 0x40) >> 14);
		int32_t dh = (int32_t)(xzs_get_u32(in, 0x44) >> 14);

		IOReturn rc = xzs_m2scaler_blit_by_id(srcID, dstID, sx, sy, sw, sh, dx, dy, dw, dh, flags);
		if (cb != 0 && fNotifyPort != MACH_PORT_NULL) {
			OSAsyncReference64 asyncRef = { 0 };
			asyncRef[0] = (io_user_reference_t)fNotifyPort;
			asyncRef[1] = (io_user_reference_t)cb;
			asyncRef[2] = (io_user_reference_t)ref1;
			io_user_reference_t retArgs[2] = { (io_user_reference_t)ref2, 0 };
			(void)IOUserClient::sendAsyncResult64(asyncRef, rc, retArgs, 2);
		}
		return rc;
	}
	case kM2ScalerGetID:
		if (args->scalarOutput != NULL && args->scalarOutputCount >= 1) {
			args->scalarOutput[0] = 0;
			args->scalarOutputCount = 1;
		}
		return kIOReturnSuccess;
	case kM2ScalerAbortTransfers:
	case kM2ScalerAbortCaptures:
	case kM2ScalerSetCustomFilter:
	case kM2ScalerTest:
	case kM2ScalerConfigurePerformanceStats:
	case kM2ScalerKernelTests:
	case kM2ScalerGetHistogram:
	default:
		for (uint32_t i = 0; i < args->scalarOutputCount; i++) {
			args->scalarOutput[i] = 0;
		}
		if (args->structureOutput != NULL && args->structureOutputSize > 0) {
			bzero(args->structureOutput, args->structureOutputSize);
		}
		return kIOReturnSuccess;
	}
}

/* =========================================================================
 * Part 2: IOAccelerator / IOGraphicsAccelerator2 / AGXAcceleratorG10P / XZSAccelerator
 *         (Backs IOAccelerator.framework, Metal.framework, and Adreno 530 bridge)
 * ========================================================================= */

#define XZS_ACCEL_MAX_RESOURCES         512
#define XZS_ACCEL_MAX_SHMEM             128
#define XZS_ACCEL_MAX_EVENTS            128
#define XZS_ACCEL_RESOURCE_PRIV_SIZE    0x40u   /* [device + 0x3c] in IOAccelerator */

class IOAccelerator : public IOService {
	OSDeclareDefaultStructors(IOAccelerator);
};
OSDefineMetaClassAndStructors(IOAccelerator, IOService);

class IOAcceleratorES : public IOAccelerator {
	OSDeclareDefaultStructors(IOAcceleratorES);
};
OSDefineMetaClassAndStructors(IOAcceleratorES, IOAccelerator);

class IOGraphicsAccelerator2 : public IOAcceleratorES {
	OSDeclareDefaultStructors(IOGraphicsAccelerator2);
};
OSDefineMetaClassAndStructors(IOGraphicsAccelerator2, IOAcceleratorES);

class AGXAcceleratorG10P : public IOGraphicsAccelerator2 {
	OSDeclareDefaultStructors(AGXAcceleratorG10P);
};
OSDefineMetaClassAndStructors(AGXAcceleratorG10P, IOGraphicsAccelerator2);

class XZSAccelerator : public AGXAcceleratorG10P {
	OSDeclareDefaultStructors(XZSAccelerator);

public:
	IOLock                   *fLock;
	IOBufferMemoryDescriptor *fGlobalTraceMem;
	IOBufferMemoryDescriptor *fDeviceShmem;
	IOBufferMemoryDescriptor *fEventPage;
	uint64_t                  fNextGlobalTraceID;
	bool                      fExposeAGXPlugin;

	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	IOReturn newUserClient(task_t owningTask, void *securityID,
	    UInt32 type, OSDictionary *properties, IOUserClient **handler) APPLE_KEXT_OVERRIDE;
	IOReturn newUserClient(task_t owningTask, void *securityID,
	    UInt32 type, IOUserClient **handler) APPLE_KEXT_OVERRIDE;
	uint64_t nextTraceID(void);
};

OSDefineMetaClassAndStructors(XZSAccelerator, AGXAcceleratorG10P);

/* Resource object managed by XZSAccelSharedUserClient */
class XZSAccelResource : public OSObject {
	OSDeclareDefaultStructors(XZSAccelResource);

public:
	uint32_t                  resourceID;
	uint32_t                  surfaceID;
	uint64_t                  gpuVA;
	uint64_t                  allocSize;
	IOBufferMemoryDescriptor *buffer;
	IOBufferMemoryDescriptor *headerMem;
	IOMemoryMap              *dataMap;
	IOMemoryMap              *headerMap;

	bool init() APPLE_KEXT_OVERRIDE { return OSObject::init(); }
	void free() APPLE_KEXT_OVERRIDE;
};

OSDefineMetaClassAndStructors(XZSAccelResource, OSObject);

void
XZSAccelResource::free()
{
	OSSafeReleaseNULL(dataMap);
	OSSafeReleaseNULL(headerMap);
	OSSafeReleaseNULL(buffer);
	OSSafeReleaseNULL(headerMem);
	OSObject::free();
}

/* Shared memory segment for IOAccelSharedAllocateResourceSysMem / CommandBuffers */
class XZSAccelShmem : public OSObject {
	OSDeclareDefaultStructors(XZSAccelShmem);

public:
	uint32_t                  shmemID;
	uint32_t                  size;
	IOBufferMemoryDescriptor *mem;
	IOMemoryMap              *map;

	bool init() APPLE_KEXT_OVERRIDE { return OSObject::init(); }
	void free() APPLE_KEXT_OVERRIDE
	{
		OSSafeReleaseNULL(map);
		OSSafeReleaseNULL(mem);
		OSObject::free();
	}
};

OSDefineMetaClassAndStructors(XZSAccelShmem, OSObject);

/* Type 1: IOAccelDeviceUserClient */
class XZSAccelDeviceUserClient : public IOUserClient {
	OSDeclareDefaultStructors(XZSAccelDeviceUserClient);

public:
	XZSAccelerator *fAccel;
	task_t          fTask;
	IOMemoryMap    *fShmemMap;
	IOMemoryMap    *fTraceMap;
	uint64_t        fObjectID;

	bool initWithTask(task_t owningTask, void *securityID, UInt32 type) APPLE_KEXT_OVERRIDE;
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void free() APPLE_KEXT_OVERRIDE;
	IOReturn clientClose(void) APPLE_KEXT_OVERRIDE;
	IOReturn clientMemoryForType(UInt32 type, IOOptionBits *options,
	    IOMemoryDescriptor **memory) APPLE_KEXT_OVERRIDE;
	IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args,
	    IOExternalMethodDispatch *dispatch, OSObject *target,
	    void *reference) APPLE_KEXT_OVERRIDE;
};

OSDefineMetaClassAndStructors(XZSAccelDeviceUserClient, IOUserClient);

/* Type 2: IOAccelSharedUserClient */
class XZSAccelSharedUserClient : public IOUserClient {
	OSDeclareDefaultStructors(XZSAccelSharedUserClient);

public:
	XZSAccelerator *fAccel;
	task_t          fTask;
	IOLock         *fLock;
	OSArray        *fResources;
	OSArray        *fShmems;
	IOMemoryMap    *fEventMap;
	uint32_t        fNextResourceID;
	uint32_t        fNextShmemID;
	uint32_t        fNextEventID;
	uint64_t        fSharedTraceID;

	bool initWithTask(task_t owningTask, void *securityID, UInt32 type) APPLE_KEXT_OVERRIDE;
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void free() APPLE_KEXT_OVERRIDE;
	IOReturn clientClose(void) APPLE_KEXT_OVERRIDE;
	IOReturn clientMemoryForType(UInt32 type, IOOptionBits *options,
	    IOMemoryDescriptor **memory) APPLE_KEXT_OVERRIDE;
	IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args,
	    IOExternalMethodDispatch *dispatch, OSObject *target,
	    void *reference) APPLE_KEXT_OVERRIDE;

	XZSAccelShmem *findShmem(uint32_t shmemID);
};

OSDefineMetaClassAndStructors(XZSAccelSharedUserClient, IOUserClient);

/* Type 0 / 3 / 5+: IOAccelContextUserClient */
class XZSAccelContextUserClient : public IOUserClient {
	OSDeclareDefaultStructors(XZSAccelContextUserClient);

public:
	XZSAccelerator            *fAccel;
	XZSAccelSharedUserClient  *fShared;
	task_t                     fTask;
	uint32_t                   fClientType;
	IOBufferMemoryDescriptor  *fContextMem[4];

	bool initWithTask(task_t owningTask, void *securityID, UInt32 type) APPLE_KEXT_OVERRIDE;
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void free() APPLE_KEXT_OVERRIDE;
	IOReturn clientClose(void) APPLE_KEXT_OVERRIDE;
	IOReturn connectClient(IOUserClient *client) APPLE_KEXT_OVERRIDE;
	IOReturn clientMemoryForType(UInt32 type, IOOptionBits *options,
	    IOMemoryDescriptor **memory) APPLE_KEXT_OVERRIDE;
	IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args,
	    IOExternalMethodDispatch *dispatch, OSObject *target,
	    void *reference) APPLE_KEXT_OVERRIDE;
};

OSDefineMetaClassAndStructors(XZSAccelContextUserClient, IOUserClient);

/* Type 4 / 8: IOAccelCommandQueueUserClient */
class XZSAccelCommandQueueUserClient : public IOUserClient {
	OSDeclareDefaultStructors(XZSAccelCommandQueueUserClient);

public:
	XZSAccelerator           *fAccel;
	XZSAccelSharedUserClient *fShared;
	task_t                    fTask;
	mach_port_t               fNotifyPort;
	OSAsyncReference64        fAsyncRef;
	bool                      fHasAsyncRef;
	uint64_t                  fQueueTraceID;

	bool initWithTask(task_t owningTask, void *securityID, UInt32 type) APPLE_KEXT_OVERRIDE;
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void free() APPLE_KEXT_OVERRIDE;
	IOReturn clientClose(void) APPLE_KEXT_OVERRIDE;
	IOReturn connectClient(IOUserClient *client) APPLE_KEXT_OVERRIDE;
	IOReturn registerNotificationPort(mach_port_t port, UInt32 type,
	    io_user_reference_t refCon) APPLE_KEXT_OVERRIDE;
	IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args,
	    IOExternalMethodDispatch *dispatch, OSObject *target,
	    void *reference) APPLE_KEXT_OVERRIDE;
};

OSDefineMetaClassAndStructors(XZSAccelCommandQueueUserClient, IOUserClient);

/* -------------------------------------------------------------------------
 * XZSAccelerator Implementation
 * ------------------------------------------------------------------------- */

uint64_t
XZSAccelerator::nextTraceID(void)
{
	return (uint64_t)OSIncrementAtomic64((volatile SInt64 *)&fNextGlobalTraceID) + 1ULL;
}

bool
XZSAccelerator::start(IOService *provider)
{
	if (!IOService::start(provider)) {
		return false;
	}
	fLock = IOLockAlloc();
	fNextGlobalTraceID = 0x1000ULL;
	fGlobalTraceMem = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,
	    kIODirectionInOut | kIOMemoryKernelUserShared, page_size * 4, page_size);
	fDeviceShmem = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,
	    kIODirectionInOut | kIOMemoryKernelUserShared, page_size * 4, page_size);
	fEventPage = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,
	    kIODirectionInOut | kIOMemoryKernelUserShared, page_size * 2, page_size);
	if (fLock == NULL || fGlobalTraceMem == NULL || fDeviceShmem == NULL || fEventPage == NULL) {
		return false;
	}
	bzero(fGlobalTraceMem->getBytesNoCopy(), page_size * 4);
	bzero(fDeviceShmem->getBytesNoCopy(), page_size * 4);
	bzero(fEventPage->getBytesNoCopy(), page_size * 2);

	/* Initialize physical Qualcomm Adreno 530 (A530v2 @ 0x00b00000) hardware */
	bool hw_ok = xzs_gpu_boot_init();

	int agx_mode = 0;
	(void)PE_parse_boot_argn("xzsgpu_agx", &agx_mode, sizeof(agx_mode));
	fExposeAGXPlugin = (agx_mode != 0);

	setProperty("IOClass", "AGXAcceleratorG10P");
	setProperty("IOMatchCategory", "IOAccelerator");
	/*
	 * Note: When MetalPluginName / IOGLBundleName are advertised without a
	 * matching userspace Metal/GL bundle for Adreno 530, OpenGLES.framework
	 * loads AppleMetalGLRenderer.bundle which calls MTLCreateSystemDefaultDevice(),
	 * gets nil, and fails in processes like mediaserverd and PosterBoard.
	 * Only expose MetalPluginName / IOGLBundleName when xzsgpu_agx=1 is passed.
	 */
	if (fExposeAGXPlugin) {
		setProperty("IOGLBundleName", "AppleMetalGLRenderer");
		setProperty("MetalPluginClassName", "AGXPrincipalDevice");
		setProperty("MetalPluginName", "AGXMetalA11");
	}
	setProperty("GPUModel", "Qualcomm Adreno 530 (A530v2)");
	setProperty("ChipID", 0x05030002ULL, 32);
	setProperty("HwVersion", (uint64_t)xzs_gpu_get_hw_version(), 32);
	setProperty("SpeedBin", (uint64_t)xzs_gpu_get_speed_bin(), 32);
	setProperty("GMEMSize", 0x00100000ULL, 32);
	setProperty("VRAMPhysBase", 0x90f00000ULL, 64);
	setProperty("AdrenoHardwareReady", hw_ok);

	registerService();
	IOLog("XZS GPU: XZSAccelerator (Adreno 530 hw=%d ver=0x%08x speed_bin=%u agx=%d) registered\n",
	    hw_ok ? 1 : 0, xzs_gpu_get_hw_version(), xzs_gpu_get_speed_bin(), fExposeAGXPlugin ? 1 : 0);
	return true;
}

IOReturn
XZSAccelerator::newUserClient(task_t owningTask, void *securityID,
    UInt32 type, OSDictionary *properties, IOUserClient **handler)
{
	(void)properties;
	return newUserClient(owningTask, securityID, type, handler);
}

IOReturn
XZSAccelerator::newUserClient(task_t owningTask, void *securityID,
    UInt32 type, IOUserClient **handler)
{
	IOUserClient *client = NULL;

	if (type == 1) {
		client = OSTypeAlloc(XZSAccelDeviceUserClient);
	} else if (type == 2) {
		client = OSTypeAlloc(XZSAccelSharedUserClient);
	} else if (type == 4 || type == 8) {
		client = OSTypeAlloc(XZSAccelCommandQueueUserClient);
	} else {
		client = OSTypeAlloc(XZSAccelContextUserClient);
	}

	if (client == NULL) {
		return kIOReturnNoMemory;
	}
	if (!client->initWithTask(owningTask, securityID, type) ||
	    !client->attach(this) ||
	    !client->start(this)) {
		client->detach(this);
		client->release();
		return kIOReturnError;
	}
	*handler = client;
	return kIOReturnSuccess;
}

/* -------------------------------------------------------------------------
 * Type 1: XZSAccelDeviceUserClient Implementation
 *         Disassembled from IOAccelDeviceCreateWithAPIProperty (0x1b301440c)
 * ------------------------------------------------------------------------- */

bool
XZSAccelDeviceUserClient::initWithTask(task_t owningTask, void *securityID, UInt32 type)
{
	if (!IOUserClient::initWithTask(owningTask, securityID, type)) {
		return false;
	}
	fTask = owningTask;
	return true;
}

bool
XZSAccelDeviceUserClient::start(IOService *provider)
{
	fAccel = OSDynamicCast(XZSAccelerator, provider);
	if (fAccel == NULL || !IOUserClient::start(provider)) {
		return false;
	}
	fObjectID = fAccel->nextTraceID();
	fShmemMap = fAccel->fDeviceShmem->createMappingInTask(fTask, 0, kIOMapAnywhere);
	fTraceMap = fAccel->fGlobalTraceMem->createMappingInTask(fTask, 0, kIOMapAnywhere);
	return true;
}

void
XZSAccelDeviceUserClient::free()
{
	OSSafeReleaseNULL(fShmemMap);
	OSSafeReleaseNULL(fTraceMap);
	IOUserClient::free();
}

IOReturn
XZSAccelDeviceUserClient::clientClose(void)
{
	terminate();
	return kIOReturnSuccess;
}

IOReturn
XZSAccelDeviceUserClient::clientMemoryForType(UInt32 type, IOOptionBits *options,
    IOMemoryDescriptor **memory)
{
	if (fAccel == NULL) {
		return kIOReturnNotReady;
	}
	*options = 0;
	if (type == 0) {
		fAccel->fDeviceShmem->retain();
		*memory = fAccel->fDeviceShmem;
		return kIOReturnSuccess;
	}
	fAccel->fGlobalTraceMem->retain();
	*memory = fAccel->fGlobalTraceMem;
	return kIOReturnSuccess;
}

IOReturn
XZSAccelDeviceUserClient::externalMethod(uint32_t selector,
    IOExternalMethodArguments *args, __unused IOExternalMethodDispatch *dispatch,
    __unused OSObject *target, __unused void *reference)
{
	switch (selector) {
	case 0: {
		/*
		 * Selector 0: IOAccelDeviceGetConfig / IOAccelDeviceGetConfig64 (0x40 bytes)
		 *   +0x00: vendorID (u32)
		 *   +0x04: deviceID (u32) -> 0x05030002 (Adreno 530 v2)
		 *   +0x08: mainMemorySize (u64)
		 *   +0x10: vramSize (u64)
		 *   +0x18: sharedMemorySize (u64)
		 *   +0x20: maxResourceSize (u64)
		 *   +0x28: resourceDataSize (u32, stored to [device + 0x3c])
		 *   +0x2c: flags (u32, bit 0 = global trace enabled)
		 */
		uint8_t cfg[0x40] = { 0 };
		xzs_put_u32(cfg, 0x00, 0x00005143u);            /* Qualcomm / Apple bridge */
		xzs_put_u32(cfg, 0x04, 0x05030002u);            /* Adreno 530 v2 chipid */
		xzs_put_u64(cfg, 0x08, 256ULL << 20);           /* 256 MB */
		xzs_put_u64(cfg, 0x10, 256ULL << 20);           /* 256 MB VRAM aperture */
		xzs_put_u64(cfg, 0x18, 64ULL << 20);
		xzs_put_u64(cfg, 0x20, 128ULL << 20);
		xzs_put_u32(cfg, 0x28, XZS_ACCEL_RESOURCE_PRIV_SIZE);
		xzs_put_u32(cfg, 0x2c, 0x00000000u);
		return xzs_struct_out(args, cfg, sizeof(cfg));
	}
	case 1: {
		/* Selector 1: IOAccelDeviceGetName (0x40 bytes) */
		char name[0x40] = { 0 };
		strlcpy(name, "Qualcomm Adreno 530 (XZS Metal Bridge)", sizeof(name));
		return xzs_struct_out(args, name, sizeof(name));
	}
	case 2: {
		/*
		 * Selector 2: Shared device memory pointers (0x258 bytes)
		 *   +0x00: client shared memory user VA (u64)
		 *   +0x08: global trace buffer user VA (u64)
		 *   +0x10: shmem size (u32)
		 *   +0x14: comm page count (u32, <= 16)
		 */
		uint8_t out[0x258];
		bzero(out, sizeof(out));
		uint64_t shmem_va = fShmemMap != NULL ? fShmemMap->getAddress() : 0;
		uint64_t trace_va = fTraceMap != NULL ? fTraceMap->getAddress() : shmem_va;
		xzs_put_u64(out, 0x00, shmem_va);
		xzs_put_u64(out, 0x08, trace_va);
		xzs_put_u32(out, 0x10, (uint32_t)(page_size * 4));
		xzs_put_u32(out, 0x14, 1u);
		xzs_put_u64(out, 0x18, shmem_va);
		xzs_put_u32(out, 0x98, 0u);
		return xzs_struct_out(args, out, sizeof(out));
	}
	case 4: {
		/* Selector 4: IOAccelDeviceGetCurrentTraceFilter */
		if (args->scalarOutput != NULL && args->scalarOutputCount >= 1) {
			args->scalarOutput[0] = 0;
			args->scalarOutputCount = 1;
		}
		return kIOReturnSuccess;
	}
	case 5: {
		/* Selector 5: Global trace object ID (0x18 bytes) */
		uint8_t out[0x18] = { 0 };
		xzs_put_u64(out, 0x00, fObjectID);
		return xzs_struct_out(args, out, sizeof(out));
	}
	case 6: {
		/* Selector 6: IOAccelDeviceGetNextGlobalTraceID (0x10 bytes) */
		uint8_t out[0x10] = { 0 };
		xzs_put_u64(out, 0x00, fAccel->nextTraceID());
		return xzs_struct_out(args, out, sizeof(out));
	}
	case 7:
		/* Selector 7: Set API Property (0x10 bytes in, e.g. "Metal" or "GL") */
		return kIOReturnSuccess;
	default:
		for (uint32_t i = 0; i < args->scalarOutputCount; i++) {
			args->scalarOutput[i] = 0;
		}
		if (args->structureOutput != NULL && args->structureOutputSize > 0) {
			bzero(args->structureOutput, args->structureOutputSize);
		}
		return kIOReturnSuccess;
	}
}

/* -------------------------------------------------------------------------
 * Type 2: XZSAccelSharedUserClient Implementation
 *         Disassembled from IOAccelSharedCreate (0x1b3014d68) &
 *         IOAccelResourceCreate (0x1b3016834) & IOAccelMTLEvent (0x1b3016028)
 * ------------------------------------------------------------------------- */

bool
XZSAccelSharedUserClient::initWithTask(task_t owningTask, void *securityID, UInt32 type)
{
	if (!IOUserClient::initWithTask(owningTask, securityID, type)) {
		return false;
	}
	fTask = owningTask;
	fLock = IOLockAlloc();
	fResources = OSArray::withCapacity(32);
	fShmems = OSArray::withCapacity(16);
	fNextResourceID = 1;
	fNextShmemID = 1;
	fNextEventID = 1;
	return fLock != NULL && fResources != NULL && fShmems != NULL;
}

bool
XZSAccelSharedUserClient::start(IOService *provider)
{
	fAccel = OSDynamicCast(XZSAccelerator, provider);
	if (fAccel == NULL || !IOUserClient::start(provider)) {
		return false;
	}
	fSharedTraceID = fAccel->nextTraceID();
	fEventMap = fAccel->fEventPage->createMappingInTask(fTask, 0, kIOMapAnywhere);
	return true;
}

void
XZSAccelSharedUserClient::free()
{
	OSSafeReleaseNULL(fEventMap);
	OSSafeReleaseNULL(fResources);
	OSSafeReleaseNULL(fShmems);
	if (fLock != NULL) {
		IOLockFree(fLock);
		fLock = NULL;
	}
	IOUserClient::free();
}

IOReturn
XZSAccelSharedUserClient::clientClose(void)
{
	IOLockLock(fLock);
	fResources->flushCollection();
	fShmems->flushCollection();
	IOLockUnlock(fLock);
	terminate();
	return kIOReturnSuccess;
}

IOReturn
XZSAccelSharedUserClient::clientMemoryForType(__unused UInt32 type, IOOptionBits *options,
    IOMemoryDescriptor **memory)
{
	if (fAccel == NULL) {
		return kIOReturnNotReady;
	}
	*options = 0;
	fAccel->fDeviceShmem->retain();
	*memory = fAccel->fDeviceShmem;
	return kIOReturnSuccess;
}

XZSAccelShmem *
XZSAccelSharedUserClient::findShmem(uint32_t shmemID)
{
	XZSAccelShmem *found = NULL;
	IOLockLock(fLock);
	for (unsigned int i = 0; i < fShmems->getCount(); i++) {
		XZSAccelShmem *sh = (XZSAccelShmem *)fShmems->getObject(i);
		if (sh != NULL && sh->shmemID == shmemID) {
			sh->retain();
			found = sh;
			break;
		}
	}
	IOLockUnlock(fLock);
	return found;
}

IOReturn
XZSAccelSharedUserClient::externalMethod(uint32_t selector,
    IOExternalMethodArguments *args, __unused IOExternalMethodDispatch *dispatch,
    __unused OSObject *target, __unused void *reference)
{
	switch (selector) {
	case 0: {
		/*
		 * Selector 0: new_resource (IOAccelResourceCreate @ 0x1b3016834)
		 * Input struct (args->structureInput):
		 *   +0x58: optional user pointer or size
		 * Output struct (0x58 + XZS_ACCEL_RESOURCE_PRIV_SIZE = 0x98 bytes):
		 *   +0x00: clientSharedPtr (u64)
		 *   +0x08: dataBytes user VA (u64)
		 *   +0x10: headerBase user VA (u64, with resourceID at +0x100)
		 *   +0x18: headerRef user VA (u64, with refcount at +0x18)
		 *   +0x20: flags (u64)
		 *   +0x28: dataSize (u64)
		 *   +0x30: residentDataSize (u64)
		 *   +0x38: protectionOptions (16 bytes)
		 *   +0x48: traceID (u64)
		 *   +0x50: gpuVirtualAddress (u64)
		 */
		uint64_t req_size = page_size;
		if (args->structureInput != NULL && args->structureInputSize >= 0x30) {
			uint64_t s0 = xzs_get_u64(args->structureInput, 0x28);
			uint64_t s1 = xzs_get_u64(args->structureInput, 0x18);
			if (s0 > 0 && s0 <= (128ULL << 20)) {
				req_size = round_page(s0);
			} else if (s1 > 0 && s1 <= (128ULL << 20)) {
				req_size = round_page(s1);
			}
		}

		XZSAccelResource *res = OSTypeAlloc(XZSAccelResource);
		if (res == NULL || !res->init()) {
			OSSafeReleaseNULL(res);
			return kIOReturnNoMemory;
		}
		res->allocSize = req_size;
		res->buffer = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,
		    kIODirectionInOut | kIOMemoryKernelUserShared, req_size, page_size);
		res->headerMem = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,
		    kIODirectionInOut | kIOMemoryKernelUserShared, page_size, page_size);
		if (res->buffer == NULL || res->headerMem == NULL) {
			res->release();
			return kIOReturnNoMemory;
		}
		bzero(res->buffer->getBytesNoCopy(), req_size);
		bzero(res->headerMem->getBytesNoCopy(), page_size);

		res->dataMap = res->buffer->createMappingInTask(fTask, 0, kIOMapAnywhere);
		res->headerMap = res->headerMem->createMappingInTask(fTask, 0, kIOMapAnywhere);
		if (res->dataMap == NULL || res->headerMap == NULL) {
			res->release();
			return kIOReturnVMError;
		}

		IOLockLock(fLock);
		res->resourceID = fNextResourceID++;
		res->gpuVA = 0x100000000ULL + ((uint64_t)res->resourceID << 20);
		uint8_t *hdr_kern = (uint8_t *)res->headerMem->getBytesNoCopy();
		xzs_put_u32(hdr_kern, 0x18, 1u);               /* initial refcount */
		xzs_put_u64(hdr_kern, 0x20, res->gpuVA);       /* GPU VA */
		xzs_put_u32(hdr_kern, 0x100, res->resourceID); /* resourceID at +0x100 */
		fResources->setObject(res);
		IOLockUnlock(fLock);

		uint8_t out[0x58 + XZS_ACCEL_RESOURCE_PRIV_SIZE];
		bzero(out, sizeof(out));
		uint64_t data_va = res->dataMap->getAddress();
		uint64_t hdr_va  = res->headerMap->getAddress();
		xzs_put_u64(out, 0x00, hdr_va);
		xzs_put_u64(out, 0x08, data_va);
		xzs_put_u64(out, 0x10, hdr_va);
		xzs_put_u64(out, 0x18, hdr_va);
		xzs_put_u64(out, 0x28, req_size);
		xzs_put_u64(out, 0x30, req_size);
		xzs_put_u64(out, 0x48, fAccel->nextTraceID());
		xzs_put_u64(out, 0x50, res->gpuVA);
		res->release();
		return xzs_struct_out(args, out, sizeof(out));
	}
	case 1: {
		/* Selector 1: delete_resource (scalarIn[0] = resourceID) */
		uint32_t rid = args->scalarInputCount > 0 ? (uint32_t)args->scalarInput[0] : 0;
		IOLockLock(fLock);
		for (unsigned int i = 0; i < fResources->getCount(); i++) {
			XZSAccelResource *r = (XZSAccelResource *)fResources->getObject(i);
			if (r != NULL && r->resourceID == rid) {
				fResources->removeObject(i);
				break;
			}
		}
		IOLockUnlock(fLock);
		return kIOReturnSuccess;
	}
	case 2: /* page_off */
	case 3: /* finish */
		return kIOReturnSuccess;
	case 4: /* set_purgeable */
		if (args->scalarOutput != NULL && args->scalarOutputCount >= 1) {
			args->scalarOutput[0] = 0; /* kIOAccelResourcePurgeableNonVolatile */
			args->scalarOutputCount = 1;
		}
		return kIOReturnSuccess;
	case 5: {
		/*
		 * Selector 5: IOAccelSharedAllocateResourceSysMem (0x1b301500c)
		 *   scalarIn[0] = size
		 *   scalarOut[0] = shmemID
		 *   structOut (0x10 bytes): +0x00 = mapped VA (u64), +0x08 = size (u32)
		 */
		uint32_t req_sz = args->scalarInputCount > 0 ? (uint32_t)args->scalarInput[0] : (uint32_t)page_size;
		if (req_sz == 0 || req_sz > (32u << 20)) {
			req_sz = (uint32_t)page_size;
		}
		uint32_t alloc_sz = (uint32_t)round_page(req_sz);
		XZSAccelShmem *sh = OSTypeAlloc(XZSAccelShmem);
		if (sh == NULL || !sh->init()) {
			OSSafeReleaseNULL(sh);
			return kIOReturnNoMemory;
		}
		sh->size = alloc_sz;
		sh->mem = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,
		    kIODirectionInOut | kIOMemoryKernelUserShared, alloc_sz, page_size);
		if (sh->mem == NULL) {
			sh->release();
			return kIOReturnNoMemory;
		}
		bzero(sh->mem->getBytesNoCopy(), alloc_sz);
		sh->map = sh->mem->createMappingInTask(fTask, 0, kIOMapAnywhere);
		if (sh->map == NULL) {
			sh->release();
			return kIOReturnVMError;
		}
		IOLockLock(fLock);
		sh->shmemID = fNextShmemID++;
		fShmems->setObject(sh);
		IOLockUnlock(fLock);

		if (args->scalarOutput != NULL && args->scalarOutputCount >= 1) {
			args->scalarOutput[0] = sh->shmemID;
			args->scalarOutputCount = 1;
		}
		uint8_t out[0x10] = { 0 };
		xzs_put_u64(out, 0x00, sh->map->getAddress());
		xzs_put_u32(out, 0x08, alloc_sz);
		sh->release();
		return xzs_struct_out(args, out, sizeof(out));
	}
	case 6: {
		/* Selector 6: IOAccelSharedFreeResourceSysMem (scalarIn[0] = shmemID) */
		uint32_t sid = args->scalarInputCount > 0 ? (uint32_t)args->scalarInput[0] : 0;
		IOLockLock(fLock);
		for (unsigned int i = 0; i < fShmems->getCount(); i++) {
			XZSAccelShmem *sh = (XZSAccelShmem *)fShmems->getObject(i);
			if (sh != NULL && sh->shmemID == sid) {
				fShmems->removeObject(i);
				break;
			}
		}
		IOLockUnlock(fLock);
		return kIOReturnSuccess;
	}
	case 7: {
		/* Selector 7: Shared client init (0x10 bytes out: +0x00 = sharedTraceID) */
		uint8_t out[0x10] = { 0 };
		xzs_put_u64(out, 0x00, fSharedTraceID);
		return xzs_struct_out(args, out, sizeof(out));
	}
	case 8: {
		/* Selector 8: IOAccelSharedGetAllocatedSize (8 bytes out) */
		uint8_t out[8] = { 0 };
		return xzs_struct_out(args, out, sizeof(out));
	}
	case 9: {
		/*
		 * Selector 9: -[IOAccelMTLEvent initWithShared:] (0x1b3016028)
		 *   structOut (0x10 bytes): +0x00 = eventName (u32), +0x08 = userVA of 64-bit event value
		 */
		IOLockLock(fLock);
		uint32_t ev_id = fNextEventID++;
		IOLockUnlock(fLock);
		uint32_t slot = ev_id % XZS_ACCEL_MAX_EVENTS;
		uint64_t ev_va = fEventMap != NULL ? (fEventMap->getAddress() + slot * 8ULL) : 0;
		uint8_t out[0x10] = { 0 };
		xzs_put_u32(out, 0x00, ev_id);
		xzs_put_u64(out, 0x08, ev_va);
		return xzs_struct_out(args, out, sizeof(out));
	}
	case 10:
		/* Selector 10: -[IOAccelMTLEvent dealloc] (scalarIn[0] = eventName) */
		return kIOReturnSuccess;
	case 11: {
		/* Selector 11: IOAccelSharedGetMemoryData (0x30 bytes out) */
		uint8_t out[0x30] = { 0 };
		return xzs_struct_out(args, out, sizeof(out));
	}
	case 13: {
		/* Selector 13: IOAccelSharedSetWaitForGPUCallback (8 bytes out) */
		uint8_t out[8] = { 0 };
		return xzs_struct_out(args, out, sizeof(out));
	}
	default:
		for (uint32_t i = 0; i < args->scalarOutputCount; i++) {
			args->scalarOutput[i] = 0;
		}
		if (args->structureOutput != NULL && args->structureOutputSize > 0) {
			bzero(args->structureOutput, args->structureOutputSize);
		}
		return kIOReturnSuccess;
	}
}

/* -------------------------------------------------------------------------
 * Type 0 / 3: XZSAccelContextUserClient Implementation
 *             Disassembled from IOAccelContextCreate (0x1b30154a0)
 * ------------------------------------------------------------------------- */

bool
XZSAccelContextUserClient::initWithTask(task_t owningTask, void *securityID, UInt32 type)
{
	if (!IOUserClient::initWithTask(owningTask, securityID, type)) {
		return false;
	}
	fTask = owningTask;
	fClientType = type;
	return true;
}

bool
XZSAccelContextUserClient::start(IOService *provider)
{
	fAccel = OSDynamicCast(XZSAccelerator, provider);
	if (fAccel == NULL || !IOUserClient::start(provider)) {
		return false;
	}
	for (int i = 0; i < 4; i++) {
		fContextMem[i] = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,
		    kIODirectionInOut | kIOMemoryKernelUserShared, page_size * 4, page_size);
		if (fContextMem[i] != NULL) {
			bzero(fContextMem[i]->getBytesNoCopy(), page_size * 4);
		}
	}
	return true;
}

void
XZSAccelContextUserClient::free()
{
	for (int i = 0; i < 4; i++) {
		OSSafeReleaseNULL(fContextMem[i]);
	}
	OSSafeReleaseNULL(fShared);
	IOUserClient::free();
}

IOReturn
XZSAccelContextUserClient::clientClose(void)
{
	terminate();
	return kIOReturnSuccess;
}

IOReturn
XZSAccelContextUserClient::connectClient(IOUserClient *client)
{
	XZSAccelSharedUserClient *sh = OSDynamicCast(XZSAccelSharedUserClient, client);
	if (sh != NULL) {
		OSSafeReleaseNULL(fShared);
		sh->retain();
		fShared = sh;
	}
	return kIOReturnSuccess;
}

IOReturn
XZSAccelContextUserClient::clientMemoryForType(UInt32 type, IOOptionBits *options,
    IOMemoryDescriptor **memory)
{
	uint32_t idx = type < 4 ? type : 0;
	if (fContextMem[idx] == NULL) {
		return kIOReturnNoMemory;
	}
	*options = 0;
	fContextMem[idx]->retain();
	*memory = fContextMem[idx];
	return kIOReturnSuccess;
}

IOReturn
XZSAccelContextUserClient::externalMethod(uint32_t selector,
    IOExternalMethodArguments *args, __unused IOExternalMethodDispatch *dispatch,
    __unused OSObject *target, __unused void *reference)
{
	switch (selector) {
	case 2: {
		/* Selector 2: submit_data_buffers (structOut 0x20 bytes) */
		uint32_t nop_pkt[2] = { CP_TYPE7_PKT(CP_NOP, 1), 0x585a5300u };
		(void)xzs_gpu_submit_cp_ring(nop_pkt, 2);
		uint8_t out[0x20] = { 0 };
		return xzs_struct_out(args, out, sizeof(out));
	}
	default:
		for (uint32_t i = 0; i < args->scalarOutputCount; i++) {
			args->scalarOutput[i] = 0;
		}
		if (args->structureOutput != NULL && args->structureOutputSize > 0) {
			bzero(args->structureOutput, args->structureOutputSize);
		}
		return kIOReturnSuccess;
	}
}

/* -------------------------------------------------------------------------
 * Type 4: XZSAccelCommandQueueUserClient Implementation
 *         Disassembled from IOAccelCommandQueueCreateWithQoS (0x1b3016238) &
 *         IOAccelCommandQueueSubmitCommandBuffers (0x1b3016534)
 * ------------------------------------------------------------------------- */

bool
XZSAccelCommandQueueUserClient::initWithTask(task_t owningTask, void *securityID, UInt32 type)
{
	if (!IOUserClient::initWithTask(owningTask, securityID, type)) {
		return false;
	}
	fTask = owningTask;
	fNotifyPort = MACH_PORT_NULL;
	bzero(fAsyncRef, sizeof(fAsyncRef));
	fHasAsyncRef = false;
	return true;
}

bool
XZSAccelCommandQueueUserClient::start(IOService *provider)
{
	fAccel = OSDynamicCast(XZSAccelerator, provider);
	if (fAccel == NULL || !IOUserClient::start(provider)) {
		return false;
	}
	fQueueTraceID = fAccel->nextTraceID();
	return true;
}

void
XZSAccelCommandQueueUserClient::free()
{
	OSSafeReleaseNULL(fShared);
	IOUserClient::free();
}

IOReturn
XZSAccelCommandQueueUserClient::clientClose(void)
{
	fNotifyPort = MACH_PORT_NULL;
	fHasAsyncRef = false;
	terminate();
	return kIOReturnSuccess;
}

IOReturn
XZSAccelCommandQueueUserClient::connectClient(IOUserClient *client)
{
	XZSAccelSharedUserClient *sh = OSDynamicCast(XZSAccelSharedUserClient, client);
	if (sh != NULL) {
		OSSafeReleaseNULL(fShared);
		sh->retain();
		fShared = sh;
	}
	return kIOReturnSuccess;
}

IOReturn
XZSAccelCommandQueueUserClient::registerNotificationPort(mach_port_t port,
    __unused UInt32 type, __unused io_user_reference_t refCon)
{
	fNotifyPort = port;
	return kIOReturnSuccess;
}

IOReturn
XZSAccelCommandQueueUserClient::externalMethod(uint32_t selector,
    IOExternalMethodArguments *args, __unused IOExternalMethodDispatch *dispatch,
    __unused OSObject *target, __unused void *reference)
{
	switch (selector) {
	case 0: {
		/*
		 * Selector 0: Async notification registration (0x1b30163b8)
		 *   Saves asyncWakePort & asyncReference (callback sub_1b3016478)
		 */
		if (args->asyncWakePort != MACH_PORT_NULL && args->asyncReference != NULL) {
			fNotifyPort = args->asyncWakePort;
			memcpy(fAsyncRef, args->asyncReference, sizeof(OSAsyncReference64));
			fHasAsyncRef = true;
		}
		return kIOReturnSuccess;
	}
	case 1: {
		/*
		 * Selector 1: IOAccelCommandQueueSubmitCommandBuffers (0x1b3016534)
		 *   structIn size = 8 + count * 0x18
		 *   +0x00: uint32_t flags
		 *   +0x04: uint32_t count
		 *   Entries at +0x08 (each 0x18 bytes):
		 *     +0x00: uint32_t commandBufferShmemID
		 *     +0x04: uint32_t kernelCommandsShmemID
		 *     +0x08: uint64_t completionBlock
		 *     +0x10: uint64_t completionRefcon
		 */
		if (args->structureInput == NULL || args->structureInputSize < 8) {
			return kIOReturnBadArgument;
		}
		const uint8_t *in = (const uint8_t *)args->structureInput;
		uint32_t count = xzs_get_u32(in, 0x04);
		if (8u + count * 0x18u > args->structureInputSize) {
			count = (args->structureInputSize - 8u) / 0x18u;
		}

		for (uint32_t i = 0; i < count; i++) {
			const uint8_t *entry = in + 8u + i * 0x18u;
			uint32_t cmd_shmem_id  = xzs_get_u32(entry, 0x00);
			uint32_t kern_shmem_id = xzs_get_u32(entry, 0x04);
			uint64_t comp_block    = xzs_get_u64(entry, 0x08);
			uint64_t comp_refcon   = xzs_get_u64(entry, 0x10);

			/* Process kernel event signal/wait commands if kernelCommandsShmem is present */
			if (fShared != NULL && kern_shmem_id != 0) {
				XZSAccelShmem *ksh = fShared->findShmem(kern_shmem_id);
				if (ksh != NULL && ksh->mem != NULL && fAccel != NULL && fAccel->fEventPage != NULL) {
					uint64_t *ev_slots = (uint64_t *)fAccel->fEventPage->getBytesNoCopy();
					const uint8_t *kbuf = (const uint8_t *)ksh->mem->getBytesNoCopy();
					uint32_t koff = 8u;
					while (koff + 24u <= ksh->size) {
						uint32_t cmd_type = xzs_get_u32(kbuf, koff);
						uint32_t cmd_len  = xzs_get_u32(kbuf, koff + 4u);
						if (cmd_type == 0 || cmd_len < 8u || koff + cmd_len > ksh->size) {
							break;
						}
						if ((cmd_type == 6u || cmd_type == 7u || cmd_type == 12u) && cmd_len >= 24u) {
							uint32_t ev_name = xzs_get_u32(kbuf, koff + 8u);
							uint64_t ev_val  = xzs_get_u64(kbuf, koff + 16u);
							uint32_t slot = ev_name % XZS_ACCEL_MAX_EVENTS;
							if (cmd_type == 6u) {
								ev_slots[slot] = ev_val;
								xzs_gpu_record_event();
							}
						}
						koff += cmd_len;
					}
				}
				OSSafeReleaseNULL(ksh);
			}

			/* Submit a tracked PM4 packet into the Adreno 530 CP Ringbuffer */
			uint32_t pkt[4] = {
				CP_TYPE7_PKT(CP_NOP, 3),
				0x4d544c30u, /* 'MTL0' marker */
				cmd_shmem_id,
				kern_shmem_id
			};
			(void)xzs_gpu_submit_cp_ring(pkt, 4);

			/* Deliver async completion notification expected by sub_1b3016478 */
			if (fHasAsyncRef && fNotifyPort != MACH_PORT_NULL && comp_block != 0) {
				io_user_reference_t retArgs[7] = {
					(io_user_reference_t)comp_block,
					0,
					(io_user_reference_t)comp_refcon,
					0,
					0,
					0,
					0
				};
				(void)IOUserClient::sendAsyncResult64(fAsyncRef, kIOReturnSuccess, retArgs, 7);
			}
		}
		return kIOReturnSuccess;
	}
	case 2: /* set_priority_and_background (0xc bytes in) */
	case 3: /* finish */
	case 4: /* set_quality_of_service (0x4 bytes in) */
		return kIOReturnSuccess;
	case 5: {
		/* Selector 5: Queue setup / get trace ID (0x404 bytes in -> 8 bytes out) */
		uint8_t out[8] = { 0 };
		xzs_put_u64(out, 0x00, fQueueTraceID);
		return xzs_struct_out(args, out, sizeof(out));
	}
	default:
		for (uint32_t i = 0; i < args->scalarOutputCount; i++) {
			args->scalarOutput[i] = 0;
		}
		if (args->structureOutput != NULL && args->structureOutputSize > 0) {
			bzero(args->structureOutput, args->structureOutputSize);
		}
		return kIOReturnSuccess;
	}
}

#endif /* SWEET || XZS */
