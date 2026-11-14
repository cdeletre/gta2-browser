# Component provenance and reuse permissions

Task: T01 (gate G0). Status of the distribution question: **unresolved**.

Date: 12 October 2026 (Australia/Sydney). Binding decisions: **D02** (language
tooling and dependency boundaries), **D16** (distribution and scope boundaries);
acceptance criteria per `docs/orchestration/tasks.json` T01.

This document is a **bounded provenance inventory**, not legal clearance. It
records what was actually found in the pinned local trees and headers, and
records every right without attributable permission evidence as **unknown**.
No component in this document is cleared for distribution by this file.

## How to read this document

**Permission status** is one of:

- **allowed** — a license document or license-bearing header was read at the
  pinned revision, and the observed grant covers the stated use.
- **unknown** — no license document or grant was found; public availability,
  community practice or the absence of a `LICENSE` file proves nothing.
- **blocked** — terms were read and they do **not** permit the intended use.

**Browser closure** is the set of components that would be compiled into, linked
with, or loaded by the browser client (Wasm core, JavaScript shell, renderer,
media path, signaling service). A component marked **excluded** is not part of
that closure and must not be reintroduced through a build fallback.

Only components with `allowed` status **and** in-closure status can be
distributed on observed evidence. "Acceptance" of T01 means this inventory is
complete and honest; it does **not** mean any right was cleared.

Evidence IDs reused from the pinned audit: **DEPS-01 … DEPS-09**,
**BUILD-ENV-01** (see `docs/evidence/gta2-build-audit-20261012/`).

## 1. Pins and inspected files

| Item | Pin / revision | Inspected this task |
| --- | --- | --- |
| `research/gta2_re` | `02de8beeb766644417f195d360eba33af0ee491f` | `git ls-tree -r --name-only HEAD` (417 tracked files, no `LICENSE`/`LICENCE`/`COPYING`/`NOTICE` match); `CMakeLists.txt`; `Source/3rdParty/mss.h`, `mss.cpp`, `bink.h`, `bink.cpp`; `Source/Bink.cpp`; `Source/NetPlay.hpp`, `NetPlay.cpp` |
| `Source/3rdParty/Manual-DLL-Loader` | `6fb8e99b629bd74b83387987ec6f6f4fdbd43bee` | `LICENSE` (full text) |
| `Source/3rdParty/Detours` | `5e081ec7a295b8b090392f579e4dd75e997b88bb` | `LICENSE.RTF` (full text) |
| `3rdParty/GTA2Hax` | `63151bcb0bf1f18db42b1594da2beb41fc9ab4ab` | `CMakeLists.txt`; `git ls-tree -r --name-only HEAD` (34 tracked files, no license-file match); `Common/d3d9.h`, `Common/d3d9caps.h` |
| `GTA2Hax/3rdParty/imgui` | `7f885bc6c32ff07474714efb4c3b8fb93dea7b6b` | `LICENSE` (full text); `stb_rect_pack.h`, `stb_textedit.h`, `stb_truetype.h` file headers |
| `3rdParty/gta2_re_compile_tools` | `7ba0d72b4be382f816086b4e76e8c252c850b06a` (pinned address, submodule **not checked out locally**) | Names/metadata only via retained `dependencies/compile-tools-tree.json` (DEPS-09); no license document was read |
| `3rdParty/cpp_permuter` | `1a799211f2ed3211cea265944c91179ca29cf723` (not checked out) | Not inspected; build-tooling only |
| Emscripten / emsdk | emsdk `6.0.12`, emsdk SHA `35ff8a6d150541276abbc6bae512ca90bcfbe220` (BUILD-ENV-01) | `research/toolchains/emsdk/LICENSE`; `research/toolchains/emsdk/upstream/emscripten/LICENSE` |
| Node.js | v26.7.0 at `node` (BUILD-ENV-01) | `<node>/LICENSE` |
| `ws` | 8.21.0, `node_modules/ws/package.json` | `package.json` license field; DEPS-01/DEPS-07 don't apply, see matrix |
| `playwright-core` | 1.58.2, `node_modules/playwright-core/package.json` | `package.json` license field |
| `rea-agents` | 6.4.0, `node_modules/rea-agents/package.json` | `package.json` license field (out of closure) |
| FFmpeg | **not found on this host** (recorded in run ledger `docs/orchestration/runs/run-20261012T2105/environment.md`) | Nothing to inspect; no binary, no version, no build configuration |
| Original game (installer / EXE / assets / official compiler package) | `downloads/GTA2INSTALLER.exe`, MD5 `764848b25aed87b69da472f18c19580d` | `README.md` redistribution statement; no archive contents read by this task |

## 2. Component matrix

### 2.1 Core reconstruction source (in the intended browser closure)

| Component | Source path(s) | Origin + license as actually found (file read) | Browser closure | Permission status |
| --- | --- | --- | --- | --- |
| gta2_re reconstruction core (game logic, mission VM, collision, formats, saves, RNG, input) | `research/gta2_re/Source/*.cpp`, `*.hpp` (256 files); `Source/Globals.*`, `fix16.*`, `ang16.*`, `Fix16_Point.*`, `ScriptManager_C1EA8.*`, `Miss2_*`, `NetPlay.*`, `Car*`, `Ped*`, `Police_*`, `PathFinder_*`, `map_0x370.*`, … | CriminalRETeam/gta2_re @ `02de8beeb…`. **No `LICENSE`, `LICENCE`, `COPYING` or `NOTICE` file exists in the 417-file tracked tree** (verified with `git ls-tree -r --name-only HEAD` here; matches DEPS-08). Source headers carry no license grant. | **In closure** (adapted into ignored `research/port-work/core/` per D02) | **unknown** |
| gta2_re build/analysis tooling | `build.py`, `vc6_setup.py`, `reccmp/*`, `requirements.txt`, `Scripts/`, `test.cpp` | Same tree; no license file. `requirements.txt` pins nothing (`requests`, `iced-x86`, `pyclip`) — DEPS-01. | Local only (not shipped) | **unknown** |
| gta2_re reccmp/hash fixtures | `reccmp-project.yml`, `reccmp/*.csv`; referenced binaries `9.6f.exe` (sha256 `420af867…`), `10.5.exe` (sha256 `399910c8…`) | Same tree; the referenced executables are **not present** locally and must not be acquired (D01, D16). | Not in closure | **unknown** |

### 2.2 Excluded Windows injection / hook / renderer / SDK dependencies

