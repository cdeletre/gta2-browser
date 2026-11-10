// The engine side of a native co-op match, as setupCoop and watchCoop in web/shell.js:
// sets up the core's network session for the lobby's match, connects the core's game
// messages to the lobby (gta2_js_net_send / gta2_port_net_receive), and while the match
// runs shares a checksum of the game every 90 ticks and compares it with the others'.
// Multiplayer build only (gta2-mp).
#include "coop_native.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

extern "C" {
void gta2_port_net_configure(int count, int me, int delay);
void gta2_port_set_friendly_fire(int on);
void gta2_port_net_set_rules(int type, int limit, int minutes);
void gta2_port_set_ride_together(int policy);
void gta2_port_set_player_team(int slot, int team, const char* side);
void gta2_port_net_set_map(const char* map, const char* style, const char* script);
void gta2_port_net_set_name(int idx, const char* name);
void gta2_port_set_activation_margin(int on);
void gta2_port_net_receive(int from, const uint8_t* data, int len);
void gta2_port_net_peer_left(int idx);
const int32_t* gta2_dev_sync_history();
const int32_t* gta2_port_net_debug();
const int32_t* gta2_port_coop_players();
const int32_t* gta2_port_coop_scores(); // port/platform/coop_web.cpp: every 150 ticks
const int32_t* gta2_dev_sync_dump(int at);
const int32_t* gta2_dev_sync_peds();
}

extern uint8_t gNetPlayerDropped_6F8470[6]; // winmain.cpp: players the game gave up on (C++ name)

