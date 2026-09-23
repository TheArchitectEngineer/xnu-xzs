# D8 display bring-up

`main` carries the clean D8-M1 runtime from tag `xzs-d8-m1-complete` (`861032cb7b137096edeb1aa5caa04aef6737533a`). That tree matches the hardware-proven read-only image. D8-M2 writes are not in this file's tree. The measured collapsed-domain values below are the D8-M1 result. A later boot on `xzs-d8-m2-power` turned MDSS on and left `mdss_ahb` halted. See [`docs/XZS_BLOCKERS_AND_DEFERRED.md`](XZS_BLOCKERS_AND_DEFERRED.md).

First pixels, later, are CPU framebuffer to MDP5 to DSI to the panel. No GPU. D8-M1 does not turn anything on.

## Path

```text
XO / GPLL0 / MMPLL / DSI PLL
        ↓
GCC  0x00300000
        ↓
MMCC 0x008c0000
        ↓
MMAGIC_MDSS_GDSC then MDSS_GDSC
        ↓
MDSS 0x00900000
        ↓
MDP5 0x00901000  (intf1 → DSI0, intf2 → DSI1)
        ↓
DSI0 0x00994000
        ↓
PHY 0x00994400 / lanes 0x00994500 / PLL 0x00994800
        ↓
panel
        ↓
backlight
```

Reference for addresses and clock parents is `device/reference/msm8996.dtsi` plus Linux `drivers/clk/qcom/mmcc-msm8996.c`. That is reference evidence, not proof the block is powered on this phone.

`status = "disabled"` on the MDSS and DSI nodes is the Linux node state. It does not say the hardware power domain is off.

## Power and reset

| Block | Register | Role |
|---|---|---|
| MMAGIC_MDSS_GDSC | MMCC `0x247c` | Parent domain. Upstream marks it votable and always-on. |
| MDSS_GDSC | MMCC `0x2304` | Domain that gates MDSS, MDP, DSI, PHY, and PLL slaves. |
| MDSS_BCR | MMCC reset cell named in the DTS | Block reset. Offset is not copied here. No write in M1. |
| MMAGIC_MDSS_BCR | MMCC `0x2470` | Reference only. No write in M1. |

GDSC bit 31 is `PWR_ON`. Bit 0 is `SW_COLLAPSE`. Branch bit 0 is the enable.

MDSS, MDP, DSI, PHY, and PLL registers stay unread. A read there while the GDSC is off can stall the bus.

## Clocks

MMCC branch registers, read allowlist:

| Clock | MMCC offset | Parents (reference) |
|---|---|---|
| mmss_mmagic_cfg_ahb | `0x5054` | ahb_clk_src, marked critical upstream |
| mdss_ahb | `0x2308` | ahb_clk_src (XO, GPLL0, MMPLL0) |
| mdss_axi | `0x2310` | axi_clk_src |
| mdss_mdp | `0x231c` | mdp_clk_src (GPLL0, MMPLL5) |
| mdss_pclk0 | `0x2314` | DSI0/DSI1 PLL |
| mdss_byte0 | `0x233c` | DSI byte PLL |
| mdss_esc0 | `0x2344` | XO or DSI byte PLL |

GCC is `0x00300000`. `gcc_mmss_noc_cfg_ahb_clk` feeds the MMSS config path. Its CBCR offset is not in this tree, so XNU does not read it yet.

## What XNU reads

Only the MMCC allowlist above. Each read prints `MMCC_READ_PRE` before the load and `MMCC_READ_POST` after it. The shell is the record of the read. MDSS `0x00900000`, MDP `0x00901000`, DSI `0x00994000`, PHY, and PLL are not touched.

MMCC sits in the device window `0x00000000–0x01ffffff`, which the bootstrap map already installs for the watchdog. The read uses that identity map.

## Panel

The repo has no panel compatible string, lane count, timing, or DCS sequence. `panel_tvdd` in the Tone dtsi is a fixed regulator on GPIO 50, active-low, wired to the Synaptics touch controller. That is not a panel init sequence.

TWRP's DTB reserves `cont_splash_mem` at `0x83401000`. That is evidence Sony keeps a splash reservation. It is not evidence the panel is scanning when XNU starts. XNU `boot_args` video fields are zero because the boot shim clears them.

## Recovery-kernel observation

On this TWRP boot, debugfs enable counts were:

```text
gcc_mmss_noc_cfg_ahb_clk = 1
mmss_mmagic_cfg_ahb_clk  = 0
mdss_ahb/axi/mdp/byte0/pclk0/esc0 = 0
```

That is the recovery kernel's clock-consumer count, not the Sony bootloader state and not an XNU MMIO read.

## XNU read on 93c511c

Host log `artifacts/hw/d8m1-93c511c/host.txt`. Nine MMCC loads each printed PRE and POST. `pwd` then returned `/`. No MDSS, DSI, PHY, or PLL register was read.

| Register | Value | bit0 | bit31 |
|---|---|---|---|
| MMAGIC_MDSS_GDSC | `0xa0222000` | 0 | 1 |
| MDSS_GDSC | `0x00222001` | 1 | 0 |
| mdss_byte0 / pclk0 / esc0 | `0x80000000` | 0 | 1 |
| mmagic_cfg_ahb / mdss_ahb | `0x80008000` | 0 | 1 |
| mdss_axi / mdss_mdp | `0x80004220` | 0 | 1 |

For a GDSC, bit 31 is power-on and bit 0 is software collapse. The parent domain reports power on. MDSS_GDSC reports collapsed. For a branch, bit 0 is enable and bit 31 set means halted. The display branches are halted.

## D8-M2, not started

Each step is one shell transaction with a pre line, the action, and a readback. Nothing here is implemented as a write.

```text
1. Read MMAGIC_MDSS_GDSC and MDSS_GDSC.
2. If PWR_ON is clear, enable MMAGIC_MDSS then MDSS_GDSC.
3. Read both GDSCR values back.
4. Enable mdss_ahb, then mdss_axi, then mdss_mdp.
5. Read those branch registers back.
```

No PLL, PHY, panel, backlight, or scanout in that first sequence.
