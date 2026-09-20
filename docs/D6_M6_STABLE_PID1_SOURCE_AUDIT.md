# D6-M6 Stable PID1 and Console Source Audit

## Scope

This audit covers the minimum D6-M6 runtime delta from the sealed D6-M5
baseline: native PID1 descriptors, one real console write from EL0, a
deterministic sustained EL0 syscall loop, and the current console-input
boundary. It does not claim that physical console input works.

## PID1 descriptor origin

`bsd_utaskbootstrap()` creates PID1 with
`cloneproc(TASK_NULL, NULL, kernproc, CLONEPROC_INITPROC)`. The `kernproc`
file-descriptor table is initialized empty by `fdt_init()`, and `fdt_fork()`
copies only non-null entries. The D6-M5 hardware result (`write(1) -> EBADF`)
independently confirmed that PID1 had no fd 1.

```text
PID1_FILEDESC_STRUCTURE=struct proc.p_fd
FD0_INITIAL_STATE=absent
FD1_INITIAL_STATE=absent
FD2_INITIAL_STATE=absent
NORMAL_INIT_STDIO_SETUP_PATH=userspace_init_opens_console
```

No kernel-side `/dev/console` open is present in this tree's normal
`load_init_program()` path. D6-M6 therefore performs the smallest explicit
bootstrap equivalent before releasing the suspended synthetic PID1.

## Native fd 0/1/2 construction

`xzs_d6m6_setup_console_stdio()` uses the native `open1()` interface three
times with `O_RDWR | O_NOCTTY`. Its `struct vfs_context` carries:

```text
vc_thread=PID1 main thread
vc_ucred=referenced PID1 credential
```

`open1()` calls `vfs_context_proc()`, which derives PID1 from that thread and
therefore allocates in PID1's `p_fd`, not in the CPU0 bootstrap process. With
the audited-empty table, the three opens must return fd 0, 1, and 2 in order.

Each result is independently checked with `fp_lookup()`:

- `FILEGLOB_DTYPE == DTYPE_VNODE`
- vnode type is `VCHR`
- `vnode_specrdev()` is major 0, minor 0
- `FREAD | FWRITE` are both present

The temporary `fp_lookup()` I/O reference is released with `fp_drop()`. The
descriptor-table references remain owned by PID1 and retain the normal close
and process-exit cleanup semantics. The VFS credential reference is released
after each `open1()` call.

## Console output path

`devfs` creates `/dev/console` as character device 0:0. On ARM64, cdev major 0
selects `cnopen/cnread/cnwrite`. `cnwrite()` forwards through `kmwrite()`, the
tty line discipline, `kmstart()/kmoutput()`, and finally `console_write()` to
the registered Qualcomm UARTDM transmit routine.

```text
DEV_CONSOLE_VNODE_PATH=/dev/console
CONSOLE_OPEN_CALL_PATH=open1 -> vn_open_auth -> spec_open -> cnopen -> kmopen
CONSOLE_WRITE_CALL_PATH=write -> fo_write -> vn_write -> spec_write -> cnwrite -> kmwrite -> tty -> console_write -> UARTDM TX
```

The static PID1 image preserves the certified first instruction sequence and
performs:

```text
write(1, 0x100000320, 26)
```

The message bytes are `[XZS-INIT] launchd entered\n`. A successful return of
26 with carry clear, together with the unique bytes in the captured physical
console log, is the D6-M6 output proof. Kernel-only printing is not accepted as
that proof.

## Stable EL0 runtime

After `write()` returns, PID1 repeatedly performs native BSD syscall 20
(`getpid`). This syscall is read-only, non-blocking, has no dynamic-loader or
filesystem dependency, and must return PID 1 with carry clear.

The acceptance reporter waits for at least 256 fully prepared successful
returns. The counter is advanced only after the real handler has completed and
`arm_prepare_syscall_return()` has installed `x0 == 1` with carry clear. This
distinguishes a continuously executing PID1 from a one-shot exception probe.

## Console input boundary

The kernel read side is present:

```text
read(0, ...)
-> fileproc/vnode
-> specfs
-> cnread
-> kmread
-> tty line discipline
```

The tty layer supports blocking reads, and `cons_cinput()` can deliver an input
character to the line discipline. The current MSM UART implementation does
not supply those characters: `msm_uart_receive_ready()` and
`msm_uart_receive_data()` in `pexpert/arm/pe_serial.c` both return zero
unconditionally.

```text
CONSOLE_INPUT_DEVICE=/dev/console
BLOCKING_READ_SUPPORTED=yes
CURRENT_INPUT_TRANSPORT=msm_uartdm_tx_only
PHYSICAL_CONSOLE_RX_AVAILABLE=no
PID1_CONSOLE_INPUT_VERIFIED=no
```

This is a source-audited D7 architecture blocker, not a D6-M6 output failure.
A real interactive shell will require the smallest correct UARTDM RX path (or
another already-existing bidirectional console transport) before stdin can be
hardware verified.

## Reproducible PID1 image

`src/xzs-userland/launchd.S` and its Makefile reproduce the rootfs executable.
The D6-M6 image preserves:

```text
Mach-O file size=16472
__PAGEZERO=0x0..0x100000000
__TEXT vmaddr=0x100000000, size=0x4000, protection=RX
entry PC=0x1000002f0
first SVC return PC=0x100000304
message address=0x100000320
```

Payload evolution intentionally changes the launchd CRC32 to `0xe212a8a2`,
the XZSFS whole-image CRC32 to `0x35c2b076`, and the XZSFS SHA-256 to
`1d737246aee8c48623d0e800c2fcdaa8400c0bc38344f2f7305e501856928962`.
The filesystem format, object identities, sizes, permissions, and `/bin/sh`
payload remain unchanged.

## Evidence classification before hardware

- SOURCE-AUDITED FACT: descriptor inheritance, `open1()` ownership, console
  device call paths, tty read path, and UARTDM RX stubs.
- BUILD-VERIFIED FACT: Mach-O layout, XZSFS host tests, ARM64 DEVELOPMENT
  build, zero PAC instructions, and boot-image packaging.
- HARDWARE VERIFIED: pending the immutable pushed implementation commit.
- XZS WORKAROUND: explicit kernel bootstrap of PID1's console descriptors and
  cross-CPU acceptance telemetry.
- PLATFORM FIX: none in D6-M6.
- INFERENCE/HYPOTHESIS: none promoted to hardware fact.
