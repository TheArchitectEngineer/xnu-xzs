/*
 * Built-in IOSurface for SWEET.
 *
 * Apple's IOSurface kext is closed. IOSurface.framework (iOS 16.7) finds
 * IOServiceNameMatching("IOCoreSurfaceRoot"), opens it with type 0 and
 * talks to the user client below:
 *
 *   sel 13  limits: struct out 0x18, u16 at +0x16 must be 1 (ABI version),
 *           +0 offset alignment mask, +4 bytes-per-row alignment mask
 *   sel 0   create: scalar [parent id], struct in = serialized property
 *           dictionary (binary or XML), struct out = lock result (0xa68)
 *   sel 4   lookup by id: scalar [id], struct out = lock result
 *   sel 34  lookup by mach port: scalar [port name], struct out = lock result
 *   sel 35  create mach port: scalars [id, options], scalar out [port name]
 *   traps   0/1 use count +/- (id, category), 2 lock / 3 unlock
 *           (id, options, &seed), 4/5 release (id)
 *
 * The lock result tells the library where the surface lives in the
 * caller's address space: +0 pixels, +8 shared state page (registry id,
 * seed at +0xc), +0x10 timestamps, then id, sizes and geometry (layout in
 * sweet_surface_fill_result). Every surface is wired kernel memory mapped
 * into each task that creates or looks it up; mappings are counted per
 * client and dropped on release or close.
 *
 * Values, notifications, accelerators and shared events are accepted and
 * ignored (logged once per selector).
 */
#include "XZSSurface.h"

#if defined(SWEET) || defined(XZS)
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/system.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSSerialize.h>
#include <libkern/c++/OSString.h>
#include <libkern/c++/OSSymbol.h>
#include <libkern/c++/OSUnserialize.h>
#include <sys/proc.h>

extern "C" bool g_xzs_springboard_surface_ready;
extern "C" void xzs_gpu_record_event(void);

enum {
	kSweetSurfaceCreate             = 0,
	kSweetSurfaceLookup             = 4,
	kSweetSurfaceSetYCbCrMatrix     = 5,
	kSweetSurfaceSetValue           = 9,
	kSweetSurfaceCopyValue          = 10,
	kSweetSurfaceRemoveValue        = 11,
	kSweetSurfaceLimits             = 13,
	kSweetSurfaceSetBulkAttachments = 27,
	kSweetSurfaceLookupFromPort     = 34,
	kSweetSurfaceCreatePort         = 35,
	kSweetSurfaceCreateSharedEvent  = 36,
	kSweetSurfaceSignalSharedEvent  = 37,
	kSweetSurfaceOpenSharedEvent    = 38,
	kSweetSurfaceNotifySharedEvent  = 39,
	kSweetSurfaceAddEventListener   = 40,
	kSweetSurfaceRemoveEventListener= 41,
	kSweetSurfaceWaitSharedEvent    = 45,
};

enum {
	kSweetSurfaceTrapIncrementUse   = 0,
	kSweetSurfaceTrapDecrementUse   = 1,
	kSweetSurfaceTrapLock           = 2,
	kSweetSurfaceTrapUnlock         = 3,
	kSweetSurfaceTrapRelease        = 4,
	kSweetSurfaceTrapReleaseFailed  = 5,
};

#define SWEET_SURFACE_RESULT_SIZE       0xa68
#define SWEET_SURFACE_MAX_ID            4096
#define SWEET_SURFACE_MAX_DIM           16384
#define SWEET_SURFACE_MAX_ALLOC         (256u << 20)
#define SWEET_SURFACE_MAX_PROPS         (64u << 10)
#define SWEET_SURFACE_ROW_ALIGN         64
#define SWEET_SURFACE_TIMESTAMPS        0x800   /* offset in the state page */
#define SWEET_SURFACE_STATE_SEED        0xc     /* u32 in the state page */
#define kIOSurfaceLockReadOnly          0x1

static IOLock       *sweet_surface_lock;
static SweetSurface *sweet_surface_table[SWEET_SURFACE_MAX_ID];
static uint32_t      sweet_surface_users[SWEET_SURFACE_MAX_ID];
static uint32_t      sweet_surface_next_id = 1;
static uint32_t      sweet_surface_creates_logged;
static uint64_t      sweet_surface_wired;       /* bytes of buffer surfaces */
#define SWEET_SURFACE_WIRED_MAX         (1536ull << 20)

OSDefineMetaClassAndStructors(SweetSurface, OSObject);

void
SweetSurface::free()
{
	if (OSDynamicCast(IOBufferMemoryDescriptor, memory) != NULL) {
		OSAddAtomic64(-(SInt64)allocSize, (volatile SInt64 *)&sweet_surface_wired);
	}
	OSSafeReleaseNULL(kernelMap);
	OSSafeReleaseNULL(memory);
	OSSafeReleaseNULL(state);
	OSSafeReleaseNULL(values);
	OSObject::free();
}

uintptr_t
SweetSurface::kernelAddress()
{
	IOBufferMemoryDescriptor *buffer = OSDynamicCast(IOBufferMemoryDescriptor, memory);
	if (buffer != NULL) {
		return (uintptr_t)buffer->getBytesNoCopy();
	}
	IOLockLock(sweet_surface_lock);
	if (kernelMap == NULL && memory != NULL) {
		kernelMap = memory->map(kIOMapWriteCombineCache);
	}
	IOLockUnlock(sweet_surface_lock);
	return kernelMap != NULL ? (uintptr_t)kernelMap->getVirtualAddress() : 0;
}

static uint32_t *
sweet_surface_seed(SweetSurface *s)
{
	return (uint32_t *)((uint8_t *)s->state->getBytesNoCopy() + SWEET_SURFACE_STATE_SEED);
}

/*
 * Give the surface an id and a table slot; the table holds one retain and a
 * user count (creators, client mappings, kernel holders). Lock held.
 */
static bool
sweet_surface_insert_locked(SweetSurface *s)
{
	for (uint32_t n = 0; n < SWEET_SURFACE_MAX_ID - 1; n++) {
		uint32_t id = sweet_surface_next_id;
		sweet_surface_next_id = id + 1 < SWEET_SURFACE_MAX_ID ? id + 1 : 1;
		if (sweet_surface_table[id] == NULL) {
			s->id = id;
			s->retain();
			sweet_surface_table[id] = s;
			sweet_surface_users[id] = 1;
			*(uint64_t *)s->state->getBytesNoCopy() = id;   /* registry id */
			return true;
		}
	}
	return false;
}

/* Add a user to a surface that may have left the table (port lookups). */
static bool
sweet_surface_get(SweetSurface *s)
{
	bool ok = true;
	IOLockLock(sweet_surface_lock);
	if (sweet_surface_table[s->id] == s) {
		sweet_surface_users[s->id]++;
	} else if (sweet_surface_table[s->id] == NULL) {
		s->retain();
		sweet_surface_table[s->id] = s;
		sweet_surface_users[s->id] = 1;
	} else {
		ok = false;
	}
	IOLockUnlock(sweet_surface_lock);
	return ok;
}

