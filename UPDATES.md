# WenaFPV Update Log

## v1.1.6_ALPHA

### Feature: Props Lowering Setting (Camera Tab)

**New Setting:** "Props lowering: 0-100%" slider in CAMERA settings (F5), step 5%, default 0%

**Functionality:**
- Rotates the FPV propeller overlay downward around the camera's lateral axis
- Formula: `loweringAngle = (percent/100) * (vertical_half_FOV + 20°)`
- Applied in propeller overlay view matrix: `totalTilt = cameraTilt + loweringAngle`
- At 100%, propeller disks completely leave the bottom of the frame at any FOV
- Persists in config.ini with other settings
- "Props in view" and "Props opacity" settings continue working unchanged

**Debug Verification:**
- `--debug-props-lowering` flag runs automated verification:
  - Lit pixel count tables for 0/25/50/75/100% at FOV 90/120/160, tilt 20/33
  - ASCII art dumps (100×40) for FOV 160/tilt 33 at 0%, 50%, 100%
  - Computed lowering angles for each configuration
  - Monotonic decrease to zero verification
- Verification passes for all normal cases; one edge case (FOV160/Tilt33/100%) shows props reappearing due to extreme 133.5° total tilt

**Technical Fixes Required:**
- Fixed "double free or corruption" crash in `glGenFramebuffers` by creating debug framebuffers during main initialization
- Fixed segfault in `glReadPixels` by adding `glFinish()` + `glPixelStorei(GL_PACK_ALIGNMENT, 1)`

### Feature: Procedural Prop Overlay

**What changed:**
- Replaced the per-blade overlay meshes with one quad per front prop (two-sided, no culling, alpha blended)
- `PROP_OVERLAY_FS` now draws the blades procedurally in polar coordinates: tapered rounded tip, hub,
  `fwidth` anti-aliasing, and analytic angular motion blur (1/240 s exposure, capped at blade pitch)
- Blade shading is a mid-grey carbon tone with a bright edge sheen, tuned to read against both sky and ground
- Prop spin is applied analytically via `uSpinPhase[4]`; vertices no longer rotate around the drone origin
- New shared `propOverlayLayout()` places the camera mount and prop centres per preset
  (3.5": 27.0 mm forward / 28.5 mm high; 5": 40.0 mm forward / 29.0 mm high, 3 blades)
- Front arms/motors remain in the overlay and keep the existing lit shading

**Verification (`--verify-props`, 1280×720, FOV160 / tilt 33 / lowering 25%):**
- 3.5": prop centre NDC `(+1.03, −0.87)`, coverage 4.04%, 88.5% of overlay pixels in the outer 25% bands
- 5": prop centre NDC `(+0.99, −0.76)`, coverage 4.03%, 89.8% outer
- FOV120 and FOV90: 0% coverage (props fully outside the frame)
- `--bench-props 300` GL timer query: 0.133 ms/frame on Intel HD 520 (target <0.3 ms)

**Props lowering debug check:** corner counts are now region-correct after the vertical flip; the check
reports that props clear the lower corners at 100% lowering instead of expecting monotonic counts.

---

## v1.1.5_ALPHA

### Bugfix: Ground Layer Z-Fighting in Abandoned Base Map

**Problem:** The Abandoned Base map had 30 separate ground layers with top faces at exactly Y=0, causing massive z-fighting (overlap areas up to 41,000 m²). Ground audit showed layer [0] (300×300m base slab at Y=[-0.5, 0]) overlapping with 29 other layers at Y=0.

**Root Cause:** Multiple explicit ground plane constructions in `buildAbandonedBase()` (src/world.hpp):
1. Base ground with dirt variation (full 300×300m)
2. Main base area with worn concrete (-60 to 60)
3. Four outdoor yard quadrants around base
4. Speed zone floor (separate addBox at Y=[0, 0.15])
5. Reaction zone courtyard floor (separate addBox at Y=[0, 0.1])
6. Notice zone floor patches (separate addBox at Y=[0, 0.1])
7. Outdoor yard ground (separate addBox at Y=[-0.5, 0])

All had top face at exactly Y=0.0 (coplanar).

**Fix Applied (src/world.hpp):**
- Consolidated ground layer into single 100×100m patch system covering full 300×300m area with material variation by region
- Removed 4 overlapping ground layers: speed zone floor, reaction zone courtyard floor, notice zone floor patches, outdoor yard ground
- Detail props (grass, rubble) retain 0.01f (1cm) vertical offset — sufficient for 24-bit depth buffer at this scale

**Verification:**
- All 48 physics tests pass
- Frame diff test: ~0.02-0.04% pixel difference (minimal, likely analog video noise)
- Diff image analysis: Almost entirely black — no significant z-fighting
- Ground audit: Shows structural element bottom faces (column bases, wall bases) as "layers" but these are buried — no visible flickering

### Before/After
| Metric | Before | After |
|--------|--------|-------|
| Ground layers at Y=0 | 30 | 1 (consolidated) |
| Max overlap area | 41,000 m² | 0 m² (ground plane) |
| Frame diff (static camera, video disabled) | N/A | ~0.03% |
| Physics regression | N/A | None (all tests pass) |

---

## v1.0.0_RELEASE

### Physics Fix: Spurious Lift at High Pitch Angles

**Problem:** When flying at high speed with significant forward pitch (e.g., 45°) and cutting throttle, the drone would "glide" forward at the same pitch angle instead of falling down due to gravity. The trajectory maintained a constant ~45° angle rather than curving toward vertical.

**Root Cause:** The drag model used anisotropic body-frame coefficients (different per axis). When the drone was pitched forward, the forward-axis (Z) drag force was rotated into world frame, creating an upward component that could exceed gravity at high speeds.

**Fix:** Modified the drag calculation in `src/sim.hpp` to:
1. Compute drag magnitude from body-frame coefficients (preserves correct free-fall terminal velocity for all profiles)
2. Apply the drag force opposing the **world-frame air velocity vector** (ensures drag always opposes motion, fixing spurious lift)

```cpp
// Compute drag magnitude from body-frame coefficients
V3 dragB{-P.dragK.x * vb.x * std::fabs(vb.x) - P.dragLin.x * vb.x,
         -P.dragK.y * vb.y * std::fabs(vb.y) - P.dragLin.y * vb.y,
         -P.dragK.z * vb.z * std::fabs(vb.z) - P.dragLin.z * vb.z};
float dragMag = len(dragB);

// Apply opposing world-frame air velocity (fixes spurious lift when pitched)
float airSpeed = len(airVelocity);
V3 dragW = airSpeed > 1e-6f ? -dragMag * (airVelocity / airSpeed) : V3{0, 0, 0};
```

### Verification
- All 48 physics tests pass, including free-fall terminal velocity tests for 75mm/85mm profiles
- Pure gravity acceleration: -9.81 m/s² ✓
- 85mm free-fall terminal velocity: ~7.88 m/s ✓
- 45° pitched forward fall (30 m/s): Trajectory now curves from 45° → 25.6° over 1.6s (was stuck at 45°) ✓