| Component | Source path(s) | Origin + license as actually found (file read) | Browser closure | Permission status |
| --- | --- | --- | --- | --- |
| Detours 3.0 Express | `research/gta2_re/Source/3rdParty/Detours/**` | `LICENSE.RTF` read in full: "Microsoft Research Shared Source License Agreement (Non-commercial Use Only)", "Detours 3.0 Express"; "You may not use or distribute this Software or any derivative works in any form for commercial purposes"; derivative works must carry notices and redistribute under the same terms. **Do not substitute the modern Detours 4 license** (DEPS-08). | **EXCLUDED** — `CMakeLists.txt:289–296` compiles `Detours/src/detours.cpp` and `disasm.cpp` into `HookLoader` only; that target is the Windows injection path (D02) | **blocked** (observed terms do not permit the intended distribution) |
| Manual-DLL-Loader | `research/gta2_re/Source/3rdParty/Manual-DLL-Loader/**` | `LICENSE` read in full: **GNU GPL version 3** (verbatim GPLv3 text). | **EXCLUDED** — `CMakeLists.txt:294` compiles `Loader.cpp` into `HookLoader` (DEPS-08) | **blocked** for the browser closure; GPLv3 is a strong copyleft grant, not a permissive one |
| HookLoader / ExePatcher / WinMainProxy | `Source/HookLoader.cpp`, `Source/ExePatcher.cpp`, `Source/Function.hpp`, `Source/WinMainProxy.cpp`, `Source/WinMainProxy*` | Same gta2_re tree; no license file. `HookLoader.cpp` redirects STUB/WIP functions back into the original executable (DEPS-04). | **EXCLUDED** — explicit D02/D16 instruction; patched/hybrid mode cannot be a standalone proof route | **unknown** (and excluded regardless) |
| MASM assembly path | `research/gta2_re/test2.asm`; `CMakeLists.txt:6` `ENABLE_LANGUAGE(ASM_MASM)` | Same tree; no license file for the MASM toolchain itself. | **EXCLUDED** (x86-assembly/Windows toolchain; wasm32 target uses no MASM) | **unknown** |
| DirectPlay (DirectPlay3 / DirectPlayLobby2 / DPLAYX) | `Source/NetPlay.hpp:4–7,188–190,290–291`; `Source/NetPlay.cpp`; `CMakeLists.txt:272–273` links `${…}/VC98/Lib/DPLAYX.lib` | Microsoft DirectX SDK component. Only filenames/metadata observed (DEPS-09); the SDK is not redistributable by us. | **EXCLUDED** — D02 forbids importing DirectPlay; D11 replaces transport with WebRTC data channels plus a Node/ws signaling service | **blocked** (no observed grant; and no browser equivalent exists) |
| Legacy VC98 / MSVC 6 compile tools (`gta2_re_compile_tools`) | `3rdParty/gta2_re_compile_tools/**` (`VC98/Bin/CL.EXE`, `LINK.EXE`, `VC98/Include/DPLAY.H`, `VC98/Lib/DPLAYX.LIB`, `cmake-3.5.0-win32-x86`) | Submodule **not checked out locally**; evidence is filenames/metadata in `dependencies/compile-tools-tree.json` (6,671 entries) plus DEPS-09. No license document read; no grant inferred from public hosting. | **EXCLUDED** — Windows/x86 build tool only; D02 pins Emscripten 6.0.12 + CMake instead | **unknown** (excluded) |
| GTA2Hax Windows renderer + D3D9/DirectDraw surface shim | `3rdParty/GTA2Hax/DmaVideo/**`, `d3ddll/**`, `Common/**`, `CMakeLists.txt` | GTA2Hax @ `63151bcb…`. **No license file in the 34-file tracked tree** (verified here; matches DEPS-08). Renderer creates `IDirect3D9`/`IDirect3DDevice9` and an HWND swap chain (DEPS-05). | **EXCLUDED** — D07 selects a pure JavaScript WebGL2 renderer consuming the C++ render view; DEPS-05 found no browser backend in GTA2Hax. Format/interface knowledge may inform the port, but **copying its code is not covered by that distinction** | **unknown** (excluded) |
| Direct3D9 / DirectDraw SDK headers in GTA2Hax | `3rdParty/GTA2Hax/Common/d3d9.h`, `d3d9caps.h`, `d3d9types.h`, `d3d_vc6.h`, `d3dcaps.h`, `d3dtypes.h`, `ddraw_vc6.h`, and tracked binary `Common/d3d9.lib` | `Copyright (C) Microsoft Corporation. All Rights Reserved.` read in the `d3d9.h` and `d3d9caps.h` headers. No grant read. | **EXCLUDED** | **unknown** (excluded) |
| imgui + DX9 example backend | `3rdParty/GTA2Hax/3rdParty/imgui/**`; built by `GTA2Hax/CMakeLists.txt:33–40` (`imgui.cpp`, `imgui_draw.cpp`, `examples/directx9_example/imgui_impl_dx9.cpp`) | `LICENSE` read in full: MIT, "Copyright (c) 2014-2015 Omar Cornut and ImGui contributors", notice must be preserved. Bundled `stb_rect_pack.h` (v0.10), `stb_textedit.h` (v1.9, Sean Barrett) and `stb_truetype.h` (v1.12) declare **public domain** in their own file headers. | **EXCLUDED** — no UI framework in v1 (D02); the DX9 backend is Windows-only | **allowed** for imgui (MIT terms read); bundled stb headers recorded as public domain per their own headers. Exclusion is a design choice, not a rights problem |
| Miles Sound System | `Source/3rdParty/mss.h` (144,885 bytes), `Source/3rdParty/mss.cpp`; game-side `Source/sound_obj.*`, `cSampleManager.*`, `root_sound.*` | `mss.h` header read: "Miles Sound System", "Author: John Miles", `MSS_COPYRIGHT "Copyright (C) 1991-99 RAD Game Tools, Inc."`, version `5.0q`. **The header is a copyright notice, not a permission grant** (DEPS-08). `mss.cpp` bodies are empty/zero-returning linking shims (DEPS-03). | **EXCLUDED** as an SDK/DLL. The browser gains audio through reconstructed game-side sound scheduling plus Web Audio `AudioBuffer` playback (D08) — no Miles code or DLL ships | **unknown** for the header; **no grant observed** for the SDK itself |
| Bink Video (RAD) | `Source/3rdParty/bink.h`, `bink.cpp`; game-side `Source/Bink.cpp`, `Bink.hpp` | No license text found in these files; `bink.h` is a reconstructed BINK/BINKBUFFER struct layout. `bink.cpp` shim bodies are empty (DEPS-03). The Bink **DLL** is an original-game component. | **EXCLUDED** — D08 converts un-decodable media with a local FFmpeg step and plays MP4/H.264 + AAC via an HTML video element; no browser Bink decoder is written | **unknown** for the reconstructed struct/shim; **no grant observed** for the Bink DLL |
| Original Windows EXE (`gta2.exe` / `9.6f.exe` / `10.5.exe`) | Not in this workspace as a research artefact; `downloads/GTA2INSTALLER.exe` only | Rockstar/Take-Two property. `README.md`: "This release may not be redistributed; keep this copy for personal use only." | **EXCLUDED** — D02 forbids importing the original EXE; D01 restricts it to a Windows reference oracle; patched-mode fallback is not standalone proof (DEPS-04) | **blocked** (no redistribution right observed) |

