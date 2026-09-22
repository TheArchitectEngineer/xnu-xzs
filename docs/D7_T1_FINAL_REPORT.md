# D7-T1 final evidence

Hardware-tested source is `70983854455c39dc0ef4a9a615ea35aa7d69b7c2` on `xzs-d7t1-tty-bridge`. That commit was pushed before the boot. The tree was clean. No partition was flashed.

```text
KERNEL_SHA256=df831580996f060c38a6ac47cabd68ccc3eb71c85b1c73be8752feac54aafab5
BOOT_SHA256=077b6ae185e4b4f480463cc08fac9010fce108c0fc4ae2f2bb5ed034c15d8188
PAC_AUDIT=PASS
HOST_LOG_SHA256=0e88ab2696d3875715ff541e89c4ea30a2d993caf0a49b07c619bcbb780a485e
PSTORE_SHA256=2a972f3759ab82c57f1c4f1d6b075312dc130411840abb6bfd0bc09e450e7370
```

Paths: `artifacts/hw/d7t1-tty-7098385/host.txt` and `artifacts/hw/d7t1-tty-7098385/pstore/console-ramoops`.

The same boot's console shows sector 1 CRC `0x811ff249` matching this image, Z1 endpoint configuration through `D740/Z30`, then `D6_REGRESSION=PASS`, `D7_M2_REGRESSION=PASS`, `D7_M3_REGRESSION=PASS`, and `D7_M4_REGRESSION=PASS`. After that the console contains the EL0 prompt burst, the echoed line `ABC`, and another `xzs# `.

The host, on this boot, recorded bulk OUT of 18 bytes, bulk IN `XZS-BULK-IN-TEST`, loopback exact match of `XZS-USB-LOOP`, then a later bulk IN of `xzs# ` after it had written `ABC\n`. That second prompt is the shell instruction that runs only after `read` returns 4 bytes equal to `41 42 43 0a`.

The structured end-of-run counter block was not retained. Recovery logging wrapped the 256 KiB console ramoops after the warm reset. Ring drop counts are therefore not in the retained pstore. `D7_T1_SEALED` was not printed by the kernel.

The earlier Z1–Z4 image `f95168fff458fc7c6605404bd229b329eb65ded465d454cbd2b2b5adbd68a8a9` remains a different source identity. See `docs/D7_T1_Z1Z4_PROVENANCE.md`.
