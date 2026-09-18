/*
 * Copyright (c) 2021 Apple Inc. All rights reserved.
 */

#include <stdint.h>
#include <arm64/proc_reg.h>
#include <kern/clock.h>
#include <mach/mach_time.h>
#include <machine/atomic.h>
#include <machine/machine_routines.h>
#include <pexpert/device_tree.h>
#include <pexpert/arm64/board_config.h>


extern void xzs_early_puts(const char *s);
extern void xzs_early_puthex64(uint64_t val);

#if HAS_GIC_V3
#define GICR_WAKE_TIMEOUT_NS (1000000000ULL) // timeout for redistributor wakeup (default 1s)
MACHINE_TIMEOUT(gicr_wake_timeout_ns, "gicr-wake-timeout", GICR_WAKE_TIMEOUT_NS, MACHINE_TIMEOUT_UNIT_NSEC, NULL);

vm_offset_t gicd_base = 0;
vm_offset_t gicr_base = 0;
vm_offset_t gicr_size = 0;

static uint32_t
_gic_read32(vm_offset_t addr)
{
	return *((volatile uint32_t *) addr);
}

static uint64_t
_gic_read64(vm_offset_t addr)
{
	return *((volatile uint64_t *) addr);
}

static void
_gic_write32(vm_offset_t addr, uint32_t value)
{
	*((volatile uint32_t *) addr) = value;
}

#define gicd_read32(offset) (_gic_read32(gicd_base + (offset)))
#define gicd_write32(offset, data) (_gic_write32(gicd_base + (offset), (data)))
#define gicr_read32(offset) (_gic_read32(gicr_pe_base + (offset)))
#define gicr_write32(offset, data) (_gic_write32(gicr_pe_base + (offset), (data)))
#define gicr_read64(offset) (_gic_read64(gicr_pe_base + (offset)))

#ifndef QCOM_GICR_STRIDE
#define QCOM_GICR_STRIDE 0x40000
#endif

static vm_offset_t
find_gicr_pe_base(void)
{
	// We only care about aff1 and aff0
	uint32_t phys_id = (uint32_t)(__builtin_arm_rsr64("MPIDR_EL1") & (MPIDR_AFF1_MASK | MPIDR_AFF0_MASK));

	for (vm_offset_t offset = 0; offset < gicr_size; offset += QCOM_GICR_STRIDE) {
		vm_offset_t gicr_pe_base = gicr_base + offset;
		uint64_t gicr_typer = gicr_read64(GICR_TYPER);
		uint32_t aff_value = (uint32_t) ((gicr_typer >> GICR_TYPER_AFFINITY_VALUE_SHIFT) & (MPIDR_AFF1_MASK | MPIDR_AFF0_MASK));

		if (phys_id == aff_value) {
			xzs_early_puts("[GICv3] find_gicr_pe_base: phys_id=");
			xzs_early_puthex64(phys_id);
			xzs_early_puts(" matched gicr_pe_base=");
			xzs_early_puthex64(gicr_pe_base);
			xzs_early_puts("\n");
			return gicr_pe_base;
		}

		if (gicr_typer & GICR_TYPER_LAST) {
			break;
		}
	}

	panic("%s: cannot find GICR base for core %u", __func__, ml_get_cpu_number(phys_id));
}

