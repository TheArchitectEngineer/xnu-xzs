/*
 * libxzs-inject.dylib — Runtime userland optimization & compatibility hooks
 * for iOS 16.7.16 (20H392) on Sony Xperia XZs (MSM8996 / Adreno 530).
 *
 * Injected via DYLD_INSERT_LIBRARIES=/usr/lib/libxzs-inject.dylib by XNU
 * kern_exec.c into:
 *   1. launchd_cache_loader    Prunes /System/Library/xpc/launchd.plist so
 *                              launchd only starts essential boot daemons and
 *                              loads the rest on-demand via MachServices;
 *                              promotes staged system apps to /Applications;
 *                              resets stale 1-page IconState & lsd .csstore
 *                              caches once so all 38 stock apps appear across
 *                              2 pages; writes SpringBoard/FrontBoard prefs.
 *   2. containermanagerd       Hooks +[MCMUserIdentityCache personasAreSupported]
 *      containermanagerd_system to return NO, eliminating 20s persona retry stalls
 *                              when launching Settings, PosterBoard, and widgets.
 *   3. SpringBoard             Enables plain_vibrancy (BSUIVibrancyEffectViewDisabled)
 *                              for clean white Lock Screen clock on SW renderer,
 *                              forces JPEG/PNG app snapshots instead of 10s CPU ASTC,
 *                              marks device Activated & SetupDone, disables black
 *                              PosterSwitcher & magenta switcher overlay wash,
 *                              skips Siri orb Metal shader prewarm, and waits 3.5s
 *                              on first rebuild boot so lsd indexes all 38 apps.
 *   4. backboardd              Hooks -[EAGLContext initWithAPI:properties:] to
 *                              fast-fail when xzsgpu_agx=0 so WindowServer does not
 *                              probe IOAccelerator bundles on every frame.
 *   5. mediaserverd            Hooks +[BWCoreImageFilterRenderer
 *                              prewarmPortraitFilterVersion:semanticStyleFilters:]
 *                              to no-op so mediaserverd stays alive and VideoToolbox
 *                              (VCPHEVC.videocodec) decodes the 4 HEIC layers of the
 *                              stock iOS 16 wallpaper for PosterBoard.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef intptr_t ssize_t;
typedef int64_t off_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef uint16_t mode_t;
typedef uint32_t useconds_t;
typedef signed char BOOL;
#define YES ((BOOL)1)
#define NO  ((BOOL)0)

typedef void *id;
typedef void *Class;
typedef void *SEL;
typedef void *Method;
typedef void (*IMP)(void);

#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_RDWR   0x0002
#define O_CREAT  0x0200
#define O_TRUNC  0x0400
#define O_NOCTTY 0x20000

#define SEEK_SET 0
#define SEEK_END 2

#define DT_DIR   4
#define DT_REG   8

struct dirent {
	uint64_t d_ino;
	uint64_t d_seekoff;
	uint16_t d_reclen;
	uint16_t d_namlen;
	uint8_t  d_type;
	char     d_name[1024];
};
typedef struct _dirdesc DIR;

struct mach_header;

/* libSystem.B.dylib */
extern const char *getprogname(void);
extern int getpid(void);
extern int open(const char *path, int flags, ...);
extern int close(int fd);
extern ssize_t read(int fd, void *buf, size_t nbyte);
extern ssize_t write(int fd, const void *buf, size_t nbyte);
extern off_t lseek(int fd, off_t offset, int whence);
extern int unlink(const char *path);
extern int rename(const char *oldpath, const char *newpath);
extern int mkdir(const char *path, mode_t mode);
extern int chmod(const char *path, mode_t mode);
extern int chown(const char *path, uid_t owner, gid_t group);
extern int stat(const char *path, void *buf);
extern DIR *opendir(const char *name);
extern struct dirent *readdir(DIR *dirp);
extern int closedir(DIR *dirp);
extern int usleep(useconds_t usec);
extern void *malloc(size_t size);
extern void free(void *ptr);
extern void *memcpy(void *dst, const void *src, size_t n);
extern void *memset(void *b, int c, size_t len);
extern size_t strlen(const char *s);
extern int strcmp(const char *s1, const char *s2);
extern int strncmp(const char *s1, const char *s2, size_t n);
extern char *strstr(const char *haystack, const char *needle);
extern char *strrchr(const char *s, int c);
extern int snprintf(char *str, size_t size, const char *format, ...);
extern int vsnprintf(char *str, size_t size, const char *format, va_list ap);
extern int sysctlbyname(const char *name, void *oldp, size_t *oldlenp, void *newp, size_t newlen);
extern void _dyld_register_func_for_add_image(void (*func)(const struct mach_header *mh, intptr_t vmaddr_slide));

