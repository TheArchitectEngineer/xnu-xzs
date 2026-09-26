# D8-M8 Artifact: Scanout Completion & Safety Verification Plan

## 1. Completion Target Definition

| Field | Proven Specification | Evidence |
|---|---|---|
| **Completion Register** | `MDSS_MDP_REG_INTR_STATUS` (`0x00901014`) | `mdss_mdp_hwio.h:52` |
| **Completion Bit** | `BIT(8)` (`0x00000100`): `MDSS_MDP_INTR_PING_PONG_0_DONE` | `mdss_mdp.h:174` |
| **Clear Register** | `MDSS_MDP_REG_INTR_CLEAR` (`0x00901018`) | `mdss_mdp_hwio.h:54` |
| **Clear Value** | Write `0x00000100` | Self-clearing ACK |
| **Timeout Policy** | 100 milliseconds (100,000 µs, ~6 display frames @ 60Hz) | `mdss_mdp_intf_cmd.c:699` |
| **Watchdog Policy** | Pet watchdog before and during polling loop | Prevent reboot during DMA |

## 2. Execution Protocol

```c
/* Pseudocode implementation for future M8-7 execution */
int xzs_d8m8_wait_frame_done(void)
{
    uint64_t start_cycles = __builtin_arm_rsr64("CNTVCT_EL0");
    uint64_t freq = 19200000ULL; /* 19.2 MHz MSM8996 system timer */
    uint64_t timeout_cycles = (freq * 100) / 1000; /* 100 ms */

    while ((__builtin_arm_rsr64("CNTVCT_EL0") - start_cycles) < timeout_cycles) {
        uint32_t status = d8p1_read32(0x00901014u);
        if (status & 0x00000100u) {
            /* Frame completed successfully: clear interrupt bit */
            d8p1_write32(0x00901018u, 0x00000100u);
            return 0; /* SUCCESS */
        }
        xzs_watchdog_pet();
    }
    return -1; /* TIMEOUT */
}
```

## 3. Post-Frame Health Verification

After frame completion, inspect the following error status registers:
1. `DSI_ACK_ERR_STATUS` (`0x00994068`): Must be `0x00000000` (no DSI ACK error packets from panel).
2. `DSI_TIMEOUT_STATUS` (`0x009940c0`): Must be `0x00000000` (no HS or LP TX timeouts).
3. `DSI_FIFO_STATUS` (`0x0099400c`): Must have bit 8 cleared (no command FIFO underflow/overflow).
4. `DSI_STATUS` (`0x00994008`): Must return to idle (`0x00000000`).
