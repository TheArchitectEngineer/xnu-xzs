/*
 * Xperia XZs IOKit BSD Storage Nub Bridge
 *
 * Implements a minimal IOService subclass for Xperia XZs eMMC storage
 * that publishes BSD registry properties (BSD Name, BSD Major, BSD Minor)
 * required for IOKit BSD root discovery compatibility.
 */

#include <IOKit/IOService.h>
#include <IOKit/IOBSD.h>
#include <IOKit/IOLib.h>
#include <sys/errno.h>

class XZSeMMCStorageNub : public IOService {
	OSDeclareDefaultStructors(XZSeMMCStorageNub);
public:
	virtual bool init(OSDictionary *dictionary = NULL) APPLE_KEXT_OVERRIDE;
};

OSDefineMetaClassAndStructors(XZSeMMCStorageNub, IOService);

bool
XZSeMMCStorageNub::init(OSDictionary *dictionary)
{
	if (!IOService::init(dictionary)) {
		return false;
	}
	return true;
}

extern "C" int
xzs_storage_nub_publish(int bsd_major, int bsd_minor)
{
	XZSeMMCStorageNub *nub = new XZSeMMCStorageNub;
	if (!nub) {
		return ENOMEM;
	}
	if (!nub->init()) {
		nub->release();
		return EIO;
	}

	nub->setName("xzs_emmc_storage");
	nub->attach(IOService::getPlatform());
	nub->setProperty(kIOBSDNameKey, "disk0");
	nub->setProperty(kIOBSDMajorKey, (unsigned long long)bsd_major, 32);
	nub->setProperty(kIOBSDMinorKey, (unsigned long long)bsd_minor, 32);
	nub->registerService();

	return 0;
}

extern "C" int
xzs_storage_nub_find_bsd_name(const char *name, char *out_name, size_t out_name_size, int *out_major, int *out_minor)
{
	if (!name) {
		return EINVAL;
	}

	OSDictionary *matching = IOBSDNameMatching(name);
	if (!matching) {
		return ENOENT;
	}

	IOService *service = IOService::waitForMatchingService(matching, 1000000); // 1s timeout
	matching->release();
	if (!service) {
		return ENOENT;
	}

	if (out_name && out_name_size > 0) {
		OSString *iostr = OSDynamicCast(OSString, service->getProperty(kIOBSDNameKey));
		if (iostr) {
			strlcpy(out_name, iostr->getCStringNoCopy(), out_name_size);
		} else {
			out_name[0] = '\0';
		}
	}

	if (out_major) {
		OSNumber *off = OSDynamicCast(OSNumber, service->getProperty(kIOBSDMajorKey));
		if (off) {
			*out_major = (int)off->unsigned32BitValue();
		} else {
			*out_major = -1;
		}
	}

	if (out_minor) {
		OSNumber *off = OSDynamicCast(OSNumber, service->getProperty(kIOBSDMinorKey));
		if (off) {
			*out_minor = (int)off->unsigned32BitValue();
		} else {
			*out_minor = -1;
		}
	}

	service->release();
	return 0;
}
