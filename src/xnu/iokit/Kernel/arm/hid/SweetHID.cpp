/*
 * SWEET: HID event services for the iOS userland (backboardd's HID event
 * system).
 *
 * backboardd watches for IOService objects with HIDServiceSupport = true,
 * opens them with IOServiceOpen type 'esuc' (IOHIDEventServiceUserClient,
 * compiled here from IOHIDFamily-1915.140.3, the version the iOS 16.7
 * userland ships) and reads serialized IOHIDEvents from the user client's
 * shared queue. This file has the minimal IOHIDEventService base class and
 * two services:
 *
 *   SweetHIDButtons  consumer-control keys (power, volume, home)
 *   SweetHIDTouch    the built-in touch screen (digitizer collections)
 *
 * Hardware drivers feed them through the C entry points sweet_hid_key()
 * and sweet_hid_touch(). Boot-arg sweethidtest=N (seconds) presses Home
 * every N seconds, to test the event path without input hardware.
 */
#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <IOKit/IOTimerEventSource.h>
#include <libkern/c++/OSArray.h>
#include <pexpert/pexpert.h>

#include "IOHIDEventService.h"
#include "IOHIDEventServiceUserClient.h"
#include "IOHIDEvent.h"
#include "IOHIDKeys.h"
#include "IOHIDPrivateKeys.h"
#include "IOHIDUsageTables.h"

extern "C" uint64_t mach_absolute_time(void);

/* One open client: the user client and its event callback. */
class IOHIDEventServiceClient : public OSObject {
	OSDeclareDefaultStructors(IOHIDEventServiceClient);
public:
	bool                       initClient() { return OSObject::init(); }
	IOService                 *client;
	void                      *context;
	IOHIDEventService::Action  action;
};
OSDefineMetaClassAndStructors(IOHIDEventServiceClient, OSObject);

#define super IOService
OSDefineMetaClassAndStructors(IOHIDEventService, IOService);

bool
IOHIDEventService::start(IOService *provider)
{
	if (!super::start(provider)) {
		return false;
	}
	_clientLock = IOLockAlloc();
	_clients = OSArray::withCapacity(2);
	_hidWorkLoop = getWorkLoop();
	if (!_clientLock || !_clients || !_hidWorkLoop) {
		return false;
	}
	_hidWorkLoop->retain();
	_hidGate = IOCommandGate::commandGate(this);
	if (!_hidGate || _hidWorkLoop->addEventSource(_hidGate) != kIOReturnSuccess) {
		return false;
	}

	OSString *transport = getTransport();
	OSString *product = getProduct();
	setProperty(kIOHIDTransportKey, transport);
	setProperty(kIOHIDProductKey, product);
	setProperty(kIOHIDManufacturerKey, "Sweet");
	setProperty(kIOHIDVendorIDKey, getVendorID(), 32);
	setProperty(kIOHIDProductIDKey, getProductID(), 32);
	setProperty(kIOHIDLocationIDKey, (unsigned long long)getProductID(), 32);
	setProperty(kIOHIDReportIntervalKey, getReportInterval(), 32);
	setProperty(kIOHIDPrimaryUsagePageKey, getPrimaryUsagePage(), 32);
	setProperty(kIOHIDPrimaryUsageKey, getPrimaryUsage(), 32);
	setProperty(kIOHIDBuiltInKey, kOSBooleanTrue);
	OSSafeReleaseNULL(transport);
	OSSafeReleaseNULL(product);

	OSDictionary *pair = OSDictionary::withCapacity(2);
	OSArray *pairs = OSArray::withCapacity(1);
	OSNumber *page = OSNumber::withNumber(getPrimaryUsagePage(), 32);
	OSNumber *usage = OSNumber::withNumber(getPrimaryUsage(), 32);
	if (pair && pairs && page && usage) {
		pair->setObject(kIOHIDDeviceUsagePageKey, page);
		pair->setObject(kIOHIDDeviceUsageKey, usage);
		pairs->setObject(pair);
		setProperty(kIOHIDDeviceUsagePairsKey, pairs);
	}
	OSSafeReleaseNULL(pair);
	OSSafeReleaseNULL(pairs);
	OSSafeReleaseNULL(page);
	OSSafeReleaseNULL(usage);

	/* What IOHIDFamily's "IOHIDEventServiceUserClient" personality merges in. */
	setProperty("HIDServiceSupport", kOSBooleanTrue);
	setProperty("IOUserClientClass", "IOHIDEventServiceUserClient");
	return true;
}

void
IOHIDEventService::free(void)
{
	if (_hidGate && _hidWorkLoop) {
		_hidWorkLoop->removeEventSource(_hidGate);
	}
	OSSafeReleaseNULL(_hidGate);
	OSSafeReleaseNULL(_hidWorkLoop);
	OSSafeReleaseNULL(_clients);
	if (_clientLock) {
		IOLockFree(_clientLock);
		_clientLock = NULL;
	}
	super::free();
}

bool
IOHIDEventService::open(IOService *client, IOOptionBits options, void *context, Action action)
{
	IOHIDEventServiceClient *data = OSTypeAlloc(IOHIDEventServiceClient);
	bool ok = false;

	if (data && data->initClient()) {
		data->client = client;
		data->context = context;
		data->action = action;
		ok = super::open(client, options, data);
	}
	OSSafeReleaseNULL(data);
	IOLog("SWEET HID: %s open by %s -> %d\n", getName(), client ? client->getName() : "?", ok);
	return ok;
}

void
IOHIDEventService::close(IOService *forClient, IOOptionBits options)
{
	super::close(forClient, options);
}

bool
IOHIDEventService::handleOpen(IOService *client, IOOptionBits options __unused, void *argument)
{
	IOHIDEventServiceClient *data = OSDynamicCast(IOHIDEventServiceClient, (OSObject *)argument);
	bool ok = false;

	if (!data) {
		return false;
	}
	IOLockLock(_clientLock);
	if (!handleIsOpenLocked(client)) {
		/* Copy on write: dispatchEvent iterates a snapshot without the lock. */
		OSArray *next = OSArray::withArray(_clients, _clients->getCount() + 1);
		if (next && next->setObject(data)) {
			_clients->release();
			_clients = next;
			ok = true;
		} else {
			OSSafeReleaseNULL(next);
		}
	}
	IOLockUnlock(_clientLock);
	return ok;
}

