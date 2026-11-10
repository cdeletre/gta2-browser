#!/bin/bash
# gta2-lan in the build container (offscreen, random play), for the match measures
# (net/net_stats.cpp) and the adaptive delay (net/net_delay.cpp): a host and PLAYERS-1 guests
# (default 2 players) that find the room by name; their logs in $OUT (a.log, b.log, c.log...).
#   [PLAYERS=n] scripts/native/test_lan_stats.sh [seconds] [build dir]    (in the gta2-pm container)
set -u
SECS=${1:-40}
BIN=${2:-build/native/debug}/gta2-lan
OUT=${OUT:-/tmp/lan-stats}
PLAYERS=${PLAYERS:-2}
NAMES=(a b c d e f)
for n in "${NAMES[@]:0:$PLAYERS}"; do mkdir -p "$OUT/$n"; done
common=(env LD_LIBRARY_PATH=/usr/local/lib SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen
        GTA2_DATA_DIR="$PWD/game" GTA2_LAN_LOOPBACK=1 GTA2_NET_ROOM=stats GTA2_SOAK=1 GTA2_SOAK_AIDS=0
        GTA2_NET_STATS=${GTA2_NET_STATS:-5} GTA2_STATUS_SECONDS=1000)
"${common[@]}" GTA2_NET_HOST=1 GTA2_NET_PLAYERS=$PLAYERS GTA2_NET_NAME=Host GTA2_SAVE_DIR="$OUT/a" GTA2_CONF_DIR="$OUT/a" \
    GTA2_SOAK_SEED=1 timeout "$SECS" "$BIN" > "$OUT/a.log" 2>&1 &
for ((i = 1; i < PLAYERS; i++)); do
  n=${NAMES[$i]}
  sleep 2
  "${common[@]}" GTA2_NET_HOST=0 GTA2_NET_NAME=Guest$i GTA2_SAVE_DIR="$OUT/$n" GTA2_CONF_DIR="$OUT/$n" \
      GTA2_SOAK_SEED=$((i + 1)) timeout "$SECS" "$BIN" > "$OUT/$n.log" 2>&1 &
done
wait
for f in "${NAMES[@]:0:$PLAYERS}"; do echo "== $f"; grep -E "net stats|net: (input delay|slower|faster|in step)|drifted|co-op: [0-9]" "$OUT/$f.log"; done
