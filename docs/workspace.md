# GTA2 Project

Workspace for a project built around **Grand Theft Auto 2** (DMA Design / Rockstar
North, 1999) using the **official Rockstar Classics release for Windows**
(December 2004).

## Layout

| Path | Purpose |
| --- | --- |
| `downloads/` | Original, untouched download artefacts (installer). |
| `game/` | Installed / extracted game files. |
| `docs/` | Project notes, references and research material. |

## Tooling

- **REA (Reverse Engineer Anything)** — project-scoped addon for analysing the
  game binaries with coding agents. See [rea-addon.md](rea-addon.md).
  Installed locally to this workspace only; nothing global.

## Browser-port audit

See [the pinned build-and-gap audit](gta2-re-build-audit-2026-10-12.md)
for tested reuse, browser/co-op gaps and revised token/cost scenarios. The local
Emscripten 6.0.12 SDK, upstream checkouts and probe outputs live in ignored
`research/`. Reproduce the asset-free math probe with
`python3 scripts/run_gta2_math_probe.py`; this is not a playable game build.

The [orchestrator execution specification](orchestration/gta2-browser-spec.md)
defines the product, worker routing, acceptance gates and task dependencies.
The [binding implementation decisions](orchestration/decisions.md) settle
the stack, timing, rendering, asset import, saves and cooperative networking;
workers implement these choices instead of researching alternatives.
Use the [launch prompt](orchestration/launch-prompt.md) when ready to begin.
Validate the static plan with `python3 scripts/validate_gta2_plan.py`.

## Provenance of the game binary

- **Source:** Internet Archive item [`gta2_rockstar-classics`](https://archive.org/details/gta2_rockstar-classics)
  — uploaded as the original distribution downloaded from Rockstar's *Rockstar Classics* page
  (archived copy of that page: [web.archive.org](https://web.archive.org/web/20120119105457/http://www.rockstargames.com/classics/?id=3)).
- **File:** `downloads/GTA2INSTALLER.exe`
- **Size:** 362,331,961 bytes
- **MD5:** `764848b25aed87b69da472f18c19580d`
- **SHA-1:** `8c10c0d20f15912a9450b6fbe028f31928b29893`

Verify with:

```sh
md5 downloads/GTA2INSTALLER.exe
shasum -a 1 downloads/GTA2INSTALLER.exe
```

## Running it

The installer is a Windows executable (it targets Windows 9x/XP-era systems,
updated by Rockstar for Windows XP with DirectX 8.1+). On modern systems:

- **Windows VM or physical PC** — run the installer directly, then run
  *AutoDetect* once before first launch to pick a rendering engine.
- **macOS/Linux** — run under Wine/CrossOver; 1999-era DirectX games usually
  need DirectDraw/Direct3D emulation (Wine's `dxvk`/`wined3d`) and often run
  best with the community [GTA2 patch v11.44](https://gtamp.com/gta2/) and
  widescreen fixes.

## Legal note

GTA2 was distributed by Rockstar Games under the Rockstar Classics label,
and it remains copyrighted by Rockstar Games / Take-Two
Interactive. This release may not be redistributed; keep this copy for
personal/project use only.

## Publishing

The public repository is **[M1sterMo/gta2-browser](https://github.com/M1sterMo/gta2-browser)**.
It was published privately on 13 October 2026, with the patches and the two README
screenshots included (owner's decisions).

It is a clean export, not this repository's history. `scripts/publish_export.py
--screenshots` builds `build/publish/gta2-browser`:
- README, LICENSE (MIT for our own code), `docs/` (without `evidence/` and
  `orchestration/runs/`), `port/`, `scripts/`, `web/`;
- local paths scrubbed, no game files, one fresh commit.

To update the public copy, re-export, then commit and push from that folder (or
force-push a fresh export).
