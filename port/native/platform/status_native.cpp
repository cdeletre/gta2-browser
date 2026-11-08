// One-line game status for the native build's periodic log (main.cpp): which map is
// loaded, the simulation tick, the ped and car counts, and where the player is. Reads
// the same state as the web developer probe (port/platform/dev_hooks.cpp):
// gta2_dev_state, and gta2_dev_sync_history, which Gta2Port_SyncRecord fills after
// every game tick (winmain.cpp, patch 0017).
#include <stdio.h>

#include "Frontend.hpp"
#include "Game_0x40.hpp"
#include "GameSession_578.hpp"

extern "C" const s32* gta2_dev_state();
extern "C" const s32* gta2_dev_sync_history();

namespace
{
constexpr int kSyncTicks = 1024; // dev_hooks.cpp
constexpr int kSyncRow = 9;
} // namespace

// Ticks run so far (0 before the first), with the latest tick's ped and car counts.
extern "C" int Gta2Native_TickCount(int* peds, int* cars)
{
    const s32* history = gta2_dev_sync_history();
    int best = -1;
    for (int i = 0; i < kSyncTicks; ++i)
    {
        const s32* row = history + i * kSyncRow;
        if ((row[0] != 0 || i == 0) && (best < 0 || row[0] > history[best * kSyncRow]))
        {
            best = i;
        }
    }
    const s32* row = history + best * kSyncRow;
    const bool any = row[0] != 0 || row[3] != 0 || row[5] != 0;
    *peds = any ? row[3] : 0;
    *cars = any ? row[5] : 0;
    return any ? row[0] + 1 : 0;
}

extern "C" void Gta2Native_StatusLine(char* out, int size)
{
    int peds = 0;
    int cars = 0;
    const int ticks = Gta2Native_TickCount(&peds, &cars);
    const s32* s = gta2_dev_state();
    const char* map = gGameSession_67E8E0.field_0_map_name;
    if (!s[0])
    {
        snprintf(out, size, "map \"%s\", tick %d, no player", map, ticks);
        return;
    }
    // Fix16: 14 fractional bits (1 block = 16384).
    snprintf(out, size, "map \"%s\", tick %d, %d peds, %d cars, player at (%.2f, %.2f, %.2f)%s", map, ticks, peds, cars,
             s[2] / 16384.0, s[3] / 16384.0, s[4] / 16384.0, s[1] ? " in a car" : "");
}

// True while the player is in the city (not in the front end's menus): the controller's
// "play" layout applies (input_sdl.cpp), as the web shell's isPlaying.
extern "C" bool Gta2Native_IsPlaying()
{
    return !gFrontend_67DC84 && gGame_0x40_67E008 && gta2_dev_state()[0] != 0;
}
