# D7-T1 T1-Z Preparation: Host Tooling and Kernel Bulk Bridge Architecture

## 1. Concurrency & Milestone Status

This document and associated host-side tooling were prepared in parallel on dedicated branch `xzs-d7t1-t1z-prep` without modifying Codex's active kernel working branch (`xzs-d7t1-candidate2c-event-buffer`) or any active kernel DWC3 implementation files.

```text
T1_Y_STATUS=IN_PROGRESS (Codex: EP0-only control path -> RUN_STOP=1 -> USB Reset -> ConnectDone -> Chapter-9 VID:PID)
T1_Z_STATUS=HOST_TOOLING_READY / KERNEL_AUDITED / HARDWARE_NOT_YET_TESTED
D7_T1_COMPLETE=no
D7_T1_SEALED=no
```

---

## 2. Status Categorization

To maintain strict scientific integrity across parallel workflows:

| Subsystem / Deliverable | Status Category |
|:---|:---|
| Host utility `tools/xzs-console/xzs-console.py` (`./xzs-console`) | `IMPLEMENTED_HOST_SIDE` |
| Multi-mode host tests (`--test-enum`, `--test-bulk-out`, `--test-bulk-in`, `--test-loopback`, `--interactive`) | `IMPLEMENTED_HOST_SIDE` |
| Hardware log acceptance verifier (`scripts/verify_d7t1_z_acceptance.py`) | `IMPLEMENTED_HOST_SIDE` |
| Correlated host/pstore acceptance parser (`scripts/parse_d7t1_z_acceptance.py`) | `IMPLEMENTED_HOST_SIDE` |
| Kernel DWC3 Bulk endpoint numbering & TRB models | `READ_ONLY_KERNEL_AUDIT` |
| TTY ingress (`cons_cinput` via deferred thread context) | `READ_ONLY_KERNEL_AUDIT` |
| Console TX mirror (`xzs_console_write` non-blocking TX ring) | `READ_ONLY_KERNEL_AUDIT` |
| End-to-end USB Bulk transport over physical USB-C | `NOT_YET_HARDWARE_TESTED` |
| Live interactive shell session over USB Bulk | `NOT_YET_HARDWARE_TESTED` |

---

## 3. Architecture

```text
========================================================================================
                                 XZS D7-T1-Z ARCHITECTURE
========================================================================================

   Mac Host (macOS)                                      Sony Xperia XZs (MSM8996)
+-----------------------+                             +--------------------------------+
|     Terminal EL0      |                             |        /bin/sh (PID 1)         |
|   (Interactive Pty)   |                             |    (Darwin EL0 User Space)     |
+-----------+-----------+                             +---------------+----------------+
            |                                                         |
            | stdin/stdout                                            | read(0)/write(1)
            v                                                         v
+-----------------------+                             +--------------------------------+
|      xzs-console      |                             |         Sealed TTY Layer       |
|    (Host Python Tool) |                             |     (bsd/dev/arm/km.c km_tty)  |
+-----------+-----------+                             +-------+----------------+-------+
            |                                                 ^                |
            | pyusb / libusb                                  | cons_cinput()  | xzs_console_write()
            v                                                 | (deferred)     | (non-blocking)
+-----------------------+                             +-------+-------+ +------+-------+
|  Host USB Controller  |                             |  RX SPSC Ring | |  TX SPSC Ring |
|  (macOS USBHost stack)|                             +-------+-------+ +------+-------+
+-----------+-----------+                                     ^                |
            |                                                 | rx_len         | count
            | USB-C D+/D- Bus                                 |                v
            |                                         +-------+----------------+-------+
            | (VID 0x1209, PID 0x000A)                |        DWC3 Core (0x06a00000)  |
            +---------------------------------------->| Bulk OUT EP 0x01 (Phys EP2)    |
            |<----------------------------------------| Bulk IN  EP 0x81 (Phys EP3)    |
                                                      +--------------------------------+
```

---

