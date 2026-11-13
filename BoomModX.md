# Nightzim BoomMod X v1.0 — how it works

BoomMod X replaces the stock DCS World explosions with **volumetric explosions**: real 3D smoke, fire and dust
that were simulated offline and are drawn on top of the game frame by the GPU, anchored to the world, lit by the
mission sun, hidden behind terrain and drifting with the wind.

It is a different line from the 3.x particle mod in this repository. The 3.x versions only re-tuned the game's own
particle effects; BoomMod X draws its own effects as an overlay, in a similar spirit to how ATMOS-X layers its
changes on top of the game — but through a ReShade add-on, so the game's protected files are not modified.

---

## Requirements

- DCS World (DirectX 11).
- **ReShade 6.x "with full add-on support"** (reshade.me), installed on `<DCS>\bin\DCS.exe`, DirectX 10/11/12.
  The regular ReShade build blocks third-party add-ons.
- Multiplayer: the server must allow **object export**, because impacts are found through the weapons the game exports.

---

## Package contents — what each file does

| File | Goes to | What it does |
|---|---|---|
| `BoomModX_Instalador.exe` | (run from the zip folder) | Installer / manager. Finds DCS and Saved Games, installs, **activates / deactivates / uninstalls**, keeps SHA-256-verified backups of every game file it replaces, and exposes the options panel. |
| `payload\bin\NightzimBoom.addon64` | `<DCS>\bin\` | The engine. A ReShade add-on (native D3D11) that captures the depth buffer and the game's camera matrices, keeps the list of active explosions and renders them every frame. |
| `payload\bin\NightzimBoom\NightzimBoom.hlsl` | `<DCS>\bin\NightzimBoom\` | GPU code, compiled at startup: the ray-marching pixel shader (smoke, fire, dust), the sun self-shadow compute shader, the shockwave refraction shader and the camera-calibration probe. |
| `payload\bin\NightzimBoom\NightzimBoom.ini` | `<DCS>\bin\NightzimBoom\` | User settings: quality, smoke detail, colours, fire glow, shockwave strength per weapon class, wind, fade times. The installer never overwrites an existing ini. |
| `jdam.nzv` | `<DCS>\bin\NightzimBoom\` | Baked volume of a 2000 lb bomb (JDAM / Mk-84 / GBU-10/24/31 class), "v2" tuned against real footage: early soot that swallows the fireball, light ejecta dust. |
| `jdam_v1.nzv` | same | The original 2000 lb volume from v1.0 ("classic"); selectable in the installer or the F10 menu. |
| `mk82.nzv` | same | 500 lb class (Mk-82, GBU-12/38, FAB-250, Mavericks…). |
| `nar.nzv` | same | Rockets and small warheads (Hydra/APKWS, S-8, S-13…). |
| `napalm.nzv` | same | Incendiary weapons. |
| `aerea.nzv` | same | Air burst (missiles detonating in the air, proximity fuzes). |
| `tank.nzv` | same | Tank gun muzzle blast, oriented along the barrel. |
| `payload\SavedGames\Scripts\Hooks\nightzim_boom_hook.lua` | `Saved Games\DCS\Scripts\Hooks\` | DCS hook. Sends the camera to the add-on, follows every bomb/rocket/missile in flight and reports where it impacted, detects tank gun shots, computes the sun position for the mission date/time/place, reads the mission wind and builds the F10 menu. |
| `payload\SavedGames\Scripts\NightzimBoom\nzboom.dll` | `Saved Games\DCS\Scripts\NightzimBoom\` | Tiny Lua 5.1 C module used by the hook. It only writes and reads the shared memory block (`Local\NightzimBoomV1`) that connects the game's Lua side to the ReShade add-on. |
| `payload\ffx\*.ffx` + `*.lua` | `<DCS>\Bazar\Effects\ParticleSystem2\ffx\` | **Blank models**: empty versions of the six stock explosion effects (big, medium, rocket, napalm, air, tank shot), so the volumetric explosion is the one you see. Original files are backed up. Optional (checkbox in the installer). |
| `LEIA-ME.txt` | — | Short readme (Portuguese / English). |

Files created at runtime:

| File | What it is |
|---|---|
| `<DCS>\bin\NightzimBoom\NightzimBoom.log` | Engine log (volumes loaded, depth buffer chosen, camera source, every explosion spawned). |
| `Saved Games\DCS\Logs\dcs.log` (lines `NIGHTZIM`) | Hook log: weapons tracked, impacts, tank shots, sun, options. |
| `Saved Games\DCS\Nightzim BoomMod X\` | Installer manifest, backups (with `.sha256`) and installer log. |
| `Saved Games\DCS\Scripts\NightzimBoom\opcoes.txt` | Choices made in the F10 menu, kept for the next missions. |

---

## How the engine works

### 1. Offline simulation (once, on a PC — not in the game)

Each explosion class was simulated with our own solver:

- **Blast phase** — a 1D compressible solver for the first milliseconds (shock front, fireball expansion).
- **Plume phase** — a 3D low-Mach solver on a grid of up to 192×192×288 cells (1–1.5 m): buoyancy, turbulence,
  entrainment, combustion of the detonation products, **soot** production, **dust** lifted from the crater and
  ejecta, temperature and flame tracers, ground interaction and ambient wind.

The result is a time series of 3D fields: soot density, dust density, temperature, flame intensity.

### 2. Baking to `.nzv`

The fields are down-sampled to 3 m cells and packed into a GPU-friendly file:

- Header `NZV1`: grid size, number of frames, cell size, burst position, value ranges, bake sun direction, duration.
- Frame times.
- For every frame, two RGBA8 3D textures:
  - **A** = soot, dust, temperature, flame;
  - **B** = pre-computed optical depths (towards the sky, the ground, the bake sun) and the fire light that scatters inside the cloud.

A 2000 lb explosion is 58 frames covering 23.5 s.

### 3. Detection — where and when

The hook asks the game for the objects in flight every frame:

- **Bombs, rockets and missiles** are followed until they disappear. If the weapon was descending, its trajectory
  is projected to the terrain, so the explosion lands on the ground even when the last reported position was a
  few dozen metres above it. Weapons that vanish high in the air become **air bursts**.
- The **class** comes from the weapon name (2000 lb, 500 lb, rocket, napalm…).
- **Tank shots**: a new high-velocity shell is traced back to the armoured vehicle that fired it. The muzzle blast
  spawns at the end of the barrel, pointing where the turret was aiming (AAA guns are excluded).
- The same method works in single player and as a multiplayer client (when the server allows object export).

### 4. Placement

Each explosion is a box (the simulation grid) anchored to the world at the impact point. To avoid identical
explosions, every instance gets a random rotation around the vertical axis, a random mirror and ±10% animation
speed (tank shots keep the barrel direction). The smoke then **drifts with the mission wind**, with wind shear:
the dust near the ground moves about 35% of the wind, the cloud on top moves 100%. Up to 12 explosions live at
the same time and are drawn back to front.

### 5. Rendering (every frame, inside ReShade)

- **Depth**: the add-on tracks the game's depth buffers, picks the main scene buffer and copies it just before it is
  cleared, so the volumes are correctly hidden behind terrain, buildings and aircraft.
- **Camera**: instead of guessing, the add-on reads the projection and view matrices that DCS uploads to the GPU
  (constant buffers), so FOV, zoom and head movement match the game exactly, with no lag. A terrain-based
  calibration is kept as a fallback.
- **Lighting**: the sun direction is computed from the mission date, time, map and coordinates. A compute shader
  recomputes the cloud's **self-shadow** for that sun every frame (the baked one is only a fallback). Sky light,
  ground bounce and the fire's own light (the reddish glow under the cloud) are added.
- **Ray marching**: for every pixel the shader walks through the volume, accumulates extinction and light, adds
  **blackbody emission** for the hot gases (short white-yellow fireball, then deep red pockets inside the soot),
  applies procedural "cauliflower" noise that rises with the smoke for small-scale detail, fades at the box walls,
  adds atmospheric fog by distance and tone-maps the result.
- **Cockpit**: anything closer than 5 m to the camera (canopy frame, panel) masks the explosion when the game draws
  the cockpit in a separate pass.
- **Shockwave**: for large weapons a spherical shell expands at the speed of sound; the background seen through
  its rim is bent (refraction), and for the 2000 lb and 500 lb classes a short **Wilson cloud** (condensation) appears
  in the first 0.4 s.

### 6. Replacing the stock explosions

The six stock explosion effects are swapped for blank ones placed under their original names in `Bazar`, the same
way the 3.x releases did, so the volumetric explosion is the visible one. This step is optional and fully
reversible from the installer.

---

## Options

**In the installer** ("Aplicar opcoes"): JDAM new / classic, realistic / classic colours, quality
(performance / balanced / max), smoke detail, shockwave on/off, Wilson cloud on/off, smoke drift with wind on/off,
replace / keep the stock explosions.

**In game, F10 → "Nightzim BoomMod X"** (single player and your own missions): JDAM version, colours and
shockwave, applied immediately and remembered.

---

## Multiplayer and integrity check

- Nothing in the game's protected `Config` or shader folders is modified. The add-on and the hook are new files;
  the blank models replace files under `Bazar\Effects` using their original names.
- The explosions are visual and local: only you see them.
- If a server does not allow object export, no impacts are detected. In that case use **Desativar** (or untick
  "replace the stock explosions") before joining, so you still see the game's own explosions.

---

## Known limitations / roadmap

- The stock ground dust puff is still drawn by the game (it lives in protected config files).
- VR is not supported yet.
- Cluster munitions open in the air and currently show as an air burst.
- More warhead classes and higher-resolution volumes are planned.

---

*Nightzim BoomMod X — by Nightzim. Feedback and ideas are welcome on r/hoggit or Discord
(ID 999680611314831390). If you like it: PayPal fcirera2007@gmail.com — "buy me a module", every penny is welcome.*
