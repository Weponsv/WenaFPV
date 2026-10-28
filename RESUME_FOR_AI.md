# WenaFPV — Resume for AI Context

## Project Overview
**WenaFPV** — 5" FPV freestyle drone simulator. SDL2 + OpenGL 3.3 core, optimized for low-end Intel iGPUs (single static mesh, one draw call, cheap shaders). Target hardware: ThinkPad X260 (i7-6500U, HD Graphics 520).

---

## Current State Summary (v1.1.6_ALPHA)

### What Was Removed (Training/Game Features)
All training-specific game mechanics have been stripped out:

**From `main.cpp`:**
- All training state variables: `GateState` struct, `gates` vector, `currentGate`, `routeTimer`, `timerRunning`, `bestTime`, `collisions`, `lastCollisionTime`, `currentZone`, `zoneNames`, `gateVertCount`, `reactionSeed`, `reactionZoneEnabled`, `useLevelFile`, `levelFilePath`
- `loadAbandonedBase` lambda — replaced with simplified version that just builds the map without gates
- `reloadLevelFile` lambda
- Gate rendering VAO/VBO (`vaoGate`, `vboGate`, `gateVerts`, `gateVertCount`)
- Training logic in main loop (gate passes, timer, zone detection, collision counting)
- Training HUD (zone display, gates progress, timer, best time, collisions, next gate indicator, key hints)
- Gate rendering in render loop
- Extra key bindings: F5 (reload level), F6 (reseed reaction), F7 (start/stop timer)
- Startup key list for training features
- `reactionSeed` usage in `loadMap` (now uses fixed seed `0xDEADBEEF`)

**From `world.hpp`:**
- No deletions — kept all map geometry, level file format, loader, generator, and materials

### What Was Added/Modified in This Session (v1.1.6_ALPHA)

#### Props Lowering Feature (Camera Settings)
- **New config option**: `propsLowering` (0.0–1.0, default 0.0) in CAMERA tab
- **Setting**: "Props lowering: 0-100%" slider, step 5%, saved in config.ini
- **Implementation**: Rotation around camera lateral axis in propeller overlay view matrix
  - `loweringAngleRad = (percent/100) * (vertical_half_FOV + 20°)`
  - `totalTiltRad = cameraTiltRad + loweringAngleRad`
- **Verification**: `--debug-props-lowering` flag produces:
  - Lit pixel tables for 0/25/50/75/100% at FOV 90/120/160, tilt 20/33
  - ASCII dumps (100×40) for FOV 160/tilt 33 at 0%, 50%, 100%
  - Computed angles per config
  - Props sweep through the lower corners as lowering increases and clear the corners at 100%
    (corner counts are not monotonic by design)
- **No regressions**: Props opacity and "Props in view" settings continue working

#### Procedural Prop Overlay (v1.1.6)
- One quad per front prop in the arm plane (two-sided, no culling, alpha blended), drawn by `PROP_OVERLAY_FS`
- Polar shader blades: tapered blade with rounded tip, hub, `fwidth` AA, analytic angular motion blur
  (1/240 s exposure, capped at blade pitch); blade count comes from the layout preset (3 blades)
- Spin is analytic via `uSpinPhase[4]` — vertices no longer rotate around the drone origin
- Props sit at the real motor positions (`±armCoord, y=0.022, z=−armCoord`); the old artificial
  forward offset (`propZ = camZ − 0.03`) is removed. The world FPV camera uses the same mount,
  so the view and the overlay agree.
- Blade shading tuned for readability: mid-grey carbon body, bright edge sheen, light tip highlight
  (~2× brighter than the first cut so blades read against both sky and ground); blade half-width
  root/mid/tip = 0.22 / 0.50 / 0.110 of the tip radius
- Shared `propOverlayLayout()` keeps all tunable geometry in one place (no new config settings):
  - 3.5" mount 27.0 mm forward / 28.5 mm high, prop radius 30 mm
  - 5" mount 40.0 mm forward / 29.0 mm high, prop radius 42 mm (`armLen × 0.40`)
