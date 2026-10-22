/*
 * Built-in AppleKeyStore for SWEET: a device without a passcode whose
 * system keybag is always loaded and unlocked.
 *
 * Apple's AppleKeyStore kext (closed, SEP-backed) owns the keybags behind
 * data protection. libaks (AppleKeyStore.framework, statically in keybagd)
 * opens IOServiceMatching("AppleKeyStore") with type 0, calls selector 0,
 * then one selector per aks_* function (iOS 16.7 numbering below). keybagd
 * --init asks for the system bag (14) and only loads systembag.kb when the
 * answer is kIOReturnNotFound; answering with a handle is the path a
 * userspace reboot takes, so no bag file is needed.
 *
 * Every call is logged (selector, caller, argument shapes, result) so the
 * next selectors the system needs show up on the console; unknown ones fail
 * with kIOReturnUnsupported.
 */
#include <pexpert/arm64/board_config.h>

#if defined(SWEET) || defined(XZS)
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <libkern/c++/OSString.h>
#include <sys/proc.h>

/* libaks selectors (AppleKeyStore.framework, iOS 16.5/16.7). */
enum {
	kAKSOpen             = 0,
	kAKSCreateBag        = 2,
	kAKSSaveBag          = 3,
	kAKSUnloadBag        = 4,
	kAKSSetSystem        = 5,
	kAKSLoadBag          = 6,
	kAKSGetLockState     = 7,
	kAKSLockDevice       = 8,
	kAKSUnlockDevice     = 9,
	kAKSWrapKey          = 10,
	kAKSUnwrapKey        = 11,
	kAKSUnlockBag        = 12,
	kAKSLockBag          = 13,
	kAKSGetSystem        = 14,
	kAKSGetDeviceState   = 17,
	kAKSGetBagUUID       = 23,
	kAKSAssertHold       = 26,
	kAKSAssertDrop       = 27,
	kAKSGetConfiguration = 31,
	kAKSLockDeviceNow    = 32,
	kAKSGetExtDeviceState = 35,
	kAKSGeneration       = 40,
	kAKSUnloadSessionBags = 55,
	kAKSAssertPromote    = 100,
	kAKSAssertConsume    = 101,
	kAKSNotifyStateSet   = 102,
	kAKSNotifyStateIsSet = 103,
	kAKSPrewarm          = 115,
};

/* The system (device) bag handle. */
#define SWEET_AKS_SYSTEM_HANDLE 0

/*
 * aks lock state bits (aks_get_lock_state): bit 1 = no passcode,
 * bit 2 = unlocked since boot (MobileKeyBag MKBDeviceUnlockedSinceBoot).
 */
#define SWEET_AKS_LOCK_STATE    0x6
/* lock state: 0 unlocked, 1 locked, 2 locking, 3 disabled (no passcode) */
#define kAKSLockStateDisabled   3

static uint64_t gAKSLogged[4];
static bool     gAKSNotifyState = false;

class AppleKeyStore : public IOService {
	OSDeclareDefaultStructors(AppleKeyStore);

public:
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
};

OSDefineMetaClassAndStructors(AppleKeyStore, IOService);

bool
AppleKeyStore::start(IOService *provider)
{
	if (!IOService::start(provider)) {
		return false;
	}
	setProperty(kIOUserClientClassKey, "AppleKeyStoreUserClient");
	setName("AppleKeyStore");
	registerService();
	IOLog("SWEET AKS: AppleKeyStore registered (no passcode, unlocked)\n");
	return true;
}

class AppleKeyStoreUserClient : public IOUserClient {
	OSDeclareDefaultStructors(AppleKeyStoreUserClient);

public:
	IOReturn clientClose(void) APPLE_KEXT_OVERRIDE;
	IOReturn registerNotificationPort(mach_port_t port, UInt32 type, io_user_reference_t refCon) APPLE_KEXT_OVERRIDE;
	IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args,
	    IOExternalMethodDispatch *dispatch, OSObject *target,
	    void *reference) APPLE_KEXT_OVERRIDE;
};

OSDefineMetaClassAndStructors(AppleKeyStoreUserClient, IOUserClient);

IOReturn
AppleKeyStoreUserClient::clientClose(void)
{
	if (!isInactive()) {
		terminate();
	}
	return kIOReturnSuccess;
}