void
IOHIDEventService::handleClose(IOService *client, IOOptionBits options __unused)
{
	IOLockLock(_clientLock);
	OSArray *next = OSArray::withCapacity(_clients->getCount());
	if (next) {
		for (unsigned int i = 0; i < _clients->getCount(); i++) {
			IOHIDEventServiceClient *data = (IOHIDEventServiceClient *)_clients->getObject(i);
			if (data->client != client) {
				next->setObject(data);
			}
		}
		_clients->release();
		_clients = next;
	}
	IOLockUnlock(_clientLock);
}

bool
IOHIDEventService::handleIsOpenLocked(const IOService *client) const
{
	for (unsigned int i = 0; i < _clients->getCount(); i++) {
		IOHIDEventServiceClient *data = (IOHIDEventServiceClient *)_clients->getObject(i);
		if (client == NULL || data->client == client) {
			return true;
		}
	}
	return false;
}

bool
IOHIDEventService::handleIsOpen(const IOService *client) const
{
	IOLockLock(_clientLock);
	bool open = handleIsOpenLocked(client);
	IOLockUnlock(_clientLock);
	return open;
}

void
IOHIDEventService::dispatchEvent(IOHIDEvent *event, IOOptionBits options)
{
	OSArray *clients;

	event->setSenderID(getRegistryEntryID());
	IOLockLock(_clientLock);
	clients = _clients;
	clients->retain();
	IOLockUnlock(_clientLock);

	for (unsigned int i = 0; i < clients->getCount(); i++) {
		IOHIDEventServiceClient *data = (IOHIDEventServiceClient *)clients->getObject(i);
		if (data->action) {
			(*data->action)(data->client, this, data->context, event, options);
		}
	}
	clients->release();
}

IOHIDEvent *
IOHIDEventService::copyEvent(IOHIDEventType type __unused, IOHIDEvent *matching __unused, IOOptionBits options __unused)
{
	return NULL;
}

IOHIDEvent *
IOHIDEventService::copyMatchingEvent(OSDictionary *matching __unused)
{
	return NULL;
}

IOReturn
IOHIDEventService::setElementValue(UInt32 usagePage __unused, UInt32 usage __unused, UInt32 value __unused)
{
	return kIOReturnUnsupported;
}

IOReturn
IOHIDEventService::setProperties(OSObject *properties)
{
	/* The event system pushes its settings here; nothing to configure. */
	return OSDynamicCast(OSDictionary, properties) ? kIOReturnSuccess : kIOReturnBadArgument;
}

UInt32
IOHIDEventService::getPrimaryUsagePage()
{
	return 0;
}

UInt32
IOHIDEventService::getPrimaryUsage()
{
	return 0;
}

OSString *
IOHIDEventService::getTransport()
{
	return OSString::withCString("SPU");
}

OSString *
IOHIDEventService::getProduct()
{
	return OSString::withCString(getName());
}

UInt32
IOHIDEventService::getVendorID()
{
	return 0x05ac;
}

UInt32
IOHIDEventService::getProductID()
{
	return 0;
}

UInt32
IOHIDEventService::getReportInterval()
{
	return 4000;	/* us (250 Hz digitizer report interval) */
}

IOReturn
IOHIDEventService::newUserClient(task_t owningTask, void *securityID, UInt32 type,
    OSDictionary *properties, IOUserClient **handler)
{
	IOUserClient *client;

	if (type != kIOHIDEventServiceUserClientType) {
		IOLog("SWEET HID: %s user client type 0x%x unsupported\n", getName(), (unsigned)type);
		return super::newUserClient(owningTask, securityID, type, properties, handler);
	}
	client = OSTypeAlloc(IOHIDEventServiceUserClient);
	if (!client) {
		return kIOReturnNoMemory;
	}
	if (!client->initWithTask(owningTask, securityID, type)) {
		client->release();
		IOLog("SWEET HID: %s user client init refused\n", getName());
		return kIOReturnBadArgument;
	}
	if (!client->attach(this)) {
		client->release();
		return kIOReturnUnsupported;
	}
	if (!client->start(this)) {
		client->detach(this);
		client->release();
		return kIOReturnUnsupported;
	}
	*handler = client;
	return kIOReturnSuccess;
}

/*
 * Buttons. iOS takes the side/volume/home buttons as consumer-control
 * keyboard events (AppleM68Buttons publishes the same usage pair).
 */
class SweetHIDButtons : public IOHIDEventService {
	OSDeclareDefaultStructors(SweetHIDButtons);
public:
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void key(UInt32 usage, bool down);
protected:
	virtual UInt32 getPrimaryUsagePage() APPLE_KEXT_OVERRIDE { return kHIDPage_Consumer; }
	virtual UInt32 getPrimaryUsage() APPLE_KEXT_OVERRIDE { return kHIDUsage_Csmr_ConsumerControl; }
	virtual UInt32 getProductID() APPLE_KEXT_OVERRIDE { return 1; }
private:
	static void testFired(OSObject *owner, IOTimerEventSource *sender);
	IOTimerEventSource *fTest;
	UInt32 fTestSecs;
	UInt32 fTestCount;
};
OSDefineMetaClassAndStructors(SweetHIDButtons, IOHIDEventService);

/*
 * Touch screen: digitizer collection events with finger children,
 * coordinates normalized to 0..1 like IOHIDEventDriver produces for
 * absolute digitizers.
 */