/* Drop a user; the last one removes the surface from the table. */
static void
sweet_surface_put(SweetSurface *s)
{
	bool drop = false;
	IOLockLock(sweet_surface_lock);
	if (sweet_surface_table[s->id] == s && --sweet_surface_users[s->id] == 0) {
		sweet_surface_table[s->id] = NULL;
		drop = true;
	}
	IOLockUnlock(sweet_surface_lock);
	if (drop) {
		s->release();
	}
}

SweetSurface *
sweet_surface_copy(uint32_t id)
{
	SweetSurface *s = NULL;
	if (id == 0 || id >= SWEET_SURFACE_MAX_ID || sweet_surface_lock == NULL) {
		return NULL;
	}
	IOLockLock(sweet_surface_lock);
	s = sweet_surface_table[id];
	if (s != NULL) {
		s->retain();
	}
	IOLockUnlock(sweet_surface_lock);
	return s;
}

/* The table lock; the framebuffer may publish a surface before the root starts. */
static void
sweet_surface_init_lock(void)
{
	if (sweet_surface_lock == NULL) {
		IOLock *lock = IOLockAlloc();
		if (!OSCompareAndSwapPtr(NULL, lock, &sweet_surface_lock)) {
			IOLockFree(lock);
		}
	}
}

static SweetSurface *
sweet_surface_alloc(void)
{
	sweet_surface_init_lock();
	SweetSurface *s = OSTypeAlloc(SweetSurface);
	if (s == NULL || !s->init()) {
		OSSafeReleaseNULL(s);
		return NULL;
	}
	s->state = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,
	    kIODirectionInOut | kIOMemoryKernelUserShared, page_size, page_size);
	s->values = OSDictionary::withCapacity(8);
	if (s->state == NULL || s->values == NULL) {
		s->release();
		return NULL;
	}
	bzero(s->state->getBytesNoCopy(), page_size);
	s->valueSeed = 1;
	uint8_t *stateBytes = (uint8_t *)s->state->getBytesNoCopy();
	*(uint32_t *)(stateBytes + 0x20) = s->valueSeed;
	*(uint32_t *)(stateBytes + 0x28) = 256;
	return s;
}

static bool
sweet_surface_publish(SweetSurface *s)
{
	bool ok;
	IOLockLock(sweet_surface_lock);
	ok = sweet_surface_insert_locked(s);
	IOLockUnlock(sweet_surface_lock);
	return ok;
}

SweetSurface *
sweet_surface_create_physical(uint64_t phys, uint32_t width, uint32_t height,
    uint32_t bytesPerRow, uint32_t pixelFormat)
{
	(void)phys;
	SweetSurface *s = sweet_surface_alloc();
	if (s == NULL) {
		return NULL;
	}
	s->width = width;
	s->height = height;
	s->bytesPerRow = bytesPerRow;
	s->bytesPerElement = 4;
	s->elementWidth = s->elementHeight = 1;
	s->pixelFormat = pixelFormat;
	s->allocSize = (uint32_t)round_page(bytesPerRow * height);
	s->memory = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,
	    kIODirectionInOut | kIOMemoryKernelUserShared | kIOMapWriteCombineCache,
	    s->allocSize, page_size);
	s->mapCache = kIOMapWriteCombineCache;
	if (s->memory == NULL) {
		s->release();
		return NULL;
	}
	OSAddAtomic64(s->allocSize, (volatile SInt64 *)&sweet_surface_wired);
	bzero(((IOBufferMemoryDescriptor *)s->memory)->getBytesNoCopy(), s->allocSize);
	if (!sweet_surface_publish(s)) {
		s->release();
		return NULL;
	}
	return s;
}

mach_port_name_t
sweet_surface_port_in_task(SweetSurface *s, task_t task)
{
	mach_port_name_t name = 0;
	IOUserClient::copyPortNameForObjectInTask(task, s, &name);
	return name;
}

static uint32_t
sweet_dict_u32(OSDictionary *dict, const char *key, uint32_t def)
{
	OSNumber *n = OSDynamicCast(OSNumber, dict->getObject(key));
	return n != NULL ? n->unsigned32BitValue() : def;
}

static uint32_t
sweet_div_up(uint32_t a, uint32_t b)
{
	return b > 1 ? (a + b - 1) / b : a;
}

static uint32_t
sweet_row_bytes(uint32_t width, uint32_t ew, uint32_t bpe)
{
	uint64_t row = (uint64_t)sweet_div_up(width, ew) * bpe;
	return (uint32_t)((row + SWEET_SURFACE_ROW_ALIGN - 1) & ~(uint64_t)(SWEET_SURFACE_ROW_ALIGN - 1));
}

/* Geometry from the creation properties (IOSurface.h keys). */
static IOReturn
sweet_surface_layout(SweetSurface *s, OSDictionary *props)
{
	uint64_t end;

	s->width = sweet_dict_u32(props, "IOSurfaceWidth", 0);
	s->height = sweet_dict_u32(props, "IOSurfaceHeight", 0);
	s->bytesPerElement = (uint16_t)sweet_dict_u32(props, "IOSurfaceBytesPerElement", 4);
	s->elementWidth = (uint8_t)sweet_dict_u32(props, "IOSurfaceElementWidth", 1);
	s->elementHeight = (uint8_t)sweet_dict_u32(props, "IOSurfaceElementHeight", 1);
	s->pixelFormat = sweet_dict_u32(props, "IOSurfacePixelFormat", 0);
	s->cacheMode = sweet_dict_u32(props, "IOSurfaceCacheMode", 0);
	s->offset = sweet_dict_u32(props, "IOSurfaceOffset", 0);
	if (s->width > SWEET_SURFACE_MAX_DIM || s->height > SWEET_SURFACE_MAX_DIM ||
	    s->bytesPerElement > 64 || s->elementWidth == 0 || s->elementHeight == 0) {
		return kIOReturnBadArgument;
	}
	s->bytesPerRow = sweet_dict_u32(props, "IOSurfaceBytesPerRow",
	    sweet_row_bytes(s->width, s->elementWidth, s->bytesPerElement));
	if (s->bytesPerRow < (uint64_t)sweet_div_up(s->width, s->elementWidth) * s->bytesPerElement) {
		return kIOReturnBadArgument;
	}
	end = (uint64_t)s->offset + (uint64_t)s->bytesPerRow * sweet_div_up(s->height, s->elementHeight);

	OSArray *planes = OSDynamicCast(OSArray, props->getObject("IOSurfacePlaneInfo"));
	s->planeCount = 0;
	if (planes != NULL) {
		uint64_t next = s->offset;
		if (planes->getCount() > SWEET_SURFACE_MAX_PLANES) {
			return kIOReturnBadArgument;
		}
		for (unsigned int i = 0; i < planes->getCount(); i++) {
			OSDictionary *pd = OSDynamicCast(OSDictionary, planes->getObject(i));
			struct sweet_surface_plane *p = &s->planes[i];
			if (pd == NULL) {
				return kIOReturnBadArgument;
			}
			p->width = sweet_dict_u32(pd, "IOSurfacePlaneWidth", s->width);
			p->height = sweet_dict_u32(pd, "IOSurfacePlaneHeight", s->height);
			p->bytesPerElement = (uint16_t)sweet_dict_u32(pd, "IOSurfacePlaneBytesPerElement", 1);
			p->elementWidth = (uint8_t)sweet_dict_u32(pd, "IOSurfacePlaneElementWidth", 1);
			p->elementHeight = (uint8_t)sweet_dict_u32(pd, "IOSurfacePlaneElementHeight", 1);
			if (p->width > SWEET_SURFACE_MAX_DIM || p->height > SWEET_SURFACE_MAX_DIM ||
			    p->bytesPerElement > 64 || p->elementWidth == 0 || p->elementHeight == 0) {
				return kIOReturnBadArgument;
			}
			p->bytesPerRow = sweet_dict_u32(pd, "IOSurfacePlaneBytesPerRow",
			    sweet_row_bytes(p->width, p->elementWidth, p->bytesPerElement));
			p->offset = sweet_dict_u32(pd, "IOSurfacePlaneOffset", (uint32_t)next);
			p->size = sweet_dict_u32(pd, "IOSurfacePlaneSize",
			    p->bytesPerRow * sweet_div_up(p->height, p->elementHeight));
			next = round_page((uint64_t)p->offset + p->size);
			if ((uint64_t)p->offset + p->size > end) {
				end = (uint64_t)p->offset + p->size;
			}
		}
		s->planeCount = planes->getCount();
	}

	uint64_t alloc = sweet_dict_u32(props, "IOSurfaceAllocSize", 0);
	if (alloc < end) {
		alloc = end;
	}
	alloc = round_page(alloc);
	if (alloc == 0 || alloc > SWEET_SURFACE_MAX_ALLOC) {
		return kIOReturnNoMemory;
	}
	s->allocSize = (uint32_t)alloc;
	return kIOReturnSuccess;
}

