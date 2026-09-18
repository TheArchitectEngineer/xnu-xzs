# Building XNU for Sony Xperia XZs (MSM8996)

This document provides a clean, reproducible guide to compiling Apple XNU for the Sony Xperia XZs (Qualcomm MSM8996 / Kryo).

---

## 1. Prerequisites & Toolchain

### Host Environment
* **Operating System**: macOS 14 (Sonoma) or macOS 15 (Sequoia).
* **Compiler / Toolchain**: Xcode with Apple Clang and macOS SDK.
  * Recommended: Xcode command-line tools or modern Xcode toolchain.
* **Dependencies**:
  * `python3` (for Mach-O flattening and boot image generation).
  * Standard Unix build tools: `make`, `grep`, `awk`, `gzip`.

---

## 2. Compilation Pipeline

### Step 1: Kernel Build
Compile XNU using the target `VMAPPLE` platform configuration with Kryo CPU optimizations and Apple PAC stripped:

```bash
# Configure DEVELOPER_DIR if using a specific Xcode installation
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"

make -C src/xnu \
    KERNEL_CONFIGS=DEVELOPMENT \
    ARCH_CONFIGS=ARM64 \
    MACHINE_CONFIGS=VMAPPLE \
    RC_DARWIN_KERNEL_VERSION=24.0.0 \
    build -j$(sysctl -n hw.ncpu)
```

The compiled Mach-O binary will be placed at:
```text
src/xnu/BUILD/obj/DEVELOPMENT_ARM64_VMAPPLE/kernel.development
```

---

## 3. Pointer Authentication (PAC) Audit Guardrail

Qualcomm Kryo is an ARMv8.0-A CPU. It does **not** support ARMv8.3-A Pointer Authentication. Any stray PAC instructions (`pacia`, `autia`, `braa`, `eretaa`, etc.) will trigger an immediate `ILL_INSTR` (illegal instruction) hardware exception at EL1.

Always execute the automated PAC verification script before packaging:

```bash
./scripts/check-no-pac.sh
```

**Expected Result**:
```text
[OK] Zero PAC instructions detected in kernel.development.
```

---

## 4. Packaging the Boot Image

To produce an Android-standard boot image compatible with Sony S1 ABOOT:

```bash
./scripts/package-boot.sh
```

### Packaging Architecture
1. **Flattening Mach-O**: `scripts/flatten_macho.py` extracts raw loadable segments from `kernel.development` into a flat binary payload.
2. **Bootshim Compilation**: Compiles `src/xzs-bootshim/` to wrap the kernel, parse the Qualcomm DTB, and generate the Apple Device Tree (ADT).
3. **DTB Concatenation**: Appends the Xperia XZs Qualcomm device tree (`device/tone-keyaki.dtb`).
4. **Boot Image Generation**: `scripts/mkbootimg.py` constructs `artifacts/builds/xzs-xnu-boot.img` with:
   * Kernel address: `0x82000000` (bootshim entry point).
   * RAM address / `boot_args`: `0x81800000`.
   * Page size: `4096`.
   * Kernel command line arguments: `debug=0x14e serial=0x3 cs_enforcement=0 -v`.

Output artifact:
```text
artifacts/builds/xzs-xnu-boot.img
```
