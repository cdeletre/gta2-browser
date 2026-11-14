# GTA2 Reverse Engineering — State of the Art (research date: 12 October 2026)

> **Pinned audit now available:** [gta2_re build, gaps and revised budget](gta2-re-build-audit-2026-10-12.md)
> records 3,400 MATCH markers, 21 WIPs and 9 CRT-wrapper STUBs at revision
> `02de8be`, successful upstream function checks, and an isolated browser math
> test. It does not establish full standalone/browser gameplay or reuse rights.

> **Follow-up checked 12 October 2026:** the maintainer's 9 October update reports
> fewer than 50 functions still to match, so the older two-thirds progress figure
> below is historical. This is a maintainer report, not a local gameplay test.
> See the [browser feasibility assessment and REA pilot](browser-port-feasibility-2026-10-12.md)
> for the source, measured tooling results, and token/cost scenarios. That review
> also did not establish a reuse license for `gta2_re`; do not read the general
> “MIT-style” discussion below as a verified license for that repository.

Scope: what the community knows about GTA2's internals (DMA Design / Rockstar, PC/PSX/Dreamcast, Oct 1999), who documented it, and what tooling exists. Sources are cited inline. Compiled from live web research; all dates below are as published.

---

## 1. Documented file formats

Note on terminology: the tile/graphics archive is `.STY` ("style"), not STL — there is no `.stl` in GTA2. Map/level files are `.gmp`.

