# Xperia XZs Hardware Deployment & Forensic Testing Guide

This guide describes how to deploy, test, and extract forensic telemetry from a physical Sony Xperia XZs running Apple XNU.

---

## 1. Hardware Requirements & Setup

* **Device**: Sony Xperia XZs (G8231 / Tone Keyaki).
* **Bootloader**: Unlocked Sony S1 bootloader.
* **Connection**: USB-C data cable connected directly to host.
* **Host Tools**: Android Platform Tools (`fastboot`, `adb`).

---

## 2. Cold-Reset Procedure (Crucial for Determinism)

Due to [Known Issue #1 (Warm Reboot Nondeterminism)](XZS_KNOWN_ISSUES.md), repeated warm boots can stall unpredictably at early checkpoints. A complete cold reset guarantees 100% deterministic baseline reproduction.

### Step-by-Step Cold Reset:
1. **Force Power-Off**: Hold `Power + Volume Up` simultaneously.
   * Hold past the single vibration.
   * Continue holding until the phone **vibrates 3 consecutive times**.
   * Release buttons immediately. The device is now completely powered down.
2. **Enter Fastboot Mode**:
   * Press and hold `Volume Down`.
   * Insert the USB-C cable connected to the host machine.
   * The notification LED will turn solid **Blue**.
3. **Verify Host Connection**:
   ```bash
   fastboot devices
   ```

---

## 3. Automated Single-Command Test Pipeline

The repository provides an automated test runner:

```bash
./scripts/run-and-extract.sh
```

### What this script automates:
1. Waits for the phone in Fastboot mode.
2. Boots the packaged XNU image: `fastboot boot artifacts/builds/xzs-xnu-boot.img`.
3. Monitors the device lifecycle:
   - XNU boots natively through Mach SMP, BSD, IOKit, and VFS.
   - Upon reaching `[D51-TERMINAL]`, the kernel automatically writes `0x77665500` to IMEM SRAM (`0x066bf65c`) and triggers an APCS watchdog bite (`xzs_spin_halt()`).
   - The device warm-reboots back to Fastboot within **+6 seconds**.
4. The script detects Fastboot re-entry, boots recovery RAM (`twrp-kagura.img`), and mounts Linux `pstore`.
5. Extracts `console-ramoops` (`0xa7fbe000`) and `dmesg-ramoops-0` (`0xa7f00000`) to host logs.

---

## 4. Manual Verification & Recovery

If running steps manually:

### 1. Boot XNU Kernel
```bash
fastboot boot artifacts/builds/xzs-xnu-boot.img
```

### 2. Observe Fastboot Return
Wait ~6 to 10 seconds. The screen will blink and the blue LED will reappear, confirming that XNU executed to the terminal condition and reset cleanly to Fastboot.

### 3. Extract Persistent Logs via Recovery
```bash
# Boot TWRP into RAM (does not overwrite internal flash)
fastboot boot artifacts/builds/twrp-kagura.img

# Wait for ADB recovery interface
adb wait-for-recovery

# Extract persistent RAM pstore buffers
adb shell "cat /sys/fs/pstore/console-ramoops" > artifacts/logs/console-ramoops.log
adb shell "cat /sys/fs/pstore/dmesg-ramoops-0" > artifacts/logs/dmesg-ramoops.log

# Return phone to Fastboot
adb reboot bootloader
```

---

## 5. Expected Phase D1 Terminal Log Output

A successful Phase D1 run will terminate with the following exact tail in `console-ramoops.log`:

```text
[XZS-BOOT] [D50] ROOT DEVICE SELECTION ENTER
[XZS-BOOT] [D50a] IOFindBSDRoot ENTER
[XZS-BOOT] [D50-IOKIT] IOFindBSDRoot ENTER
[XZS-BOOT] [D50-I0] alloc matching ENTER
[XZS-BOOT] [D50-I0a] alloc matching RETURN
[XZS-BOOT] [D50-I1] fromPath /chosen ENTER
[XZS-BOOT] [D50-I1a] fromPath /chosen RETURN
[XZS-BOOT] [D50-I2] fromPath /chosen/memory-map ENTER
[XZS-BOOT] [D50-I2a] fromPath /chosen/memory-map RETURN
[XZS-BOOT] [D50-I3] serviceMatching(IOMedia) ENTER
[XZS-BOOT] [D50-I3a] serviceMatching(IOMedia) RETURN
[XZS-BOOT] [D50-I4] waitQuiet SKIPPED (not set in gIOKitDebug)
[XZS-BOOT] [D50-I5] serialize matching ENTER
[XZS-BOOT] [XZS-WORKAROUND] IOFindBSDRoot matching serialization/logging DEFERRED
[XZS-BOOT] [D50-I5a] serialize matching RETURN / DEFERRED
[XZS-BOOT] [D50-I6] startDeferredMatches SKIPPED
[XZS-BOOT] [D50-I7] canonical root-service wait ENTER
[XZS-BOOT] [XZS-SELFTEST] bounded root-device wait (timeout=1.0s)...
[XZS-BOOT] [D50-I8] no matching physical root service
[XZS-BOOT] [XZS-SELFTEST] root-device wait timed out: no matching physical IOMedia
IOFindBSDRoot: no root device matched after timeout, failing gracefully
[XZS-BOOT] [D50-IOKIT] IOFindBSDRoot: returning kIOReturnNotFound (0xe00002bc)
[XZS-BOOT] [D50b] IOFindBSDRoot RETURN err=0x0xe00002f0
[XZS-BOOT] [D50c] synthetic rootdev selected (XZS-WORKAROUND / SELFTEST)
setconf: IOFindBSDRoot returned an error (-536870160); setting rootdevice to 'sd0a'.
[XZS-BOOT] [D50d] rootdev major=0x0x6 minor=0x0x0 rootdevice=sd0a
[XZS-BOOT] [D51] vfs_mountroot ENTER
[XZS-BOOT] [D51b] bdevvp(rootdev, ...) error=0x0x13
vfs_mountroot: can't setup bdevvp
[XZS-BOOT] [D51-TERMINAL] cannot mount root, errno = 0x0x13 (expected: physical storage / UFS not implemented)
[XZS-BOOT] PHASE D1 TERMINAL CONDITION REACHED — WARM REBOOTING TO FASTBOOT
```

If this terminal output is present, the Phase D1 acceptance gate is 100% satisfied.