void
pe_init_fiq(void)
{
	int error;
	DTEntry entry;

	xzs_early_puts("[GICv3] init ENTER\n");

	// gicd_base is 0x0 only before it's initialized by the boot processor.
	// Avoid calling SecureDT* routines on secondary processors to avoid
	// race conditions because they are not thread-safe.
	if (!gicd_base) {
		// Find GIC DT node
		error = SecureDTLookupEntry(NULL, "/arm-io/gic", &entry);
		if (error != kSuccess) {
			panic("%s: cannot find GIC node in DT", __func__);
		}

		// Find "reg" property
		void const *prop;
		unsigned int prop_size;
		error = SecureDTGetProperty(entry, "reg", &prop, &prop_size);
		if (error != kSuccess) {
			panic("%s: cannot find GIC MMIO regions in DT", __func__);
		}

		// Need at least GICD base, GICD size, GICR base and GICR size
		if (prop_size < 4 * sizeof(uint64_t)) {
			panic("%s: incorrect reg property size in GIC DT node; expecting 32 bytes but got %u bytes", __func__, prop_size);
		}

		vm_offset_t soc_base_phys = pe_arm_get_soc_base_phys();

		uint64_t const gicd_base_prop = ((uint64_t const *) prop)[0];
		uint64_t const gicd_size_prop = ((uint64_t const *) prop)[1];
		uint64_t const gicr_base_prop = ((uint64_t const *) prop)[2];
		uint64_t const gicr_size_prop = ((uint64_t const *) prop)[3];

		// Find GICD base address
		gicd_base = ml_io_map(soc_base_phys + gicd_base_prop, (vm_size_t)gicd_size_prop);

		if (!gicd_base) {
			panic("%s: cannot map GICD region", __func__);
		}

		// Find GICR base address
		gicr_base = ml_io_map(soc_base_phys + gicr_base_prop, (vm_size_t)gicr_size_prop);

		if (!gicr_base) {
			panic("%s: cannot map GICR region", __func__);
		}

		gicr_size = (vm_offset_t)gicr_size_prop;
	}

	// Enable Distributor forwarding for Group 1 Non-secure
	uint32_t gicd_ctlr = gicd_read32(GICD_CTLR);
	gicd_ctlr |= 0x3; // EnableGrp1A | EnableGrp1NS
	gicd_write32(GICD_CTLR, gicd_ctlr);
	xzs_early_puts("[GICv3] distributor ready (GICD_CTLR=");
	xzs_early_puthex64(gicd_read32(GICD_CTLR));
	xzs_early_puts(")\n");

	// Find the redistributor for this processor
	vm_offset_t gicr_pe_base = find_gicr_pe_base();

	// Mark this PE to be awake
	uint32_t gicr_waker = gicr_read32(GICR_WAKER);
	if (gicr_waker & GICR_WAKER_CHILDRENASLEEP) {
		gicr_waker &= ~GICR_WAKER_PROCESSORSLEEP;

		gicr_write32(GICR_WAKER, gicr_waker);

		uint64_t gicr_wake_deadline;
		nanoseconds_to_deadline(os_atomic_load(&gicr_wake_timeout_ns, relaxed), &gicr_wake_deadline);
		while (gicr_read32(GICR_WAKER) & GICR_WAKER_CHILDRENASLEEP) {
			// Spin
			if (gicr_wake_timeout_ns > 0 && mach_absolute_time() > gicr_wake_deadline) {
				panic("%s: core %u timed out waiting for redistributor to wake up",
				    __func__, ml_get_cpu_number_local());
			}
		}
	}
	xzs_early_puts("[GICv3] redistributor awake (GICR_WAKER=");
	xzs_early_puthex64(gicr_read32(GICR_WAKER));
	xzs_early_puts(")\n");

	// Configure SGIs 0-15 & PPIs 27, 30 to Group 1 Non-secure
	uint32_t igroupr0 = gicr_read32(GICR_IGROUPR0);
	igroupr0 |= 0x0000FFFF | (1U << 27) | (1U << 30);
	gicr_write32(GICR_IGROUPR0, igroupr0);

	// Set priority for SGIs 0-15 (offset 0x10400..0x1040C, priority 0x80)
	for (int p_idx = 0; p_idx < 4; p_idx++) {
		gicr_write32(0x10400 + p_idx * 4, 0x80808080);
	}

	// Set priority for PPI 27 in GICR_IPRIORITYR6 (offset 0x10418, bits 31:24)
	uint32_t prio = gicr_read32(0x10418);
	prio &= ~(0xffU << 24);
	prio |= (0x80U << 24); // Priority 0x80
	gicr_write32(0x10418, prio);

	// Enable SGIs 0-15, PPI 27 and PPI 30
	gicr_write32(GICR_ISENABLER0, 0x0000FFFF | (1U << 27) | (1U << 30));
	xzs_early_puts("[GICv3] timer PPI & SGI enabled (GICR_ISENABLER0=");
	xzs_early_puthex64(gicr_read32(GICR_ISENABLER0));
	xzs_early_puts(" GICR_IGROUPR0=");
	xzs_early_puthex64(gicr_read32(GICR_IGROUPR0));
	xzs_early_puts(")\n");

	// Enable system register access
	uint64_t icc_sre = __builtin_arm_rsr64("ICC_SRE_EL1");
	icc_sre |= ICC_SRE_SRE;
	__builtin_arm_wsr64("ICC_SRE_EL1", icc_sre);
	__builtin_arm_isb(ISB_SY);

	// Set priority masks and binary point for Group 1
	__builtin_arm_wsr64("ICC_BPR1_EL1", 0);
	__builtin_arm_wsr64("ICC_PMR_EL1", 0xFF);

	// Set EOI mode of this processor: drop priority and deactivate together
	uint64_t icc_ctlr = __builtin_arm_rsr64("ICC_CTLR_EL1");
	icc_ctlr &= ~ICC_CTLR_EOIMODE;
	__builtin_arm_wsr64("ICC_CTLR_EL1", icc_ctlr);

	// Enable Group 1 interrupts in CPU interface
	__builtin_arm_wsr64("ICC_IGRPEN1_EL1", 1);
	__builtin_arm_isb(ISB_SY);

	xzs_early_puts("[GICv3] CPU interface ready\n");
	xzs_early_puts("[GICv3] init RETURN\n");
}
#else
void
pe_init_fiq(void)
{
}
#endif
