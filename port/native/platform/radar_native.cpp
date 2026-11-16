// The radar and the map screen (not in the original game), drawn over the game's frame with the
// port's overlay (render/gles_renderer.cpp).
//
// The radar: a round, north-up minimap in a bottom corner, the native counterpart of the
// browser's (web/map.js drawMinimap). Options: Radar Off, Left or Right (registry
// screen\port_radar, conf/registry.txt: 0 off, 1 right, 2 left; right by default).
//
// The map screen: with the pause (Start: the game's quit prompt), the district's map over the
// screen, a rectangle as the screen's shape, centred on the player, with the same markers; the
// quit prompt moves to a band at the bottom edge, left uncovered (native patch 0020:
// gGta2PortQuitAtBottom, gGta2PortQuitBandTop). Tokens are left off it (no discovery yet).
//
// What it shows, from port/platform/map_web.cpp (the same reads as the browser's map, all
// read-only towards the game, so it changes nothing in a network game's simulation):
//   - the district's map, built once per district from the player's own map and style files
//     (gta2_port_map_render_rows), a few rows per frame, the rows nearest the player first;
//   - weapons, power-ups, tokens, kill frenzies, garages, crushers and church save points
//     within its range (about 9 blocks around the player: what is near the screen anyway);
//   - the game's gang job and mission arrows (gta2_port_map_arrows: the targets the HUD would
//     point at now): inside, the gang's logo or a pulsing ring; outside, the game's own arrow
//     sprite on the rim, pointing their way. While the radar is on, the game's arrows around the
//     player are not drawn (gta2_port_set_hud_arrows, as in the browser);
//   - in gta2-online and gta2-lan, the other players, allies and enemies, at any distance, and
//     the waypoints (net/hud_net.cpp, Gta2Net_RadarMarks; the browser shows enemies only close);
//   - the player, an arrow in the middle, and the north on the rim.
// Not here (the browser has them): discovery of the map, panning and zooming the map screen.
#if !defined(GTA2_NATIVE_HEADLESS)
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <math.h>

#include <algorithm>
#include <unordered_map>
#include <vector>

extern "C" {
const int32_t* gta2_port_map_state();                                   // port/platform/map_web.cpp
int gta2_port_map_render_rows(int y0, int rows, int px, uint8_t* out); // same
int gta2_port_map_objects(int32_t* out, int max);
int gta2_port_map_triggers(int32_t* out, int max);
int gta2_port_map_gangs(int32_t* out, int max);
int gta2_port_map_arrows(int32_t* out, int max);
int gta2_port_sprite_rgba(int sprite_type, int sprite_idx, uint8_t* out, int max);
void gta2_port_set_hud_arrows(int show);
void gta2_port_reg_set_dword(const char* path, const char* name, unsigned int value);
unsigned int Gta2Native_RegistryDword(const char* path, const char* name, unsigned int fallback);
bool Gta2Native_IsPlaying();        // status_native.cpp
bool Gta2Native_QuitPromptShown(); // status_native.cpp
bool Gta2Native_OptionsOpen();     // options_native.cpp
extern int32_t gGta2PortQuitAtBottom; // native patch 0020 (Hud.cpp)
extern int32_t gGta2PortQuitBandTop;
void Gta2Native_OverlayImage(int id, int w, int h, const uint8_t* rgba, bool linear); // render/gles_renderer.cpp
void Gta2Native_OverlayImageRows(int id, int w, int y, int rows, const uint8_t* rgba);
void Gta2Native_OverlayFreeImage(int id);
void Gta2Native_OverlayTriangle(int id, const float* xyuv, uint32_t rgba);
void Gta2Native_OverlayText(float x, float y, float px, uint32_t rgba, const char* text);
// net/hud_net.cpp (gta2-online, gta2-lan): [kind (0 a player, 1 their waypoint, 2 this player's
// waypoint), x, y (Fix16 raw), rotation (Ang16), colour (0xAABBGGRR)] each. Weak: solo has none.
int Gta2Net_RadarMarks(int32_t* out, int max) __attribute__((weak));
}

