// Measures of a running match: see net_stats.h.
#include "net_stats.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <string>

#include "lobby.h"

extern "C" const int32_t* gta2_port_net_debug(); // winmain.cpp (patch 0017): [3] waiting, [4] tick, [6] for whom
// window_sdl.cpp: the time spent in SDL_GL_SwapWindow so far (none in a headless build)
extern "C" double Gta2Native_SwapMsTotal() __attribute__((weak));

namespace gta2net
{

namespace
{
constexpr uint8_t kInputs = 3; // packet_subtype::PlayerInputs_3 (net_web.cpp)

struct Peer
{
    std::string name;
    int inputs = 0;
    int slackMin = 0, slackMax = 0, slackN = 0;
    long slackSum = 0;
    double waitMs = 0;  // time this console was held up with this player's input missing
    double rttLast = -1, rttSum = 0, rttMax = 0;
    int rttN = 0, rttFail = 0;
    bool pingOut = false;
};

Lobby* gLobby = nullptr;
int gMe = -1;
int gCount = 0;
Peer gPeers[kMaxPlayers];
double gPeriodMs = 5000;

double gMatchStart = -1;
double gWindowStart = -1;
uint32_t gWindowFrame = 0;
uint32_t gLastFrame = 0;
double gPrevEnd = -1;
bool gPrevPending = false;
uint32_t gPrevWaiting = 0;
double gLastSwap = 0;
// this window
int gIters = 0, gTickIters = 0;
double gWorkSum = 0, gWorkMax = 0, gTickWorkSum = 0, gTickWorkMax = 0, gSwapSum = 0, gGapMax = 0;
double gWaitMs = 0, gWaitMax = 0, gCurWait = 0;
double gDue = 0; // when the next tick is due
int gWaits = 0;
// the whole match
uint32_t gTotalWaits = 0;
double gTotalWaitMs = 0;

void ResetWindow(double now, uint32_t frame)
{
    gWindowStart = now;
    gWindowFrame = frame;
    gIters = gTickIters = 0;
    gWorkSum = gWorkMax = gTickWorkSum = gTickWorkMax = gSwapSum = gGapMax = 0;
    gWaitMs = gWaitMax = 0;
    gWaits = 0;
    for (Peer& p : gPeers)
    {
        p.inputs = p.slackN = 0;
        p.slackSum = 0;
        p.slackMin = 1000;
        p.slackMax = -1000;
        p.waitMs = 0;
        p.rttSum = p.rttMax = 0;
        p.rttN = p.rttFail = 0;
    }
}

void PingAll()
{
    for (int s = 0; s < gCount; ++s)
    {
        Peer& p = gPeers[s];
        if (s == gMe || p.pingOut)
            continue;
        p.pingOut = true;
        gLobby->PingSlot(
            s,
            [s](double ms) {
                Peer& q = gPeers[s];
                q.pingOut = false;
                q.rttLast = ms;
                q.rttSum += ms;
                q.rttMax = std::max(q.rttMax, ms);
                ++q.rttN;
            },
            [s] {
                gPeers[s].pingOut = false;
                ++gPeers[s].rttFail;
            });
    }
}

void Report(double now, uint32_t frame)
{
    const double span = now - gWindowStart;
    const uint32_t ticks = frame - gWindowFrame;
    std::string line;
    char buf[256];
    snprintf(buf, sizeof(buf), "[%.1f s, tick %u] %u ticks (%.1f/s); held up %d times, %.0f ms (max %.0f)",
             (now - gMatchStart) / 1000.0, frame, ticks, span > 0 ? ticks * 1000.0 / span : 0.0, gWaits, gWaitMs,
             gWaitMax);
    line += buf;
    std::string who;
    for (int s = 0; s < gCount; ++s)
    {
        if (s != gMe && gPeers[s].waitMs >= 1)
        {
            snprintf(buf, sizeof(buf), "%s%s %.0f", who.empty() ? " for " : ", ", gPeers[s].name.c_str(),
                     gPeers[s].waitMs);
            who += buf;
        }
    }
    line += who;
    snprintf(buf, sizeof(buf), "; loop %d x %.1f ms (max gap %.0f), work %.1f max %.1f ms, with a tick %.1f max %.1f, swap %.1f",
             gIters, gIters ? span / gIters : 0.0, gGapMax, gIters ? gWorkSum / gIters : 0.0, gWorkMax,
             gTickIters ? gTickWorkSum / gTickIters : 0.0, gTickWorkMax, gIters ? gSwapSum / gIters : 0.0);
    line += buf;
    for (int s = 0; s < gCount; ++s)
    {
        const Peer& p = gPeers[s];
        if (s == gMe)
            continue;
        snprintf(buf, sizeof(buf), "; %s: %d inputs", p.name.c_str(), p.inputs);
        line += buf;
        if (p.slackN)
        {
            snprintf(buf, sizeof(buf), ", margin %d/%.1f/%d", p.slackMin, static_cast<double>(p.slackSum) / p.slackN,
                     p.slackMax);
            line += buf;
        }
        if (p.rttN)
        {
            snprintf(buf, sizeof(buf), ", rtt %.0f/%.0f ms", p.rttSum / p.rttN, p.rttMax);
            line += buf;
        }
        if (p.rttFail)
        {
            snprintf(buf, sizeof(buf), ", %d pings lost", p.rttFail);
            line += buf;
        }
    }
    fprintf(stderr, "gta2: net stats: %s\n", line.c_str());
}
} // namespace

void NetStatsStart(Lobby* lobby, const Json& match)
{
    const char* env = getenv("GTA2_NET_STATS");
    gPeriodMs = (env ? atof(env) : 5.0) * 1000.0;
    gLobby = gPeriodMs > 0 ? lobby : nullptr;
    gMe = match["mySlot"].Int(-1);
    const Json& players = match["players"];
    gCount = std::min(static_cast<int>(players.size()), kMaxPlayers);
    for (int s = 0; s < gCount; ++s)
    {
        gPeers[s] = Peer();
        gPeers[s].name = players[static_cast<size_t>(s)]["name"].Str();
    }
    gMatchStart = -1;
    if (gLobby)
        fprintf(stderr, "gta2: net stats: every %.0f s; margin min/avg/max in ticks, rtt avg/max; input delay %d ticks\n",
                gPeriodMs / 1000.0, match["delay"].Int(1));
}

void NetStatsGameMessage(int slot, const Bytes& bytes)
{
    if (!gLobby || slot < 0 || slot >= gCount || bytes.size() < 2 || bytes[0] != kInputs)
        return;
    // bytes[1]: the tick these inputs are for (u8); the core is at tick [4] (the ticks completed),
    // and reads the inputs for tick t once it has completed t ticks
    const uint32_t frame = static_cast<uint32_t>(gta2_port_net_debug()[4]);
    const int slack = static_cast<int8_t>(static_cast<uint8_t>(bytes[1] - static_cast<uint8_t>(frame)));
    Peer& p = gPeers[slot];
    ++p.inputs;
    ++p.slackN;
    p.slackSum += slack;
    p.slackMin = std::min(p.slackMin, slack);
    p.slackMax = std::max(p.slackMax, slack);
}

} // namespace gta2net

