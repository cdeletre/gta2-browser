// The game's network session (NetPlay.cpp, DirectPlay) and lobby dialogs
// (Network_20324.cpp) are excluded from the browser build. This file provides
// the NetPlay calls the game makes during a network game, on top of a
// browser transport run by the shell (web/net.js): BroadcastChannel between
// tabs, or WebRTC data channels between players (D20, peer-to-peer, no
// server). Without a session it reports "no network", as single player
// expects.
//
// The transport delivers each peer's messages reliably and in order, so the
// original's packet layer (resends, out-of-order buffering, pings) reduces to
// a queue: each message is [subtype, input sequence, payload...], and the
// game's resends of older inputs (Net_4DA9B0, for lost packets) are not sent.
//
// Input delay (for internet latency): the inputs a player sends at tick N are
// for tick N + delay (winmain.cpp applies the local player's own inputs that
// late as well), so a message has `delay` ticks to arrive before anyone waits
// for it. The first `delay` ticks use empty inputs, queued here at the start.
#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <deque>
#include <vector>

#include "NetPlay.hpp"
#include "Network_20324.hpp"

DEFINE_GLOBAL(NetPlay, gNetPlay_7071E8, 0x7071E8);
EXTERN_GLOBAL(s32, gNetworkGameSpeed_7071A0); // Game_0x40.cpp; the DirectPlay lobby set these
EXTERN_GLOBAL(s32, gNetworkPolice_7071B0);

namespace
{
constexpr u8 kStandard = 2; // packet_subtype::StandardPacket_2
constexpr u8 kInputs = 3;   // packet_subtype::PlayerInputs_3
constexpr u8 kKeepAlive = 2; // std_packet_subtype::KeepAlive_2
constexpr u8 kGiveUp = 1;    // std_packet_subtype::GiveUp_1

struct Message
{
    s32 from;
    u8 subtype;
    u8 seq;
    std::vector<u8> payload;
};

std::deque<Message> gInbox;
u8 gExpectedSeq[MAX_PLAYERS];
// Three players or more: the inputs delivered so far per player, and the players gone (see
// ReceiveGameMessage_521890).
s32 gInputsDelivered[MAX_PLAYERS];
bool gGone[MAX_PLAYERS];
u8 gDelivered[256];
wchar_t gNames[MAX_PLAYERS][32];
bool gSession = false;
constexpr s32 kMaxInputDelay = 15;
// the match's map, style and script (data\ is added by the game)
char gMap[3][64];
} // namespace

extern "C" s32 gGta2PortNetInputDelay = 0; // ticks; read by winmain.cpp

EM_JS(void, gta2_js_net_send, (int to, const void* data, int len), {
    Module.gta2Net && Module.gta2Net.send(to, HEAPU8.slice(data, data + len));
});

static void Send(s32 to, u8 subtype, u8 seq, const void* payload, s32 len)
{
    u8 buf[2 + 256];
    if (len < 0 || len > 256)
    {
        return;
    }
    buf[0] = subtype;
    buf[1] = seq;
    memcpy(buf + 2, payload, len);
    gta2_js_net_send(to, buf, len + 2);
}