/* libobjc.A.dylib */
extern Class objc_getClass(const char *name);
extern Class object_getClass(id obj);
extern const char *object_getClassName(id obj);
extern SEL sel_registerName(const char *str);
extern Method class_getInstanceMethod(Class cls, SEL name);
extern Method class_getClassMethod(Class cls, SEL name);
extern IMP method_getImplementation(Method m);
extern IMP method_setImplementation(Method m, IMP imp);
extern void objc_msgSend(void);

/* CoreFoundation.framework */
typedef const void *CFTypeRef;
typedef const struct __CFAllocator *CFAllocatorRef;
typedef const struct __CFString *CFStringRef;
typedef const struct __CFBoolean *CFBooleanRef;
typedef const struct __CFNumber *CFNumberRef;
typedef const struct __CFData *CFDataRef;
typedef const struct __CFDictionary *CFDictionaryRef;
typedef struct __CFDictionary *CFMutableDictionaryRef;
typedef CFTypeRef CFPropertyListRef;
typedef unsigned long CFTypeID;
typedef long CFIndex;
typedef unsigned long CFOptionFlags;
typedef uint32_t CFStringEncoding;
typedef unsigned char Boolean;

#define kCFStringEncodingUTF8 ((CFStringEncoding)0x08000100)
#define kCFPropertyListMutableContainersAndLeaves ((CFOptionFlags)2)
#define kCFPropertyListBinaryFormat_v1_0 ((CFIndex)200)
#define kCFNumberSInt32Type ((CFIndex)3)
#define kCFNumberFloat64Type ((CFIndex)6)

extern const CFAllocatorRef kCFAllocatorDefault;
extern const CFBooleanRef kCFBooleanTrue;
extern const CFBooleanRef kCFBooleanFalse;
extern const CFStringRef kCFPreferencesCurrentApplication;
extern const void *kCFTypeDictionaryKeyCallBacks;
extern const void *kCFTypeDictionaryValueCallBacks;

extern void CFRelease(CFTypeRef cf);
extern CFTypeID CFGetTypeID(CFTypeRef cf);
extern CFTypeID CFDictionaryGetTypeID(void);
extern CFTypeID CFStringGetTypeID(void);
extern CFStringRef CFStringCreateWithCString(CFAllocatorRef alloc, const char *cStr, CFStringEncoding encoding);
extern Boolean CFStringGetCString(CFStringRef theString, char *buffer, CFIndex bufferSize, CFStringEncoding encoding);
extern CFDataRef CFDataCreate(CFAllocatorRef allocator, const uint8_t *bytes, CFIndex length);
extern const uint8_t *CFDataGetBytePtr(CFDataRef theData);
extern CFIndex CFDataGetLength(CFDataRef theData);
extern CFNumberRef CFNumberCreate(CFAllocatorRef allocator, CFIndex theType, const void *valuePtr);
extern CFMutableDictionaryRef CFDictionaryCreateMutable(CFAllocatorRef allocator, CFIndex capacity,
    const void *keyCallBacks, const void *valueCallBacks);
extern CFIndex CFDictionaryGetCount(CFDictionaryRef theDict);
extern void CFDictionaryGetKeysAndValues(CFDictionaryRef theDict, const void **keys, const void **values);
extern const void *CFDictionaryGetValue(CFDictionaryRef theDict, const void *key);
extern void CFDictionarySetValue(CFMutableDictionaryRef theDict, const void *key, const void *value);
extern void CFDictionaryRemoveValue(CFMutableDictionaryRef theDict, const void *key);
extern CFPropertyListRef CFPropertyListCreateWithData(CFAllocatorRef allocator, CFDataRef data,
    CFOptionFlags options, CFIndex *format, void *error);
extern CFDataRef CFPropertyListCreateData(CFAllocatorRef allocator, CFPropertyListRef propertyList,
    CFIndex format, CFOptionFlags options, void *error);
extern void CFPreferencesSetAppValue(CFStringRef key, CFTypeRef value, CFStringRef applicationID);
extern Boolean CFPreferencesAppSynchronize(CFStringRef applicationID);

static void
xzs_log(const char *fmt, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (n <= 0) {
		return;
	}
	if (n >= (int)sizeof(buf)) {
		n = (int)sizeof(buf) - 1;
	}
	int fd = open("/dev/console", O_WRONLY | O_NOCTTY);
	if (fd >= 0) {
		(void)write(fd, buf, (size_t)n);
		close(fd);
	} else {
		(void)write(2, buf, (size_t)n);
	}
}

static bool
xzs_file_exists(const char *path)
{
	uint8_t st[256];
	return stat(path, st) == 0;
}

static CFDataRef
xzs_read_file_cfdata(const char *path)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		return NULL;
	}
	off_t sz = lseek(fd, 0, SEEK_END);
	if (sz <= 0 || sz > 16 * 1024 * 1024) {
		close(fd);
		return NULL;
	}
	(void)lseek(fd, 0, SEEK_SET);
	uint8_t *buf = (uint8_t *)malloc((size_t)sz);
	if (!buf) {
		close(fd);
		return NULL;
	}
	size_t total = 0;
	while (total < (size_t)sz) {
		ssize_t r = read(fd, buf + total, (size_t)sz - total);
		if (r <= 0) {
			break;
		}
		total += (size_t)r;
	}
	close(fd);
	CFDataRef data = NULL;
	if (total == (size_t)sz) {
		data = CFDataCreate(kCFAllocatorDefault, buf, (CFIndex)total);
	}
	free(buf);
	return data;
}