### 2.3 Build, runtime and service dependencies (intended closure)

| Component | Source path(s) | Origin + license as actually found (file read) | Browser closure | Permission status |
| --- | --- | --- | --- | --- |
| Emscripten / emsdk | `research/toolchains/emsdk/**`; `upstream/emscripten/**` | `emsdk/LICENSE`: "Copyright (c) 2018 Emscripten authors … Permission is hereby granted, free of charge …" (MIT-form). `upstream/emscripten/LICENSE`: "Emscripten is available under 2 licenses, the MIT license and the University of Illinois/NCSA Open Source License", "Both are permissive open source licenses". | Build toolchain; emits the Wasm module that ships | **allowed** (per terminology above: a license document was read and the observed grant covers this use) |
| Node.js | `node` (v26.7.0) | `<node>/LICENSE`: "Node.js is licensed for use as follows … Copyright Node.js contributors. All rights reserved. Permission is hereby granted, free of charge …" | Development + signaling service runtime only (never shipped to the browser) | **allowed** |
| `ws` 8.21.0 | `node_modules/ws/package.json` | `"license": "MIT"` read from the installed package manifest. Declared a direct exact dependency by D02. | Signaling service only (not the browser client) | **allowed** |
| `playwright-core` 1.58.2 | `node_modules/playwright-core/package.json` | `"license": "Apache-2.0"` read from the installed package manifest. | Test tooling only | **allowed** |
| FFmpeg (local, externally supplied) | Not present on this host | **Nothing inspected.** No binary, version, or build configuration exists to read; FFmpeg is LGPL/GPL depending on build flags and no build has been selected (D08). | Media preparation tool run locally; its **output** (MP4/H.264 + AAC) is generated from the user's own assets and is not redistributed (D16) | **unknown** |
| Browser APIs (WebGL2, Web Audio, IndexedDB, WebRTC, HTML media) | n/a | W3C/WHATWG specifications; no code dependency. | In closure by design (D03, D07, D08, D11) | not a rights question; no third-party code |

