# D7-T1 Z1–Z4 hardware provenance

The first bulk-endpoint candidate was tested on the Xperia XZs before it had a commit of its own.

```text
SOURCE_BASE=4953f60312919c20f1323097da8ab732e3c1bb00
TESTED_TREE=that commit plus the uncommitted Z1–Z4 diff
KERNEL_SHA256=bd4486074ababdc3b1a1d4ed54a23b32d7f7df636f94a8aeb89385694931cdac
BOOT_SHA256=f95168fff458fc7c6605404bd229b329eb65ded465d454cbd2b2b5adbd68a8a9
```

That image is not reproduced by rebuilding a later commit. The tested printer emitted a doubled `0x` prefix (`0x0x...`). The commit that records the Z1–Z4 behavior keeps that prefix. The next commit removes only the extra prefix and was not in the tested binary.

Hardware result of that image: Z1 EP configuration, Z2 bulk OUT (18 bytes), Z3 bulk IN (17 bytes), and Z4 loopback (13 bytes, host exact match) passed. TTY was not enabled.
