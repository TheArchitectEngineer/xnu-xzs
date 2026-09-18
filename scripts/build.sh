#!/bin/bash
set -euo pipefail

echo "=== Building xzs-bootshim ==="
make -C src/xzs-bootshim
echo "=== Bootshim build successful ==="