#define SWEET_TOUCH_FINGERS        10
#define SWEET_TOUCH_EDGE_SIDE_PX   60
#define SWEET_TOUCH_EDGE_TOP_PX    96
#define SWEET_TOUCH_EDGE_BOTTOM_PX 96
class SweetHIDTouch : public IOHIDEventService {
	OSDeclareDefaultStructors(SweetHIDTouch);
public:
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void touch(int finger, bool down, int x, int y);
protected:
	virtual UInt32 getPrimaryUsagePage() APPLE_KEXT_OVERRIDE { return kHIDPage_Digitizer; }
	virtual UInt32 getPrimaryUsage() APPLE_KEXT_OVERRIDE { return kHIDUsage_Dig_TouchScreen; }
	virtual UInt32 getProductID() APPLE_KEXT_OVERRIDE { return 2; }
private:
	IOLock *fLock;
	bool fDown[SWEET_TOUCH_FINGERS];
	bool fEdge[SWEET_TOUCH_FINGERS];
	IOFixed fX[SWEET_TOUCH_FINGERS];
	IOFixed fY[SWEET_TOUCH_FINGERS];
};
OSDefineMetaClassAndStructors(SweetHIDTouch, IOHIDEventService);

static SweetHIDButtons *gSweetButtons;
static SweetHIDTouch *gSweetTouch;
extern "C" {
extern bool g_xzs_gui_presented;
extern bool g_xzs_power_key_used;
extern uint32_t xzs_fb_py_to_ly(uint32_t py);
extern int xzs_spmi_read8(uint8_t sid, uint16_t addr, uint8_t *val);
extern int xzs_spmi_write8(uint8_t sid, uint16_t addr, uint8_t val);
extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
void sweet_hid_key(int key, int down);
void sweet_hid_touch(int finger, int down, int x, int y);
}

static void xzs_keys_start(void);
static void xzs_touch_start(void);

bool
SweetHIDButtons::start(IOService *provider)
{
	if (!IOHIDEventService::start(provider)) {
		return false;
	}
	fTestSecs = 0;
	PE_parse_boot_argn("sweethidtest", &fTestSecs, sizeof(fTestSecs));
	fTest = IOTimerEventSource::timerEventSource(this, &SweetHIDButtons::testFired);
	if (fTest && _hidWorkLoop->addEventSource(fTest) == kIOReturnSuccess) {
		fTest->setTimeoutMS(2000);
	}
	gSweetButtons = this;
	registerService();
	xzs_keys_start();
	IOLog("SWEET HID: buttons registered (auto-unlock enabled, periodic test=%u s)\n", fTestSecs);
	return true;
}

void
SweetHIDButtons::key(UInt32 usage, bool down)
{
	IOHIDEvent *event = IOHIDEvent::keyboardEvent(mach_absolute_time(), kHIDPage_Consumer, usage, down);

	if (event) {
		IOLog("SWEET HID: key 0x%x %s (%u clients)\n", (unsigned)usage, down ? "down" : "up",
		    _clients ? _clients->getCount() : 0);
		dispatchEvent(event);
		event->release();
	}
}

static volatile bool s_real_touch_seen = false;

void
SweetHIDButtons::testFired(OSObject *owner, IOTimerEventSource *sender)
{
	SweetHIDButtons *self = (SweetHIDButtons *)owner;

	if (!g_xzs_gui_presented) {
		sender->setTimeoutMS(2000);
		return;
	}

	if (self->fTestSecs == 0) {
		return;
	}
	UInt32 step = self->fTestCount++;
	bool press = (step & 1) == 0;
	self->key(kHIDUsage_Csmr_Menu, press);
	sender->setTimeoutMS(press ? 100 : self->fTestSecs * 1000);
}

bool
SweetHIDTouch::start(IOService *provider)
{
	fLock = IOLockAlloc();
	if (!fLock || !IOHIDEventService::start(provider)) {
		return false;
	}
	/* The digitizer covers the main display (backboardd maps it 1:1). */
	setProperty("DisplayIntegrated", kOSBooleanTrue);
	setProperty("SurfaceWidth", (unsigned long long)(65 << 16), 32);	/* mm, 16.16 */
	setProperty("SurfaceHeight", (unsigned long long)(115 << 16), 32);
	gSweetTouch = this;
	registerService();
	xzs_touch_start();
	IOLog("SWEET HID: touch registered\n");
	return true;
}

void
SweetHIDTouch::touch(int finger, bool down, int x, int y)
{
	uint64_t now = mach_absolute_time();
	IOHIDEvent *collection;
	bool any = false;

	if (finger < 0 || finger >= SWEET_TOUCH_FINGERS) {
		return;
	}
	x = x < 0 ? 0 : (x > 1079 ? 1079 : x);
	y = y < 0 ? 0 : (y > 1919 ? 1919 : y);

	IOLockLock(fLock);
	bool was = fDown[finger];
	IOFixed nx = (IOFixed)(((int64_t)x << 16) / 1080);
	IOFixed ny = (IOFixed)(((int64_t)y << 16) / 1920);
	bool moved = down && (nx != fX[finger] || ny != fY[finger]);
	if (was == down && !moved) {
		IOLockUnlock(fLock);
		return;
	}
	if (!was && down) {
		fEdge[finger] = x < SWEET_TOUCH_EDGE_SIDE_PX || x > 1079 - SWEET_TOUCH_EDGE_SIDE_PX ||
		    y < SWEET_TOUCH_EDGE_TOP_PX || y > 1919 - SWEET_TOUCH_EDGE_BOTTOM_PX;
	}

	collection = IOHIDEvent::digitizerEvent(now, 0, kIOHIDDigitizerTransducerTypeHand, false, 0, 0, 0);
	if (!collection) {
		IOLockUnlock(fLock);
		return;
	}
	collection->setIntegerValue(kIOHIDEventFieldDigitizerCollection, TRUE);
	collection->setIntegerValue(kIOHIDEventFieldDigitizerIsDisplayIntegrated, TRUE);
	collection->setIntegerValue(kIOHIDEventFieldIsBuiltIn, TRUE);

	UInt32 cmask = 0;
	for (int i = 0; i < SWEET_TOUCH_FINGERS; i++) {
		bool d = (i == finger) ? down : fDown[i];
		if (i != finger && !d) {
			continue;
		}
		IOFixed fx = (i == finger && down) ? nx : fX[i];
		IOFixed fy = (i == finger && down) ? ny : fY[i];
		IOHIDEvent *child = IOHIDEvent::digitizerEvent(now, (UInt32)i + 2, kIOHIDDigitizerTransducerTypeFinger,
		    d, d ? 1 : 0, fx, fy);
		if (!child) {
			continue;
		}
		UInt32 mask = fEdge[i] ? kIOHIDDigitizerEventFromEdgeTip : 0;
		if (i == finger) {
			if (was != down) {
				mask |= kIOHIDDigitizerEventTouch | kIOHIDDigitizerEventRange;
			}
			if (moved) {
				mask |= kIOHIDDigitizerEventPosition;
			}
			if (!was && down) {
				mask |= kIOHIDDigitizerEventIdentity;
			}
		}
		child->setIntegerValue(kIOHIDEventFieldDigitizerIndex, i);
		child->setIntegerValue(kIOHIDEventFieldDigitizerIdentity, i + 2);
		child->setIntegerValue(kIOHIDEventFieldDigitizerTouch, d);
		child->setIntegerValue(kIOHIDEventFieldDigitizerIsDisplayIntegrated, TRUE);
		child->setIntegerValue(kIOHIDEventFieldDigitizerEventMask, mask);
		collection->appendChild(child);
		child->release();
		cmask |= mask;
		any |= d;
	}
	fDown[finger] = down;
	if (down) {
		fX[finger] = nx;
		fY[finger] = ny;
	}
	collection->setFixedValue(kIOHIDEventFieldDigitizerX, nx);
	collection->setFixedValue(kIOHIDEventFieldDigitizerY, ny);
	collection->setIntegerValue(kIOHIDEventFieldDigitizerRange, any);
	collection->setIntegerValue(kIOHIDEventFieldDigitizerTouch, any);
	collection->setIntegerValue(kIOHIDEventFieldDigitizerEventMask, cmask);
	collection->setIntegerValue(kIOHIDEventFieldDigitizerButtonMask, any ? 1 : 0);
	IOLockUnlock(fLock);

	static uint32_t s_touch_events_logged = 0;
	if (was != down && s_touch_events_logged < 64) {
		s_touch_events_logged++;
		IOLog("SWEET HID: touch %d %s at %d,%d\n", finger, down ? "down" : "up", x, y);
	}
	dispatchEvent(collection);
	collection->release();
}