IOReturn
AppleKeyStoreUserClient::registerNotificationPort(mach_port_t port, __unused UInt32 type, __unused io_user_reference_t refCon)
{
	releaseNotificationPort(port);
	return kIOReturnSuccess;
}

/* Copy len bytes to the caller's output structure (inband or descriptor). */
static IOReturn
sweet_aks_struct_out(IOExternalMethodArguments *args, const void *buf, uint32_t len)
{
	if (args->structureOutputDescriptor != NULL) {
		IOMemoryDescriptor *out = args->structureOutputDescriptor;
		if (len > out->getLength()) {
			return kIOReturnNoSpace;
		}
		IOReturn ret = out->prepare();
		if (ret == kIOReturnSuccess) {
			out->writeBytes(0, buf, len);
			out->complete();
			args->structureOutputDescriptorSize = len;
		}
		return ret;
	}
	if (args->structureOutput == NULL || len > args->structureOutputSize) {
		return kIOReturnNoSpace;
	}
	memcpy(args->structureOutput, buf, len);
	args->structureOutputSize = len;
	return kIOReturnSuccess;
}

/* Append SEQUENCE { UTF8String key, INTEGER value } (short forms only). */
static uint32_t
sweet_aks_der_pair(uint8_t *p, const char *key, uint8_t value)
{
	uint32_t klen = (uint32_t)strlen(key);
	uint32_t n = 0;
	p[n++] = 0x30;
	p[n++] = (uint8_t)(2 + klen + 3);
	p[n++] = 0x0c;
	p[n++] = (uint8_t)klen;
	memcpy(p + n, key, klen);
	n += klen;
	p[n++] = 0x02;
	p[n++] = 0x01;
	p[n++] = value;
	return n;
}

/*
 * Device state (selector 17 / 35): a DER SET of key/INTEGER pairs that libaks
 * decode_extended_state reads: ss state, sls lock state (3 = disabled / no passcode),
 * sb backoff, sr, sfa failed attempts, sgs generation, sas assertions, sgpe grace period,
 * srcd recovery countdown.
 */
static IOReturn
sweet_aks_device_state(IOExternalMethodArguments *args)
{
	static const struct {
		const char *key;
		uint8_t value;
	} fields[] = {
		{ "sb", 0 }, { "sr", 0 }, { "ss", SWEET_AKS_LOCK_STATE },
		{ "sas", 0 }, { "sfa", 0 }, { "sgs", 0 },
		{ "sls", kAKSLockStateDisabled }, { "sgpe", 0 }, { "srcd", 0 },
	};
	uint8_t buf[128];
	uint32_t n = 2;
	for (unsigned int i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
		n += sweet_aks_der_pair(buf + n, fields[i].key, fields[i].value);
	}
	buf[0] = 0x31;
	buf[1] = (uint8_t)(n - 2);
	return sweet_aks_struct_out(args, buf, n);
}

static IOReturn
sweet_aks_configuration(IOExternalMethodArguments *args)
{
	static const struct {
		const char *key;
		uint8_t value;
	} fields[] = {
		{ "bd", 0 }, { "gp", 0 }, { "mua", 10 },
	};
	uint8_t buf[64];
	uint32_t n = 2;
	for (unsigned int i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
		n += sweet_aks_der_pair(buf + n, fields[i].key, fields[i].value);
	}
	buf[0] = 0x31;
	buf[1] = (uint8_t)(n - 2);
	return sweet_aks_struct_out(args, buf, n);
}

static const uint8_t kWrapMark[4] = { 'H', 'K', 'w', 'r' };

static void
wrapMix(uint8_t *bytes, size_t length)
{
	for (size_t i = 0; i < length; i++) {
		bytes[i] ^= (uint8_t)(0x5a + 37 * i);
	}
}