namespace gta2net
{

namespace
{
Lobby* gLobby = nullptr;
Json gMatch;
int gMySlot = -1;

constexpr int kSyncEvery = 90; // ticks between checksums (3 s)
constexpr int kSyncTicks = 1024;
constexpr int kSyncRow = 9;
std::map<int, std::string> gMine;                            // tick -> checksum
std::map<int, std::vector<std::pair<int, std::string>>> gTheirs; // tick -> [slot, checksum]
int gLastSent = -1;
constexpr int kScoreRows = 8;     // gta2_port_coop_scores(): the last 8 score ticks
constexpr int kScoreRow = 1 + 6 * 3;
int gLastScore = 0;               // the last score tick reported
bool gDesync = false;
int gHeldFrame = -1;
double gHeldSince = 0;
uint32_t gLastWaiting = 0;
double gLastWatch = 0;
std::string gWinner;     // Gang Wars: the crew that reached the points target
double gWonAt = 0;
double gMatchStart = 0;
int32_t gLastQuit = 0;
uint8_t gLastDropped[6];


// "[12.3 s, frame 456]": when, for the co-op log lines
std::string Stamp()
{
    char buf[48];
    snprintf(buf, sizeof(buf), "[%.1f s, frame %d] ", (NowMs() - gMatchStart) / 1000.0, gta2_port_net_debug()[4]);
    return buf;
}

// The original's multiplayer setups for the three story districts (DISTRICTS in web/net.js).
struct District
{
    const char* key;
    const char* gmp;
    const char* sty;
    const char* scr; // %d: the player count
};
const District kDistricts[] = {
    {"downtown", "wil-multi.gmp", "wil.sty", "downtown-%dp.scr"},
    {"residential", "ste.gmp", "ste.sty", "res-%dp.scr"},
    {"industrial", "bil-multi.gmp", "bil.sty", "Industrial-%dP.scr"},
};

std::string PlayerName(int slot)
{
    const Json& p = gMatch["players"][static_cast<size_t>(slot)];
    if (p["name"].IsString())
    {
        return p["name"].Str();
    }
    return "player " + std::to_string(slot + 1);
}

void Compare(int tick)
{
    auto own = gMine.find(tick);
    for (const auto& [slot, h] : gTheirs[tick])
    {
        if (own != gMine.end() && h != own->second && !gDesync)
        {
            gDesync = true;
            // GTA2_NET_DUMP_ON_DRIFT=1: the last 1024 ticks' records, to find the first that differs
            if (getenv("GTA2_NET_DUMP_ON_DRIFT"))
            {
                const int32_t* v = gta2_dev_sync_history();
                for (int t = 0; t < kSyncTicks; ++t)
                {
                    const int32_t* row = v + t * kSyncRow;
                    fprintf(stderr, "gta2: co-op: last %d %d %d %d %d %d %d\n", row[0], row[1], row[2], row[3], row[4],
                            row[5], row[6]);
                }
            }
            fprintf(stderr, "gta2: co-op: %sgames drifted apart at tick %d (%s: %s, here: %s)\n", Stamp().c_str(), tick,
                    PlayerName(slot).c_str(), h.c_str(), own->second.c_str());
        }
    }
}

// Each player's crew in Gang Wars (index in match.crews), or -1.
int CrewOf(const Json& match, const std::string& id)
{
    const Json& crews = match["crews"];
    for (size_t c = 0; c < crews.size(); ++c)
    {
        for (const Json& m : crews[c]["members"].Items())
        {
            if (m.Str() == id)
            {
                return static_cast<int>(c);
            }
        }
    }
    return -1;
}
} // namespace

void CheckPointsTarget();

// setupCoop()
void SetupCoop(Lobby* lobby, const Json& match)
{
    gLobby = lobby;
    gMatch = match;
    gMySlot = match["mySlot"].Int(-1);
    const Json& players = match["players"];
    const int count = static_cast<int>(players.size());
    const std::string mode = match["mode"].IsString() ? match["mode"].Str() : "roam";
    const District* d = &kDistricts[0];
    for (const District& k : kDistricts)
    {
        d = match["district"].Str() == k.key ? &k : d;
    }

    gta2_port_net_configure(count, gMySlot, match["delay"].Int(1));
    // the mode's rules: free-for-all is the original frag game; Gang Wars and free roam a points
    // game; friendly fire everywhere in free-for-all, by crew in Gang Wars
    gta2_port_set_friendly_fire(mode == "ffa" || (mode == "roam" && match["friendlyFire"].Truthy()) ? 1 : 0);
    gta2_port_net_set_rules(mode == "ffa" ? 1 : 2, mode == "ffa" ? match["killLimit"].Int(0) : 0,
                            mode == "roam" ? 0 : match["minutes"].Int(0));
    // riding together: anyone in free roam, your crew in Gang Wars, nobody in free-for-all
    gta2_port_set_ride_together(mode == "roam" ? 1 : mode == "wars" ? 2 : 0);
    for (int i = 0; i < count; ++i)
    {
        const int crew = mode == "wars" ? CrewOf(match, players[static_cast<size_t>(i)]["id"].Str()) : -1;
        const std::string side = crew >= 0 ? match["crews"][static_cast<size_t>(crew)]["side"].Str() : "";
        gta2_port_set_player_team(i, crew, side.c_str());
    }
    char script[64];
    snprintf(script, sizeof(script), d->scr, count);
    gta2_port_net_set_map(d->gmp, d->sty, script);
    for (int i = 0; i < count; ++i)
    {
        // ASCII for the game's HUD and kill messages
        std::string name = players[static_cast<size_t>(i)]["name"].Str();
        for (char& c : name)
        {
            c = c >= 0x20 && c <= 0x7e ? c : '?';
        }
        gta2_port_net_set_name(i, name.c_str());
    }
    // Every player must simulate identically: the presentation-only activation margin
    // depends on each player's window and camera, so it is off in co-op.
    gta2_port_set_activation_margin(0);

    lobby->onGameMessage = [](int slot, const Bytes& bytes) {
        gta2_port_net_receive(slot, bytes.data(), static_cast<int>(bytes.size()));
    };
    lobby->onPlayerLeft = [](int slot) {
        fprintf(stderr, "gta2: co-op: %s%s left (the connection closed)\n", Stamp().c_str(), PlayerName(slot).c_str());
        gta2_port_net_peer_left(slot);
    };
    lobby->onSync = [](int slot, int tick, const std::string& h) {
        gTheirs[tick].emplace_back(slot, h);
        Compare(tick);
    };
    lobby->onChat = [](int slot, const std::string& text) {
        fprintf(stderr, "gta2: co-op: chat from %s: %s\n", PlayerName(slot).c_str(), text.c_str());
        CoopChatLine(slot, text);
    };
    // GTA2_NET_SYNC_DUMP=tick: the live peds at that tick (as ?syncdump in the browser), logged once
    if (getenv("GTA2_NET_SYNC_DUMP"))
    {
        gta2_dev_sync_dump(atoi(getenv("GTA2_NET_SYNC_DUMP")));
    }
    gMatchStart = NowMs();
    lobby->onWaypoint = [](int slot, bool set, double x, double y) { CoopWaypointLine(slot, set, x, y); };
    lobby->GameReady();
    fprintf(stderr, "gta2: co-op: %d players, this is slot %d, mode %s, district %s, %s, input delay %d ticks\n", count,
            gMySlot, mode.c_str(), d->key, script, match["delay"].Int(1));
}

// watchCoop(): every 250 ms, the checksums of new sync ticks go to the others and are
// compared; a hold-up of more than half a second names who everyone is waiting for.
void WatchCoop(double nowMs)
{
    if (!gLobby || nowMs - gLastWatch < 250)
    {
        return;
    }
    gLastWatch = nowMs;
    const int32_t* v = gta2_dev_sync_history();
    for (int i = 0; i < kSyncTicks; ++i)
    {
        const int32_t* row = v + i * kSyncRow;
        const int tick = row[0];
        if (tick > 0 && tick % kSyncEvery == 0 && !gMine.count(tick))
        {
            // [1, 3, 4, 5, 6].join(':') as the browser does
            gMine[tick] = std::to_string(row[1]) + ":" + std::to_string(row[3]) + ":" + std::to_string(row[4]) + ":" +
                std::to_string(row[5]) + ":" + std::to_string(row[6]);
        }
    }
    for (const auto& [tick, h] : gMine)
    {
        if (tick > gLastSent)
        {
            gLobby->ShareSync(tick, h);
            gLastSent = tick;
            Compare(tick);
        }
    }
    // the standings at each new score tick, to the game server (as web/shell.js watchCoop)
    const int32_t* scores = gta2_port_coop_scores();
    std::vector<const int32_t*> due;
    for (int i = 0; i < kScoreRows; ++i)
    {
        const int32_t* row = scores + i * kScoreRow;
        if (row[0] > gLastScore)
            due.push_back(row);
    }
    std::sort(due.begin(), due.end(), [](const int32_t* a, const int32_t* b) { return a[0] < b[0]; });
    const Json& who = gMatch["players"];
    for (const int32_t* row : due)
    {
        Json players = Json::Array();
        for (size_t s = 0; s < who.size() && s < 6; ++s)
        {
            Json p = Json::Object();
            p.Set("id", who[s]["id"]);
            p.Set("kills", row[1 + s * 3]);
            p.Set("deaths", row[2 + s * 3]);
            p.Set("points", row[3 + s * 3]);
            players.Push(p);
        }
        gLobby->ReportScore(row[0], players);
        gLastScore = row[0];
    }
    for (auto it = gMine.begin(); it != gMine.end();)
    {
        if (it->first < gLastSent - kSyncEvery * 20)
        {
            gTheirs.erase(it->first);
            it = gMine.erase(it);
        }
        else
        {
            ++it;
        }
    }
    // GTA2_NET_DUMP_HISTORY=n: the first n ticks' records (tick, game RNG, world RNG, peds, ped
    // hash, cars, car hash), once, to find the first tick where two players' games differ
    static const int dumpTicks = getenv("GTA2_NET_DUMP_HISTORY") ? atoi(getenv("GTA2_NET_DUMP_HISTORY")) : 0;
    static bool dumped = false;
    if (dumpTicks > 0 && !dumped && gLastSent >= dumpTicks)
    {
        dumped = true;
        for (int t = 0; t < dumpTicks && t < kSyncTicks; ++t)
        {
            const int32_t* row = v + t * kSyncRow;
            fprintf(stderr, "gta2: co-op: history %d %d %d %d %d %d %d\n", row[0], row[1], row[2], row[3], row[4], row[5],
                    row[6]);
        }
    }
    static bool pedsDumped = false;
    if (getenv("GTA2_NET_SYNC_DUMP") && !pedsDumped && gLastSent > atoi(getenv("GTA2_NET_SYNC_DUMP")))
    {
        pedsDumped = true;
        const int32_t* p = gta2_dev_sync_peds();
        for (int i = 0; i < p[0]; ++i)
        {
            const int32_t* o = p + 1 + i * 5;
            fprintf(stderr, "gta2: co-op: ped %d id %d at (%d, %d) sprite %d type %d\n", i, o[0], o[1], o[2], o[3], o[4]);
        }
    }
    CheckPointsTarget();
    // who are we waiting for?
    const int32_t* d = gta2_port_net_debug();
    if (d[4] != gHeldFrame)
    {
        gHeldFrame = d[4];
        gHeldSince = nowMs;
    }
    const uint32_t waiting = d[3] && nowMs - gHeldSince > 500 ? static_cast<uint32_t>(d[6]) & ~(1u << gMySlot) : 0;
    if (waiting != gLastWaiting)
    {
        gLastWaiting = waiting;
        std::string names;
        for (int s = 0; s < 6; ++s)
        {
            if (waiting & (1u << s))
            {
                names += (names.empty() ? "" : ", ") + PlayerName(s);
            }
        }
        fprintf(stderr, waiting ? "gta2: co-op: %swaiting for %s\n" : "gta2: co-op: %sno longer waiting%s\n",
                Stamp().c_str(), names.c_str());
    }
    // the game giving up on a player (no inputs for several seconds), or ending the match
    for (int p = 0; p < 6; ++p)
    {
        if (gNetPlayerDropped_6F8470[p] != gLastDropped[p])
        {
            gLastDropped[p] = gNetPlayerDropped_6F8470[p];
            if (gLastDropped[p])
                fprintf(stderr, "gta2: co-op: %sthe game gave up on %s (no inputs from them)\n", Stamp().c_str(),
                        PlayerName(p).c_str());
        }
    }
    if (d[9] != gLastQuit)
    {
        gLastQuit = d[9];
        if (d[9])
            fprintf(stderr, "gta2: co-op: %sthe match ends here (a player quit, or the host left)\n", Stamp().c_str());
    }
}

const Json& CoopMatch()
{
    return gMatch;
}

int CoopMySlot()
{
    return gMySlot;
}

// hud_net.cpp shows chat lines; headless builds have no screen.
void HudChat(int slot, const std::string& text) __attribute__((weak));
void CoopChatLine(int slot, const std::string& text)
{
    if (HudChat)
        HudChat(slot, text);
}

void HudWaypoint(int slot, bool set, double x, double y) __attribute__((weak));
void CoopWaypointLine(int slot, bool set, double x, double y)
{
    if (HudWaypoint)
        HudWaypoint(slot, set, x, y);
}

const std::string& CoopWinner()
{
    return gWinner;
}

bool CoopEndRequested()
{
    return !gWinner.empty() && NowMs() - gWonAt > 6000;
}

// Gang Wars with a points target (web/shell.js watchCoop): the crews' points (their members'
// cash earned, as gta2_port_coop_players has it); the leading crew wins once it has the target.
void CheckPointsTarget()
{
    if (!gWinner.empty() || gMatch["mode"].Str() != "wars")
        return;
    const int target = gMatch["pointsTarget"].Int(0);
    if (target <= 0)
        return;
    const int32_t* v = gta2_port_coop_players();
    const Json& crews = gMatch["crews"];
    const Json& players = gMatch["players"];
    int bestPoints = 0;
    std::string best;
    for (const Json& crew : crews.Items())
    {
        int points = 0;
        for (const Json& m : crew["members"].Items())
            for (size_t s = 0; s < players.size() && s < 6; ++s)
                if (players[s]["id"].Str() == m.Str() && v[s * 12])
                    points += v[s * 12 + 11];
        if (best.empty() || points > bestPoints)
        {
            best = crew["name"].Str();
            bestPoints = points;
        }
    }
    if (!best.empty() && bestPoints >= target)
    {
        gWinner = best;
        gWonAt = NowMs();
        fprintf(stderr, "gta2: co-op: %s%s win the Gang War with %d points (target %d)\n", Stamp().c_str(), best.c_str(),
                bestPoints, target);
    }
}

// The latest checksum this player shared and whether a drift was seen (status line, tests).
void CoopStatus(int* lastSyncTick, bool* drifted)
{
    *lastSyncTick = gLastSent;
    *drifted = gDesync;
}

} // namespace gta2net

// The core's game messages (port/platform/net_web.cpp): to a slot, or -1 for everyone.
extern "C" void gta2_js_net_send(int to, const void* data, int len)
{
    if (gta2net::gLobby && len > 0)
    {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        gta2net::gLobby->SendGame(to, gta2net::Bytes(p, p + len));
    }
}