## 4. Expected Endpoint Mapping

In Synopsys DesignWare USB3 (DWC3), physical endpoint indices map strictly as:
- Physical EP $2n$ = OUT endpoint
- Physical EP $2n + 1$ = IN endpoint

| Logical Endpoint | Transfer Type | Direction | Physical DWC3 EP | Buffer Size | FIFO / Resource |
|:---|:---|:---|:---|:---|:---|
| **EP 0x00** | Control OUT | Host $\to$ Device | **Physical EP0** | 64 bytes (`s_setup_pkt_buf`) | Setup / Control OUT |
| **EP 0x80** | Control IN | Device $\to$ Host | **Physical EP1** | 64 bytes (`s_ep0_in_buf`) | EP0 IN FIFO 0 |
| **EP 0x01** | Vendor Bulk OUT | Host $\to$ Xperia | **Physical EP2** | 512 bytes (`s_bulk_out_buf`) | Bulk OUT FIFO 2 |
| **EP 0x81** | Vendor Bulk IN | Xperia $\to$ Host | **Physical EP3** | 512 bytes (`s_bulk_in_buf`) | Bulk IN FIFO 3 |

### USB Descriptors
- `VID`: `0x1209` (pid.codes open-source test PID)
- `PID`: `0x000A`
- `bDeviceClass`: `0xFF` (Vendor Specific)
- `bNumConfigurations`: 1
- `bConfigurationValue`: 1
- Interface 0:
  - `bInterfaceNumber`: 0
  - `bInterfaceClass`: `0xFF` (Vendor Specific)
  - `bNumEndpoints`: 2
  - Endpoint 1: `0x01` (Bulk OUT, max packet 512 bytes)
  - Endpoint 2: `0x81` (Bulk IN, max packet 512 bytes)

---

## 5. Host Tooling (`xzs-console`)

The host utility is implemented in Python 3 with `pyusb` (backed by `libusb 1.0` on macOS).
Executable entry point: `./xzs-console` or `tools/xzs-console/xzs-console.py`.

### 5.1 Modes of Operation

#### 1. Enumeration Test (`./xzs-console --test-enum`)
- Discovers `0x1209:0x000A` on the host USB bus.
- Reads device, configuration, and interface descriptors.
- Emits structured key-value output:
  ```text
  USB_DEVICE_FOUND=yes/no
  VID=0x1209
  PID=0x000a
  BUS=<bus>
  ADDRESS=<address>
  CONFIGURATION=1
  INTERFACE_CLASS=0xff
  BULK_OUT_EP=0x01
  BULK_IN_EP=0x81
  ENUM_TEST=PASS/FAIL
  ```

#### 2. Bulk OUT Test (`./xzs-console --test-bulk-out [payload]`)
- Default payload: `XZS-BULK-OUT-TEST\n` (18 bytes).
- Submits one bounded transfer with a 2000 ms timeout.
- Emits:
  ```text
  BULK_OUT_BYTES_REQUESTED=18
  BULK_OUT_BYTES_WRITTEN=18
  BULK_OUT_STATUS=PASS
  ```

#### 3. Bulk IN Test (`./xzs-console --test-bulk-in`)
- Performs bounded read (up to 512 bytes) with a 3000 ms timeout.
- Emits:
  ```text
  BULK_IN_BYTES_RECEIVED=<len>
  BULK_IN_DATA_HEX=<hex>
  BULK_IN_DATA_ASCII=<ascii>
  BULK_IN_STATUS=PASS
  ```

#### 4. Loopback Mode (`./xzs-console --test-loopback`)
- Payload: `58 5a 53 2d 55 53 42 2d 4c 4f 4f 50 0a` (`XZS-USB-LOOP\n`).
- Transmits to Bulk OUT, then reads from Bulk IN.
- Validates bit-exact match.
- Emits: `BULK_LOOPBACK_HARDWARE=NOT_TESTED` (until hardware loopback is enabled).

