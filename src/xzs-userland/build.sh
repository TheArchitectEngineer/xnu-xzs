#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"

clang -arch arm64 -target arm64-apple-ios16.0 -march=armv8-a -O2 -Wall -Wextra \
  -dynamiclib -nostdlib \
  -Wl,-install_name,/usr/lib/libxzs-inject.dylib \
  -Lstubs -lSystem.B -lobjc.A -lCoreFoundation \
  -o libxzs-inject.dylib inject.c

codesign -f -s - libxzs-inject.dylib

python3 - <<'PY'
with open("libxzs-inject.dylib", "rb") as f:
    data = f.read()

out_path = "../xnu/bsd/vfs/xzs_inject_dylib.h"
with open(out_path, "w") as out:
    out.write("/* Auto-generated from src/xzs-userland/inject.c — DO NOT EDIT */\n")
    out.write("#ifndef _XZS_INJECT_DYLIB_H_\n#define _XZS_INJECT_DYLIB_H_\n\n")
    out.write(f"static const uint8_t k_xzs_inject_dylib[{len(data)}] = {{\n")
    for i in range(0, len(data), 16):
        chunk = ", ".join(f"0x{b:02x}" for b in data[i:i+16])
        out.write(f"\t{chunk},\n")
    out.write("};\n\n#endif /* _XZS_INJECT_DYLIB_H_ */\n")
print(f"Generated {out_path} ({len(data)} bytes)")
PY
