// gta2-online on screen during a match, drawn over the game in the Options screen's style
// (render/gles_renderer.cpp's overlay, through platform/options_native.cpp's hooks):
//   - the scoreboard while Select is held (Tab on a keyboard), the browser's columns and
//     order (web/shell.js coopView): player, kills, deaths, points; free-for-all by kills,
//     free roam by points, Gang Wars by crew; with the four quick messages as a legend;
//   - chat lines, the last four for 9 s, the sender's name in the player's colour
//     (Options: Chat messages on/off);
//   - the quick messages: Select + up, down, left, right (conf/net.ini chat_up...);
//   - waypoints: the other players' (from the browser's map or a console) and this player's
//     own (Select + L1 sets it where the player stands, Select + R1 clears it; reached, it
//     goes, as in the browser): a marker on the spot when it is in view, else on the screen's
//     edge in its direction with the distance in blocks (Options: Waypoints on/off hides the
//     other players');
//   - the final standings when the match ends, before the lobby.
// Multiplayer build with a window only (gta2-online).
#if !defined(GTA2_NATIVE_HEADLESS)
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <math.h>

#include <algorithm>
#include <deque>
#include <string>
#include <vector>

#include "Camera.hpp"
#include "map_0x370.hpp"

#include "coop_native.h"
#include "net_session.h"

extern "C" void Gta2Native_OverlayRect(float x, float y, float w, float h, uint32_t rgba);
extern "C" void Gta2Native_OverlayText(float x, float y, float px, uint32_t rgba, const char* text);
extern "C" void Gta2Native_OverlayTextExact(float x, float y, float px, uint32_t rgba, const char* text); // names, chat
extern "C" bool Gta2Native_PumpEvents(); // platform/window_sdl.cpp
extern "C" void gta2_js_present(const void* cmdData, int cmdCount, const void* vertData, int vertCount, int refW);
extern "C" const int32_t* gta2_port_coop_players(); // port/platform/coop_web.cpp
extern "C" bool Gta2Native_PadAPressed();           // input_sdl.cpp
extern "C" const int32_t* gta2_dev_state();          // port/platform/dev_hooks.cpp: player
extern "C" s32 gGta2PortDrawnCam[2]; // port_interpolation.cpp: the camera's x, y as last drawn

