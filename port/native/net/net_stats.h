// Measures of a running match (step 0 of the lockstep work, docs/native-status.md): where the
// time goes on this console, so that a lag can be put down to the network, to Wi-Fi, or to a
// slow console. One log line every GTA2_NET_STATS seconds (5 by default, 0: none):
//   ticks played (30 a second when nothing holds the game up);
//   the time held up: the next tick due, an input for it still missing (and whose);
//   this console's work per main-loop iteration (the game and its drawing), without the
//   buffer swap (vsync: idle time, not work);
//   per player: how many ticks before it is needed their input for a tick arrives (the
//   margin; 0: just in time or waited for), and the round trip measured during the match
//   (a ping every 2.5 s, whether the stats show or not: the scoreboard's Ping column).
// Multiplayer builds only (gta2-online, gta2-lan).
#pragma once

#include "json.h"
#include "transport.h"

namespace gta2net
{

class Lobby;

// The match starts (coop_native.cpp SetupCoop).
void NetStatsStart(Lobby* lobby, const Json& match);
// A game message from a slot, as it arrives (before the core sees it).
void NetStatsGameMessage(int slot, const Bytes& bytes);
// The round trip to a player, in ms (the median of the last three pings, one every 2.5 s);
// false for this player or before the first answer.
bool NetStatsPing(int slot, int* ms);

} // namespace gta2net

// main.cpp, once per loop iteration: the game's slice ran from startMs to endMs.
extern "C" void Gta2Net_AfterSlice(double startMs, double endMs);
