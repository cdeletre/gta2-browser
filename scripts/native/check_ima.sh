#!/bin/sh
# Compares the native WAV / IMA ADPCM decoder (port/native/audio/wav_ima.cpp) with the
# browser's (web/audio.js), sample for sample, on WAV files of the user's game.
# Run inside the gta2-pm container (needs clang++ and node):
#   scripts/native/check_ima.sh game/data/GTAudio/1.wav game/data/Audio/Vocals/*.wav
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/build/native/check_ima"
mkdir -p "$OUT"
clang++ -std=c++17 -O2 -o "$OUT/wav_dump" "$ROOT/port/native/tools/wav_dump.cpp" "$ROOT/port/native/audio/wav_ima.cpp"
status=0
for f in "$@"; do
  native=$("$OUT/wav_dump" "$f" "$OUT/native.f32")
  js=$(node "$ROOT/scripts/native/wav_dump_js.js" "$ROOT/web/audio.js" "$f" "$OUT/js.f32")
  if [ "$native" != "$js" ]; then
    echo "DIFF  $f: native [$native], js [$js]"; status=1; continue
  fi
  if [ "$native" = unsupported ]; then
    echo "skip  $f (unsupported by both)"; continue
  fi
  if cmp -s "$OUT/native.f32" "$OUT/js.f32"; then
    echo "same  $f: $native, $(wc -c < "$OUT/native.f32") bytes"
  else
    echo "DIFF  $f: $(cmp "$OUT/native.f32" "$OUT/js.f32" | head -1)"; status=1
  fi
done
exit $status
