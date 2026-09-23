/*
 * XZSFS Vnode Operations Table
 *
 * Implements read-only vnode operations and enforces EROFS on all mutations.
 */

#include "xzsfs.h"
#include <sys/vnode_internal.h>
#include <vfs/vfs_support.h>
#include <sys/ubc.h>
#include <sys/ubc_internal.h>
#include <libkern/libkern.h>
#include <mach/vm_param.h>
#include <mach/vm_statistics.h>
#include <kern/kalloc.h>

extern void xzs_bringup_console_write(const void *buf, int len);

#define VOPFUNC int (*)(void *)

int (**xzsfs_vnodeop_p)(void *);

int
xzsfs_rofs_err(__unused void *ap)
{
    return EROFS;
}

static int
xzsfs_lookup(struct vnop_lookup_args *ap)
{
    vnode_t dvp = ap->a_dvp;
    vnode_t *vpp = ap->a_vpp;
    struct componentname *cnp = ap->a_cnp;
    struct xzsfs_node *dir_node;
    const struct xzsfs_core_node *matched_core = NULL;
    int error;

    *vpp = NULL;

    if (dvp->v_type != VDIR) {
        return ENOTDIR;
    }

    dir_node = (struct xzsfs_node *)vnode_fsnode(dvp);
    if (!dir_node) {
        return EINVAL;
    }

    error = xzsfs_core_lookup(&dir_node->xmp->fs, dir_node->core.object_id,
                              cnp->cn_nameptr, (size_t)cnp->cn_namelen,
                              &matched_core);
    if (error != 0) {
        return error;
    }

    /* Locate in-memory xzsfs_node */
    struct xzsfs_node *target_node = &dir_node->xmp->nodes[matched_core->object_id - 1U];
    return xzsfs_get_vnode(dvp->v_mount, target_node, vpp);
}

static int
xzsfs_open(struct vnop_open_args *ap)
{
    if (ap->a_mode & FWRITE) {
        return EROFS;
    }
    return 0;
}

static int
xzsfs_close(__unused struct vnop_close_args *ap)
{
    return 0;
}

static int
xzsfs_read(struct vnop_read_args *ap)
{
    vnode_t vp = ap->a_vp;
    struct uio *uio = ap->a_uio;
    struct xzsfs_node *node;
    off_t foffset;
    user_ssize_t resid;

    if (vp->v_type != VREG) {
        return EINVAL;
    }

    node = (struct xzsfs_node *)vnode_fsnode(vp);
    if (!node) {
        return EINVAL;
    }

    foffset = uio_offset(uio);
    resid = uio_resid(uio);

    if (foffset < 0 || (uint64_t)foffset >= node->core.data_length || resid <= 0) {
        return 0;
    }

    size_t to_read = (size_t)resid;
    if ((uint64_t)foffset + to_read > node->core.data_length) {
        to_read = (size_t)(node->core.data_length - (uint64_t)foffset);
    }

    size_t done = 0;
    while (done < to_read) {
        uint64_t cur_pos = node->core.data_offset + (uint64_t)foffset + done;
        uint64_t lba = cur_pos / XZSFS_SECTOR_SIZE;
        uint32_t sec_offset = (uint32_t)(cur_pos % XZSFS_SECTOR_SIZE);
        uint32_t chunk = XZSFS_SECTOR_SIZE - sec_offset;
        if (chunk > (to_read - done)) {
            chunk = (uint32_t)(to_read - done);
        }

        buf_t bp = NULL;
        int error = buf_bread(node->xmp->devvp, (daddr64_t)lba, XZSFS_SECTOR_SIZE, NOCRED, &bp);
        if (error != 0) {
            if (bp) buf_brelse(bp);
            return error;
        }

        error = uiomove((const char *)(uintptr_t)buf_dataptr(bp) + sec_offset, (int)chunk, uio);
        buf_brelse(bp);
        if (error != 0) {
            return error;
        }

        done += chunk;
    }

    return 0;
}

static int
xzsfs_getattr(struct vnop_getattr_args *ap)
{
    vnode_t vp = ap->a_vp;
    struct vnode_attr *vap = ap->a_vap;
    struct xzsfs_node *node = (struct xzsfs_node *)vnode_fsnode(vp);

    if (!node) {
        return EINVAL;
    }

    return xzsfs_helper_format_getattr(&node->core, vap);
}

