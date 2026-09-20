/*
 * XZSFS Core — Pure Parser and Validation Engine
 *
 * Designed for both in-kernel VFS driver and host unit testing.
 * Independent implementation — does NOT share code with host generator or verifier.
 */

#ifndef _XZSFS_CORE_H_
#define _XZSFS_CORE_H_

#include <stdint.h>
#include <stddef.h>

#define XZSFS_MAGIC             0x5346535AU  /* "XZSF" in little-endian */
#define XZSFS_VERSION           1U
#define XZSFS_SECTOR_SIZE       512U
#define XZSFS_HEADER_SIZE       512U
#define XZSFS_MAX_OBJECTS       32U
#define XZSFS_MAX_STRTAB_SIZE   2048U

#define XZSFS_TYPE_DIR          1U
#define XZSFS_TYPE_REG          2U

#define XZSFS_ERR_OK            0
#define XZSFS_ERR_INVAL         22   /* EINVAL */
#define XZSFS_ERR_NOENT          2   /* ENOENT */
#define XZSFS_ERR_NOTDIR        20   /* ENOTDIR */
#define XZSFS_ERR_ROFS          30   /* EROFS */
#define XZSFS_ERR_IO             5   /* EIO */

#pragma pack(push, 1)

/* 512-byte on-disk superblock (Sector 0) */
struct xzsfs_disk_superblock {
    uint32_t magic;
    uint32_t format_version;
    uint32_t header_size;
    uint32_t sector_size;
    uint64_t image_size_bytes;
    uint32_t object_count;
    uint32_t root_object_id;
    uint64_t object_table_offset;
    uint64_t object_table_size;
    uint64_t string_table_offset;
    uint64_t string_table_size;
    uint64_t data_offset;
    uint64_t data_size;
    uint32_t metadata_crc32;
    uint32_t reserved;
    uint8_t  padding[424];
};

/* 64-byte on-disk object entry (Sectors 1..M) */
struct xzsfs_disk_object {
    uint32_t object_id;
    uint32_t parent_id;
    uint32_t type;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t name_offset;
    uint32_t name_length;
    uint64_t data_offset;
    uint64_t data_length;
    uint64_t reserved1;
    uint64_t reserved2;
};

#pragma pack(pop)

/* In-memory parsed node */
struct xzsfs_core_node {
    uint32_t    object_id;
    uint32_t    parent_id;
    uint32_t    type;
    uint32_t    mode;
    uint32_t    uid;
    uint32_t    gid;
    uint64_t    data_offset;
    uint64_t    data_length;
    const char *name;
    uint32_t    name_length;
};

/* In-memory filesystem instance */
struct xzsfs_core_fs {
    struct xzsfs_disk_superblock sb;
    uint32_t                     node_count;
    struct xzsfs_core_node       nodes[XZSFS_MAX_OBJECTS];
    char                         strtab[XZSFS_MAX_STRTAB_SIZE];
};

/* Abstract block read callback */
typedef int (*xzsfs_block_read_fn)(void *ctx, uint64_t lba, uint32_t count, void *buf);

/* CRC32 IEEE 802.3 calculation */
uint32_t xzsfs_crc32(uint32_t crc, const void *buf, size_t len);

/* Pure parser routines */
int xzsfs_core_read_superblock(xzsfs_block_read_fn read_fn, void *ctx, struct xzsfs_disk_superblock *sb);
int xzsfs_core_validate_superblock(const struct xzsfs_disk_superblock *sb);
int xzsfs_core_load_objects(xzsfs_block_read_fn read_fn, void *ctx, const struct xzsfs_disk_superblock *sb,
                            struct xzsfs_disk_object *objects, uint32_t max_objects);
int xzsfs_core_load_strtab(xzsfs_block_read_fn read_fn, void *ctx, const struct xzsfs_disk_superblock *sb,
                           char *strtab, size_t max_strtab);
int xzsfs_core_load_metadata(xzsfs_block_read_fn read_fn, void *ctx, const struct xzsfs_disk_superblock *sb,
                             struct xzsfs_disk_object *objects, uint32_t max_objects,
                             char *strtab, size_t max_strtab);
int xzsfs_core_validate_metadata_crc(const struct xzsfs_disk_superblock *sb,
                                     const struct xzsfs_disk_object *objects,
                                     const char *strtab);
int xzsfs_core_validate_graph(const struct xzsfs_disk_superblock *sb,
                              const struct xzsfs_disk_object *objects,
                              const char *strtab);
int xzsfs_core_validate_metadata(const struct xzsfs_disk_superblock *sb,
                                const struct xzsfs_disk_object *objects,
                                const char *strtab);
int xzsfs_core_init_fs(struct xzsfs_core_fs *fs,
                       const struct xzsfs_disk_superblock *sb,
                       const struct xzsfs_disk_object *objects,
                       const char *strtab);
int xzsfs_core_lookup(const struct xzsfs_core_fs *fs, uint32_t parent_id,
                      const char *name, size_t namelen,
                      const struct xzsfs_core_node **node_out);
int xzsfs_core_read(xzsfs_block_read_fn read_fn, void *ctx,
                    const struct xzsfs_core_node *node,
                    uint64_t offset, size_t len, void *buf, size_t *bytes_read);

#endif /* _XZSFS_CORE_H_ */
