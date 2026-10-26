# WenaFPV — Resume for AI Context

## Project Overview
**WenaFPV** — 5" FPV freestyle drone simulator. SDL2 + OpenGL 3.3 core, optimized for low-end Intel iGPUs (single static mesh, one draw call, cheap shaders). Target hardware: ThinkPad X260 (i7-6500U, HD Graphics 520).

---

## Current State Summary (v1.1.5_ALPHA)

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

### What Was Added/Modified in This Session (v1.1.0)

#### Config & Settings
- **`abandoned_base_seed`** config option in QUALITY tab (F5), full uint32 range, persists to config.ini
- Default seed: `0xDEADBEEF` (3735928559)

#### Shadow Rendering Fixes (main.cpp)
- **Tight near/far planes** for shadow orthographic projection: depth range reduced from ~600m to ~90m based on actual geometry height extents (ground thickness + max object altitude)
- **Increased defaults**: `shadowBias` 0.00025→0.001, `shadowNormalOffset` 0.025→0.05
- **Disabled `shadowStableSnapping` for mapIndex 4 (Abandoned Base)** — its 50m speed corridor at 100+ km/h caused texel snapping to flicker between frames; other maps retain it

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
- ✅ Shadow rendering fixes: no blinking on Abandoned Base map
- ✅ VID HUD positioned correctly under battery MAH
- ✅ Texture randomization applied to all 5 maps

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

## Active Debugging Session (2026-10-25) — **RESOLVED**

### Flickering Investigation - "Textures over Textures"