static bool
xzs_write_file_bytes(const char *path, const uint8_t *bytes, size_t len, mode_t mode, uid_t uid, gid_t gid)
{
	char tmp[256];
	snprintf(tmp, sizeof(tmp), "%s.xzstmp", path);
	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, mode);
	if (fd < 0) {
		return false;
	}
	size_t written = 0;
	while (written < len) {
		ssize_t w = write(fd, bytes + written, len - written);
		if (w <= 0) {
			close(fd);
			(void)unlink(tmp);
			return false;
		}
		written += (size_t)w;
	}
	close(fd);
	(void)chmod(tmp, mode);
	(void)chown(tmp, uid, gid);
	if (rename(tmp, path) != 0) {
		(void)unlink(tmp);
		return false;
	}
	(void)chmod(path, mode);
	(void)chown(path, uid, gid);
	return true;
}

static bool
xzs_write_plist_file(const char *path, CFPropertyListRef plist, mode_t mode, uid_t uid, gid_t gid)
{
	CFDataRef data = CFPropertyListCreateData(kCFAllocatorDefault, plist,
	    kCFPropertyListBinaryFormat_v1_0, 0, NULL);
	if (!data) {
		return false;
	}
	bool ok = xzs_write_file_bytes(path, CFDataGetBytePtr(data),
	    (size_t)CFDataGetLength(data), mode, uid, gid);
	CFRelease(data);
	return ok;
}

static void
xzs_dict_set_bool(CFMutableDictionaryRef dict, const char *key, bool val)
{
	CFStringRef k = CFStringCreateWithCString(kCFAllocatorDefault, key, kCFStringEncodingUTF8);
	if (k) {
		CFDictionarySetValue(dict, k, val ? kCFBooleanTrue : kCFBooleanFalse);
		CFRelease(k);
	}
}

static void
xzs_dict_set_str(CFMutableDictionaryRef dict, const char *key, const char *val)
{
	CFStringRef k = CFStringCreateWithCString(kCFAllocatorDefault, key, kCFStringEncodingUTF8);
	CFStringRef v = CFStringCreateWithCString(kCFAllocatorDefault, val, kCFStringEncodingUTF8);
	if (k && v) {
		CFDictionarySetValue(dict, k, v);
	}
	if (k) CFRelease(k);
	if (v) CFRelease(v);
}

static void
xzs_dict_set_int32(CFMutableDictionaryRef dict, const char *key, int32_t val)
{
	CFStringRef k = CFStringCreateWithCString(kCFAllocatorDefault, key, kCFStringEncodingUTF8);
	CFNumberRef n = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &val);
	if (k && n) {
		CFDictionarySetValue(dict, k, n);
	}
	if (k) CFRelease(k);
	if (n) CFRelease(n);
}

static void
xzs_dict_set_f64(CFMutableDictionaryRef dict, const char *key, double val)
{
	CFStringRef k = CFStringCreateWithCString(kCFAllocatorDefault, key, kCFStringEncodingUTF8);
	CFNumberRef n = CFNumberCreate(kCFAllocatorDefault, kCFNumberFloat64Type, &val);
	if (k && n) {
		CFDictionarySetValue(dict, k, n);
	}
	if (k) CFRelease(k);
	if (n) CFRelease(n);
}

static void
xzs_dict_remove_key(CFMutableDictionaryRef dict, const char *key)
{
	CFStringRef k = CFStringCreateWithCString(kCFAllocatorDefault, key, kCFStringEncodingUTF8);
	if (k) {
		CFDictionaryRemoveValue(dict, k);
		CFRelease(k);
	}
}

static const void *
xzs_dict_get_key(CFDictionaryRef dict, const char *key)
{
	CFStringRef k = CFStringCreateWithCString(kCFAllocatorDefault, key, kCFStringEncodingUTF8);
	if (!k) {
		return NULL;
	}
	const void *v = CFDictionaryGetValue(dict, k);
	CFRelease(k);
	return v;
}

static CFMutableDictionaryRef
xzs_load_or_create_mutable_dict(const char *path)
{
	CFDataRef data = xzs_read_file_cfdata(path);
	if (data) {
		CFPropertyListRef pl = CFPropertyListCreateWithData(kCFAllocatorDefault, data,
		    kCFPropertyListMutableContainersAndLeaves, NULL, NULL);
		CFRelease(data);
		if (pl && CFGetTypeID(pl) == CFDictionaryGetTypeID()) {
			return (CFMutableDictionaryRef)pl;
		}
		if (pl) {
			CFRelease(pl);
		}
	}
	return CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
	    &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
}

