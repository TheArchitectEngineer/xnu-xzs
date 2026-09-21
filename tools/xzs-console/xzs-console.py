#!/usr/bin/env python3
"""
XZS USB-C Console Host Tool (Phase D7-T1)
Bidirectional live console and automated test transport over USB-C Bulk endpoints.
Target VID: 0x1209, PID: 0x000A
Bulk OUT: EP 0x01 (Host -> Xperia)
Bulk IN:  EP 0x81 (Xperia -> Host)
"""

import sys
import os
import time
import argparse
import select
import termios
import tty
import threading

try:
    import usb.core
    import usb.util
    HAVE_PYUSB = True
except ImportError:
    HAVE_PYUSB = False

TARGET_VID = 0x1209
TARGET_PID = 0x000A
EP_BULK_OUT = 0x01
EP_BULK_IN  = 0x81

def find_xzs_device():
    if not HAVE_PYUSB:
        print("[XZS-CONSOLE] ERROR: pyusb library not found. Install via 'pip3 install pyusb'.", file=sys.stderr)
        return None
    dev = usb.core.find(idVendor=TARGET_VID, idProduct=TARGET_PID)
    return dev

def test_enumeration(timeout_sec=10):
    print(f"[XZS-CONSOLE] Scanning for device {TARGET_VID:04x}:{TARGET_PID:04x} (timeout {timeout_sec}s)...")
    start = time.time()
    while time.time() - start < timeout_sec:
        dev = find_xzs_device()
        if dev is not None:
            try:
                mfg = usb.util.get_string(dev, dev.iManufacturer)
                prod = usb.util.get_string(dev, dev.iProduct)
                serial = usb.util.get_string(dev, dev.iSerialNumber)
            except Exception as e:
                mfg = prod = serial = f"<read failed: {e}>"
            print(f"[XZS-CONSOLE] FOUND XZS USB CONSOLE:")
            print(f"  VID:PID       = {dev.idVendor:04x}:{dev.idProduct:04x}")
            print(f"  Manufacturer  = {mfg}")
            print(f"  Product       = {prod}")
            print(f"  Serial        = {serial}")
            print("HOST_USB_DEVICE_VISIBLE=yes")
            print("HOST_USB_VID_PID_MATCH=yes")
            print("USB_DEVICE_ENUMERATED=yes")
            return dev
        time.sleep(0.5)
    print(f"[XZS-CONSOLE] Device not found within {timeout_sec}s.")
    return None

def test_bulk_out(dev, hex_str):
    data = bytes.fromhex(hex_str)
    print(f"[XZS-CONSOLE] Sending {len(data)} bytes to EP 0x{EP_BULK_OUT:02x}: {data.hex(' ')}")
    written = dev.write(EP_BULK_OUT, data, timeout=2000)
    print(f"USB_BULK_OUT_TX_LENGTH={written}")
    print(f"USB_BULK_OUT_TX_HEX={data.hex(' ')}")
    if written == len(data):
        print("USB_BULK_OUT_PASS=yes")
        return True
    return False

def test_bulk_in(dev, timeout_ms=3000):
    print(f"[XZS-CONSOLE] Reading from EP 0x{EP_BULK_IN:02x} (timeout {timeout_ms}ms)...")
    try:
        data = dev.read(EP_BULK_IN, 512, timeout=timeout_ms)
        raw_bytes = bytes(data)
        print(f"USB_BULK_IN_RX_LENGTH={len(raw_bytes)}")
        print(f"HOST_BULK_IN_RX_HEX={raw_bytes.hex(' ')}")
        try:
            ascii_text = raw_bytes.decode('utf-8', errors='replace')
            print(f"HOST_BULK_IN_RX_ASCII={ascii_text.strip()}")
        except Exception:
            pass
        if len(raw_bytes) > 0:
            print("USB_BULK_IN_PASS=yes")
            return raw_bytes
    except usb.core.USBError as e:
        print(f"[XZS-CONSOLE] Read failed or timed out: {e}")
    return None

def interactive_console(dev):
    print("[XZS-CONSOLE] Entering live interactive console (Ctrl-] or Ctrl-C to exit)...")
    stop_event = threading.Event()

    def reader_thread():
        while not stop_event.is_set():
            try:
                data = dev.read(EP_BULK_IN, 512, timeout=200)
                if data:
                    sys.stdout.buffer.write(bytes(data))
                    sys.stdout.buffer.flush()
            except usb.core.USBTimeoutError:
                continue
            except usb.core.USBError as e:
                if not stop_event.is_set():
                    time.sleep(0.1)
                continue

    t = threading.Thread(target=reader_thread, daemon=True)
    t.start()

    old_settings = None
    if sys.stdin.isatty():
        old_settings = termios.tcgetattr(sys.stdin)
        tty.setraw(sys.stdin.fileno())

    try:
        while not stop_event.is_set():
            r, _, _ = select.select([sys.stdin], [], [], 0.1)
            if r:
                ch = sys.stdin.buffer.read(1)
                if not ch:
                    break
                if ch == b'\x1d':  # Ctrl-]
                    break
                dev.write(EP_BULK_OUT, ch, timeout=1000)
    except KeyboardInterrupt:
        pass
    finally:
        stop_event.set()
        t.join(timeout=1.0)
        if old_settings is not None:
            termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old_settings)
        print("\n[XZS-CONSOLE] Live session terminated cleanly.")

def main():
    parser = argparse.ArgumentParser(description="XZS USB-C Console Host Tool")
    parser.add_argument("--test-enum", action="store_true", help="Verify device enumeration")
    parser.add_argument("--test-bulk-out", metavar="HEX", help="Send hex payload to Bulk OUT (e.g. 4142430a)")
    parser.add_argument("--test-bulk-in", action="store_true", help="Read payload from Bulk IN")
    parser.add_argument("--interactive", action="store_true", help="Launch live interactive console")
    parser.add_argument("--timeout", type=int, default=10, help="Device search timeout in seconds")

    args = parser.parse_args()

    dev = test_enumeration(timeout_sec=args.timeout)
    if dev is None:
        sys.exit(1)

    # Detach kernel driver if needed (on Linux)
    try:
        if dev.is_kernel_driver_active(0):
            dev.detach_kernel_driver(0)
    except Exception:
        pass

    try:
        dev.set_configuration()
    except Exception:
        pass

    if args.test_enum:
        sys.exit(0)

    if args.test_bulk_out:
        ok = test_bulk_out(dev, args.test_bulk_out)
        sys.exit(0 if ok else 1)

    if args.test_bulk_in:
        res = test_bulk_in(dev)
        sys.exit(0 if res else 1)

    if args.interactive or (not args.test_enum and not args.test_bulk_out and not args.test_bulk_in):
        interactive_console(dev)

if __name__ == "__main__":
    main()