namespace gta2net
{
namespace
{
constexpr int kFields = 12; // gta2_port_coop_players: per slot
constexpr double kChatMs = 9000;
constexpr size_t kChatLines = 4;
// Player colours by slot, as web/shell.js PLAYER_COLOURS (#4fc3ff, #ff6b6b, ...), as ABGR.
const uint32_t kColours[6] = {0xffffc34f, 0xff6b6bff, 0xff81e07e, 0xffff92c7, 0xff4da9ff, 0xffd97aff};
const uint32_t kWhite = 0xffffffff, kGrey = 0xffa0a0a0, kYellow = 0xff30d0ff;

struct ChatLine
{
    int slot;
    std::string text;
    double at;
};
std::deque<ChatLine> gChat;
struct Waypoint
{
    bool set = false;
    double x = 0, y = 0, z = 0; // blocks; z: the ground's height there, for the perspective
};
Waypoint gWaypoints[6];
bool gOwnArmed = false; // this player's waypoint goes once reached after being farther away
constexpr double kReached = 2;  // blocks, as WAYPOINT_REACHED in web/map.js
bool gScoreboard = false;
bool gMatchOver = false;

double Now()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

std::string NameOf(int slot)
{
    const Json& p = CoopMatch()["players"][slot];
    std::string n = p["name"].IsString() ? p["name"].Str() : "player " + std::to_string(slot + 1);
    for (char& c : n)
        c = c >= 0x20 && c <= 0x7e ? c : '?'; // the overlay font is ASCII
    return n;
}

std::string Money(int v)
{
    std::string digits = std::to_string(v < 0 ? -v : v);
    std::string out;
    for (size_t i = 0; i < digits.size(); ++i)
    {
        if (i && (digits.size() - i) % 3 == 0)
            out += ',';
        out += digits[i];
    }
    return (v < 0 ? "-$" : "$") + out;
}

struct Row
{
    std::string name;
    int kills, deaths, points;
    uint32_t colour;
    bool strong;
};

// The standings in the browser's order and columns; title: who leads.
std::vector<Row> Standings(std::string* title)
{
    const int32_t* v = gta2_port_coop_players();
    const Json& match = CoopMatch();
    const std::string mode = match["mode"].IsString() ? match["mode"].Str() : "roam";
    const int me = CoopMySlot();
    std::vector<Row> players;
    std::vector<int> slots;
    for (int s = 0; s < 6; ++s)
    {
        const int32_t* o = v + s * kFields;
        if (!o[0] || s >= static_cast<int>(match["players"].size()))
            continue;
        players.push_back({NameOf(s) + (s == me ? " (you)" : ""), o[7], o[8], o[11], kColours[s], s == me});
        slots.push_back(s);
    }
    std::vector<Row> rows;
    if (mode == "wars")
    {
        // one line per crew (its gang, kills and points added up), then its members
        const Json& crews = match["crews"];
        struct Team
        {
            size_t crew;
            int kills, points;
        };
        std::vector<Team> teams;
        for (size_t c = 0; c < crews.size(); ++c)
        {
            Team t{c, 0, 0};
            for (const Json& m : crews[c]["members"].Items())
                for (size_t i = 0; i < slots.size(); ++i)
                    if (match["players"][slots[i]]["id"].Str() == m.Str())
                    {
                        t.kills += players[i].kills;
                        t.points += players[i].points;
                    }
            teams.push_back(t);
        }
        std::sort(teams.begin(), teams.end(), [](const Team& a, const Team& b) { return a.points > b.points; });
        for (const Team& t : teams)
        {
            const Json& c = crews[t.crew];
            rows.push_back({c["name"].Str() + " - " + c["gang"].Str(), t.kills, -1, t.points, kYellow, true});
            std::vector<size_t> members;
            for (const Json& m : c["members"].Items())
                for (size_t i = 0; i < slots.size(); ++i)
                    if (match["players"][slots[i]]["id"].Str() == m.Str())
                        members.push_back(i);
            std::sort(members.begin(), members.end(),
                      [&](size_t a, size_t b) { return players[a].points > players[b].points; });
            for (size_t i : members)
            {
                Row r = players[i];
                r.name = "  " + r.name;
                r.strong = false;
                rows.push_back(r);
            }
        }
        if (title)
        {
            const bool tied = teams.size() > 1 && teams[0].points == teams[1].points;
            *title = teams.empty() ? "Gang Wars"
                : tied ? "Gang Wars: tied"
                       : crews[teams[0].crew]["name"].Str() + " leads with " + Money(teams[0].points);
        }
        return rows;
    }
    rows = players;
    if (mode == "ffa")
        std::stable_sort(rows.begin(), rows.end(),
                         [](const Row& a, const Row& b) { return a.kills != b.kills ? a.kills > b.kills : a.points > b.points; });
    else
        std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.points > b.points; });
    if (title)
    {
        if (rows.empty())
            *title = "";
        else if (mode == "ffa")
            *title = rows[0].name + " leads with " + std::to_string(rows[0].kills) + (rows[0].kills == 1 ? " kill" : " kills");
        else
            *title = rows[0].name + " leads with " + Money(rows[0].points);
    }
    return rows;
}

float PixelSize(int dw, int dh, int cols, int rows)
{
    return static_cast<float>(std::max(1, std::min(dh / (rows * 12 + 8), dw / (cols * 6 + 8))));
}

