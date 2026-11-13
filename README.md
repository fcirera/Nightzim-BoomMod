# Nightzim BoomMod

> **NEW: Nightzim BoomMod X v1.0** - volumetric explosions drawn on top of the game (ReShade add-on), triggered by real bombs, rockets and missiles. How it works and what every file does: [BoomModX.md](BoomModX.md)

Physically simulated explosion effects for **DCS World**.

**Video (stock effects vs. mod, side by side):** https://youtu.be/ScPUO9IXvjo

Simulation renders of the other warheads:

- Rocket / small explosion: https://youtu.be/Lwa2Qc4Zwtc
- Medium bomb: https://youtu.be/Y25AtSTF7u8
- Napalm: https://youtu.be/-kpGMlM3ZQs
- Tank shell impact: https://youtu.be/gMRdXqjrats

Instead of hand-drawing an explosion, BoomMod simulates it: a custom solver computes the blast wave, combustion and buoyant rise of the hot gases, and that physics is what builds the mushroom cloud. The result is converted to DCS's own particle format (`.ffx`).

- Does **not** touch shaders or `Config\Effects` - passes the multiplayer integrity check
- Same particle budget as the stock effects (1024 per effect)
- Original files are backed up (with SHA-256) and can be restored at any time

## What is replaced

| DCS effect | BoomMod file | Used for |
|---|---|---|
| `airExplosion_01` | `nightzim_air_02` | air burst |
| `groundExplosion_big` | `nightzim_jdam_01` | large bombs (2000 lb class) |
| `groundExplosion_03_01` | `nightzim_mk82_01` | medium bombs |
| `groundExplosion_nar` | `nightzim_nar_01` | rockets / small warheads |
| `groundExplosion_napalm` | `nightzim_napalm_01` | napalm |
| `T90A_shot_exp_b` | `nightzim_tankshot_01` | tank shell impact |

**Download:** https://github.com/fcirera/Nightzim-BoomMod/releases/latest

## Beta

**v3.3 beta** (feedback from r/hoggit): grey-brown smoke instead of black, smaller and denser particles at the base, visible initial expansion, stronger inverted cone of earth and debris. Files in `beta/`; download from the [v3.3-beta pre-release](https://github.com/fcirera/Nightzim-BoomMod/releases/tag/v3.3-beta). Same installer, same IC-safe method. Run the v3.2c installer again to go back.

## Install

1. Close DCS.
2. Download `Nightzim_BoomMod_v3.2c_installer.zip` from the [latest release](https://github.com/fcirera/Nightzim-BoomMod/releases/latest), extract the .bat, run it and choose **1** (install) or **2** (restore originals).
3. After a DCS update or repair, run it again.

The installer menu is currently in Portuguese. Backups are kept in `Saved Games\DCS\Nightzim BoomMod\Backup`.

### Option B - OVGME / mod manager

Download `Nightzim_BoomMod_v3.2c_OVGME.zip` from the [latest release](https://github.com/fcirera/Nightzim-BoomMod/releases/latest), put it (or the extracted `Nightzim BoomMod v3.2c` folder) in your OVGME mods folder for the DCS **game root** (not Saved Games) and enable it. The mod manager keeps its own backup of the original files. After a DCS update or repair, disable and re-enable the mod.

Use either the .bat installer or the mod manager, not both.

## Known limits

- Medium explosions sometimes use a stock preset defined in `Config`, which the mod cannot replace without failing the integrity check.
- Smoke colour ageing is approximated through the glow channel, so smoke is slightly luminous at night.

## What would unlock more in DCS

1. Per-particle colour over lifetime (today: one `SmokeColor` per effect)
2. More than 1024 particles per effect, with distance LOD
3. Per-particle size
4. Black-body colour ramp instead of a fixed temperature^4 glow
5. User effects in Saved Games that can pass the integrity check
6. A separate long-lived ground-dust layer

## Support

If you enjoy the mod, **buy me a module** - it keeps me motivated to keep improving the sim's effects. Every penny is welcome.

PayPal: `fcirera2007@gmail.com`

## Contact

Questions and suggestions are welcome. Discord ID: `999680611314831390`

## Status

Version 3.2c, work in progress. This repository contains the mod files and the installer. The simulation engine and the `.ffx` exporter are not published here.

Not affiliated with Eagle Dynamics. DCS World is a trademark of its owner.
