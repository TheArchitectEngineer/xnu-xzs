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
extern int dup2(int fd1, int fd2);
extern void _dyld_register_func_for_add_image(void (*func)(const struct mach_header *mh, intptr_t vmaddr_slide));

typedef uint64_t dispatch_time_t;
#define DISPATCH_TIME_NOW 0ULL
#define NSEC_PER_SEC 1000000000ULL
extern struct dispatch_queue_s { uint8_t _opaque[128]; } _dispatch_main_q;
extern dispatch_time_t dispatch_time(dispatch_time_t when, int64_t delta);
extern void dispatch_after_f(dispatch_time_t when, void *queue, void *context, void (*work)(void *));

/* libobjc.A.dylib */
extern Class objc_getClass(const char *name);
extern Class object_getClass(id obj);
extern const char *object_getClassName(id obj);
extern SEL sel_registerName(const char *str);
extern Method class_getInstanceMethod(Class cls, SEL name);
extern Method class_getClassMethod(Class cls, SEL name);
extern BOOL class_respondsToSelector(Class cls, SEL sel);
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
	"com.apple.containermanagerd_system",
	"com.apple.runningboardd",
	"com.apple.backboardd",
	"com.apple.mobile.usermanagerd",
	"com.apple.usermanagerd",
	"com.apple.mobile.keybagd",
	"com.apple.keybagd",
	"com.apple.mobilegestalt.xpc",
	"com.apple.lsd",
	"com.apple.powerd",
	"com.apple.analyticsd",
	"com.apple.logd_helper",
	"com.apple.iconservices.iconservicesagent",
	"com.apple.pluginkit.pkd",
	"com.apple.SpringBoard",
};

