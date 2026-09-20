/*
 * XZSFS Core — Pure Parser and Validation Engine Implementation
 *
 * Designed for both in-kernel VFS driver and host unit testing.
 * Independent implementation — does NOT share code with host generator or verifier.
 */

#include "xzsfs_core.h"

#ifdef KERNEL
#include <libkern/libkern.h>
#else
#include <string.h>
#endif

static const uint32_t xzsfs_crc32_table[16] = {
    0x00000000U, 0x1db71064U, 0x3b6e20c8U, 0x26d930acU,
    0x76dc4190U, 0x6b6b51f4U, 0x4db26158U, 0x5005713cU,
    0xedb88320U, 0xf00f9344U, 0xd6d6a3e8U, 0xcb61b38cU,
    0x9b64c2b0U, 0x86d3d2d4U, 0xa00ae278U, 0xbdbdf21cU
};

uint32_t xzsfs_crc32(uint32_t crc, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        uint8_t byte = p[i];
        crc = (crc >> 4) ^ xzsfs_crc32_table[(crc ^ (byte & 0x0fU)) & 0x0fU];
        crc = (crc >> 4) ^ xzsfs_crc32_table[(crc ^ (byte >> 4)) & 0x0fU];
    }
    return ~crc;
}

int xzsfs_core_read_superblock(xzsfs_block_read_fn read_fn, void *ctx, struct xzsfs_disk_superblock *sb) {
    if (!read_fn || !sb) return XZSFS_ERR_INVAL;
    uint8_t sec_buf[XZSFS_SECTOR_SIZE];
    int err = read_fn(ctx, 0, 1, sec_buf);
    if (err) return err;
    memcpy(sb, sec_buf, sizeof(struct xzsfs_disk_superblock));
    return XZSFS_ERR_OK;
}

int xzsfs_core_validate_superblock(const struct xzsfs_disk_superblock *sb) {
    if (!sb) return XZSFS_ERR_INVAL;
    if (sb->magic != XZSFS_MAGIC) return XZSFS_ERR_INVAL;
    if (sb->format_version != XZSFS_VERSION) return XZSFS_ERR_INVAL;
    if (sb->header_size != XZSFS_HEADER_SIZE) return XZSFS_ERR_INVAL;
    if (sb->sector_size != XZSFS_SECTOR_SIZE) return XZSFS_ERR_INVAL;
    if (sb->image_size_bytes % XZSFS_SECTOR_SIZE != 0) return XZSFS_ERR_INVAL;
    if (sb->image_size_bytes < (4U * XZSFS_SECTOR_SIZE)) return XZSFS_ERR_INVAL;
    if (sb->object_count == 0 || sb->object_count > XZSFS_MAX_OBJECTS) return XZSFS_ERR_INVAL;
    if (sb->root_object_id != 1U) return XZSFS_ERR_INVAL;
    if (sb->object_table_offset != XZSFS_SECTOR_SIZE) return XZSFS_ERR_INVAL;
    if (sb->object_table_size != ((uint64_t)sb->object_count * sizeof(struct xzsfs_disk_object))) return XZSFS_ERR_INVAL;
    if (sb->string_table_offset < sb->object_table_offset + sb->object_table_size) return XZSFS_ERR_INVAL;
    if (sb->string_table_offset % XZSFS_SECTOR_SIZE != 0) return XZSFS_ERR_INVAL;
    if (sb->string_table_size == 0 || sb->string_table_size > XZSFS_MAX_STRTAB_SIZE) return XZSFS_ERR_INVAL;
    if (sb->string_table_size % XZSFS_SECTOR_SIZE != 0) return XZSFS_ERR_INVAL;
    if (sb->data_offset < sb->string_table_offset + sb->string_table_size) return XZSFS_ERR_INVAL;
    if (sb->data_offset % XZSFS_SECTOR_SIZE != 0) return XZSFS_ERR_INVAL;
    if (sb->data_offset + sb->data_size > sb->image_size_bytes) return XZSFS_ERR_INVAL;
    return XZSFS_ERR_OK;
}