static int
xzsfs_readdir(struct vnop_readdir_args *ap)
{
    vnode_t vp = ap->a_vp;
    struct uio *uio = ap->a_uio;
    struct xzsfs_node *dir_node;
    struct dirent de;
    off_t startpos, pos;
    int error = 0;

    if (vp->v_type != VDIR) {
        return ENOTDIR;
    }

    dir_node = (struct xzsfs_node *)vnode_fsnode(vp);
    if (!dir_node) {
        return EINVAL;
    }

    startpos = uio_offset(uio);
    pos = 0;

    /* Enumerate directory: index 0 is '.', index 1 is '..', then matching children */
    uint32_t entry_idx = 0;
    while (uio_resid(uio) > 0) {
        if (entry_idx == 0) {
            xzsfs_helper_format_dirent(&dir_node->core, ".", DT_DIR, &de);
        } else if (entry_idx == 1) {
            uint32_t p = dir_node->core.parent_id;
            const struct xzsfs_core_node *pnode = &dir_node->xmp->fs.nodes[p - 1U];
            xzsfs_helper_format_dirent(pnode, "..", DT_DIR, &de);
        } else {
            uint32_t child_found = 0;
            uint32_t current_child_num = 2;
            for (uint32_t i = 1; i < dir_node->xmp->fs.node_count; i++) {
                if (dir_node->xmp->fs.nodes[i].parent_id == dir_node->core.object_id) {
                    if (current_child_num == entry_idx) {
                        const struct xzsfs_core_node *cnode = &dir_node->xmp->fs.nodes[i];
                        uint8_t dt = (cnode->type == XZSFS_TYPE_DIR) ? DT_DIR : DT_REG;
                        xzsfs_helper_format_dirent(cnode, cnode->name, dt, &de);
                        child_found = 1;
                        break;
                    }
                    current_child_num++;
                }
            }
            if (!child_found) {
                break; /* No more children */
            }
        }

        if (pos >= startpos) {
            if (uio_resid(uio) < de.d_reclen) {
                break;
            }
            error = uiomove((const char *)&de, de.d_reclen, uio);
            if (error != 0) {
                break;
            }
        }
        pos += de.d_reclen;
        entry_idx++;
    }

    uio_setoffset(uio, pos);
    return error;
}

static int
xzsfs_reclaim(struct vnop_reclaim_args *ap)
{
    vnode_t vp = ap->a_vp;
    struct xzsfs_node *node = (struct xzsfs_node *)vnode_fsnode(vp);
    if (node) {
        return xzsfs_node_reclaim(node, vp);
    }
    return 0;
}

