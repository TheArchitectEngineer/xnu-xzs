# D8-M4 Forensic Analysis: MSM8996 DSI0 Host Controller (v1.4.1)

## 1. Overview & Objective

Milestone **D8-M4** configures the Qualcomm Snapdragon 820 (MSM8996 v3.0) DSI0 Host Controller from its post-M3 idle state to a hardware-accepted, correctly configured MIPI DSI Command-Mode host state.

### Proven Foundation (D8-M3 Frozen Lower Layer)
D8-M4 inherits an immutable, hardware-verified physical foundation:
- DSI0 PLL: Locked (`0x009948cc = 0x2f`), VCO = 1794.078 MHz, Pixel Clock = 149.507 MHz, Byte Clock = 112.130 MHz
- MMCC Clock Trees:
  - `BYTE0`: unhalted (`0x008c233c = 0x00000001`, `root_off=0`, `src_sel=1:DSI0_BYTE`)
  - `PCLK0`: unhalted (`0x008c2314 = 0x00000001`, `root_off=0`, `src_sel=1:DSI0_PIXEL`)
  - `ESC0`: unhalted (`0x008c2344 = 0x00000001`, `root_off=0`, `src_sel=0:XO`)
- 14nm PHY Stage B:
  - 5-lane regulator bias: `0x1d` across DL0, DL1, DL2, DL3, CLK
  - 5-lane drive strength: `0x0ff` across DL0, DL1, DL2, DL3, CLK
  - Lane Status: LP-11 idle

---

## 2. Keyaki Display Specifications & Topology

- **SoC**: Qualcomm MSM8996 v3.0 (`keyaki` / Sony Xperia XZs G8231)
- **Display Topology**: Single DSI0 Controller, 4 Data Lanes + 1 Clock Lane
- **Mode**: Command Mode (MIPI DSI Command Mode with Smart Panel / TE sync)
- **Panel Geometry**: 1080 x 1920 @ 60 Hz, 24 bpp (RGB888)
- **Timing Parameters**:
  - Horizontal: Active 1080, Front Porch 56, Pulse Width 8, Back Porch 8
  - Vertical: Active 1920, Front Porch 227, Pulse Width 8, Back Porch 8
  - Clocks: Pixel 149.507 MHz, Byte 112.130 MHz, Bit 897.039 MHz, Escape 19.200 MHz

---

## 3. Forensic Register Audit: DSI0 Host Controller (`0x00994000`)

The MSM8996 DSI controller revision is `0x10040001` (DSI v1.4.1).

### Four Target Host Registers Deferred from M3

| Address | Symbolic Name | Post-M3 Actual | Golden Linux Target | Function & Mask | Rationale / Bitfield Semantics |
|---|---|---|---|---|---|
| `0x0099400c` | `DSI_CTRL_0` | `0x11111000` | `0x33333000` | Mask `0xfffff000` | Lane controller configuration. 5 nibbles for CLK, DL0..DL3. Bits [31:12] = 0x33333 enables operational mode for all 5 lanes. |
| `0x00994018` | `DSI_TIMING_CTRL` | `0x0000000e` | `0x0000001b` | Mask `0x0000003f` | Clock postamble timing (`t_clk_post` = 27 byte clock cycles). Linux write 0069. |
| `0x009942a0` | `DSI_T_CLK_PRE_EXTEND` | `0x00000000` | `0x0000002b` | Mask `0x0000003f` | Clock preamble extension timing (`t_clk_pre` = 43 byte clock cycles). Linux write 0070. |
| `0x009940f0` | `DSI_CTRL` | `0x00000000` | `0x00000001` | Mask `0x00000001` | DSI Master Controller Enable. Bit 0 = 1 enables the controller core. Linux write 0072. |

### Supporting Operational Registers (Verified from Golden Linux State)

| Address | Symbolic Name | Golden Value | Verification / Status |
|---|---|---|---|
| `0x00994000` | `DSI_HW_VERSION` | `0x10040001` | Read-only hardware version (MSM8996 v1.4.1) |
| `0x00994014` | `DSI_FIFO_STATUS` | `0x31211101` | FIFO fill status; shows idle empty buffers |
| `0x00994110` | `DSI_COMMAND_MODE_MDP_CTRL` | `0xa220aa02` | Command Mode MDP interface configuration |
| `0x009941b4` | `DSI_EOT_PACKET_CTRL` | `0x00000024` | EoT append and check enabled |
| `0x009941b8` | `DSI_LANE_STATUS` | `0x00000006` | Status flags: 0x6 indicates all lanes in LP-11 idle state |
| `0x009941f4` | `DSI_LANE_CTRL` | `0x03000104` | 4 data lanes enabled (DL0..DL3) + CLK lane |

---

## 4. D8-M4 Strict Safety Boundaries

In accordance with project safety guidelines:
- **NO DCS Traffic**: Zero DCS commands sent (e.g. no `0x11` Sleep Out, no `0x29` Display On, no `0x35` Tear On).
- **NO Panel Power/GPIO**: Panel reset GPIO, LAB/IBB (+/-5.5V rails), and WLED backlight drivers remain completely untouched.
- **Controller State**: The DSI host controller transitions from uninitialized/disabled to configured/enabled in idle command mode only.