/* Per-client mapping of one surface. */
class SweetSurfaceRef : public OSObject {
	OSDeclareDefaultStructors(SweetSurfaceRef);

public:
	SweetSurface *surface;
	IOMemoryMap  *data;
	IOMemoryMap  *state;
	uint32_t      count;

	bool init() APPLE_KEXT_OVERRIDE { return OSObject::init(); }
	void free() APPLE_KEXT_OVERRIDE;
};

OSDefineMetaClassAndStructors(SweetSurfaceRef, OSObject);

void
SweetSurfaceRef::free()
{
	OSSafeReleaseNULL(data);
	OSSafeReleaseNULL(state);
	if (surface != NULL) {
		sweet_surface_put(surface);
		surface->release();
		surface = NULL;
	}
	OSObject::free();
}

class SweetSharedEvent : public OSObject {
	OSDeclareDefaultStructors(SweetSharedEvent);

public:
	uint32_t                  eventID;
	IOBufferMemoryDescriptor *sharedMem;

	bool init() APPLE_KEXT_OVERRIDE { return OSObject::init(); }
	void free() APPLE_KEXT_OVERRIDE;
	volatile uint64_t *valuePtr() const {
		return sharedMem != NULL ? (volatile uint64_t *)sharedMem->getBytesNoCopy() : NULL;
	}
};

OSDefineMetaClassAndStructors(SweetSharedEvent, OSObject);

static uint32_t sweet_shared_event_next_id = 1;

void
SweetSharedEvent::free()
{
	OSSafeReleaseNULL(sharedMem);
	OSObject::free();
}

static SweetSharedEvent *
sweet_shared_event_alloc(void)
{
	SweetSharedEvent *ev = OSTypeAlloc(SweetSharedEvent);
	if (ev == NULL || !ev->init()) {
		OSSafeReleaseNULL(ev);
		return NULL;
	}
	ev->eventID = OSIncrementAtomic((volatile SInt32 *)&sweet_shared_event_next_id) + 1;
	ev->sharedMem = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,
	    kIODirectionInOut | kIOMemoryKernelUserShared, page_size, page_size);
	if (ev->sharedMem == NULL) {
		ev->release();
		return NULL;
	}
	bzero(ev->sharedMem->getBytesNoCopy(), page_size);
	return ev;
}

class IOCoreSurfaceRoot : public IOService {
	OSDeclareDefaultStructors(IOCoreSurfaceRoot);

public:
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
};

OSDefineMetaClassAndStructors(IOCoreSurfaceRoot, IOService);

bool
IOCoreSurfaceRoot::start(IOService *provider)
{
	if (!IOService::start(provider)) {
		return false;
	}
	sweet_surface_init_lock();
	setProperty(kIOUserClientClassKey, "IOSurfaceRootUserClient");
	registerService();
	IOLog("SWEET SURF: IOCoreSurfaceRoot registered\n");
	return true;
}

class IOSurfaceRootUserClient : public IOUserClient {
	OSDeclareDefaultStructors(IOSurfaceRootUserClient);

	task_t             fTask;
	IOLock            *fLock;
	OSArray           *fRefs;
	OSArray           *fEventMaps;
	OSAsyncReference64 fEventAsyncRef;
	bool               fHasEventAsyncRef;

	SweetSurfaceRef *findRef(uint32_t id, unsigned int *index);
	IOReturn addRef(SweetSurface *s, SweetSurfaceRef **refOut, bool *existing);
	IOReturn returnSurface(SweetSurface *s, IOExternalMethodArguments *args);
	IOReturn create(IOExternalMethodArguments *args);
	IOReturn setValue(IOExternalMethodArguments *args);
	IOReturn copyValue(IOExternalMethodArguments *args);
	IOReturn removeValue(IOExternalMethodArguments *args);
	IOReturn setBulkAttachments(IOExternalMethodArguments *args);

public:
	bool initWithTask(task_t owningTask, void *securityID, UInt32 type) APPLE_KEXT_OVERRIDE;
	void free() APPLE_KEXT_OVERRIDE;
	IOReturn clientClose(void) APPLE_KEXT_OVERRIDE;
	IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args,
	    IOExternalMethodDispatch *dispatch, OSObject *target,
	    void *reference) APPLE_KEXT_OVERRIDE;
	IOExternalTrap *getTargetAndTrapForIndex(IOService **targetP, UInt32 index) APPLE_KEXT_OVERRIDE;

	IOReturn trapUseCount(void *id, void *category, void *, void *, void *, void *);
	IOReturn trapLock(void *id, void *options, void *seedOut, void *, void *, void *);
	IOReturn trapUnlock(void *id, void *options, void *seedOut, void *, void *, void *);
	IOReturn trapRelease(void *id, void *, void *, void *, void *, void *);
};

OSDefineMetaClassAndStructors(IOSurfaceRootUserClient, IOUserClient);

static uint64_t sweet_surf_logged;      /* selectors / traps already logged (< 64) */

