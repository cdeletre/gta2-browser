# GTA2 in the browser: benchmark report

**Grand Theft Auto 2 (DMA Design, 1999), fully playable in a web browser, with modern
enhancements the original never had.** The port was built by Claude (Anthropic, Claude
Opus 5.5 in Claude Code), working with the project owner, as an AI-capability benchmark.

The repository history covers 12–13 October 2026: the first commit at 21:14 on 12
October, the last listed here at 15:49 on 13 October. The work went from a
research workspace to a browser build with sound, saves, three districts, a map
system and a modern camera.

## Results at a glance

| | |
|---|---|
| **Playable** | All three districts (Downtown, Residential, Industrial), menus, walking, driving, shooting, missions, saves |
| **Browsers** | Chromium and Firefox, confirmed on macOS. The owner played 15+ minutes, including missions |
| **Download size** | The game engine is 1.1 MB of WebAssembly (**406 KB gzipped**); the whole port is about 1.5 MB |
| **Frame rate** | **Smooth at the display's refresh rate** (120 fps measured on a 120 Hz screen), interpolated between the original's 30 updates per second; memory flat at 128 MB in every soak |
| **Stability** | No errors in any soak: 180 s on foot, 120 s widescreen driving, 30 s in each of Residential and Industrial |
| **Fidelity** | The game logic is the original's, decompiled C++ compiled to WebAssembly, not a re-implementation |
| **Game data** | None shipped. Players use their own GTA2 files, which are verified and kept in the browser |

## How it works

- **The engine** is the community's matching decompilation of GTA2 ([`gta2_re`](https://github.com/CriminalRETeam/gta2_re)):
  108 C++ source files compiled to WebAssembly with Emscripten.
- **14 small patches** (+928/−144 lines across 47 files, all under `GTA2_PORT`) make
  it build and run standalone in a browser.
- **The port's own layers** (3,753 lines of C++, about 2,700 lines of JavaScript and
  HTML) replace what the original got from Windows:
  - a WebGL2 renderer for the original's Direct3D "gbh" interface, with 62 functions,
    each checked against the game's own function types;
  - Web Audio for the Miles sound system, including the IMA-ADPCM radio and speech;
  - keyboard input for DirectInput;
  - IndexedDB for files and saves;
  - a stand-in registry and message loop.
- **Reproducible build.** The upstream code stays untouched, and the patch series is
  re-applied on every clean build (`scripts/build_port.py --prepare`).

## Engineering problems solved

These were found and fixed with evidence, not guessed:

- **Why the upstream standalone build stalls on entering gameplay.** Six data tables
  are built only on a debug path. The original's C runtime built them before `WinMain`.
  The port builds them at start-up.
- **Initialised data missing from the decompilation.** Four tables that the original
  executable carried were zero: the key bindings, the stereo pan curve, the AI horn
  rhythms and the articulated-truck hitch offsets.
  - **The values were recovered from the original 2004 executable**, which is packed
    on disk, by running it under Wine and dumping its unpacked memory with a purpose-built
    tool. Each table was located through the matching function in the older build.
  - Sound panning went from "everything centred" to 20 distinct positions while
    driving.
- **WebAssembly versus 1999 C++:**
  - functions that fall off the end, which MSVC allowed;
  - indirect calls with mismatched signatures;
  - MSVC bitfield layout and 2-byte `wchar_t`;
  - inline assembly, translated to C++.

  All of the decompilation's struct size checks pass on wasm32.
- **Graphics:** the game refills shared texture objects per glyph, so textures are
  content-addressed. Style pages have a 256-byte pitch; palettes are stored as columns.

## Enhancements over the original

### Widescreen, done properly
- **Any aspect ratio, 16:9 by default.** You see more of the world at the sides with
  the same vertical view; the original stretched.
- **No pop-in at the widened edges.** Measured: at 16:9 pedestrians would spawn
  *inside* the visible area.
  - An "activation margin" widens the game's own spawn and despawn rectangle to match
    what is drawn.
  - It is a recorded, switchable deviation.