/* key: 0 power (side button), 1 volume up, 2 volume down, 3 home */
extern "C" void
sweet_hid_key(int key, int down)
{
	static const UInt32 usages[] = {
		kHIDUsage_Csmr_Power, kHIDUsage_Csmr_VolumeIncrement,
		kHIDUsage_Csmr_VolumeDecrement, kHIDUsage_Csmr_Menu,
	};

	if (gSweetButtons && key >= 0 && key < (int)(sizeof(usages) / sizeof(usages[0]))) {
		gSweetButtons->key(usages[key], down != 0);
	}
}

extern "C" void
sweet_hid_touch(int finger, int down, int x, int y)
{
	if (gSweetTouch) {
		gSweetTouch->touch(finger, down != 0, x, y);
	}
}

/* ============================================================================
 * Sony Xperia XZs (MSM8996 / keyaki) Hardware Input Drivers:
 *   1. Physical Keys via PM8994 SPMI (SID 0):
 *      - Power (PON_RT_STS 0x0810 bit 0)         -> key 0 (Power)
 *      - Volume Up (GPIO3_RT_STS 0xc210 bit 0)   -> key 1 (Volume Increment)
 *      - Volume Down (GPIO2_RT_STS 0xc110 bit 0) -> key 2 (Volume Decrement)
 *      - Camera Key (GPIO4_RT_STS 0xc310 bit 0)  -> key 3 (Home / Menu)
 *   2. Synaptics ClearPad RMI4 In-Cell Touchscreen on BLSP2 QUP6 I2C (0x2c):
 *      - GPIO87 (SDA), GPIO88 (SCL), GPIO125 (IRQ active-low)
 *      - Strictly preserves In-Cell DDIC/Touch state (never touches GPIO89)
 * ============================================================================
 */

static bool s_xzs_keys_started = false;
static bool s_xzs_touch_started = false;

static void
xzs_pm8994_gpio_enable_input(uint16_t base)
{
	/* QPNP PIN: MODE_CTL=0x00 (Digital In), DIG_VIN_CTL=0x02 (VIN2 1.8V),
	 * DIG_PULL_CTL=0x00 (30uA pull-up), EN_CTL=0x80 (Master Enable) */
	(void)xzs_spmi_write8(0, (uint16_t)(base + 0x40u), 0x00u);
	(void)xzs_spmi_write8(0, (uint16_t)(base + 0x41u), 0x02u);
	(void)xzs_spmi_write8(0, (uint16_t)(base + 0x42u), 0x00u);
	(void)xzs_spmi_write8(0, (uint16_t)(base + 0x46u), 0x80u);
}

static void
xzs_keys_thread(void *arg __unused, wait_result_t wr __unused)
{
	IOSleep(2000);

	/* Ensure PM8994 GPIO2 (Vol Dn), GPIO3 (Vol Up), GPIO4 (Cam Snap), GPIO5 (Cam Focus) are enabled with 30uA pull-ups */
	xzs_pm8994_gpio_enable_input(0xc100u);
	xzs_pm8994_gpio_enable_input(0xc200u);
	xzs_pm8994_gpio_enable_input(0xc300u);
	xzs_pm8994_gpio_enable_input(0xc400u);
	IODelay(200);

	/* Capture resting baseline of each PM8994 button register:
	 * 0: PON_RT_STS (0x0810, Power)
	 * 1: GPIO3_RT_STS (0xc210, Vol Up)
	 * 2: GPIO2_RT_STS (0xc110, Vol Down)
	 * 3: GPIO4_RT_STS (0xc310, Camera Snapshot full-press -> Home)
	 * 4: GPIO5_RT_STS (0xc410, Camera Focus half-press -> Home) */
	static const uint16_t k_regs[5] = { 0x0810u, 0xc210u, 0xc110u, 0xc310u, 0xc410u };
	uint8_t base_bit[5] = { 0, 1, 1, 1, 1 };
	bool valid[5] = { false, false, false, false, false };
	int state[5] = { 0, 0, 0, 0, 0 };
	int home_down = 0;

	for (int k = 0; k < 5; k++) {
		uint8_t v = 0;
		if (xzs_spmi_read8(0, k_regs[k], &v) == 0) {
			base_bit[k] = v & 1u;
			valid[k] = true;
		}
	}
	IOLog("XZS KEYS: PM8994 baseline pon=%u/%d vol_up=%u/%d vol_dn=%u/%d cam_snap=%u/%d cam_focus=%u/%d\n",
	    base_bit[0], valid[0], base_bit[1], valid[1], base_bit[2], valid[2],
	    base_bit[3], valid[3], base_bit[4], valid[4]);

	for (;;) {
		for (int k = 0; k < 5; k++) {
			if (!valid[k]) {
				continue;
			}
			uint8_t v = 0;
			if (xzs_spmi_read8(0, k_regs[k], &v) == 0) {
				int down = ((v & 1u) != base_bit[k]) ? 1 : 0;
				if (down != state[k]) {
					state[k] = down;
					IOLog("XZS KEYS: hw_key %d %s (reg 0x%04x=0x%02x)\n",
					    k, down ? "down" : "up", k_regs[k], v);
					if (k == 0 && down) {
						g_xzs_power_key_used = true;
					}
					if (k < 3) {
						sweet_hid_key(k, down);
					} else {
						int next_home = (state[3] || state[4]) ? 1 : 0;
						if (next_home != home_down) {
							home_down = next_home;
							sweet_hid_key(3, home_down);
						}
					}
				}
			}
		}
		IOSleep(10);
	}
}

