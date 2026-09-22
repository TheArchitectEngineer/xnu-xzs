# D7-T1 final evidence

D7-T1 is complete and sealed. The seal is the tag, not a later documentation commit.

```text
TESTED_COMMIT=70983854455c39dc0ef4a9a615ea35aa7d69b7c2
TAG=xzs-d7t1-complete

KERNEL_SHA256=df831580996f060c38a6ac47cabd68ccc3eb71c85b1c73be8752feac54aafab5
BOOT_SHA256=077b6ae185e4b4f480463cc08fac9010fce108c0fc4ae2f2bb5ed034c15d8188
PSTORE_SHA256=2a972f3759ab82c57f1c4f1d6b075312dc130411840abb6bfd0bc09e450e7370
HOST_LOG_SHA256=0e88ab2696d3875715ff541e89c4ea30a2d993caf0a49b07c619bcbb780a485e

ENUMERATION=PASS
Z1=PASS
Z2=PASS
Z3=PASS
Z4=PASS
Z5=PASS
Z6=PASS

HOST_TO_EL0_PATH=PASS
EL0_TO_HOST_PATH=PASS

D6_REGRESSION=PASS
D7_M2_REGRESSION=PASS
D7_M3_REGRESSION=PASS
D7_M4_REGRESSION=PASS

RX_RING_DROPS=NOT_RETAINED
TX_RING_DROPS=NOT_RETAINED
```

Hardware-tested source is `70983854455c39dc0ef4a9a615ea35aa7d69b7c2` on `xzs-d7t1-tty-bridge`. That commit was pushed before the boot. The tree was clean. No partition was flashed. Tag `xzs-d7t1-complete` points at that commit and is not moved by later notes.

```text
KERNEL_SHA256=df831580996f060c38a6ac47cabd68ccc3eb71c85b1c73be8752feac54aafab5
BOOT_SHA256=077b6ae185e4b4f480463cc08fac9010fce108c0fc4ae2f2bb5ed034c15d8188
PAC_AUDIT=PASS
HOST_LOG_SHA256=0e88ab2696d3875715ff541e89c4ea30a2d993caf0a49b07c619bcbb780a485e
PSTORE_SHA256=2a972f3759ab82c57f1c4f1d6b075312dc130411840abb6bfd0bc09e450e7370
```

Evidence files, re-hashed on 2026-09-22 to the values above:

```text
/Users/lechaukha12/.grok/worktrees/desktop-xnu-xzs/xnu-xzs/artifacts/hw/d7t1-tty-7098385/host.txt
/Users/lechaukha12/.grok/worktrees/desktop-xnu-xzs/xnu-xzs/artifacts/hw/d7t1-tty-7098385/pstore/console-ramoops
```

`artifacts/` is gitignored. Those logs stay on disk and are not added to Git.

The same boot's console shows sector 1 CRC `0x811ff249` matching this image, Z1 endpoint configuration through `D740/Z30`, then `D6_REGRESSION=PASS`, `D7_M2_REGRESSION=PASS`, `D7_M3_REGRESSION=PASS`, and `D7_M4_REGRESSION=PASS`. After that the console contains the EL0 prompt burst, the echoed line `ABC`, and another `xzs# `.

The host, on this boot, recorded bulk OUT of 18 bytes, bulk IN `XZS-BULK-IN-TEST`, loopback exact match of `XZS-USB-LOOP`, then a later bulk IN of `xzs# ` after it had written `ABC\n`. That second prompt is the shell instruction that runs only after `read` returns 4 bytes equal to `41 42 43 0a`.

The final `=== D7-T1 T1-Z TARGET TELEMETRY ===` block is not in the retained console. Recovery logging wrapped the 256 KiB ramoops, so `USB_RX_RING_DROPS` and `USB_TX_RING_DROPS` from that block are `NOT_RETAINED`. They are not recorded as zero.

The retained lines `D7_T1_COMPLETE=no` and `D7_T1_SEALED=no` are the intermediate T1-Y snapshot printed from `status.c`. In this console that snapshot appears after `D740/Z00` through `D740/Z30`, so it is not a claim that no Z code had run. `status.c` hardcodes those two lines and then calls `xzs_usb_t1z_service()`. They are not the repository seal. The seal is tag `xzs-d7t1-complete` at `70983854455c39dc0ef4a9a615ea35aa7d69b7c2`, on the host bytes plus the shell success prompt described above.

The sealed host log also printed `LIVE_BIDIRECTIONAL_TRANSPORT=no` and exited 1. That predicate required a prompt from the 50-second poll before `ABC\n`. The post-read `xzs#` was observed after the send. That summary flag is a host-reporting false negative, not a failed transport.

The earlier Z1–Z4 image `f95168fff458fc7c6605404bd229b329eb65ded465d454cbd2b2b5adbd68a8a9` remains a different source identity. See `docs/D7_T1_Z1Z4_PROVENANCE.md`.