#### 5. Interactive Mode (`./xzs-console`)
- Transparent bidirectional streaming without custom framing.
- Configures terminal in raw mode.
- Reader thread: Polls Bulk IN (100 ms timeout) $\to$ `sys.stdout.buffer`.
- Main thread: `select([sys.stdin])` $\to$ Bulk OUT (1000 ms timeout).
- Exit sequence: `Ctrl-]` or `Ctrl-C` cleanly restores terminal state.

---

## 6. Kernel Read-Only Audit (T1-Z Kernel Implementation Map)

### 6.1 TTY Input Bridge Audit
- **Entry Function**: `cons_cinput(char ch)` ([src/xnu/bsd/dev/arm/km.c:459](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/bsd/dev/arm/km.c#L459)).
- **Call Context**: Thread context only (deferred `thread_call` via `thread_call_enter(s_usb_rx_tty_call)`).
- **Locking Requirements**: Caller must **NOT** hold `tty_lock(tp)`. `cons_cinput` acquires `tty_lock(tp)` internally before calling `linesw[tp->t_line].l_rint(ch, tp)`. It cannot be called from hard interrupt context or while holding spinlocks.
- **Batch Limit**: Bounded by SPSC RX ring capacity (typically 512–1024 bytes) drained sequentially in deferred context.

### 6.2 Console Output Mirror Audit
- **Hook Candidate**: `xzs_console_write(const unsigned char *buf, int len)` ([src/xnu/bsd/dev/arm/km.c:369](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/bsd/dev/arm/km.c#L369)) called from `kmoutput()`.
- **Call Context**: Kernel thread executing `write()` syscall from EL0 (`/bin/sh`), holding `tty_lock(tp)`.
- **Recursion Risk**: High if USB TX functions call `printf` or `kprintf`. Any console hook must write to a non-blocking TX ring buffer only, and never call logging/printing functions within the hook.
- **Blocking Allowed**: `NO`. USB TX hardware must never stall console writes. Bytes are dropped or ring-buffered; DWC3 Bulk IN drains the TX ring asynchronously.

### 6.3 Future Kernel Work Required for T1-Z (Step-by-Step)
1. **Enable Bulk Physical Endpoints**:
   - In `dwc3_configure_endpoints()`, issue `DEPCMD_SETEPCONFIG` and `DEPCMD_SETTRANSXFR` for Physical EP2 (Bulk OUT) and Physical EP3 (Bulk IN).
   - In `DWC3_DALEPENA`, unmask bits 2 and 3: `dalep |= (1u << 2) | (1u << 3);`.
2. **Submit Bulk OUT Prime TRB**:
   - After host sets configuration (`USB_REQ_SET_CONFIGURATION`), call `dwc3_submit_bulk_out()` with a 512-byte coherent buffer.
3. **Connect Bulk OUT to TTY Ingress**:
   - On `dwc3_handle_bulk_out_complete()`, copy received bytes to `s_usb_rx_ring` and invoke `thread_call_enter(s_usb_rx_tty_call)`.
4. **Connect Console Write to Bulk IN**:
   - In `xzs_console_write()`, push stdout bytes to `s_usb_tx_ring`.
   - In DWC3 poll loop / IRQ handler, trigger `dwc3_flush_tx_to_bulk_in()`.

---

## 7. Verification & Acceptance Scripts

1. **`scripts/verify_d7t1_z_acceptance.py`**:
   - Validates physical pstore / console logs.
   - Requires verified runtime markers (`BULK_OUT_WORKING=yes`, `BULK_IN_WORKING=yes`, `USB_TO_TTY_BYTES>0`, `TTY_TO_USB_BYTES>0`, `LIVE_SHELL_PROMPT_OBSERVED=yes`).
2. **`scripts/parse_d7t1_z_acceptance.py`**:
   - Correlates host-side test outputs (`xzs-console`) with device pstore logs (`console-ramoops`).
   - Ensures no untested field is marked PASS.