void DrawScoreboard(int dw, int dh, bool final)
{
    std::string title;
    const std::vector<Row> rows = Standings(&title);
    constexpr int kCols = 44;
    const int lines = static_cast<int>(rows.size()) + (final ? 5 : 9);
    const float px = PixelSize(dw, dh, kCols, std::max(lines, 14));
    const float cell = 6 * px;
    const float lineH = 12 * px;
    const float width = kCols * cell;
    const float x0 = (dw - width) / 2;
    float y = (dh - lines * lineH) / 2;
    Gta2Native_OverlayRect(x0 - 2 * cell, y - lineH, width + 4 * cell, (lines + 1) * lineH, 0xd8000000);
    if (!CoopWinner().empty())
        title = CoopWinner() + " win the Gang War";
    const std::string head = final ? "Match over: " + title : title;
    Gta2Native_OverlayText(x0, y, px, kYellow, head.substr(0, kCols).c_str());
    y += 1.5f * lineH;
    // columns: name, kills, deaths, points (right-aligned)
    auto right = [&](float colEnd, uint32_t colour, const std::string& t) {
        Gta2Native_OverlayText(x0 + colEnd * cell - static_cast<float>(t.size()) * cell, y, px, colour, t.c_str());
    };
    Gta2Native_OverlayText(x0, y, px, kGrey, "Player");
    right(28, kGrey, "Kills");
    right(35, kGrey, "Deaths");
    right(kCols, kGrey, "Points");
    y += lineH;
    for (const Row& r : rows)
    {
        if (r.strong)
            Gta2Native_OverlayRect(x0 - cell, y - 3 * px, width + 2 * cell, lineH, 0xff303030);
        Gta2Native_OverlayTextExact(x0, y, px, r.colour, r.name.substr(0, 20).c_str());
        right(28, kWhite, std::to_string(r.kills));
        if (r.deaths >= 0)
            right(35, kWhite, std::to_string(r.deaths));
        right(kCols, kWhite, Money(r.points));
        y += lineH;
    }
    y += lineH;
    if (final)
    {
        Gta2Native_OverlayText(x0, y, px, kGrey, "A: back to the lobby");
        return;
    }
    // the quick messages' legend
    const NetConfig* c = gConfig;
    if (c)
    {
        const char* arrows[4] = {"Up", "Down", "Left", "Right"};
        Gta2Native_OverlayText(x0, y, px, kGrey, "Select + L1 / R1: set / clear your waypoint");
        y += lineH;
        Gta2Native_OverlayText(x0, y, px, kGrey, "Select + d-pad sends:");
        y += lineH;
        for (int i = 0; i < 4; ++i)
        {
            char buf[96];
            snprintf(buf, sizeof(buf), "%s: %s", arrows[i], c->chat[i].c_str());
            Gta2Native_OverlayText(x0 + (i % 2) * (kCols / 2) * cell, y + (i / 2) * lineH, px, kWhite,
                                   std::string(buf).substr(0, kCols / 2 - 1).c_str());
        }
    }
}

// The player's position in blocks; false without a player on the map.
bool PlayerAt(double* x, double* y, double* z = nullptr)
{
    const int32_t* s = gta2_dev_state();
    if (!s[0])
        return false;
    *x = s[2] / 16384.0;
    *y = s[3] / 16384.0;
    if (z)
        *z = s[4] / 16384.0;
    return true;
}

// The ground's height at a point (blocks), for a waypoint that came without one.
double GroundAt(double x, double y)
{
    if (!gMap_0x370_6F6268)
        return 2;
    return gMap_0x370_6F6268
        ->FindGroundZForCoord_4E5B60(Fix16(static_cast<s32>(x * 16384), 0), Fix16(static_cast<s32>(y * 16384), 0))
        .ToFloat();
}