/* ============================================================================
 * 1. launchd_cache_loader: Prune launchd.plist, promote apps, reset caches
 * ============================================================================ */

static const char *const k_boot_jobs[] = {
	"com.apple.notifyd",
	"com.apple.cfprefsd.xpc.daemon",
	"com.apple.cfprefsd.xpc.daemon.system",
	"com.apple.logd",
	"com.apple.syslogd",
	"com.apple.distnoted.xpc.daemon",
	"com.apple.diagnosticd",
	"com.apple.fseventsd",
	"com.apple.containermanagerd",
	"com.apple.containermanagerd.system",
	"com.apple.runningboardd",
	"com.apple.backboardd",
	"com.apple.mobile.usermanagerd",
	"com.apple.mobile.keybagd",
	"com.apple.mobilegestalt.xpc",
	"com.apple.lsd",
	"com.apple.powerd",
	"com.apple.analyticsd",
	"com.apple.logd_helper",
	"com.apple.iconservices.iconservicesagent",
	"com.apple.pluginkit.pkd",
	"com.apple.SpringBoard",
	"com.apple.wifid",
};

static const char *const k_deny_substrings[] = {
	"watchdog", "CommCenter", "bluetooth", "BTServer", "cloudpaird",
	"WirelessRadioManager", "wirelessproxd", "locationd", "softwareupdate",
	"datamigrator", "backupd", "thermalmonitord", "nfcd", "seld",
	"biometrickitd", "pearld", "obliterat", "storage_mounter",
	"com.apple.fud", "sepd", "seputil", "camera", "ReportCrash",
	"osanalytics", "DumpPanic", "OTACrashCopier", "lockdown",
	"jetsamproperties", "purplebuddy", "restore", "usb", "accessoryd",
	"accessoryupdaterd", "mobileassetd", "bird", "homed", "routined",
	"duetexpertd", "com.apple.dmd", "syncdefaultsd", "photoanalysisd",
	"avconferenced", "driverkitd", "fairplayd", "nanoprefsyncd",
	"nanoregistry", "wcd", "atc", "ptpd", "remoted", "IOMFB_FDR_Loader",
	"passd", "assistantd", "voiced", "siri", "parsec", "remindd",
	"navd", "healthd", "imagent", "identityservicesd", "sharingd",
	"rapportd", "searchpartyd", "findmy", "biomed", "contextstored", "dasd"
};

static const char *const k_autostart_keys[] = {
	"RunAtLoad", "KeepAlive", "StartInterval", "StartCalendarInterval",
	"WatchPaths", "QueueDirectories", "StartOnMount", "LaunchEvents"
};

static bool
xzs_is_boot_job(const char *label)
{
	for (size_t i = 0; i < sizeof(k_boot_jobs) / sizeof(k_boot_jobs[0]); i++) {
		if (strcmp(label, k_boot_jobs[i]) == 0) {
			return true;
		}
	}
	return false;
}

static bool
xzs_is_denied_job(const char *path, const char *label)
{
	for (size_t i = 0; i < sizeof(k_deny_substrings) / sizeof(k_deny_substrings[0]); i++) {
		const char *w = k_deny_substrings[i];
		if (strstr(path, w) != NULL || strstr(label, w) != NULL) {
			return true;
		}
	}
	return false;
}

