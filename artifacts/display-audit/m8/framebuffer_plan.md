# D8-M8 Artifact: Framebuffer Memory & Pattern Plan

## 1. Framebuffer Parameters
- **Geometry:** 1080 (width) x 1920 (height)
- **Format:** `XRGB8888` (32 bpp, 4 bytes/pixel)
- **Raw Row Bytes:** 4320 bytes (`1080 * 4`)
- **Hardware Aligned Stride:** 4352 bytes (`0x1100`, 128-byte alignment required by MDP5 DMA and proven by `/sys/class/graphics/fb0/stride`)
- **Total Memory Required:** 8,355,840 bytes (`4352 * 1920` = ~7.97 MiB)
- **Buffer Alignment:** 128-byte boundary requirement for MDP DMA
- **Addressing Mode:** Physical memory addressing (SMMU in bypass mode)
- **Candidate Physical Region:** `0x83401000` (Qualcomm continuous splash memory region in device tree) or dynamically allocated kernel pages.

## 2. Test Patterns
1. **SOLID RED:**
   - Byte pattern: `0x00, 0x00, 0xFF, 0x00` (Little-endian: `0x00FF0000`)
   - Channel Verification: Immediately proves whether Red and Blue channels are swapped or aligned correctly.
2. **SOLID GREEN:**
   - Byte pattern: `0x00, 0xFF, 0x00, 0x00`
3. **SOLID BLUE:**
   - Byte pattern: `0xFF, 0x00, 0x00, 0x00`
4. **SOLID WHITE:**
   - Byte pattern: `0xFF, 0xFF, 0xFF, 0x00`
5. **SOLID BLACK:**
   - Byte pattern: `0x00, 0x00, 0x00, 0x00`
6. **COLOR BARS:**
   - Vertical 8-bar test pattern (White, Yellow, Cyan, Green, Magenta, Red, Blue, Black).

## 3. Cache Maintenance Policy
- CPU write writes must be cleaned to the Point of Coherency (`dc cvac` via `CleanPoC_DcacheRegion_Force`) over the virtual address range before writing `CTL_START`.