int xzsfs_core_load_objects(xzsfs_block_read_fn read_fn, void *ctx, const struct xzsfs_disk_superblock *sb,
                            struct xzsfs_disk_object *objects, uint32_t max_objects) {
    if (!read_fn || !sb || !objects) return XZSFS_ERR_INVAL;
    if (sb->object_count > max_objects) return XZSFS_ERR_INVAL;

    /* Load object table sectors */
    uint64_t obj_lba = sb->object_table_offset / XZSFS_SECTOR_SIZE;
    uint32_t obj_sec_count = (uint32_t)((sb->string_table_offset - sb->object_table_offset) / XZSFS_SECTOR_SIZE);
    uint8_t *obj_dest = (uint8_t *)objects;
    for (uint32_t s = 0; s < obj_sec_count; s++) {
        uint8_t sec[XZSFS_SECTOR_SIZE];
        int err = read_fn(ctx, obj_lba + s, 1, sec);
        if (err) return err;
        size_t to_copy = XZSFS_SECTOR_SIZE;
        size_t copied = s * XZSFS_SECTOR_SIZE;
        if (copied + to_copy > sb->object_table_size) {
            to_copy = (size_t)(sb->object_table_size - copied);
        }
        if (to_copy > 0) {
            memcpy(obj_dest + copied, sec, to_copy);
        }
    }
    return XZSFS_ERR_OK;
}

int xzsfs_core_load_strtab(xzsfs_block_read_fn read_fn, void *ctx, const struct xzsfs_disk_superblock *sb,
                           char *strtab, size_t max_strtab) {
    if (!read_fn || !sb || !strtab) return XZSFS_ERR_INVAL;
    if (sb->string_table_size > max_strtab) return XZSFS_ERR_INVAL;

    /* Load string table sectors */
    uint64_t str_lba = sb->string_table_offset / XZSFS_SECTOR_SIZE;
    uint32_t str_sec_count = (uint32_t)(sb->string_table_size / XZSFS_SECTOR_SIZE);
    for (uint32_t s = 0; s < str_sec_count; s++) {
        int err = read_fn(ctx, str_lba + s, 1, strtab + (s * XZSFS_SECTOR_SIZE));
        if (err) return err;
    }
    return XZSFS_ERR_OK;
}

int xzsfs_core_load_metadata(xzsfs_block_read_fn read_fn, void *ctx, const struct xzsfs_disk_superblock *sb,
                             struct xzsfs_disk_object *objects, uint32_t max_objects,
                             char *strtab, size_t max_strtab) {
    int err = xzsfs_core_load_objects(read_fn, ctx, sb, objects, max_objects);
    if (err) return err;
    return xzsfs_core_load_strtab(read_fn, ctx, sb, strtab, max_strtab);
}

int xzsfs_core_validate_metadata_crc(const struct xzsfs_disk_superblock *sb,
                                     const struct xzsfs_disk_object *objects,
                                     const char *strtab) {
    if (!sb || !objects || !strtab) return XZSFS_ERR_INVAL;

    /* Verify CRC32 of metadata area: Superblock (crc zeroed) + Object Table + String Table */
    struct xzsfs_disk_superblock sb_copy = *sb;
    sb_copy.metadata_crc32 = 0;
    uint32_t crc = xzsfs_crc32(0, &sb_copy, XZSFS_SECTOR_SIZE);

    /* Object table */
    crc = xzsfs_crc32(crc, objects, (size_t)sb->object_table_size);

    /* String table */
    crc = xzsfs_crc32(crc, strtab, (size_t)sb->string_table_size);

    if (crc != sb->metadata_crc32) {
        return XZSFS_ERR_INVAL; /* Bad metadata CRC32 */
    }
    return XZSFS_ERR_OK;
}

