# Porting Status: XNU on Sony Xperia XZs (MSM8996)

> [!NOTE]
> This historical document has been superseded by:
> * [`docs/XZS_PORT_STATUS.md`](XZS_PORT_STATUS.md) — Executive 3-minute status overview.
> * [`docs/XZS_HARDWARE_VERIFICATION.md`](XZS_HARDWARE_VERIFICATION.md) — Comprehensive hardware verification matrix.
> * [`docs/XZS_ROADMAP.md`](XZS_ROADMAP.md) — Technical phase roadmap.

---

## Device Specification
- **Device**: Sony Xperia XZs (`G8231` / `tone` / `keyaki`)
- **SoC**: Qualcomm MSM8996 Snapdragon 820 (Kryo quad-core, ARMv8.0-A)
- **Constraint**: Sealed Device (no hardware disassembly, no soldering, telemetry via USB / fastboot / persistent DRAM only)

---

## Current Status Summary
* **Phase A (Native Kernel Entry)**: COMPLETE
* **Phase B (Platform Bring-up)**: COMPLETE for current scope
* **Phase C (Mach SMP Scheduler)**: COMPLETE for current scope
* **Phase D1 (BSD/VFS to Storage Boundary)**: COMPLETE
* **Phase D2 (Qualcomm UFS Controller)**: NOT STARTED
