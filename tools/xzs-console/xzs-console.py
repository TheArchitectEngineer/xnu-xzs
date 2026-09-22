#!/usr/bin/env python3
"""
XZS USB-C Console Host Tool (Phase D7-T1 / T1-Z)
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
import signal

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
DEFAULT_BULK_OUT_PAYLOAD = b"XZS-BULK-OUT-TEST\n"
DEFAULT_LOOPBACK_PAYLOAD = bytes.fromhex("585a532d5553422d4c4f4f500a")  # "XZS-USB-LOOP\n"


def find_xzs_device(vid=TARGET_VID, pid=TARGET_PID):
    if not HAVE_PYUSB:
        print("[XZS-CONSOLE] ERROR: pyusb library not found. Install via 'pip3 install pyusb'.", file=sys.stderr)
        return None
    return usb.core.find(idVendor=vid, idProduct=pid)


def test_enumeration(timeout_sec=5, vid=TARGET_VID, pid=TARGET_PID):
    """
    Mode 4.1: Enumeration Test
    Discover VID:PID, open device, read descriptors, print active config and endpoint map.
    """
    start = time.time()
    dev = None
    while time.time() - start < timeout_sec:
        dev = find_xzs_device(vid, pid)
        if dev is not None:
            break
        time.sleep(0.2)

    if dev is None:
        print("USB_DEVICE_FOUND=no")
        print(f"VID=0x{vid:04x}")
        print(f"PID=0x{pid:04x}")
        print("BUS=")
        print("ADDRESS=")
        print("")
        print("CONFIGURATION=")
        print("INTERFACE_CLASS=")
        print("")
        print(f"BULK_OUT_EP=0x{EP_BULK_OUT:02x}")
        print(f"BULK_IN_EP=0x{EP_BULK_IN:02x}")
        print("")
        print("ENUM_TEST=FAIL")
        return None

    bus = getattr(dev, "bus", "unknown")
    address = getattr(dev, "address", "unknown")

    # Read configuration & interface class
    cfg_val = "unknown"
    iface_class = "0xff"
    try:
        cfg = dev.get_active_configuration()
        cfg_val = str(cfg.bConfigurationValue)
        if cfg.bNumInterfaces > 0:
            iface = cfg[(0, 0)]
            iface_class = f"0x{iface.bInterfaceClass:02x}"
    except Exception:
        pass

    try:
        mfg = usb.util.get_string(dev, dev.iManufacturer) or "unknown"
        prod = usb.util.get_string(dev, dev.iProduct) or "unknown"
        serial = usb.util.get_string(dev, dev.iSerialNumber) or "unknown"
    except Exception:
        mfg = prod = serial = "<descriptor read failed>"

    print("USB_DEVICE_FOUND=yes")
    print(f"VID=0x{dev.idVendor:04x}")
    print(f"PID=0x{dev.idProduct:04x}")
    print(f"BUS={bus}")
    print(f"ADDRESS={address}")
    print("")
    print(f"CONFIGURATION={cfg_val}")
    print(f"INTERFACE_CLASS={iface_class}")
    print("")
    print(f"BULK_OUT_EP=0x{EP_BULK_OUT:02x}")
    print(f"BULK_IN_EP=0x{EP_BULK_IN:02x}")
    print("")
    print(f"DEVICE_MANUFACTURER={mfg}")
    print(f"DEVICE_PRODUCT={prod}")
    print(f"DEVICE_SERIAL={serial}")
    print("ENUM_TEST=PASS")
    return dev


def test_bulk_out(dev, payload=None, timeout_ms=2000):
    """
    Mode 5: Bulk OUT Test
    Open device, claim interface, send one bounded transfer, report byte count.
    """
    if payload is None:
        data = DEFAULT_BULK_OUT_PAYLOAD
    elif isinstance(payload, str):
        # Allow hex string if valid, otherwise ascii encode
        try:
            data = bytes.fromhex(payload.replace(" ", ""))
        except ValueError:
            data = payload.encode("utf-8")
    else:
        data = bytes(payload)

    print(f"BULK_OUT_BYTES_REQUESTED={len(data)}")

    if dev is None:
        print("BULK_OUT_BYTES_WRITTEN=0")
        print("BULK_OUT_STATUS=FAIL")
        return False

    try:
        written = dev.write(EP_BULK_OUT, data, timeout=timeout_ms)
        print(f"BULK_OUT_BYTES_WRITTEN={written}")
        if written == len(data):
            print("BULK_OUT_STATUS=PASS")
            return True
        else:
            print("BULK_OUT_STATUS=SHORT_WRITE")
            return False
    except usb.core.USBTimeoutError:
        print("BULK_OUT_BYTES_WRITTEN=0")
        print("BULK_OUT_STATUS=TIMEOUT")
        return False
    except usb.core.USBError as e:
        print("BULK_OUT_BYTES_WRITTEN=0")
        print(f"BULK_OUT_STATUS=FAIL ({e})")
        return False


def test_bulk_in(dev, timeout_ms=3000):
    """
    Mode 6: Bulk IN Test
    Bounded read with timeout.
    """
    if dev is None:
        print("BULK_IN_BYTES_RECEIVED=0")
        print("BULK_IN_DATA_HEX=")
        print("BULK_IN_DATA_ASCII=")
        print("BULK_IN_STATUS=FAIL")
        return None

    try:
        data = dev.read(EP_BULK_IN, 512, timeout=timeout_ms)
        raw_bytes = bytes(data)
        hex_str = raw_bytes.hex(" ")
        ascii_str = "".join(chr(b) if 32 <= b <= 126 or b in (10, 13) else f"\\x{b:02x}" for b in raw_bytes)
        print(f"BULK_IN_BYTES_RECEIVED={len(raw_bytes)}")
        print(f"BULK_IN_DATA_HEX={hex_str}")
        print(f"BULK_IN_DATA_ASCII={ascii_str.strip()}")
        print("BULK_IN_STATUS=PASS")
        return raw_bytes
    except usb.core.USBTimeoutError:
        print("BULK_IN_BYTES_RECEIVED=0")
        print("BULK_IN_DATA_HEX=")
        print("BULK_IN_DATA_ASCII=")
        print("BULK_IN_STATUS=TIMEOUT")
        return None
    except usb.core.USBError as e:
        print("BULK_IN_BYTES_RECEIVED=0")
        print("BULK_IN_DATA_HEX=")
        print("BULK_IN_DATA_ASCII=")
        print(f"BULK_IN_STATUS=FAIL ({e})")
        return None


def test_loopback(dev, timeout_ms=3000):
    """
    Mode 7: Loopback Mode
    Mac Bulk OUT -> XNU -> loopback -> Bulk IN -> Mac
    """
    payload = DEFAULT_LOOPBACK_PAYLOAD
    print(f"[XZS-CONSOLE] Starting Bulk Loopback Test (payload: {payload.hex(' ')})")

    if dev is None:
        print("BULK_LOOPBACK_STATUS=DEVICE_NOT_FOUND")
        print("BULK_LOOPBACK_HARDWARE=NOT_TESTED")
        return False

    out_ok = test_bulk_out(dev, payload=payload, timeout_ms=timeout_ms)
    if not out_ok:
        print("BULK_LOOPBACK_STATUS=OUT_FAILED")
        print("BULK_LOOPBACK_HARDWARE=NOT_TESTED")
        return False

    in_data = test_bulk_in(dev, timeout_ms=timeout_ms)
    if in_data is None:
        print("BULK_LOOPBACK_STATUS=IN_TIMEOUT")
        print("BULK_LOOPBACK_HARDWARE=NOT_TESTED")
        return False

    if in_data == payload:
        print("BULK_LOOPBACK_MATCH=yes")
        print("BULK_LOOPBACK_STATUS=PASS")
        print("BULK_LOOPBACK_HARDWARE=PASS")
        return True
    else:
        print(f"BULK_LOOPBACK_MATCH=no (rx={in_data.hex(' ')})")
        print("BULK_LOOPBACK_STATUS=MISMATCH")
        print("BULK_LOOPBACK_HARDWARE=NOT_TESTED")
        return False


def interactive_console(dev):
    """
    Mode 8: Interactive Console Mode
    Transparent bidirectional byte stream between stdin/stdout and USB Bulk endpoints.
    """
    print("[XZS-CONSOLE] Entering live interactive console (Press Ctrl-] or Ctrl-C to exit)...")
    stop_event = threading.Event()

    def reader_thread():
        while not stop_event.is_set():
            try:
                data = dev.read(EP_BULK_IN, 512, timeout=100)
                if data:
                    sys.stdout.buffer.write(bytes(data))
                    sys.stdout.buffer.flush()
            except usb.core.USBTimeoutError:
                continue
            except usb.core.USBError as e:
                if not stop_event.is_set():
                    time.sleep(0.05)
                continue
            except Exception:
                break

    t = threading.Thread(target=reader_thread, daemon=True)
    t.start()

    old_settings = None
    if sys.stdin.isatty():
        try:
            old_settings = termios.tcgetattr(sys.stdin)
            tty.setraw(sys.stdin.fileno())
        except Exception:
            old_settings = None

    def cleanup(*args):
        stop_event.set()
        if old_settings is not None:
            try:
                termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old_settings)
            except Exception:
                pass

    signal.signal(signal.SIGINT, cleanup)
    signal.signal(signal.SIGTERM, cleanup)

    try:
        while not stop_event.is_set():
            r, _, _ = select.select([sys.stdin], [], [], 0.05)
            if r:
                ch = sys.stdin.buffer.read(1)
                if not ch:
                    break
                if ch == b'\x1d':  # Ctrl-]
                    break
                try:
                    dev.write(EP_BULK_OUT, ch, timeout=1000)
                except usb.core.USBError as e:
                    pass
    except Exception:
        pass
    finally:
        cleanup()
        t.join(timeout=0.5)
        print("\n[XZS-CONSOLE] Live session terminated cleanly.")


def main():
    parser = argparse.ArgumentParser(description="XZS USB-C Console Host Tool (Phase D7-T1 / T1-Z)")
    parser.add_argument("--test-enum", action="store_true", help="Verify device enumeration and print descriptor map")
    parser.add_argument("--test-bulk-out", nargs="?", const="DEFAULT", metavar="DATA", help="Send payload to Bulk OUT (default: XZS-BULK-OUT-TEST\\n)")
    parser.add_argument("--test-bulk-in", action="store_true", help="Read payload from Bulk IN (bounded timeout)")
    parser.add_argument("--test-loopback", action="store_true", help="Perform Bulk OUT -> Bulk IN loopback test")
    parser.add_argument("--interactive", action="store_true", help="Launch live interactive console")
    parser.add_argument("--timeout", type=int, default=5, help="Device search timeout in seconds (default: 5)")
    parser.add_argument("--vid", type=lambda x: int(x, 0), default=TARGET_VID, help="Target USB VID (default: 0x1209)")
    parser.add_argument("--pid", type=lambda x: int(x, 0), default=TARGET_PID, help="Target USB PID (default: 0x000A)")

    args = parser.parse_args()

    # If --test-enum requested, execute enumeration test directly
    if args.test_enum:
        dev = test_enumeration(timeout_sec=args.timeout, vid=args.vid, pid=args.pid)
        sys.exit(0 if dev is not None else 1)

    # For other modes, find and open the device first
    dev = find_xzs_device(vid=args.vid, pid=args.pid)
    if dev is None and args.timeout > 0:
        start = time.time()
        while time.time() - start < args.timeout:
            dev = find_xzs_device(vid=args.vid, pid=args.pid)
            if dev is not None:
                break
            time.sleep(0.2)

    if dev is not None:
        try:
            if dev.is_kernel_driver_active(0):
                dev.detach_kernel_driver(0)
        except Exception:
            pass
        try:
            dev.set_configuration()
        except Exception:
            pass

    if args.test_bulk_out is not None:
        payload = None if args.test_bulk_out == "DEFAULT" else args.test_bulk_out
        ok = test_bulk_out(dev, payload=payload)
        sys.exit(0 if ok else 1)

    if args.test_bulk_in:
        res = test_bulk_in(dev)
        sys.exit(0 if res is not None else 1)

    if args.test_loopback:
        ok = test_loopback(dev)
        sys.exit(0 if ok else 1)

    if args.interactive or (not args.test_enum and args.test_bulk_out is None and not args.test_bulk_in and not args.test_loopback):
        if dev is None:
            print(f"[XZS-CONSOLE] ERROR: USB device {args.vid:04x}:{args.pid:04x} not found within {args.timeout}s.", file=sys.stderr)
            sys.exit(1)
        interactive_console(dev)


if __name__ == "__main__":
    main()