using namespace gta2net;

extern "C" void Gta2Net_AfterSlice(double startMs, double endMs)
{
    if (!gLobby)
        return;
    const int32_t* d = gta2_port_net_debug();
    if (!d[0])
        return; // no game yet
    const uint32_t frame = static_cast<uint32_t>(d[4]);
    const double swap = Gta2Native_SwapMsTotal ? Gta2Native_SwapMsTotal() : 0.0;
    if (gMatchStart < 0)
    {
        gMatchStart = endMs;
        gPrevEnd = endMs;
        gLastFrame = frame;
        gLastSwap = swap;
        ResetWindow(endMs, frame);
        return;
    }
    const double swapMs = swap - gLastSwap;
    gLastSwap = swap;
    const double work = std::max(0.0, endMs - startMs - swapMs);
    const double gap = endMs - gPrevEnd;
    ++gIters;
    gWorkSum += work;
    gWorkMax = std::max(gWorkMax, work);
    gSwapSum += swapMs;
    gGapMax = std::max(gGapMax, gap);
    if (frame != gLastFrame)
    {
        ++gTickIters;
        gTickWorkSum += work;
        gTickWorkMax = std::max(gTickWorkMax, work);
    }
    // Held up: the inputs for the next tick still missing when that tick was due (a tick is
    // due 1/30 s after the previous one ran; before that, waiting for them costs nothing).
    const bool pending = d[3] != 0;
    double held = 0;
    if (gPrevPending)
    {
        // waiting since the previous slice; if a tick ran in this slice, the wait ended at its start
        const double until = frame != gLastFrame ? startMs : endMs;
        held = std::max(0.0, until - std::max(gPrevEnd, gDue));
    }
    if (held > 0)
    {
        gWaitMs += held;
        gCurWait += held;
        for (int s = 0; s < gCount; ++s)
            if (gPrevWaiting & (1u << s))
                gPeers[s].waitMs += held;
    }
    if ((!pending || frame != gLastFrame) && gCurWait > 0)
    {
        ++gWaits;
        ++gTotalWaits;
        gTotalWaitMs += gCurWait;
        gWaitMax = std::max(gWaitMax, gCurWait);
        gCurWait = 0;
    }
    if (frame != gLastFrame || (pending && !gPrevPending))
        gDue = startMs + kTickMs; // a tick ran in this slice
    gPrevPending = pending;
    gPrevWaiting = pending ? static_cast<uint32_t>(d[6]) & ~(1u << gMe) : 0;
    gPrevEnd = endMs;
    gLastFrame = frame;
    if (endMs - gWindowStart >= gPeriodMs)
    {
        Report(endMs, frame);
        ResetWindow(endMs, frame);
        PingAll();
    }
}
