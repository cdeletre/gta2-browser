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
// The match as the lobby started it (players, mode, crews), null before.
const Json& CoopMatch();
int CoopMySlot();
// A chat line for the screen (hud_net.cpp), from a player or this one.
void CoopChatLine(int slot, const std::string& text);

} // namespace gta2net