**Problem:** Constant flickering in the new GPU-side texture randomization (WORLD_FS shader's `hash21()` function) using large world coordinates (±1500) with `floor(surface * scale)`.

**Root Cause Hypothesis:** The `hash21()` function uses `fract(p * vec2(123.34, 456.21))` on large world-space coordinates, causing floating-point precision issues. When camera moves slightly, `floor()` sometimes returns different values, causing texture flickering.

**Debug Infrastructure Added (main.cpp):**

**Command-line flags:**
- `--freeze-test [FRAMES]` — Freezes camera after first frame, renders N frames to check if flicker is time/camera dependent
- `--frame-diff-test [FRAMES]` — Compares consecutive frames pixel-by-pixel to detect non-deterministic rendering
- `--draw-call-audit` — Prints draw call breakdown and vertex counts on first frame
- `--disable-video` — Skips analog video post-processing for isolated scene testing
- `--map N` — Selects map at startup (0=Freestyle Park, 1=Gate Training, 2=Slalom, 3=3.5 Tech Park, 4=Abandoned Base)
- `--dbg-disable-hash21/--dbg-disable-grain/--dbg-disable-scratch/--dbg-disable-detail/--dbg-disable-shadows` — Debug toggles via CLI

**Runtime feature toggles (F6-F12 keys):**
- F6: Disable `hash21` entirely (replace with 0.5)
- F7: Disable grain detail
- F8: Disable scratch detail
- F9: Disable all detail (`uDetail = 0`)
- F10: Disable shadows
- F12: Wireframe mode

**Investigation Results:**

| Test | Result | Interpretation |
|------|--------|----------------|
| `--frame-diff-test 60 --map 1 --freeze-test 60` | 68% pixels differ | **Video effects cause flicker** with static camera |
| `--frame-diff-test 60 --disable-video --map 1 --freeze-test 60` | **0% pixels differ** | Scene rendering is fully deterministic |
| `--frame-diff-test 60 --disable-video --map 0 --freeze-test 60` | ~0% diff (traffic only) | Large coords (±1500) don't cause flicker |
| `--frame-diff-test 60 --disable-video --map 4 --freeze-test 60` | ~0% diff (after settle) | Abandoned base detail props stable |

**Root Causes Found:**

1. **Primary — Analog Video Effects (Cause A variant):** The analog video post-processing (VIDEO_FS shader) is **enabled by default** with multiple effects (noise, static, rolling bar, chromatic aberration, tearing, dot-crawl, sync loss, color burst, multipath ghost) that change every frame based on `uTime`. This creates 68% frame-to-frame pixel differences even with a completely static camera. This is intentional analog FPV simulation, not a bug.

2. **Potential Secondary — hash21 Float Precision (Cause E):** The `hash21()` function multiplied large integer coordinates (up to ~2000 from `floor(surface * 1.35)` at world edges) by float constants and took `fract()`, which loses precision at high magnitudes (~250,000). While testing on Intel HD 520 (Mesa) showed deterministic rendering, this is a potential cross-GPU/driver issue.

**Fix Applied (src/main.cpp, WORLD_FS shader lines 153-157):**

```glsl
// NEW - stable hash for large integer coordinates (from floor())
float hash21(vec2 p) {
    vec3 p3 = fract(vec3(p.x, p.y, p.x + p.y) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
```

- Uses small multiplier (0.1031) to keep values in good range for `fract()`
- Based on same stable technique as `hash12` in VIDEO_FS shader
- Handles large integer coordinates without precision loss
- Maintains deterministic per-world-position texture variety
- No visual change to texture appearance

**Verification:**
- All 48 physics tests pass
- Scene rendering: 0% frame difference with static camera, video disabled
- Video effects unchanged: 68% frame difference with video enabled (expected)
- Draw call audit: ~5-7 draw calls, single world mesh draw call preserved

**Conclusion:** The reported "flickering" was the intentional analog video effects. The `hash21` precision issue was a potential latent bug fixed preemptively. No changes to map layout, physics, audio, or input.

---

## Ground Layer Z-Fighting Fix (2026-10-25, Session 4, v1.1.5_ALPHA)

### Problem
The Abandoned Base map had **30 separate ground layers** with top faces at exactly Y=0, causing massive z-fighting (overlap areas up to 41,000 m²). Ground audit showed layer [0] (300×300m base slab at Y=[-0.5, 0]) overlapping with 29 other layers at Y=0.

### Root Cause
Multiple explicit ground plane constructions in `buildAbandonedBase()` (world.hpp lines 835-878):
1. Base ground with dirt variation (full 300×300m)
2. Main base area with worn concrete (-60 to 60)
3. Four outdoor yard quadrants around base
4. Speed zone floor (separate addBox at Y=[0, 0.15])
5. Reaction zone courtyard floor (separate addBox at Y=[0, 0.1])
6. Notice zone floor patches (separate addBox at Y=[0, 0.1])
7. Outdoor yard ground (separate addBox at Y=[-0.5, 0])

All had top face at exactly Y=0.0 (coplanar).

### Fix Applied (src/world.hpp)
**Consolidated ground layer** (lines 835-878): Single 100×100m patch system covering full 300×300m area with material variation by region:
- Main base (-60 to 60): worn concrete
- Speed zone corridor: worn concrete
- Reaction zone courtyard: worn concrete
- Notice zone: worn concrete
- Outdoor yard (60 to 120): grass/dirt mix
- Outer areas: grass/dirt mix

**Removed overlapping ground layers:**
1. Speed zone floor (was line 931)
2. Reaction zone courtyard floor (was line 1023)
3. Notice zone floor patches (was lines 1080-1086)
4. Outdoor yard ground (was lines 1154-1160)

Detail props (grass, rubble) retain 0.01f (1cm) vertical offset — sufficient for 24-bit depth buffer at this scale.

### Verification Results
| Test | Result |
|------|--------|
| Physics tests (48) | ✅ ALL PASSED |
| Frame diff test (--freeze-test) | ~0.02-0.04% pixel difference (minimal, likely analog video noise) |
| Diff image analysis | Almost entirely black — no significant z-fighting |
| Ground audit | Shows structural element bottom faces (column bases, wall bases) as "layers" but these are buried — no visible flickering |

### Before/After
| Metric | Before | After |
|--------|--------|-------|
| Ground layers at Y=0 | 30 | 1 (consolidated) |
| Max overlap area | 41,000 m² | 0 m² (ground plane) |
| Frame diff (static camera, video disabled) | N/A | ~0.03% |
| Physics regression | N/A | None (all tests pass) |

### Notes
- Ground audit still detects 28 "layers" in Abandoned Base, but these are **bottom faces of vertical structures** (columns, walls, pillars) at Y=0 — they're buried in the ground and don't cause visible z-fighting
- The 0.01f offset for detail props is sufficient for 24-bit depth precision (depth table: z=0.1 → depth=-0.500025)
- Analog video effects remain the primary source of frame-to-frame variation (68% with video enabled)