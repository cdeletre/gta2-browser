// gta2-mp's lobby screen, for the controller, before the game starts: the browser's co-op
// lobby (web/shell.js wireCoopLobby) in the Options screen's style (overlay font, the same
// inputs: up/down choose a line, left/right change it, A acts, B goes back).
//
//   1. this player's name and the room (picked from conf/net.ini's lists: no on-screen
//      keyboard yet), then Create room (host) or Join room;
//   2. in the room: the players with their round trip; the host sets the mode, the
//      district and the limits; in Gang Wars everyone picks a crew and the crew a gang;
//      the host starts the match;
//   3. the match starts once every player is loaded (EnterMatch), and the game runs.
// A browser joins the same room by typing its name in its lobby (rooms are upper case
// there: the default names are).
// GTA2_LOBBY_KEYS=up,down,left,right,ok,back,wait,x,y,start,select,...: presses for tests,
// one a second (to the on-screen keyboard while it is up).
// Multiplayer build with a window only (gta2-mp).
#if !defined(GTA2_NATIVE_HEADLESS)
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <ctype.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "net_session.h"

extern "C" bool Gta2Native_PumpEvents();                  // platform/window_sdl.cpp
extern "C" void Gta2Native_OverlayRect(float x, float y, float w, float h, uint32_t rgba);
extern "C" void Gta2Native_OverlayText(float x, float y, float px, uint32_t rgba, const char* text);
extern "C" void gta2_js_present(const void* cmdData, int cmdCount, const void* vertData, int vertCount, int refW);
extern "C" bool Gta2Native_OskOpen();
extern "C" void Gta2Native_OskInput(int action);
extern "C" void Gta2Native_OskStart(const char* title, const char* initial, int maxLen, void (*done)(bool ok, const char* text));

