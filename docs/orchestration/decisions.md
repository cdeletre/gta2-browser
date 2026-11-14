# GTA2 implementation decisions

Version 1.1, 12 October 2026. These decisions are binding implementation inputs
for the orchestrator and workers. They supersede open choices in specification
1.0. They are engineering decisions, not claims of tested game behavior.

Workers implement and test the selected design. They do not research alternative
engines, frameworks, transports, authority models, storage systems or codecs.
Reopening a decision requires a failing reproduction, its impact on a required
outcome, and an orchestrator decision. An unverified external fact stays an
explicit acceptance dependency; it is not replaced by an invented answer.

## D01 Product and reference version

Target the locally supplied **Rockstar Classics 9.6F data profile** for v1.
Use the pinned 10.5 reconstruction as implementation material and the locally
installed 9.6F original as the gameplay oracle. Differences are resolved in
favor of 9.6F observable single-player behavior, except the modern display and
co-op deviations specified here. Support one exact, hashed asset profile first.
Reject other installations clearly; do not add compatibility heuristics.

T02 generates the concrete per-file manifest from local extraction; file hashes
cannot be selected in advance. Compare candidates to this manifest. Core map,
style and script hashes must match for a room; text/language and converted
media are separate profile fields. The first release requires one complete
language profile, English, with correct original strings and glyphs. Existing
other-language assets can be retained but are not a new translation project.

Use an existing **Windows 11 x64 host or VM** for original-game reference
capture. Do not spend implementation time trying Wine on this Mac, recompiling
VC6, unpacking the original EXE or acquiring 10.5. If no Windows host exists,
record the exact missing reference capability and escalate access once; continue
synthetic tests and browser implementation independently. G1 requires original
game smoke/capture, not a second native build of the reconstruction. **G2 is
the first required standalone whole-game execution, in the browser.**

Reference capture uses scripted keyboard/mouse input plus video and observable
event/state annotations. Do not start with memory injection or assume original
replays can drive the port. Produce one original walk/drive/mission-start capture
first, then extend only for identified parity questions. [Audit SG-SYSTEMS-01,
DEPS-04, BUILD-ENV-01]

## D02 Language tooling and dependency boundaries

Use **C++17, wasm32, Emscripten 6.0.12, CMake and Python 3**. Preserve fixed-width
game arithmetic. Keep the upstream checkout pristine; apply a versioned local
patch series into ignored `research/port-work/core/`. Record each source's
provenance. Do not import the Windows hook loader, Detours, DirectPlay, MASM,
original EXE, Miles DLL or Bink DLL into the browser target.

Use **plain JavaScript ES modules, JSDoc typedefs, HTML and CSS** for the shell,
renderer, media and networking. No React, game engine, TypeScript build pipeline,
ECS rewrite, Embind object graph, SDL compatibility layer or bundler for v1.
Use Node's built-in test runner for pure JavaScript and CTest for C++ unit tests.
Use Playwright for automated browser scenarios; its WebKit results are a
preflight, not a substitute for release tests in actual Safari.

Use Node **26.7.0** for development/signaling and **ws 8.21.0**, already present
locally, declared as a direct exact dependency in the signaling package. Do not
rely on REA's transitive dependency at runtime. T00 locks the browser test-tool
version once and records it; that is dependency resolution, not a framework
selection task. Security fixes may update pinned dependencies through review.

Create three owned command entry points in T00: `python3 scripts/build_port.py
--preset debug|release`, `python3 scripts/check_port.py --suite <name>`, and
`python3 scripts/serve_port.py --host 127.0.0.1 --port 8765`. Suite names are
`numeric`, `assets`, `save`, `render`, `slice`, `network`, `campaign`, `perf` and
`release`. Missing implementations must exit nonzero with a precise message.
Do not create green placeholder tests. These commands are specified, not yet
implemented by this decision document.

## D03 Runtime and language boundary

One Wasm instance runs on the browser main thread, with no pthreads, SharedArrayBuffer,
Asyncify, WebGPU or OffscreenCanvas in v1. Emscripten emits an ES module factory.
Initial build settings: `-sMODULARIZE=1 -sEXPORT_ES6=1 -sALLOW_MEMORY_GROWTH=1
-sINITIAL_MEMORY=134217728 -sMAXIMUM_MEMORY=1073741824 -sFORCE_FILESYSTEM=1`.
Use `-O0 -g` plus assertions in debug; `-O2` in release; **no fast-math**. Keep
native sanitizer targets separately. Do not use `-fshort-wchar` across libc:
replace game text representations with explicit UTF-16 code units and convert
at boundaries. Remove MS annotations or use a narrow compatibility header;
never declare Windows APIs successful when they do nothing.

