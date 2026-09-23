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

const struct vnodeopv_entry_desc xzsfs_vnodeop_entries[] = {
    { .opve_op = &vnop_default_desc, .opve_impl = (VOPFUNC)(void *)vn_default_error },
    { .opve_op = &vnop_lookup_desc,  .opve_impl = (VOPFUNC)xzsfs_lookup },
    { .opve_op = &vnop_open_desc,    .opve_impl = (VOPFUNC)xzsfs_open },
    { .opve_op = &vnop_close_desc,   .opve_impl = (VOPFUNC)xzsfs_close },
    { .opve_op = &vnop_read_desc,    .opve_impl = (VOPFUNC)xzsfs_read },
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
}
