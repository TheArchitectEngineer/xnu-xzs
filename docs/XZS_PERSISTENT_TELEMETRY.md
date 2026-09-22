# Persistent telemetry

XNU and TWRP share one reserved DRAM region. A console file in `/sys/fs/pstore` is not evidence unless it contains an `[XZS]` marker.

## Region

| Field | Value | Basis |
|---|---|---|
| `PERSIST_BASE` | `0xa7f00000` | TWRP DTB `pstore_reserve_mem_region_region@a7f00000` |
| `PERSIST_SIZE` | `0x100000` | same node, `no-map` |
| Kernel window | `0x80000000`–`0x85800000` | boot args `memSize` `0x05800000` |
| Overlap with kernel, boot args, ADT, USB, or the empty framebuffer handoff | none | the reserved region starts at `0xa7f00000` |

TWRP's `ramoops` node is `compatible = "pstore-ramoops"` and only has `memory-region`. It does not set `record-size`, `console-size`, or `ecc-size` in that DTB.

The failed LITE pull is hardware observation, not a format spec:

```text
console-ramoops = 262132 bytes
dmesg-ramoops-* = 4084 bytes each
[XZS] present = no
```

`262132` is `256 KiB - 12`. `4084` is `4 KiB - 12`. Linux `persistent_ram` uses a 12-byte header (`sig`, `start`, `size`) and exports the following bytes. Those sizes are what TWRP exposed. They are not a substitute for a marker.

TWRP `/proc/iomem` on the kagura recovery kernel shows the live layout:

```text
0xa7f00000  190 records × 0x1000
0xa7fbe000  console, 0x40000
0xa7ffe000  one 0x1000 record
0xa7fff000  one 0x1000 record
```

XNU's console header address matches that console zone. The record at `0xa7f00000` is `dmesg-ramoops-0`.

Candidate `94c1c84` wrote `MAGIC=XZSP` and the running CPU read both headers back as `sig=0x43474244`, `size=0x7b`. After shutdown, both pstore files were still the full zone (`262132` and `4084` bytes) and neither contained the marker.

Candidate `7a57645` maps that block as Normal write-combine and repeats the same live readback. After another shutdown, `console-ramoops` is again `262132` bytes and `dmesg-ramoops-0` is `4084` bytes. A search of every file under `/sys/fs/pstore` finds no `XZSP`. The two pulls are not byte-identical, so the region is not a frozen image, but the exported bytes are not the XNU marker.

No recovery path is authoritative yet. Do not treat `/sys/fs/pstore` as proof of an XNU run unless the file contains `MAGIC=XZSP` or another `[XZS]` line from the booted commit.

## What XNU writes

`xzs_raw_tx` in `osfmk/arm64/start.s` stores each console byte at:

```text
0x80060000   low-DRAM log, only if magic XZSD is already present
0xa7fbe000   persistent_ram header + payload
0xa7f00000   second copy, same header
```

Header signature is `0x43474244` (`DBGC`), the Linux `PERSISTENT_RAM_SIG`. Each store is followed by `dsb sy` and the existing `dc cvac`. No new cache operation was added.

A diagnostic command plants this text once, then appends the command:

```text
[XZS-PSTORE] MAGIC=XZSP
[XZS-PSTORE] VERSION=1
[XZS-PSTORE] TEST=1122334455667788
[XZS-PSTORE] CHECKPOINT=PERSIST_TEST
```

`mem` and `irq` also emit `[XZS-DIAG] mem` and `[XZS-DIAG] irq`. USB and the persistent buffers are fed from that same text.

## What TWRP can destroy

The ramoops driver runs during recovery boot, before `adb pull`. If the header signature or size is not a valid `persistent_ram` record, Linux replaces the header. The 2026-09-22 pull waited long enough for that driver to run, and the file had no XNU text. That single pull cannot separate "XNU never reached DRAM" from "TWRP rewrote the record". The marker test is the check.

Authoritative recovery until a pull shows the magic:

```text
manual Sony force shutdown
→ fastboot
→ fastboot boot twrp-kagura.img
→ adb pull /sys/fs/pstore/console-ramoops
→ adb pull /sys/fs/pstore/dmesg-ramoops-0
```

Trust the file only when it contains `MAGIC=XZSP` or another `[XZS]` line from the booted commit.

## Display blocks

These stay reference-only. DTS `status = "disabled"` means the Linux node is disabled. It does not prove the power domain is off.

| Block | Class |
|---|---|
| MDSS | REFERENCE_ONLY |
| MDP5 | REFERENCE_ONLY |
| DSI0 / DSI1 | REFERENCE_ONLY |
| DSI PHY | REFERENCE_ONLY |
| display PLL | REFERENCE_ONLY |
| MDSS GDSC | REQUIRES_POWER_DOMAIN |
| GCC display clocks | REFERENCE_ONLY |
| panel regulators | REFERENCE_ONLY |
| display interrupt registers | UNKNOWN |

No display register is `SAFE_TO_READ` yet. D8-M2 writes stay blocked until a persistent marker survives recovery.