static int
xzsfs_vnop_pagein(struct vnop_pagein_args *ap)
{
    extern void xzs_bringup_console_write(const void *buf, int len);
    xzs_bringup_console_write("[XZS-PAGEIN] ENTER\n", 19);
    vnode_t vp = ap->a_vp;
    upl_t upl = ap->a_pl;
    upl_offset_t pl_offset = ap->a_pl_offset;
    off_t f_offset = ap->a_f_offset;
    size_t size = ap->a_size;
    int flags = ap->a_flags;
    static uint32_t s_pagein_seq = 0;
    uint32_t seq = ++s_pagein_seq;
    char logbuf[256];
    int len;

    if (!vp || !upl || size == 0) {
        return EINVAL;
    }

    if (vnode_vtype(vp) != VREG) {
        if ((flags & UPL_NOCOMMIT) == 0) {
            ubc_upl_abort_range(upl, pl_offset, (upl_size_t)size, UPL_ABORT_FREE_ON_EMPTY | UPL_ABORT_ERROR);
        }
        return EINVAL;
    }

    struct xzsfs_node *node = (struct xzsfs_node *)vnode_fsnode(vp);
    if (!node || !node->xmp || !node->xmp->devvp) {
        if ((flags & UPL_NOCOMMIT) == 0) {
            ubc_upl_abort_range(upl, pl_offset, (upl_size_t)size, UPL_ABORT_FREE_ON_EMPTY | UPL_ABORT_ERROR);
        }
        return EINVAL;
    }

    uint64_t filesize = node->core.data_length;

    /* Section 25: [XZS-PAGEIN] PRE */
    len = snprintf(logbuf, sizeof(logbuf),
        "[XZS-PAGEIN] PRE SEQ=%u VP=%p FILE_SIZE=%llu UPL=%p PL_OFFSET=%u FILE_OFFSET=%lld SIZE=%zu FLAGS=0x%x UPL_NOCOMMIT=%s\n",
        seq, (void *)vp, (unsigned long long)filesize, (void *)upl, (unsigned int)pl_offset,
        (long long)f_offset, size, flags, (flags & UPL_NOCOMMIT) ? "yes" : "no");
    xzs_bringup_console_write(logbuf, len);

    /*
     * Range and alignment validation:
     * File offset, size, and UPL offset must be page aligned.
     * Offset must be non-negative and strictly within file size.
     */
    if (f_offset < 0 || (uint64_t)f_offset >= filesize ||
        (f_offset & PAGE_MASK_64) || (size & PAGE_MASK) || (pl_offset & PAGE_MASK)) {
        if ((flags & UPL_NOCOMMIT) == 0) {
            ubc_upl_abort_range(upl, pl_offset, (upl_size_t)size, UPL_ABORT_FREE_ON_EMPTY | UPL_ABORT_ERROR);
        }
        len = snprintf(logbuf, sizeof(logbuf),
            "[XZS-PAGEIN] POST UPL_MAP_RESULT=NOT_ATTEMPTED COMMIT_ACTION=NONE COMMIT_RESULT=NOT_APPLICABLE ABORT_ACTION=%s RESULT=EINVAL\n",
            (flags & UPL_NOCOMMIT) ? "NONE" : "ABORT_ALL");
        xzs_bringup_console_write(logbuf, len);
        return EINVAL;
    }

    /* Overflow check */
    if ((uint64_t)f_offset + (uint64_t)size < (uint64_t)f_offset) {
        if ((flags & UPL_NOCOMMIT) == 0) {
            ubc_upl_abort_range(upl, pl_offset, (upl_size_t)size, UPL_ABORT_FREE_ON_EMPTY | UPL_ABORT_ERROR);
        }
        len = snprintf(logbuf, sizeof(logbuf),
            "[XZS-PAGEIN] POST UPL_MAP_RESULT=NOT_ATTEMPTED COMMIT_ACTION=NONE COMMIT_RESULT=NOT_APPLICABLE ABORT_ACTION=%s RESULT=EINVAL\n",
            (flags & UPL_NOCOMMIT) ? "NONE" : "ABORT_ALL");
        xzs_bringup_console_write(logbuf, len);
        return EINVAL;
    }

    /* Determine valid file bytes in this requested range */
    uint64_t max_size = filesize - (uint64_t)f_offset;
    size_t io_size = (size < max_size) ? size : (size_t)max_size;
    size_t rounded_size = (size_t)round_page_64(io_size);
    if (rounded_size > size) {
        rounded_size = size;
    }
    size_t zero_bytes = (rounded_size > io_size) ? (rounded_size - io_size) : 0;

    /* Section 25: [XZS-PAGEIN] RANGE */
    len = snprintf(logbuf, sizeof(logbuf),
        "[XZS-PAGEIN] RANGE VALID_FILE_BYTES=%zu ROUNDED_VALID_BYTES=%zu ZERO_BYTES=%zu\n",
        io_size, rounded_size, zero_bytes);
    xzs_bringup_console_write(logbuf, len);

    /* Map the UPL into kernel address space */
    vm_offset_t ioaddr = 0;
    kern_return_t kr = ubc_upl_map(upl, &ioaddr);
    if (kr != KERN_SUCCESS) {
        if ((flags & UPL_NOCOMMIT) == 0) {
            ubc_upl_abort_range(upl, pl_offset, (upl_size_t)size, UPL_ABORT_FREE_ON_EMPTY | UPL_ABORT_ERROR);
        }
        len = snprintf(logbuf, sizeof(logbuf),
            "[XZS-PAGEIN] POST UPL_MAP_RESULT=FAILED COMMIT_ACTION=NONE COMMIT_RESULT=NOT_APPLICABLE ABORT_ACTION=%s RESULT=EIO\n",
            (flags & UPL_NOCOMMIT) ? "NONE" : "ABORT_ALL");
        xzs_bringup_console_write(logbuf, len);
        return EIO;
    }

    uint8_t *dst = (uint8_t *)ioaddr + pl_offset;
    size_t bytes_read = 0;
    int error = xzsfs_core_read(xzsfs_kernel_block_read, node->xmp->devvp,
                                &node->core, (uint64_t)f_offset, io_size,
                                dst, &bytes_read);
    if (error != 0 || bytes_read != io_size) {
        ubc_upl_unmap(upl);
        if ((flags & UPL_NOCOMMIT) == 0) {
            ubc_upl_abort_range(upl, pl_offset, (upl_size_t)size, UPL_ABORT_FREE_ON_EMPTY | UPL_ABORT_ERROR);
        }
        len = snprintf(logbuf, sizeof(logbuf),
            "[XZS-PAGEIN] POST UPL_MAP_RESULT=SUCCESS COMMIT_ACTION=NONE COMMIT_RESULT=NOT_APPLICABLE ABORT_ACTION=%s RESULT=%d\n",
            (flags & UPL_NOCOMMIT) ? "NONE" : "ABORT_ALL", error ? error : EIO);
        xzs_bringup_console_write(logbuf, len);
        return error ? error : EIO;
    }

    /* Zero any tail bytes within the last populated page beyond EOF */
    if (zero_bytes > 0) {
        bzero(dst + bytes_read, zero_bytes);
    }

    /* Calculate lightweight CRC32 checksum over the read bytes while still mapped */
    uint32_t csum = xzsfs_crc32(0, dst, bytes_read);

    /* Section 25: [XZS-PAGEIN] DATA */
    len = snprintf(logbuf, sizeof(logbuf),
        "[XZS-PAGEIN] DATA READ_BYTES=%zu SOURCE_CHECKSUM=0x%08x\n",
        bytes_read, csum);
    xzs_bringup_console_write(logbuf, len);

    /* Unmap UPL before committing */
    kr = ubc_upl_unmap(upl);
    if (kr != KERN_SUCCESS) {
        if ((flags & UPL_NOCOMMIT) == 0) {
            ubc_upl_abort_range(upl, pl_offset, (upl_size_t)size, UPL_ABORT_FREE_ON_EMPTY | UPL_ABORT_ERROR);
        }
        return EIO;
    }

    /* Handle UPL commit / abort semantics */
    const char *commit_action = "NONE";
    const char *commit_result = "NOT_APPLICABLE";
    const char *abort_action = "NONE";
    if ((flags & UPL_NOCOMMIT) == 0) {
        int ckr = ubc_upl_commit_range(upl, pl_offset, (upl_size_t)rounded_size,
                                       UPL_COMMIT_FREE_ON_EMPTY | UPL_COMMIT_CLEAR_DIRTY);
        commit_action = "UPL_COMMIT_RANGE";
        commit_result = (ckr == KERN_SUCCESS) ? "SUCCESS" : "ERROR";
        if (size > rounded_size) {
            ubc_upl_abort_range(upl, pl_offset + (upl_offset_t)rounded_size,
                                (upl_size_t)(size - rounded_size),
                                UPL_ABORT_FREE_ON_EMPTY | UPL_ABORT_ERROR);
            abort_action = "ABORT_REMAINDER";
        }
    }

    /* Section 25: [XZS-PAGEIN] POST */
    len = snprintf(logbuf, sizeof(logbuf),
        "[XZS-PAGEIN] POST UPL_MAP_RESULT=SUCCESS COMMIT_ACTION=%s COMMIT_RESULT=%s ABORT_ACTION=%s RESULT=0\n",
        commit_action, commit_result, abort_action);
    xzs_bringup_console_write(logbuf, len);

    return 0;
}