Use a small `extern "C"` boundary with numeric handles and pointer/length
buffers. Required operations: create/destroy a session, mount the validated
profile, begin/end a district, submit input, advance one tick, expose a render
view, drain events, encode/decode a save, encode/apply a network view. Return a
status code plus a retrievable error string. Object pointers never cross as
JavaScript object identity or network IDs. JavaScript reacquires heap views
after any allocation that can grow memory; no retained stale typed-array views.
Export only required functions. [Emscripten interaction reference]

The C++ core emits a versioned render view; JavaScript performs WebGL calls.
Batch vertices and commands in shared linear-memory buffers and cross the
language boundary once per frame, not once per sprite. JavaScript owns browser
APIs and asynchronous I/O. The game tick contains no browser promise waits.

## D04 Simulation cadence and overloaded frames

Fix normal simulation at **33 ms per tick**, matching the selected source path
in `Game_0x40::GetFrameDurationMs_4B8BB0`. Disable the 11 ms fast mode and legacy
multiplayer speed options for v1. Do not substitute 60 simulation updates or
change the original `30 * seconds` script-timer conversion. Tick index and RNG
advance only when a game update actually runs. Original timing parity remains
a required capture test, not an assumption proved by this source constant.

Drive rendering with `requestAnimationFrame`. Accumulate monotonic elapsed
milliseconds and run at most **four simulation ticks per callback**. Render
between previous/current states using accumulator/33. If more than four ticks
remain overdue, enter a visible performance pause and require an explicit
resume; do not discard authoritative ticks or simulate unbounded catch-up.
Pause SP on focus loss and page hide. In co-op only host page hide pauses the
session; an unfocused guest sends neutral inputs and keeps receiving state.
On resume clear stale input and obtain a full state baseline. Network liveness
and reconnect deadlines use elapsed wall time, independent of paused game ticks.

## D05 Numeric compatibility and RNG

Keep Fix16's **14 fractional bits**, signed 32-bit storage and signed 64-bit
intermediates. Addition/subtraction/negation use explicitly defined modulo-2^32
bit operations; reconstruct signed values without undefined signed overflow.
Multiplication is floor((int64(a) * b) / 16384), including negative products,
then retains the low 32 bits. Division is truncation toward zero of
`(int64(a) * 16384) / b`, followed by the same narrowing. Replace negative left
shifts with multiplication. Division by zero is a named fatal simulation
error with a saved reproduction; returning zero is forbidden. For signed
integer `INT_MIN / -1`, implement the selected wrap result explicitly and test
it, rather than executing undefined C++ division.

Retain reconstructed trig/sqrt formulas and initialization order initially;
do not replace them with prettier floating-point physics. Round/narrow in
explicit helpers. Deterministic hashes concern the same supported build and
logical state; v1 does not require cross-CPU bit-identical speculative physics.
Workers produce boundary tests. Original disagreement, especially x87 rounding,
goes to the specialist with a minimal case, not to an open-ended rewrite.

Use an explicit **MSVC-style 32-bit LCG** for the reconstructed CRT RNG:
`state = state * 214013u + 2531011u`, result `(state >> 16) & 0x7fff`, seeded
with 1 at the original initialization point. Preserve original call order and
modulo operations; do not substitute libc `rand`, JavaScript `Math.random` or a
new statistically superior generator. Save the state and call count. Seed-1
fixture begins `41, 18467, 6334, 26500, 19169`. This is the selected compatibility
algorithm, **not yet a measured original-EXE equivalence claim**. T05 checks the selected algorithm fixtures; T12 checks
the original/reference sequence before asserting parity. [Audit BUILD-UB-01,
source `fix16.cpp`, `fix16.hpp`, `rng.cpp`]

Use explicit little-endian readers for assets. A file offset is `uint32_t`,
never a C++ pointer loaded from disk. Validate bounds before resolving offsets;
preserve on-disk layout in reader structs separately from live objects.

## D06 Asset import and memory

The supported browser archive is a project-local **`.gta2pack`**, produced by
`python3 scripts/prepare_assets.py --source game --output
research/runtime-data/classics96f.gta2pack`. Directory/file selection is also
supported. No arbitrary installer/ZIP/RAR extraction in the browser and no
download service. Keep all scripts/tools asset-free.

Pack format v1 is deliberately simple: 8-byte ASCII `GTA2PK01`, uint32 LE JSON
manifest byte length, uint32 LE entry count, UTF-8 JSON, zero padding to an
8-byte boundary, then concatenated file payloads. Manifest entries contain
normalized relative path, role, payload-relative offset, byte length and SHA-256;
also record profile/schema and conversion provenance. JSON integers must be
safe integers. No compression or encryption. Limits: 2 GiB total, 512 MiB per
entry, 4,096 entries, 1 MiB manifest. Reject overlaps, duplicate/case-colliding
paths, absolute/parent paths, overflow, bad hashes and unsupported schemas.
If actual original files exceed a limit, return their sizes to the orchestrator;
do not silently drop them or lift all limits.