namespace
{
const char* const kScreen = "hklm\\software\\dma design ltd\\gta2\\screen";
constexpr double kFx = 16384.0;    // Fix16: 1.0 block
constexpr double kPi = 3.14159265358979323846;
constexpr int kMapImage = 1;       // overlay image ids: the map, then HUD sprites from kSpriteBase
constexpr int kSpriteBase = 1000;
constexpr int kHudSprites = 6;     // sprite type of the HUD's sprites (DrawFigure)
constexpr double kBudgetMs = 3.0;  // map building per frame
constexpr double kRefreshMs = 500; // points of interest and arrows are read again this often

// Colours as the overlay takes them, 0xAABBGGRR (web/map.js's CSS colours).
constexpr uint32_t kBlack = 0xff050505, kGround = 0xff0f0c0b, kWhite = 0xffffffff, kRim = 0x2effffff;
constexpr uint32_t kTargetRed = 0xff3b3bff, kOwnWaypoint = 0xff3fd2ff;

enum Kind
{
    kSave,
    kGarage,
    kCrusher,
    kFrenzy,
    kWeapon,
    kPowerup,
    kToken
};
const uint32_t kKindColour[] = {0xffffffff, 0xffff7dc7, 0xff8f979a, 0xff3b3bff, 0xff1c9fff, 0xff84dc3d, 0xff3fd2ff};

struct Poi
{
    double x, y;
    Kind kind;
};

// Gang job phones by HUD arrow colour: their colour and their difficulty triangle (HUD sprite).
struct JobStyle
{
    uint32_t colour;
    int dot;
};
bool JobOf(int colour, JobStyle* out)
{
    switch (colour)
    {
        case 3: *out = {0xff84dc3d, 9}; return true;  // easy, green phone
        case 1: *out = {0xff3fd2ff, 8}; return true;  // medium, yellow
        case 2: *out = {0xff3b3bff, 10}; return true; // hard, red
        case 4: *out = {0xfff0c94c, 0}; return true;  // info phone, blue
        default: return false;
    }
}

struct Mark
{
    double x, y;
    bool job;
    int colour;      // a job's arrow colour
    int logo;        // a job's gang logo (HUD sprite), or 0
    int arrowSprite; // the HUD arrow sprite
    bool shown;
};

struct Radar
{
    bool loaded = false; // the setting read
    int mode = 1;        // 0 off, 1 bottom right, 2 bottom left
    int epoch = 0;       // the district the map was built for (gta2_port_map_state)
    int px = 0;          // map image pixels per block
    std::vector<bool> rowDone;
    int rowsLeft = 0;
    std::vector<uint8_t> rowBuf;
    double builtAt = 0;  // when this district's map started
    std::unordered_map<int, int> sprites; // HUD sprite -> (width << 16) | height, 0: none
    std::vector<uint8_t> spriteBuf;
    double readAt = -1e9;
    std::vector<Poi> pois;
    std::vector<Mark> marks;
    std::vector<int32_t> rows;
};
Radar gRadar;

double NowMs()
{
    timespec t = {};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

void Load()
{
    if (gRadar.loaded)
    {
        return;
    }
    gRadar.loaded = true;
    gRadar.mode = static_cast<int>(std::min(2u, Gta2Native_RegistryDword(kScreen, "port_radar", 1)));
    gta2_port_set_hud_arrows(gRadar.mode ? 0 : 1);
}

// ---- drawing helpers (drawable pixels, top left origin) ----

void Tri(int id, float x0, float y0, float u0, float v0, float x1, float y1, float u1, float v1, float x2, float y2,
         float u2, float v2, uint32_t rgba)
{
    const float t[12] = {x0, y0, u0, v0, x1, y1, u1, v1, x2, y2, u2, v2};
    Gta2Native_OverlayTriangle(id, t, rgba);
}
void Tri(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t rgba)
{
    Tri(0, x0, y0, 0, 0, x1, y1, 0, 0, x2, y2, 0, 0, rgba);
}

int Segments(float r)
{
    return std::clamp(static_cast<int>(r * 0.8f), 12, 72);
}

void Disc(float cx, float cy, float r, uint32_t rgba)
{
    const int n = Segments(r);
    for (int i = 0; i < n; ++i)
    {
        const double a0 = 2 * kPi * i / n, a1 = 2 * kPi * (i + 1) / n;
        Tri(cx, cy, cx + r * static_cast<float>(cos(a0)), cy + r * static_cast<float>(sin(a0)),
            cx + r * static_cast<float>(cos(a1)), cy + r * static_cast<float>(sin(a1)), rgba);
    }
}

void Ring(float cx, float cy, float r0, float r1, uint32_t rgba)
{
    const int n = Segments(r1);
    for (int i = 0; i < n; ++i)
    {
        const float c0 = static_cast<float>(cos(2 * kPi * i / n)), s0 = static_cast<float>(sin(2 * kPi * i / n));
        const float c1 = static_cast<float>(cos(2 * kPi * (i + 1) / n)), s1 = static_cast<float>(sin(2 * kPi * (i + 1) / n));
        Tri(cx + r0 * c0, cy + r0 * s0, cx + r1 * c0, cy + r1 * s0, cx + r1 * c1, cy + r1 * s1, rgba);
        Tri(cx + r0 * c0, cy + r0 * s0, cx + r1 * c1, cy + r1 * s1, cx + r0 * c1, cy + r0 * s1, rgba);
    }
}

// An arrow pointing at angle (radians, x right, y down), r from its middle to its tip, outlined.
void Arrow(float x, float y, double angle, float r, uint32_t rgba, float outline)
{
    const float c = static_cast<float>(cos(angle)), s = static_cast<float>(sin(angle));
    auto at = [&](float ax, float ay, float k, float* ox, float* oy) {
        *ox = x + (ax * c - ay * s) * k;
        *oy = y + (ax * s + ay * c) * k;
    };
    for (int pass = 0; pass < 2; ++pass)
    {
        const float k = pass == 0 ? 1 + outline / r * 1.6f : 1;
        const uint32_t colour = pass == 0 ? 0xff000000 : rgba;
        float tx, ty, lx, ly, nx, ny, rx, ry;
        at(r, 0, k, &tx, &ty);
        at(-r * 0.7f, r * 0.65f, k, &lx, &ly);
        at(-r * 0.35f, 0, k, &nx, &ny);
        at(-r * 0.7f, -r * 0.65f, k, &rx, &ry);
        Tri(tx, ty, lx, ly, nx, ny, colour);
        Tri(tx, ty, nx, ny, rx, ry, colour);
    }
}

void Rect(float x0, float y0, float x1, float y1, uint32_t rgba)
{
    Tri(x0, y0, x1, y0, x1, y1, rgba);
    Tri(x0, y0, x1, y1, x0, y1, rgba);
}

void Diamond(float x, float y, float r, uint32_t rgba, float outline)
{
    for (int pass = 0; pass < 2; ++pass)
    {
        const float k = pass == 0 ? r + outline * 1.4f : r;
        const uint32_t colour = pass == 0 ? 0xff000000 : rgba;
        Tri(x, y - k, x + k, y, x, y + k, colour);
        Tri(x, y - k, x, y + k, x - k, y, colour);
    }
}

// A HUD sprite of the player's style, height pixels tall, rotated by angle; false if it has none.
bool Sprite(int idx, float x, float y, float height, double angle = 0)
{
    auto it = gRadar.sprites.find(idx);
    if (it == gRadar.sprites.end())
    {
        gRadar.spriteBuf.resize(64 * 64 * 4);
        const int wh = gta2_port_sprite_rgba(kHudSprites, idx, gRadar.spriteBuf.data(), static_cast<int>(gRadar.spriteBuf.size()));
        if (wh)
        {
            Gta2Native_OverlayImage(kSpriteBase + idx, wh >> 16, wh & 0xffff, gRadar.spriteBuf.data(), true);
        }
        it = gRadar.sprites.emplace(idx, wh).first;
    }
    if (!it->second)
    {
        return false;
    }
    const float w = static_cast<float>(it->second >> 16), h = static_cast<float>(it->second & 0xffff);
    const float k = height / h;
    const float hw = w * k / 2, hh = height / 2;
    const float c = static_cast<float>(cos(angle)), s = static_cast<float>(sin(angle));
    float px[4], py[4];
    const float cx[4] = {-hw, hw, hw, -hw}, cy[4] = {-hh, -hh, hh, hh};
    for (int i = 0; i < 4; ++i)
    {
        px[i] = x + cx[i] * c - cy[i] * s;
        py[i] = y + cx[i] * s + cy[i] * c;
    }
    const int id = kSpriteBase + idx;
    Tri(id, px[0], py[0], 0, 0, px[1], py[1], 1, 0, px[2], py[2], 1, 1, kWhite);
    Tri(id, px[0], py[0], 0, 0, px[2], py[2], 1, 1, px[3], py[3], 0, 1, kWhite);
    return true;
}

// ---- the map image ----

void NewDistrict(int epoch, int px)
{
    for (const auto& kv : gRadar.sprites)
    {
        if (kv.second)
        {
            Gta2Native_OverlayFreeImage(kSpriteBase + kv.first);
        }
    }
    gRadar.sprites.clear();
    gRadar.epoch = epoch;
    gRadar.px = px;
    gRadar.rowDone.assign(256, false);
    gRadar.rowsLeft = 256;
    gRadar.rowBuf.assign(static_cast<size_t>(256) * px * px * 4, 0);
    gRadar.builtAt = NowMs();
    gRadar.readAt = -1e9;
    Gta2Native_OverlayImage(kMapImage, 256 * px, 256 * px, nullptr, true);
}

// A few more block rows of the map, the nearest to the player first.
void BuildRows(double playerY)
{
    if (gRadar.rowsLeft <= 0)
    {
        return;
    }
    const double start = NowMs();
    do
    {
        const int at = std::clamp(static_cast<int>(playerY), 0, 255);
        int row = -1;
        for (int d = 0; d < 256 && row < 0; ++d)
        {
            if (at - d >= 0 && !gRadar.rowDone[at - d])
                row = at - d;
            else if (at + d < 256 && !gRadar.rowDone[at + d])
                row = at + d;
        }
        if (row < 0 || !gta2_port_map_render_rows(row, 1, gRadar.px, gRadar.rowBuf.data()))
        {
            return;
        }
        Gta2Native_OverlayImageRows(kMapImage, 256 * gRadar.px, row * gRadar.px, gRadar.px, gRadar.rowBuf.data());
        gRadar.rowDone[row] = true;
        if (--gRadar.rowsLeft == 0)
        {
            fprintf(stderr, "gta2: radar: map built (%d px a block) in %.1f s\n", gRadar.px, (NowMs() - gRadar.builtAt) / 1000.0);
        }
    } while (gRadar.rowsLeft > 0 && NowMs() - start < kBudgetMs);
}

// ---- what it shows ----

int ReadRows(int (*fn)(int32_t*, int), int stride, int max)
{
    gRadar.rows.resize(static_cast<size_t>(stride) * max);
    return fn(gRadar.rows.data(), max);
}

void ReadPoints()
{
    gRadar.pois.clear();
    std::vector<Poi> doors;
    const int n = ReadRows(gta2_port_map_objects, 5, 1024);
    for (int i = 0; i < n; ++i)
    {
        const int32_t* o = &gRadar.rows[i * 5];
        const int m = o[0];
        const Poi p{o[1] / kFx, o[2] / kFx, kWeapon};
        if (m == 130)
            doors.push_back({p.x, p.y, kGarage});
        else if (m == 143)
            gRadar.pois.push_back({p.x, p.y, kCrusher});
        else if (m == 286)
            gRadar.pois.push_back({p.x, p.y, kFrenzy});
        else if (m >= 200 && m <= 227)
            gRadar.pois.push_back({p.x, p.y, kWeapon});
        else if (m >= 228 && m <= 249)
            gRadar.pois.push_back({p.x, p.y, kPowerup});
        else if (m == 266)
            gRadar.pois.push_back({p.x, p.y, kToken});
    }
    // a garage has several doors (respray, bombs, guns): one marker, in their middle
    struct Garage
    {
        double sx, sy;
        int count;
    };
    std::vector<Garage> garages;
    for (const Poi& d : doors)
    {
        bool joined = false;
        for (Garage& g : garages)
        {
            if (hypot(g.sx / g.count - d.x, g.sy / g.count - d.y) < 8)
            {
                g.sx += d.x, g.sy += d.y, ++g.count;
                joined = true;
                break;
            }
        }
        if (!joined)
            garages.push_back({d.x, d.y, 1});
    }
    for (const Garage& g : garages)
        gRadar.pois.push_back({g.sx / g.count, g.sy / g.count, kGarage});
    const int t = ReadRows(gta2_port_map_triggers, 4, 64);
    for (int i = 0; i < t; ++i)
    {
        const int32_t* o = &gRadar.rows[i * 4];
        if (o[3])
            gRadar.pois.push_back({o[0] / kFx, o[1] / kFx, kSave});
    }

    // gang jobs and mission targets, from the HUD's arrows (web/map.js jobs())
    int gangColour[10] = {};
    const int g = ReadRows(gta2_port_map_gangs, 6, 10);
    for (int i = 0; i < g; ++i)
    {
        const int32_t* o = &gRadar.rows[i * 6];
        if (o[0] >= 0 && o[0] < 10)
            gangColour[o[0]] = o[1];
    }
    gRadar.marks.clear();
    const int a = ReadRows(gta2_port_map_arrows, 11, 64);
    for (int i = 0; i < a; ++i)
    {
        const int32_t* o = &gRadar.rows[i * 11];
        const int gang = o[0], colour = o[1], kind = o[2];
        Mark m{o[3] / kFx, o[4] / kFx, false, colour, 0, o[10], o[6] != 0};
        JobStyle style;
        if (gang >= 0 && gang < 10 && JobOf(colour, &style) && (kind == 4 || kind == 5))
        {
            m.job = true;
            m.logo = gangColour[gang] ? 63 + gangColour[gang] : 0;
            bool same = false;
            for (Mark& j : gRadar.marks)
            {
                if (j.job && j.logo == m.logo && j.colour == colour && hypot(j.x - m.x, j.y - m.y) < 4)
                {
                    j.shown = j.shown || m.shown;
                    same = true;
                    break;
                }
            }
            if (!same)
                gRadar.marks.push_back(m);
        }
        else
        {
            m.shown = true; // the mission's target: always
            gRadar.marks.push_back(m);
        }
    }
}

// The other players and the waypoints (gta2-online, gta2-lan), [kind, x, y, rotation, colour] each.
int NetMarks(int32_t* out, int max)
{
    return Gta2Net_RadarMarks ? Gta2Net_RadarMarks(out, max) : 0;
}

// The full map, while the pause's quit prompt shows: over the screen but the prompt's band at the
// bottom edge, centred on the player (kept inside the district), 64 blocks from top to bottom.
void DrawMapScreen(int dw, int dh, double px0, double py0, double heading, double now)
{
    const float boxH = std::min(static_cast<float>(dh), dw * 0.75f); // the HUD's 4:3 box
    const float u = boxH / 480.0f;
    const float bandTop = dh - (480 - std::clamp(static_cast<int>(gGta2PortQuitBandTop), 240, 470)) * u;
    const float m = 10 * u;
    const float x0 = m, y0 = m, x1 = dw - m, y1 = bandTop - m / 2;
    if (x1 - x0 < 40 || y1 - y0 < 40)
    {
        return;
    }
    Rect(0, 0, static_cast<float>(dw), bandTop, 0xf0000000);
    Rect(x0 - std::max(1.0f, u), y0 - std::max(1.0f, u), x1 + std::max(1.0f, u), y1 + std::max(1.0f, u), 0x60ffffff);
    Rect(x0, y0, x1, y1, kGround);
    const float k = (y1 - y0) / 64.0f; // pixels per block
    const double halfW = (x1 - x0) / 2 / k, halfH = (y1 - y0) / 2 / k;
    const double vcx = halfW >= 128 ? 128 : std::clamp(px0, halfW, 256 - halfW);
    const double vcy = halfH >= 128 ? 128 : std::clamp(py0, halfH, 256 - halfH);
    const float ox = (x0 + x1) / 2, oy = (y0 + y1) / 2;
    auto sx = [&](double wx) { return ox + static_cast<float>((wx - vcx) * k); };
    auto sy = [&](double wy) { return oy + static_cast<float>((wy - vcy) * k); };
    // the map, where the view meets the district
    {
        const double wx0 = std::max(0.0, vcx - halfW), wx1 = std::min(256.0, vcx + halfW);
        const double wy0 = std::max(0.0, vcy - halfH), wy1 = std::min(256.0, vcy + halfH);
        const float u0 = static_cast<float>(wx0 / 256), u1 = static_cast<float>(wx1 / 256);
        const float v0 = static_cast<float>(wy0 / 256), v1 = static_cast<float>(wy1 / 256);
        Tri(kMapImage, sx(wx0), sy(wy0), u0, v0, sx(wx1), sy(wy0), u1, v0, sx(wx1), sy(wy1), u1, v1, kWhite);
        Tri(kMapImage, sx(wx0), sy(wy0), u0, v0, sx(wx1), sy(wy1), u1, v1, sx(wx0), sy(wy1), u0, v1, kWhite);
    }
    auto inside = [&](float x, float y, float pad) { return x > x0 + pad && x < x1 - pad && y > y0 + pad && y < y1 - pad; };
    const float dot = std::max(2.0f, 3.2f * u);
    for (const Poi& p : gRadar.pois)
    {
        const float x = sx(p.x), y = sy(p.y);
        if (p.kind == kToken || !inside(x, y, dot + 1))
            continue;
        Disc(x, y, dot + std::max(1.0f, u), 0xff000000);
        Disc(x, y, dot, kKindColour[p.kind]);
    }
    for (const Mark& mk : gRadar.marks)
    {
        const float x = sx(mk.x), y = sy(mk.y);
        if (!mk.shown || !inside(x, y, 8 * u))
            continue;
        if (mk.job)
        {
            JobStyle style{kTargetRed, 0};
            JobOf(mk.colour, &style);
            const float h = 13 * u;
            Disc(x, y, h * 0.62f + std::max(1.0f, u), style.colour);
            Disc(x, y, h * 0.62f, 0xa6000000);
            if (!mk.logo || !Sprite(mk.logo, x, y, h * 0.8f))
                Disc(x, y, h * 0.35f, style.colour);
            if (style.dot)
                Sprite(style.dot, x + h * 0.45f, y - h * 0.45f, 6 * u);
        }
        else
        {
            const float t = static_cast<float>(fmod(now, 1000.0) / 1000.0);
            const float rr = (5 + 4 * t) * u;
            Ring(x, y, rr, rr + 2 * u, (static_cast<uint32_t>((1 - t) * 255) << 24) | (kTargetRed & 0xffffff));
            Disc(x, y, 4 * u + std::max(1.0f, u), kWhite);
            Disc(x, y, 4 * u, kTargetRed);
        }
    }
    int32_t net[13 * 5];
    const int n = NetMarks(net, 13);
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int i = 0; i < n; ++i)
        {
            const int32_t* o = &net[i * 5];
            const bool player = o[0] == 0;
            const float x = sx(o[1] / kFx), y = sy(o[2] / kFx);
            if (player != (pass == 1) || !inside(x, y, 6 * u))
                continue;
            const uint32_t colour = o[0] == 2 ? kOwnWaypoint : static_cast<uint32_t>(o[4]);
            if (player)
            {
                const double h = o[3] / 1440.0 * 2 * kPi;
                Arrow(x, y, atan2(cos(h), sin(h)), 7 * u, colour, u);
            }
            else
            {
                Diamond(x, y, 5 * u, colour, u);
            }
        }
    }
    Arrow(sx(px0), sy(py0), atan2(cos(heading), sin(heading)), 9 * u, kWhite, std::max(1.0f, u));
}

} // namespace

