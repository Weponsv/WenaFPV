# WenaFPV

> A low-end friendly 5" FPV freestyle drone simulator with focus on physics fidelity over graphics.

![WenaFPV](https://img.shields.io/badge/status-Alpha-orange)
![License](https://img.shields.io/badge/license-MIT-blue)
![Platform](https://img.shields.io/badge/platform-Linux-lightgrey)
![Language](https://img.shields.io/badge/C%2B%2B-17-blue)

---

## What is WenaFPV?

WenaFPV is a 5-inch FPV freestyle drone simulator built for **low-end hardware** (ThinkPad X260 with Intel HD 520 iGPU) that prioritizes **physics accuracy over visual fidelity**.

### Design Philosophy: Worse Graphics, Better Physics

| Aspect | Approach |
|--------|----------|
| **Rendering** | Single static mesh, one draw call, cheap shaders, no instancing |
| **Physics** | 500 Hz fixed timestep, 4 lagged motors, gyroscopic coupling, body-axis drag, battery sag |
| **Target FPS** | Monitor refresh rate on Intel HD 520 (60-75 FPS @ 1280×720) |

**Why?** Most FPV sims either look great but run poorly on integrated graphics, or run well but feel "arcade-y". WenaFPV aims for the sweet spot: buttery smooth on potato hardware, with physics that actually resemble a real quad.

---

## Features

### Physics (Headless, Unit Tested)
- **500 Hz simulation step** — rates, motor lag, thrust mixing, quaternion integration
- **Betaflight "Actual" rates** — centre sensitivity / max rate / expo per axis
- **Body-axis aerodynamic drag** — fixes spurious lift at high pitch angles
- **Battery model** — voltage sag, capacity drain, 3.0 V/cell cutoff
- **Collision system** — sphere vs box, swept sphere vs thin cylinders (wires, rebar, pipes)
- **Landing assist** — contact-gated, gentle touchdowns only
- **Wind** — smooth gusts + turbulence, configurable per session
- **48 headless physics tests** — rates, hover, battery, collisions, wind, terminal velocity

### Maps (5 built-in, selectable via F3/F4)
| Map | Description |
|-----|-------------|
| **Freestyle Park** | Gates, gantry crane, dive towers (6 m gap), 3-floor bando frame, containers, slalom, trees, asphalt loop |
| **Gate Training** | Progressive 8-gate line with varying sizes |
| **Slalom Training** | S-bend gates with marker poles |
| **3.5" Tech Park** | Compact 100×100 m arena: channels, rails, willows, playground, skate bowl, pipe rack, scaffolds |
| **Abandoned Base** | Ruined industrial/military base (~120×120 m) with 5 zones: Speed, Aim, Reaction, Notice, Outdoor |

### Analog Video Effects (NTSC-style)
Distance loss, occlusion, multipath, low-battery interference, high-speed breakup, rolling sync bar, chromatic aberration, horizontal tear, frame freeze/dropout, dot-crawl, sync loss, color burst loss, ghost image.

### Procedural Generation
- Deterministic map generation from seed: `wenafpv --generate SEED OUTPUT.level`
- Abandoned Base uses configurable seed (`abandoned_base_seed` in config, default `0xDEADBEEF`)

### Other
- Real-time shadows (configurable distance, fade, resolution, bias, stable snapping)
- Per-instance texture randomization + detail props (rocks, debris, tires, pallets, pipes, cones, weeds)
- Settings persisted to `~/.config/wenafpv/config.ini`
- Hot-plug joystick support

---

## Building (Arch Linux)

```bash
# Dependencies
sudo pacman -S --needed base-devel sdl2 mesa libglvnd

# Build
git clone https://github.com/weponsv/WenaFPV
cd WenaFPV
make            # builds ./wenafpv (Skylake-optimized)
# or: make OPT=  # generic build

# Run tests
make test       # 48 headless physics tests (rates, hover, battery, collisions, wind, terminal velocity, config persistence)

# Expected output: "ALL TESTS PASSED (0 failures)"

# Install system-wide (optional)
sudo make install  # installs to /usr/bin/wenafpv
```

### Build Options
| Variable | Default | Description |
|----------|---------|-------------|
| `OPT` | `-march=skylake -mtune=skylake` | Compiler optimization flags |
| `CXX` | `g++` | C++ compiler |
| `PREFIX` | `/usr` | Install prefix |

---

## Running

```bash
./wenafpv
```

### Command Line Options
```
wenafpv [--config FILE] [--list-joysticks] [--fonttest] [--generate [SEED] [OUTPUT]]
        [--freeze-test [FRAMES]] [--frame-diff-test [FRAMES]] [--draw-call-audit]
        [--ground-audit] [--color-census] [--disable-ground-layer N]
        [--map N] [--disable-video] [--dbg-disable-hash21] [--dbg-disable-grain]
        [--dbg-disable-scratch] [--dbg-disable-detail] [--dbg-disable-shadows]
```

---

## Controls

| Key | Action |
|-----|--------|
| **F1** | RC input visualizer (Mode 2 stick boxes) |
| **F2** | Force keyboard mode |
| **F3 / F4** | Previous / Next map |
| **F5** | Settings menu (Tab=tab, Arrows=edit, Enter/Space=select) |
| **F11** | Fullscreen toggle |
| **R** | Reset quad + battery |
| **ESC** | Quit (saves config) |
| **`[` `]` / PgDn/PgUp** | Camera tilt ±1° |
| **`-` `=`** | Horizontal FOV ±5° |

### Keyboard Fallback (no radio)
| Key | Action |
|-----|--------|
| **W / S** | Throttle |
| **A / D** | Yaw |
| **Arrows** | Pitch / Roll |
| **SPACE** | Arm |

---

## Radio / Joystick Support

**Primarily tested on RadioMaster Pocket (EdgeTX, USB HID Joystick mode).** Other HID joysticks *may* work but are not officially supported.

### RadioMaster Pocket Setup
1. On radio: **SYS → General → USB Mode → Joystick (HID)**
2. Plug in via USB-C
3. Run `./wenafpv --list-joysticks` to verify detection
4. Default mapping (EdgeTX AETR order):
   - CH1: Roll
   - CH2: Pitch
   - CH3: Throttle
   - CH4: Yaw
   - **CH5: Arm switch** (high = armed)
   - Optional: set `axis_reset = 5` for reset on CH6
5. Press **F1** for live axis monitor — bars should move RIGHT when pushing stick right/forward/up

> **Note:** Pitch is inverted by default (EdgeTX reports stick-forward as negative). Flip `invert_pitch` in F5 → Radio if needed. Keep radio mixes at ±100 travel with no weights/expo — do rates in the sim.

---

## Configuration

Settings are saved to `~/.config/wenafpv/config.ini` on first run and updated via **F5 → Save** (also saved on exit).

### Settings Tabs (F5 Menu)
- **QUALITY** — Low/Medium/High presets, detail, fog, visibility
- **DISPLAY** — Resolution, vsync, MSAA, fullscreen
- **CAMERA** — Tilt, FOV
- **RADIO** — Axis mapping, inversion, deadband, arm threshold, joystick filter
- **RATES** — Betaflight Actual rates per axis (centre / max / expo)
- **DRONE** — Size selector (75mm, 85mm, 3", 3.5", 5", Custom), physics profile
- **BATTERY** — Cells, capacity, pack resistance
- **VIDEO** — 14 analog effect toggles + 7 intensity knobs (0..2)
- **LANDING** — Assist level, ground damping, snap threshold
- **WIND** — Enabled, strength (0-100 → 0-18 m/s), direction, gust, turbulence, HUD
- **SHADOW** — Enabled, distance, fade, altitude, resolution, bias, filtering, stable snapping, debug HUD

---

## Project Structure

```
WenaFPV/
├── src/
│   ├── main.cpp      # SDL2/OpenGL renderer, radio input, OSD, RC visualizer
│   ├── sim.hpp       # Physics core (headless, unit tested)
│   ├── world.hpp     # Level format, map builders
│   ├── config.hpp    # INI load/save, 50+ settings
│   └── video.hpp     # Analog video signal system
├── tests/
│   └── physics_test.cpp  # 48 headless physics tests
├── Makefile
├── README.md
├── UPDATES.md
├── RESUME_FOR_AI.md
└── wenafpv           # Built executable (after make)
```

---

## License

MIT License — see [LICENSE](LICENSE) for details.

---

## Credits

- **Core physics & architecture** — Developed with assistance from **Claude** (Anthropic)
- **Additional features & fixes** — **Nemotron 3 Ultra 550B A55B** (NVIDIA)
- Target hardware: ThinkPad X260 (i7-6500U, Intel HD Graphics 520)
- Controller: RadioMaster Pocket (EdgeTX)

---

## Roadmap / Known Limitations

- [ ] Motor sound synthesis (params defined, not hooked to audio output)
- [ ] Turtle mode, Angle/Horizon modes
- [ ] Race tracks, lap timing, replays
- [ ] Physics tuning against real motor/prop/airframe data
- [ ] Texture randomization z-fighting on coplanar surfaces (detail props on floors/walls)
- [ ] Windows/macOS build instructions (currently Linux-only)

---

## Repository

**GitHub:** https://github.com/weponsv/WenaFPV

Issues, PRs, and feedback welcome!