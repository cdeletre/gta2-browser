// Co-op (D20): what the shell's map, radar and scoreboard show about every
// player. Each player's game simulates everyone, so this is read from the
// local game; it only reads.
#include <emscripten.h>

#include "Car_BC.hpp"
#include "Game_0x40.hpp"
#include "GameSession_578.hpp"
#include "Ped.hpp"
#include "Player.hpp"
#include "char.hpp"
#include "sprite.hpp"

extern "C" s32 gGta2PortDrawnPlayers[6][4]; // port_interpolation.cpp: positions as last drawn
extern "C" s32 gGta2PortInterpolate;
extern "C" s32 gGta2PortWasted[6];           // port_coop.cpp: deaths and arrests per player
extern "C" s32 gGta2PortBusted[6];         // (only kept up to date while interpolation is on)

namespace
{
constexpr s32 kFields = 12;
s32 gPlayers[MAX_PLAYERS * kFields];

Sprite* PedSprite(Ped* ped)
{
    if (Car_BC* car = ped->get_car_416B60())
    {
        return car->field_50_car_sprite;
    }
    return ped->field_168_game_object ? ped->field_168_game_object->field_80_sprite_ptr : nullptr;
}
} // namespace

extern "C" {

// Per player slot 0-5: in use, has a ped on the map, x, y (Fix16 raw), rotation (Ang16),
// in a car, health, kills (players killed, less own deaths), deaths (any cause), score (cash),
// arrests, points (the multiplayer score: cash earned).
EMSCRIPTEN_KEEPALIVE const s32* gta2_port_coop_players()
{
    if (!gGame_0x40_67E008)
    {
        return gPlayers; // the match has ended: the standings as of its last tick (Gta2Port_CoopSnapshot)
    }
    for (s32& v : gPlayers)
    {
        v = 0;
    }
    for (u8 i = 0; i < MAX_PLAYERS; ++i)
    {
        Player* p = gGame_0x40_67E008->get_player_4219E0(i);
        s32* o = &gPlayers[i * kFields];
        if (!p || !p->field_8E_bInUse)
        {
            continue;
        }
        o[0] = 1;
        if (Ped* ped = p->field_2C4_player_ped)
        {
            if (gGta2PortInterpolate && gGta2PortDrawnPlayers[i][0])
            {
                // where the last (interpolated) draw put them, so markers move as smoothly as the view
                o[1] = 1;
                o[2] = gGta2PortDrawnPlayers[i][1];
                o[3] = gGta2PortDrawnPlayers[i][2];
                o[4] = gGta2PortDrawnPlayers[i][3];
            }
            else if (Sprite* sp = PedSprite(ped))
            {
                o[1] = 1;
                o[2] = sp->field_14_xy.x.mValue;
                o[3] = sp->field_14_xy.y.mValue;
                o[4] = sp->field_0.rValue;
            }
            o[5] = ped->get_car_416B60() != nullptr;
            o[6] = ped->field_216_health;
        }
        o[7] = gGameSession_67E8E0.field_490_frags_list[i];
        o[8] = gGta2PortWasted[i];
        o[9] = p->GetScore_421980();
        o[10] = gGta2PortBusted[i];
        o[11] = gGameSession_67E8E0.GetPointsForPlayerIdx_4C5CB0(i);
    }
    return gPlayers;
}

// Every 150 ticks (5 s) of a network game: each slot's kills, deaths and points at that tick.
// Lockstep makes them the same on every machine, so every player reports them to the game
// server, which compares the reports of one tick (server/server.mjs, GET /games).
namespace
{
constexpr s32 kScoreEvery = 150;
constexpr s32 kScoresKept = 8;
s32 gScores[kScoresKept][1 + MAX_PLAYERS * 3]; // tick (0: none yet), then kills, deaths, points per slot
} // namespace
extern "C" s32 Gta2Port_SyncTick(); // dev_hooks.cpp

// After every tick of a network game (winmain.cpp): keeps the standings, so the end-of-match
// results include the winning kill even though the game is gone by the time the shell looks.
void Gta2Port_CoopSnapshot()
{
    gta2_port_coop_players();
    const s32 tick = Gta2Port_SyncTick();
    if (tick > 0 && tick % kScoreEvery == 0)
    {
        s32* row = gScores[(tick / kScoreEvery) % kScoresKept];
        row[0] = tick;
        for (s32 i = 0; i < MAX_PLAYERS; ++i)
        {
            row[1 + i * 3] = gPlayers[i * kFields + 7];
            row[2 + i * 3] = gPlayers[i * kFields + 8];
            row[3 + i * 3] = gPlayers[i * kFields + 11];
        }
    }
}

// The last 8 score ticks (rows of 1 + 6 * 3: tick, then kills, deaths, points per slot; any
// order, tick 0 for an unused row).
EMSCRIPTEN_KEEPALIVE const s32* gta2_port_coop_scores()
{
    return &gScores[0][0];
}

// Gang Wars: player slot's crew (-1: none) and the district gang the crew sides with, as its
// three-letter name ("zai", "loo", ...; empty: none).
extern "C" s32 gGta2PortTeam[6];
extern "C" s32 gGta2PortSide[6];
EMSCRIPTEN_KEEPALIVE void gta2_port_set_player_team(int slot, int team, const char* side)
{
    if (slot < 0 || slot >= 6)
    {
        return;
    }
    gGta2PortTeam[slot] = team;
    gGta2PortSide[slot] = side && side[0] && side[1] && side[2]
        ? static_cast<u8>(side[0]) | (static_cast<u8>(side[1]) << 8) | (static_cast<u8>(side[2]) << 16)
        : 0;
}

// Riding together: 0 never, 1 with any player (free roam), 2 with your crew (Gang Wars).
extern "C" s32 gGta2PortRideTogether;
EMSCRIPTEN_KEEPALIVE void gta2_port_set_ride_together(int policy)
{
    gGta2PortRideTogether = policy == 1 || policy == 2 ? policy : 0;
}

} // extern "C"
