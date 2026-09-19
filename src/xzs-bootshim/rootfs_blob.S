/*
 * rootfs_blob.S - Embedded XZSFS v1 rootfs image for RAMDisk bootstrap
 * Sony Xperia XZs (MSM8996)
 */

.section ".rodata.xzsfs", "a"
.balign 512

.global xzs_rootfs_img
.global xzs_rootfs_img_end

xzs_rootfs_img:
    .incbin "../../artifacts/builds/xzs-rootfs.img"
xzs_rootfs_img_end:
