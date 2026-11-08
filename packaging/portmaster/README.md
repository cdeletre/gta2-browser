## Notes

Thanks to [DMA Design and Rockstar Games](https://www.rockstargames.com/) for creating Grand Theft Auto 2,
and for releasing it as freeware. This port would not exist without the
[GTA2 reverse engineering project (gta2_re)](https://github.com/CriminalRETeam/gta2_re) by the
CriminalRETeam: a decompilation of the original game, function by function. It is built here as a
native Linux binary with SDL2 and OpenGL ES 2.0, from the code of the
[GTA2 browser port](https://github.com/M1sterMo/gta2). Source of this port: <FORK_URL>.

**Game files:** you need the freeware PC installer of GTA2. Copy `GTA2INSTALLER.exe` into
`ports/gta2/gamedata/`. On the first launch the game files are extracted from it (this takes a while),
then the installer is deleted.

**Saves and settings:** the game's profiles, church saves and high scores are in `ports/gta2/saves/`.
The settings are in `ports/gta2/conf/`: `registry.txt` holds the game's own options, and
`controls.ini` is the controller layout (it is written with comments on the first launch).

**What differs from the PC game:**
- The view fills the screen whatever its shape. On a 4:3 screen it is the original 640x480 view,
  scaled. A wider screen (16:9) shows more of the city on the sides, and a taller one (1:1, as on the
  RG CubeXX) shows more above and below. Menus stay 4:3, with black bars.
- The intro movies are not played.
- Options (main menu, or Select during the pause in the city) opens a simple settings screen instead of the original's separate GTA2
  Manager program: music and radio volume, sound effects volume, text speed, exploding scores,
  lighting, and the A/B layout (`face_buttons`).
- Quitting: Start opens the game's pause and quit prompt, then A twice quits (the second press
  confirms "ARE YOU SURE?"), B or Start carries on.

**If A and B (or X and Y) are swapped,** set `face_buttons = nintendo` or `face_buttons = xbox` in
`conf/controls.ini`. The default, `auto`, follows PortMaster's controller mapping.

**Logs:** the game writes its log to `ports/gta2/log.txt`, with a status line every 60 seconds.
To help with a bug report, add `export GTA2_LOG_INPUT=1` to the launch script (it logs the controller
mapping and every button press) and send the log. After a crash, the log ends with a crash report: please include it.

Tested on: MANGMI Air X (ROCKNIX), R36S (AmberELEC), RG CubeXX, RG34XX-H and RG40XX-H (muOS), RG353VS (dArkOS and
ROCKNIX), G350 (dArkOS), TrimUI Smart Pro S (REG Linux).

## Controls

| Button | Action |
|--|--|
| D-pad / Left stick | Walk, steer |
| R2 / L2 | Accelerate / Reverse (also forward / backward on foot) |
| A | Attack, shoot |
| B | Horn (in a car), taunt (on foot) |
| X | Jump, handbrake |
| Y | Enter / exit a vehicle |
| L1 / R1 | Previous / next weapon |
| L3 (hold) | Look around with the D-pad |
| R3 | Status |
| Start | Pause, quit prompt (then Select: Options) |
| Select | Next radio station (in a car) |
| Menus: A / B | Confirm / Back |

Every button can be changed in `conf/controls.ini`.

## Compile

The binary is built for aarch64 with clang, in a container based on the PortMaster build image
(on an ARM64 machine, or with emulation).

```shell
git clone <FORK_URL> gta2
cd gta2
git clone https://github.com/CriminalRETeam/gta2_re research/gta2_re
git -C research/gta2_re checkout 02de8be
docker build --platform=linux/arm64 -t gta2-pm-img docker/
docker run -d --name gta2-pm --platform=linux/arm64 -v "$PWD":/workspace gta2-pm-img
./scripts/build_native.sh --preset release
```

The port binary is `build/native/release/dist/gta2.aarch64`. The build script applies the engine
patches to a copy of `gta2_re` (pinned at commit `02de8be`); see `docs/native-status.md` for details.