const struct vnodeopv_entry_desc xzsfs_vnodeop_entries[] = {
    { .opve_op = &vnop_default_desc, .opve_impl = (VOPFUNC)(void *)vn_default_error },
    { .opve_op = &vnop_lookup_desc,  .opve_impl = (VOPFUNC)xzsfs_lookup },
    { .opve_op = &vnop_open_desc,    .opve_impl = (VOPFUNC)xzsfs_open },
    { .opve_op = &vnop_close_desc,   .opve_impl = (VOPFUNC)xzsfs_close },
    { .opve_op = &vnop_read_desc,    .opve_impl = (VOPFUNC)xzsfs_read },
    { .opve_op = &vnop_pagein_desc,  .opve_impl = (VOPFUNC)xzsfs_vnop_pagein },
    { .opve_op = &vnop_getattr_desc, .opve_impl = (VOPFUNC)xzsfs_getattr },
    { .opve_op = &vnop_readdir_desc, .opve_impl = (VOPFUNC)xzsfs_readdir },
    { .opve_op = &vnop_inactive_desc,.opve_impl = (VOPFUNC)nop_inactive },
    { .opve_op = &vnop_reclaim_desc, .opve_impl = (VOPFUNC)xzsfs_reclaim },

    /* Prohibited mutations deterministically returning EROFS */
    { .opve_op = &vnop_create_desc,  .opve_impl = (VOPFUNC)xzsfs_rofs_err },
    { .opve_op = &vnop_write_desc,   .opve_impl = (VOPFUNC)xzsfs_rofs_err },
    { .opve_op = &vnop_mknod_desc,   .opve_impl = (VOPFUNC)xzsfs_rofs_err },
    { .opve_op = &vnop_setattr_desc, .opve_impl = (VOPFUNC)xzsfs_rofs_err },
    { .opve_op = &vnop_remove_desc,  .opve_impl = (VOPFUNC)xzsfs_rofs_err },
    { .opve_op = &vnop_link_desc,    .opve_impl = (VOPFUNC)xzsfs_rofs_err },
    { .opve_op = &vnop_rename_desc,  .opve_impl = (VOPFUNC)xzsfs_rofs_err },
    { .opve_op = &vnop_mkdir_desc,   .opve_impl = (VOPFUNC)xzsfs_rofs_err },
    { .opve_op = &vnop_rmdir_desc,   .opve_impl = (VOPFUNC)xzsfs_rofs_err },
    { .opve_op = &vnop_symlink_desc, .opve_impl = (VOPFUNC)xzsfs_rofs_err },

    /* Safe fallbacks */
    { .opve_op = &vnop_fsync_desc,   .opve_impl = (VOPFUNC)nop_fsync },
    { .opve_op = &vnop_ioctl_desc,   .opve_impl = (VOPFUNC)err_ioctl },
    { .opve_op = &vnop_select_desc,  .opve_impl = (VOPFUNC)err_select },
    { .opve_op = &vnop_mmap_desc,    .opve_impl = (VOPFUNC)err_mmap },
    { .opve_op = &vnop_pathconf_desc,.opve_impl = (VOPFUNC)err_pathconf },
    { .opve_op = &vnop_advlock_desc, .opve_impl = (VOPFUNC)err_advlock },
    { .opve_op = &vnop_bwrite_desc,  .opve_impl = (VOPFUNC)err_bwrite },
    { .opve_op = &vnop_pageout_desc, .opve_impl = (VOPFUNC)err_pageout },
    { .opve_op = &vnop_copyfile_desc,.opve_impl = (VOPFUNC)err_copyfile },
    { .opve_op = &vnop_blktooff_desc,.opve_impl = (VOPFUNC)err_blktooff },
    { .opve_op = &vnop_offtoblk_desc,.opve_impl = (VOPFUNC)err_offtoblk },
    { .opve_op = &vnop_blockmap_desc,.opve_impl = (VOPFUNC)err_blockmap },

    { .opve_op = (struct vnodeop_desc *)NULL, .opve_impl = (VOPFUNC)NULL }
};