Persist imported files as browser IndexedDB Blobs keyed by profile/hash only
after validation. Hash one bounded file at a time. Mount only the current
district's engine-required files into Emscripten MEMFS before starting its tick
loop; stream music/video through Blob URLs outside Wasm. Unmount/free the old
district on transition. Do not copy the entire installation into Wasm. Use
separate IndexedDB stores for original-file cache, converted media, preferences
and saves. Do not use IDBFS auto-sync for save transactions. [Filesystem reference]

Import and media preparation may be slow, but must show progress and allow
cancellation. They are outside steady-play frame measurements. No game-derived
files, generated pack or converted media enters a public bundle or external CI.

## D07 Graphics and modern presentation

Use a **pure JavaScript WebGL2 renderer** consuming the C++ render view.
Convert original palette/index/sprite-delta resources to RGBA on load or
material change; cache by style hash, resource/delta, palette and alpha mode.
Use 2048×2048 atlases with duplicated one-pixel borders, nearest sampling and
clamp-to-edge. Start with a **128 MiB texture-cache budget**; evict only unused
resources and upload incrementally outside hot draw loops. Use per-frame vertex
and index buffers; merge only consecutive commands with identical state.
Preserve draw order, depth/alpha rules and blend factors from the reference;
no global material sorting of transparent sprites. No new lighting model.

The default world view preserves the original **640×480 reference projection's
vertical span** and expands horizontal coverage for aspect ratio. At 16:9 its
reference-space width is 480×16/9; render at the actual selected 1080p/1440p
pixel dimensions. Preserve game art; no AI upscaling or replacement textures.
HUD/text has its own orthographic layer, default UI scale 1.0, with 0.75, 1.0,
1.25 and 1.5 options. Letterbox cutscenes at their original aspect.

Separate a canonical 4:3 gameplay camera/activation rectangle from presentation
culling. Widescreen must not activate missions, alter AI range or accelerate
despawns. Co-op uses a canonical rectangle around each active player; union
those regions for simulation activation. Existing entities/static world may
be drawn in the expanded visual margin without changing simulation decisions.
Test edge pop-in and camera locks explicitly; keep this policy instead of
changing simulation to disguise a rendering defect.

**Outcome (13 October 2026, patch 0010).** Edge pop-in was measured and is visible:
- At 16:9 the view reaches 5.24 blocks either side, against a 4:3 half-width of 4.5.
  Pedestrians spawn at 5.0 and cars at 5.5.
- The game's despawn and "on screen" checks use the same rectangle.

The D17 presentation activation margin is therefore implemented and **on by default
whenever the view is wider than 4:3**. It widens the rectangle horizontally by the extra
visible width, restoring the original headroom ratio. It is a recorded deviation,
`?margin=0` turns it off, and at 4:3 it does nothing. In co-op it applies identically to
every player.

## D08 Audio and video

Use Web Audio `AudioBuffer` playback for effects/vocals, with explicit gain,
pan, loop offsets and playback rate from game events. Cache decoded samples
with a **64 MiB budget**. Keep at most 64 concurrent voices; preserve player
weapon/mission speech/UI voices first, then nearest loudest ambient voices.
Every channel has stop/end lifecycle handling; duplicate network events cannot
play it twice. Use an HTML media element for radio/music streams, connected to
the audio volume graph. Start/resume only after a user gesture.

Use a **local FFmpeg preparation step** for codecs browsers do not decode,
especially Bink. Generate MP4/H.264 (`yuv420p`) plus AAC with `+faststart`,
preserving aspect, original timing and audio; no new interpolation/upscale.
Retain source and output hashes and FFmpeg version/build configuration. The
browser plays the generated local Blob with an HTML video element. If the
selected FFmpeg build lacks required decoder/encoder support, report the exact
missing capability; do not research/implement a browser Bink decoder.

The pack preparation tool invokes an explicitly configured local FFmpeg binary;
it does not bundle FFmpeg, download it automatically or send media to a server.
FFmpeg was not found on this host during this specification update. Supplying
that local tool is an environment prerequisite, not an undecided codec strategy.
Every required video/audio item must still pass before release. FFmpeg documents
Bink decoding; browser playback of the prepared files remains a test gate.
[FFmpeg format and CLI references]

## D09 Saves and recovery

Use native browser save schema v1, independent of original SVG binary layout.
Original SVG import/export is **out of v1 scope**; preserve original files.
Support all original save opportunities with correct costs/eligibility. Do not
add arbitrary mid-tick quicksave. Snapshot at a completed authoritative tick,
pause mutation while encoding, and save logical entities, references by stable
ID, script threads/program counters/locals, player/progression, timers and RNG.
Never dump raw Wasm memory or C++ object bytes. Enumerate concrete fields from
source mechanically; unresolved semantic fields get a focused specialist ticket.

