/*
 * xnu-xzs Milestone D13: GPU / Accelerator IOKit Stack (Adreno 530 + IOSurfaceAccelerator + IOAccelerator)
 *
 * Provides:
 *   1. AppleM2ScalerCSCDriver & AppleM2ScalerCSCDriverUserClient:
 *      Kernel hardware/SWAR-accelerated surface blitter, scaler, and CSC backing
 *      IOSurfaceAccelerator.framework and IOSurface.framework.
 *   2. IOAccelerator / IOAcceleratorES / IOGraphicsAccelerator2 / AGXAcceleratorG10P / XZSAccelerator:
 *      Full IOAccelerator user client hierarchy (Device type 1, Shared type 2,
 *      Context types 0/3, CommandQueue type 4) bridged to the bare-metal Qualcomm
 *      Adreno 530 (A530v2 @ 0x00b00000) hardware engine.
 */

#ifndef _XZS_GPU_IOKIT_H_
#define _XZS_GPU_IOKIT_H_

#include <pexpert/arm64/board_config.h>

#if defined(SWEET) || defined(XZS)
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOBufferMemoryDescriptor.h>

/* Adreno 530 (A5xx) PM4 Type-7 CP ringbuffer packet helpers */
#ifndef CP_NOP
#define CP_NOP                  0x10u
#endif
#ifndef CP_TYPE7_PKT
static inline uint32_t
xzs_cp_odd_parity_bit(uint32_t val)
{
	val ^= val >> 16;
	val ^= val >> 8;
	val ^= val >> 4;
	val &= 0xfu;
	return ((~0x6996u >> val) & 1u);
}
#define CP_TYPE7_PKT(opcode, cnt) \
	((3u << 28) | (((cnt) & 0x7fffu) << 0) | \
	 (xzs_cp_odd_parity_bit((cnt) & 0x7fffu) << 15) | \
	 (((opcode) & 0x7fu) << 16) | \
	 (xzs_cp_odd_parity_bit((opcode) & 0x7fu) << 23))
#endif

extern "C" {
bool xzs_gpu_boot_init(void);
bool xzs_gpu_is_ready(void);
uint32_t xzs_gpu_get_hw_version(void);
uint32_t xzs_gpu_get_rbbm_status(void);
uint32_t xzs_gpu_get_speed_bin(void);
uintptr_t xzs_gpu_get_vram_va(void);
uint32_t xzs_gpu_submit_cp_ring(const uint32_t *dwords, uint32_t count);
void xzs_gpu_record_blit(uint64_t bytes);
void xzs_gpu_record_fill(uint64_t bytes);
void xzs_gpu_record_event(void);
}

/* Kernel-callable surface blit/transform helper shared with XZSSurface */
IOReturn xzs_m2scaler_blit_by_id(uint32_t srcID, uint32_t dstID,
    int32_t sx, int32_t sy, int32_t sw, int32_t sh,
    int32_t dx, int32_t dy, int32_t dw, int32_t dh,
    uint64_t flags);

#endif /* SWEET || XZS */
#endif /* _XZS_GPU_IOKIT_H_ */
