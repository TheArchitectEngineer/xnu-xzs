/*
 * Compile Apple's HFS kext sources inside SWEET the way the kext is built:
 * with the kext (KPI) view of the kernel headers, and with only HFS's own
 * kext-config.h deciding features. The kernel's -DCONFIG_* options are not
 * visible to a kext build, so the ones HFS tests are cleared first. SWEET
 * has no AppleKeyStore, so content protection stays off.
 */
#ifndef SWEET_HFS_COMPAT_H
#define SWEET_HFS_COMPAT_H
#undef BSD_KERNEL_PRIVATE
#undef XNU_KERNEL_PRIVATE
#ifndef __startup_func
#define __startup_func
#endif
#undef CONFIG_PROTECT
#undef CONFIG_SEARCHFS
#undef CONFIG_HFS_DIRLINK
#undef CONFIG_SECLUDED_RENAME
#undef CONFIG_EXT_RESOLVER
#undef CONFIG_MACF
#undef CONFIG_HFS_RBTREE
#undef QUOTA
#undef NAMEDSTREAMS
#undef FIFO
#undef HFS_COMPRESSION
#include "core/kext-config.h"

/* Removed in Darwin 24 (xnu-10063) sys/fsctl.h */
#define nspace_snapshot_event(vp, ctime, op, arg) (0)
#define resolve_nspace_item(vp, op) (0)

/* Built in, not a loadable kext (see sweet_hfs_iokit.c). */
#define OSKextGetCurrentLoadTag sweet_hfs_current_load_tag
#endif
