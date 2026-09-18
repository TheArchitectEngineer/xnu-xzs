#!/bin/bash
set -euo pipefail

mkdir -p artifacts/builds

echo "=== Packaging artifacts/builds/xzs-xnu-boot.img ==="

# 1. Rebuild bootshim if needed
make -C src/xzs-bootshim all

# 2. Compress bootshim with gzip and append genuine DTB (matching TWRP Image.gz-dtb format)
gzip -n -9 -c src/xzs-bootshim/bootshim.bin > src/xzs-bootshim/bootshim.bin.gz
cat src/xzs-bootshim/bootshim.bin.gz artifacts/builds/twrp-extracted.dtb > src/xzs-bootshim/bootshim-gz-dtb.bin
echo "Created Image.gz-dtb: bootshim.gz ($(wc -c < src/xzs-bootshim/bootshim.bin.gz) B) + DTB ($(wc -c < artifacts/builds/twrp-extracted.dtb) B) = total $(wc -c < src/xzs-bootshim/bootshim-gz-dtb.bin) B"

# 3. Package boot.img with bootshim as kernel and XNU Mach-O as ramdisk (at 0x82000000)
KERNEL_XNU="src/xnu/BUILD/obj/DEVELOPMENT_ARM64_VMAPPLE/kernel.development.vmapple"

if [ "${STANDALONE:-0}" != "1" ] && [ -f "${KERNEL_XNU}" ]; then
    echo "Found compiled XNU kernel: ${KERNEL_XNU}"
    echo "Flattening Mach-O segments to 1:1 physical memory mapping..."
    python3 scripts/flatten_macho.py "${KERNEL_XNU}" artifacts/builds/kernel.flat
    python3 scripts/mkbootimg.py \
      --kernel src/xzs-bootshim/bootshim-gz-dtb.bin \
      --ramdisk artifacts/builds/kernel.flat \
      --base 0x80000000 \
      --kernel_offset 0x00008000 \
      --ramdisk_offset 0x02200000 \
      --tags_offset 0x02000000 \
      --cmdline "console=ttyMSM0,115200 earlycon=msm_serial_dm,0x075b0000 debug=0x14e serial=2 -v keepends=1" \
      --pagesize 4096 \
      -o artifacts/builds/xzs-xnu-boot.img
else
    echo "Warning: XNU kernel not found, packaging standalone bootshim..."
    python3 scripts/mkbootimg.py \
      --kernel src/xzs-bootshim/bootshim-gz-dtb.bin \
      --base 0x80000000 \
      --kernel_offset 0x00008000 \
      --cmdline "console=ttyMSM0,115200 earlycon=msm_serial_dm,0x075b0000" \
      --pagesize 4096 \
      -o artifacts/builds/xzs-xnu-boot.img
fi

echo "=== Generated artifacts/builds/xzs-xnu-boot.img ==="
ls -lh artifacts/builds/xzs-xnu-boot.img
echo "=== SHA256: $(sha256sum artifacts/builds/xzs-xnu-boot.img | awk '{print $1}') ==="