int xzsfs_core_validate_graph(const struct xzsfs_disk_superblock *sb,
                              const struct xzsfs_disk_object *objects,
                              const char *strtab) {
    if (!sb || !objects || !strtab) return XZSFS_ERR_INVAL;

    /* 2. Root object validation */
    if (objects[0].object_id != 1U || objects[0].parent_id != 1U || objects[0].type != XZSFS_TYPE_DIR) {
        return XZSFS_ERR_INVAL;
    }
    if (objects[0].name_offset != 0 || objects[0].name_length != 0) {
        return XZSFS_ERR_INVAL;
    }
    if (strtab[0] != '\0') {
        return XZSFS_ERR_INVAL;
    }

    /* 3. Validate every object */
    for (uint32_t i = 0; i < sb->object_count; i++) {
        const struct xzsfs_disk_object *obj = &objects[i];
        if (obj->object_id != i + 1U) return XZSFS_ERR_INVAL; /* Unique, strictly contiguous 1-based IDs */
        if (obj->type != XZSFS_TYPE_DIR && obj->type != XZSFS_TYPE_REG) return XZSFS_ERR_INVAL;

        if (i > 0) {
            /* Child object must have valid parent: strictly less than child ID (prevents all cycles!) */
            if (obj->parent_id == 0 || obj->parent_id >= obj->object_id) {
                return XZSFS_ERR_INVAL;
            }
            /* Parent object must be a directory */
            if (objects[obj->parent_id - 1U].type != XZSFS_TYPE_DIR) {
                return XZSFS_ERR_INVAL;
            }

            /* Filename validation */
            if (obj->name_length == 0 || (uint64_t)obj->name_offset + obj->name_length >= sb->string_table_size) {
                return XZSFS_ERR_INVAL; /* String offset out of bounds */
            }
            if (strtab[obj->name_offset + obj->name_length] != '\0') {
                return XZSFS_ERR_INVAL; /* Missing NUL termination */
            }
            const char *name = &strtab[obj->name_offset];
            for (uint32_t c = 0; c < obj->name_length; c++) {
                char ch = name[c];
                if (ch == '/' || ch == '\0') return XZSFS_ERR_INVAL;
            }

            /* No duplicate child names under the same parent directory */
            for (uint32_t j = 0; j < i; j++) {
                if (objects[j].parent_id == obj->parent_id) {
                    if (objects[j].name_length == obj->name_length &&
                        memcmp(&strtab[objects[j].name_offset], name, obj->name_length) == 0) {
                        return XZSFS_ERR_INVAL; /* Duplicate name under same parent */
                    }
                }
            }
        }

        /* Payload validation */
        if (obj->type == XZSFS_TYPE_DIR) {
            if (obj->data_offset != 0 || obj->data_length != 0) {
                return XZSFS_ERR_INVAL; /* Directory carrying invalid payload */
            }
        } else {
            if (obj->data_offset < sb->data_offset) return XZSFS_ERR_INVAL;
            if (obj->data_offset % XZSFS_SECTOR_SIZE != 0) return XZSFS_ERR_INVAL;
            if (obj->data_offset + obj->data_length > sb->image_size_bytes) return XZSFS_ERR_INVAL; /* Payload OOB */

            /* Check non-overlapping regular file extents */
            for (uint32_t j = 0; j < i; j++) {
                if (objects[j].type == XZSFS_TYPE_REG) {
                    uint64_t start_a = obj->data_offset;
                    uint64_t end_a = obj->data_offset + obj->data_length;
                    uint64_t start_b = objects[j].data_offset;
                    uint64_t end_b = objects[j].data_offset + objects[j].data_length;
                    if (start_a < end_b && start_b < end_a) {
                        return XZSFS_ERR_INVAL; /* Overlapping payload ranges */
                    }
                }
            }
        }
    }

    return XZSFS_ERR_OK;
}

int xzsfs_core_validate_metadata(const struct xzsfs_disk_superblock *sb,
                                const struct xzsfs_disk_object *objects,
                                const char *strtab) {
    int err = xzsfs_core_validate_metadata_crc(sb, objects, strtab);
    if (err) return err;
    return xzsfs_core_validate_graph(sb, objects, strtab);
}

