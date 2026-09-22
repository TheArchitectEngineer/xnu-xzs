#!/usr/bin/env python3
"""Host USB configurator for D7-T1 XNU USB enumeration."""

import time
import sys

def main():
    import usb.core
    import usb.util

    print("[HOST-USB] Waiting for XNU USB device (1209:000a)...", flush=True)
    timeout = 30.0
    start = time.time()
    dev = None
    while time.time() - start < timeout:
        try:
            dev = usb.core.find(idVendor=0x1209, idProduct=0x000A)
            if dev is not None:
                break
        except Exception:
            pass
        time.sleep(0.05)

    if dev is None:
        print("[HOST-USB] TIMEOUT: XNU USB device not found within 30s", flush=True)
        sys.exit(1)

    print(f"[HOST-USB] FOUND XNU USB DEVICE at +{time.time() - start:.2f}s!", flush=True)
    try:
        print(f"[HOST-USB] Manufacturer: {dev.manufacturer}", flush=True)
        print(f"[HOST-USB] Product:      {dev.product}", flush=True)
        print(f"[HOST-USB] SerialNumber: {dev.serial_number}", flush=True)
    except Exception as e:
        print(f"[HOST-USB] String descriptor read warning: {e}", flush=True)

    try:
        print("[HOST-USB] Sending SET_CONFIGURATION(1)...", flush=True)
        dev.set_configuration(1)
        print("[HOST-USB] SET_CONFIGURATION(1) SUCCESS!", flush=True)
        cfg = dev.get_active_configuration()
        print(f"[HOST-USB] ACTIVE_CONFIGURATION={cfg.bConfigurationValue}", flush=True)
    except Exception as e:
        print(f"[HOST-USB] set_configuration error: {e}", flush=True)
        sys.exit(1)

if __name__ == "__main__":
    main()
