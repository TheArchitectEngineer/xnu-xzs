# XNU-XZS Current Session Handoff

```text
FINAL_D6_MAIN_COMMIT=cc297b0e721ea0c002e008a4a330ac1aa3c9a3b3

D6_FINAL_HARDWARE_COMMIT=f5dd7a36bfedfbcf68d7a6bf2a95d4d09dab5ac0
D6_FINAL_SEAL_COMMIT=f57fee97e72d05b6ecee2563b8edb21ba283315a
D6_MAIN_MERGE_COMMIT=cc297b0e721ea0c002e008a4a330ac1aa3c9a3b3
D6_TAG=xzs-d6-userspace-complete

D7_M1_COMMIT=ef71c290913d3743ee32a3dc243d658ae8df80c6
D7_M2_HARDWARE_COMMIT=694446c0af85095e3045fb3d15bd12215aea203e
D7_M2_BOOT_IMAGE_SHA256=a78af05bbd39e2c93a04d3b3384d885a83005e40b25dd6cb5b7140795c71e24e
D7_M2_TAG=xzs-d7m2-complete

D7_M3_BOOT_IMAGE_SHA256=c2b66cea0a6475bc140e729b2827e41d636b855f2a29f7619a56c5d1cccbe68c
D7_M3_TAG=xzs-d7m3-complete

D7_M4_INTERNAL_HARDWARE_COMMIT=639fe1c42478c8ca07aa0019cbe59bfd846d9866
D7_M4_INTERNAL_BOOT_IMAGE_SHA256=329f869a55e7185bda99d14f38575d13c693298b04c7fa0b87f5d3439b34044a
D7_M4_INTERNAL_RAW_LOG_SHA256=49422168deee99ad9c8fa3b5d07adb70edf6c367b185174e44cb8e3ff769d469

LAST_SEALED_MILESTONE=D7-M3

CURRENT_BRANCH=xzs-d7m4-readiness

NEXT_PHASE=D7
NEXT_MILESTONE=D7-M4

D7_M1_STATUS=COMPLETE
D7_M2_STATUS=COMPLETE / SEALED
D7_M3_STATUS=COMPLETE / SEALED
D7_M4_P0_READINESS_STATUS=COMPLETE
D7_M4_INTERNAL_PIPELINE_STATUS=HARDWARE VERIFIED
D7_M4_EXTERNAL_PIPELINE_STATUS=NOT VERIFIED
D7_M4_STATUS=IN PROGRESS / NOT SEALED

NEXT_GOAL=
Phase D7-M4 final gate:
  host TX sends 41 42 43 0a
  external GPIO5 RX reaches UARTDM IRQ
  common ring/deferred tty/read(0) pipeline returns exact bytes to EL0
  external-mode verifier passes

KNOWN_BLOCKER=
External host USB-UART TX / Xperia GPIO5 physical path has not yet produced immutable acceptance evidence.

SHELL_BINARY_SHA256=848a10da132fb4482c3cae01a35a73fb6fe4a79bf9e170800489d12f3fbb7bd3

CURRENT_KNOWN_PLATFORM_WORKAROUNDS=
- devfs_getattr pointer-hardening workaround / 3e417bb (bypasses vm_kernel_addrhash SHA-256 hang; returns fsid 0x64657666; to be re-audited in D9 XZSPlatform)
- memorystatus static jetsam buffer / kern_memorystatus.c (pre-allocated static snapshot buffer)
- thread_call zone priming / thread_call.c (pre-allocates 105 elements to avoid early zone lock contention)
- non-Apple silicon CTRR compatibility / machine_routines.c (marks unsafe_kernel_text false without Apple DT CTRR property)
- tcp_tfo_init deferral / tcp_subr.c (defers AES-128 random key generation until crypto provider is available)
- skywalk_init deferral / bsd_init.c (defers packet networking arena allocations)
- dtrace_fbt deferral / fbt.c (defers kernel-wide function boundary tracing instrumentation)

NEXT_EXACT_ACTION=
Run the immutable external UART acceptance candidate at 115200 8N1 with host bytes 41 42 43 0a; do not seal M4 unless the external-mode verifier passes.
```
