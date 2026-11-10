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
- Options opens a simple settings screen of the port, instead of the original's separate GTA2
  Manager program (see below).
- Select changes the car radio station (F1 on a PC keyboard).

**Options:** from the main menu (Options), or during a game: press Start (the quit prompt), then
Select. The game is paused while the screen is open. Up/down choose a line, left/right change it,
B or Start close it (in a game, B again resumes play). Settings:
- Music and radio volume, sound effects volume (applied at once).
- Text speed (from the next game), exploding scores, lighting.
- Face buttons: `auto`, `nintendo` or `xbox` (the A/B and X/Y layout, see below).

**Quitting:** press Start (the quit prompt), then A twice (the second press confirms "ARE YOU
SURE?"). B or Start resumes play. The quit prompt alone does not pause the game.

**If A and B (or X and Y) are swapped,** change Face buttons in Options to `nintendo` or `xbox`
(it is saved as `face_buttons` in `conf/controls.ini`). The default, `auto`, follows PortMaster's
controller mapping; on some systems (TrimUI Smart Pro S with REG Linux) `nintendo` is needed.

**Logs:** the game writes its log to `ports/gta2/log.txt`, with a status line every 60 seconds.
To help with a bug report, add `export GTA2_LOG_INPUT=1` to the launch script (it logs the controller
mapping and every button press) and send the log. After a crash, the log ends with a crash report: please include it.

Tested on: MANGMI Air X (ROCKNIX), R36S (AmberELEC), RG CubeXX, RG34XX-H and RG40XX-H (muOS), RG353VS (dArkOS and
ROCKNIX), G350 (dArkOS), TrimUI Smart Pro S (REG Linux).

## Controls

| Button | Action |
|--|--|
| D-pad / Left stick | Walk, steer (up / down: forward / backward) |
| A / B | Accelerate / Reverse (also forward / backward on foot) |
| R2 | Attack, shoot |
| X | Horn (in a car), taunt (on foot) |
| R1 | Jump, handbrake |
| Y | Enter / exit a vehicle |
| L1 / L2 | Next / previous weapon |
| L3 (hold) | Look around with the D-pad |
| R3 | Status |
| Select | Next radio station (in a car) |
| Start | Quit prompt: then A, A to quit, B to resume, Select for Options |
| Menus: A / B | Confirm / Back |

The layout follows the PlayStation version (cross = A, square = B, triangle = Y, circle = X). Every button
can be changed in `conf/controls.ini`.

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