const struct vnodeopv_desc xzsfs_vnodeop_opv_desc = {
    .opv_desc_vector_p = &xzsfs_vnodeop_p,
    .opv_desc_ops = xzsfs_vnodeop_entries
};

extern void xzs_bringup_console_write(const void *buf, int len);

static void
xzs_ubc_emit(const char *s)
{
    int n = 0;
    while (s[n] != '\0') n++;
    xzs_bringup_console_write(s, n);
}

void
xzs_diag_xzsfs_ubc(uint64_t user_path)
{
    char kpath[256];
    char line[128];
    size_t len = 0;
    int err;
    vnode_t vp = NULL;

    err = copyinstr((user_addr_t)user_path, kpath, sizeof(kpath), &len);
    if (err != 0) {
        snprintf(line, sizeof(line), "[XZS-UBC] copyinstr failed err=%d\n", err);
        xzs_ubc_emit(line);
        return;
    }

    err = vnode_lookup(kpath, 0, &vp, vfs_context_current());
    if (err != 0 || vp == NULL) {
        snprintf(line, sizeof(line), "[XZS-UBC] vnode_lookup failed path=%s err=%d\n", kpath, err);
        xzs_ubc_emit(line);
        return;
    }

    enum vtype vtype = vnode_vtype(vp);
    const char *vtype_str = "VOTHER";
    if (vtype == VREG) {
        vtype_str = "VREG";
    } else if (vtype == VDIR) {
        vtype_str = "VDIR";
    }

    uint64_t file_size = 0;
    struct xzsfs_node *node = (struct xzsfs_node *)vnode_fsnode(vp);
    if (node != NULL) {
        file_size = node->core.data_length;
    }

    int ubc_present = UBCINFOEXISTS(vp);
    off_t ubc_sz = 0;
    memory_object_control_t control = MEMORY_OBJECT_CONTROL_NULL;
    if (ubc_present) {
        ubc_sz = ubc_getsize(vp);
        control = ubc_getobject(vp, UBC_FLAGS_NONE);
    }

    int size_match = (ubc_present && ((uint64_t)ubc_sz == file_size));
    int control_not_null = (control != MEMORY_OBJECT_CONTROL_NULL);

    snprintf(line, sizeof(line), "PATH=%s\n", kpath);
    xzs_ubc_emit(line);
    snprintf(line, sizeof(line), "VNODE=%p\n", (void *)vp);
    xzs_ubc_emit(line);
    snprintf(line, sizeof(line), "VTYPE=%s\n", vtype_str);
    xzs_ubc_emit(line);
    snprintf(line, sizeof(line), "FILE_SIZE=%llu\n", (unsigned long long)file_size);
    xzs_ubc_emit(line);
    snprintf(line, sizeof(line), "UBC_INFO_PRESENT=%s\n", ubc_present ? "yes" : "no");
    xzs_ubc_emit(line);
    snprintf(line, sizeof(line), "UBC_SIZE=%lld\n", (long long)ubc_sz);
    xzs_ubc_emit(line);
    snprintf(line, sizeof(line), "SIZE_MATCH=%s\n", size_match ? "yes" : "no");
    xzs_ubc_emit(line);
    snprintf(line, sizeof(line), "MEMORY_OBJECT_CONTROL=%p\n", (void *)control);
    xzs_ubc_emit(line);
    snprintf(line, sizeof(line), "CONTROL_IS_NULL=%s\n", control_not_null ? "no" : "yes");
    xzs_ubc_emit(line);
    snprintf(line, sizeof(line), "RESULT=%s\n", (vtype == VREG && ubc_present && size_match && control_not_null) ? "PASS" : "FAIL");
    xzs_ubc_emit(line);

    vnode_put(vp);

    /* D7-T2N-4: Automatically perform Page 0 and EOF Pagecheck validations */
    xzs_diag_xzsfs_pagecheck(user_path, 0, 16384);
    xzs_diag_xzsfs_pagecheck(user_path, 16384, 16384);
}

