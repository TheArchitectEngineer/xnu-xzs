#!/bin/bash
echo "Waiting for Sony Xperia XZs to enter Fastboot mode (LED BLUE)..."
while true; do
    DEV=$(fastboot devices 2>/dev/null | head -n 1 | awk '{print $1}')
    if [ -n "$DEV" ]; then
        echo "DEVICE_DETECTED_IN_FASTBOOT: $DEV"
        exit 0
    fi
    sleep 1
done