- **The HUD is anchored to the screen edges:** cash, lives and weapon on the right,
  respect bars on the left, messages centred.
- **Live resizing and full screen**, at any window size.

### Smooth at any refresh rate
The original draws 30 frames a second, so on a 120 Hz screen each frame is held for four
refreshes and scrolling judders. The port interpolates sprites and the camera between
the game's 30 updates per second and draws at the display's rate.

- Simulation state is restored byte for byte after every in-between draw, including
  the collision boxes the draw recomputes.
- Measured: 120 frames per second, with frame-to-frame speed variation down from 0.47
  to 0.18.

### A fix for GTA2's zoom pumping
The original sets the camera height from the car's raw speed, so every bump, corner or
brake pumps the zoom in and out. The new **Modern camera** filters only the drawn
height; the simulation is untouched. On the same scripted drive, both cameras recorded
frame by frame:

| | Original | Modern |
|---|---|---|
| Zoom direction changes per minute | 32.3 | 10.2 |
| Peak zoom speed (blocks/s) | 6.04 | 2.74 |
| Height travel per minute (blocks) | 59.9 | 34.4 |
| Zoom-ins triggered by a brief (< 1.8 s) slowdown | many | **none** |

Measured with the final tuning on a 90 s scripted drive. The first tuning measured
25.9 against 10.7 reversals per minute.

The original camera is one click away (Camera: Classic). There is also a **view
distance** setting (−/= keys) to see more of the city, without pop-in.

### A real map, built from the game itself
- **M opens a full-screen map** of the district, drawn in the browser from the
  player's own map and style files. The game pauses while it is open.
- **The markers are the game's own live data**, not a hand-made database: kill
  frenzies, weapons, power-ups, tokens, garages (with their services), car crushers and
  save points. Save points are found by reading the mission script's triggers.
- **Gang jobs as waypoints.** The jobs you can actually take now come from the HUD's
  own arrows, filtered by your respect and mission state. They are shown with each
  gang's logo.
- **Discovery mode.** The map stays dark until you have seen an area, and items appear
  only once found. F shows everything.
- **A GTA-style radar** with a black ring and N marker. The original gang arrows (the
  game's own sprites) and your waypoint sit on its rim.

### Built for the web
- **Saves in the browser:** atomic, with the previous version kept, plus export and
  import.
- **Game files kept after the first pick**, and loaded automatically on the owner's
  machine.
- **One-click installer download (private testing).** The browser fetches Rockstar's
  2004 Rockstar Classics installer from the Internet Archive and verifies it by SHA-1. It is
  unpacked in a worker by `unshield` compiled to WebAssembly.
  - **All 332 unpacked files matched the reference installation byte for byte.**
  - The port's host never handles game files.
- **WASD/JKL controls** (the original Ctrl/Alt bindings clash with macOS), a mute
  button and an FPS readout.

## Verification

- **Gameplay mechanics checked with test hooks:**
  - respect gates the medium and hard jobs at the game's own thresholds (40 and 80);
  - starting a job switches the map to the mission target;
  - the articulated truck's trailer stays hitched 1.2 blocks behind and articulates in
    turns, which depends on the recovered hitch offsets.

- **Gameplay probes in a real browser:** menus, all three districts, car entry and
  driving, shooting, church save, reload and resume, radio, map and radar.
- **Soaks:** randomised driving and walking with frame counts, memory and error
  capture.
- **Reference runtime:** the original 9.6F executable runs under Wine on the same Mac
  for side-by-side checks.
- **Acceptance test for the camera:** `scripts/port/camera_test.js`.
- **Every claim in this report is backed by a measurement or test** recorded in
  [docs/browser-build-status.md](browser-build-status.md).

## Not done yet

- **Co-op:** single player was the first goal. The code keeps per-player state where it
  matters, ready for networking.
- **Platforms:** Safari, Windows, Linux and mobile are untested; there are no touch
  controls.
- **Missing pieces:** the attract-mode demo and Bink movies (skipped).