static const char *const k_deny_substrings[] = {
	"watchdog", "CommCenter", "wifi", "WiFi", "WirelessRadioManager",
	"wirelessproxd", "bluetooth", "BTServer", "cloudpaird", "locationd",
	"softwareupdate", "datamigrator", "backupd", "thermalmonitord",
	"nfcd", "seld", "biometrickitd", "pearld", "obliterat",
	"storage_mounter", "com.apple.fud", "sepd", "seputil", "camera",
	"ReportCrash", "ReportMemoryException", "Spindump", "TailSpin",
	"osanalytics", "DumpPanic", "OTACrashCopier",
	"lockdown", "audio", "jetsamproperties", "purplebuddy", "restore",
	"usb", "accessoryd", "accessoryupdaterd", "Accessories", "BlueTool",
	"mobileassetd", "bird", "homed", "routined", "duetexpertd",
	"com.apple.dmd", "syncdefaultsd", "photoanalysisd", "avconferenced",
	"driverkitd", "fairplayd", "corespeechd", "wifianalyticsd", "carkitd",
	"awdd", "nanoprefsyncd", "nanoregistry", "nano", "wcd", "atc", "ptpd",
	"remoted", "IOMFB_FDR_Loader",
	/* Prevent UserEventAgent & background widget/sync daemons from auto-spawning at boot: */
	"UserEventAgent", "dasd", "biomed", "biomesyncd", "contextstored",
	"coreduetd", "rapportd", "sharingd", "assistantd", "voiced",
	"imagent", "searchd", "spotlight", "parsec", "proactiveeventtrackerd",
	"UsageTrackingAgent", "ScreenTimeAgent", "SafariBookmarksSyncAgent",
	"healthd", "chronod", "PosterBoard", "mediaanalysisd", "suggestd",
	"cloudd", "passd", "identityservicesd", "apsd", "triald", "fmfd",
	"CallHistorySyncHelper", "amsaccountsd", "amsengagementd",
	"appleaccountd", "adid", "adprivacyd", "rtcreportingd", "symptomsd",
	"aggregated", "destinationd", "navd", "geod", "vmd", "sleepd",
	"sociallayerd", "familycircled", "askpermissiond", "followupd",
	"cdpd", "coreidvd", "transparencyd", "safetyalertsd",
	"liveactivitiesd", "donotdisturbd", "financed", "useractivityd",
	"contactsdonationagent", "ScreenTime", "callservicesd", "GSSCred",
	"pfd", "deleted", "FTConversationService", "findmydeviced",
	"searchpartyd", "siri", "Siri", "proactive", "duet", "biome"
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
			if (strcmp(label, "com.apple.lsd") == 0 ||
			    strcmp(label, "com.apple.iconservices.iconservicesagent") == 0 ||
			    strcmp(label, "com.apple.pluginkit.pkd") == 0) {
				xzs_dict_set_bool(job, "RunAtLoad", true);
			}
			boot_cnt++;
			continue;
		}

		if (xzs_is_denied_job(path, label) ||
		    strcmp(label, "com.apple.AggregateDictionary.addaily") == 0) {
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
	if (depth > 9) {
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
		if (strcmp(de->d_name, "staged_system_apps") == 0 ||
		    strcmp(de->d_name, "Bins") == 0) {
			continue;
		}
		char child[512];
		snprintf(child, sizeof(child), "%s/%s", dir_path, de->d_name);
		if (de->d_type == DT_DIR || de->d_type == 0) {
			xzs_unlink_matching_recursive(child, depth + 1);
		}
		if (strstr(de->d_name, "LaunchServices") != NULL ||
		    strstr(de->d_name, ".csstore") != NULL ||
		    strstr(de->d_name, "lsdidentifiers") != NULL ||
		    strstr(de->d_name, "IconsCache") != NULL ||
		    strstr(de->d_name, "SystemAppInstallState.plist") != NULL ||
		    strstr(de->d_name, "UninstalledApplications.plist") != NULL ||
		    strstr(de->d_name, "LastLaunchServicesMap.plist") != NULL ||
		    strstr(de->d_name, "LastBuildInfo.plist") != NULL ||
		    strstr(de->d_name, "IconState.plist") != NULL ||
		    strstr(de->d_name, "DesiredIconState.plist") != NULL ||
		    strstr(de->d_name, "IconSupportState.plist") != NULL) {
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
	/* 0. Disable /System/Library/AppPlaceholders, create SAD canary, and disable chronod widget storm */
	(void)rename("/System/Library/AppPlaceholders", "/System/Library/AppPlaceholders.disabled");
	uint8_t sad_one = '1';
	(void)xzs_write_file_bytes("/System/Library/disableSystemAppDeletion", &sad_one, 1, 0644, 0, 0);
	(void)rename("/System/Library/PrivateFrameworks/DataMigration.framework/XPCServices/com.apple.datamigrator.xpc.disabled",
	    "/System/Library/PrivateFrameworks/DataMigration.framework/XPCServices/com.apple.datamigrator.xpc");
	(void)rename("/System/Library/PrivateFrameworks/ChronoCore.framework/Support/chronod",
	    "/System/Library/PrivateFrameworks/ChronoCore.framework/Support/chronod.disabled");

	/* Restore any entries moved to /private/var/containers/Data/System.old */
	(void)mkdir("/private/var/containers", 0755);
	(void)mkdir("/private/var/containers/Data", 0755);
	(void)mkdir("/private/var/containers/Data/System", 0755);
	DIR *old_sys = opendir("/private/var/containers/Data/System.old");
	if (old_sys) {
		struct dirent *de;
		while ((de = readdir(old_sys)) != NULL) {
			if (de->d_name[0] == '.') {
				continue;
			}
			char src[256], dst[256];
			snprintf(src, sizeof(src), "/private/var/containers/Data/System.old/%s", de->d_name);
			snprintf(dst, sizeof(dst), "/private/var/containers/Data/System/%s", de->d_name);
			(void)rename(src, dst);
		}
		closedir(old_sys);
	}

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

	/* 2. Ensure /private/var/mobile/tmp, SpringBoard, and Preferences exist */
	(void)mkdir("/private/var/mobile/tmp", 0700);
	(void)chown("/private/var/mobile/tmp", 501, 501);
	(void)mkdir("/private/var/mobile/Library", 0755);
	(void)chown("/private/var/mobile/Library", 501, 501);
	(void)mkdir("/private/var/mobile/Library/SpringBoard", 0755);
	(void)chown("/private/var/mobile/Library/SpringBoard", 501, 501);
	(void)mkdir("/private/var/mobile/Library/Preferences", 0755);
	(void)chown("/private/var/mobile/Library/Preferences", 501, 501);

	/* com.apple.migration.plist — prevent datamigrator from running on boot */
	{
		static const char *const mig_paths[] = {
			"/private/var/mobile/Library/Preferences/com.apple.migration.plist",
			"/private/var/root/Library/Preferences/com.apple.migration.plist",
			"/Library/Preferences/com.apple.migration.plist"
		};
		(void)mkdir("/private/var/root", 0755);
		(void)mkdir("/private/var/root/Library", 0755);
		(void)mkdir("/private/var/root/Library/Preferences", 0755);
		for (size_t mi = 0; mi < sizeof(mig_paths) / sizeof(mig_paths[0]); mi++) {
			const char *p = mig_paths[mi];
			uid_t owner = (mi == 0) ? 501 : 0;
			CFMutableDictionaryRef d = xzs_load_or_create_mutable_dict(p);
			if (d) {
				xzs_dict_set_str(d, "LastSystemVersion", "20H392");
				CFMutableDictionaryRef res = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
				    &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
				if (res) {
					xzs_dict_set_str(res, "buildVersion", "20H392");
					xzs_dict_set_bool(res, "success", true);
					CFStringRef rk = CFStringCreateWithCString(kCFAllocatorDefault,
					    "DMLastMigrationResults", kCFStringEncodingUTF8);
					if (rk) {
						CFDictionarySetValue(d, rk, res);
						CFRelease(rk);
					}
					CFRelease(res);
				}
				CFMutableDictionaryRef disp = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
				    &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
				if (disp) {
					xzs_dict_set_str(disp, "buildVersion", "20H392");
					xzs_dict_set_bool(disp, "SyntheticDidUpgrade", false);
					CFStringRef dk = CFStringCreateWithCString(kCFAllocatorDefault,
					    "DMUserDataDisposition", kCFStringEncodingUTF8);
					if (dk) {
						CFDictionarySetValue(d, dk, disp);
						CFRelease(dk);
					}
					CFRelease(disp);
				}
				(void)xzs_write_plist_file(p, d, 0644, owner, owner);
				CFRelease(d);
			}
		}
	}
	/* com.apple.purplebuddy.plist — mark Setup Assistant complete */
	{
		const char *p = "/private/var/mobile/Library/Preferences/com.apple.purplebuddy.plist";
		CFMutableDictionaryRef d = xzs_load_or_create_mutable_dict(p);
		if (d) {
			xzs_dict_set_bool(d, "SetupDone", true);
			xzs_dict_set_bool(d, "SetupFinishedAllSteps", true);
			xzs_dict_set_int32(d, "SetupVersion", 11);
			xzs_dict_set_str(d, "SetupState", "SetupUsingAssistant");
			xzs_dict_set_bool(d, "ForceNoBuddy", true);
			(void)xzs_write_plist_file(p, d, 0644, 501, 501);
			CFRelease(d);
		}
	}
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
			(void)xzs_write_plist_file(p, d, 0644, 501, 501);
			CFRelease(d);
		}
	}
	/* com.apple.frontboard.plist */
	{
		const char *p = "/private/var/mobile/Library/Preferences/com.apple.frontboard.plist";
		CFMutableDictionaryRef d = xzs_load_or_create_mutable_dict(p);
		if (d) {
			xzs_dict_set_f64(d, "FBLaunchWatchdogScale", 20.0);
			(void)xzs_write_plist_file(p, d, 0644, 501, 501);
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
			(void)xzs_write_plist_file(p, d, 0644, 501, 501);
			CFRelease(d);
		}
	}
	/* com.apple.coremedia.plist */
	{
		const char *p = "/private/var/mobile/Library/Preferences/com.apple.coremedia.plist";
		CFMutableDictionaryRef d = xzs_load_or_create_mutable_dict(p);
		if (d) {
			xzs_dict_set_int32(d, "wirelessdisplay_max_count", 0);
			(void)xzs_write_plist_file(p, d, 0644, 501, 501);
			CFRelease(d);
		}
	}

	/* 3. Write default 2-page IconState.plist / DesiredIconState.plist & reset stale lsd caches */
	const char *marker = "/private/var/db/.xzs_userland_v9_done";
	const char *icon_marker = "/private/var/db/.xzs_iconstate_v10_done";
	if (moved > 0 || !xzs_file_exists(marker) || !xzs_file_exists(icon_marker) ||
	    !xzs_file_exists("/private/var/mobile/Library/SpringBoard/IconState.plist")) {
		if (moved > 0 || !xzs_file_exists(marker)) {
			xzs_unlink_matching_recursive("/private/var", 0);
		}
		(void)unlink("/private/var/mobile/Library/SpringBoard/IconSupportState.plist");
		(void)unlink("/private/var/mobile/Library/SpringBoard/TodayViewArchive.plist");

		static const char k_icon_state_xml[] =
		    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		    "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
		    "<plist version=\"1.0\">\n"
		    "<dict>\n"
		    "\t<key>buttonBar</key>\n"
		    "\t<array>\n"
		    "\t\t<string>com.apple.mobilephone</string>\n"
		    "\t\t<string>com.apple.mobilesafari</string>\n"
		    "\t\t<string>com.apple.MobileSMS</string>\n"
		    "\t\t<string>com.apple.Music</string>\n"
		    "\t</array>\n"
		    "\t<key>iconLists</key>\n"
		    "\t<array>\n"
		    "\t\t<array>\n"
		    "\t\t\t<string>com.apple.facetime</string>\n"
		    "\t\t\t<string>com.apple.mobilecal</string>\n"
		    "\t\t\t<string>com.apple.mobileslideshow</string>\n"
		    "\t\t\t<string>com.apple.camera</string>\n"
		    "\t\t\t<string>com.apple.mobilemail</string>\n"
		    "\t\t\t<string>com.apple.mobiletimer</string>\n"
		    "\t\t\t<string>com.apple.Maps</string>\n"
		    "\t\t\t<string>com.apple.weather</string>\n"
		    "\t\t\t<string>com.apple.reminders</string>\n"
		    "\t\t\t<string>com.apple.mobilenotes</string>\n"
		    "\t\t\t<string>com.apple.stocks</string>\n"
		    "\t\t\t<string>com.apple.iBooks</string>\n"
		    "\t\t\t<string>com.apple.AppStore</string>\n"
		    "\t\t\t<string>com.apple.podcasts</string>\n"
		    "\t\t\t<string>com.apple.tv</string>\n"
		    "\t\t\t<string>com.apple.Health</string>\n"
		    "\t\t\t<string>com.apple.Home</string>\n"
		    "\t\t\t<string>com.apple.Passbook</string>\n"
		    "\t\t\t<string>com.apple.Preferences</string>\n"
		    "\t\t\t<string>com.apple.DocumentsApp</string>\n"
		    "\t\t</array>\n"
		    "\t\t<array>\n"
		    "\t\t\t<string>com.apple.calculator</string>\n"
		    "\t\t\t<string>com.apple.compass</string>\n"
		    "\t\t\t<string>com.apple.measure</string>\n"
		    "\t\t\t<string>com.apple.VoiceMemos</string>\n"
		    "\t\t\t<string>com.apple.MobileAddressBook</string>\n"
		    "\t\t\t<string>com.apple.findmy</string>\n"
		    "\t\t\t<string>com.apple.shortcuts</string>\n"
		    "\t\t\t<string>com.apple.Translate</string>\n"
		    "\t\t\t<string>com.apple.freeform</string>\n"
		    "\t\t\t<string>com.apple.tips</string>\n"
		    "\t\t\t<string>com.apple.MobileStore</string>\n"
		    "\t\t\t<string>com.apple.Bridge</string>\n"
		    "\t\t\t<string>com.apple.Fitness</string>\n"
		    "\t\t\t<string>com.apple.Magnifier</string>\n"
		    "\t\t\t<string>com.apple.news</string>\n"
		    "\t\t</array>\n"
		    "\t</array>\n"
		    "</dict>\n"
		    "</plist>\n";

		(void)mkdir("/private/var/mobile/Library/SpringBoard", 0755);
		(void)chown("/private/var/mobile/Library/SpringBoard", 501, 501);

		CFDataRef xml_data = CFDataCreate(kCFAllocatorDefault,
		    (const uint8_t *)k_icon_state_xml, (CFIndex)(sizeof(k_icon_state_xml) - 1));
		if (xml_data) {
			CFPropertyListRef plist = CFPropertyListCreateWithData(kCFAllocatorDefault,
			    xml_data, 0, NULL, NULL);
			if (plist) {
				(void)xzs_write_plist_file("/private/var/mobile/Library/SpringBoard/IconState.plist",
				    plist, 0644, 501, 501);
				(void)xzs_write_plist_file("/private/var/mobile/Library/SpringBoard/DesiredIconState.plist",
				    plist, 0644, 501, 501);
				CFRelease(plist);
			}
			CFRelease(xml_data);
		}

		uint8_t one = '1';
		(void)xzs_write_file_bytes(marker, &one, 1, 0644, 0, 0);
		(void)xzs_write_file_bytes(icon_marker, &one, 1, 0644, 0, 0);
		xzs_log("[XZS-INJECT] tzinit: wrote 2-page IconState.plist & DesiredIconState.plist (moved=%u total_apps=%u)\n",
		    moved, total_apps);
	} else {
		xzs_log("[XZS-INJECT] tzinit: prefs updated (total_apps=%u)\n", total_apps);
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
xzs_sb_poster_switcher_allowed(__unused id self, __unused SEL _cmd, id *explanation)
{
	if (explanation != NULL) {
		static CFStringRef s_exp = NULL;
		if (!s_exp) {
			s_exp = CFStringCreateWithCString(kCFAllocatorDefault,
			    "PosterSwitcher disabled on XZS SW renderer", kCFStringEncodingUTF8);
		}
		*explanation = (id)s_exp;
	}
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

typedef struct {
	double x, y;
} xzs_cgpoint_t;

typedef struct {
	double width, height;
} xzs_cgsize_t;

typedef struct {
	xzs_cgpoint_t origin;
	xzs_cgsize_t size;
} xzs_cgrect_t;

static id g_xzs_wallpaper_uiimage = NULL;

static uint8_t
xzs_clamp_byte(int v)
{
	return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

static id
xzs_get_or_create_wallpaper_image(void)
{
	if (g_xzs_wallpaper_uiimage != NULL) {
		return g_xzs_wallpaper_uiimage;
	}
	Class cls_data = objc_getClass("NSData");
	Class cls_img = objc_getClass("UIImage");
	SEL sel_data = sel_registerName("dataWithBytes:length:");
	SEL sel_img = sel_registerName("imageWithData:");
	if (!cls_data || !cls_img ||
	    !class_getClassMethod(cls_data, sel_data) ||
	    !class_getClassMethod(cls_img, sel_img)) {
		return NULL;
	}

	const int w = 375;
	const int h = 812;
	const size_t pix_bytes = (size_t)w * (size_t)h * 4u;
	const size_t bmp_size = 54u + pix_bytes;
	uint8_t *bmp = (uint8_t *)malloc(bmp_size);
	if (!bmp) {
		return NULL;
	}
	memset(bmp, 0, 54u);
	/* 54-byte top-down 32-bpp BGRA8888 BMP header */
	bmp[0] = 'B';
	bmp[1] = 'M';
	uint32_t fsz = (uint32_t)bmp_size;
	uint32_t off = 54u;
	uint32_t hdr_sz = 40u;
	int32_t bw = w;
	int32_t bh = -h; /* negative height = top-down scanlines */
	uint16_t planes = 1u;
	uint16_t bpp = 32u;
	memcpy(bmp + 2, &fsz, 4);
	memcpy(bmp + 10, &off, 4);
	memcpy(bmp + 14, &hdr_sz, 4);
	memcpy(bmp + 18, &bw, 4);
	memcpy(bmp + 22, &bh, 4);
	memcpy(bmp + 26, &planes, 2);
	memcpy(bmp + 28, &bpp, 2);

	/* Render iOS 16 WWDC 2022 iconic teal / cyan / gold / deep-indigo wave arches */
	uint32_t *px = (uint32_t *)(bmp + 54u);
	for (int y = 0; y < h; y++) {
		int yn = (y * 256) / h; /* 0..255 */
		for (int x = 0; x < w; x++) {
			int xn = (x * 256) / w; /* 0..255 */
			/* Base sky-to-ocean diagonal gradient */
			int r = 12 + (yn * 22) / 256 + (xn * 18) / 256;
			int g = 95 + ((256 - yn) * 85) / 256 + (xn * 25) / 256;
			int b = 185 + ((256 - yn) * 55) / 256;

			/* Arch 1: Upper-left cyan/turquoise wave */
			int dx1 = x - 65;
			int dy1 = y - 240;
			int d1 = (dx1 * dx1 + dy1 * dy1) >> 8; /* scaled distance^2 */
			if (d1 < 360) {
				int w1 = 360 - d1;
				r = (r * (360 - w1) + 18 * w1) / 360;
				g = (g * (360 - w1) + 212 * w1) / 360;
				b = (b * (360 - w1) + 235 * w1) / 360;
			}

			/* Arch 2: Center-right warm golden/amber WWDC22 ribbon */
			int dx2 = x - 295;
			int dy2 = y - 430;
			int d2 = (dx2 * dx2 + dy2 * dy2) >> 8;
			if (d2 < 320) {
				int w2 = 320 - d2;
				r = (r * (320 - w2) + 242 * w2) / 320;
				g = (g * (320 - w2) + 196 * w2) / 320;
				b = (b * (320 - w2) + 68 * w2) / 320;
			}

			/* Arch 3: Lower-left rich royal blue / teal wave */
			int dx3 = x - 95;
			int dy3 = y - 640;
			int d3 = (dx3 * dx3 + dy3 * dy3) >> 8;
			if (d3 < 400) {
				int w3 = 400 - d3;
				r = (r * (400 - w3) + 24 * w3) / 400;
				g = (g * (400 - w3) + 138 * w3) / 400;
				b = (b * (400 - w3) + 228 * w3) / 400;
			}

			uint8_t rb = xzs_clamp_byte(r);
			uint8_t gb = xzs_clamp_byte(g);
			uint8_t bb = xzs_clamp_byte(b);
			px[y * w + x] = 0xff000000u | ((uint32_t)rb << 16) | ((uint32_t)gb << 8) | (uint32_t)bb;
		}
	}

	id nsdata = ((id (*)(Class, SEL, const void *, unsigned long))objc_msgSend)(
	    cls_data, sel_data, bmp, (unsigned long)bmp_size);
	id img = nsdata ? ((id (*)(Class, SEL, id))objc_msgSend)(cls_img, sel_img, nsdata) : NULL;
	free(bmp);

	if (img) {
		((id (*)(id, SEL))objc_msgSend)(img, sel_registerName("retain"));
		g_xzs_wallpaper_uiimage = img;
	}
	return g_xzs_wallpaper_uiimage;
}

static void
xzs_install_wallpaper_into_window(id win, id ui_img, bool bring_to_front)
{
	if (!win || !ui_img) {
		return;
	}
	const long k_wall_tag = 0x585a5357L; /* 'XZSW' */
	id existing = ((id (*)(id, SEL, long))objc_msgSend)(win, sel_registerName("viewWithTag:"), k_wall_tag);
	if (existing) {
		if (bring_to_front) {
			((void (*)(id, SEL, id))objc_msgSend)(win, sel_registerName("bringSubviewToFront:"), existing);
		} else {
			((void (*)(id, SEL, id))objc_msgSend)(win, sel_registerName("sendSubviewToBack:"), existing);
		}
		return;
	}

	xzs_cgrect_t bounds = ((xzs_cgrect_t (*)(id, SEL))objc_msgSend)(win, sel_registerName("bounds"));
	if (bounds.size.width <= 0.0 || bounds.size.height <= 0.0) {
		bounds.origin.x = 0.0;
		bounds.origin.y = 0.0;
		bounds.size.width = 375.0;
		bounds.size.height = 812.0;
	}

	Class cls_iv = objc_getClass("UIImageView");
	if (!cls_iv) {
		return;
	}
	id wall_view = ((id (*)(id, SEL, xzs_cgrect_t))objc_msgSend)(
	    ((id (*)(Class, SEL))objc_msgSend)(cls_iv, sel_registerName("alloc")),
	    sel_registerName("initWithFrame:"), bounds);
	if (!wall_view) {
		return;
	}
	((void (*)(id, SEL, id))objc_msgSend)(wall_view, sel_registerName("setImage:"), ui_img);
	((void (*)(id, SEL, long))objc_msgSend)(wall_view, sel_registerName("setContentMode:"), 0L); /* UIViewContentModeScaleToFill */
	((void (*)(id, SEL, long))objc_msgSend)(wall_view, sel_registerName("setTag:"), k_wall_tag);
	((void (*)(id, SEL, BOOL))objc_msgSend)(wall_view, sel_registerName("setUserInteractionEnabled:"), NO);
	((void (*)(id, SEL, unsigned long))objc_msgSend)(wall_view, sel_registerName("setAutoresizingMask:"), 18UL);

	id layer = ((id (*)(id, SEL))objc_msgSend)(wall_view, sel_registerName("layer"));
	if (layer && !bring_to_front && class_respondsToSelector(object_getClass(layer), sel_registerName("setZPosition:"))) {
		((void (*)(id, SEL, double))objc_msgSend)(layer, sel_registerName("setZPosition:"), -1000.0);
	}

	((void (*)(id, SEL, id))objc_msgSend)(win, sel_registerName("addSubview:"), wall_view);
	if (bring_to_front) {
		((void (*)(id, SEL, id))objc_msgSend)(win, sel_registerName("bringSubviewToFront:"), wall_view);
	} else {
		((void (*)(id, SEL, id))objc_msgSend)(win, sel_registerName("sendSubviewToBack:"), wall_view);
	}
}

static void
xzs_sb_ensure_wallpaper(void)
{
	id ui_img = xzs_get_or_create_wallpaper_image();
	if (!ui_img) {
		return;
	}

	/* 1. Install into SBWallpaperController._window (_SBWallpaperSecureWindow) */
	Class cls_wc = objc_getClass("SBWallpaperController");
	SEL sel_shared = sel_registerName("sharedInstance");
	SEL sel_win = sel_registerName("_window");
	if (cls_wc && class_getClassMethod(cls_wc, sel_shared)) {
		id wc = ((id (*)(Class, SEL))objc_msgSend)(cls_wc, sel_shared);
		if (wc && class_respondsToSelector(object_getClass(wc), sel_win)) {
			id wp_win = ((id (*)(id, SEL))objc_msgSend)(wc, sel_win);
			if (wp_win) {
				((void (*)(id, SEL, BOOL))objc_msgSend)(wp_win, sel_registerName("setHidden:"), NO);
				((void (*)(id, SEL, double))objc_msgSend)(wp_win, sel_registerName("setAlpha:"), 1.0);
				xzs_install_wallpaper_into_window(wp_win, ui_img, true);
			}
		}
	}

	/* 2. Also install at the back (zPosition = -1000) of SBHomeScreenWindow & SBCoverSheetWindow */
	Class cls_win = objc_getClass("UIWindow");
	SEL sel_all = sel_registerName("allWindowsIncludingInternalWindows:onlyVisibleWindows:");
	if (cls_win && class_getClassMethod(cls_win, sel_all)) {
		id wins = ((id (*)(Class, SEL, BOOL, BOOL))objc_msgSend)(cls_win, sel_all, YES, NO);
		long wcnt = wins ? ((long (*)(id, SEL))objc_msgSend)(wins, sel_registerName("count")) : 0;
		for (long i = 0; i < wcnt; i++) {
			id w = ((id (*)(id, SEL, long))objc_msgSend)(wins, sel_registerName("objectAtIndex:"), i);
			const char *cname = w ? object_getClassName(w) : "";
			if (!cname) continue;
			if (strstr(cname, "Wallpaper") != NULL) {
				((void (*)(id, SEL, BOOL))objc_msgSend)(w, sel_registerName("setHidden:"), NO);
				((void (*)(id, SEL, double))objc_msgSend)(w, sel_registerName("setAlpha:"), 1.0);
				xzs_install_wallpaper_into_window(w, ui_img, true);
			} else if (strcmp(cname, "SBHomeScreenWindow") == 0 ||
			           strcmp(cname, "SBCoverSheetWindow") == 0) {
				xzs_install_wallpaper_into_window(w, ui_img, false);
			}
		}
	}
	xzs_log("[XZS-INJECT] SpringBoard: installed raster iOS 16 WWDC22 UIImage wallpaper (375x812 BMP)\n");
}

static void
xzs_sb_ensure_all_apps(id model)
{
	static const struct {
		const char *app_dir;
		const char *bundle_id;
	} k_promoted_apps[] = {
		{ "AppleTV.app", "com.apple.tv" },
		{ "Books.app", "com.apple.iBooks" },
		{ "Bridge.app", "com.apple.Bridge" },
		{ "Calculator.app", "com.apple.calculator" },
		{ "Compass.app", "com.apple.compass" },
		{ "Contacts.app", "com.apple.MobileAddressBook" },
		{ "FaceTime.app", "com.apple.facetime" },
		{ "Files.app", "com.apple.DocumentsApp" },
		{ "FindMy.app", "com.apple.findmy" },
		{ "Fitness.app", "com.apple.Fitness" },
		{ "Freeform.app", "com.apple.freeform" },
		{ "Home.app", "com.apple.Home" },
		{ "Magnifier.app", "com.apple.Magnifier" },
		{ "Maps.app", "com.apple.Maps" },
		{ "Measure.app", "com.apple.measure" },
		{ "MobileCal.app", "com.apple.mobilecal" },
		{ "MobileMail.app", "com.apple.mobilemail" },
		{ "MobileNotes.app", "com.apple.mobilenotes" },
		{ "MobileStore.app", "com.apple.MobileStore" },
		{ "MobileTimer.app", "com.apple.mobiletimer" },
		{ "Music.app", "com.apple.Music" },
		{ "News.app", "com.apple.news" },
		{ "Passbook.app", "com.apple.Passbook" },
		{ "Podcasts.app", "com.apple.podcasts" },
		{ "Reminders.app", "com.apple.reminders" },
		{ "SequoiaTranslator.app", "com.apple.Translate" },
		{ "Shortcuts.app", "com.apple.shortcuts" },
		{ "Stocks.app", "com.apple.stocks" },
		{ "Tips.app", "com.apple.tips" },
		{ "VoiceMemos.app", "com.apple.VoiceMemos" },
		{ "Weather.app", "com.apple.weather" }
	};

	Class cls_ws = objc_getClass("LSApplicationWorkspace");
	Class cls_proxy = objc_getClass("LSApplicationProxy");
	Class cls_rec = objc_getClass("LSApplicationRecord");
	Class cls_appctrl = objc_getClass("SBApplicationController");
	Class cls_url = objc_getClass("NSURL");
	Class cls_set = objc_getClass("NSSet");
	if (!cls_ws || !cls_appctrl || !cls_url) {
		return;
	}

	id ws = ((id (*)(Class, SEL))objc_msgSend)(cls_ws, sel_registerName("defaultWorkspace"));
	id appctrl = ((id (*)(Class, SEL))objc_msgSend)(cls_appctrl, sel_registerName("sharedInstance"));
	if (!ws || !appctrl) {
		return;
	}

	/* Clear any removed/demoted system app identifiers in LaunchServices */
	SEL sel_set_removed = sel_registerName("_LSPrivateSetRemovedSystemAppIdentifiers:");
	if (cls_set && class_respondsToSelector(object_getClass(ws), sel_set_removed)) {
		id empty_set = ((id (*)(Class, SEL))objc_msgSend)(cls_set, sel_registerName("set"));
		((void (*)(id, SEL, id))objc_msgSend)(ws, sel_set_removed, empty_set);
	}

	id ws_installed = ((id (*)(id, SEL))objc_msgSend)(ws, sel_registerName("allInstalledApplications"));
	long ws_cnt = ws_installed ? ((long (*)(id, SEL))objc_msgSend)(ws_installed, sel_registerName("count")) : 0;
	id sb_apps = ((id (*)(id, SEL))objc_msgSend)(appctrl, sel_registerName("allApplications"));
	long sb_cnt = sb_apps ? ((long (*)(id, SEL))objc_msgSend)(sb_apps, sel_registerName("count")) : 0;
	id applib = class_respondsToSelector(object_getClass(appctrl), sel_registerName("_appLibrary")) ?
	    ((id (*)(id, SEL))objc_msgSend)(appctrl, sel_registerName("_appLibrary")) : NULL;

	SEL sel_app_by_id = sel_registerName("applicationWithBundleIdentifier:");
	SEL sel_reg_app = sel_registerName("registerApplication:");
	SEL sel_restore_app = sel_registerName("restoreSystemApplication:");
	SEL sel_add_proxy = sel_registerName("addApplicationProxy:withOverrideURL:");
	SEL sel_add_sbapp = sel_registerName("addApplication:");

	uint32_t missing_cnt = 0, registered_cnt = 0, added_proxy_cnt = 0, model_added_cnt = 0;
	for (size_t i = 0; i < sizeof(k_promoted_apps) / sizeof(k_promoted_apps[0]); i++) {
		char full_path[256];
		snprintf(full_path, sizeof(full_path), "/Applications/%s", k_promoted_apps[i].app_dir);
		if (!xzs_file_exists(full_path)) {
			continue;
		}
		CFStringRef bid_cf = CFStringCreateWithCString(kCFAllocatorDefault,
		    k_promoted_apps[i].bundle_id, kCFStringEncodingUTF8);
		CFStringRef path_cf = CFStringCreateWithCString(kCFAllocatorDefault,
		    full_path, kCFStringEncodingUTF8);
		if (!bid_cf || !path_cf) {
			if (bid_cf) CFRelease(bid_cf);
			if (path_cf) CFRelease(path_cf);
			continue;
		}

		id sb_app = ((id (*)(id, SEL, id))objc_msgSend)(appctrl, sel_app_by_id, (id)bid_cf);
		if (!sb_app) {
			missing_cnt++;
			id burl = ((id (*)(Class, SEL, id))objc_msgSend)(cls_url,
			    sel_registerName("fileURLWithPath:"), (id)path_cf);
			if (burl) {
				if (class_respondsToSelector(object_getClass(ws), sel_restore_app)) {
					((BOOL (*)(id, SEL, id))objc_msgSend)(ws, sel_restore_app, burl);
				}
				if (class_respondsToSelector(object_getClass(ws), sel_reg_app)) {
					if (((BOOL (*)(id, SEL, id))objc_msgSend)(ws, sel_reg_app, burl)) {
						registered_cnt++;
					}
				}
				id proxy = NULL;
				if (cls_rec) {
					id err = NULL;
					id rec = ((id (*)(id, SEL, id, BOOL, id *))objc_msgSend)(
					    ((id (*)(Class, SEL))objc_msgSend)(cls_rec, sel_registerName("alloc")),
					    sel_registerName("initWithURL:allowPlaceholder:error:"),
					    burl, NO, &err);
					if (rec && class_respondsToSelector(object_getClass(rec), sel_registerName("compatibilityObject"))) {
						proxy = ((id (*)(id, SEL))objc_msgSend)(rec, sel_registerName("compatibilityObject"));
					}
				}
				if (!proxy && cls_proxy) {
					proxy = ((id (*)(Class, SEL, id))objc_msgSend)(cls_proxy,
					    sel_registerName("applicationProxyForIdentifier:"), (id)bid_cf);
				}
				if (proxy && applib && class_respondsToSelector(object_getClass(applib), sel_add_proxy)) {
					((void (*)(id, SEL, id, id))objc_msgSend)(applib, sel_add_proxy, proxy, burl);
					added_proxy_cnt++;
				}
			}
			sb_app = ((id (*)(id, SEL, id))objc_msgSend)(appctrl, sel_app_by_id, (id)bid_cf);
		}
		if (sb_app && model && class_respondsToSelector(object_getClass(model), sel_add_sbapp)) {
			((void (*)(id, SEL, id))objc_msgSend)(model, sel_add_sbapp, sb_app);
			model_added_cnt++;
		}

		CFRelease(bid_cf);
		CFRelease(path_cf);
	}

	xzs_log("[XZS-INJECT] SpringBoard: apps ws=%ld sb=%ld missing_promoted=%u ls_reg=%u applib_added=%u model_added=%u\n",
	    ws_cnt, sb_cnt, missing_cnt, registered_cnt, added_proxy_cnt, model_added_cnt);
}

static void
xzs_sb_post_boot_check(__unused void *ctx)
{
	/* 0. Ensure iOS 16 WWDC22 wallpaper is visible in _SBWallpaperSecureWindow */
	xzs_sb_ensure_wallpaper();

	/* 1. Auto-unlock Lock Screen unless autounlock=0 is set in boot-args */
	char bootargs[512] = "";
	size_t bsz = sizeof(bootargs) - 1;
	bool autounlock = true;
	if (sysctlbyname("kern.bootargs", bootargs, &bsz, NULL, 0) == 0 &&
	    strstr(bootargs, "autounlock=0") != NULL) {
		autounlock = false;
	}
	Class cls_lsm = objc_getClass("SBLockScreenManager");
	SEL sel_shared = sel_registerName("sharedInstance");
	SEL sel_locked = sel_registerName("isUILocked");
	SEL sel_unlock = sel_registerName("unlockUIFromSource:withOptions:");
	if (cls_lsm && class_getClassMethod(cls_lsm, sel_shared)) {
		id lsm = ((id (*)(Class, SEL))objc_msgSend)(cls_lsm, sel_shared);
		if (lsm && class_respondsToSelector(object_getClass(lsm), sel_locked)) {
			BOOL locked = ((BOOL (*)(id, SEL))objc_msgSend)(lsm, sel_locked);
			if (locked && autounlock && class_respondsToSelector(object_getClass(lsm), sel_unlock)) {
				BOOL ok = ((BOOL (*)(id, SEL, long, id))objc_msgSend)(lsm, sel_unlock, 24L, NULL);
				xzs_log("[XZS-INJECT] SpringBoard: SBLockScreenManager unlockUIFromSource:24 -> %d\n", ok ? 1 : 0);
			} else {
				xzs_log("[XZS-INJECT] SpringBoard: SBLockScreenManager isUILocked=%d\n", locked ? 1 : 0);
			}
		}
	}

	/* 2. Inspect SBIconController Home Screen pages & ensure all 39 apps are present */
	Class cls_ic = objc_getClass("SBIconController");
	if (cls_ic && class_getClassMethod(cls_ic, sel_shared)) {
		id ic = ((id (*)(Class, SEL))objc_msgSend)(cls_ic, sel_shared);
		SEL sel_model = sel_registerName("model");
		SEL sel_rf = sel_registerName("rootFolder");
		id model = (ic && class_respondsToSelector(object_getClass(ic), sel_model)) ?
		    ((id (*)(id, SEL))objc_msgSend)(ic, sel_model) : NULL;

		xzs_sb_ensure_all_apps(model);

		id rf = (ic && class_respondsToSelector(object_getClass(ic), sel_rf)) ?
		    ((id (*)(id, SEL))objc_msgSend)(ic, sel_rf) :
		    ((model && class_respondsToSelector(object_getClass(model), sel_rf)) ?
		     ((id (*)(id, SEL))objc_msgSend)(model, sel_rf) : NULL);
		if (rf && class_respondsToSelector(object_getClass(rf), sel_registerName("lists"))) {
			id lists = ((id (*)(id, SEL))objc_msgSend)(rf, sel_registerName("lists"));
			long page_cnt = lists ? ((long (*)(id, SEL))objc_msgSend)(lists, sel_registerName("count")) : 0;
			long p0_icons = 0, p1_icons = 0;
			if (page_cnt >= 1) {
				id l0 = ((id (*)(id, SEL, long))objc_msgSend)(lists, sel_registerName("objectAtIndex:"), 0);
				if (l0 && class_respondsToSelector(object_getClass(l0), sel_registerName("numberOfIcons"))) {
					p0_icons = ((long (*)(id, SEL))objc_msgSend)(l0, sel_registerName("numberOfIcons"));
				}
			}
			if (page_cnt >= 2) {
				id l1 = ((id (*)(id, SEL, long))objc_msgSend)(lists, sel_registerName("objectAtIndex:"), 1);
				if (l1 && class_respondsToSelector(object_getClass(l1), sel_registerName("numberOfIcons"))) {
					p1_icons = ((long (*)(id, SEL))objc_msgSend)(l1, sel_registerName("numberOfIcons"));
				}
			}
			if (page_cnt < 2 && model) {
				if (class_respondsToSelector(object_getClass(model), sel_registerName("clearDesiredIconStateWithOptions:"))) {
					((void (*)(id, SEL, unsigned long))objc_msgSend)(model,
					    sel_registerName("clearDesiredIconStateWithOptions:"), 0UL);
				}
				if (class_respondsToSelector(object_getClass(model), sel_registerName("loadAllIcons"))) {
					((void (*)(id, SEL))objc_msgSend)(model, sel_registerName("loadAllIcons"));
				}
				if (class_respondsToSelector(object_getClass(model), sel_registerName("layout"))) {
					((void (*)(id, SEL))objc_msgSend)(model, sel_registerName("layout"));
				}
				xzs_sb_ensure_all_apps(model);
				lists = ((id (*)(id, SEL))objc_msgSend)(rf, sel_registerName("lists"));
				page_cnt = lists ? ((long (*)(id, SEL))objc_msgSend)(lists, sel_registerName("count")) : 0;
				if (page_cnt >= 1) {
					id l0 = ((id (*)(id, SEL, long))objc_msgSend)(lists, sel_registerName("objectAtIndex:"), 0);
					if (l0 && class_respondsToSelector(object_getClass(l0), sel_registerName("numberOfIcons"))) {
						p0_icons = ((long (*)(id, SEL))objc_msgSend)(l0, sel_registerName("numberOfIcons"));
					}
				}
				if (page_cnt >= 2) {
					id l1 = ((id (*)(id, SEL, long))objc_msgSend)(lists, sel_registerName("objectAtIndex:"), 1);
					if (l1 && class_respondsToSelector(object_getClass(l1), sel_registerName("numberOfIcons"))) {
						p1_icons = ((long (*)(id, SEL))objc_msgSend)(l1, sel_registerName("numberOfIcons"));
					}
				}
				xzs_log("[XZS-INJECT] SpringBoard: rebuilt SBIconModel -> pages=%ld (page0=%ld icons, page1=%ld icons)\n",
				    page_cnt, p0_icons, p1_icons);
			} else {
				xzs_log("[XZS-INJECT] SpringBoard: HomeScreen rootFolder pages=%ld (page0=%ld icons, page1=%ld icons)\n",
				    page_cnt, p0_icons, p1_icons);
			}
		}
		if (model && class_respondsToSelector(object_getClass(model), sel_registerName("saveIconStateIfNeeded"))) {
			((void (*)(id, SEL))objc_msgSend)(model, sel_registerName("saveIconStateIfNeeded"));
		}
		SEL sel_relayout = sel_registerName("layoutIconLists:domino:forceRelayout:");
		if (ic && class_respondsToSelector(object_getClass(ic), sel_relayout)) {
			((void (*)(id, SEL, double, BOOL, BOOL))objc_msgSend)(ic, sel_relayout, 0.0, NO, YES);
		}
	}

	/* 3. Log visible UIWindow count & schedule a follow-up check if first pass */
	Class cls_win = objc_getClass("UIWindow");
	SEL sel_all = sel_registerName("allWindowsIncludingInternalWindows:onlyVisibleWindows:");
	if (cls_win && class_getClassMethod(cls_win, sel_all)) {
		id wins = ((id (*)(Class, SEL, BOOL, BOOL))objc_msgSend)(cls_win, sel_all, YES, YES);
		long wcnt = wins ? ((long (*)(id, SEL))objc_msgSend)(wins, sel_registerName("count")) : 0;
		xzs_log("[XZS-INJECT] SpringBoard: visible UIWindows=%ld\n", wcnt);
	}
	if (ctx == NULL) {
		dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW, 8ULL * NSEC_PER_SEC),
		    &_dispatch_main_q, (void *)1, xzs_sb_post_boot_check);
	}
}

static void
xzs_init_springboard(void)
{
	/* Point stdout to /dev/console so any framework diagnostics are visible */
	int cfd = open("/dev/console", O_WRONLY | O_NOCTTY);
	if (cfd >= 0) {
		(void)dup2(cfd, 1);
		close(cfd);
	}

	/* Force plain white vibrancy on Lock Screen clock (fixes red alpha mask on SW renderer) */
	CFStringRef key = CFStringCreateWithCString(kCFAllocatorDefault,
	    "BSUIVibrancyEffectViewDisabled", kCFStringEncodingUTF8);
	if (key) {
		CFPreferencesSetAppValue(key, kCFBooleanTrue, kCFPreferencesCurrentApplication);
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
	 * Give lsd time to finish registering all 39 /Applications/<app> bundles
	 * before SpringBoard builds the 2-page Home Screen icon layout.
	 */
	const char *delay_marker = "/private/var/mobile/Library/SpringBoard/.xzs_sb_rebuild_delay";
	if (xzs_file_exists(delay_marker)) {
		(void)unlink(delay_marker);
		xzs_log("[XZS-INJECT] SpringBoard[%d]: hooks active (plain_vibrancy=1, JPEG snapshots=1); waiting 6.0s for lsd 39-app rebuild\n",
		    getpid());
		usleep(6000000);
	} else {
		xzs_log("[XZS-INJECT] SpringBoard[%d]: hooks active (plain_vibrancy=1, JPEG snapshots=1); waiting 3.0s for lsd\n",
		    getpid());
		usleep(3000000);
	}

	dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW, 4ULL * NSEC_PER_SEC),
	    &_dispatch_main_q, NULL, xzs_sb_post_boot_check);
	dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW, 10ULL * NSEC_PER_SEC),
	    &_dispatch_main_q, NULL, xzs_sb_post_boot_check);
	dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW, 20ULL * NSEC_PER_SEC),
	    &_dispatch_main_q, NULL, xzs_sb_post_boot_check);
	dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW, 35ULL * NSEC_PER_SEC),
	    &_dispatch_main_q, NULL, xzs_sb_post_boot_check);
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