- `--verify-props` renders through the production shader and reports pixel-readback stats
  plus 100×40 ASCII alpha/look dumps (static and 15000 RPM):
  - 3.5" @ FOV160/tilt33/lowering25: prop centre NDC (+1.03, −0.87), coverage 4.25%,
    87.2% of overlay pixels in the outer 25% bands
  - 5" @ same: NDC (+0.99, −0.76), coverage 4.25%, 88.9% outer
  - FOV120 and FOV90: 0% coverage (props fully out of frame); at 15000 RPM stays ≥80% outer
- `--bench-props 300` (batched GL timer query, 1280×720): 0.15–0.17 ms/frame on Intel HD 520
  (target <0.3 ms)
- Debug flags `--debug-overlay` / `--debug-props-lowering` now render through the production shader
  (flat per-prop colour path via `uDebugFlat`) using the same shared layout; the old
  `DEBUG_PROP_OVERLAY_*` shader pair is deleted

#### Config & Settings (v1.1.5)
- **`abandoned_base_seed`** config option in QUALITY tab (F5), full uint32 range, persists to config.ini
- Default seed: `0xDEADBEEF` (3735928559)

#### HUD Positioning (main.cpp)
- **VID quality HUD** moved from top-right to upper-left, directly under battery MAH display

#### Texture Randomization & Detail Props (world.hpp)
New helper functions:
- `jitterColor()` — adds ±random variation to base color
- `pickColor()` — picks from palette
- `groundColor()`, `dirtColor()`, `concreteColor()`, `rustColor()`, `steelColor()` — 5-color palettes each
- `addDetailProp()` — places random detail props: rocks, debris piles, metal scrap, tires, wooden pallets, concrete pipes, traffic cones, weed clumps

Maps updated with per-instance color variation and detail props:
| Map | Texture Changes | Detail Props |
|-----|-----------------|--------------|
| **Freestyle Park** | Ground split into 50m patches with grass variation; structures use varied colors | 80 props |
| **Gate Training** | Ground patches with grass variation | 40 props |
| **Slalom Training** | Ground patches with grass variation | 40 props |
| **3.5 Tech Park** | All concrete/steel/rust surfaces use varied colors | 60 props |
| **Abandoned Base** | Ground patches (grass/dirt mix), all materials use lambdas for per-instance variation, yard areas mixed grass/dirt | 100 props in ruin details |

All variations are deterministic per seed — same seed = identical map.

---

### What Remains (Core Freestyle Simulator)

#### Maps (5 selectable via F3/F4)
1. **FREESTYLE PARK** — Gates, gantry crane, dive towers (6m gap), 3-floor bando frame, containers, slalom poles, trees, asphalt loop with traffic
2. **GATE TRAINING** — Progressive gate line (8 gates, varying sizes)
3. **SLALOM TRAINING** — S-bend gates with marker poles
4. **3.5" TECH PARK** — Compact 100×100m arena: sunken channels, perimeter rails, gate line, willows, swing set, jungle gym, skate bowl, pipe rack, scaffold row, abandoned shell, silo, warehouse
5. **ABANDONED BASE** — Ruined concrete industrial/military base (~120×120m) with 5 zones:
   - **SPEED** — Long corridor (50m, 6m wide, 8m tall) with irregular wall gaps, low beams, broken ceiling
   - **AIM** — 5 walls with decreasing gaps (2.0m → 1.5m → 1.0m → 0.7m → 0.5m), vertical shaft for dive
   - **REACTION** — Courtyard with 35 seeded obstacles (hanging beams, fallen slabs, barrels, pipes, fences, rubble)
   - **NOTICE** — Thin/low-contrast hazards (exposed rebar, hanging cables, wire fence, thin poles, camo beams, graffiti)
   - **OUTDOOR** — Overgrown yard, barrels, rusted container, 8-sided tower/chimney (18m), roof dive ramp

