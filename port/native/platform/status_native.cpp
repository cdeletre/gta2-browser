// One-line game status for the native build's periodic log (main.cpp): which map is
// loaded and where the player is. Reads the same state as the web developer probe
// (gta2_dev_state, port/platform/dev_hooks.cpp).
#include <stdio.h>

#include "GameSession_578.hpp"

extern "C" const s32* gta2_dev_state();

extern "C" void Gta2Native_StatusLine(char* out, int size)
{
    const s32* s = gta2_dev_state();
    const char* map = gGameSession_67E8E0.field_0_map_name;
    if (!s[0])
    {
        snprintf(out, size, "map \"%s\", no player", map);
        return;
    }
    // Fix16: 14 fractional bits (1 block = 16384).
    snprintf(out, size, "map \"%s\", player at (%.2f, %.2f, %.2f) blocks%s", map, s[2] / 16384.0, s[3] / 16384.0,
             s[4] / 16384.0, s[1] ? ", in a car" : "");
}