static void
xzs_keys_start(void)
{
	int enabled = 1;
	if (s_xzs_keys_started) {
		return;
	}
	s_xzs_keys_started = true;
	(void)PE_parse_boot_argn("sweetkeys", &enabled, sizeof(enabled));
	if (!enabled) {
		IOLog("XZS KEYS: disabled by boot-arg\n");
		return;
	}
	thread_t th = THREAD_NULL;
	if (kernel_thread_start(xzs_keys_thread, NULL, &th) == KERN_SUCCESS) {
		thread_deallocate(th);
	}
}

/* Open-drain bit-banged I2C on MSM8996 TLMM GPIO87 (SDA) & GPIO88 (SCL) */
#define XZS_TLMM_PA         0x01010000ULL
#define XZS_TLMM_SIZE       0x00100000ULL
#define XZS_GPIO_CFG(n)     ((uint32_t)(n) * 0x1000u)
#define XZS_GPIO_IN_OUT(n)  ((uint32_t)(n) * 0x1000u + 4u)
#define XZS_I2C_SDA_PIN     87u
#define XZS_I2C_SCL_PIN     88u
#define XZS_TOUCH_IRQ_PIN   125u

static vm_offset_t s_tlmm_base = 0;

static inline uint32_t
xzs_tlmm_rd(uint32_t off)
{
	uint32_t v = *(volatile uint32_t *)(s_tlmm_base + off);
	__asm__ volatile("dmb oshld" ::: "memory");
	return v;
}

static inline void
xzs_tlmm_wr(uint32_t off, uint32_t v)
{
	__asm__ volatile("dmb oshst" ::: "memory");
	*(volatile uint32_t *)(s_tlmm_base + off) = v;
	__asm__ volatile("dsb osh" ::: "memory");
}

/* CFG: FUNC_SEL=0 (GPIO), GPIO_PULL=3 (Pull-Up), DRV=2 (6mA); OE=bit 9 */
#define XZS_I2C_PIN_HI_CFG  0x0083u
#define XZS_I2C_PIN_LO_CFG  0x0283u

static inline void
xzs_sda_hi(void)
{
	xzs_tlmm_wr(XZS_GPIO_CFG(XZS_I2C_SDA_PIN), XZS_I2C_PIN_HI_CFG);
}

static inline void
xzs_sda_lo(void)
{
	xzs_tlmm_wr(XZS_GPIO_IN_OUT(XZS_I2C_SDA_PIN), 0u);
	xzs_tlmm_wr(XZS_GPIO_CFG(XZS_I2C_SDA_PIN), XZS_I2C_PIN_LO_CFG);
}

static inline uint32_t
xzs_sda_in(void)
{
	return xzs_tlmm_rd(XZS_GPIO_IN_OUT(XZS_I2C_SDA_PIN)) & 1u;
}

static inline void
xzs_scl_hi(void)
{
	xzs_tlmm_wr(XZS_GPIO_CFG(XZS_I2C_SCL_PIN), XZS_I2C_PIN_HI_CFG);
	for (unsigned i = 0; i < 4000; i++) {
		if (xzs_tlmm_rd(XZS_GPIO_IN_OUT(XZS_I2C_SCL_PIN)) & 1u) {
			break;
		}
		IODelay(1);
	}
}

static inline void
xzs_scl_lo(void)
{
	xzs_tlmm_wr(XZS_GPIO_IN_OUT(XZS_I2C_SCL_PIN), 0u);
	xzs_tlmm_wr(XZS_GPIO_CFG(XZS_I2C_SCL_PIN), XZS_I2C_PIN_LO_CFG);
}

static void
xzs_i2c_start_cond(void)
{
	xzs_sda_hi();
	xzs_scl_hi();
	IODelay(2);
	xzs_sda_lo();
	IODelay(2);
	xzs_scl_lo();
	IODelay(2);
}

static void
xzs_i2c_stop_cond(void)
{
	xzs_sda_lo();
	IODelay(2);
	xzs_scl_hi();
	IODelay(2);
	xzs_sda_hi();
	IODelay(2);
}

static bool
xzs_i2c_tx_byte(uint8_t b)
{
	for (int bit = 7; bit >= 0; bit--) {
		if ((b >> bit) & 1u) {
			xzs_sda_hi();
		} else {
			xzs_sda_lo();
		}
		IODelay(1);
		xzs_scl_hi();
		IODelay(2);
		xzs_scl_lo();
		IODelay(1);
	}
	xzs_sda_hi();
	IODelay(1);
	xzs_scl_hi();
	IODelay(2);
	bool ack = (xzs_sda_in() == 0u);
	xzs_scl_lo();
	IODelay(1);
	return ack;
}