#### Core Systems Preserved
- **Materials, colors, lighting** — All shaders intact (world, shadow, sky, video, OSD)
- **Collision system** — Sphere vs box, thin cylinder swept tests, spatial hash (4 ground contacts + 1 body sphere)
- **Level file format & loader** — `box`, `thin`, `spawn`, `marker` parsing with error handling for malformed lines
- **F5 reload** — On abandoned base map: reloads level file; on other maps: opens settings
- **Procedural generator** — `wenafpv --generate SEED OUTPUT` creates level files from seeds
- **Normal crash/respawn** — R key resets to spawn point
- **Map selection** — F3/F4 cycles through all 5 maps
- **Standard key bindings** — Printed at startup (F1-F5, F11, R, SPACE, arrows, WASD, etc.)
- **Analog video effects** — Full NTSC-style interference simulation (noise, static, chromatic aberration, scanlines, rolling bar, tear, dropout, dot-crawl, sync loss, color burst loss, multipath ghost)
- **Procedural prop overlay** — One shader-drawn quad per front prop (polar blades + analytic motion blur), placed at the screen edges by the preset camera mount; props opacity / props in view / props lowering still control it

---

## File Structure

```
fpvsim-working/
├── src/
│   ├── main.cpp       # SDL2/OpenGL renderer, radio input, OSD, RC visualizer (~2000 lines)
│   ├── sim.hpp        # Physics core: rates, motors, drag, battery, collisions (headless, unit tested)
│   ├── world.hpp      # Level format, map builders (freestyle, abandoned base, 3.5" tech park, etc.)
│   ├── config.hpp     # INI load/save, 50+ settings entries
│   └── video.hpp      # Analog video signal system (FPV interference effects)
├── tests/
│   └── physics_test.cpp  # 48 headless physics tests
├── Makefile           # Build: wenafpv, test, install, clean
├── README.md          # User documentation
├── UPDATES.md         # Change log (physics fix for spurious lift)
├── RESUME_FOR_AI.md   # AI context resume
├── wenafpv             # Built executable
```

---

## Build Verification
- ✅ Main executable builds successfully (only pre-existing unused variable warnings in map builder)
- ✅ All 48 physics tests pass
- ✅ No dead code, unused variables from removed features, or commented-out blocks
- ✅ `--generate` command works: `wenafpv --generate 12345 my_map.level`
- ✅ Level file loads correctly (boxes, thin, spawn, markers)
- ✅ F5 reloads level file on abandoned base map
- ✅ Abandoned base seed setting persists in config.ini
- ✅ VID HUD positioned correctly under battery MAH
- ✅ Texture randomization applied to all 5 maps
- ✅ Props lowering setting works, persists, verified with `--debug-props-lowering`
- ✅ Procedural prop overlay verified with `--verify-props` (pixel readback + ASCII dumps; FOV120/90 keep props out of frame)
- ✅ Overlay GPU cost measured with `--bench-props 300`: ≤0.17 ms/frame on HD 520 (target <0.3 ms)

---

## Physics Model (sim.hpp)

### Drone Parameters (DroneParams)
- Mass, thrust-to-weight, throttle curve, motor lag, rate lag, yaw lag
- Prop pitch speed, collision radius, arm length
- Collider scale, contact skin, landing assist (0-3), ground damping, landing snap
- Body-axis quadratic drag (dragK) and linear drag (dragLin)
- Battery: cells, capacity, pack resistance
- Rates: Betaflight "Actual" (center, max, expo) per axis

### Simulation Step (500 Hz fixed)
1. Arming logic (throttle must be low)
2. Motor thrust commands → rate targets → PID-like torque → per-motor thrust with lag
3. Quaternion integration with gyroscopic coupling
4. Aerodynamic drag in body frame, applied opposite world-frame air velocity (fixes spurious lift)
5. Wind velocity (optional, smooth gusts + turbulence)
6. Battery model with voltage sag, capacity drain, hard cutoff at 3.0V/cell
7. Collision: sphere vs boxes (world), swept sphere vs thin cylinders (wires/rebars/pipes)
8. Landing assist: contact-gated, only on gentle flat landings