IndexedDB database `gta2-browser-v1` uses immutable save revisions and a slot
head record. Commit revision data, checksum and head in **one transaction**;
resolve “Saved” only after transaction completion. Retain current plus previous
revision. Verify checksum/profile/schema on load; preserve a corrupt/newer save
for export and offer the previous valid revision. Keep ten named slots per
campaign and a separate rolling autosave. Autosave between missions/districts
only. Namespace keys include profile, mode and campaign UUID.

Export a JSON metadata file plus binary state in the same simple framed format
used by the pack, with separate `GTA2SV01` magic. Reject mismatched profiles.
SP-to-co-op import is supported only between missions: clone into a new co-op
campaign, retain shared progress, initialize guest actors on joining and never
overwrite the SP source. Co-op-to-original or co-op-to-SP conversion is absent.

## D10 Authority and client implementation

Use **one browser host and up to three guests**. Only the host advances world
simulation, mission VM, RNG, collisions, police, damage and progression. Guests
run a presentation-only mode: load local static assets and apply authoritative
render/world views. They never run the full gameplay update loop. Extract a
pointer-free render-view adapter so client presentation does not require hidden
game logic or fake mission state.

**No client physics prediction or rollback in v1.** Clients interpolate
authoritative positions and angles; provide immediate UI/input feedback only.
Do not predict damage, vehicle collisions, pickups or mission completion. This
removes a second physics authority and avoids requiring exact cross-browser
lockstep. Tests of equal snapshot state compare the same applied tick, never
the interpolated frame. If measured responsiveness misses the release criterion,
escalate the specific trace; workers may not invent prediction as a local fix.

Host input uses the same validation/queue path as guest input. Stable player
slots are 0–3, and entities use uint32 ID plus uint32 generation. IDs are scoped
to a campaign/session epoch; generation prevents stale references after reuse.
Assign IDs on authoritative creation, never from pointers or array order alone.

## D11 Transport and room service

Use **WebRTC data channels in a host-star topology**; guests connect only to the
host. Use a Node/ws WebSocket service for signaling and ephemeral private rooms.
No gameplay WebSocket fallback, peer mesh, distributed simulation, public lobby,
account service or database in v1. Room state is in server memory; server restart
ends room discovery and clients reconnect or end clearly, never invent a save.

Create three channels with in-band negotiation: `control-v1` reliable ordered;
`input-v1` unordered with `maxRetransmits: 0`; `state-v1` unordered with
`maxRetransmits: 0`. Do not also set `maxPacketLifeTime`. Use JSON for small
control messages, binary LE for inputs/snapshots. Control channel carries
handshake, full baselines, critical events, pause/reconnect and heartbeat.
Reliable delivery does not remove the need for event IDs and deduplication.
[WebRTC specification]

Invite URL uses a fragment containing cryptographically random 128-bit room
join token; send it only in the authenticated signaling handshake. A separate
host capability authorizes room changes. Never log either. Use origin allowlist,
64 KiB maximum signaling message, four player slots, 30 messages/second per
connection with bounded burst, and 60-second unused-room expiry. HTTPS/WSS in
deployment, localhost HTTP/WS in development. Store no assets or campaign saves
on signaling service.

Use **coturn**, separately configured, for relay fallback. Local tests first use
host candidates; release explicitly exercises relay-only ICE between two
internet sites. TURN credentials are short-lived and supplied by the service;
no static shared secret in the client. Deployment shape is a small persistent
container/VM service behind HTTPS plus coturn. No serverless invocation per
socket, hosting-provider comparison or infrastructure purchase in worker tasks.

## D12 Inputs snapshots and bandwidth failure

Inputs are sent on change and every **33 ms**, carrying the latest frame plus
two preceding frames. Fields: protocol version, session epoch, player slot,
input sequence, client-estimated host tick, button bitset, quantized aim values.
Validate ownership, bounded values and monotonically new sequences. Host applies
the newest valid frame available at the next tick; there is **no rollback or
two-tick artificial input delay**. Treat an edge action once by sequence/action
ID. After 250 ms without fresh input, neutralize it. Reject impossible future
tick claims; never let a client choose how many simulation updates run.

Host emits a snapshot **every simulation tick, 33 ms**, replacing the earlier
20 Hz proposal. Render guests **two ticks, 66 ms, behind** the newest estimated
host time; interpolate only between known states. Extrapolate at most one tick
using last velocity, then freeze that entity until fresh state. This is visual
extrapolation, not collision/gameplay prediction. Hide/despawn only on explicit
authoritative removal, not because one snapshot packet was lost.