static uint8_t
xzs_i2c_rx_byte(bool ack)
{
	uint8_t b = 0;
	xzs_sda_hi();
	for (int bit = 7; bit >= 0; bit--) {
		IODelay(1);
		xzs_scl_hi();
		IODelay(2);
		if (xzs_sda_in()) {
			b |= (uint8_t)(1u << bit);
		}
		xzs_scl_lo();
		IODelay(1);
	}
	if (ack) {
		xzs_sda_lo();
	} else {
		xzs_sda_hi();
	}
	IODelay(1);
	xzs_scl_hi();
	IODelay(2);
	xzs_scl_lo();
	xzs_sda_hi();
	IODelay(1);
	return b;
}

static bool
xzs_rmi4_write(uint8_t slave, uint8_t reg, const uint8_t *buf, uint32_t len)
{
	xzs_i2c_start_cond();
	if (!xzs_i2c_tx_byte((uint8_t)(slave << 1)) || !xzs_i2c_tx_byte(reg)) {
		xzs_i2c_stop_cond();
		return false;
	}
	for (uint32_t i = 0; i < len; i++) {
		if (!xzs_i2c_tx_byte(buf[i])) {
			xzs_i2c_stop_cond();
			return false;
		}
	}
	xzs_i2c_stop_cond();
	return true;
}

static bool
xzs_rmi4_read(uint8_t slave, uint8_t reg, uint8_t *buf, uint32_t len)
{
	xzs_i2c_start_cond();
	if (!xzs_i2c_tx_byte((uint8_t)(slave << 1)) || !xzs_i2c_tx_byte(reg)) {
		xzs_i2c_stop_cond();
		return false;
	}
	IODelay(5);
	xzs_i2c_start_cond();
	if (!xzs_i2c_tx_byte((uint8_t)((slave << 1) | 1u))) {
		xzs_i2c_stop_cond();
		return false;
	}
	for (uint32_t i = 0; i < len; i++) {
		buf[i] = xzs_i2c_rx_byte(i + 1u < len);
	}
	xzs_i2c_stop_cond();
	return true;
}