IOReturn
AppleKeyStoreUserClient::externalMethod(uint32_t selector, IOExternalMethodArguments *args,
    __unused IOExternalMethodDispatch *dispatch, __unused OSObject *target,
    __unused void *reference)
{
	static const uint8_t bagUUID[16] = {
		0x72, 0x65, 0x64, 0x77, 0x6f, 0x6f, 0x64, 0x00,
		0x6b, 0x65, 0x79, 0x62, 0x61, 0x67, 0x00, 0x01
	};

	switch (selector) {
	case kAKSOpen:
	case kAKSUnloadBag:
	case kAKSSetSystem:
	case kAKSLockDevice:
	case kAKSLockDeviceNow:
	case kAKSUnlockDevice:
	case kAKSUnlockBag:
	case kAKSLockBag:
	case kAKSAssertHold:
	case kAKSAssertDrop:
	case kAKSAssertPromote:
	case kAKSAssertConsume:
	case kAKSUnloadSessionBags:
	case kAKSPrewarm:
		args->scalarOutputCount = 0;
		return kIOReturnSuccess;

	case kAKSNotifyStateSet:
		gAKSNotifyState = true;
		args->scalarOutputCount = 0;
		return kIOReturnSuccess;

	case kAKSGetSystem:
	case kAKSGeneration:
		if (args->scalarOutputCount >= 1) {
			args->scalarOutput[0] = SWEET_AKS_SYSTEM_HANDLE;
			args->scalarOutputCount = 1;
		}
		return kIOReturnSuccess;

	case kAKSGetLockState:
		if (args->scalarOutputCount < 1) {
			return kIOReturnBadArgument;
		}
		args->scalarOutput[0] = SWEET_AKS_LOCK_STATE;
		args->scalarOutputCount = 1;
		return kIOReturnSuccess;

	case kAKSNotifyStateIsSet:
		if (args->scalarOutputCount < 1) {
			return kIOReturnBadArgument;
		}
		args->scalarOutput[0] = gAKSNotifyState ? 1 : 0;
		args->scalarOutputCount = 1;
		return kIOReturnSuccess;

	case kAKSGetDeviceState:
	case kAKSGetExtDeviceState:
		args->scalarOutputCount = 0;
		return sweet_aks_device_state(args);

	case kAKSGetConfiguration:
		args->scalarOutputCount = 0;
		return sweet_aks_configuration(args);

	case kAKSGetBagUUID:
		args->scalarOutputCount = 0;
		return sweet_aks_struct_out(args, bagUUID, sizeof(bagUUID));

	case kAKSWrapKey: {
		uint8_t wrapped[8 + 64];
		size_t length = args->structureInputSize;
		if (args->structureInput == NULL || length == 0 || length > 64 || args->scalarInputCount < 2) {
			return kIOReturnBadArgument;
		}
		memcpy(wrapped, kWrapMark, sizeof(kWrapMark));
		wrapped[4] = (uint8_t)args->scalarInput[1];
		wrapped[5] = wrapped[6] = wrapped[7] = 0;
		memcpy(wrapped + 8, args->structureInput, length);
		wrapMix(wrapped + 8, length);
		if (args->scalarOutputCount >= 1) {
			args->scalarOutput[0] = args->scalarInput[1];
			args->scalarOutputCount = 1;
		}
		return sweet_aks_struct_out(args, wrapped, (uint32_t)(8 + length));
	}

	case kAKSUnwrapKey: {
		uint8_t key[64];
		const uint8_t *wrapped = (const uint8_t *)args->structureInput;
		size_t length = args->structureInputSize;
		if (wrapped == NULL || length <= 8 || length > 8 + sizeof(key) ||
		    memcmp(wrapped, kWrapMark, sizeof(kWrapMark)) != 0) {
			return kIOReturnBadArgument;
		}
		memcpy(key, wrapped + 8, length - 8);
		wrapMix(key, length - 8);
		args->scalarOutputCount = 0;
		return sweet_aks_struct_out(args, key, (uint32_t)(length - 8));
	}

	default:
		break;
	}

	if (selector < 256 && !(gAKSLogged[selector / 64] & (1ULL << (selector % 64)))) {
		char name[32] = "?";
		gAKSLogged[selector / 64] |= 1ULL << (selector % 64);
		proc_selfname(name, sizeof(name));
		IOLog("SWEET AKS: %s[%d] selector 0x%x not implemented (scalars %u in, %u out; structure %u in, %u out)\n",
		    name, proc_selfpid(), selector, args->scalarInputCount, args->scalarOutputCount,
		    args->structureInputSize, args->structureOutputSize);
	}
	return kIOReturnUnsupported;
}
#endif /* SWEET || XZS */