static void
sweet_surf_log_once(const char *what, uint32_t n)
{
	uint32_t bit = (what[0] == 't' ? 48 : 0) + n;
	if (bit >= 64 || (sweet_surf_logged & (1ULL << bit))) {
		return;
	}
	sweet_surf_logged |= 1ULL << bit;
	char name[32];
	proc_selfname(name, sizeof(name));
	IOLog("SWEET SURF: %s %s %u\n", name, what, n);
}

bool
IOSurfaceRootUserClient::initWithTask(task_t owningTask, void *securityID, UInt32 type)
{
	if (!IOUserClient::initWithTask(owningTask, securityID, type)) {
		return false;
	}
	fTask = owningTask;
	fLock = IOLockAlloc();
	fRefs = OSArray::withCapacity(16);
	fEventMaps = OSArray::withCapacity(8);
	bzero(fEventAsyncRef, sizeof(fEventAsyncRef));
	fHasEventAsyncRef = false;
	return fLock != NULL && fRefs != NULL && fEventMaps != NULL;
}

void
IOSurfaceRootUserClient::free()
{
	OSSafeReleaseNULL(fRefs);
	OSSafeReleaseNULL(fEventMaps);
	if (fLock != NULL) {
		IOLockFree(fLock);
		fLock = NULL;
	}
	IOUserClient::free();
}

IOReturn
IOSurfaceRootUserClient::clientClose(void)
{
	IOLockLock(fLock);
	fRefs->flushCollection();
	if (fEventMaps != NULL) {
		fEventMaps->flushCollection();
	}
	fHasEventAsyncRef = false;
	IOLockUnlock(fLock);
	terminate();
	return kIOReturnSuccess;
}

/* Lock held. */
SweetSurfaceRef *
IOSurfaceRootUserClient::findRef(uint32_t id, unsigned int *index)
{
	for (unsigned int i = 0; i < fRefs->getCount(); i++) {
		SweetSurfaceRef *ref = (SweetSurfaceRef *)fRefs->getObject(i);
		if (ref->surface->id == id) {
			if (index != NULL) {
				*index = i;
			}
			return ref;
		}
	}
	return NULL;
}

/*
 * Count one more create/lookup of s by this client, mapping it into the
 * task the first time. Takes over the caller's user count on s.
 *
 * A lookup of a surface this client already has maps nothing and counts
 * nothing (*existing): IOSurface.framework then gets base address -1 in the
 * lock result and reuses its cached IOSurfaceClient. Counting it instead
 * made the framework build a second client for the same id; releasing that
 * one (traps 4 + 5) unmapped the memory under the first (backboardd
 * snapshot crash in CGDataProviderCreateWithData).
 */
IOReturn
IOSurfaceRootUserClient::addRef(SweetSurface *s, SweetSurfaceRef **refOut, bool *existing)
{
	*existing = false;
	IOLockLock(fLock);
	SweetSurfaceRef *ref = findRef(s->id, NULL);
	if (ref != NULL && ref->surface == s) {
		IOLockUnlock(fLock);
		sweet_surface_put(s);
		*refOut = ref;
		*existing = true;
		return kIOReturnSuccess;
	}
	IOLockUnlock(fLock);

	ref = OSTypeAlloc(SweetSurfaceRef);
	if (ref == NULL || !ref->init()) {
		OSSafeReleaseNULL(ref);
		sweet_surface_put(s);
		return kIOReturnNoMemory;
	}
	s->retain();
	ref->surface = s;
	ref->count = 1;
	ref->data = s->memory->createMappingInTask(fTask, 0, kIOMapAnywhere | s->mapCache);
	ref->state = s->state->createMappingInTask(fTask, 0, kIOMapAnywhere | kIOMapReadOnly);
	if (ref->data == NULL || ref->state == NULL) {
		ref->release();
		return kIOReturnVMError;
	}
	IOLockLock(fLock);
	SweetSurfaceRef *raced = findRef(s->id, NULL);
	if (raced != NULL && raced->surface == s) {
		/* Another thread mapped it meanwhile: count there, drop ours. */
		raced->count++;
		IOLockUnlock(fLock);
		ref->release();
		*refOut = raced;
		return kIOReturnSuccess;
	}
	fRefs->setObject(ref);
	IOLockUnlock(fLock);
	ref->release();         /* fRefs holds it */
	*refOut = ref;
	return kIOReturnSuccess;
}

static void
put32(uint8_t *p, uint32_t off, uint32_t v)
{
	memcpy(p + off, &v, 4);
}

static void
put64(uint8_t *p, uint32_t off, uint64_t v)
{
	memcpy(p + off, &v, 8);
}

/* IOSurface.framework's lock result (copied to IOSurfaceClient + 0x68). */
static void
sweet_surface_fill_result(SweetSurface *s, SweetSurfaceRef *ref, uint8_t *out)
{
	bzero(out, SWEET_SURFACE_RESULT_SIZE);
	put64(out, 0x00, ref->data->getAddress());
	put64(out, 0x08, ref->state->getAddress());
	put64(out, 0x10, ref->state->getAddress() + SWEET_SURFACE_TIMESTAMPS);
	put32(out, 0x18, s->id);
	put32(out, 0x1c, s->allocSize);
	put32(out, 0x20, s->width);
	put32(out, 0x24, s->height);
	put32(out, 0x28, s->bytesPerRow);
	put32(out, 0x2c, s->offset);
	put32(out, 0x30, s->pixelFormat);
	put32(out, 0x34, s->allocSize);
	put32(out, 0x38, s->planeCount);
	memcpy(out + 0x40, &s->bytesPerElement, 2);
	out[0x42] = s->elementWidth;
	out[0x43] = s->elementHeight;
	put32(out, 0x44, s->cacheMode);
	put32(out, 0x50, s->parentID);
	put32(out, 0x54, 1);
	for (uint32_t i = 0; i < s->planeCount; i++) {
		uint8_t *p = out + 0x68 + i * 0x80;
		const struct sweet_surface_plane *pl = &s->planes[i];
		put32(p, 0x00, pl->width);
		put32(p, 0x04, pl->height);
		put32(p, 0x08, pl->bytesPerRow);
		put32(p, 0x0c, pl->offset);
		put32(p, 0x14, pl->size);
		memcpy(p + 0x18, &pl->bytesPerElement, 2);
		p[0x1a] = pl->elementWidth;
		p[0x1b] = pl->elementHeight;
	}
}

static IOReturn
sweet_struct_out(IOExternalMethodArguments *args, const void *buf, uint32_t len)
{
	if (args->structureOutputDescriptor != NULL) {
		if (len > args->structureOutputDescriptor->getLength()) {
			return kIOReturnNoSpace;
		}
		args->structureOutputDescriptor->writeBytes(0, buf, len);
		args->structureOutputDescriptorSize = len;
		return kIOReturnSuccess;
	}
	if (args->structureOutput == NULL || len > args->structureOutputSize) {
		return kIOReturnNoSpace;
	}
	memcpy(args->structureOutput, buf, len);
	args->structureOutputSize = len;
	return kIOReturnSuccess;
}