static void
xzs_touch_thread(void *arg __unused, wait_result_t wr __unused)
{
	IOSleep(2000);

	s_tlmm_base = ml_io_map(XZS_TLMM_PA, XZS_TLMM_SIZE);
	if (s_tlmm_base == 0) {
		IOLog("XZS TOUCH: failed to map TLMM\n");
		return;
	}

	uint32_t orig_sda_cfg = xzs_tlmm_rd(XZS_GPIO_CFG(XZS_I2C_SDA_PIN));
	uint32_t orig_scl_cfg = xzs_tlmm_rd(XZS_GPIO_CFG(XZS_I2C_SCL_PIN));
	uint32_t orig_irq_cfg = xzs_tlmm_rd(XZS_GPIO_CFG(XZS_TOUCH_IRQ_PIN));

	/* Configure GPIO125 (IRQ) as input with pull-up, and GPIO87/88 for open-drain I2C */
	xzs_tlmm_wr(XZS_GPIO_CFG(XZS_TOUCH_IRQ_PIN), 0x0003u);
	xzs_tlmm_wr(XZS_GPIO_IN_OUT(XZS_I2C_SDA_PIN), 0u);
	xzs_tlmm_wr(XZS_GPIO_IN_OUT(XZS_I2C_SCL_PIN), 0u);
	xzs_sda_hi();
	xzs_scl_hi();
	IODelay(50);

	static const uint8_t k_addrs[3] = { 0x2cu, 0x20u, 0x70u };
	uint8_t slave = 0;
	for (unsigned a = 0; a < 3; a++) {
		uint8_t zero = 0x00u;
		if (xzs_rmi4_write(k_addrs[a], 0xffu, &zero, 1)) {
			slave = k_addrs[a];
			break;
		}
	}

	if (slave == 0) {
		IOLog("XZS TOUCH: no Synaptics RMI4 ACK on GPIO87/88 (orig_cfg sda=0x%x scl=0x%x irq=0x%x in=%u)\n",
		    orig_sda_cfg, orig_scl_cfg, orig_irq_cfg,
		    xzs_tlmm_rd(XZS_GPIO_IN_OUT(XZS_TOUCH_IRQ_PIN)) & 1u);
		return;
	}

	uint8_t f01_ctrl = 0, f01_data = 0;
	uint8_t f11_data = 0, f12_data = 0, f12_ctrl = 0, f12_query = 0;
	uint8_t f12_page = 0, f11_page = 0;

	for (uint8_t page = 0; page < 2; page++) {
		if (!xzs_rmi4_write(slave, 0xffu, &page, 1)) {
			continue;
		}
		for (uint32_t addr = 0xe9u; addr >= 0xbfu; addr -= 6u) {
			uint8_t pdt[6] = { 0 };
			if (!xzs_rmi4_read(slave, (uint8_t)addr, pdt, 6)) {
				break;
			}
			uint8_t fn = pdt[5];
			if (fn == 0x00u || fn == 0xffu) {
				break;
			}
			IOLog("XZS TOUCH: RMI4 addr=0x%02x page=%u F%02x q=0x%02x c=0x%02x ctrl=0x%02x data=0x%02x irqs=%u\n",
			    slave, page, fn, pdt[0], pdt[1], pdt[2], pdt[3], pdt[4] & 7u);
			if (fn == 0x01u && f01_data == 0) {
				f01_ctrl = pdt[2];
				f01_data = pdt[3];
			} else if (fn == 0x12u && f12_data == 0) {
				f12_query = pdt[0];
				f12_ctrl = pdt[2];
				f12_data = pdt[3];
				f12_page = page;
			} else if (fn == 0x11u && f11_data == 0) {
				f11_data = pdt[3];
				f11_page = page;
			}
		}
	}

	uint8_t page0 = 0x00u;
	(void)xzs_rmi4_write(slave, 0xffu, &page0, 1);

	/* Ensure F01 is awake (SleepMode=00 Normal, Configured=1) and interrupts enabled */
	if (f01_ctrl != 0) {
		uint8_t ctrl[2] = { 0x80u, 0xffu };
		if (xzs_rmi4_read(slave, f01_ctrl, ctrl, 2)) {
			ctrl[0] = (uint8_t)((ctrl[0] & ~0x03u) | 0x80u);
			ctrl[1] = 0xffu;
			(void)xzs_rmi4_write(slave, f01_ctrl, ctrl, 2);
		}
	}
	if (f01_data != 0) {
		uint8_t st[2] = { 0 };
		(void)xzs_rmi4_read(slave, f01_data, st, 2);
		IOLog("XZS TOUCH: F01 status=0x%02x irq=0x%02x gpio125=%u\n",
		    st[0], st[1], xzs_tlmm_rd(XZS_GPIO_IN_OUT(XZS_TOUCH_IRQ_PIN)) & 1u);
	}

	/* Inspect F12 Query 5 (ctrl presence), Query 8 (data presence), Ctrl 8 (max X/Y), Ctrl 23 (obj enable) */
	uint8_t f12_data1_off = 1u;
	int sensor_max_x = 1079;
	int sensor_max_y = 1919;
	if (f12_data != 0 && f12_query != 0) {
		(void)xzs_rmi4_write(slave, 0xffu, &f12_page, 1);
		uint8_t q5[5] = { 0 };
		uint8_t q8[4] = { 0 };
		(void)xzs_rmi4_read(slave, (uint8_t)(f12_query + 5u), q5, sizeof(q5));
		if (xzs_rmi4_read(slave, (uint8_t)(f12_query + 8u), q8, sizeof(q8))) {
			f12_data1_off = (q8[1] & 0x01u) ? 1u : 0u;
		}
		IOLog("XZS TOUCH: F12 q5=%02x %02x %02x %02x %02x q8=%02x %02x %02x %02x -> data1_off=%u (reg 0x%02x)\n",
		    q5[0], q5[1], q5[2], q5[3], q5[4], q8[0], q8[1], q8[2], q8[3],
		    f12_data1_off, (uint8_t)(f12_data + f12_data1_off));

		if (f12_ctrl != 0) {
			/* Compute ctrl_8, ctrl_20, ctrl_23 register offsets from q5 presence bitmap */
			uint8_t ctrl_off = 0;
			uint8_t ctrl8_reg = 0, ctrl20_reg = 0, ctrl23_reg = 0;
			for (uint8_t c = 0; c < 24; c++) {
				uint8_t byte_idx = (uint8_t)(1u + (c / 8u));
				uint8_t bit_mask = (uint8_t)(1u << (c % 8u));
				if (q5[byte_idx] & bit_mask) {
					if (c == 8)  ctrl8_reg  = (uint8_t)(f12_ctrl + ctrl_off);
					if (c == 20) ctrl20_reg = (uint8_t)(f12_ctrl + ctrl_off);
					if (c == 23) ctrl23_reg = (uint8_t)(f12_ctrl + ctrl_off);
					ctrl_off++;
				}
			}
			if (ctrl8_reg != 0) {
				uint8_t c8[4] = { 0 };
				if (xzs_rmi4_read(slave, ctrl8_reg, c8, 4)) {
					int mx = (int)(c8[0] | ((uint16_t)c8[1] << 8));
					int my = (int)(c8[2] | ((uint16_t)c8[3] << 8));
					if (mx > 100 && mx <= 4096 && my > 100 && my <= 4096) {
						sensor_max_x = mx;
						sensor_max_y = my;
					}
					IOLog("XZS TOUCH: F12 ctrl8(0x%02x)=%02x %02x %02x %02x max=%dx%d\n",
					    ctrl8_reg, c8[0], c8[1], c8[2], c8[3], sensor_max_x, sensor_max_y);
				}
			}
			if (ctrl20_reg != 0) {
				uint8_t c20[2] = { 0 };
				if (xzs_rmi4_read(slave, ctrl20_reg, c20, 2)) {
					IOLog("XZS TOUCH: F12 ctrl20(0x%02x)=%02x %02x\n", ctrl20_reg, c20[0], c20[1]);
					/* Set byte 0 to 0x00 (Continuous reporting mode instead of 0x01 Reduced mode) */
					if (c20[0] != 0x00u) {
						c20[0] = 0x00u;
						(void)xzs_rmi4_write(slave, ctrl20_reg, c20, 2);
					}
				}
			}
			if (ctrl23_reg != 0) {
				uint8_t c23[2] = { 0 };
				if (xzs_rmi4_read(slave, ctrl23_reg, c23, 2)) {
					IOLog("XZS TOUCH: F12 ctrl23(0x%02x)=%02x %02x\n", ctrl23_reg, c23[0], c23[1]);
					if ((c23[0] & 0x03u) == 0u) {
						c23[0] |= 0x03u;
						(void)xzs_rmi4_write(slave, ctrl23_reg, c23, 2);
					}
				}
			}
		}

		uint8_t d0[8] = { 0 }, d1[8] = { 0 };
		(void)xzs_rmi4_read(slave, f12_data, d0, sizeof(d0));
		(void)xzs_rmi4_read(slave, (uint8_t)(f12_data + 1u), d1, sizeof(d1));
		IOLog("XZS TOUCH: F12 init d[0x%02x]=%02x %02x %02x %02x %02x d[0x%02x]=%02x %02x %02x %02x %02x\n",
		    f12_data, d0[0], d0[1], d0[2], d0[3], d0[4],
		    (uint8_t)(f12_data + 1u), d1[0], d1[1], d1[2], d1[3], d1[4]);

		(void)xzs_rmi4_write(slave, 0xffu, &page0, 1);
	}

	if (f12_data == 0 && f11_data == 0) {
		IOLog("XZS TOUCH: no F11/F12 2D touch sensor found on 0x%02x\n", slave);
		return;
	}

	uint32_t active_mask = 0;
	int last_x[SWEET_TOUCH_FINGERS] = { 0 };
	int last_y[SWEET_TOUCH_FINGERS] = { 0 };
	uint8_t lift_streak[SWEET_TOUCH_FINGERS] = { 0 };
	uint32_t poll_tick = 0;
	uint32_t irq_events_logged = 0;
	uint32_t fail_streak = 0;

	for (;;) {
		uint32_t irq_pin = xzs_tlmm_rd(XZS_GPIO_IN_OUT(XZS_TOUCH_IRQ_PIN)) & 1u;
		poll_tick++;
		uint8_t st[2] = { 0 };
		if (irq_pin == 0u || (poll_tick & 15u) == 0u) {
			if (f01_data != 0) {
				(void)xzs_rmi4_read(slave, f01_data, st, 2);
				/* If device reset/unconfigured bit 7 set, re-assert F01 Configured */
				if ((st[0] & 0x80u) != 0u && (st[0] & 0x70u) == 0u && f01_ctrl != 0) {
					uint8_t ctrl[2] = { 0x80u, 0xffu };
					(void)xzs_rmi4_write(slave, f01_ctrl, ctrl, 2);
				}
			}
		}
		if (irq_pin == 0u || st[1] != 0u || active_mask != 0u) {
			uint32_t next_mask = 0;
			if (f12_data != 0) {
				if (f12_page != 0) {
					(void)xzs_rmi4_write(slave, 0xffu, &f12_page, 1);
				}
				uint8_t raw[16] = { 0 }; /* 2 fingers * 8 bytes */
				bool ok = xzs_rmi4_read(slave, (uint8_t)(f12_data + f12_data1_off), raw, sizeof(raw));
				/* If finger is active during an IRQ and In-Cell FIFO returned transient 00, retry once after 150us */
				if (ok && active_mask != 0u && (irq_pin == 0u || st[1] != 0u) &&
				    raw[0] == 0u && raw[8] == 0u) {
					IODelay(150);
					ok = xzs_rmi4_read(slave, (uint8_t)(f12_data + f12_data1_off), raw, sizeof(raw));
				}
				if ((irq_pin == 0u || st[1] != 0u) && irq_events_logged < 32) {
					irq_events_logged++;
					IOLog("XZS TOUCH IRQ: gpio125=%u st=%02x/%02x reg=0x%02x raw=%02x %02x %02x %02x %02x %02x %02x %02x\n",
					    irq_pin, st[0], st[1], (uint8_t)(f12_data + f12_data1_off),
					    raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7]);
				}
				if (ok) {
					fail_streak = 0;
					for (int i = 0; i < 2; i++) {
						const uint8_t *fo = &raw[i * 8];
						uint8_t type = fo[0];
						bool down = (type == 0x01u || type == 0x03u);
						int rx = (int)(fo[1] | ((uint16_t)fo[2] << 8));
						int ry = (int)(fo[3] | ((uint16_t)fo[4] << 8));
						if (rx < 0 || rx > sensor_max_x || ry < 0 || ry > sensor_max_y) {
							down = false;
						}
						int x = (sensor_max_x > 0 && sensor_max_x != 1079) ?
						    (rx * 1079) / sensor_max_x : rx;
						int y = (sensor_max_y > 0 && sensor_max_y != 1919) ?
						    (ry * 1919) / sensor_max_y : ry;
						/* Reject 2.5D curved-bezel grip contacts on initial touch-down */
						if (down && !(active_mask & (1u << i)) && (x < 12 || x > 1067)) {
							down = false;
						}
						if (down && x >= 0 && x <= 1079 && y >= 0 && y <= 1919) {
							s_real_touch_seen = true;
							lift_streak[i] = 0;
							next_mask |= (1u << i);
							last_x[i] = x;
							last_y[i] = y;
							sweet_hid_touch(i, 1, x, y);
						} else if (active_mask & (1u << i)) {
							/* Require 3 consecutive lift samples (~12ms) to prevent mid-swipe dropout */
							if (++lift_streak[i] < 3u) {
								next_mask |= (1u << i);
							} else {
								lift_streak[i] = 0;
								sweet_hid_touch(i, 0, last_x[i], last_y[i]);
							}
						}
					}
					active_mask = next_mask;
				} else if (active_mask != 0u) {
					if (++fail_streak >= 3u) {
						for (int i = 0; i < 2; i++) {
							if (active_mask & (1u << i)) {
								lift_streak[i] = 0;
								sweet_hid_touch(i, 0, last_x[i], last_y[i]);
							}
						}
						active_mask = 0;
						fail_streak = 0;
					}
				}
				if (f12_page != 0) {
					(void)xzs_rmi4_write(slave, 0xffu, &page0, 1);
				}
			} else if (f11_data != 0) {
				if (f11_page != 0) {
					(void)xzs_rmi4_write(slave, 0xffu, &f11_page, 1);
				}
				uint8_t raw[28] = { 0 }; /* 3 state bytes + 5 fingers * 5 bytes */
				if (xzs_rmi4_read(slave, f11_data, raw, sizeof(raw))) {
					for (int i = 0; i < 5; i++) {
						uint8_t fstate = (raw[i / 4] >> ((i % 4) * 2)) & 0x03u;
						const uint8_t *fd = &raw[3 + i * 5];
						int x = ((int)fd[0] << 4) | (fd[2] & 0x0fu);
						int y = ((int)fd[1] << 4) | ((fd[2] >> 4) & 0x0fu);
						if (fstate != 0u && x <= 1200 && y <= 2000) {
							next_mask |= (1u << i);
							last_x[i] = x;
							last_y[i] = y;
							sweet_hid_touch(i, 1, x, y);
						} else if (active_mask & (1u << i)) {
							sweet_hid_touch(i, 0, last_x[i], last_y[i]);
						}
					}
					active_mask = next_mask;
				}
				if (f11_page != 0) {
					(void)xzs_rmi4_write(slave, 0xffu, &page0, 1);
				}
			}
		}
		IOSleep(active_mask != 0u ? 4 : 6);
	}
}

static void
xzs_touch_start(void)
{
	int enabled = 1;
	if (s_xzs_touch_started) {
		return;
	}
	s_xzs_touch_started = true;
	(void)PE_parse_boot_argn("sweettouch", &enabled, sizeof(enabled));
	if (!enabled) {
		IOLog("XZS TOUCH: disabled by boot-arg\n");
		return;
	}
	thread_t th = THREAD_NULL;
	if (kernel_thread_start(xzs_touch_thread, NULL, &th) == KERN_SUCCESS) {
		thread_deallocate(th);
	}
}