static void
xzs_prune_launchd_plist(void)
{
	const char *plist_path = "/System/Library/xpc/launchd.plist";
	const char *orig_path  = "/System/Library/xpc/launchd.plist.orig";
	const char *src_path   = xzs_file_exists(orig_path) ? orig_path : plist_path;

	CFDataRef raw = xzs_read_file_cfdata(src_path);
	if (!raw) {
		xzs_log("[XZS-INJECT] launchd_cache_loader: failed to read %s\n", src_path);
		return;
	}
	if (!xzs_file_exists(orig_path)) {
		(void)xzs_write_file_bytes(orig_path, CFDataGetBytePtr(raw),
		    (size_t)CFDataGetLength(raw), 0644, 0, 0);
	}

	CFPropertyListRef root = CFPropertyListCreateWithData(kCFAllocatorDefault, raw,
	    kCFPropertyListMutableContainersAndLeaves, NULL, NULL);
	CFRelease(raw);
	if (!root || CFGetTypeID(root) != CFDictionaryGetTypeID()) {
		if (root) CFRelease(root);
		return;
	}

	CFMutableDictionaryRef root_dict = (CFMutableDictionaryRef)root;
	CFMutableDictionaryRef daemons = (CFMutableDictionaryRef)xzs_dict_get_key(root_dict, "LaunchDaemons");
	if (!daemons || CFGetTypeID(daemons) != CFDictionaryGetTypeID()) {
		CFRelease(root);
		return;
	}

	CFIndex total = CFDictionaryGetCount(daemons);
	const void **keys = (const void **)malloc(sizeof(void *) * (size_t)total);
	const void **vals = (const void **)malloc(sizeof(void *) * (size_t)total);
	if (!keys || !vals) {
		if (keys) free(keys);
		if (vals) free(vals);
		CFRelease(root);
		return;
	}
	CFDictionaryGetKeysAndValues(daemons, keys, vals);

	uint32_t boot_cnt = 0, ondemand_cnt = 0, dropped_cnt = 0;

	for (CFIndex i = 0; i < total; i++) {
		CFStringRef k_cf = (CFStringRef)keys[i];
		CFMutableDictionaryRef job = (CFMutableDictionaryRef)vals[i];
		if (!k_cf || !job || CFGetTypeID(k_cf) != CFStringGetTypeID() ||
		    CFGetTypeID(job) != CFDictionaryGetTypeID()) {
			continue;
		}
		char path[256] = "";
		char label[256] = "";
		(void)CFStringGetCString(k_cf, path, sizeof(path), kCFStringEncodingUTF8);
		CFStringRef lbl_cf = (CFStringRef)xzs_dict_get_key(job, "Label");
		if (lbl_cf && CFGetTypeID(lbl_cf) == CFStringGetTypeID()) {
			(void)CFStringGetCString(lbl_cf, label, sizeof(label), kCFStringEncodingUTF8);
		}

		if (strncmp(path, "/System/Library/LaunchDaemons/", 30) != 0 &&
		    strncmp(path, "/System/Library/LaunchAngels/", 29) != 0) {
			CFDictionaryRemoveValue(daemons, k_cf);
			dropped_cnt++;
			continue;
		}

		const void *panic_val = xzs_dict_get_key(job, "_PanicOnCrash");
		if (panic_val) {
			xzs_dict_set_bool(job, "_PanicOnCrasX", true);
			xzs_dict_remove_key(job, "_PanicOnCrash");
		}
		xzs_dict_remove_key(job, "_LimitLoadToDeviceTree");

		if (xzs_is_boot_job(label)) {
			if (strcmp(label, "com.apple.lsd") == 0) {
				xzs_dict_set_bool(job, "RunAtLoad", true);
			}
			boot_cnt++;
			continue;
		}

		if (xzs_is_denied_job(path, label)) {
			CFDictionaryRemoveValue(daemons, k_cf);
			dropped_cnt++;
			continue;
		}

		const void *mach_svcs = xzs_dict_get_key(job, "MachServices");
		if (!mach_svcs || CFGetTypeID(mach_svcs) != CFDictionaryGetTypeID() ||
		    CFDictionaryGetCount((CFDictionaryRef)mach_svcs) == 0) {
			CFDictionaryRemoveValue(daemons, k_cf);
			dropped_cnt++;
			continue;
		}

		for (size_t k = 0; k < sizeof(k_autostart_keys) / sizeof(k_autostart_keys[0]); k++) {
			xzs_dict_remove_key(job, k_autostart_keys[k]);
		}
		ondemand_cnt++;
	}

	free(keys);
	free(vals);

	bool ok = xzs_write_plist_file(plist_path, root_dict, 0644, 0, 0);
	CFRelease(root);
	xzs_log("[XZS-INJECT] launchd_cache_loader: pruned launchd.plist (ok=%d boot=%u ondemand=%u dropped=%u)\n",
	    ok ? 1 : 0, boot_cnt, ondemand_cnt, dropped_cnt);
}

static void
xzs_unlink_matching_recursive(const char *dir_path, int depth)
{
	if (depth > 4) {
		return;
	}
	DIR *d = opendir(dir_path);
	if (!d) {
		return;
	}
	struct dirent *de;
	while ((de = readdir(d)) != NULL) {
		if (de->d_name[0] == '.' &&
		    (de->d_name[1] == '\0' || (de->d_name[1] == '.' && de->d_name[2] == '\0'))) {
			continue;
		}
		char child[512];
		snprintf(child, sizeof(child), "%s/%s", dir_path, de->d_name);
		if (de->d_type == DT_DIR) {
			xzs_unlink_matching_recursive(child, depth + 1);
		} else if (strstr(de->d_name, "LaunchServices") != NULL ||
		           strstr(de->d_name, ".csstore") != NULL ||
		           strstr(de->d_name, "lsdidentifiers") != NULL ||
		           strstr(de->d_name, "IconsCache") != NULL) {
			if (unlink(child) == 0) {
				xzs_log("[XZS-INJECT] unlinked stale cache: %s\n", child);
			}
		}
	}
	closedir(d);
}

