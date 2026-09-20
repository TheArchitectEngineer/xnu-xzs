# XNU-XZS Current Session Handoff

```text
CURRENT_MAIN_COMMIT=da0640433eeb53d1f40bfc9523a33b0acfc288fd

LAST_SEALED_MILESTONE=D6-M6

D6-M6_HARDWARE_TESTED_COMMIT=e64ce18b506bdafbd78c0c0d428a19d7dac85124
D6-M6_SEAL_COMMIT=9f871c43f9325fbc8ebfd321be50a449b32c8964
D6-M6_MAIN_MERGE_COMMIT=da0640433eeb53d1f40bfc9523a33b0acfc288fd


NEXT_MILESTONE=D6-M7

UARTDM_TX=working
UARTDM_RX=not implemented
PHYSICAL_CONSOLE_RX_AVAILABLE=no

CURRENT_KNOWN_PLATFORM_WORKAROUNDS=
- devfs_getattr pointer-hardening workaround / 3e417bb (bypasses vm_kernel_addrhash SHA-256 hang; returns fsid 0x64657666)
- memorystatus static jetsam buffer / kern_memorystatus.c (pre-allocated static snapshot buffer)
- thread_call zone priming / thread_call.c (pre-allocates 105 elements to avoid early zone lock contention)
- non-Apple silicon CTRR compatibility / machine_routines.c (marks unsafe_kernel_text false without Apple DT CTRR property)
- tcp_tfo_init deferral / tcp_subr.c (defers AES-128 random key generation until crypto provider is available)
- skywalk_init deferral / bsd_init.c (defers packet networking arena allocations)
- dtrace_fbt deferral / fbt.c (defers kernel-wide function boundary tracing instrumentation)

NEXT_EXACT_ACTION=
perform D6-M7 final D6 regression/seal
```