void
xzs_diag_xzsfs_pagecheck(uint64_t user_path, uint64_t f_offset, uint64_t req_size)
{
    char kpath[256];
    char line[160];
    size_t len = 0;
    int err;
    vnode_t vp = NULL;

    err = copyinstr((user_addr_t)user_path, kpath, sizeof(kpath), &len);
    if (err != 0) {
        snprintf(line, sizeof(line), "[XZS-PAGECHECK] copyinstr failed err=%d\n", err);
        xzs_ubc_emit(line);
        return;
    }

    err = vnode_lookup(kpath, 0, &vp, vfs_context_current());
    if (err != 0 || vp == NULL) {
        snprintf(line, sizeof(line), "[XZS-PAGECHECK] vnode_lookup failed path=%s err=%d\n", kpath, err);
        xzs_ubc_emit(line);
        return;
    }

    enum vtype vtype = vnode_vtype(vp);
    const char *vtype_str = (vtype == VREG) ? "VREG" : ((vtype == VDIR) ? "VDIR" : "VOTHER");

    uint64_t file_size = 0;
    struct xzsfs_node *node = (struct xzsfs_node *)vnode_fsnode(vp);
    if (node != NULL) {
        file_size = node->core.data_length;
    }

    if (vtype != VREG || node == NULL || node->xmp == NULL || node->xmp->devvp == NULL) {
        snprintf(line, sizeof(line), "[XZS-PAGECHECK] invalid vnode for pagecheck\n");
        xzs_ubc_emit(line);
        vnode_put(vp);
        return;
    }

    /* Determine valid file bytes in this requested range */
    uint64_t max_bytes = (file_size > f_offset) ? (file_size - f_offset) : 0;
    size_t valid_file_bytes = (req_size < max_bytes) ? (size_t)req_size : (size_t)max_bytes;
    size_t rounded_valid_bytes = (size_t)round_page_64(valid_file_bytes);
    if (rounded_valid_bytes > req_size) {
        rounded_valid_bytes = (size_t)req_size;
    }
    size_t zero_bytes = (rounded_valid_bytes > valid_file_bytes) ? (rounded_valid_bytes - valid_file_bytes) : 0;

    /* 1. SOURCE PATH (Independent Observation) */
    uint8_t *src_buf = (uint8_t *)kalloc_data(req_size, Z_WAITOK | Z_ZERO);
    size_t src_read_bytes = 0;
    int src_err = 0;
    if (valid_file_bytes > 0) {
        src_err = xzsfs_core_read(xzsfs_kernel_block_read, node->xmp->devvp,
                                  &node->core, f_offset, valid_file_bytes,
                                  src_buf, &src_read_bytes);
    }
    uint32_t src_csum = (src_err == 0 && src_read_bytes == valid_file_bytes) ?
                        xzsfs_crc32(0, src_buf, src_read_bytes) : 0;

    /* 2. PAGER PATH (Via UPL & XZSFS VNOP_PAGEIN) */
    upl_t upl = NULL;
    upl_page_info_t *pl = NULL;
    kern_return_t kr = ubc_create_upl_kernel(vp, (off_t)f_offset, (int)req_size,
                                             &upl, &pl, UPL_SET_LITE, VM_KERN_MEMORY_FILE);

    int pagein_err = -1;
    uint32_t pager_csum = 0;
    int valid_bytes_match = 0;
    size_t nonzero_tail_bytes = 0;
    int zero_tail_valid = 0;

    if (kr == KERN_SUCCESS && upl != NULL) {
        struct vnop_pagein_args ap;
        ap.a_desc = &vnop_pagein_desc;
        ap.a_vp = vp;
        ap.a_pl = upl;
        ap.a_pl_offset = 0;
        ap.a_f_offset = (off_t)f_offset;
        ap.a_size = req_size;
        ap.a_flags = UPL_NOCOMMIT;
        ap.a_context = vfs_context_current();

        pagein_err = xzsfs_vnop_pagein(&ap);
        if (pagein_err == 0) {
            vm_offset_t pager_addr = 0;
            kr = ubc_upl_map(upl, &pager_addr);
            if (kr == KERN_SUCCESS && pager_addr != 0) {
                uint8_t *pager_ptr = (uint8_t *)pager_addr;
                pager_csum = xzsfs_crc32(0, pager_ptr, valid_file_bytes);
                if (src_err == 0 && src_read_bytes == valid_file_bytes &&
                    src_csum == pager_csum &&
                    bcmp(src_buf, pager_ptr, valid_file_bytes) == 0) {
                    valid_bytes_match = 1;
                }
                for (size_t i = valid_file_bytes; i < rounded_valid_bytes; i++) {
                    if (pager_ptr[i] != 0) {
                        nonzero_tail_bytes++;
                    }
                }
                zero_tail_valid = (nonzero_tail_bytes == 0);
                ubc_upl_unmap(upl);
            }
            ubc_upl_commit_range(upl, 0, (upl_size_t)rounded_valid_bytes,
                                 UPL_COMMIT_FREE_ON_EMPTY | UPL_COMMIT_CLEAR_DIRTY);
            if (req_size > rounded_valid_bytes) {
                ubc_upl_abort_range(upl, (upl_offset_t)rounded_valid_bytes,
                                    (upl_size_t)(req_size - rounded_valid_bytes),
                                    UPL_ABORT_FREE_ON_EMPTY | UPL_ABORT_ERROR);
            }
        } else {
            ubc_upl_abort_range(upl, 0, (upl_size_t)req_size,
                                UPL_ABORT_FREE_ON_EMPTY | UPL_ABORT_ERROR);
        }
    }

    int overall_pass = (valid_bytes_match && zero_tail_valid && pagein_err == 0);

    /* Diagnostic output */
    snprintf(line, sizeof(line), "PATH=%s\n", kpath); xzs_ubc_emit(line);
    snprintf(line, sizeof(line), "VNODE=%p\n", (void *)vp); xzs_ubc_emit(line);
    snprintf(line, sizeof(line), "VTYPE=%s\n", vtype_str); xzs_ubc_emit(line);
    snprintf(line, sizeof(line), "FILE_SIZE=%llu\n", (unsigned long long)file_size); xzs_ubc_emit(line);
    snprintf(line, sizeof(line), "FILE_OFFSET=%llu\n", (unsigned long long)f_offset); xzs_ubc_emit(line);
    snprintf(line, sizeof(line), "REQUEST_SIZE=%llu\n", (unsigned long long)req_size); xzs_ubc_emit(line);
    snprintf(line, sizeof(line), "VALID_FILE_BYTES=%zu\n", valid_file_bytes); xzs_ubc_emit(line);
    snprintf(line, sizeof(line), "ROUNDED_VALID_BYTES=%zu\n", rounded_valid_bytes); xzs_ubc_emit(line);
    snprintf(line, sizeof(line), "ZERO_BYTES=%zu\n", zero_bytes); xzs_ubc_emit(line);
    snprintf(line, sizeof(line), "SOURCE_CHECKSUM=0x%08x\n", src_csum); xzs_ubc_emit(line);
    snprintf(line, sizeof(line), "PAGER_CHECKSUM=0x%08x\n", pager_csum); xzs_ubc_emit(line);
    snprintf(line, sizeof(line), "VALID_BYTES_MATCH=%s\n", valid_bytes_match ? "yes" : "no"); xzs_ubc_emit(line);
    if (zero_bytes > 0) {
        snprintf(line, sizeof(line), "SOURCE_VALID_CHECKSUM=0x%08x\n", src_csum); xzs_ubc_emit(line);
        snprintf(line, sizeof(line), "PAGER_VALID_CHECKSUM=0x%08x\n", pager_csum); xzs_ubc_emit(line);
        snprintf(line, sizeof(line), "VALID_CHECKSUM_MATCH=%s\n", valid_bytes_match ? "yes" : "no"); xzs_ubc_emit(line);
        snprintf(line, sizeof(line), "ZERO_TAIL_START=%zu\n", valid_file_bytes); xzs_ubc_emit(line);
        snprintf(line, sizeof(line), "ZERO_TAIL_LENGTH=%zu\n", zero_bytes); xzs_ubc_emit(line);
        snprintf(line, sizeof(line), "ZERO_TAIL_NONZERO_BYTES=%zu\n", nonzero_tail_bytes); xzs_ubc_emit(line);
    }
    snprintf(line, sizeof(line), "ZERO_TAIL_VALID=%s\n", zero_tail_valid ? "yes" : "no"); xzs_ubc_emit(line);
    snprintf(line, sizeof(line), "RESULT=%s\n", overall_pass ? "PASS" : "FAIL"); xzs_ubc_emit(line);

    kfree_data(src_buf, req_size);
    vnode_put(vp);
}