Initial/full baseline contains every live dynamic entity required by the shared
world view, player HUD/progression, world modifications and relevant effect state;
static assets are identified by local profile hashes. Keep all dynamic entities
in v1; no interest-management framework. Deltas are computed against the last
snapshot **acknowledged as completely applied** by that client. Apply a delta
atomically only if its baseline exists. ACK applied sequence on control, at most
once per tick. Keep 64 snapshots per connection; if baseline is unavailable or
the client requests resync, send a fresh reliable baseline.

Binary state header: magic `G2N1` (4 bytes), protocol uint16, kind uint16,
epoch uint32, sequence uint32, tick uint32, base sequence uint32, part index
uint16, part count uint16, payload length uint32: **32 bytes total**, LE.
Use sequence 0 as “no baseline”; start live sequences at 1, never wrap within a
session. Payload is ordered records: operation uint8, type uint8, flags uint16,
entity ID uint32, generation uint32, byte length uint32, then typed payload.
Type-specific field schemas are explicit; no raw structs. Unknown record types
reject the update and request version-compatible resync, not guessed parsing.

Chunk messages to at most **12 KiB payload**, with at most 512 parts and 4 MiB
reassembled snapshot. Discard incomplete unreliable updates after 200 ms. Full
baseline timeout is 10 seconds. Buffer at most two assembling unreliable
snapshots and one full baseline. On state-channel bufferedAmount above 256 KiB,
skip generating that tick's replaceable snapshot; send the next delta from the
last ACK. On reliable buffering above 1 MiB, stop sending bulk state, signal
connection trouble and disconnect after 10 seconds without recovery. Never
drop reliable mission events or block the host simulation on a slow client.

Critical events carry epoch + monotonically increasing uint32 event sequence,
tick, event type and typed payload. Deduplicate using highest contiguous applied
sequence; reliable ordered delivery and baseline event watermark support resume.
Budget target is **at most 1 MiB/s per guest** steady host upload; measure it.
Exceeding the target invokes a bounded packing/profile task, not silent removal
of world entities or unreviewed interest management. [Protocol is a design
decision; channel behavior comes from WebRTC, not measured GTA2 performance.]

## D13 Cooperative mission and progression semantics

There is **one active campaign mission**. First eligible trigger in host tick/
player-slot order wins; other starts return “Mission already active.” The
initiator is the mission actor until mission end. Legacy implicit-player
opcodes bind to the actor; explicit valid player targets remain explicit.
Never replace every reference with host slot 0. New adapters select `actor`,
`any`, `all` or `team` in a per-mission declarative table. Default is `actor`;
workers do not guess a more convenient rule for a failing mission.

Score/cash, gang respect and progression are shared. A reward updates the shared
ledger **once**, visible to everyone; it is not multiplied by player count.
Purchases/save fees debit that ledger once. Health, wanted level, weapons and
ammunition are individual. Ordinary pickups go to the first valid claimant;
scripted mission cargo belongs to the mission actor unless the adapter explicitly
assigns it to a team vehicle/object. Mission completion is authoritative and
idempotent. Preserve solo behavior when there is only one player.

Actor arrest/death follows the original mission's failure path once. Non-actor
arrest/death uses the original player's loss/penalty operation once, then a
five-simulation-second respawn without creating an extra mission failure.
Safe spawn search uses fixed compass order around the actor, tile radii 2, 4,
8 and 16, with map support/collision/hazard checks; if no valid candidate exists,
use the district's original hospital/police spawn as appropriate. Respawn must
not trigger mission-start zones. Missions with a locked actor sequence defer
guest respawn until that sequence ends; never resurrect inside an occupied car.

Guests cannot independently advance cutscene/control-lock sequences; apply
actor camera/control locks to the actor, and show an event/status to others.
Full-screen story video pauses world simulation for everyone; host skip ends
it for everyone. Mission-specific hard locks require a catalog entry and test.

## D14 Joining disconnection and campaign ownership

New players join only **between missions**, avoiding undefined “safe checkpoint”
detection. Before admission compare build/protocol/core asset profile. A guest
imports its own assets before joining; the host never transfers them.

Keep a disconnected slot for **60 wall-clock seconds** with an unguessable
resume token. For a non-actor, neutralize inputs and remove it from `all`
participant quorum after two seconds; retain logical inventory/state for resume.
For the actor, pause the mission while attempting reconnect. After 60 seconds,
run one normal mission-failure path and resume the remaining session. If the
host disappears, guests stop gameplay immediately after a five-second heartbeat
timeout and offer return-to-menu. No election or host migration.

Host page hide pauses the room; on resume send a full baseline and clear input
edges. Campaign saves belong to the host. Guests retain local settings only and
are told this before joining. A departing host leaves its last completed save
available to restart a room. No cross-user account/progress synchronization.

## D15 Validation thresholds and optimization order