static void
xzs_setup_preferences_and_apps(void)
{
	/* 1. Promote any remaining staged_system_apps to /Applications */
	uint32_t moved = 0, total_apps = 0;
	DIR *sd = opendir("/private/var/staged_system_apps");
	if (sd) {
		struct dirent *de;
		while ((de = readdir(sd)) != NULL) {
			size_t len = strlen(de->d_name);
			if (len > 4 && strcmp(de->d_name + len - 4, ".app") == 0) {
				char src[256], dst[256];
				snprintf(src, sizeof(src), "/private/var/staged_system_apps/%s", de->d_name);
				snprintf(dst, sizeof(dst), "/Applications/%s", de->d_name);
				if (rename(src, dst) == 0) {
					moved++;
				}
			}
		}
		closedir(sd);
	}
	DIR *ad = opendir("/Applications");
	if (ad) {
		struct dirent *de;
		while ((de = readdir(ad)) != NULL) {
			size_t len = strlen(de->d_name);
			if (len > 4 && strcmp(de->d_name + len - 4, ".app") == 0) {
				total_apps++;
			}
		}
		closedir(ad);
	}

	/* 2. Ensure /private/var/mobile/tmp and Preferences exist */
	(void)mkdir("/private/var/mobile/tmp", 0700);
	(void)chown("/private/var/mobile/tmp", 501, 501);
	(void)mkdir("/private/var/mobile/Library", 0755);
	(void)chown("/private/var/mobile/Library", 501, 501);
	(void)mkdir("/private/var/mobile/Library/Preferences", 0755);
	(void)chown("/private/var/mobile/Library/Preferences", 501, 501);

	/* com.apple.springboard.plist */
	{
		const char *p = "/private/var/mobile/Library/Preferences/com.apple.springboard.plist";
		CFMutableDictionaryRef d = xzs_load_or_create_mutable_dict(p);
		if (d) {
			xzs_dict_set_bool(d, "BSUIVibrancyEffectViewDisabled", true);
			xzs_dict_set_bool(d, "BKNoWatchdogs", true);
			xzs_dict_set_f64(d, "FBLaunchWatchdogScale", 20.0);
			xzs_dict_set_bool(d, "SBShowBatteryPercentage", true);
			xzs_dict_set_bool(d, "MCForceShowCloudConfigurationUI", false);
			(void)xzs_write_plist_file(p, d, 0600, 501, 501);
			CFRelease(d);
		}
	}
	/* com.apple.frontboard.plist */
	{
		const char *p = "/private/var/mobile/Library/Preferences/com.apple.frontboard.plist";
		CFMutableDictionaryRef d = xzs_load_or_create_mutable_dict(p);
		if (d) {
			xzs_dict_set_f64(d, "FBLaunchWatchdogScale", 20.0);
			(void)xzs_write_plist_file(p, d, 0600, 501, 501);
			CFRelease(d);
		}
	}
	/* com.apple.VideoProcessing.plist */
	{
		const char *p = "/private/var/mobile/Library/Preferences/com.apple.VideoProcessing.plist";
		CFMutableDictionaryRef d = xzs_load_or_create_mutable_dict(p);
		if (d) {
			xzs_dict_set_str(d, "threading", "auto");
			xzs_dict_set_str(d, "max-threads", "7");
			(void)xzs_write_plist_file(p, d, 0600, 501, 501);
			CFRelease(d);
		}
	}
	/* com.apple.coremedia.plist */
	{
		const char *p = "/private/var/mobile/Library/Preferences/com.apple.coremedia.plist";
		CFMutableDictionaryRef d = xzs_load_or_create_mutable_dict(p);
		if (d) {
			xzs_dict_set_int32(d, "wirelessdisplay_max_count", 0);
			(void)xzs_write_plist_file(p, d, 0600, 501, 501);
			CFRelease(d);
		}
	}

	/* 3. One-time reset of stale 1-page IconState.plist & lsd .csstore caches */
	const char *marker = "/private/var/db/.xzs_userland_v4_done";
	if (moved > 0 || !xzs_file_exists(marker)) {
		(void)unlink("/private/var/mobile/Library/SpringBoard/IconState.plist");
		(void)unlink("/private/var/mobile/Library/SpringBoard/DesiredIconState.plist");
		(void)unlink("/private/var/mobile/Library/SpringBoard/IconSupportState.plist");

		xzs_unlink_matching_recursive("/private/var/db/lsd", 0);
		xzs_unlink_matching_recursive("/private/var/mobile/Library/Caches", 0);
		xzs_unlink_matching_recursive("/private/var/containers/Shared/SystemGroup", 0);
		xzs_unlink_matching_recursive("/private/var/containers/Data/System", 0);

		uint8_t one = '1';
		(void)xzs_write_file_bytes(marker, &one, 1, 0644, 0, 0);
		(void)xzs_write_file_bytes("/private/var/db/.xzs_sb_rebuild_delay", &one, 1, 0644, 0, 0);
		xzs_log("[XZS-INJECT] launchd_cache_loader: reset IconState & lsd caches (moved=%u total_apps=%u)\n",
		    moved, total_apps);
	} else {
		xzs_log("[XZS-INJECT] launchd_cache_loader: prefs updated (total_apps=%u)\n", total_apps);
	}
}