int xzsfs_core_init_fs(struct xzsfs_core_fs *fs,
                       const struct xzsfs_disk_superblock *sb,
                       const struct xzsfs_disk_object *objects,
                       const char *strtab) {
    if (!fs || !sb || !objects || !strtab) return XZSFS_ERR_INVAL;
    fs->sb = *sb;
    fs->node_count = sb->object_count;
    memcpy(fs->strtab, strtab, (size_t)sb->string_table_size);

    for (uint32_t i = 0; i < sb->object_count; i++) {
        fs->nodes[i].object_id = objects[i].object_id;
        fs->nodes[i].parent_id = objects[i].parent_id;
        fs->nodes[i].type = objects[i].type;
        fs->nodes[i].mode = objects[i].mode;
        fs->nodes[i].uid = objects[i].uid;
        fs->nodes[i].gid = objects[i].gid;
        fs->nodes[i].data_offset = objects[i].data_offset;
        fs->nodes[i].data_length = objects[i].data_length;
        fs->nodes[i].name = &fs->strtab[objects[i].name_offset];
        fs->nodes[i].name_length = objects[i].name_length;
    }
    return XZSFS_ERR_OK;
}

int xzsfs_core_lookup(const struct xzsfs_core_fs *fs, uint32_t parent_id,
                      const char *name, size_t namelen,
                      const struct xzsfs_core_node **node_out) {
    if (!fs || !name || !node_out) return XZSFS_ERR_INVAL;

    if (parent_id == 0 || parent_id > fs->node_count) return XZSFS_ERR_INVAL;
    if (fs->nodes[parent_id - 1U].type != XZSFS_TYPE_DIR) return XZSFS_ERR_NOTDIR;

    if (namelen == 1 && name[0] == '.') {
        *node_out = &fs->nodes[parent_id - 1U];
        return XZSFS_ERR_OK;
    }
    if (namelen == 2 && name[0] == '.' && name[1] == '.') {
        uint32_t p = fs->nodes[parent_id - 1U].parent_id;
        *node_out = &fs->nodes[p - 1U];
        return XZSFS_ERR_OK;
    }

    for (uint32_t i = 0; i < fs->node_count; i++) {
        if (fs->nodes[i].parent_id == parent_id && i != 0) {
            if (fs->nodes[i].name_length == namelen &&
                memcmp(fs->nodes[i].name, name, namelen) == 0) {
                *node_out = &fs->nodes[i];
                return XZSFS_ERR_OK;
            }
        }
    }
    return XZSFS_ERR_NOENT;
}

int xzsfs_core_read(xzsfs_block_read_fn read_fn, void *ctx,
                    const struct xzsfs_core_node *node,
                    uint64_t offset, size_t len, void *buf, size_t *bytes_read) {
    if (!read_fn || !node || !buf || !bytes_read) return XZSFS_ERR_INVAL;
    if (node->type != XZSFS_TYPE_REG) return XZSFS_ERR_INVAL;

    if (offset >= node->data_length || len == 0) {
        *bytes_read = 0;
        return XZSFS_ERR_OK;
    }

    size_t to_read = len;
    if (offset + to_read > node->data_length) {
        to_read = (size_t)(node->data_length - offset);
    }

    uint8_t *out = (uint8_t *)buf;
    size_t done = 0;

    while (done < to_read) {
        uint64_t cur_pos = node->data_offset + offset + done;
        uint64_t lba = cur_pos / XZSFS_SECTOR_SIZE;
        uint32_t sec_offset = (uint32_t)(cur_pos % XZSFS_SECTOR_SIZE);
        uint32_t chunk = XZSFS_SECTOR_SIZE - sec_offset;
        if (chunk > (to_read - done)) {
            chunk = (uint32_t)(to_read - done);
        }

        uint8_t sec_buf[XZSFS_SECTOR_SIZE];
        int err = read_fn(ctx, lba, 1, sec_buf);
        if (err) return err;

        memcpy(out + done, sec_buf + sec_offset, chunk);
        done += chunk;
    }

    *bytes_read = done;
    return XZSFS_ERR_OK;
}