extern "C" {

// The shell sets up a session before starting a network game: count players
// (slots 0..count-1, 0 is the host), this player is slot me, inputs delayed by
// delay ticks (the same for every player).
EMSCRIPTEN_KEEPALIVE void gta2_port_net_configure(int count, int me, int delay)
{
    NetPlay& n = gNetPlay_7071E8;
    gSession = count > 1;
    n.field_8F0_disabled = gSession ? 0 : 1;
    n.field_5D4_player_idx = me;
    n.field_758_n2.field_4_players_count = count;
    for (s32 i = 0; i < MAX_PLAYERS; ++i)
    {
        n.field_758_n2.field_10_players[i].field_0_in_use = i < count ? 1 : 0;
        n.field_758_n2.field_10_players[i].field_10_player_id = i;
        n.field_758_n2.field_8_packet_game_tick[i] = 0;
        gExpectedSeq[i] = 0;
        gInputsDelivered[i] = 0;
        gGone[i] = false;
    }
    gInbox.clear();
    gGta2PortNetInputDelay = delay < 0 ? 0 : delay > kMaxInputDelay ? kMaxInputDelay : delay;
    // Free roam (D20): the fastest network speed, 33 ms per tick like single player (0 = 50 ms,
    // the original's slowest, was left by default), and the police on.
    gNetworkGameSpeed_7071A0 = 2;
    gNetworkPolice_7071B0 = 1;
    // Our first real inputs are for tick `delay`; every other player's ticks before that are empty.
    n.field_758_n2.field_8_packet_game_tick[me] = static_cast<u8>(gGta2PortNetInputDelay);
    for (s32 i = 0; gSession && i < count; ++i)
    {
        for (s32 t = 0; i != me && t < gGta2PortNetInputDelay; ++t)
        {
            gInbox.push_back({i, kInputs, static_cast<u8>(t), std::vector<u8>(4, 0)});
        }
    }
}

extern s32 gRuleType, gRuleLimit, gRuleMinutes;

// The match rules: type 1 = frag game (free-for-all), 2 = points game; limit 0 = none; minutes 0 = none.
EMSCRIPTEN_KEEPALIVE void gta2_port_net_set_rules(int type, int limit, int minutes)
{
    gRuleType = type == 1 ? 1 : 2;
    gRuleLimit = limit > 0 ? limit : 0x7FFFFFFF;
    gRuleMinutes = minutes > 0 ? (minutes > 60 ? 60 : minutes) : 0;
}

// Friendly fire (co-op, D20): 0 = players cannot hurt each other. The same for every player.
extern "C" s32 gGta2PortFriendlyFire; // port_coop.cpp
EMSCRIPTEN_KEEPALIVE void gta2_port_set_friendly_fire(int on)
{
    gGta2PortFriendlyFire = on ? 1 : 0;
}

// The district to play: the original's multiplayer map, style and script for
// the player count (data/*.mmp).
EMSCRIPTEN_KEEPALIVE void gta2_port_net_set_map(const char* map, const char* style, const char* script)
{
    const char* names[3] = {map, style, script};
    for (s32 i = 0; i < 3; ++i)
    {
        strncpy(gMap[i], names[i], sizeof(gMap[i]) - 1);
        gMap[i][sizeof(gMap[i]) - 1] = 0;
    }
}

// A player's name for the game's HUD and kill messages (ASCII).
EMSCRIPTEN_KEEPALIVE void gta2_port_net_set_name(int idx, const char* name)
{
    if (idx < 0 || idx >= MAX_PLAYERS)
    {
        return;
    }
    s32 i = 0;
    for (; name[i] && i < 31; ++i)
    {
        gNames[idx][i] = static_cast<u8>(name[i]);
    }
    gNames[idx][i] = 0;
}

// A message from player `from` arrived (the shell calls this from its transport).
EMSCRIPTEN_KEEPALIVE void gta2_port_net_receive(int from, const u8* data, int len)
{
    if (!gSession || from < 0 || from >= MAX_PLAYERS || len < 2)
    {
        return;
    }
    gInbox.push_back({from, data[0], data[1], std::vector<u8>(data + 2, data + len)});
}

// A player's connection closed: the game handles it as if they had given up.
EMSCRIPTEN_KEEPALIVE void gta2_port_net_peer_left(int idx)
{
    if (!gSession || idx < 0 || idx >= MAX_PLAYERS)
    {
        return;
    }
    u8 payload[5] = {kGiveUp, 0, 0, 0, 0};
    memcpy(payload + 1, &idx, 4);
    // Behind that player's last inputs: every remaining player's game sees the departure at the
    // same tick, and only then (ReceiveGameMessage_521890) stops waiting for them.
    gInbox.push_back({idx, kStandard, 0, std::vector<u8>(payload, payload + 5)});
}

} // extern "C"

NetPlay::NetPlay()
{
    field_8F0_disabled = 1;
}

NetPlay::~NetPlay()
{
}

// The network game start-up (winmain.cpp, after -N) goes through these: they
// succeed when the shell has set up a session.
s32 NetPlay::SetProtoAndConnection_51DAE0(GUID*, Connection_Unknown*)
{
    return gSession ? 1 : 0;
}