namespace gta2net
{
namespace
{
enum Phase
{
    kSetup,
    kRoom,
    kStarting
};

struct Item
{
    std::string label;
    std::function<std::string()> value; // shown on the right ("" for none)
    std::function<void(int)> change;    // left/right (-1, +1); null: not changeable
    std::function<void()> act;          // A; null: A changes it forward
};

struct Screen
{
    NetConfig* config = nullptr;
    bool active = false;
    Phase phase = kSetup;
    int line = 0;
    std::string message;    // the last error or notice
    bool messageError = false;
    bool quit = false;
    Json match;             // set by the lobby's onStart
    double lastPing = 0;
    std::vector<Item> items;
    std::string refused;    // set while the transport polls; acted on after it (Leave frees it)
    double joinedAt = 0;    // when this player entered the room
    bool otherHost = false; // host: someone else hosts this room too
};
Screen gS;

const char* const kModes[] = {"roam", "wars", "ffa"};
const char* const kModeNames[] = {"Free roam", "Gang Wars", "Free-for-all"};
const char* const kDistricts[] = {"downtown", "residential", "industrial"};
const char* const kDistrictNames[] = {"Downtown", "Residential", "Industrial"};
const int kMinutes[] = {0, 5, 10, 15, 20, 30, 45, 60};
const int kKills[] = {5, 10, 15, 20, 30, 50};

template <typename T, size_t N>
int IndexOf(const T (&list)[N], const T& v)
{
    for (size_t i = 0; i < N; ++i)
        if (list[i] == v)
            return static_cast<int>(i);
    return 0;
}

int IndexOfStr(const char* const* list, int n, const std::string& v)
{
    for (int i = 0; i < n; ++i)
        if (v == list[i])
            return i;
    return 0;
}

int Wrap(int i, int n)
{
    return ((i % n) + n) % n;
}

void Notice(const std::string& text, bool error)
{
    gS.message = text;
    gS.messageError = error;
    fprintf(stderr, "gta2: lobby: %s\n", text.c_str());
}

const Json* Me()
{
    if (!gLobby)
        return nullptr;
    for (const Json& p : gLobby->Players().Items())
        if (p["id"].Str() == gLobby->MyId())
            return &p;
    return nullptr;
}

void SetSetting(const char* key, Json value)
{
    Json c = Json::Object();
    c.Set(key, std::move(value));
    gLobby->SetSettings(c);
}

void Leave(const std::string& why, bool error)
{
    LeaveRoom();
    gS.phase = kSetup;
    gS.line = 0;
    if (!why.empty())
        Notice(why, error);
}

void Enter(bool host)
{
    std::string error;
    Notice(host ? "Creating room " + gS.config->room + "..." : "Joining room " + gS.config->room + "...", false);
    if (!JoinRoom(*gS.config, gS.config->name, gS.config->room, host, &error))
    {
        Notice(error, true);
        return;
    }
    SaveNetConfig(*gS.config);
    gLobby->onStart = [](const Json& m) { gS.match = m; };
    gLobby->onOtherHost = [](const std::string&) { gS.otherHost = true; };
    gS.otherHost = false;
    gS.joinedAt = NowMs();
    gLobby->onRefused = [](const std::string& why) {
        gS.refused = why == "full"                     ? "That room is full."
            : why.compare(0, 8, "version:") == 0 ? "That room runs another version of the game (co-op " + why.substr(8) +
                "; this one " + std::to_string(kCoopVersion) + ")."
                                                       : "Could not enter the room (id taken).";
    };
    gS.phase = kRoom;
    gS.line = 0;
    gS.message = host ? "Room created: the others join " + gS.config->room + "." : "";
    gS.messageError = false;
}

// A value typed on the on-screen keyboard joins the front of its list (left/right then turn
// from it, and it is offered next time; conf/net.ini keeps the list).
void RememberIn(std::vector<std::string>& list, const std::string& value)
{
    if (std::find(list.begin(), list.end(), value) == list.end())
        list.insert(list.begin(), value);
}

// The lines of the current phase.
void BuildItems()
{
    std::vector<Item>& it = gS.items;
    it.clear();
    NetConfig& c = *gS.config;
    if (gS.phase == kSetup)
    {
        // left/right: the lists of conf/net.ini; A: the on-screen keyboard (any name; a room
        // created by a browser has a code of its own, upper case as the browser makes it)
        it.push_back({"Name", [&c] { return c.name; },
                      [&c](int d) {
                          auto pos = std::find(c.names.begin(), c.names.end(), c.name);
                          const int i = pos == c.names.end() ? 0 : static_cast<int>(pos - c.names.begin());
                          c.name = c.names[Wrap(i + d, static_cast<int>(c.names.size()))];
                      },
                      [&c] {
                          Gta2Native_OskStart("Your name", c.name.c_str(), 16, [](bool ok, const char* text) {
                              if (ok && text[0])
                              {
                                  RememberIn(gS.config->names, text);
                                  gS.config->name = text;
                                  SaveNetConfig(*gS.config);
                              }
                          });
                      }});
        it.push_back({"Room", [&c] { return c.room; },
                      [&c](int d) {
                          auto pos = std::find(c.rooms.begin(), c.rooms.end(), c.room);
                          const int i = pos == c.rooms.end() ? 0 : static_cast<int>(pos - c.rooms.begin());
                          c.room = c.rooms[Wrap(i + d, static_cast<int>(c.rooms.size()))];
                      },
                      [&c] {
                          Gta2Native_OskStart("Room (as the host shows it)", c.room.c_str(), 32, [](bool ok, const char* text) {
                              std::string room = text;
                              for (char& ch : room)
                                  ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
                              while (!room.empty() && room.back() == ' ')
                                  room.pop_back();
                              if (ok && !room.empty())
                              {
                                  RememberIn(gS.config->rooms, room);
                                  gS.config->room = room;
                                  SaveNetConfig(*gS.config);
                              }
                          });
                      }});
        it.push_back({"Create room", nullptr, nullptr, [] { Enter(true); }});
        it.push_back({"Join room", nullptr, nullptr, [] { Enter(false); }});
        it.push_back({"Quit", nullptr, nullptr, [] { gS.quit = true; }});
        return;
    }
    if (gS.phase != kRoom || !gLobby)
        return;
    const Json& s = gLobby->Settings();
    const bool host = gLobby->IsHost();
    const std::string mode = s["mode"].Str();
    const int modeIdx = IndexOfStr(kModes, 3, mode);
    const int districtIdx = IndexOfStr(kDistricts, 3, s["district"].Str());
    auto hostOnly = [host](std::function<void(int)> f) { return host ? f : std::function<void(int)>(); };
    it.push_back({"Mode", [modeIdx] { return std::string(kModeNames[modeIdx]); },
                  hostOnly([modeIdx](int d) { SetSetting("mode", kModes[Wrap(modeIdx + d, 3)]); }), nullptr});
    it.push_back({"District", [districtIdx] { return std::string(kDistrictNames[districtIdx]); },
                  hostOnly([districtIdx](int d) { SetSetting("district", kDistricts[Wrap(districtIdx + d, 3)]); }),
                  nullptr});
    if (mode == "roam")
    {
        const bool ff = s["friendlyFire"].Truthy();
        it.push_back({"Friendly fire", [ff] { return std::string(ff ? "On" : "Off"); },
                      hostOnly([ff](int) { SetSetting("friendlyFire", !ff); }), nullptr});
    }
    else
    {
        const int m = IndexOf(kMinutes, s["minutes"].Int(0));
        it.push_back({"Time limit", [m] { return kMinutes[m] ? std::to_string(kMinutes[m]) + " min" : std::string("None"); },
                      hostOnly([m](int d) { SetSetting("minutes", kMinutes[Wrap(m + d, 8)]); }), nullptr});
    }
    if (mode == "ffa")
    {
        const int k = IndexOf(kKills, s["killLimit"].Int(10));
        it.push_back({"Kill limit", [k] { return std::to_string(kKills[k]); },
                      hostOnly([k](int d) { SetSetting("killLimit", kKills[Wrap(k + d, 6)]); }), nullptr});
    }
    if (mode == "wars")
    {
        const Json* me = Me();
        const std::string crew = me ? (*me)["crew"].Str() : "";
        std::vector<std::string> crews = {""};
        crews.insert(crews.end(), c.crews.begin(), c.crews.end());
        auto pos = std::find(crews.begin(), crews.end(), crew);
        if (pos == crews.end())
        {
            crews.push_back(crew); // a crew from a browser's list
            pos = crews.end() - 1;
        }
        const int ci = static_cast<int>(pos - crews.begin());
        it.push_back({"Crew", [crew] { return crew.empty() ? std::string("None") : crew; },
                      [crews, ci](int d) { gLobby->SetCrew(crews[Wrap(ci + d, static_cast<int>(crews.size()))]); },
                      nullptr});
        if (!crew.empty())
        {
            const Gang* gangs = GangsOf(s["district"].Str());
            std::string side;
            for (const Json& cr : gLobby->Crews().Items())
                if (cr["name"].Str() == crew)
                    side = cr["side"].Str();
            int gi = 0;
            for (int g = 0; g < 3; ++g)
                gi = side == gangs[g].code ? g + 1 : gi;
            it.push_back({"Crew's gang", [gangs, gi] { return gi ? std::string(gangs[gi - 1].name) : std::string("None"); },
                          [gangs, gi](int d) {
                              const int n = Wrap(gi + d, 4);
                              gLobby->PickSide(n ? gangs[n - 1].code : "");
                          },
                          nullptr});
        }
    }
    if (host)
    {
        it.push_back({"Start the match", nullptr, nullptr, [] {
                          const std::string problem = gLobby->StartProblem();
                          if (problem.empty())
                              gLobby->Start();
                          else
                              Notice(ProblemText(problem), true);
                      }});
    }
    it.push_back({"Leave the room", nullptr, nullptr, [] { Leave("", false); }});
}

void Input(int action)
{
    if (gS.phase == kStarting)
        return;
    BuildItems();
    const int n = static_cast<int>(gS.items.size());
    if (n == 0)
        return;
    gS.line = std::min(gS.line, n - 1);
    Item& item = gS.items[static_cast<size_t>(gS.line)];
    switch (action)
    {
        case 0: gS.line = Wrap(gS.line - 1, n); break;
        case 1: gS.line = Wrap(gS.line + 1, n); break;
        case 2:
        case 3:
            if (item.change)
                item.change(action == 2 ? -1 : 1);
            break;
        case 4:
            if (item.act)
                item.act();
            else if (item.change)
                item.change(1);
            break;
        case 5:
            if (gS.phase == kRoom)
                Leave("", false);
            break;
        default: break;
    }
}

void Draw(int dw, int dh)
{
    BuildItems();
    constexpr int kCols = 44;
    constexpr int kRows = 17;
    const float px = static_cast<float>(std::max(1, std::min(dh / (kRows * 12 + 8), dw / (kCols * 6 + 8))));
    const float cell = 6 * px;
    const float lineH = 12 * px;
    const uint32_t kWhite = 0xffffffff, kGrey = 0xffa0a0a0, kYellow = 0xff30d0ff, kBar = 0xff303030, kRed = 0xff5060ff,
                   kGreen = 0xff70e070;
    Gta2Native_OverlayRect(0, 0, static_cast<float>(dw), static_cast<float>(dh), 0xff000000);
    const float width = kCols * cell;
    const float x0 = (dw - width) / 2;
    float y = (dh - kRows * lineH) / 2;
    char buf[128];

    if (gS.phase == kSetup)
    {
        Gta2Native_OverlayText(x0, y, px, kWhite, "GTA2 multiplayer");
        y += lineH;
        snprintf(buf, sizeof(buf), "Server: %s", gS.config->server.url.c_str());
        Gta2Native_OverlayText(x0, y, px, kGrey, buf);
        y += 2 * lineH;
    }
    else if (gLobby)
    {
        snprintf(buf, sizeof(buf), "Room %s%s", gS.config->room.c_str(), gLobby->IsHost() ? " (you host)" : "");
        Gta2Native_OverlayText(x0, y, px, kWhite, buf);
        y += lineH;
        // the players, two per line: name, round trip, crew
        const Json& players = gLobby->Players();
        const int rows = 3;
        for (int r = 0; r < rows; ++r)
        {
            for (int col = 0; col < 2; ++col)
            {
                const size_t i = static_cast<size_t>(r * 2 + col);
                if (i >= players.size())
                    continue;
                const Json& p = players[i];
                std::string t = p["name"].Str();
                if (p["ping"].IsNumber())
                    t += " " + std::to_string(p["ping"].Int()) + "ms";
                if (!p["crew"].Str().empty())
                    t += " [" + p["crew"].Str() + "]";
                const bool me = p["id"].Str() == gLobby->MyId();
                Gta2Native_OverlayText(x0 + col * (kCols / 2) * cell, y + r * lineH, px, me ? kGreen : kWhite,
                                       t.substr(0, kCols / 2 - 1).c_str());
            }
        }
        if (players.size() == 0)
            Gta2Native_OverlayText(x0, y, px, kGrey, "Looking for the room's host...");
        y += (rows + 1) * lineH;
    }

    if (gS.phase == kStarting)
    {
        Gta2Native_OverlayText(x0, y, px, kYellow, "Starting: waiting for every player...");
    }
    else
    {
        const int n = static_cast<int>(gS.items.size());
        gS.line = std::min(gS.line, std::max(0, n - 1));
        for (int i = 0; i < n; ++i, y += lineH)
        {
            const Item& item = gS.items[static_cast<size_t>(i)];
            const bool sel = i == gS.line;
            if (sel)
                Gta2Native_OverlayRect(x0 - cell, y - 3 * px, width + 2 * cell, lineH, kBar);
            const uint32_t colour = sel ? kYellow : item.change || item.act ? kWhite : kGrey;
            Gta2Native_OverlayText(x0, y, px, colour, item.label.c_str());
            const std::string value = item.value ? item.value() : "";
            if (!value.empty())
            {
                snprintf(buf, sizeof(buf), sel && item.change ? "< %s >" : "%s", value.c_str());
                Gta2Native_OverlayText(x0 + width - static_cast<float>(strlen(buf)) * cell, y, px, colour, buf);
            }
        }
    }
    const float bottom = (dh + kRows * lineH) / 2 - 2 * lineH;
    // hints for the two ways a room goes wrong
    std::string hint;
    if (gS.phase == kRoom && gLobby && !gLobby->IsHost() && gLobby->HostId().empty() && NowMs() - gS.joinedAt > 5000)
        hint = "Nobody hosts " + gS.config->room + " yet: one player must pick Create room.";
    if (gS.phase == kRoom && gLobby && gLobby->IsHost() && (gS.otherHost || gLobby->SilentPeers(3000) > 0))
        hint = gS.config->room + " already has a host: leave, then pick Join room.";
    if (!hint.empty())
    {
        // two lines when it does not fit
        const size_t cut = hint.size() > kCols ? hint.rfind(' ', kCols) : std::string::npos;
        Gta2Native_OverlayText(x0, bottom - 2 * lineH, px, kYellow, hint.substr(0, cut).c_str());
        if (cut != std::string::npos)
            Gta2Native_OverlayText(x0, bottom - lineH, px, kYellow, hint.substr(cut + 1).c_str());
    }
    if (gS.phase == kRoom && gLobby && gLobby->IsHost() && gS.message.empty())
    {
        const std::string problem = gLobby->StartProblem();
        if (!problem.empty())
            Gta2Native_OverlayText(x0, bottom, px, kGrey, ProblemText(problem).substr(0, kCols).c_str());
    }
    if (!gS.message.empty())
        Gta2Native_OverlayText(x0, bottom, px, gS.messageError ? kRed : kGrey, gS.message.substr(0, kCols).c_str());
    Gta2Native_OverlayText(x0, bottom + lineH, px, kGrey,
                           gS.phase == kRoom ? "A: choose  Left/right: change  B: leave" : "A: choose/type  Left/right: change");
}

void Sleep5ms()
{
    timespec ts = {0, 5 * 1000 * 1000};
    nanosleep(&ts, nullptr);
}

// GTA2_LOBBY_KEYS: the next scripted press, one a second.
void ScriptedKeys()
{
    static std::vector<int> keys;
    static bool loaded = false;
    static size_t next = 0;
    static double at = 0;
    if (!loaded)
    {
        loaded = true;
        if (const char* list = getenv("GTA2_LOBBY_KEYS"))
        {
            // ok/back: A/B; x, y, start, select: the on-screen keyboard's other buttons
            static const char* const kNames[] = {"up", "down", "left", "right", "ok", "back", "wait", "x", "y", "start", "select"};
            std::string all = list;
            size_t pos = 0;
            while (pos <= all.size())
            {
                size_t end = all.find(',', pos);
                end = end == std::string::npos ? all.size() : end;
                const std::string name = all.substr(pos, end - pos);
                for (int k = 0; k < 11; ++k)
                    if (name == kNames[k])
                        keys.push_back(k);
                pos = end + 1;
            }
        }
        at = NowMs() + 1000;
    }
    if (next < keys.size() && NowMs() >= at)
    {
        at = NowMs() + 1000;
        static const int kOskAction[11] = {0, 1, 2, 3, 4, 5, -1, 6, 7, 8, 9};
        if (Gta2Native_OskOpen() && kOskAction[keys[next]] >= 0)
            Gta2Native_OskInput(kOskAction[keys[next]]);
        else if (keys[next] < 6)
            Input(keys[next]);
        ++next;
    }
}

// One frame of the screen: events, the transport, the host's pings, drawing.
bool Frame()
{
    ScriptedKeys();
    if (!Gta2Native_PumpEvents())
    {
        gS.quit = true;
        return false;
    }
    if (gTransport)
    {
        gTransport->Poll();
        if (!gS.refused.empty())
        {
            const std::string why = gS.refused;
            gS.refused.clear();
            Leave(why, true);
        }
        else if (!gTransport->Connected() && gS.phase != kSetup)
        {
            Leave("Lost the connection to the server.", true);
        }
    }
    if (gLobby && gLobby->IsHost() && gS.phase == kRoom && NowMs() - gS.lastPing > 2000)
    {
        gS.lastPing = NowMs();
        gLobby->MeasurePings(); // every 2 s, as the browser's lobby
    }
    gta2_js_present(nullptr, 0, nullptr, 0, 640); // draws the overlay (this screen) only
    Sleep5ms();
    return !gS.quit;
}
} // namespace

// Returns 0 with the match entered, 1 if the player quit.
int RunLobbyScreen(NetConfig& config)
{
    gS.config = &config;
    gS.active = true;
    fprintf(stderr, "gta2: lobby: server %s\n", config.server.url.c_str());
    while (Frame())
    {
        if (!gS.match.IsNull())
        {
            gS.phase = kStarting;
            const Json match = gS.match;
            gS.match = Json();
            if (EnterMatch(match, [] { return Frame(); }))
            {
                gS.active = false;
                return 0;
            }
            if (gS.quit)
                break;
            Leave("The match could not start.", true);
        }
    }
    gS.active = false;
    LeaveRoom();
    return 1;
}

} // namespace gta2net

// The Options screen's hooks (platform/options_native.cpp): this screen has the input and
// the drawing while it is up.
extern "C" bool Gta2Net_LobbyActive()
{
    return gta2net::gS.active;
}

extern "C" void Gta2Net_LobbyInput(int action)
{
    gta2net::Input(action);
}

extern "C" void Gta2Net_LobbyDraw(int dw, int dh)
{
    gta2net::Draw(dw, dh);
}
#endif