/* Map s for this client and answer with its lock result. Takes s's user count. */
IOReturn
IOSurfaceRootUserClient::returnSurface(SweetSurface *s, IOExternalMethodArguments *args)
{
	SweetSurfaceRef *ref = NULL;
	bool existing = false;
	IOReturn ret = addRef(s, &ref, &existing);
	if (ret != kIOReturnSuccess) {
		return ret;
	}
	uint8_t *out = (uint8_t *)IOMallocZeroData(SWEET_SURFACE_RESULT_SIZE);
	if (out == NULL) {
		ret = kIOReturnNoMemory;
	} else {
		sweet_surface_fill_result(s, ref, out);
		if (existing) {
			put64(out, 0x00, ~0ULL);        /* "already yours": use the cached client */
		}
		ret = sweet_struct_out(args, out, SWEET_SURFACE_RESULT_SIZE);
		IOFreeData(out, SWEET_SURFACE_RESULT_SIZE);
	}
	if (ret != kIOReturnSuccess && !existing) {
		/* The caller never learns the id: undo this create/lookup. */
		trapRelease((void *)(uintptr_t)s->id, NULL, NULL, NULL, NULL, NULL);
	}
	return ret;
}

IOReturn
IOSurfaceRootUserClient::create(IOExternalMethodArguments *args)
{
	const char *buf = (const char *)args->structureInput;
	uint32_t len = args->structureInputSize;
	char *copy = NULL;
	IOReturn ret;

	if (args->structureInputDescriptor != NULL) {
		len = (uint32_t)args->structureInputDescriptor->getLength();
		if (len == 0 || len > SWEET_SURFACE_MAX_PROPS) {
			return kIOReturnBadArgument;
		}
		copy = (char *)IOMallocData(len);
		if (copy == NULL) {
			return kIOReturnNoMemory;
		}
		args->structureInputDescriptor->readBytes(0, copy, len);
		buf = copy;
	}
	OSObject *obj = (buf != NULL && len > 0) ? OSUnserializeXML(buf, len) : NULL;
	if (copy != NULL) {
		IOFreeData(copy, len);
	}
	OSDictionary *props = OSDynamicCast(OSDictionary, obj);
	if (props == NULL) {
		OSSafeReleaseNULL(obj);
		IOLog("SWEET SURF: create: bad property dictionary (%u bytes)\n", len);
		return kIOReturnBadArgument;
	}

	SweetSurface *s = sweet_surface_alloc();
	if (s == NULL) {
		props->release();
		return kIOReturnNoMemory;
	}
	s->parentID = args->scalarInputCount > 0 ? (uint32_t)args->scalarInput[0] : 0;
	ret = sweet_surface_layout(s, props);
	if (ret == kIOReturnSuccess) {
		IOBufferMemoryDescriptor *mem = NULL;
		if ((uint64_t)OSAddAtomic64(s->allocSize, (volatile SInt64 *)&sweet_surface_wired) +
		    s->allocSize <= SWEET_SURFACE_WIRED_MAX) {
			mem = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,
			    kIODirectionInOut | kIOMemoryKernelUserShared, s->allocSize, page_size);
		}
		if (mem == NULL) {
			OSAddAtomic64(-(SInt64)s->allocSize, (volatile SInt64 *)&sweet_surface_wired);
			ret = kIOReturnNoMemory;
		} else {
			bzero(mem->getBytesNoCopy(), s->allocSize);
			s->memory = mem;
			s->mapCache = kIOMapDefaultCache;
			if (!sweet_surface_publish(s)) {
				ret = kIOReturnNoResources;
			}
		}
	}
	{
		char name[32];
		proc_selfname(name, sizeof(name));
		if (ret == kIOReturnSuccess && strncmp(name, "SpringBoard", 11) == 0) {
			g_xzs_springboard_surface_ready = true;
		}
		if (ret != kIOReturnSuccess || sweet_surface_creates_logged < 64) {
			sweet_surface_creates_logged++;
			IOLog("SWEET SURF: %s create id %u %ux%u fmt 0x%x bpe %u bpr %u planes %u size %u keys %u -> 0x%x\n",
			    name, s->id, s->width, s->height, s->pixelFormat, s->bytesPerElement,
			    s->bytesPerRow, s->planeCount, s->allocSize, props->getCount(), ret);
		}
	}
	props->release();
	if (ret != kIOReturnSuccess) {
		s->release();
		return ret;
	}
	ret = returnSurface(s, args);   /* takes the creator's user count */
	s->release();
	return ret;
}

static void
sweet_surface_update_value_seed_locked(SweetSurface *s)
{
	s->valueSeed = (s->valueSeed + 2) | 1u;
	uint32_t approxBytes = 256;
	OSSerialize *ser = OSSerialize::binaryWithCapacity(256);
	if (ser != NULL) {
		if (s->values != NULL && s->values->serialize(ser)) {
			approxBytes = ser->getLength() + 64;
			if (approxBytes < 256) {
				approxBytes = 256;
			}
		}
		ser->release();
	}
	uint8_t *stateBytes = (uint8_t *)s->state->getBytesNoCopy();
	*(uint32_t *)(stateBytes + 0x20) = s->valueSeed;
	*(uint32_t *)(stateBytes + 0x28) = approxBytes;
}

IOReturn
IOSurfaceRootUserClient::setValue(IOExternalMethodArguments *args)
{
	const char *buf = (const char *)args->structureInput;
	uint32_t len = args->structureInputSize;
	char *copy = NULL;

	if (args->structureInputDescriptor != NULL) {
		len = (uint32_t)args->structureInputDescriptor->getLength();
		if (len <= 8 || len > SWEET_SURFACE_MAX_PROPS) {
			return kIOReturnBadArgument;
		}
		copy = (char *)IOMallocData(len);
		if (copy == NULL) {
			return kIOReturnNoMemory;
		}
		args->structureInputDescriptor->readBytes(0, copy, len);
		buf = copy;
	}
	if (buf == NULL || len <= 8) {
		if (copy != NULL) {
			IOFreeData(copy, len);
		}
		return kIOReturnBadArgument;
	}

	uint32_t surfaceID = 0;
	memcpy(&surfaceID, buf, 4);
	OSObject *obj = OSUnserializeXML(buf + 8, len - 8);
	if (copy != NULL) {
		IOFreeData(copy, len);
	}
	if (obj == NULL) {
		return kIOReturnBadArgument;
	}

	SweetSurface *s = sweet_surface_copy(surfaceID);
	if (s == NULL) {
		obj->release();
		return kIOReturnNotFound;
	}

	uint32_t newSeed = 1;
	IOLockLock(sweet_surface_lock);
	if (s->values != NULL) {
		OSArray *arr = OSDynamicCast(OSArray, obj);
		OSDictionary *dict = OSDynamicCast(OSDictionary, obj);
		if (arr != NULL) {
			if (arr->getCount() == 1) {
				OSDictionary *d = OSDynamicCast(OSDictionary, arr->getObject(0));
				if (d != NULL) {
					s->values->merge(d);
				}
			} else {
				for (unsigned int i = 0; i + 1 < arr->getCount(); i += 2) {
					OSObject *val = arr->getObject(i);
					OSString *key = OSDynamicCast(OSString, arr->getObject(i + 1));
					if (key != NULL && val != NULL) {
						s->values->setObject(key, val);
					}
				}
			}
		} else if (dict != NULL) {
			s->values->merge(dict);
		}
		sweet_surface_update_value_seed_locked(s);
		newSeed = s->valueSeed;
	}
	IOLockUnlock(sweet_surface_lock);
	obj->release();
	s->release();

	if (args->structureOutput != NULL && args->structureOutputSize >= 4) {
		memcpy(args->structureOutput, &newSeed, 4);
		args->structureOutputSize = 4;
	}
	return kIOReturnSuccess;
}