void DrawWaypoints(int dw, int dh)
{
    double px0, py0;
    if (!PlayerAt(&px0, &py0))
        return;
    const int me = CoopMySlot();
    // this player's own: gone once reached (after having been farther away)
    if (me >= 0 && me < 6 && gWaypoints[me].set)
    {
        const double d = hypot(gWaypoints[me].x - px0, gWaypoints[me].y - py0);
        if (d > kReached + 1)
            gOwnArmed = true;
        else if (d <= kReached && gOwnArmed)
        {
            gWaypoints[me].set = false;
            if (gLobby)
                gLobby->ShareWaypoint(false, 0, 0);
        }
    }
    // the view camera as drawn (interpolated position, Modern camera's height)
    Camera_0xBC* cam = gViewCamera_676978;
    if (!cam || cam->field_68_screen_px_width <= 0 || cam->field_6C_screen_px_height <= 0)
        return;
    const double camX = gGta2PortDrawnCam[0] ? gGta2PortDrawnCam[0] / 16384.0 : cam->field_98_cam_pos2.field_0_x.ToFloat();
    const double camY = gGta2PortDrawnCam[1] ? gGta2PortDrawnCam[1] / 16384.0 : cam->field_98_cam_pos2.field_4_y.ToFloat();
    const double camZ = Gta2Port_PresentedCameraZ(cam).ToFloat();
    const double f60 = cam->field_60.x.ToFloat();
    const double toDw = static_cast<double>(dw) / cam->field_68_screen_px_width;
    const double toDh = static_cast<double>(dh) / cam->field_6C_screen_px_height;
    const float px = PixelSize(dw, dh, 52, 22);
    const float size = 7 * px;
    const float margin = 14 * px;
    for (int slot = 0; slot < 6; ++slot)
    {
        const Waypoint& w = gWaypoints[slot];
        if (!w.set || (slot != me && gConfig && !gConfig->showWaypoints))
            continue;
        // the game's own projection, at the ground's height there (Camera_0xBC
        // ProjectWorldToScreen_4B90E0: scale 1 / ((8 - z) + camera height))
        const double k = 1.0 / ((8.0 - w.z) + camZ);
        double sx = ((w.x - camX) * f60 * k + cam->field_70_screen_px_center_x) * toDw;
        double sy = ((w.y - camY) * f60 * k + cam->field_74_screen_px_center_y) * toDh;
        static const bool trace = getenv("GTA2_NET_TEST_HUD") != nullptr;
        static double lastTrace = 0;
        if (trace && Now() - lastTrace > 2000 && slot == 2)
        {
            lastTrace = Now();
            fprintf(stderr,
                    "gta2: hud: waypoint %d at (%.2f, %.2f, %.2f), camera (%.2f, %.2f, %.2f), f60 %.1f, screen %dx%d "
                    "centre (%d, %d), drawable %dx%d -> (%.0f, %.0f); player at (%.2f, %.2f)\n",
                    slot, w.x, w.y, w.z, camX, camY, camZ, f60, cam->field_68_screen_px_width, cam->field_6C_screen_px_height,
                    cam->field_70_screen_px_center_x, cam->field_74_screen_px_center_y, dw, dh, sx, sy, px0, py0);
        }
        const bool inView = sx >= margin && sx <= dw - margin && sy >= margin && sy <= dh - margin;
        std::string label = slot == me ? "Your waypoint" : NameOf(slot);
        if (!inView)
        {
            // on the edge, in the waypoint's direction from the screen's centre
            const double cx = dw / 2.0, cy = dh / 2.0;
            const double dx = sx - cx, dy = sy - cy;
            const double k = std::min((cx - margin) / std::max(1e-6, fabs(dx)), (cy - margin) / std::max(1e-6, fabs(dy)));
            sx = cx + dx * k;
            sy = cy + dy * k;
            label += " " + std::to_string(static_cast<int>(lround(hypot(w.x - px0, w.y - py0))));
        }
        const uint32_t colour = kColours[slot % 6];
        Gta2Native_OverlayRect(static_cast<float>(sx) - size / 2 - px, static_cast<float>(sy) - size / 2 - px, size + 2 * px,
                               size + 2 * px, 0xff000000);
        Gta2Native_OverlayRect(static_cast<float>(sx) - size / 2, static_cast<float>(sy) - size / 2, size, size, colour);
        // the label beside it, kept on the screen
        const float tw = static_cast<float>(label.size()) * 6 * px;
        float tx = static_cast<float>(sx) + size;
        if (tx + tw > dw - 2 * px)
            tx = static_cast<float>(sx) - size - tw;
        const float ty = static_cast<float>(sy) - 4 * px;
        Gta2Native_OverlayRect(tx - px, ty - 2 * px, tw + 2 * px, 11 * px, 0x90000000);
        Gta2Native_OverlayTextExact(tx, ty, px, colour, label.c_str());
    }
}

