# D8-M8 Artifact: Kickoff & Completion Architecture

## 1. Kickoff Trigger Sequence
1. Ensure DSI0 controller and PHY are initialized in Command Mode (`DSI_CTRL = 0x1F5`).
2. Program DSI MDP command stream registers:
   - `DSI_COMMAND_MODE_MDP_CTRL` (`0x00994040`) = `0x00000008`
   - `DSI_COMMAND_MODE_MDP_DCS_CMD_CTRL` (`0x00994044`) = `0x00013C2C`
   - `DSI_COMMAND_MODE_MDP_STREAM0_CTRL` (`0x00994058`) = `0x0CA90039`
   - `DSI_COMMAND_MODE_MDP_STREAM0_TOTAL` (`0x0099405C`) = `0x07800438`
3. Program SSPP RGB0 registers (`0x00915000..38`):
   - Set source size, img size, out size to `0x07800438`
   - Set `SRC0_ADDR` to framebuffer physical address
   - Set `SRC_YSTRIDE0` to `4320` (`0x10E0`)
   - Set `SRC_FORMAT` to `0x000236AA` and `SRC_UNPACK_PATTERN` to `0x03010002`
4. Program LM0 registers (`0x00945000..08`):
   - Set `LM0_OUT_SIZE` to `0x07800438`
5. Program PP0:
   - Set `PP0_TEAR_CHECK_EN` (`0x00971000`) = `0x00000000` (Disable TE wait)
6. Program CTL0:
   - Set `DISP_INTF_SEL` (`0x00901004`) = `0x00000100` (INTF1 = DSI)
   - Set `CTL_LAYER_0` (`0x00902000`) = `0x00000200` (RGB0 on BASE stage)
   - Set `CTL_TOP` (`0x00902014`) = `0x00020010` (CMD mode, OUT_SEL=INTF1)
7. Flush shadow registers:
   - Write `CTL_FLUSH` (`0x00902018`) = `0x00020048`
8. Kickoff frame:
   - Write `CTL_START` (`0x0090201C`) = `0x00000001`

## 2. Completion Detection Protocol
- Poll `MDSS_MDP_REG_INTR_STATUS` (`0x00901014`) for `BIT(8)` (`0x00000100`).
- Timeout: 100 ms (~6 frames @ 60Hz) using `cntvct_el0` cycle counter.
- On match (`status & 0x100 != 0`):
  - Write `0x00000100` to `MDSS_MDP_REG_INTR_CLEAR` (`0x00901018`).
  - Check DSI error registers (`0x009940ac` and `0x009940b0` == 0).
  - Verdict: `PASS_KICKOFF`.