extern "C" {

// Options: Radar Off (0), Right (1) or Left (2), kept in the registry (conf/registry.txt).
int Gta2Native_RadarMode()
{
    Load();
    return gRadar.mode;
}

void Gta2Native_SetRadarMode(int mode)
{
    Load();
    gRadar.mode = std::clamp(mode, 0, 2);
    gta2_port_reg_set_dword(kScreen, "port_radar", static_cast<unsigned int>(gRadar.mode));
    gta2_port_set_hud_arrows(gRadar.mode ? 0 : 1);
}

// render/gles_renderer.cpp, after the game's frame, under the port's screens.
void Gta2Native_RadarDraw(int dw, int dh)
{
    Load();
    const bool playing = Gta2Native_IsPlaying();
    const bool mapScreen = playing && Gta2Native_QuitPromptShown() && !Gta2Native_OptionsOpen();
    gGta2PortQuitAtBottom = mapScreen ? 1 : 0; // the next frame's quit prompt (patch 0020)
    if (!playing || (!gRadar.mode && !mapScreen))
    {
        return;
    }
    const int32_t* s = gta2_port_map_state();
    if (!s[0] || !s[3])
    {
        return;
    }
    const bool drawn = s[14] != 0 || s[15] != 0; // interpolated: where the last draw put the player
    const double px0 = (drawn ? s[14] : s[4]) / kFx, py0 = (drawn ? s[15] : s[5]) / kFx;
    const double heading = (drawn ? s[16] : s[7]) / 1440.0 * 2 * kPi;

    // layout: web/map.js drawMinimap, scaled from its 480-pixel-high view
    const float u = dh / 480.0f;
    const float r = roundf(std::max(40.0f, dh * 0.15f)); // inner radius
    const float border = std::max(3.0f, roundf(r * 0.11f));
    const float cx = gRadar.mode == 2 ? r + border + 14 * u : dw - r - border - 14 * u;
    const float cy = dh - r - border - 14 * u;
    const float scale = r / 9.0f; // pixels per block: 18 blocks across
    const float rim = r + border / 2;

    // the district's map: an image pixel for each screen pixel or better, at most 8 a block
    if (s[1] != gRadar.epoch)
    {
        NewDistrict(s[1], scale > 6 || mapScreen ? 8 : 4);
    }
    BuildRows(py0);
    const double now = NowMs();
    if (now - gRadar.readAt > kRefreshMs)
    {
        gRadar.readAt = now;
        ReadPoints();
    }
    if (mapScreen)
    {
        DrawMapScreen(dw, dh, px0, py0, heading, now);
        return;
    }

    // the ground, then the map over it: a fan whose texture coordinates are the blocks it covers
    Disc(cx, cy, r, kGround);
    {
        const int n = Segments(r);
        const double half = r / scale;
        const float cu = static_cast<float>(px0 / 256), cv = static_cast<float>(py0 / 256);
        for (int i = 0; i < n; ++i)
        {
            const double a0 = 2 * kPi * i / n, a1 = 2 * kPi * (i + 1) / n;
            const float c0 = static_cast<float>(cos(a0)), s0 = static_cast<float>(sin(a0));
            const float c1 = static_cast<float>(cos(a1)), s1 = static_cast<float>(sin(a1));
            const float k = static_cast<float>(half / 256);
            Tri(kMapImage, cx, cy, cu, cv, cx + r * c0, cy + r * s0, cu + k * c0, cv + k * s0, cx + r * c1, cy + r * s1,
                cu + k * c1, cv + k * s1, kWhite);
        }
    }

    // points of interest inside
    const float dot = std::max(2.0f, r * 0.045f);
    for (const Poi& p : gRadar.pois)
    {
        const float dx = static_cast<float>((p.x - px0) * scale), dy = static_cast<float>((p.y - py0) * scale);
        if (hypotf(dx, dy) < r - dot - 2)
        {
            Disc(cx + dx, cy + dy, dot + std::max(1.0f, u), 0xff000000);
            Disc(cx + dx, cy + dy, dot, kKindColour[p.kind]);
        }
    }

    // the border: thick and black, with a thin light edge inside; the north on it
    Ring(cx, cy, r, r + border, kBlack);
    Ring(cx, cy, r - std::max(1.0f, u * 0.75f), r, kRim);
    {
        const float nr = border * 1.3f;
        Disc(cx, cy - rim, nr, kBlack);
        Ring(cx, cy - rim, nr - std::max(1.0f, u), nr, 0xb3ffffff);
        const float fp = std::max(1.0f, floorf(nr * 1.1f / 7));
        Gta2Native_OverlayText(cx - 2.5f * fp, cy - rim - 3.5f * fp, fp, kWhite, "N");
    }

    // inside: where it is; outside: on the rim, at its bearing
    struct Place
    {
        float x, y;
        bool inside;
        double angle;
    };
    auto place = [&](double wx, double wy, float inset) {
        const float dx = static_cast<float>((wx - px0) * scale), dy = static_cast<float>((wy - py0) * scale);
        const double a = atan2(dy, dx);
        if (hypotf(dx, dy) < r - inset)
            return Place{cx + dx, cy + dy, true, a};
        return Place{cx + static_cast<float>(cos(a)) * rim, cy + static_cast<float>(sin(a)) * rim, false, a};
    };

    // the game's gang job and mission arrows
    const float rimArrow = std::max(18 * u, border * 3);
    for (const Mark& m : gRadar.marks)
    {
        if (!m.shown)
            continue;
        const Place at = place(m.x, m.y, 8 * u);
        JobStyle style{kTargetRed, 0};
        if (m.job)
            JobOf(m.colour, &style);
        if (at.inside && m.job)
        {
            const float h = 10 * u;
            Disc(at.x, at.y, h * 0.62f + std::max(1.0f, u), style.colour);
            Disc(at.x, at.y, h * 0.62f, 0xa6000000);
            if (!m.logo || !Sprite(m.logo, at.x, at.y, h * 0.8f))
                Disc(at.x, at.y, h * 0.35f, style.colour);
            if (style.dot)
                Sprite(style.dot, at.x + h * 0.45f, at.y - h * 0.45f, 5 * u);
        }
        else if (at.inside)
        {
            // the mission's target: a pulsing ring
            const float t = static_cast<float>(fmod(now, 1000.0) / 1000.0);
            const float rr = (4 + 3 * t) * u;
            const uint32_t alpha = static_cast<uint32_t>((1 - t) * 255) << 24;
            Ring(at.x, at.y, rr, rr + 2 * u, alpha | (kTargetRed & 0xffffff));
            Disc(at.x, at.y, 3.2f * u + std::max(1.0f, u), kWhite);
            Disc(at.x, at.y, 3.2f * u, kTargetRed);
        }
        else
        {
            // the game's arrow sprite (tip up when unrotated) on the rim, pointing out
            const double angle = at.angle + kPi / 2;
            const int sprite = m.job ? (m.arrowSprite ? m.arrowSprite : 7) : m.arrowSprite;
            if (Sprite(sprite, at.x, at.y, rimArrow, angle))
            {
                if (m.job && style.dot)
                    Sprite(style.dot, at.x, at.y, 6 * u, angle);
            }
            else
            {
                Arrow(at.x, at.y, at.angle, 6 * u, style.colour, u);
            }
        }
    }

    // gta2-online, gta2-lan: waypoints, then the other players, in their colours
    {
        int32_t net[13 * 5];
        const int n = NetMarks(net, 13);
        for (int pass = 0; pass < 2; ++pass)
        {
            for (int i = 0; i < n; ++i)
            {
                const int32_t* o = &net[i * 5];
                const bool player = o[0] == 0;
                if (player != (pass == 1))
                    continue;
                const uint32_t colour = o[0] == 2 ? kOwnWaypoint : static_cast<uint32_t>(o[4]);
                const Place at = place(o[1] / kFx, o[2] / kFx, 6 * u);
                if (player && at.inside)
                {
                    const double h = o[3] / 1440.0 * 2 * kPi;
                    Arrow(at.x, at.y, atan2(cos(h), sin(h)), 6 * u, colour, u);
                }
                else if (player)
                {
                    Disc(at.x, at.y, std::max(4 * u, border * 1.1f) + std::max(1.0f, u), 0xff000000);
                    Disc(at.x, at.y, std::max(4 * u, border * 1.1f), colour);
                }
                else
                {
                    Diamond(at.x, at.y, at.inside ? 4 * u : std::max(4 * u, border * 1.2f), colour, u);
                }
            }
        }
    }

    // the player: heading as (sin a, cos a) in map coordinates (web/map.js drawPlayer)
    Arrow(cx, cy, atan2(cos(heading), sin(heading)), 7.5f * u, kWhite, std::max(1.0f, u));
}

} // extern "C"
#endif
