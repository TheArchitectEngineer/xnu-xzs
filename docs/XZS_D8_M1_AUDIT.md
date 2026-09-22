# D8-M1 read-only display audit

D8-M1 records the display handoff XNU already has. It does not program the panel, the DSI PHY, clocks, or the MDP.

The shell commands `display`, `dsi`, `clocks`, `irq`, `fb`, and `mem` call one kernel backend, `xzs_diag_dispatch()`. That backend builds each line once. `xzs_bringup_console_write()` then mirrors the same bytes to the USB console and to the existing pstore console path. A copy is also kept in an 8 KiB kernel ring.

## What is safe to report

| Command | Evidence class | What it reports |
|---|---|---|
| `display` | CODE AUDIT | Reference MDSS and MDP addresses from `device/reference/msm8996.dtsi`. No MMIO read. |
| `dsi` | CODE AUDIT | Reference DSI0 host, PHY, lane, and PLL addresses. No MMIO read. |
| `clocks` | CODE AUDIT | Clock names from that same dtsi. MMCC is not mapped. |
| `irq` | TARGET plus CODE AUDIT | Live USB and UART interrupt counters the kernel already increments. Display INTID 115 is reference only. GIC pending bits are not read. |
| `fb` | TARGET | `PE_state.video` copied from boot args. Pixel memory is not read. |
| `mem` | TARGET | `boot_args` physical base, virtual base, and `memSize`. |

The dtsi marks the MDSS and DSI nodes `disabled` and attaches MDSS to the `MDSS_GDSC` power domain. This port has no proof that those registers answer while that domain is off. A read there can stall the bus, so D8-M1 does not touch them.

The boot shim zeroes `boot_args.Video` before entering XNU. `fb` therefore reports the handoff structure XNU stored. A zero base is a target-side observation of that structure, not a measurement of the panel.

## Later phases

D8-M2 and later may change power domains, clocks, the PHY, the panel, the backlight, and scanout. Those steps need a persistent checkpoint before the write and another after it. The first pixels, when that work starts, are solid red, green, blue, white, and black through the CPU framebuffer, MDP5, and DSI. That path does not use the GPU.
