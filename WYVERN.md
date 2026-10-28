# This fork, for the EMAX Wyvern stack

Integration branch: `pixel-fusion-sync`, on top of OpenIPC/msposd `main`
(`upstream` remote; level with it on 2026-10-28). The commit flying is pinned
in wyvern-ai-osd `versions.lock`, the ground build in sbc-groundstations
`package/msposd/msposd.mk`.

## What it adds, and where

| Area | Files | What |
|---|---|---|
| Pixel OSD | `osd/pixel/` | widgets placed in pixels from a layout file (`-P layout.ini`): TrueType text with a real outline, tapes, ladder, compass, home vector, bars; dirty-rectangle redraw; fed from MSP or MAVLink |
| Detection boxes | `osd/util/` | the camera's YOLO boxes and track ids drawn into the OSD's own RGN region (the SoC cannot stack two full-screen regions) |
| MAVLink OSD | `osd/mav_osd.c` | Betaflight and ArduPilot flight modes, RSSI, FC messages with severity |
| Link status | `osd/pixel/px_datalink.c` | parses aalink's status line; REDUCING HEAT when its thermal step-down runs |
| Ground map | `osd.c`, `osd/util/Render_gs.c` | upstream's map renderer, made to run in the goggles (Rockchip VRX build): shown at start, follows `msposd.ini` and the map pack live, a provisional home before arming, redrawn on attitude when no DisplayPort reaches the ground; the camera forwards MSP to it (`-o`) |
| MSP | `osd/msp/`, `msposd.c` | aggregation buffer overflow fixed; ArduPilot flight mode from `MSP_NAME` |
| Fonts | `fonts/` | UAV OSD Mono / Sans Mono and JetBrains Mono Bold, subset to the OSD glyphs (OFL texts alongside) |

Everything else is upstream's, so merging `upstream/main` stays a merge.

## Build

- Camera (star6e): `build_star6e.sh`, or wyvern-ai-osd
  `tools/build/build_msposd_local.sh` (builds the local commit in WSL).
- VRX (Rockchip): through the sbc-groundstations image, which pins a commit.