### Key Physics Fix (v1.0.0)
**Problem:** At high pitch angles with throttle cut, drone would "glide" at constant pitch instead of falling.
**Root Cause:** Anisotropic body-frame drag created upward component when rotated to world frame.
**Fix:** Compute drag magnitude from body-frame coefficients, apply force opposing **world-frame air velocity vector**.

---

## Level File Format (world.hpp)

```ini
# box x y z  x2 y2 z2  r g b  mat
# thin x1 y1 z1  x2 y2 z2  radius  r g b  mat
# spawn x y z  yawDeg
# marker x y z  type  index  radius  r g b
# type: 0=checkpoint, 1=gate
```

- **Materials:** 0=flat, 1=ground checker, 2=building facade (window shader)
- **Thin cylinders:** Rendered as 4 thin boxes; collision uses swept sphere-cylinder test
- **Markers:** Loaded but ignored for rendering/collision (training features removed)

---

## Config System (config.hpp)

**Settings file:** `~/.config/wenafpv/config.ini` (created on first run)
**Tabs in F5 menu:** QUALITY, DISPLAY, CAMERA, RADIO, RATES, DRONE, BATTERY, SOUND, VIDEO, LANDING, WIND, SHADOW

Key settings:
- **Quality:** Low/Medium/High (resolution, MSAA, fog, detail)
- **Video effects:** 14 toggles + 7 intensity knobs (0..2) for analog FPV simulation
- **Drone size:** -1=Custom, 0=75mm, 1=85mm, 2=3in, 3=3.5in, 4=5in (loads physics profiles)
- **Wind:** Enabled, strength (0-100→0-18m/s), direction, gust, turbulence, HUD
- **Shadows:** Enabled, resolution (512/1024/2048), distance, fade, altitude, strength, bias, filtering
- **Radio:** Axis mapping, inversion, deadband, arm threshold, joystick name filter
- **Camera:** Props in view, Props opacity, **Props lowering (0-100%, step 5%)**

---

## Controls

| Key | Action |
|-----|--------|
| F1 | RC input visualizer (Mode 2 stick boxes) |
| F2 | Force keyboard mode |
| F3/F4 | Previous/Next map |
| F5 | Settings menu (Tab=tab, arrows=edit, Enter/Space=select) |
| F11 | Fullscreen toggle |
| R | Reset quad + battery |
| ESC | Quit (saves config) |
| `[` `]` / PgDn/PgUp | Camera tilt ±1° |
| `-` `=` | Horizontal FOV ±5° |

**Keyboard fallback (no radio):**
- W/S = throttle, A/D = yaw, Arrows = pitch/roll, SPACE = arm

---

## Usage Examples

```bash
# Generate a level file from seed
./wenafpv --generate 12345 my_map.level

# Run sim, press F3/F4 to select "ABANDONED BASE" map
./wenafpv

# While on abandoned base map, press F5 to reload my_map.level

# Debug props lowering verification
./wenafpv --debug-props-lowering

# Verify prop overlay placement/appearance (renders via the production shader, then exits)
./wenafpv --verify-props

# Benchmark the overlay's GPU cost (batched GL timer query, N draws per run)
./wenafpv --bench-props 300

# Build from scratch and install to /usr
make clean && sudo make install
```

---

## Testing

```bash
make test
# Runs 48 physics tests covering:
# - Rate curves, hover throttle, climb rate, roll/pitch/yaw response
# - Top speed (5" and 3.5" profiles)
# - Battery sag, drain, low-voltage cutoff (1S/2S/4S/6S)
# - Free-fall terminal velocity (75mm, 85mm)
# - Landing assist (grounded settle, no mid-air leveling, crash protection)
# - Wind (disabled=zero, bearing, profile scaling, reset restarts phase)
# - Config load/save round-trip with all video/wind/shadow/landing settings
# - No ground tunnelling at high speed
```

All 48 tests pass.

---

## Video Signal System (video.hpp)