void DrawChat(int dw, int dh)
{
    const double now = Now();
    while (!gChat.empty() && now - gChat.front().at > kChatMs)
        gChat.pop_front();
    if (gChat.empty())
        return;
    constexpr int kCols = 52;
    const float px = PixelSize(dw, dh, kCols, 22);
    const float lineH = 12 * px;
    // bottom left, above the game's own bottom line of text
    float y = dh - (gChat.size() * 2 + 4) * lineH;
    const float x0 = 8 * px;
    for (const ChatLine& l : gChat)
    {
        const std::string name = (l.slot >= 0 ? NameOf(l.slot) : std::string("?")) + ": ";
        std::string text = l.text;
        for (char& ch : text)
            ch = ch >= 0x20 && ch <= 0x7e ? ch : '?';
        // the name in the player's colour, the text after it; a long text on two lines
        const int first = std::max(10, kCols - static_cast<int>(name.size()));
        Gta2Native_OverlayRect(x0 - 2 * px, y - 2 * px, (std::min<int>(kCols, static_cast<int>(name.size() + text.size())) + 1) * 6 * px,
                               lineH, 0x90000000);
        Gta2Native_OverlayTextExact(x0, y, px, l.slot >= 0 ? kColours[l.slot % 6] : kGrey, name.c_str());
        Gta2Native_OverlayTextExact(x0 + name.size() * 6 * px, y, px, kWhite, text.substr(0, first).c_str());
        y += lineH;
        if (static_cast<int>(text.size()) > first)
        {
            const std::string rest = text.substr(first, kCols);
            Gta2Native_OverlayRect(x0 - 2 * px, y - 2 * px, (rest.size() + 1) * 6 * px, lineH, 0x90000000);
            Gta2Native_OverlayTextExact(x0, y, px, kWhite, rest.c_str());
            y += lineH;
        }
    }
}
} // namespace

// coop_native.cpp: a player's waypoint changed.
void HudWaypoint(int slot, bool set, double x, double y)
{
    if (slot >= 0 && slot < 6)
        gWaypoints[slot] = {set, x, y, set ? GroundAt(x, y) : 0};
}

// coop_native.cpp: a chat message from a player (this one's own included).
void HudChat(int slot, const std::string& text)
{
    if (!gConfig || !gConfig->showChat)
        return;
    gChat.push_back({slot, text, Now()});
    while (gChat.size() > kChatLines)
        gChat.pop_front();
}

} // namespace gta2net

using namespace gta2net;

extern "C" void Gta2Net_QuickChat(int dir);
extern "C" void Gta2Net_Waypoint(bool set);

// input_sdl.cpp: Select (or Tab) held long enough in a match.
extern "C" void Gta2Net_SelectHeld(bool held)
{
    gScoreboard = held;
}

// input_sdl.cpp: Select + d-pad: 0 up, 1 down, 2 left, 3 right.
extern "C" void Gta2Net_QuickChat(int dir)
{
    if (!gConfig || !gLobby || dir < 0 || dir > 3)
        return;
    const std::string& text = gConfig->chat[dir];
    gLobby->SendChat(text);
    fprintf(stderr, "gta2: co-op: chat sent: %s\n", text.c_str());
    // our own line too, as the browser shows it (even with other players' messages hidden)
    gChat.push_back({CoopMySlot(), text, Now()});
    while (gChat.size() > kChatLines)
        gChat.pop_front();
}