/* ============================================================================
 * 2. containermanagerd: Disable personas (saves 20s stall on app/widget launch)
 * ============================================================================ */

static BOOL
xzs_personas_are_supported(__unused id self, __unused SEL _cmd)
{
	return NO;
}

static void
xzs_init_containermanagerd(void)
{
	Class cls = objc_getClass("MCMUserIdentityCache");
	Method m = cls ? class_getClassMethod(cls, sel_registerName("personasAreSupported")) : NULL;
	if (m) {
		method_setImplementation(m, (IMP)xzs_personas_are_supported);
		xzs_log("[XZS-INJECT] %s[%d]: +[MCMUserIdentityCache personasAreSupported] -> NO\n",
		    getprogname(), getpid());
	}
}

/* ============================================================================
 * 3. SpringBoard: White Lock Screen clock, JPEG snapshots, 2-page lsd sync
 * ============================================================================ */

static void (*g_orig_set_setup_required_reason)(id self, SEL _cmd, long reason);
static bool g_siri_orb_hooked = false;

static void
xzs_sb_setup_activation_state(id self, __unused SEL _cmd)
{
	*(int *)((char *)self + 8) = 2; /* LOCKDOWN_ACTIVATED */
}

static void
xzs_sb_set_setup_required_reason(id self, SEL _cmd, __unused long reason)
{
	if (g_orig_set_setup_required_reason) {
		g_orig_set_setup_required_reason(self, _cmd, 0);
	}
}

static long
xzs_sb_snapshot_output_format(__unused id self, __unused SEL _cmd, id snapshot)
{
	SEL opaque_sel = sel_registerName("isImageOpaque");
	if (snapshot != NULL &&
	    class_getInstanceMethod(object_getClass(snapshot), opaque_sel) != NULL &&
	    ((BOOL (*)(id, SEL))objc_msgSend)(snapshot, opaque_sel)) {
		return 3; /* SNAPSHOT_FORMAT_JPEG */
	}
	return 0; /* SNAPSHOT_FORMAT_PNG */
}

static BOOL
xzs_sb_poster_switcher_allowed(__unused id self, __unused SEL _cmd, __unused id *explanation)
{
	return NO;
}

static void
xzs_sb_switcher_wallpaper_overlay(__unused id self, __unused SEL _cmd)
{
	/* Leave out placeholder magenta wash over App Switcher cards */
}

static void
xzs_sb_prewarm_orb_view_shaders(__unused id self, __unused SEL _cmd)
{
	/* Skip Siri orb Metal shader prewarm */
}

static void
xzs_sb_image_added(__unused const struct mach_header *mh, __unused intptr_t slide)
{
	if (g_siri_orb_hooked) {
		return;
	}
	Class cls = objc_getClass("SiriPresentationViewController");
	if (!cls) {
		return;
	}
	g_siri_orb_hooked = true;
	Method m = class_getInstanceMethod(cls, sel_registerName("prewarmOrbViewShaders"));
	if (m) {
		method_setImplementation(m, (IMP)xzs_sb_prewarm_orb_view_shaders);
	}
}

static void
xzs_init_springboard(void)
{
	/* Force plain white vibrancy on Lock Screen clock (fixes red alpha mask on SW renderer) */
	CFStringRef key = CFStringCreateWithCString(kCFAllocatorDefault,
	    "BSUIVibrancyEffectViewDisabled", kCFStringEncodingUTF8);
	if (key) {
		CFPreferencesSetAppValue(key, kCFBooleanTrue, kCFPreferencesCurrentApplication);
		(void)CFPreferencesAppSynchronize(kCFPreferencesCurrentApplication);
		CFRelease(key);
	}

	Class cls_lockdown = objc_getClass("SBLockdownManager");
	if (cls_lockdown) {
		Method m = class_getInstanceMethod(cls_lockdown, sel_registerName("_setupActivationState"));
		if (m) method_setImplementation(m, (IMP)xzs_sb_setup_activation_state);
	}

	Class cls_setup = objc_getClass("SBSetupManager");
	if (cls_setup) {
		Method m = class_getInstanceMethod(cls_setup, sel_registerName("_setSetupRequiredReason:"));
		if (m) {
			g_orig_set_setup_required_reason = (void (*)(id, SEL, long))method_getImplementation(m);
			method_setImplementation(m, (IMP)xzs_sb_set_setup_required_reason);
		}
	}

	Class cls_snap = objc_getClass("XBApplicationSnapshotManifestImpl");
	if (cls_snap) {
		Method m = class_getClassMethod(cls_snap, sel_registerName("_outputFormatForSnapshot:"));
		if (m) method_setImplementation(m, (IMP)xzs_sb_snapshot_output_format);
	}

	Class cls_policy = objc_getClass("SBMainDisplayPolicyAggregator");
	if (cls_policy) {
		Method m = class_getInstanceMethod(cls_policy,
		    sel_registerName("_allowsCapabilityPosterSwitcherWithExplanation:"));
		if (m) method_setImplementation(m, (IMP)xzs_sb_poster_switcher_allowed);
	}

	Class cls_switcher = objc_getClass("SBAppSwitcherPageView");
	if (cls_switcher) {
		Method m = class_getInstanceMethod(cls_switcher,
		    sel_registerName("_createOrDestroyWallpaperOverlayView"));
		if (m) method_setImplementation(m, (IMP)xzs_sb_switcher_wallpaper_overlay);
	}

	_dyld_register_func_for_add_image(xzs_sb_image_added);

	/*
	 * Give lsd time to finish registering all 38 /Applications/<app> bundles
	 * before SpringBoard builds the 2-page Home Screen icon layout.
	 */
	const char *delay_marker = "/private/var/db/.xzs_sb_rebuild_delay";
	if (xzs_file_exists(delay_marker)) {
		(void)unlink(delay_marker);
		xzs_log("[XZS-INJECT] SpringBoard[%d]: hooks active (plain_vibrancy=1, JPEG snapshots=1); waiting 4.0s for lsd rebuild\n",
		    getpid());
		usleep(4000000);
	} else {
		xzs_log("[XZS-INJECT] SpringBoard[%d]: hooks active (plain_vibrancy=1, JPEG snapshots=1); waiting 2.0s for lsd\n",
		    getpid());
		usleep(2000000);
	}
}