| Format | What it is | Documentation status | Where |
|---|---|---|---|
| **GMP** (.gmp) | Map/level: 256×256×8 `block_info` cube, zones, map objects, lights, tile animation, routefinder junction list | **Fully documented by an official DMA Design internal spec** ("GBH Map Format", ~13 pages; header `"GBMP"`, version 500; chunk types UMAP/CMAP/DMAP/ZONE/MOBJ/PSXM/ANIM/LGHT/RGEN; CMAP = 16-bit, DMAP = 32-bit compression with hidden-surface removal). Mirrored by GTAMP; also on XentaxWiki. Forum research identifies **Colin MacDonald** as the author of the original `GTA2 Map format.doc` | [GTAMP GMP format](https://gtamp.com/gta2/gta2-gmp-map-file-format/), [XentaxWiki GMP](https://wiki.xentax.spektr.name/index.php/GTA_Series_GMP), [GTAMP authorship note](https://gtamp.com/forum/viewtopic.php?p=11568) |
| **STY** (.sty) | Style/graphics archive: palettes (PALX/PPAL/PALB), 64×64 tiles, sprites, deltas, fonts, car info (CARI), map object info (OBJI), PSX tiles, car recycling | **Fully documented by official DMA spec** ("GBH Style Format", version 600; same chunk system as GMP) mirrored on GTAMP; plus a plain-language community walkthrough (ModDB "GTA2 Editing Compendium", 2023). Delta animations (DELS/DELX) remain the fiddliest part | [GTAMP STY format](https://gtamp.com/gta2/gta2-style-sty-graphics-file-format/), [ModDB compendium](https://www.moddb.com/games/grand-theft-auto-2/tutorials/gta2-editing-compendium) |
| **Mission scripts** (.mis → `miss2.exe` → .scr binary) | Proprietary "GTA2script" language; multiscript system (level script + mission scripts) | **Both source language and binary format fully understood.** DMA officially released the compiler `miss2.exe` + 105-page manual + Industrial-district .mis sources on 23 Dec 1999 under an **MIT licence** ([GTA Wiki](https://gta.wiki/w/GTA2script_Compiler)). The binary `.scr` opcode format was reverse-engineered by T.M./Vike (SCR Decompiler, full command list, ~447 opcodes) and perfected by **Valps** — as of Nov 2024 the decompiler produces **recompilable .mis files for all three districts**, including fully decoded IF/WHILE/AND/OR/ELSE semantics (including the compiler's "lefty-form" bug) and a decomp.me scratch of gta2.exe's IF_JUMP handler | [GTAMP SCR decompiler thread](https://gtamp.com/forum/viewtopic.php?t=447), [IF/WHILE analysis](https://gtamp.com/forum/viewtopic.php?style=1&t=1393), [GTAForums "SCR decompiler finally 100% functional"](https://gtaforums.com/topic/999466-scr-decompiler-finally-100-functional/), [WikiGTA scripting manual](https://en.wikigta.org/wiki/Scripting_manual_(GTA2)), [Project Cerbera scripting page](https://projectcerbera.com/gta/2/tutorials/scripting) |
| **Audio** (SFX.RAW + SFX.SDT, per district: bil/ste/wil) | `.sdt` = 24-byte-per-entry directory (offset, size, sample rate, loop points); `.raw` = headerless 16-bit mono PCM | Fully documented on GTAMods Wiki (incl. exact WAV header template); radio (GTAudio) and vocals ship as plain WAV. Tools: GTA Wave 4.0 (2002), ermaccer's RockstarAudioTool | [GTAMods Wiki SFX](https://gtamods.com/wiki/SFX), [Grand Theft Wiki SFX](https://www.grandtheftwiki.com/SFX), [RockstarAudioTool](https://github.com/ermaccer/RockstarAudioTool) |
| **Other data files** | GCI (car handling), GXT/FXT (text), MMP (multiplayer map manifest), SVG (saves), REP (replays), d3ddll.dll / DmaVideo.dll (renderer helper DLLs) | Saves documented on GTAForums (2017, header/stats/weapons/respect layout); memory-address documentation for saves/trainers on GTAMP (t=700); **d3ddll and DmaVideo have been fully reimplemented in C++** by the gta2_re team ("GTA2Hax") | [GTAForums save format](https://gtaforums.com/topic/891744-save-file-format-documentation/), [GTAMP memory addresses](https://gtamp.com/forum/viewtopic.php?style=1&t=700), [gta2_re summary](https://gtamp.com/forum/viewtopic.php?t=1424) |
| **Official bundle** | `gta2formats.zip` on GTAMP = the original DMA GMP + STY technical docs | The single most authoritative reference; hosted alongside the tools | [GTAMP tools page](https://gtamp.com/gta2/tools/) |

**Completeness verdict:** the data-side formats (GMP, STY, audio, GXT/GCI/MMP) are essentially completely documented — unusually, from *official* documentation that DMA released publicly. The executable side (opcode interpreter, AI, physics) is being recovered by the decompilation project (§3).

---

## 2. Community modding ecosystem

**Hub:** [GTAMP.com](https://gtamp.com/gta2/) (run by Sektor) — main site, [tools page](https://gtamp.com/gta2/tools/), [forum](https://gtamp.com/forum/), and Discord. It is unambiguously the centre of the GTA2 modding world.

**How active:** **Still alive in 2026.** The forum front page showed new posts dated 10–11 October 2026 ("New GTA2 Map Editor" by B-$hep with trubetz/valps/The_Loonies; "official GTA2 Map Editor patches"; "Essential mods for GTA2 unofficial v11.44?"). Game Hunter's changelog shows releases 2023, 2024, and 24 June 2025 (v1.5993) ([changelog.txt](https://gtamp.com/GTA2/changelog.txt)). The golden era was ~2009–2015; activity resurged strongly from ~2021 onwards, driven by the decompilation project.

**Key community tooling:**

- **GTA2 v11.44 "unofficial" executable** — Vike the Hube's continuous patch series (11.00→11.44) applied *by disassembly* to the game: multiplayer desync fixes, windowed mode, Windows 8/10/11 compatibility, replay improvements, traffic-flow fixes, script-command hardening. Distributed via [gtamp.com/gta2](https://gtamp.com/gta2/) with a [full changelog](https://gtamp.com/forum/viewtopic.php?t=73). Works on Windows 11.
- **GTA2 Game Hunter** (Sektor) — IRC-based multiplayer lobby/matchmaking with automatic map download (`gta2://` protocol), actively maintained into 2025.
- **Map editors** — official DMA GTA2 Map Editor (with community white-tile/crash fixes); **"New GTA2 Map Editor"** by B-$hep, in active development October 2026 ([forum](https://gtamp.com/forum/index.php)); Epic GTA2 Map Editor (T.M.) — map viewer, zone editor, script decompiler.
- **Script tools** — official `gta2script` kit (miss2.exe + docs + Industrial .mis sources); **GTA2 ScriptPad** (B-$hep, editor + compiler front-end); mispad (Jernej); **SCR decompiler** (T.M./Valps, 100% round-trip since 2024).
- **Graphics/audio** — GTA2 STY Tool (Jernej), GTA2 File Editor (Stephen Dawkins et al.), GTA Wave 4.0, TXT2GTA2.
- **Utilities** — GTA2_svge save editor (Ishani/Harry Denholm), Zone Editor, cmp2gmp (GTA1→GTA2 maps), INI2MIS, replay chat viewer, GXT editor.
- **Runtime mods (2025–2026)** — Seuko mod menu ([izawartka/gta2-seuko-mod](https://github.com/izawartka/gta2-seuko-mod)), GTA2 Radar mod for v11.44 ([forum, July 2026](https://gtamp.com/forum/viewtopic.php?t=1443), [GitHub](https://github.com/r3538987/gta2-radar-mod)), FrontendFix, widescreen fix for 9.6 (fusionfix.io/wfp#gta2).

**Related wikis:** [WikiGTA](https://en.wikigta.org/wiki/Scripting_manual_(GTA2)) (scripting manual, script compiler), [GTAMods Wiki](https://gtamods.com/wiki/SFX) (file formats; SFX page updated Feb 2025), [XentaxWiki](https://wiki.xentax.spektr.name/index.php/GTA_Series_GMP), [GTA Wiki](https://gta.wiki/w/GTA2script_Compiler). [Project Cerbera](https://projectcerbera.com/gta/2/tutorials/scripting) (BenMillard) hosts polished GTA2 tutorials. GTAForums' GTA 2 board still sees tooling threads (e.g. SCR decompiler, Nov 2024).

---

## 3. Reverse engineering / engine recreation projects

The current headline project, plus the significant historical ones:

| Project | What it is / achieved | Language | Last activity | Link |
|---|---|---|---|---|
| **CriminalRETeam/gta2_re** | **RE3-style decompilation of gta2.exe v10.5 (PC)**. Team of 4+ (Valps et al.). **2,300+ functions matched (>⅔ of the exe)** as of the Nov 2025 summary; frontend fully functional; GTA2Hax submodule reimplements `d3ddll.dll` and `DmaVideo.dll`; builds with a pinned MSVC 6 toolchain to produce byte-matching code. Knowledge base: **GTA2 RE Hub** (classes Fix16/Ang16, IDA scripts, 9.6f address maps, matching quirks doc) | C++ | **Pushed 11 Oct 2026** (created Sep 2024); 114★ | [GitHub](https://github.com/CriminalRETeam/gta2_re), [RE Hub](https://valps.github.io/gta2-re-hub/), [forum summary](https://gtamp.com/forum/viewtopic.php?t=1424) |
| **gebdag/gta2-rtx-remix** | Direct3D 9 renderer hook for **RTX Remix** — path-traced GTA2 with dynamic lights from explosions/fires, dynamic time of day, 16:9 fix (no sprite pop-in), 60 fps via frame gen. Includes engine research notes (map/style/mesh/camera code) | C++ | Created Sep 2026, pushed 4 Oct 2026; 172★ | [GitHub](https://github.com/gebdag/gta2-rtx-remix) |
| **ikkentim/OpenGta2** | From-scratch reimplementation on MonoGame using original assets; early stage | C# (MIT) | Pushed 8 Mar 2026; 27★ | [GitHub](https://github.com/ikkentim/opengta2) |
| **sp00nznet/gta** | **Static recompilation** of GTA1/London/GTA2 to x86-64. GTA1 fully lifted (1,957 functions → 238K lines of C, builds & runs). **GTA2 blocked: every known gta2.exe is TAC-packed** ("The Anti-Cracker"), *including the Rockstar Classics installer* — needs a runtime dump first. Confirms GTA2 = same Race'n'Chase lineage, DirectX 6-era | C/C++ | 2025–2026 (GTA2 portion blocked) | [GitHub](https://github.com/sp00nznet/gta) |
| **gennariarmando/havoc2** | WIP reimplementation ("very early stage, not playable yet"); author also maintains gta2-debug (ImGui in-game trainer via plugin-sdk-style injection), gta2-frontend-fix, gta2-radar | C (MIT) | Pushed Mar 2024; 11★ | [GitHub](https://github.com/gennariarmando/havoc2) |
| **h0x91b/gta2-resurection** | Script-extender/runtime mod for 11.44 (Detours DLL injection + **LuaJIT** scripting); ships a Ghidra project file and Cheat Engine table; dev streamed RE sessions on YouTube | C++ (MIT) | Pushed Nov 2021; 24★ | [GitHub](https://github.com/h0x91b/gta2-resurection) |
| **u9oqcd4p/OpenGTA2** (Black Phoenix) | "OpenGBH" clean-room engine + GTA2 asset conversion toolchain; header explicitly states no DMA/Rockstar code or specs used (beerware). Elder project (SVN era) | C++ | GitHub mirror, historic (~2015) | [GitHub](https://github.com/u9oqcd4p/OpenGTA2), [GTA Wiki page](https://gta.wiki/w/OpenGTA2) |
| **Bytewerk/gta2-hackers-remix** | Hackerspace project: XInput gamepad support + **splitscreen mod** for GTA2 via memory patching | C | Pushed Jan 2017; 28★ | [GitHub](https://github.com/Bytewerk/gta2-hackers-remix) |
| **Valps/gta2-map-compressor** | Reimplements GMP compression (CMAP/DMAP) for **PC and PSX** maps, faster than the official editor; also enables PSX ISO map replacement | Python | 2024–2025 era | [GitHub](https://github.com/Valps/gta2-map-compressor) |
| **jpires/gta2_reversing** | Personal Ghidra-based RE of the Steam 9.6.0.0 and 11.44.0.0 exes; Detours-based DLL injector | C++ | Pushed ~2021 | [GitHub](https://github.com/jpires/gta2_reversing) |
| Others | Kajakklubben/gta2-webgl (WebGL port, 2016), andrecarlucci/gta2net (C#, 2015), izawartka/gta2-seuko-mod (mod menu, 2025–26), MrSapps/GTA2Hax (trainer) | various | varies | see links |

**Spam warning:** several 2026 repos matching "gta2 trainer/mod menu 2026" (e.g. `btshahraag20/gta2-memory-master`, `heeeet111/gta2-seuko-vice-edition`) are SEO-bait mirrors with recycled code and no provenance — treat as untrusted; get mod menus from the gtamp-linked originals.

**Legal posture:** the major projects require the user's own copy of the game and ship no assets (havoc2 and OpenGTA2 state this explicitly; gta2_re distributes decompiled/rewritten code + documentation, i.e. no Rockstar assets). Rockstar/Take-Two still own the copyright and have a DMCA history with reverse-engineered GTA code (the 2021 GTA III/Vice City RE takedown-and-restore saga — [Ars Technica](https://arstechnica.com/gaming/2021/05/github-reverses-takedown-of-reverse-engineered-gta-source-code/)), so licence hygiene (MIT, no assets) is the community norm.

---

## 4. Original developers, source availability, leaks

- **No public source leak for GTA2 exists.** Unlike GTA III/VC (reverse-engineered) or GTA V (leaked), GTA2's original source has never leaked or been released. A [2006 GTAForums petition](https://gtaforums.com/topic/243222-petition-to-release-gta1gta2-source-code/) to release the GTA1/GTA2 sources (supported by a former DMA dev) went nowhere; the community consensus there (B-$hep): the 2004 Rockstar Classics re-release was **recompiled from surviving source (v9.6F, MSVC 7.0, with debug info)** — the retail 1999 exe was MSVC 5.1/6.0 — and the Russian publisher BUKA's 10.1/10.3 builds were compiled from a different (`c:\gbh\coop101release`) source tree, so complete, current sources may be lost or scattered.
- **What DMA legitimately gave the community:** the mission-script compiler (`miss2.exe`, MIT-licensed 23 Dec 1999, with a 105-page manual and Industrial-district mission sources — [GTA Wiki](https://gta.wiki/w/GTA2script_Compiler)), the official GMP/STY format docs ([gta2formats.zip](https://gtamp.com/gta2/tools/)), and the official map editor. This is why data-format knowledge is so complete.
- **Named people behind the internals:** lead programmer **Keith R. Hamilton** ([Wikipedia credits](https://en.wikipedia.org/wiki/Grand_Theft_Auto_2)); the format docs' metadata points to **Colin MacDonald** (GMP/map format) and **Keith Hamilton** (CDS.doc) ([forum discussion](https://gtamp.com/forum/viewtopic.php?p=11568)); designers Billy Thomson/Stephen Banks; directors Jim Woods/David Jones. **Mike Dailly** (GTA1) has published development history of the shared Race'n'Chase lineage. An ex-DMA dev also uploaded original uncompressed GTA2 music to YouTube in 2024 ([GTAForums thread](https://gtaforums.com/topic/999226-ex-dma-dev-uploading-original-uncut-and-uncompressed-gta-2-music-tracks-onto-youtube/)).
- **Preserved material:** a September 1999 PSX prototype ("Hardwood Heroes" title screen) on archive.org ([GTAForums](https://gtaforums.com/topic/970832-prototype-build-of-gta2-sep1999/)) and an April 2000 Dreamcast prototype at [Hidden Palace](https://hiddenpalace.org/Grand_Theft_Auto_2_(Apr_7,_2000_prototype)).
- **Understood vs unknown:** data formats — fully understood. Script VM — fully understood (opcodes, threads, compiler quirks, working decompiler). Executable — roughly two-thirds matched by the decomp project (game loop, frontend, HUD, map rendering, script engine, car/ped classes largely mapped; sound backend and boot-to-gameplay incomplete as of the last public summary). Multiplayer/netcode — understood well enough that community patches fixed desyncs at the source level. PSX-specific format deltas — partially supported (map compressor yes; most PC editors no).

---

## 5. Notable deep-dive write-ups

- **[WIP] GTA2 Reverse Engineering — Summary** (GTAMP forum, Nov 2025, replies into Oct 2026) — the single best technical overview of the exe's internals: class inventory (Car_BC, CarPhysics_B0, Ped, Police_7B8, Fix16 16.16 fixed point, Ang16 with 0–1439 angle units), matching methodology, and engine quirks (20-slot phone-thread array, null-pointer crashes in CHECK_CHAR_CURR_WEAPON, multiplayer control-status desync cause). https://gtamp.com/forum/viewtopic.php?t=1424
- **"Finally understanding IFs, WHILEs, ANDs, ORs and ELSEs"** (GTAMP, Oct 2024) — hex-level dissection of the script VM's IF_JUMP opcode, the compiler's boolean-nesting bug, and a decomp.me C++ scratch of the handler. https://gtamp.com/forum/viewtopic.php?style=1&t=1393 and https://decomp.me/scratch/cgNfD
- **Sektor's GTA2 memory addresses** (GTAMP, t=700) — the canonical annotated memory map for v9.6f (base 0x400000) and v11.44 (base 0x3F0000): player/ped/car/sprite structures, controls bitmap, camera fields, used by trainers and speedrun timers. https://gtamp.com/forum/viewtopic.php?style=1&t=700
- **"Found PED (dummies) walking code in exe"** (GTAMP, May 2022, B-$hep) — locating the ped state machine with Ghidra + Cheat Engine; insights on the nav logic and struct-driven design. https://gtamp.com/forum/viewtopic.php?t=1352
- **GTA2 Editing Compendium** (ModDB, May 2023, LithTechGuru) — a friendly walkthrough of the STY format section by section. https://www.moddb.com/games/grand-theft-auto-2/tutorials/gta2-editing-compendium
- **Project Cerbera GTA2 tutorials** — scripting, character remaps, map optimisation (2017+). https://projectcerbera.com/gta/2/tutorials/scripting and https://projectcerbera.com/gta/2/tutorials/optimising
- **WikiGTA scripting manual + Script Compiler pages** — still the standard reference for the GTA2script language. https://en.wikigta.org/wiki/Scripting_manual_(GTA2)
- **DMA Design behind-the-scenes interviews** (GTAMP thread with YouTube interviews of ex-DMA staff; the yellow-cover Empire State Building anecdote; Mike Dailly's prototype footage). https://gtamp.com/forum/viewtopic.php?t=1354
- **h0x91b's YouTube channel** — live reverse-engineering streams of GTA2 (Ghidra, Cheat Engine, building gta2-resurection). https://www.youtube.com/c/h0x91B
- **Adjacent but method-relevant:** OpenGTA's city-renderer deep dive for **GTA1** (Ghidra-driven, addresses + algorithm-level explanation of the projection/rendering the two games share) — an excellent model for a future GTA2 equivalent. https://opengta.emdzej.pl/render
- Non-technical but recent: "GTA2 — A Necessary Bridge" video essay (Dec 2025) for cultural context. https://www.youtube.com/watch?v=DhJCkDu0LJ4

---

## Relevance to your copy of the game

Your `GTA2INSTALLER.exe` (Rockstar Classics, Dec 2004) is the v9.6F build — the same input the tooling ecosystem standardised on: OpenGTA2 and sp00nznet's recompilation download and unpack exactly this installer; gta2-rtx-remix is tested against v9.6 and v11.44; Sektor's memory-address docs cover both. Two practical notes from the research: (1) the Rockstar Classics `gta2.exe` is **TAC-packed in every known distribution**, which blocks naive static unpacking — runtime dumping is required, which is why the gta2_re decompilation targets the 10.5 freeloader exe instead; (2) for modding rather than RE, the community standard is installing the unofficial **v11.44** alongside it.

---

## Bottom line

GTA2 is in unusually good shape for a 27-year-old game: its data formats are completely documented (partly from *official* DMA documentation), its script language has a fully working compiler *and* a 100%-functional decompiler since 2024, and an active four-person team has matched over two-thirds of the executable in an RE3-style decompilation as of October 2026 — with no original source ever leaked or released, so all exe knowledge comes from disciplined disassembly. The modding community on gtamp.com is small but demonstrably alive (patches, a new map editor, RTX path tracing and a multiplayer lobby all updated in 2025–26). If your project needs deep engine work, gta2_re + the GTA2 RE Hub are the vehicles to join or build on; for content modding, the toolchain is already mature end-to-end.
