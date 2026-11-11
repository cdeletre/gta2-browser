// The adaptive input delay and the pacing: see net_delay.h.
#include "net_delay.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <algorithm>

#include "lobby.h"

extern "C" const int32_t* gta2_port_coop_players(); // coop_web.cpp: 12 fields a slot, [0] in use
extern "C" int32_t gGta2PortNetInputDelay;          // net_web.cpp: the delay now (patch 0017 keeps it)

namespace gta2net
{

namespace
{
constexpr double kRoundMs = 2000;     // a report to each player, and a decision, this often
constexpr unsigned kQuietTicks = 300; // no delay change in the first 10 s (loading speeds differ)
constexpr int kDownRounds = 5;        // 10 s of a margin of a tick or more before going shorter
constexpr double kUpHoldMs = 15000;   // no shorter within 15 s of going longer
constexpr int kHeldMs = 50;           // our inputs late by more than this in a round...
constexpr int kLateRounds = 2;        // ...in 2 of a player's last 3 reports: longer (one Wi-Fi drop
                                      // is not mended by a tick more; latency that stays is)
constexpr double kInStep = 0.5;       // ticks: closer than this to the others, no pacing
constexpr double kSettled = 1.0;      // ticks: further than this, pacing first, no delay change
constexpr int kPaceEvery = 2;         // pacing: 1 ms on one tick in 2 (1.5 %)...
constexpr int kPaceMaxMs = 30;        // ...for at most 30 ms a round (0.9 tick)

struct Seen // what this machine saw of a player's inputs, this round
{
    double heldMs = 0;
    int minMargin = 1000;
    long sum = 0;
    int n = 0;
};
struct Report // what a player said of this machine's inputs
{
    int heldMs = 0;
    int minMargin = 0;
    double avgMargin = 0;
    int delay = 0;
    int round = -1; // the round it came in
    unsigned late = 0; // bit 0: the last report said late, bit 1 the one before...
};

Lobby* gLobby = nullptr;
bool gOn = false, gPaceOn = false;
int gMe = -1, gCount = 0;
int gMin = 1, gMax = 8;
Seen gSeen[kMaxPlayers];
double gMyAvg[kMaxPlayers]; // the average margin of their inputs here, last round
bool gMyAvgKnown[kMaxPlayers];
Report gReport[kMaxPlayers];
int gGood[kMaxPlayers]; // rounds in a row this player reported a margin of a tick or more
int gRound = 0;
double gRoundStart = -1;
double gLastUp = -1e9;
int gStep = 0;     // the delay change to make at the next tick (patch 0017 takes it)
int gPace = 0;     // +1: slower (ahead of the others), -1: faster, 0: in step
int gPaceLeft = 0; // ms of pacing left this round
int gPaceTick = 0; // counts ticks for kPaceEvery
double gOffset = 0; // ticks this machine is ahead of the others (their mean), last estimate
bool gOffsetKnown = false;

bool InGame(int slot)
{
    return gta2_port_coop_players()[slot * 12] != 0;
}

// How far ahead of player s this machine runs, in ticks, from the margins both ways: their
// inputs arrive here with (their delay - offset - latency) ticks to spare, ours there with
// (our delay + offset - latency).
bool OffsetTo(int s, double* offset)
{
    const Report& r = gReport[s];
    if (r.round < gRound - 2 || !gMyAvgKnown[s] || r.delay <= 0)
        return false;
    *offset = ((r.avgMargin - gGta2PortNetInputDelay) - (gMyAvg[s] - r.delay)) / 2;
    return true;
}

void Decide(double now, unsigned tick)
{
    // the pacing: in step with the mean of the others
    double sum = 0;
    int known = 0, others = 0, good = 0, worstHeld = 0;
    bool lateOften = false;
    for (int s = 0; s < gCount; ++s)
    {
        if (s == gMe || !InGame(s))
            continue;
        ++others;
        double o;
        if (OffsetTo(s, &o))
        {
            sum += o;
            ++known;
        }
        const Report& r = gReport[s];
        if (r.round >= gRound - 1) // came in this round or the last
        {
            worstHeld = std::max(worstHeld, r.heldMs);
            lateOften = lateOften || __builtin_popcount(r.late & 7u) >= kLateRounds;
            if (gGood[s] >= kDownRounds)
                ++good;
        }
    }
    if (others == 0)
        return;
    gOffsetKnown = known > 0;
    gOffset = known ? sum / known : 0;
    const int pace = !gPaceOn || !gOffsetKnown ? 0 : gOffset > kInStep ? 1 : gOffset < -kInStep ? -1 : 0;
    if (pace != gPace)
        fprintf(stderr, "gta2: net: %s at tick %u (%.1f ticks %s the others)\n",
                pace > 0 ? "slower" : pace < 0 ? "faster" : "in step", tick, fabs(gOffset),
                gOffset >= 0 ? "ahead of" : "behind");
    gPace = pace;
    // half the gap (the others mend the other half), within a round: the next estimate comes a
    // round or two late, and pacing until it says "in step" went past it
    gPaceLeft = pace == 0 ? 0 : std::min(kPaceMaxMs, static_cast<int>(lround(fabs(gOffset) / 2 * 1000.0 / 30)));
    // the delay: only once in step (out of step, a player is held up because the other runs
    // ahead, which a longer delay does not mend: it lets the other run further ahead)
    const int delay = gGta2PortNetInputDelay;
    if (!gOn || tick < kQuietTicks || gStep != 0 || (gOffsetKnown && fabs(gOffset) > kSettled))
        return;
    if (lateOften && delay < gMax)
    {
        gStep = 1;
        gLastUp = now;
        fprintf(stderr, "gta2: net: input delay %d -> %d ticks at tick %u (our inputs late in %d of 3 rounds, %d ms last)\n",
                delay, delay + 1, tick, kLateRounds, worstHeld);
        for (int s = 0; s < gCount; ++s)
        {
            gGood[s] = 0;
            gReport[s].late = 0;
        }
    }
    else if (good == others && delay > gMin && now - gLastUp > kUpHoldMs)
    {
        gStep = -1;
        fprintf(stderr, "gta2: net: input delay %d -> %d ticks at tick %u (a margin of a tick or more for %d s)\n", delay,
                delay - 1, tick, static_cast<int>(kDownRounds * kRoundMs / 1000));
        for (int s = 0; s < gCount; ++s)
            gGood[s] = 0;
    }
}
} // namespace

void NetDelayStart(Lobby* lobby, const Json& match)
{
    gLobby = lobby;
    gOn = !getenv("GTA2_NET_ADAPT") || atoi(getenv("GTA2_NET_ADAPT")) != 0;
    gPaceOn = !getenv("GTA2_NET_PACE") || atoi(getenv("GTA2_NET_PACE")) != 0;
    gMe = match["mySlot"].Int(-1);
    gCount = std::min(static_cast<int>(match["players"].size()), kMaxPlayers);
    if (getenv("GTA2_NET_DELAY_MIN"))
        gMin = std::max(1, atoi(getenv("GTA2_NET_DELAY_MIN")));
    if (getenv("GTA2_NET_DELAY_MAX"))
        gMax = std::min(15, std::max(gMin, atoi(getenv("GTA2_NET_DELAY_MAX"))));
    lobby->onMargin = [](int slot, int heldMs, int minMargin, int avgMargin10, int delay) {
        if (slot < 0 || slot >= gCount)
            return;
        Report& r = gReport[slot];
        r.heldMs = heldMs;
        r.minMargin = minMargin;
        r.avgMargin = avgMargin10 / 10.0;
        r.delay = delay;
        r.round = gRound;
        r.late = (r.late << 1) | (heldMs > kHeldMs ? 1u : 0u);
        gGood[slot] = heldMs == 0 && minMargin >= 1 ? gGood[slot] + 1 : 0;
    };
    fprintf(stderr, "gta2: net: input delay %s (%d to %d ticks), pacing %s\n", gOn ? "adaptive" : "fixed", gMin, gMax,
            gPaceOn ? "on" : "off");
}

void NetDelayMargin(int slot, int margin)
{
    if (slot < 0 || slot >= kMaxPlayers)
        return;
    Seen& s = gSeen[slot];
    s.minMargin = std::min(s.minMargin, margin);
    s.sum += margin;
    ++s.n;
}

void NetDelayHeld(int slot, double ms)
{
    if (slot >= 0 && slot < kMaxPlayers)
        gSeen[slot].heldMs += ms;
}

void NetDelayFrame(double nowMs, unsigned tick)
{
    if (!gLobby)
        return;
    if (gRoundStart < 0)
        gRoundStart = nowMs;
    if (nowMs - gRoundStart < kRoundMs)
        return;
    gRoundStart = nowMs;
    ++gRound;
    for (int s = 0; s < gCount; ++s)
    {
        Seen& seen = gSeen[s];
        gMyAvgKnown[s] = seen.n > 0;
        gMyAvg[s] = seen.n ? static_cast<double>(seen.sum) / seen.n : 0;
        if (s != gMe && seen.n > 0)
            gLobby->ShareMargin(s, tick < kQuietTicks ? 0 : static_cast<int>(seen.heldMs), seen.minMargin,
                                static_cast<int>(lround(gMyAvg[s] * 10)), gGta2PortNetInputDelay);
        seen = Seen();
    }
    Decide(nowMs, tick);
}

int NetDelayNow()
{
    return gGta2PortNetInputDelay;
}

bool NetDelayOffset(double* ticksAhead)
{
    *ticksAhead = gOffset;
    return gOffsetKnown;
}

} // namespace gta2net

// Native patch 0017 (winmain.cpp), once a tick: +1, -1 or 0 ticks of delay for this player.
extern "C" int32_t Gta2Port_NetDelayStep(int32_t delay)
{
    (void)delay;
    const int step = gta2net::gStep;
    gta2net::gStep = 0;
    return step;
}

// Native patch 0018 (winmain.cpp), once a tick: ms to add to the wait before the next one.
extern "C" int32_t Gta2Port_NetTickAdjustMs()
{
    if (gta2net::gPace == 0 || gta2net::gPaceLeft <= 0 || ++gta2net::gPaceTick % gta2net::kPaceEvery != 0)
        return 0;
    --gta2net::gPaceLeft;
    return gta2net::gPace;
}
