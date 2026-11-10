#!/bin/bash
# Two gta2-lan in the build container (offscreen, random play), for the match measures
# (net/net_stats.cpp): a host and a guest that finds its room by name; their logs in $OUT.
#   scripts/native/test_lan_stats.sh [seconds] [build dir]    (in the gta2-pm container)
set -u
SECS=${1:-40}
BIN=${2:-build/native/debug}/gta2-lan
OUT=${OUT:-/tmp/lan-stats}
mkdir -p "$OUT/a" "$OUT/b"
common=(env LD_LIBRARY_PATH=/usr/local/lib SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen
        GTA2_DATA_DIR="$PWD/game" GTA2_LAN_LOOPBACK=1 GTA2_NET_ROOM=stats GTA2_SOAK=1 GTA2_SOAK_AIDS=0
        GTA2_NET_STATS=${GTA2_NET_STATS:-5} GTA2_STATUS_SECONDS=1000)
"${common[@]}" GTA2_NET_HOST=1 GTA2_NET_NAME=Host GTA2_SAVE_DIR="$OUT/a" GTA2_CONF_DIR="$OUT/a" \
    GTA2_SOAK_SEED=1 timeout "$SECS" "$BIN" > "$OUT/a.log" 2>&1 &
sleep 2
"${common[@]}" GTA2_NET_HOST=0 GTA2_NET_NAME=Guest GTA2_SAVE_DIR="$OUT/b" GTA2_CONF_DIR="$OUT/b" \
    GTA2_SOAK_SEED=2 timeout "$SECS" "$BIN" > "$OUT/b.log" 2>&1 &
wait
for f in a b; do echo "== $f"; grep -E "net stats|drifted|co-op: [0-9]" "$OUT/$f.log"; done