IOReturn
IOSurfaceRootUserClient::copyValue(IOExternalMethodArguments *args)
{
	const char *buf = (const char *)args->structureInput;
	uint32_t len = args->structureInputSize;
	char *copy = NULL;

	if (args->structureInputDescriptor != NULL) {
		len = (uint32_t)args->structureInputDescriptor->getLength();
		if (len < 9 || len > SWEET_SURFACE_MAX_PROPS) {
			return kIOReturnBadArgument;
		}
		copy = (char *)IOMallocData(len);
		if (copy == NULL) {
			return kIOReturnNoMemory;
		}
		args->structureInputDescriptor->readBytes(0, copy, len);
		buf = copy;
	}
	if (buf == NULL || len < 9) {
		if (copy != NULL) {
			IOFreeData(copy, len);
		}
		return kIOReturnBadArgument;
	}

	uint32_t surfaceID = 0;
	memcpy(&surfaceID, buf, 4);
	char keyBuf[128];
	size_t keyMax = len - 8 < sizeof(keyBuf) - 1 ? len - 8 : sizeof(keyBuf) - 1;
	memcpy(keyBuf, buf + 8, keyMax);
	keyBuf[keyMax] = '\0';
	if (copy != NULL) {
		IOFreeData(copy, len);
	}

	SweetSurface *s = sweet_surface_copy(surfaceID);
	if (s == NULL) {
		return kIOReturnNotFound;
	}

	OSSerialize *ser = OSSerialize::binaryWithCapacity(256);
	if (ser == NULL) {
		s->release();
		return kIOReturnNoMemory;
	}

	bool ok = false;
	uint32_t curSeed = 1;
	IOLockLock(sweet_surface_lock);
	curSeed = s->valueSeed;
	if (s->values != NULL) {
		if (keyBuf[0] == '\0') {
			ok = s->values->serialize(ser);
		} else {
			OSObject *val = s->values->getObject(keyBuf);
			if (val != NULL) {
				ok = val->serialize(ser);
			}
		}
	}
	IOLockUnlock(sweet_surface_lock);
	s->release();

	if (!ok) {
		ser->release();
		if (args->structureOutput != NULL) {
			args->structureOutputSize = 0;
		}
		return kIOReturnSuccess;
	}

	uint32_t serLen = ser->getLength();
	uint32_t totalLen = 8 + serLen;
	uint8_t *outBuf = (uint8_t *)IOMallocZeroData(totalLen);
	if (outBuf == NULL) {
		ser->release();
		return kIOReturnNoMemory;
	}
	put32(outBuf, 0, curSeed);
	put32(outBuf, 4, 0);
	memcpy(outBuf + 8, ser->text(), serLen);
	ser->release();

	IOReturn ret = sweet_struct_out(args, outBuf, totalLen);
	IOFreeData(outBuf, totalLen);
	if (ret == kIOReturnNoSpace) {
		return (IOReturn)0xe00002db; /* kIOReturnMessageTooLarge */
	}
	return ret;
}

IOReturn
IOSurfaceRootUserClient::removeValue(IOExternalMethodArguments *args)
{
	const char *buf = (const char *)args->structureInput;
	uint32_t len = args->structureInputSize;
	if (buf == NULL || len < 9) {
		return kIOReturnBadArgument;
	}
	uint32_t surfaceID = 0;
	memcpy(&surfaceID, buf, 4);
	char keyBuf[128];
	size_t keyMax = len - 8 < sizeof(keyBuf) - 1 ? len - 8 : sizeof(keyBuf) - 1;
	memcpy(keyBuf, buf + 8, keyMax);
	keyBuf[keyMax] = '\0';

	SweetSurface *s = sweet_surface_copy(surfaceID);
	if (s == NULL) {
		return kIOReturnNotFound;
	}

	uint32_t newSeed = 1;
	IOLockLock(sweet_surface_lock);
	if (s->values != NULL) {
		if (keyBuf[0] == '\0') {
			s->values->flushCollection();
		} else {
			s->values->removeObject(keyBuf);
		}
		sweet_surface_update_value_seed_locked(s);
		newSeed = s->valueSeed;
	}
	IOLockUnlock(sweet_surface_lock);
	s->release();

	if (args->structureOutput != NULL && args->structureOutputSize >= 4) {
		memcpy(args->structureOutput, &newSeed, 4);
		args->structureOutputSize = 4;
	}
	return kIOReturnSuccess;
}

IOReturn
IOSurfaceRootUserClient::setBulkAttachments(IOExternalMethodArguments *args)
{
	const uint8_t *buf = (const uint8_t *)args->structureInput;
	uint32_t len = args->structureInputSize;
	if (buf == NULL || len < 0x84) {
		return kIOReturnSuccess;
	}
	uint32_t surfaceID = 0;
	memcpy(&surfaceID, buf + 0x80, 4);
	SweetSurface *s = sweet_surface_copy(surfaceID);
	if (s == NULL) {
		return kIOReturnNotFound;
	}
	IOLockLock(sweet_surface_lock);
	if (s->state != NULL) {
		uint8_t *stateBytes = (uint8_t *)s->state->getBytesNoCopy();
		memcpy(stateBytes + 0x30, buf, 0x70);
	}
	IOLockUnlock(sweet_surface_lock);
	s->release();
	return kIOReturnSuccess;
}