Retain the specification's named hardware/browser matrix and thresholds as
**binding release targets**, not provisional suggestions. Performance workers
implement measurement against them; they cannot select an easier GPU or reduce
render scale. Run the first G2 comparison in Chrome and Firefox. Actual Safari
and the Windows matrix remain mandatory before release; WebKit automation alone
does not satisfy Safari. Test baseline machines must be identified, not assumed
available. Missing access is a release verification blocker, not a design choice.

Fix performance in this order: redundant work/allocations, draw batching and
texture churn, data movement between JS/Wasm, visibility submission, then
measured core hotspots. Keep required entity simulation, mission coverage,
resolution and audio. No threads, lower simulation rate, dynamic resolution,
hidden dropped gameplay or engine rewrite as a worker-selected optimization.
If these steps cannot meet target, send profiles to the orchestrator for a
specific architecture amendment. The 160–550M planning range is not reduced
merely because choices are now documented; measure the actual savings at G2.

## D16 Distribution and scope boundaries

Proceed with authorized local analysis, tools and implementation against local
inputs. No source license is inferred from public availability. Unknown core
reuse permission remains a **distribution blocker**; only actual permission
evidence can close it. Exclude injection dependencies rather than asking workers
to negotiate their licenses. Publish neither original/converted assets nor a
local game-derived pack. Do not promise a distributable product until this gate
passes. If permission fails, the orchestrator must obtain a new scoped plan;
a from-scratch rewrite is not an automatic worker fallback.

## D17 Modern camera (user request, 13 October 2026)

The user wants the original speed-driven camera zoom fixed, not only preserved.
Source facts:

- `Camera_0xBC::ApplyCarVelocityCameraOffset_436200` adds car speed directly
  to camera height above a threshold. It also adds a look-ahead offset of 180
  or 240 reference units in the direction of travel, shrunk while
  `field_44_suspicion` is set.
- `SmoothApproach_4F7540` eases height and zoom once per 33 ms tick.
- `IsCoordsPosVisible_435A70` and `UpdateBoundaries_435B90` define a fixed
  640×480 view. `Char_Pool`, `Car_BC`, `SpriteGrid_400` and `Hud` consume it,
  so the camera rectangle is **simulation input**, not just presentation.

Decision:

- Ship two camera modes. **Classic** is the reference camera of D07,
  interpolated per D04. **Modern** is the default for play.
- Modern camera is **presentation only**. It derives from the canonical
  camera state each tick and never writes back into `Camera_0xBC`, so spawning,
  activation and mission logic stay on the canonical rectangle.
- Modern camera behaviour:
  - Height comes from smoothed speed with a cap and hysteresis, instead of
    raw per-tick speed.
  - It is critically damped in display time (60+ Hz), not per tick.
  - Look-ahead is continuous rather than switched by 45° sectors.
  - The player can set the zoom bias in options.
- Where Modern shows more world than the canonical rectangle, D07's
  expanded-margin rule applies: existing entities are drawn there, nothing new
  spawns early. Pop-in at the canonical edge is measured. If it is visible, the
  approved fix is a configurable "presentation activation margin". That is a
  recorded gameplay deviation, off in Classic, applied identically to every
  player in co-op.
- **The primary target is zoom pumping** (user, 13 October 2026), especially
  noticeable at low resolution. The cause is in the source: the height target
  is raw instantaneous speed, so every bump, corner, brake and collision
  re-targets the zoom.
  - Modern filters speed with a time constant of about 1 s.
  - It zooms out over about 1 s when you speed up, but waits about 1.5 s
    before zooming back in, so a quick brake or corner causes no change.
  - It ignores small speed changes entirely (deadband), and the camera
    height has a ceiling.
  - Sprites are sampled sharp-but-smoothed at texel edges ("sharp bilinear")
    while zoom changes, so pixels don't crawl during continuous scaling.
    This amends D07's plain nearest-neighbour sampling, for Modern mode only.
- **Acceptance test:** a scripted drive (accelerate, corner, brake, bump),
  recorded in both modes.
  - Report zoom direction reversals per minute and peak zoom rate.
  - Modern must show no zoom reversal caused by a speed change shorter than
    the hold time.
  - Classic must match the original capture.
- Order: the G2 slice ships Classic only, so there is a correct baseline.
  Modern lands immediately after G2 and before campaign-wide QA, so all
  campaign testing happens with the camera players will actually use.

**Outcome (13 October 2026, patch 0011).** Modern ships as the default.
- **Implemented:**
  - a presentation-only filter on the canonical height: rises in about 1.2 s, holds
    1.5 s, 0.3-block deadband, ceiling of 3 blocks above canonical, snaps on cuts;
  - one filter state per updated camera, ready for co-op;
  - the activation margin covering the drawn height.
- **Measured** on one scripted drive with traffic: zoom reversals 25.9 → 10.7 per
  minute, peak zoom rate 6.0 → 3.3 blocks/s. No Modern descent started after a speed dip
  shorter than 1.5 s, except the ceiling clause after a crash.
