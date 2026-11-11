// A multiplayer player's colour as the game shows it (their arrow, their score in $), for the
// native overlays (net/hud_net.cpp: scoreboard, waypoints, chat names).
//
// The game colours a player by their ped's remap: Player::ColorScoreFromRemap_569A10 picks a
// "user remap" palette (field_790_hud_palette, of type 7) for the score digits, and the arrow
// sprites follow the same remaps. The palette lives in the loaded style (.sty): it is read here
// from the style's physical palettes (256 colours a palette, in 64-palette pages, a colour every
// 256 bytes: gtx_0x106C::GetPalData_5AA6A0). The player's colour is the middle shade of those it
// changes from user remap 1, the plain one.
#include <stdint.h>
#include <stdio.h>

#include <algorithm>
#include <vector>

#include "Game_0x40.hpp"
#include "Player.hpp"
#include "gtx_0x106C.hpp"

namespace
{
// the user remaps Player::ColorScoreFromRemap_569A10 gives players
constexpr int16_t kBaseRemap = 1;
uint32_t gCache[6];
int16_t gCachedPalette[6] = {-1, -1, -1, -1, -1, -1};

// of 0xAARRGGBB (the style's BGRA bytes): the brightest channel, and how far the others are
int Brightness(uint32_t argb)
{
    const int r = (argb >> 16) & 0xff, g = (argb >> 8) & 0xff, b = argb & 0xff;
    return r > g ? (r > b ? r : b) : (g > b ? g : b);
}
int Saturation(uint32_t argb)
{
    const int r = (argb >> 16) & 0xff, g = (argb >> 8) & 0xff, b = argb & 0xff;
    const int lo = r < g ? (r < b ? r : b) : (g < b ? g : b);
    return Brightness(argb) - lo;
}
} // namespace

// 0xAABBGGRR as the overlay takes it, or 0 when the game has none for that slot (not in use, no
// gang remap: single player palettes).
extern "C" uint32_t Gta2Native_PlayerColour(int slot)
{
    if (slot < 0 || slot >= 6 || !gGame_0x40_67E008 || !gGtx_0x106C_703DD4)
        return 0;
    Player* p = gGame_0x40_67E008->get_player_4219E0(static_cast<u8>(slot));
    if (!p || !p->field_8E_bInUse || p->field_78C_hud_palette_type != palette_types_enum::user_remaps_7)
        return 0;
    const int16_t pal = static_cast<int16_t>(p->field_790_hud_palette);
    if (gCachedPalette[slot] == pal)
        return gCache[slot];
    gtx_0x106C* gtx = gGtx_0x106C_703DD4;
    auto palette = [gtx](int16_t user) {
        return gtx->GetPalData_5AA6A0(
            gtx->get_phys_pal_5AA6F0(static_cast<u16>(gtx->GetTruePalette_5AA5F0(palette_types_enum::user_remaps_7, user))));
    };
    auto colour = [](const BYTE* data, int c) { // 0xAARRGGBB from b, g, r, a
        const BYTE* e = data + c * 256;
        return (static_cast<uint32_t>(e[2]) << 16) | (static_cast<uint32_t>(e[1]) << 8) | e[0];
    };
    // the colours this player's remap changes from the plain one (user remap 1: in the original
    // style a player's differs from it by a ramp of 5 shades, dark to light, the digits' colours,
    // and a grey), without the greys; the player's colour is the middle shade of the ramp, the one
    // the digits on screen look like (the lightest went yellow for green players)
    const BYTE* mine = palette(pal);
    const BYTE* base = palette(kBaseRemap);
    std::vector<uint32_t> shades;
    for (int c = 1; c < 256; ++c)
    {
        const uint32_t own = colour(mine, c);
        if (colour(base, c) != own && Saturation(own) >= 32)
            shades.push_back(own);
    }
    if (shades.empty())
        return 0;
    std::sort(shades.begin(), shades.end(), [](uint32_t a, uint32_t b) { return Brightness(a) < Brightness(b); });
    const uint32_t best = shades[shades.size() / 2];
    const uint32_t r = (best >> 16) & 0xff, g = (best >> 8) & 0xff, b = best & 0xff;
    gCache[slot] = 0xff000000u | (b << 16) | (g << 8) | r;
    gCachedPalette[slot] = pal;
    fprintf(stderr, "gta2: player %d colour: user remap %d, #%02x%02x%02x\n", slot, pal, r, g, b);
    return gCache[slot];
}