Simulates authentic analog FPV interference:
- **Distance loss** — Grace zone 30m, quadratic falloff to 400m
- **Occlusion** — Raycast drone→pilot, counts blockers + thickness
- **Close-wall multipath** — <4m proximity, burst static <1.5m
- **Low battery** — <3.6V/cell adds interference
- **High speed/aggression** — >25m/s or >10 rad/s
- **Dynamic effects:** Rolling sync bar, chroma glitch, horizontal tear, frame freeze/dropout, NTSC dot-crawl, sync loss waves, color burst rainbow bands, multipath ghost image

---

## Notes for Future Work

- No motor sound yet (audio synthesis params defined in sim.hpp but not hooked to output)
- No turtle mode, angle/horizon modes, race tracks, replays
- Physics remains an approximation until tuned against measured motor/prop/airframe data
- Level file markers are parsed but not used (training features removed)
- **Known issue:** Texture randomization causes z-fighting on coplanar surfaces (detail props on floors/walls, overlapping boxes from generator). Needs: offset detail geometry by 1-2cm along normals, or use polygon offset for detail geometry, or implement hidden face culling.
- The abandoned base map uses configurable seed (`abandoned_base_seed` in config.ini, default `0xDEADBEEF`)

---

## Resolved Debugging Sessions

### Flickering Investigation — RESOLVED (2026-10-25, Session 3)
**Problem:** Constant flickering in GPU-side texture randomization (`hash21()` in WORLD_FS shader) using large world coordinates (±1500).

**Root Cause Found:** The reported "flickering" was the **intentional analog video effects** (enabled by default with time-varying noise, rolling bar, chromatic aberration, etc.). Scene rendering itself was fully deterministic.

**Potential Secondary Issue (Fixed Preemptively):** `hash21()` multiplied large integer coordinates by float constants and took `fract()`, risking precision loss at high magnitudes.

**Fix Applied (WORLD_FS shader):**
```glsl
// Stable hash for large integer coordinates (from floor())
float hash21(vec2 p) {
    vec3 p3 = fract(vec3(p.x, p.y, p.x + p.y) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
```

**Verification:**
- All 48 physics tests pass
- Scene rendering: 0% frame difference with static camera, video disabled
- Video effects unchanged: 68% frame difference with video enabled (expected)
- Draw call audit: ~5-7 draw calls, single world mesh draw call preserved

---

### Ground Layer Z-Fighting Fix — RESOLVED (2026-10-25, Session 4, v1.1.5_ALPHA)

**Problem:** Abandoned Base map had **30 separate ground layers** with top faces at exactly Y=0, causing massive z-fighting (overlap areas up to 41,000 m²).

**Fix Applied (src/world.hpp):** Consolidated to **single 100×100m patch system** covering full 300×300m area with material variation by region. Removed 4 overlapping ground layers. Detail props retain 0.01f (1cm) vertical offset.

**Verification:**
| Metric | Before | After |
|--------|--------|-------|
| Ground layers at Y=0 | 30 | 1 (consolidated) |
| Max overlap area | 41,000 m² | 0 m² (ground plane) |
| Frame diff (static camera, video disabled) | N/A | ~0.03% |
| Physics regression | N/A | None (all 48 tests pass) |

---

### Props Lowering Debug Crash — RESOLVED (2026-10-26, v1.1.6_ALPHA)

**Problem:** `--debug-props-lowering` crashed with "double free or corruption" at `glGenFramebuffers` and segfault at `glReadPixels`.

**Root Cause:** Debug functions created framebuffers after initialization where GL driver state caused malloc corruption on `glGenFramebuffers`. Pixel readback crashed due to missing synchronization and pixel packing alignment.

**Fix Applied (src/main.cpp):**
1. Create debug framebuffers during main initialization (where GL works reliably)
2. Pass pre-created handles to debug functions, resize attachments instead of creating new
3. Add `glFinish()` + `glPixelStorei(GL_PACK_ALIGNMENT, 1)` before `glReadPixels`

**Result:** Verification runs successfully, produces complete output tables and ASCII dumps.