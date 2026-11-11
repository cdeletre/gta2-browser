// The adaptive input delay (lockstep step 2): each machine picks its own player's delay, one
// tick at a time, from what the others report on its inputs (native patch 0017 plays a
// player's inputs as a stream, so a change needs no agreement).
//
// Every 2 s each machine tells each other player how their inputs did here: the time its
// game was held up by their inputs coming in after their tick was due (only the late input is
// counted), and the smallest margin they arrived with (ticks before being needed). A player whose
// inputs were late by more than 50 ms in 2 of a player's last 3 reports goes one tick longer (one
// Wi-Fi drop alone does not: a tick more would not have mended it); one whose inputs
// had a margin of a tick or more everywhere for 10 s, and no increase in the last 15 s, goes one
// tick shorter (not below 1). Nothing changes in the first 10 s of a match (loading speeds
// differ). A browser does not report: with one in the match, the delay can go up, never down.
//
// Pacing (step 3): from the margins both ways each machine knows how far ahead of the others
// it runs; more than half a tick ahead, it waits 1 ms more on one tick in 2 (1.5 % slower), behind,
// 1 ms less (native patch 0018), for half the gap (the others mend the other half) and at most
// 30 ms (0.9 tick) a round. The delay only changes once the machines are
// within a tick of each other: a player held up because another runs ahead is mended by the
// pacing; a longer delay would only let the other run further ahead.
//
// GTA2_NET_ADAPT=0: the match's delay stays (as in the browser). GTA2_NET_PACE=0: no pacing.
// GTA2_NET_DELAY_MIN / GTA2_NET_DELAY_MAX: the bounds (1 and 8).
// Multiplayer builds only (gta2-online, gta2-lan).
#pragma once

#include "json.h"

namespace gta2net
{

class Lobby;

void NetDelayStart(Lobby* lobby, const Json& match);
// net_stats.cpp feeds these: a player's input arrived with a margin (ticks); this machine was
// held up ms waiting for a player's input; once per main-loop iteration.
void NetDelayMargin(int slot, int margin);
void NetDelayHeld(int slot, double ms);
void NetDelayFrame(double nowMs, unsigned tick);
// The delay now (ticks), for the stats line.
int NetDelayNow();
// How many ticks this machine runs ahead of the others (negative: behind), last estimate.
bool NetDelayOffset(double* ticksAhead);

} // namespace gta2net
