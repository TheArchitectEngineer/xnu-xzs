#!/usr/bin/env python3
"""
Host UART deterministic injector for XNU-XZS D7-M4 external acceptance.
Injects 41 42 43 0a ('ABC\\n') across the kernel RX acceptance window.
"""

import glob
import os
import sys
import termios
import time

TARGET_PAYLOAD = b"ABC\n"  # 41 42 43 0a


def find_serial_ports():
    ports = []
    for pattern in ["/dev/cu.usb*", "/dev/cu.wch*", "/dev/cu.SLAB*", "/dev/cu.FTDI*", "/dev/cu.debug-console"]:
        ports.extend(glob.glob(pattern))
    # Deduplicate while preserving order
    seen = set()
    result = []
    for p in ports:
        if p not in seen:
            seen.add(p)
            result.append(p)
    return result


def open_and_configure(port):
    try:
        fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        attrs = termios.tcgetattr(fd)
        attrs[4] = termios.B115200  # ispeed
        attrs[5] = termios.B115200  # ospeed
        attrs[0] = 0  # iflag (raw)
        attrs[1] = 0  # oflag (raw)
        attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL  # cflag: 8N1, no flow
        attrs[3] = 0  # lflag (raw)
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
        return fd
    except Exception as e:
        print(f"[HOST-INJECTOR] Warning: Failed to open {port}: {e}", file=sys.stderr)
        return None


def main():
    duration = float(sys.argv[1]) if len(sys.argv) > 1 else 20.0
    interval = float(sys.argv[2]) if len(sys.argv) > 2 else 0.5

    ports = find_serial_ports()
    if not ports:
        print("[HOST-INJECTOR] Error: No candidate serial ports found!", file=sys.stderr)
        sys.exit(1)

    print(f"[HOST-INJECTOR] Found candidate ports: {ports}")
    open_fds = []
    for p in ports:
        fd = open_and_configure(p)
        if fd is not None:
            open_fds.append((p, fd))
            print(f"[HOST-INJECTOR] Configured {p} at 115200 8N1")

    if not open_fds:
        print("[HOST-INJECTOR] Error: Could not configure any serial port!", file=sys.stderr)
        sys.exit(1)

    start_time = time.time()
    count = 0
    print(f"[HOST-INJECTOR] Starting transmission of {TARGET_PAYLOAD.hex()} ('ABC\\n') for {duration}s...")
    while (time.time() - start_time) < duration:
        for p, fd in open_fds:
            try:
                os.write(fd, TARGET_PAYLOAD)
                try:
                    termios.tcdrain(fd)
                except Exception:
                    pass
            except Exception as e:
                print(f"[HOST-INJECTOR] Error writing to {p}: {e}", file=sys.stderr)
        count += 1
        time.sleep(interval)

    for p, fd in open_fds:
        try:
            os.close(fd)
        except Exception:
            pass

    print(f"[HOST-INJECTOR] Complete: transmitted {count} bursts of ABC\\n ({TARGET_PAYLOAD.hex()})")


if __name__ == "__main__":
    main()