IOReturn
IOSurfaceRootUserClient::externalMethod(uint32_t selector, IOExternalMethodArguments *args,
    __unused IOExternalMethodDispatch *dispatch, __unused OSObject *target,
    __unused void *reference)
{
	uint64_t arg0 = args->scalarInputCount > 0 ? args->scalarInput[0] : 0;

	switch (selector) {
	case kSweetSurfaceLimits: {
		uint8_t limits[0x18] = { 0 };
		put32(limits, 0x0, SWEET_SURFACE_ROW_ALIGN - 1);
		put32(limits, 0x4, SWEET_SURFACE_ROW_ALIGN - 1);
		limits[0x16] = 1;
		sweet_surf_log_once("sel", selector);
		return sweet_struct_out(args, limits, sizeof(limits));
	}
	case kSweetSurfaceCreate:
		return create(args);
	case kSweetSurfaceLookup: {
		SweetSurface *s = sweet_surface_copy((uint32_t)arg0);
		IOReturn ret = kIOReturnNotFound;
		if (s != NULL) {
			ret = sweet_surface_get(s) ? returnSurface(s, args) : kIOReturnNotFound;
			s->release();
		}
		sweet_surf_log_once("sel", selector);
		return ret;
	}
	case kSweetSurfaceSetYCbCrMatrix: {
		SweetSurface *s = sweet_surface_copy((uint32_t)arg0);
		if (s != NULL) {
			s->ycbcrMatrix = args->scalarInputCount > 1 ? (uint32_t)args->scalarInput[1] : 0;
			if (s->state != NULL) {
				*(uint32_t *)((uint8_t *)s->state->getBytesNoCopy() + 0x24) = s->ycbcrMatrix;
			}
			s->release();
		}
		sweet_surf_log_once("sel", selector);
		return kIOReturnSuccess;
	}
	case kSweetSurfaceSetValue:
		sweet_surf_log_once("sel", selector);
		return setValue(args);
	case kSweetSurfaceCopyValue:
		sweet_surf_log_once("sel", selector);
		return copyValue(args);
	case kSweetSurfaceRemoveValue:
		sweet_surf_log_once("sel", selector);
		return removeValue(args);
	case kSweetSurfaceSetBulkAttachments:
		sweet_surf_log_once("sel", selector);
		return setBulkAttachments(args);
	case kSweetSurfaceLookupFromPort: {
		OSObject *obj = NULL;
		IOReturn ret = IOUserClient::copyObjectForPortNameInTask(fTask, (mach_port_name_t)arg0, &obj);
		SweetSurface *s = OSDynamicCast(SweetSurface, obj);
		if (ret == kIOReturnSuccess) {
			ret = (s != NULL && sweet_surface_get(s)) ? returnSurface(s, args) : kIOReturnBadArgument;
		}
		OSSafeReleaseNULL(obj);
		sweet_surf_log_once("sel", selector);
		return ret;
	}
	case kSweetSurfaceCreatePort: {
		SweetSurface *s = sweet_surface_copy((uint32_t)arg0);
		if (s == NULL || args->scalarOutputCount < 1) {
			OSSafeReleaseNULL(s);
			return kIOReturnNotFound;
		}
		args->scalarOutput[0] = sweet_surface_port_in_task(s, fTask);
		args->scalarOutputCount = 1;
		s->release();
		sweet_surf_log_once("sel", selector);
		return kIOReturnSuccess;
	}
	case kSweetSurfaceCreateSharedEvent: {
		SweetSharedEvent *ev = sweet_shared_event_alloc();
		if (ev == NULL) {
			return kIOReturnNoMemory;
		}
		IOMemoryMap *map = ev->sharedMem->createMappingInTask(fTask, 0, kIOMapAnywhere | kIOMapDefaultCache);
		mach_port_name_t portName = 0;
		IOReturn ret = IOUserClient::copyPortNameForObjectInTask(fTask, ev, &portName);
		if (map == NULL || ret != kIOReturnSuccess || portName == 0) {
			OSSafeReleaseNULL(map);
			ev->release();
			return kIOReturnVMError;
		}
		IOLockLock(fLock);
		if (fEventMaps != NULL) {
			fEventMaps->setObject(map);
		}
		IOLockUnlock(fLock);
		if (args->scalarOutput != NULL) {
			if (args->scalarOutputCount >= 1) args->scalarOutput[0] = portName;
			if (args->scalarOutputCount >= 2) args->scalarOutput[1] = map->getAddress();
			if (args->scalarOutputCount >= 3) args->scalarOutput[2] = ev->eventID;
		}
		map->release();
		ev->release();
		xzs_gpu_record_event();
		sweet_surf_log_once("sel", selector);
		return kIOReturnSuccess;
	}
	case kSweetSurfaceOpenSharedEvent: {
		OSObject *obj = NULL;
		IOReturn ret = IOUserClient::copyObjectForPortNameInTask(fTask, (mach_port_name_t)arg0, &obj);
		SweetSharedEvent *ev = OSDynamicCast(SweetSharedEvent, obj);
		if (ret != kIOReturnSuccess || ev == NULL) {
			OSSafeReleaseNULL(obj);
			return kIOReturnBadArgument;
		}
		IOMemoryMap *map = ev->sharedMem->createMappingInTask(fTask, 0, kIOMapAnywhere | kIOMapDefaultCache);
		if (map == NULL) {
			obj->release();
			return kIOReturnVMError;
		}
		IOLockLock(fLock);
		if (fEventMaps != NULL) {
			fEventMaps->setObject(map);
		}
		IOLockUnlock(fLock);
		if (args->scalarOutput != NULL) {
			if (args->scalarOutputCount >= 1) args->scalarOutput[0] = 0;
			if (args->scalarOutputCount >= 2) args->scalarOutput[1] = map->getAddress();
			if (args->scalarOutputCount >= 3) args->scalarOutput[2] = ev->eventID;
		}
		map->release();
		obj->release();
		xzs_gpu_record_event();
		sweet_surf_log_once("sel", selector);
		return kIOReturnSuccess;
	}
	case kSweetSurfaceSignalSharedEvent: {
		uint64_t val = args->scalarInputCount > 1 ? args->scalarInput[1] : 0;
		OSObject *obj = NULL;
		if (IOUserClient::copyObjectForPortNameInTask(fTask, (mach_port_name_t)arg0, &obj) == kIOReturnSuccess) {
			SweetSharedEvent *ev = OSDynamicCast(SweetSharedEvent, obj);
			if (ev != NULL && ev->valuePtr() != NULL) {
				*ev->valuePtr() = val;
			}
			obj->release();
		}
		xzs_gpu_record_event();
		sweet_surf_log_once("sel", selector);
		return kIOReturnSuccess;
	}
	case kSweetSurfaceAddEventListener: {
		if (args->asyncWakePort != MACH_PORT_NULL && args->asyncReferenceCount >= 3) {
			IOLockLock(fLock);
			setAsyncReference64(fEventAsyncRef, args->asyncWakePort,
			    args->asyncReference[kIOAsyncCalloutFuncIndex],
			    args->asyncReference[kIOAsyncCalloutRefconIndex]);
			fHasEventAsyncRef = true;
			IOLockUnlock(fLock);
		}
		sweet_surf_log_once("sel", selector);
		return kIOReturnSuccess;
	}
	case kSweetSurfaceRemoveEventListener: {
		IOLockLock(fLock);
		fHasEventAsyncRef = false;
		bzero(fEventAsyncRef, sizeof(fEventAsyncRef));
		IOLockUnlock(fLock);
		sweet_surf_log_once("sel", selector);
		return kIOReturnSuccess;
	}
	case kSweetSurfaceNotifySharedEvent: {
		/*
		 * scalarInput[0] = eventPort
		 * scalarInput[1] = listenerSelf
		 * scalarInput[2] = eventSelf (CFRetained by caller)
		 * scalarInput[3] = blockPtr (_Block_copied by caller)
		 * scalarInput[4] = targetValue
		 */
		uint64_t eventSelf   = args->scalarInputCount > 2 ? args->scalarInput[2] : 0;
		uint64_t blockPtr    = args->scalarInputCount > 3 ? args->scalarInput[3] : 0;
		uint64_t targetValue = args->scalarInputCount > 4 ? args->scalarInput[4] : 0;
		uint64_t finalValue  = targetValue;

		OSObject *obj = NULL;
		if (IOUserClient::copyObjectForPortNameInTask(fTask, (mach_port_name_t)arg0, &obj) == kIOReturnSuccess) {
			SweetSharedEvent *ev = OSDynamicCast(SweetSharedEvent, obj);
			if (ev != NULL && ev->valuePtr() != NULL) {
				uint64_t cur = *ev->valuePtr();
				if (cur < targetValue) {
					*ev->valuePtr() = targetValue;
				} else {
					finalValue = cur;
				}
			}
			obj->release();
		}

		OSAsyncReference64 asyncRefCopy;
		bool hasAsync = false;
		IOLockLock(fLock);
		if (fHasEventAsyncRef) {
			memcpy(asyncRefCopy, fEventAsyncRef, sizeof(asyncRefCopy));
			hasAsync = true;
		}
		IOLockUnlock(fLock);

		if (hasAsync && blockPtr != 0) {
			io_user_reference_t notifyArgs[3] = {
				(io_user_reference_t)eventSelf,
				(io_user_reference_t)blockPtr,
				(io_user_reference_t)finalValue
			};
			sendAsyncResult64(asyncRefCopy, kIOReturnSuccess, notifyArgs, 3);
		}
		xzs_gpu_record_event();
		sweet_surf_log_once("sel", selector);
		return kIOReturnSuccess;
	}
	case kSweetSurfaceWaitSharedEvent: {
		uint64_t targetValue = args->scalarInputCount > 1 ? args->scalarInput[1] : 0;
		OSObject *obj = NULL;
		if (IOUserClient::copyObjectForPortNameInTask(fTask, (mach_port_name_t)arg0, &obj) == kIOReturnSuccess) {
			SweetSharedEvent *ev = OSDynamicCast(SweetSharedEvent, obj);
			if (ev != NULL && ev->valuePtr() != NULL && *ev->valuePtr() < targetValue) {
				*ev->valuePtr() = targetValue;
			}
			obj->release();
		}
		xzs_gpu_record_event();
		sweet_surf_log_once("sel", selector);
		return kIOReturnSuccess;
	}
	default:
		/* Remaining notifications, ownership, timestamps...: accept and ignore. */
		for (uint32_t i = 0; i < args->scalarOutputCount; i++) {
			args->scalarOutput[i] = 0;
		}
		if (args->structureOutput != NULL) {
			bzero(args->structureOutput, args->structureOutputSize);
		}
		sweet_surf_log_once("sel", selector);
		return kIOReturnSuccess;
	}
}

