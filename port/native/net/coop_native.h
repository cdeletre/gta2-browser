// See coop_native.cpp. Multiplayer build only (gta2-mp).
#pragma once

#include "lobby.h"

namespace gta2net
{

// The lobby's match starts: sets up the core's network session (before the game starts)
// and connects the core's game messages to the lobby.
void SetupCoop(Lobby* lobby, const Json& match);
// Once per main-loop iteration while the match runs: sync checks and hold-ups.
void WatchCoop(double nowMs);
void CoopStatus(int* lastSyncTick, bool* drifted);

} // namespace gta2net
