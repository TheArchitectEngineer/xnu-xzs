# XNU-XZS Current Session Handoff

```text
FINAL_D6_MAIN_COMMIT=cc297b0e721ea0c002e008a4a330ac1aa3c9a3b3



D6_FINAL_HARDWARE_COMMIT=f5dd7a36bfedfbcf68d7a6bf2a95d4d09dab5ac0
D6_FINAL_SEAL_COMMIT=f57fee97e72d05b6ecee2563b8edb21ba283315a
D6_MAIN_MERGE_COMMIT=cc297b0e721ea0c002e008a4a330ac1aa3c9a3b3

D6_TAG=xzs-d6-userspace-complete

LAST_SEALED_MILESTONE=D6-M7

NEXT_PHASE=D7
NEXT_MILESTONE=D7-M1

NEXT_GOAL=
interactive EL0 shell

KNOWN_BLOCKER=
MSM8996 UARTDM RX not implemented

CURRENT_KNOWN_PLATFORM_WORKAROUNDS=
- devfs_getattr pointer-hardening workaround / 3e417bb (bypasses vm_kernel_addrhash SHA-256 hang; returns fsid 0x64657666; to be re-audited in D9 XZSPlatform)
- memorystatus static jetsam buffer / kern_memorystatus.c (pre-allocated static snapshot buffer)
- thread_call zone priming / thread_call.c (pre-allocates 105 elements to avoid early zone lock contention)
- non-Apple silicon CTRR compatibility / machine_routines.c (marks unsafe_kernel_text false without Apple DT CTRR property)
- tcp_tfo_init deferral / tcp_subr.c (defers AES-128 random key generation until crypto provider is available)
- skywalk_init deferral / bsd_init.c (defers packet networking arena allocations)
- dtrace_fbt deferral / fbt.c (defers kernel-wide function boundary tracing instrumentation)

NEXT_EXACT_ACTION=
begin Phase D7-M1 shell artifact and dependency audit
```