### 2.4 On-host analysis tooling (out of closure, recorded for completeness)

| Component | Source path(s) | Origin + license as actually found | Browser closure | Permission status |
| --- | --- | --- | --- | --- |
| REA (`rea-agents` 6.4.0) | `node_modules/rea-agents/**` | `"license": "MIT"` read from the installed package manifest. | Not in closure | **allowed** |
| `cpp_permuter` | `3rdParty/cpp_permuter` @ `1a799211…` (not checked out) | Not inspected. | Not in closure | **unknown** |
| CMake / Python / git | Host installs | Not inspected this task. | Not in closure | **unknown** (tooling, not distributed) |

## 3. Explicit exclusion from the browser closure

The following are excluded from the intended browser dependency closure and must
not be compiled, linked, loaded or reintroduced through a build fallback:

1. **Detours** (`Source/3rdParty/Detours`) — non-commercial shared-source terms.
2. **GTA2Hax hook loader path** — `Source/HookLoader.cpp`, `Source/ExePatcher.cpp`,
   `Source/3rdParty/Manual-DLL-Loader` (GPLv3), `Source/WinMainProxy.cpp`,
   `Source/Function.*` status-routing.
3. **MASM** — `ENABLE_LANGUAGE(ASM_MASM)` and `test2.asm`.
4. **DirectPlay** — `NetPlay.hpp/.cpp` COM use of `IDirectPlay3` /
   `IDirectPlayLobby2` and the VC98 `DPLAYX.lib` link.
5. **The original executable** and any patched/hybrid build of it.
6. **Miles and Bink DLLs** (`mss32`, `binkw32`) and their SDK headers as a
   shipped dependency.
7. **The Windows VC98 compile-tools tree** and the GTA2Hax D3D9 renderer and
   Microsoft D3D/DirectDraw headers (`Common/d3d9*.h`, `d3d3*.h`, `d3dcaps.h`,
   `d3dtypes.h`, `ddraw_vc6.h`, `d3d9.lib`).
8. **imgui and the DX9 example backend** — excluded by D02 (no UI framework),
   notwithstanding its permissive license.

Exclusion is a **containment measure, not a clearance**. It removes these
components from the shipped closure; it does not resolve the unresolved rights in
§2.1, and it does not authorise copying code from an excluded component into an
in-closure one. Per the browser spec: calling decompiled code "clean-room" or
adding an MIT header does not establish rights.

## 4. Distribution gate (D16)

**Gate statement.** D16: "No source license is inferred from public availability.
Unknown core reuse permission remains a distribution blocker; only actual
permission evidence can close it… Publish neither original/converted assets nor a
local game-derived pack. Do not promise a distributable product until this gate
passes."

**Evidence owner.** The **orchestrator** owns this gate. Task definitions assign
it to the orchestrator ("Permission to distribute selected source — Record
attributable permission or keep release blocked", `docs/orchestration/decisions.md`
§Evidence dependencies; escalation is addressed to the orchestrator, not to a
worker). T01 supplies the inventory; it does not own, clear or waive the gate.

**Required resolution before any release.** An attributable, component-by-component
rights dossier that either records a grant or records that the component is not
distributed. Specifically, at minimum:

| # | Unknown/blocked right | Required evidence to close |
| --- | --- | --- |
| 1 | gta2_re core `Source/` (in closure) | Written permission from the gta2_re rights holders (CriminalRETeam / contributing authors) permitting use and distribution of the adapted browser client, or a recorded decision plus a new scoped plan per D16 (a from-scratch rewrite is **not** an automatic worker fallback) |
| 2 | gta2_re-derived algorithms, constants, struct layouts and names carried into `research/port-work/core/` | The same grant as #1, itemised against the actual adapted file list (the D02 local patch series is the manifest to review) |
| 3 | GTA2Hax renderer, format/interface knowledge and any ported fragment | Written permission from the GTA2Hax authors, with a code-review statement of exactly what was or was not copied |
| 4 | Excluded components (Detours, Manual-DLL-Loader, DirectPlay, VC98 tools, D3D9 headers, MASM path, Miles/Bink SDK, original EXE) | A build-level attestation that none is compiled, linked or shipped, plus a source-diff of the shipped tree against those paths |
| 5 | Miles/Bink SDK headers and DLLs (if any header, constant or struct survives into the shipped tree) | Written permission, or removal from the shipped artefact with the removal recorded |
| 6 | FFmpeg build used for media preparation (local dependency, absent on this host) | The selected binary, version, configure flags/build configuration and license (LGPL vs GPL) recorded before release; no FFmpeg binary is bundled (D08) |
| 7 | Permissive toolchain/runtime components (Emscripten, Node, ws, Playwright, imgui if it ever ships) | Preserve the notices their terms require and include them in the shipped source/notice manifest (R08) |
| 8 | Original game assets and any `.gta2pack` or converted media | Stay user-supplied and out of every published bundle (D06, D16). Assets are never the resolution path for a source-rights failure |

**Gate trigger.** Passing G0/T01 does **not** satisfy this gate. The gate must be
passed before any public distribution of a build, before any promise of a
distributable product, and it is a **release blocker** for G6 (release candidate
accepted, not published). If permission fails or is refused, the orchestrator
must obtain a new scoped plan; workers may not substitute an invented license
position.

## 5. Known discrepancies and findings

- **In-tree wording overstates rights.** `AGENTS.md` describes work products as
  "MIT-style, no assets" and calls the bundled script compiler "official
  MIT-licensed". Neither claim is established by any artefact read for this task.
  This document does not repeat them; outstanding permission questions remain
  unknown until the gate in §4 closes.
- **Pinned audit findings are reused, not re-derived.** DEPS-08 (no root license
  in gta2_re or GTA2Hax; GPLv3 loader; MSR-SSLA Detours; MIT imgui; RAD copyright
  in `mss.h`; Microsoft headers) was re-verified only for the file-level
  observations listed in §1 and is otherwise cited as-is.
- **Submodules are not all present locally.** `gta2_re_compile_tools` (both
  copies) and `cpp_permuter` are pinned but not checked out, so their licenses
  were **not** read; their entries above are explicitly filename/metadata
  inferences (DEPS-09) and remain `unknown`.
- **No vendor decision has been made.** The spec requires a future source
  vendor/import decision to record provenance and permission status
  (`gta2-browser-spec.md`, "Do not mutate `research/gta2_re`"). This document is
  the input to that decision, not the decision.

## 6. Unknowns ledger

| Unknown | Exact missing fact |
| --- | --- |
| U1 | Any grant covering gta2_re `Source/` reuse and redistribution (no license file in 417 tracked files) |
| U2 | Any grant covering GTA2Hax D3D9 renderer code and interface knowledge (no license file in 34 tracked files) |
| U3 | Whether any gta2_re contributor besides the repository owners holds rights in the reconstructed code |
| U4 | Terms of `gta2_re_compile_tools` / VC98 SDK content, and whether any file from it is required by the browser build (currently believed not required) |
| U5 | Whether the Miles/Bink headers (`mss.h`, `bink.h`) or their constants would ship, and if so under what grant |
| U6 | The FFmpeg build, version and license to be used locally |
| U7 | Whether the official DMA Design script compiler/format documentation carries an MIT grant as `AGENTS.md` asserts (not verified; no file read) |
| U8 | DirectPlay/D3D9/VC98 SDK terms applicable to anything retained in the shipped tree (believed nil, needs a build attestation) |

## 7. What this file does not claim

- It does not claim any component in the browser closure is MIT, clean-room,
  public-domain or otherwise cleared, and it must not be summarised that way.
- It does not decide the source vendor/import question.
- It does not authorise shipping assets, a `.gta2pack`, converted media or any
  original/converted game file.
- It does not claim that excluding injection components resolves the reuse
  question for the remaining source.
- Acceptance of T01 records that this bounded inventory exists; it is **not**
  permission clearance, and it does not clear release.
