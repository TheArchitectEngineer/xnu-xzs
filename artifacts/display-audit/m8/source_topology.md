# D8-M8 Artifact: Source Proven Topology Specification

## Target Device Configuration
```text
Hardware Platform: Sony Xperia XZs (G8231 / keyaki / tone)
SoC:               Qualcomm Snapdragon 820 (MSM8996 v3.0)
Display Panel:     Sharp + Synaptics command-mode panel (internal: "9")
Resolution:        1080 x 1920 (FHD), 60 Hz
Interface:         MIPI DSI0 in Command Mode (4 Data Lanes + 1 Clock Lane)
```

## Physical Address Map
```text
MDSS Base:         0x00900000 (Size: 0x90000)
MDP5 Core Base:    0x00901000 (Offset: 0x1000)
CTL0 Base:         0x00902000 (Offset: 0x2000)
SSPP VIG0 Base:    0x00905000 (Offset: 0x5000)
SSPP RGB0 Base:    0x00915000 (Offset: 0x15000)
LM0 Base:          0x00945000 (Offset: 0x45000)
INTF1 Base:        0x0096b800 (Offset: 0x6b800)
PP0 Base:          0x00971000 (Offset: 0x71000)
DSI0 Base:         0x00994000
```

## Evidence Provenance
- `keyaki.dts`: Lines 206, 226, 234-252, 268-270.
- `mdss_mdp.c`: Lines 1716, 2008.
- `mdss_mdp_ctl.c`: Lines 3147, 3395, 3757.
- `mdss_mdp_intf_cmd.c`: Lines 640, 699.
- `mdss_dsi_host.c`: Lines 334, 1143-1164.