- **Deferred:**
  - continuous look-ahead;
  - sharp-bilinear sampling;
  - display-rate interpolation (D04).

## Evidence dependencies and exact escalation

| Fact still needed | Prescribed action | Work that continues |
|---|---|---|
| Concrete 9.6F file hashes/formats | T02 extracts and inventories the supplied installer; compare against the selected profile | Synthetic parsers, shell and core adapters |
| Original runtime behavior / numeric sequence | T03 captures on the selected Windows host; specialist resolves a specific mismatch | Portable arithmetic and synthetic fixtures |
| FFmpeg executable with required capabilities | Configure one local build; save version and codec probe; do not choose another media architecture | PCM effects and browser media integration with synthetic clips |
| Actual mission field/serialization/opcode semantics | Worker enumerates source fields; specialist answers a bounded unresolved semantic question with source/runtime evidence | Other independent fields/missions |
| Exact test devices and internet relay access | Provision/configure the selected targets through the authorized environment | Local/browser/unit tests |
| Permission to distribute selected source | Record attributable permission or keep release blocked | Authorized local development |

These are evidence/access checks, not requests for lower-cost workers to do
broad research. A worker escalation includes one reproduction, relevant paths,
observed contradiction, the binding decision ID and the exact fact missing.

## Source notes

Local starting facts are in [the pinned audit](../gta2-re-build-audit-2026-10-12.md)
and its evidence ledger. API references checked on 12 October 2026:

- [Emscripten interaction reference](https://emscripten.org/docs/porting/connecting_cpp_and_javascript/Interacting-with-code.html): exported C calls and linear-memory boundary; exact settings also inspected in the installed 6.0.12 `src/settings.js`.
- [Filesystem reference](https://emscripten.org/docs/api_reference/Filesystem-API.html): MEMFS is in-memory; persistence needs a separate path. Our IndexedDB transaction design is a project decision.
- [WebRTC specification](https://www.w3.org/TR/webrtc/): channel ordering and partial reliability. Message layouts, limits and authority above are this project's selected protocol.
- [FFmpeg formats](https://ffmpeg.org/general.html) and [CLI reference](https://ffmpeg.org/ffmpeg.html): Bink decoding and local conversion tooling. Actual source-file decode and target-browser playback are still required tests.

## D18 Map screen and discovery (user request, 13 October 2026)

The user asked for a full map with waypoints (M), and for points of interest to stay
hidden until found.

- **Decision:** an optional shell overlay, recorded as a non-original feature.
  - It reads game state only. The one exception is pausing the main loop while the map
    screen is open.
  - The map image and markers come from the player's own files and the live object
    pool.
  - **Third-party map sites (e.g. MapGenie) are not used.** Their imagery and marker
    data are their content and their terms forbid scraping. The game data is also more
    exact.
- **Discovery is the default**, with a "show everything" toggle.
- M was unbound in the original controls. While a level is running, the shell takes M
  and Esc only when the map is open.

## D19 Getting the game files on a hosted page (13 October 2026)

The user wanted a hosted copy to fetch the game files for players automatically.

- **Rejected: serving the game files from our own server.** It redistributes Rockstar's
  files, which Rockstar's terms do not allow (README, AGENTS.md).
- **Chosen, for private testing only (user: "it will not be distributed"):**
  - a one-click download of Rockstar's own 2004 installer, from the Internet
    Archive straight into the player's browser;
  - it is verified by SHA-1, then unpacked and kept in the browser.
  - The port's host never handles game files. This mirrors how other GTA2 projects
    fetch this installer.
- **The local server's copy of `game/` and the folder picker remain.** The picker is
  the route for anyone with their own copy.


## D20 Co-op: free roam, lockstep, peer-to-peer without our own server (13 October 2026)

The owner decided (13 October 2026):
- **Scope:** free-roam co-op first (friends in a story district, no shared missions).
- **Sync:** **lockstep.** Every player runs the full game and only inputs are exchanged.
  This is how GTA2's own network game works (`do_network_and_local_inputs_4DAF30`).
- **Supersedes D10's host-authority and snapshot design** for this phase. WebAssembly
  gives the same results on every browser, which removes D10's main objection.
- **No server of ours.** "Something open that anyone can host; others join and share
  bandwidth instead of a server." This supersedes D11's Node signalling service and
  coturn.

Design:
- **Transport.** WebRTC data channels directly between players, with inputs only
  (a few bytes per tick).
  - Peers find each other through Trystero (MIT), using public WebTorrent trackers or
    Nostr relays, with a room code and password.
  - Public STUN only. Players behind symmetric NAT may need a public TURN relay
    (optional, user-configurable).
  - BroadcastChannel is used for two-tab local tests.
- **Engine.** The excluded DirectPlay `NetPlay` is replaced by a port implementation of
  the calls used in play. The blocking input wait (`Net_Receive_Players_Inputs_4DA9F0`)
  becomes resumable across browser frames.
- **Match start.** The game starts directly in network mode (`-N` equivalent) with the
  multiplayer map and script set through the game's own debug settings (`mapname`,
  `scriptname`, `stylename`), skipping the DirectPlay lobby. Industrial
  (`bil-multi.gmp`, `Industrial-NP.scr`) comes first.
- **Determinism checks.** Use the original sync check (`bDo_sync_check`, which compares
  an RNG value every tick). Anything that reads wall-clock time inside the simulation is
  a desync bug to fix.
- **Later.** Add input delay for internet latency, and free-roam scripts for Downtown
  and Residential, compiled with DMA's MIT-licensed `miss2`.

Implemented (13 October 2026, patch 0017, `port/platform/net_web.cpp`, `web/net.js`):
- **Lobby** at `?coop`: host a room (6-character code) or join one, up to six players,
  host picks Downtown, Residential or Industrial. Each player needs their own game files.
- **Transport.** Trystero 0.25.4 (vendored in `web/vendor/trystero`, torrent strategy):
  public WebTorrent trackers introduce the players, then WebRTC data channels carry
  everything directly. The room code is also the password for the connection set-up.
  Public STUN only, no TURN yet. `?coop&local` uses BroadcastChannel between tabs.
- **Match start** goes through the original `-N` path. The DirectPlay calls succeed
  when the shell has configured a session, and the lobby dialog hands over the
  original multiplayer map, style and script for the player count (the districts'
  `.mmp` setups). A free-roam points game with an unreachable limit and no time limit.
  This replaces the planned debug-settings route.
- **Everyone loads first.** Players enter the match together after every game's
  files are loaded (the game gives up on a player who is not there in about 25 s).
- **Input delay.** Inputs sent at tick N are played at tick N + delay by everyone,
  including the sender. The host picks the delay from the slowest measured round trip
  (`ceil((rtt/2 + 10 ms) / 33 ms)`, 1–8 ticks). The first ticks are empty inputs.
- **Reliable, ordered channels.** The original's resends after packet loss are not sent.
- **Port features that read the local screen stay presentation-only.** The activation
  margin is off in co-op. The map screen does not pause a co-op match.
- **Hidden tabs.** A worker timer keeps a hidden tab's game ticking, since browsers
  stop animation frames there and everyone else would wait. A player waiting for
  inputs yields to the browser so the messages can arrive.
- **A match ends back at the lobby page** instead of the original's DirectPlay score
  screen.
- **Verification.** Sync was checked with per-tick hashes of the ped and car pools
  (`gta2_dev_sync_history`), not the original sync check, which input delay breaks.
  - Over BroadcastChannel: 1,130 ticks with both players moving. Both machines
    agreed on every sampled tick, with ped counts varying between 21 and 29.
  - Over WebRTC: 285 ticks, all messages delivered, checksums identical.
  - Sprite-pool hashes are not a valid check. A sprite's cached box is built lazily
    for what each screen shows.

## D21 Private game server on a VPS, hosting the game files (14 October 2026)

The owner decided (14 October 2026):
- **A VPS server** for playing with friends. It replaces D20's "no server of ours".
  - A stable connection for every player.
  - Later: up to 12 players, player-formed gangs with saved reputation, and
    gang-vs-gang play.
- **The server hosts the game files** so that installing is seamless:
  - open the link;
  - the game downloads once and is kept in the browser;
  - play.
- **Accepted risk.** GTA2 is Rockstar's copyrighted game. The owner reports that Rockstar staff said this
  is fine and accepts the risk.
- **Scope of the exception.** Small private use only, behind a password. Game files
  still never go into the repo, the GitHub export or any public page (AGENTS.md).
- **Any OS.** It must work in desktop browsers on Windows, macOS, Linux and ChromeOS.

Design (stage 1):
- **One Node server** (`server/`) serves:
  - the built game;
  - the game files under `/gamedata/` (with a manifest);
  - the co-op relay on a WebSocket at `/relay`.
- **Password.**
  - A login page sets a signed cookie.
  - An invite link (`/?invite=…`) logs in with one click.
- **Co-op through the server.** Rooms and lockstep inputs relay through the server (a
  star). There are no trackers and no WebRTC, so every network works.
- **Hosting** is a Docker container on the owner's shared DigitalOcean droplet, under
  that droplet's rules (`/opt/HOSTING.md`):
  - everything in `/opt/gta2`;
  - the image is built in CI and pulled from GHCR;
  - external `edge` network, 256 MB memory limit, no published ports;
  - one Caddy site file for HTTPS.

  The game files are mounted from `/opt/gta2/data` and never put in the image.
- **First visit.**
  - The game files download from the server with a progress bar and are cached in
    IndexedDB.
  - Later visits start straight from the cache.

