#ifndef _TRUSTCACHE_API_H_
#define _TRUSTCACHE_API_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define kTCEntryHashSize 20
#define kUUIDSize 16

enum {
    kTCReturnSuccess = 0,
    kTCReturnError = 1,
    kTCReturnNotFound = 2,
    kTCReturnDuplicate = 3,
};

enum {
    kTCCapabilityNone = 0,
};

enum {
    kTCTypeInvalid = 0,
    kTCTypeStatic = 1,
    kTCTypeEngineering = 2,
    kTCTypeLegacy = 3,
    kTCTypeCryptex1BootOS = 4,
    kTCTypeCryptex1BootApp = 5,
    kTCTypeLTRS = 6,
    kTCTypeDTRS = 7,
    kTCTypeTotal = 8,
};

enum {
    kTCQueryTypeAll = 0,
    kTCQueryTypeLoadable = 1,
    kTCQueryTypeStatic = 2,
    kTCQueryTypeTotal = 3,
};

typedef struct _TCReturn {
    uint8_t component;
    int error;
    uint16_t uniqueError;
} TCReturn_t;

typedef uint64_t TCCapabilities_t;
typedef uint32_t TCType_t;
typedef uint32_t TCQueryType_t;

struct TrustCacheTypeConfig {
    const char *entitlementValue;
};

static const struct TrustCacheTypeConfig TCTypeConfig[kTCTypeTotal] = {
    [kTCTypeInvalid] = { .entitlementValue = NULL },
    [kTCTypeStatic] = { .entitlementValue = NULL },
    [kTCTypeEngineering] = { .entitlementValue = "personalized.engineering-root" },
    [kTCTypeLegacy] = { .entitlementValue = NULL },
    [kTCTypeCryptex1BootOS] = { .entitlementValue = "cryptex1.boot-os" },
    [kTCTypeCryptex1BootApp] = { .entitlementValue = "cryptex1.boot-app" },
    [kTCTypeLTRS] = { .entitlementValue = "personalized.loadable-root" },
    [kTCTypeDTRS] = { .entitlementValue = "personalized.engineering-root" },
};

typedef struct _TrustCacheRuntime {
    bool allowSecondStaticTC;
    bool allowEngineeringTC;
    uint8_t opaque[64];
} TrustCacheRuntime_t;

typedef struct _TrustCacheMutableRuntime {
    uint8_t opaque[64];
} TrustCacheMutableRuntime_t;

typedef struct _TrustCache {
    uint8_t opaque[64];
} TrustCache_t;

typedef struct _TrustCacheQueryToken {
    uint8_t opaque[64];
} TrustCacheQueryToken_t;

static inline void trustCacheInitializeRuntime(
    TrustCacheRuntime_t *rt,
    TrustCacheMutableRuntime_t *mut_rt,
    bool allow_second_static,
    bool allow_engineering,
    bool allow_legacy,
    const void *img4_runtime)
{
    if (rt) {
        rt->allowSecondStaticTC = allow_second_static;
        rt->allowEngineeringTC = allow_engineering;
    }
}

#endif /* _TRUSTCACHE_API_H_ */