s32 NetPlay::InitializeConnection_51E5C0()
{
    return gSession ? 1 : 0;
}

void NetPlay::Disconnect_520D10()
{
    gSession = false;
    field_8F0_disabled = 1;
}

s32 NetPlay::DeletePlayerFromGroup_521000(u32 idx)
{
    if (idx < MAX_PLAYERS)
    {
        field_758_n2.field_10_players[idx].field_0_in_use = 0;
    }
    return 0;
}

void NetPlay::GetPlayerName_521100(wchar_t* Destination, u32 idx)
{
    Destination[0] = 0;
    if (idx < MAX_PLAYERS)
    {
        wcscpy(Destination, gNames[idx]);
    }
}

void NetPlay::SetExitGameCallBack_521330(s32, Game_0x40*)
{
}

s32 NetPlay::GetMaxPlayers_521350()
{
    return gSession ? static_cast<s32>(field_758_n2.field_4_players_count) : 1;
}

// The shell's lobby already has every player connected before the game starts.
bool NetPlay::WaitForPlayersSync_5213E0()
{
    return gSession;
}

// Only used to resend earlier inputs after a packet loss, which a reliable transport does not have.
s32 NetPlay::SendToPlayer_521630(Network_8*, s32, char_type)
{
    return 0;
}

extern u8 gNetPlayerDropped_6F8470[6]; // winmain.cpp: players the game gave up on

// Whether player `from`'s next inputs may go to the game now. The game reads messages until
// it has every other player's inputs for the tick (winmain.cpp PortNetReceivePoll), and an
// input overwrites that player's previous one: with three players or more, a player whose
// inputs for the next tick came in before a slower player's for this tick had them read one
// tick early, on that machine only, which drifted the games apart (seen at three players,
// tick 2). An input is held back while its player is ahead of another player still in the
// game. With two players there is no other player to wait for: nothing changes.
static bool InputDue(s32 from)
{
    const NetPlay& n = gNetPlay_7071E8;
    const s32 me = n.field_5D4_player_idx;
    for (s32 q = 0; q < MAX_PLAYERS; ++q)
    {
        if (q == from || q == me || !n.field_758_n2.field_10_players[q].field_0_in_use || gGone[q] ||
            gNetPlayerDropped_6F8470[q])
        {
            continue;
        }
        if (gInputsDelivered[q] < gInputsDelivered[from])
        {
            return false;
        }
    }
    return true;
}

char_type NetPlay::ReceiveGameMessage_521890(Network_8* pOut, s32* pPlayerIdx, u32* packet_subtype)
{
    // the first message the game may have now (each player's messages stay in their order:
    // only a player's first message in the queue can go, or be dropped as a resend)
    bool seen[MAX_PLAYERS] = {};
    for (auto it = gInbox.begin(); gSession && it != gInbox.end();)
    {
        const s32 from = it->from;
        if (from < 0 || from >= MAX_PLAYERS)
        {
            it = gInbox.erase(it);
            continue;
        }
        if (seen[from])
        {
            ++it; // behind an earlier message of the same player
            continue;
        }
        if (it->subtype == kInputs && it->seq != gExpectedSeq[from])
        {
            it = gInbox.erase(it); // a resend of inputs already delivered
            continue;
        }
        seen[from] = true;
        // inputs, and a departure too: delivered at a player's next tick only, so that every
        // game drops a leaving player at the same tick (read while waiting for someone else,
        // it went in one tick early on that machine only)
        if (!InputDue(from))
        {
            ++it;
            continue;
        }
        Message m = std::move(*it);
        gInbox.erase(it);
        if (m.subtype == kInputs)
        {
            ++gExpectedSeq[m.from];
            ++gInputsDelivered[m.from];
        }
        else if (m.payload.size() >= 5 && m.payload[0] == kGiveUp)
        {
            s32 who;
            memcpy(&who, m.payload.data() + 1, 4);
            if (who >= 0 && who < MAX_PLAYERS)
            {
                gGone[who] = true;
            }
#if defined(GTA2_PORT_NATIVE)
            extern s32 gNetworkFrameCounter_6F5868;
            fprintf(stderr, "gta2: net: player %d gone, delivered at network frame %d after %d of their inputs\n", who,
                    gNetworkFrameCounter_6F5868, who >= 0 && who < MAX_PLAYERS ? gInputsDelivered[who] : -1);
#endif
        }
        const size_t len = m.payload.size() < sizeof(gDelivered) ? m.payload.size() : sizeof(gDelivered);
        memcpy(gDelivered, m.payload.data(), len);
        pOut->field_0_data = gDelivered;
        pOut->field_4_len = static_cast<s32>(len);
        *pPlayerIdx = m.from;
        *packet_subtype = m.subtype;
        return 1;
    }
#if defined(GTA2_PORT_NATIVE)
    // GTA2_NET_TRACE=1: what is held back when nothing can be delivered
    static const bool trace = getenv("GTA2_NET_TRACE") != nullptr;
    static u32 traced = 0;
    if (trace && !gInbox.empty() && traced < 40)
    {
        ++traced;
        fprintf(stderr, "gta2: net trace: nothing due; delivered");
        for (s32 q = 0; q < MAX_PLAYERS; ++q)
        {
            fprintf(stderr, " %d", gInputsDelivered[q]);
        }
        fprintf(stderr, "; inbox:");
        s32 n = 0;
        for (const Message& m : gInbox)
        {
            if (n++ < 12)
            {
                fprintf(stderr, " %d:%u/%u", m.from, m.subtype, m.seq);
            }
        }
        fprintf(stderr, " (%d)\n", n);
    }
#endif
    return 0;
}