/* ============================================================================
 * 4. backboardd: Fast-fail EAGLContext when using SW renderer
 * ============================================================================ */

static bool g_eagl_hooked = false;

static id
xzs_eagl_init_with_api(__unused id self, __unused SEL _cmd, __unused unsigned long api, __unused id props)
{
	return NULL;
}

static void
xzs_bb_image_added(__unused const struct mach_header *mh, __unused intptr_t slide)
{
	if (g_eagl_hooked) {
		return;
	}
	Class cls = objc_getClass("EAGLContext");
	if (!cls) {
		return;
	}
	g_eagl_hooked = true;
	Method m = class_getInstanceMethod(cls, sel_registerName("initWithAPI:properties:"));
	if (m) {
		method_setImplementation(m, (IMP)xzs_eagl_init_with_api);
		xzs_log("[XZS-INJECT] backboardd[%d]: -[EAGLContext initWithAPI:properties:] fast-fail hooked\n",
		    getpid());
	}
}

static void
xzs_init_backboardd(void)
{
	char bootargs[512] = "";
	size_t sz = sizeof(bootargs) - 1;
	if (sysctlbyname("kern.bootargs", bootargs, &sz, NULL, 0) == 0 &&
	    strstr(bootargs, "xzsgpu_agx=1") != NULL) {
		return;
	}
	_dyld_register_func_for_add_image(xzs_bb_image_added);
}

/* ============================================================================
 * 5. mediaserverd: Skip portrait filter GL prewarm so HEIC wallpaper decodes
 * ============================================================================ */

static bool g_media_hooked = false;

static void
xzs_media_prewarm_portrait_filter(__unused id self, __unused SEL _cmd, __unused int ver, __unused BOOL styles)
{
	/* No-op: prevents mediaserverd crash in CoreImage GL context prewarm */
}

static void
xzs_media_image_added(__unused const struct mach_header *mh, __unused intptr_t slide)
{
	if (g_media_hooked) {
		return;
	}
	Class cls = objc_getClass("BWCoreImageFilterRenderer");
	if (!cls) {
		return;
	}
	g_media_hooked = true;
	Method m = class_getClassMethod(cls,
	    sel_registerName("prewarmPortraitFilterVersion:semanticStyleFilters:"));
	if (m) {
		method_setImplementation(m, (IMP)xzs_media_prewarm_portrait_filter);
		xzs_log("[XZS-INJECT] mediaserverd[%d]: +[BWCoreImageFilterRenderer prewarmPortraitFilterVersion:semanticStyleFilters:] hooked\n",
		    getpid());
	}
}

static void
xzs_init_mediaserverd(void)
{
	_dyld_register_func_for_add_image(xzs_media_image_added);
}

/* ============================================================================
 * Constructor Entry Point
 * ============================================================================ */

__attribute__((constructor))
static void
xzs_inject_init(void)
{
	const char *prog = getprogname();
	if (!prog) {
		return;
	}
	if (strcmp(prog, "tzinit") == 0) {
		xzs_prune_launchd_plist();
		xzs_setup_preferences_and_apps();
	} else if (strncmp(prog, "containermanagerd", 17) == 0) {
		xzs_init_containermanagerd();
	} else if (strcmp(prog, "SpringBoard") == 0) {
		xzs_init_springboard();
	} else if (strcmp(prog, "backboardd") == 0) {
		xzs_init_backboardd();
	} else if (strcmp(prog, "mediaserverd") == 0) {
		xzs_init_mediaserverd();
	}
}