IOReturn
IOSurfaceRootUserClient::trapUseCount(__unused void *id, __unused void *category,
    void *, void *, void *, void *)
{
	return kIOReturnSuccess;
}

static IOReturn
sweet_surface_seed_out(SweetSurface *s, void *seedOut, bool bump)
{
	uint32_t seed = bump ? OSIncrementAtomic((volatile SInt32 *)sweet_surface_seed(s)) + 1 :
	    *sweet_surface_seed(s);
	if (seedOut != NULL && copyout(&seed, (user_addr_t)seedOut, sizeof(seed)) != 0) {
		return kIOReturnBadArgument;
	}
	return kIOReturnSuccess;
}

/* Retained surface this client created or looked up, or NULL. */
static SweetSurface *
sweet_surface_held(IOLock *lock, OSArray *refs, uint32_t id)
{
	SweetSurface *s = NULL;
	IOLockLock(lock);
	for (unsigned int i = 0; i < refs->getCount(); i++) {
		SweetSurfaceRef *ref = (SweetSurfaceRef *)refs->getObject(i);
		if (ref->surface->id == id) {
			s = ref->surface;
			s->retain();
			break;
		}
	}
	IOLockUnlock(lock);
	return s;
}

IOReturn
IOSurfaceRootUserClient::trapLock(void *id, __unused void *options, void *seedOut,
    void *, void *, void *)
{
	SweetSurface *s = sweet_surface_held(fLock, fRefs, (uint32_t)(uintptr_t)id);
	if (s == NULL) {
		return kIOReturnNotFound;
	}
	IOReturn ret = sweet_surface_seed_out(s, seedOut, false);
	s->release();
	return ret;
}

IOReturn
IOSurfaceRootUserClient::trapUnlock(void *id, void *options, void *seedOut,
    void *, void *, void *)
{
	SweetSurface *s = sweet_surface_held(fLock, fRefs, (uint32_t)(uintptr_t)id);
	if (s == NULL) {
		return kIOReturnNotFound;
	}
	IOReturn ret = sweet_surface_seed_out(s, seedOut,
	    !((uintptr_t)options & kIOSurfaceLockReadOnly));
	s->release();
	return ret;
}

IOReturn
IOSurfaceRootUserClient::trapRelease(void *id, void *, void *, void *, void *, void *)
{
	unsigned int index;
	IOLockLock(fLock);
	SweetSurfaceRef *ref = findRef((uint32_t)(uintptr_t)id, &index);
	if (ref != NULL && --ref->count == 0) {
		fRefs->removeObject(index);     /* unmaps and drops the user count */
	}
	IOLockUnlock(fLock);
	return ref != NULL ? kIOReturnSuccess : kIOReturnNotFound;
}

static IOExternalTrap sweet_surface_traps[] = {
	[kSweetSurfaceTrapIncrementUse]  = { NULL, (IOTrap) & IOSurfaceRootUserClient::trapUseCount },
	[kSweetSurfaceTrapDecrementUse]  = { NULL, (IOTrap) & IOSurfaceRootUserClient::trapUseCount },
	[kSweetSurfaceTrapLock]          = { NULL, (IOTrap) & IOSurfaceRootUserClient::trapLock },
	[kSweetSurfaceTrapUnlock]        = { NULL, (IOTrap) & IOSurfaceRootUserClient::trapUnlock },
	[kSweetSurfaceTrapRelease]       = { NULL, (IOTrap) & IOSurfaceRootUserClient::trapRelease },
	[kSweetSurfaceTrapReleaseFailed] = { NULL, (IOTrap) & IOSurfaceRootUserClient::trapRelease },
};

IOExternalTrap *
IOSurfaceRootUserClient::getTargetAndTrapForIndex(IOService **targetP, UInt32 index)
{
	if (index >= sizeof(sweet_surface_traps) / sizeof(sweet_surface_traps[0])) {
		sweet_surf_log_once("trap", index);
		return NULL;
	}
	sweet_surf_log_once("trap", index);
	*targetP = this;
	return &sweet_surface_traps[index];
}
#endif /* SWEET || XZS */
