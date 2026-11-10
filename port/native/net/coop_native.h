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
// Gang Wars with a points target: the crew that reached it first ("" until one does).
const std::string& CoopWinner();
// True once the match should end (6 s after a Gang Wars win, as the browser ends it).
bool CoopEndRequested();
// A chat line for the screen (hud_net.cpp), from a player or this one.
void CoopChatLine(int slot, const std::string& text);
// A player's waypoint (blocks; set false: cleared), for the screen (hud_net.cpp).
void CoopWaypointLine(int slot, bool set, double x, double y);

} // namespace gta2net