void NetPlay::SendToAll_521B20(Network_8* pSendData)
{
    if (!gSession)
    {
        return;
    }
    const s32 me = GetPlayerIdx_409C40();
    Send(-1, kInputs, field_758_n2.field_8_packet_game_tick[me], pSendData->field_0_data, pSendData->field_4_len);
    field_758_n2.field_8_packet_game_tick[me] = static_cast<u8>(field_758_n2.field_8_packet_game_tick[me] + 1);
}

s32 NetPlay::SendKeepAlive_521D20()
{
    if (gSession)
    {
        const u8 subtype = kKeepAlive;
        Send(-1, kStandard, 0, &subtype, 1);
    }
    return 0;
}

s32 NetPlay::SendGiveUp_521DB0(s32 player_idx)
{
    if (gSession)
    {
        u8 payload[5] = {kGiveUp, 0, 0, 0, 0};
        memcpy(payload + 1, &player_idx, 4);
        Send(-1, kStandard, 0, payload, 5);
    }
    return 0;
}

static char_type gNoMap[1] = {0};

Network_20324::Network_20324()
{
}

Network_20324::~Network_20324()
{
}

s32 Network_20324::ShowNetworkUiBlocking_519BD0(HINSTANCE)
{
    return gSession ? 1 : 0; // the DirectPlay lobby is replaced by the shell's co-op lobby (web/net.js)
}

// The match rules (set by the shell): free roam and Gang Wars are a points game with no reachable
// limit (Gang Wars is scored by the shell, per crew); free-for-all is the original frag game.
s32 gRuleType = 2;            // POINTS_GAME_2, or FRAG_GAME_1
s32 gRuleLimit = 0x7FFFFFFF;  // frags or points to win
s32 gRuleMinutes = 0;         // time limit, 0: none (the game allows up to 60)

void Network_20324::CopyGameSettings_51C7F0(NetworkGameSettings* pSettings)
{
    memset(pSettings, 0, sizeof(*pSettings));
    pSettings->field_20190_game_speed = 0;
    pSettings->field_20194_frag_limit = gRuleLimit;
    pSettings->field_20198_game_type = gRuleType;
    pSettings->field_201A0_police_on = 1;
    pSettings->field_201A4_game_time_limit = gRuleMinutes;
}

char_type* Network_20324::GetMapName_51CA10()
{
    return gMap[0][0] ? gMap[0] : gNoMap;
}

char_type* Network_20324::GetMapStyName_51CA50()
{
    return gMap[1][0] ? gMap[1] : gNoMap;
}

char_type* Network_20324::GetMapScrName_51CA90()
{
    return gMap[2][0] ? gMap[2] : gNoMap;
}