// input_sdl.cpp: Select + L1 (set: this player's waypoint where they stand) or R1 (clear).
extern "C" void Gta2Net_Waypoint(bool set)
{
    const int me = CoopMySlot();
    double x, y;
    if (!gLobby || me < 0 || me >= 6)
        return;
    double z = 0;
    if (set && PlayerAt(&x, &y, &z))
    {
        gWaypoints[me] = {true, x, y, z};
        gOwnArmed = false;
        gLobby->ShareWaypoint(true, x, y);
        fprintf(stderr, "gta2: co-op: waypoint set at (%.1f, %.1f)\n", x, y);
    }
    else if (!set && gWaypoints[me].set)
    {
        gWaypoints[me].set = false;
        gLobby->ShareWaypoint(false, 0, 0);
        fprintf(stderr, "gta2: co-op: waypoint cleared\n");
    }
}

// platform/options_native.cpp, after the game's frame.
extern "C" void Gta2Net_HudDraw(int dw, int dh)
{
    if (CoopMatch().IsNull())
        return;
    // GTA2_NET_TEST_HUD=1, for screenshots: two quick messages 5 and 7 s into the match, the
    // scoreboard from 9 to 14 s
    static const bool test = getenv("GTA2_NET_TEST_HUD") != nullptr;
    static double start = 0;
    static int step = 0;
    if (test)
    {
        start = start ? start : Now();
        const double t = Now() - start;
        if (step == 0 && t > 5000)
            Gta2Net_QuickChat(0), step = 1;
        if (step == 1 && t > 7000)
        {
            Gta2Net_QuickChat(3);
            Gta2Net_Waypoint(true); // where we stand: in view
            double x, y;
            if (PlayerAt(&x, &y))
                HudWaypoint(1, true, x + 30, y - 12); // the other player's, far: on the edge
            if (PlayerAt(&x, &y))
                HudWaypoint(2, true, x + 3, y - 2); // near: in view, at the ground's height
            step = 2;
        }
        gScoreboard = t > 9000 && t < 14000;
    }
    if (gMatchOver)
    {
        DrawScoreboard(dw, dh, true);
        return;
    }
    DrawWaypoints(dw, dh);
    DrawChat(dw, dh);
    if (!CoopWinner().empty())
    {
        // the browser's banner: who won, until the match ends
        const std::string t = CoopWinner() + " win the Gang War!";
        const float px = PixelSize(dw, dh, 30, 14);
        const float w = static_cast<float>(t.size()) * 6 * px;
        const float x = (dw - w) / 2, y = dh * 0.18f;
        Gta2Native_OverlayRect(x - 6 * px, y - 4 * px, w + 12 * px, 15 * px, 0xd0000000);
        Gta2Native_OverlayTextExact(x, y, px, kYellow, t.c_str());
    }
    if (gScoreboard)
        DrawScoreboard(dw, dh, false);
}

// main.cpp, when the game ends a match: the final standings until A, or 10 s.
extern "C" void Gta2Net_MatchOverScreen()
{
    if (CoopMatch().IsNull())
        return;
    gMatchOver = true;
    gChat.clear();
    const double end = Now() + 10000;
    bool released = false; // an A held from the game (shooting) does not count
    while (Now() < end && Gta2Native_PumpEvents())
    {
        const bool a = Gta2Native_PadAPressed();
        if (a && released)
            break;
        released = released || !a;
        gta2_js_present(nullptr, 0, nullptr, 0, 640);
        timespec ts = {0, 10 * 1000 * 1000};
        nanosleep(&ts, nullptr);
    }
    gMatchOver = false;
}

// Options (multiplayer lines): the other players' chat messages and waypoints on screen.
extern "C" bool Gta2Net_ShowChat()
{
    return gConfig && gConfig->showChat;
}
extern "C" void Gta2Net_SetShowChat(bool on)
{
    if (!gConfig)
        return;
    gConfig->showChat = on;
    if (!on)
        gChat.clear();
    SaveNetConfig(*gConfig);
}
extern "C" bool Gta2Net_ShowWaypoints()
{
    return gConfig && gConfig->showWaypoints;
}
extern "C" void Gta2Net_SetShowWaypoints(bool on)
{
    if (!gConfig)
        return;
    gConfig->showWaypoints = on;
    SaveNetConfig(*gConfig);
}
#endif
