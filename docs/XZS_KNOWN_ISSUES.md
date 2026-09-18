# Xperia XZs / MSM8996 Known Issues & Engineering Audit

This document details all known anomalies, hardware behaviors, and logging inconsistencies identified during the Phase D1 bring-up on Sony Xperia XZs (Qualcomm MSM8996).

---

## 1. Warm Reboot Nondeterminism

### Description
Under cold-reset conditions (device powered off completely, cold-started via hardware keys or fresh power cycle), the kernel execution deterministically reaches the Phase D1 acceptance gate:
```text
[D45] -> [D46a] -> [D47] -> [D48a] -> [D49a] -> [D50] -> [D50-I0..I8] -> [D50c] -> [D51] -> [D51b] error=0x13 -> [D51-TERMINAL]
```
The kernel cleanly triggers `xzs_spin_halt()`, writing `0x77665500` to IMEM SRAM (`0x066bf65c`) and arming the APCS watchdog bite, rebooting the phone back to Fastboot within **+6 seconds**.

However, under repeated warm reboots (e.g. issuing `fastboot reboot bootloader`, executing consecutive warm cycles without complete power cut, or multiple rapid deployments):
* The kernel execution can intermittently stall prior to D50, observed at checkpoints such as `[D43]` (`memorystatus_init`), `[D44]` (`bsd_autoconf`), `[D47]` (`ether_family_init`), or `[D50-I5]` (`IOFindBSDRoot`).
* When a stall occurs, the APCS watchdog bite timer triggers after **~39 seconds**, resetting the device back to Fastboot.

### Empirical Evidence & Current Status
* **Root Cause**: **UNKNOWN**.
* **Prohibited Speculation**: It is strictly prohibited to assert unverified root causes (such as dirty DRAM residual state, thermal throttling, TrustZone / QSEE state retention, or VM allocator lock contention) without direct empirical instrumentation or trace evidence.
* **Reproduction Rule**: To ensure 100% reproducible baseline execution, a cold-reset procedure must be observed (see [`docs/XZS_HARDWARE_TESTING.md`](XZS_HARDWARE_TESTING.md)).

### Classification
```text
Classification: KNOWN ISSUE
Root Cause:     UNKNOWN (under active hardware investigation)
Impact:         Development / rapid testing cycles require cold-reset discipline.
```

---

## 2. IOFindBSDRoot Error Logging Inconsistency

### Description
During root-device selection in `setconf()`, console logs and diagnostic strings exhibit a numeric mismatch:
* The diagnostic log string in `IOKitBSDInit.cpp` literally printed:
  ```text
  [XZS-BOOT] [D50-IOKIT] IOFindBSDRoot: returning kIOReturnNotFound (0xe00002bc)
  ```
* While the actual numerical return value received and printed by `setconf()` is:
  ```text
  [XZS-BOOT] [D50b] IOFindBSDRoot RETURN err=0x0xe00002f0
  setconf: IOFindBSDRoot returned an error (-536870160); setting rootdevice to 'sd0a'.
  ```

### Constant Audit & Verification
A source audit of `src/xnu/iokit/IOKit/IOReturn.h` resolves this discrepancy completely:

```c
#define sub_iokit_common        err_sub(0)
#define iokit_common_err(return) (sys_iokit|sub_iokit_common|(return))

#define kIOReturnError          iokit_common_err(0x2bc) // 0xe00002bc: general error
#define kIOReturnNotFound       iokit_common_err(0x2f0) // 0xe00002f0: data was not found
```

* `kIOReturnError` is numerically `0xe00002bc`.
* `kIOReturnNotFound` is numerically `0xe00002f0`.
* In `signed int32`, `(int)0xe00002f0 == -536870160`.

**Conclusion**: The C++ implementation in `IOKitBSDInit.cpp` correctly returns the canonical symbol `kIOReturnNotFound` (`0xe00002f0`). The diagnostic text in the telemetry string contained a hardcoded human typo `(0xe00002bc)`. The runtime behavior is 100% canonical and correct; only the cosmetic log string is mismatched.

### Classification
```text
Classification: COSMETIC LOG MISMATCH
Root Cause:     Hardcoded string typo in diagnostic puts; audited value is canonical kIOReturnNotFound.
Status:         Audited and documented. Retained for exact D1 freeze compatibility.
```

---

## 3. Cosmetic Telemetry Duplication (`0x0x...`)

### Description
Certain early console messages print duplicate hex prefixes, such as:
```text
[XZS-BOOT] [D50b] IOFindBSDRoot RETURN err=0x0xe00002f0
[XZS-BOOT] [D51b] bdevvp(rootdev, ...) error=0x0x13
```

### Source Audit
The diagnostic function `xzs_early_puthex64` in `osfmk/arm64/start.s` automatically emits the `"0x"` prefix before outputting hexadecimal digits:
```assembly
mov     w0, #'0'
bl      EXT(xzs_early_putc)
mov     w0, #'x'
bl      EXT(xzs_early_putc)
```
When callers in C code invoke:
```c
xzs_early_puts("... err=0x");
xzs_early_puthex64(err);
```
the result is a duplicated `"0x0x"`.

### Classification
```text
Classification: COSMETIC TELEMETRY
Impact:         None. Hexadecimal values are 100% valid and parseable.
Status:         Scheduled for formatting cleanup during Phase G.
```

---

## 4. Deferred Networking Subsystems

### Description
The following networking subsystems are temporarily deferred under `CONFIG_XZS_BRINGUP` to isolate the BSD/VFS root-device boundary from non-storage lock contention:
1. **Skywalk** (`skywalk_init`): Deferred in `bsd_init.c`.
2. **Loopback Interface** (`lo0` / `loopattach`): Deferred in `bsd_init.c`.
3. **IPv6 Tunnel Interface** (`gif0` / `gif_init`): Deferred in `bsd_init.c`.
4. **Ethernet Family** (`ether_family_init`): Deferred in `bsd_init.c`.
5. **TCP Fast Open** (`tcp_fastopen = 0`): Deferred in `tcp_subr.c`.
6. **TCP Congestion Control Debug** (`tcp_ccdbg_control_register`): Deferred in `tcp_cc.c`.

### Impact & Plan
* No network interfaces (`lo0`, `en0`, `gif0`) are instantiated.
* Sockets and domain tables are initialized, but packet routing is inactive.
* These components will be systematically restored during **Phase H (Networking / Device Drivers)** after root filesystem and storage drivers are operational.

### Classification
```text
Classification: ARCHITECTURAL WORKAROUND (Intentional Deferral)
Status:         Documented in docs/XZS_WORKAROUNDS.md; scheduled for Phase H.
```
