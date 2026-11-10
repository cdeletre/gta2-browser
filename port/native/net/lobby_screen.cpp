// gta2-online's lobby screen, for the controller, before the game starts: the browser's co-op
// lobby (web/shell.js wireCoopLobby) in the Options screen's style (overlay font, the same
// inputs: up/down choose a line, left/right change it, A acts, B goes back).
//
//   1. the game server (conf/online.ini's [server] sections: A connects, or disconnects; a
//      password the server wants and the file has not is asked in a window), this player's
//      name and the room, then Create room (host) or Join room;
//   2. in the room: the players with their round trip; the host sets the mode, the
//      district and the limits; in Gang Wars everyone picks a crew and the crew a gang;
//      the host starts the match;
//   3. the match starts once every player is loaded (EnterMatch), and the game runs.
// A browser joins the same room by typing its name in its lobby (rooms are upper case
// there: the default names are).
// GTA2_LOBBY_KEYS=up,down,left,right,ok,back,wait,x,y,start,select,...: presses for tests,
// one a second (to the on-screen keyboard while it is up).
// gta2-lan (GTA2_NET_LAN): instead of the servers, the games heard on the local network (A
// joins one), Host a game, the room's PIN (A edits it as a padlock: left/right the digit,
// up/down its value, A done) and Join by address; the room itself is the same screen.
// Multiplayer builds with a window only (gta2-online, gta2-lan).
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
extern "C" void Gta2Native_OverlayTextExact(float x, float y, float px, uint32_t rgba, const char* text); // names as typed
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
    std::function<void()> remove;       // X; null: nothing to delete
    uint32_t colour = 0;                // 0: the screen's; else the line's own (a crew's gang)
    bool info = false;                  // a line to read, not to choose
    std::string help;                   // the help line while it is chosen ("" for the default)
    bool off = false;                   // shown grey: not possible yet (A says why)
    bool exact = false;                 // the label as typed (a server's name), not in capitals
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
    bool nameDefault = false; // the Name line shows its "DEFAULT" entry
    bool roomDefault = false; // the Room line shows its "DEFAULT" entry
    int newCrew = 0;          // New crew: the name offered (an index; past the names: type one)
    bool crewSent = false;    // the crew joined last went to the host
    int scroll = 0;           // the first line shown
    int askPassword = -1;     // the server whose password the window asks for (next frame)
    int passwordFor = -1;     // the server the window is asking for
    int pinDigit = -1;        // gta2-lan: the PIN digit being changed (-1: not editing)
    std::string pinBefore;    // the PIN before editing (B puts it back)
    std::string chosen;       // the chosen line's label: kept on it when lines come and go above
};
Screen gS;

const char* const kModes[] = {"roam", "wars", "ffa"};
const char* const kModeNames[] = {"Free roam", "Gang Wars", "Free-for-all"};
const char* const kDistricts[] = {"downtown", "residential", "industrial"};
const char* const kDistrictNames[] = {"Downtown", "Residential", "Industrial"};
const int kMinutes[] = {0, 5, 10, 15, 20, 30, 45, 60};
const int kKills[] = {5, 10, 15, 20, 30, 50};
const int kPoints[] = {0, 10000, 25000, 50000, 100000, 250000, 500000, 1000000}; // Gang Wars' target

std::string MoneyText(int v)
{
    std::string d = std::to_string(v), out;
    for (size_t i = 0; i < d.size(); ++i)
    {
        if (i && (d.size() - i) % 3 == 0)
            out += ',';
        out += d[i];
    }
    return "$" + out;
}

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

#if defined(GTA2_NET_LAN)
bool JoinLan(bool host, const std::string& roomId, const std::vector<std::string>& seeds, const std::string& room,
             std::string* error)
{
    return JoinLanRoom(*gS.config, roomId, seeds, room, host, error);
}
#endif

// host: creates the room; else joins it. gta2-lan: lanGame is the room heard of (or null:
// joinAddress), or none when hosting.
void Enter(bool host, const LanGame* lanGame = nullptr, const std::string& joinAddress = "")
{
    gS.crewSent = false;
    gS.scroll = 0;
    gS.nameDefault = false;
    gS.roomDefault = false;
    std::string error;
#if defined(GTA2_NET_LAN)
    const std::string roomName = lanGame ? lanGame->room : gS.config->room;
    Notice(host ? "Creating room " + roomName + "..."
                : lanGame ? "Joining room " + roomName + "..." : "Joining the game at " + joinAddress + "...",
           false);
    std::vector<std::string> seeds;
    if (lanGame)
    {
        for (const LanGame::Member& m : lanGame->members)
            seeds.push_back(m.address);
    }
    else if (!joinAddress.empty())
    {
        seeds.push_back(joinAddress.find(':') == std::string::npos ? joinAddress + ":" + std::to_string(kLanPort) : joinAddress);
    }
    if (!JoinLan(host, lanGame ? lanGame->roomId : "", seeds, roomName, &error))
    {
        Notice(error, true);
        return;
    }
    gS.config->room = roomName; // the room its screen shows
#else
    Notice(host ? "Creating room " + gS.config->room + "..." : "Joining room " + gS.config->room + "...", false);
    if (!JoinRoom(*gS.config, gS.config->name, gS.config->room, host, &error))
    {
        Notice(error, true);
        return;
    }
#endif
    SaveNetConfig(*gS.config);
    gLobby->onStart = [](const Json& m) { gS.match = m; };
    gLobby->onOtherHost = [](const std::string&) { gS.otherHost = true; };
    gS.otherHost = false;
    gS.joinedAt = NowMs();
    gLobby->onRefused = [](const std::string& why) {
        gS.refused = why == "full"                     ? "That room is full."
            : why == "pin"                       ? "Wrong PIN for that game (set it on the PIN line)."
            : why == "room"                      ? "That game is gone."
            : why.compare(0, 8, "version:") == 0 ? "That room runs another version of the game (co-op " + why.substr(8) +
                "; this one " + std::to_string(kCoopVersion) + ")."
                                                       : "Could not enter the room (id taken).";
    };
    gS.phase = kRoom;
    gS.line = 0;
    gS.message = host ? "Room created: the others join " + gS.config->room + "." : "";
    gS.messageError = false;
}

#if !defined(GTA2_NET_LAN)
void Connect(int index, bool typed);

// The window for a server's password (the on-screen keyboard over the lobby).
void AskPassword(int index)
{
    gS.passwordFor = index;
    const std::string title = "Password for " + gS.config->servers[static_cast<size_t>(index)].name;
    Gta2Native_OskStart(title.c_str(), "", 64, [](bool ok, const char* text) {
        const int i = gS.passwordFor;
        gS.passwordFor = -1;
        if (!ok || !text[0] || i < 0)
        {
            Notice("Not connected.", false);
            return;
        }
        ServerEntry& e = gS.config->servers[static_cast<size_t>(i)];
        const std::string before = e.password;
        const std::string invite = e.invite;
        e.password = text;
        e.invite.clear();
        Connect(i, true);
        if (gS.config->connected != i)
        {
            e.password = before; // kept only once the server takes it
            e.invite = invite;
        }
    });
}

// A on a server: connects to it (after letting the previous one go), or disconnects from it.
void Connect(int index, bool typed)
{
    NetConfig& c = *gS.config;
    const std::string name = c.servers[static_cast<size_t>(index)].name;
    if (c.connected == index && !typed)
    {
        DisconnectServer(c);
        Notice("Disconnected from " + name + ".", false);
        return;
    }
    Notice("Connecting to " + name + "...", false);
    const ServerCheck r = ConnectServer(c, index);
    if (r.ok)
    {
        Notice("Connected to " + name + ": pick a room.", false);
        SaveNetConfig(c); // the server connected last (and a password typed for it)
        return;
    }
    Notice(r.error.empty() ? "Could not connect to " + name + "." : r.error, true);
    if (r.needsPassword)
        gS.askPassword = index; // the window opens on the next frame
}

#endif

// A value typed on the on-screen keyboard joins the front of its list (left/right then turn
// from it, and it is offered next time; conf/online.ini keeps the list).
void RememberIn(std::vector<std::string>& list, const std::string& value)
{
    if (std::find(list.begin(), list.end(), value) == list.end())
        list.insert(list.begin(), value);
}

// Left/right on a list line: its entries, then "DEFAULT" (onDefault), round again.
void Cycle(std::vector<std::string>& list, std::string& current, bool& onDefault, int d)
{
    const int n = static_cast<int>(list.size());
    auto pos = std::find(list.begin(), list.end(), current);
    const int i = onDefault ? n : pos == list.end() ? 0 : static_cast<int>(pos - list.begin());
    const int next = Wrap(i + d, n + 1);
    onDefault = next == n;
    if (!onDefault)
        current = list[static_cast<size_t>(next)];
}

// X on a list line: the entry shown leaves the list (one at least stays), the next one shows.
void Forget(std::vector<std::string>& list, std::string& current, bool onDefault)
{
    auto pos = std::find(list.begin(), list.end(), current);
    if (onDefault || pos == list.end() || list.size() <= 1)
        return;
    const size_t i = static_cast<size_t>(pos - list.begin());
    list.erase(pos);
    current = list[std::min(i, list.size() - 1)];
    SaveNetConfig(*gS.config);
}

// "#rrggbb" as the overlay's colour (ABGR).
uint32_t Colour(const std::string& hex)
{
    if (hex.size() != 7 || hex[0] != '#')
        return 0;
    const unsigned v = static_cast<unsigned>(strtoul(hex.c_str() + 1, nullptr, 16));
    return 0xff000000u | ((v & 0xff) << 16) | (v & 0xff00) | ((v >> 16) & 0xff);
}

// This player joins a crew ("" leaves it), remembered for the next room.
void JoinCrew(const std::string& name)
{
    gLobby->SetCrew(name);
    gS.config->crew = CleanCrew(name);
    SaveNetConfig(*gS.config);
}

// Gang Wars: the crews as the browser shows them (web/shell.js render): one line each, in its
// gang's colour, with its members; A joins it (or leaves it, for this player's own); left/right
// change its gang, for its members, or for any crew by the host; a gang another crew has is
// skipped. Then New crew (a name from conf/online.ini's crews not in use, or one typed) and who has
// no crew yet. A crew with no one left is gone (crews come from the players).
void CrewLines(std::vector<Item>& it, bool host)
{
    const Json& s = gLobby->Settings();
    const Json crews = gLobby->Crews();
    const Json* me = Me();
    const std::string mine = me ? (*me)["crew"].Str() : "";
    const Gang* gangs = GangsOf(s["district"].Str());
    it.push_back({"-- Crews --", nullptr, nullptr, nullptr, nullptr, 0, true});
    for (const Json& cr : crews.Items())
    {
        const std::string name = cr["name"].Str();
        const std::string side = cr["side"].IsString() ? cr["side"].Str() : "";
        const bool member = name == mine;
        std::string members;
        for (const Json& id : cr["members"].Items())
            for (const Json& p : gLobby->Players().Items())
                if (p["id"].Str() == id.Str())
                    members += (members.empty() ? "" : ", ") + p["name"].Str();
        // the gangs this crew may take: none, or one no other crew has
        std::vector<std::string> options = {""};
        for (int g = 0; g < 3; ++g)
        {
            bool held = false;
            for (const Json& o : crews.Items())
                held = held || (o["name"].Str() != name && o["side"].Str() == gangs[g].code && o["side"].IsString());
            if (!held)
                options.push_back(gangs[g].code);
        }
        const std::string gangName = cr["gang"].IsString() ? cr["gang"].Str() : std::string("no gang");
        Item line;
        line.label = (member ? "* " : "  ") + name + ": " + members;
        line.value = [gangName] { return gangName; };
        if (member || host)
        {
            line.change = [options, side, name, member](int d) {
                auto pos = std::find(options.begin(), options.end(), side);
                const int i = pos == options.end() ? 0 : static_cast<int>(pos - options.begin());
                const std::string next = options[static_cast<size_t>(Wrap(i + d, static_cast<int>(options.size())))];
                if (member)
                    gLobby->PickSide(next);
                else
                    gLobby->HostSetSide(name, next);
            };
        }
        line.act = [name, member] { JoinCrew(member ? "" : name); };
        line.colour = Colour(cr["colour"].Str());
        line.help = std::string(member ? "A: leave" : "A: join") + (member || host ? "  Left/right: gang" : "");
        it.push_back(line);
    }
    // New crew: the names of conf/online.ini not in use, then "Type a name..."
    std::vector<std::string> offered;
    for (const std::string& n : gS.config->crews)
    {
        bool used = false;
        for (const Json& cr : crews.Items())
            used = used || cr["name"].Str() == CleanCrew(n);
        if (!used)
            offered.push_back(n);
    }
    const int count = static_cast<int>(offered.size()) + 1;
    gS.newCrew = std::min(gS.newCrew, count - 1);
    const bool typing = gS.newCrew == count - 1;
    const std::string candidate = typing ? std::string("Type a name...") : offered[static_cast<size_t>(gS.newCrew)];
    Item add;
    add.label = "New crew";
    add.value = [candidate] { return candidate; };
    add.change = [count](int d) { gS.newCrew = Wrap(gS.newCrew + d, count); };
    add.act = [typing, candidate] {
        if (!typing)
        {
            JoinCrew(candidate);
            return;
        }
        Gta2Native_OskStart("New crew", "", 16, [](bool ok, const char* text) {
            const std::string name = CleanCrew(text);
            if (ok && !name.empty())
            {
                RememberIn(gS.config->crews, name);
                JoinCrew(name);
            }
        });
    };
    add.help = "A: create  Left/right: name";
    it.push_back(add);
    std::string loners;
    for (const Json& p : gLobby->Players().Items())
        if (p["crew"].Str().empty())
            loners += (loners.empty() ? "" : ", ") + p["name"].Str();
    if (!loners.empty())
        it.push_back({"No crew yet: " + loners, nullptr, nullptr, nullptr, nullptr, 0, true});
}

// The lines of the current phase.
void BuildItems()
{
    std::vector<Item>& it = gS.items;
    it.clear();
    NetConfig& c = *gS.config;
#if defined(GTA2_NET_LAN)
    if (gS.phase == kSetup)
    {
        // the games heard on the network: A joins one in its lobby
        it.push_back({"-- Games on this network --", nullptr, nullptr, nullptr, nullptr, 0, true});
        int found = 0;
        for (const auto& [roomId, g] : Discovery()->Games())
        {
            if (g.hostName.empty())
                continue; // its host is not heard (yet): nothing to join
            ++found;
            const LanGame game = g;
            const bool other = g.version != kCoopVersion;
            const bool playing = g.state == "playing";
            Item line;
            line.label = g.room + " (" + g.hostName + ")";
            const std::string mode = g.mode == "wars" ? "Gang Wars" : g.mode == "ffa" ? "Free-for-all" : "Free roam";
            const std::string value = other ? std::string("other version")
                : playing                   ? std::string("playing")
                                            : std::to_string(g.players) + (g.players == 1 ? " player, " : " players, ") + mode;
            line.value = [value] { return value; };
            line.act = [game, other, playing] {
                if (other)
                    Notice("That game runs another version of gta2-lan.", true);
                else if (playing)
                    Notice("That game has started: wait for its next match.", true);
                else
                    Enter(false, &game);
            };
            line.off = other || playing;
            line.exact = true;
            line.help = "A: join (with the PIN below)";
            it.push_back(line);
        }
        if (!found)
            it.push_back({"No game heard yet: host one below.", nullptr, nullptr, nullptr, nullptr, 0, true});
        Item hostLine;
        hostLine.label = "Host a game";
        hostLine.act = [] { Enter(true); };
        hostLine.help = "A: host room " + c.room + " with the PIN below";
        it.push_back(hostLine);
    }
#endif
    if (gS.phase == kSetup)
    {
#if !defined(GTA2_NET_LAN)
        // the servers of conf/online.ini: A connects (or disconnects)
        for (int i = 0; i < static_cast<int>(c.servers.size()); ++i)
        {
            Item line;
            line.label = c.servers[static_cast<size_t>(i)].name;
            const bool on = c.connected == i;
            line.value = [on] { return std::string(on ? "Connected" : ""); };
            line.act = [i] { Connect(i, false); };
            line.colour = on ? 0xff70e070u : 0;
            line.help = on ? "A: disconnect" : "A: connect";
            line.exact = true;
            it.push_back(line);
        }
#endif
        // left/right: the lists of conf/online.ini, then "DEFAULT"; A: the on-screen keyboard (any
        // name; a room created by a browser has a code of its own, upper case as the browser
        // makes it), or on "DEFAULT" the lists a new settings file has; X: the entry leaves the list
        it.push_back({"Name", [&c] { return gS.nameDefault ? std::string("DEFAULT") : c.name; },
                      [&c](int d) { Cycle(c.names, c.name, gS.nameDefault, d); },
                      [&c] {
                          if (gS.nameDefault)
                          {
                              c.names = DefaultNames();
                              c.name = c.names[0];
                              gS.nameDefault = false;
                              SaveNetConfig(c);
                              Notice("Names reset to the defaults.", false);
                              return;
                          }
                          Gta2Native_OskStart("Your name", c.name.c_str(), 16, [](bool ok, const char* text) {
                              if (ok && text[0])
                              {
                                  RememberIn(gS.config->names, text);
                                  gS.config->name = text;
                                  SaveNetConfig(*gS.config);
                              }
                          });
                      },
                      [&c] { Forget(c.names, c.name, gS.nameDefault); }});
        it.push_back({"Room", [&c] { return gS.roomDefault ? std::string("DEFAULT") : c.room; },
                      [&c](int d) { Cycle(c.rooms, c.room, gS.roomDefault, d); },
                      [&c] {
                          if (gS.roomDefault)
                          {
                              c.rooms = DefaultRooms();
                              c.room = c.rooms[0];
                              gS.roomDefault = false;
                              SaveNetConfig(c);
                              Notice("Rooms reset to the defaults.", false);
                              return;
                          }
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
                      },
                      [&c] { Forget(c.rooms, c.room, gS.roomDefault); }});
#if defined(GTA2_NET_LAN)
        // the PIN, as a padlock: A edits it (left/right the digit, up/down its value, A done)
        Item pin;
        pin.label = "PIN";
        pin.value = [&c] {
            if (gS.pinDigit < 0)
                return c.lanPin;
            std::string v;
            for (int d = 0; d < 4; ++d)
                v += d == gS.pinDigit ? "<" + c.lanPin.substr(static_cast<size_t>(d), 1) + ">" : " " + c.lanPin.substr(static_cast<size_t>(d), 1) + " ";
            return v;
        };
        pin.act = [&c] {
            gS.pinBefore = c.lanPin;
            gS.pinDigit = 0;
        };
        pin.help = "A: change it (the same for all players)";
        it.push_back(pin);
        Item byAddress;
        byAddress.label = "Join by address";
        byAddress.act = [] {
            Gta2Native_OskStart("A player's address (ip, or ip:port)", "", 21, [](bool ok, const char* text) {
                std::string a = text;
                while (!a.empty() && a.back() == ' ')
                    a.pop_back();
                if (ok && !a.empty())
                    Enter(false, nullptr, a);
            });
        };
        byAddress.help = "A: type a player's address";
        it.push_back(byAddress);
#else
        const bool connected = c.connected >= 0;
        for (const bool host : {true, false})
        {
            Item line;
            line.label = host ? "Create room" : "Join room";
            line.act = [host, connected] {
                if (connected)
                    Enter(host);
                else
                    Notice("Connect to a server first (A on its line).", true);
            };
            line.off = !connected;
            it.push_back(line);
        }
#endif
        it.push_back({"Quit", nullptr, nullptr, [] { gS.quit = true; }});
        // the chosen line stays chosen when the games heard come and go above it
        for (size_t i = 0; i < it.size() && !gS.chosen.empty(); ++i)
        {
            if (it[i].label == gS.chosen)
                gS.line = static_cast<int>(i);
        }
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
        // the points target (a browser host's own value shows as it is; left/right go from the nearest)
        const int points = s["pointsTarget"].Int(0);
        int pi = 0;
        for (int i = 0; i < 8; ++i)
            pi = kPoints[i] <= points ? i : pi;
        it.push_back({"Points target", [points] { return points > 0 ? MoneyText(points) : std::string("None"); },
                      hostOnly([pi](int d) { SetSetting("pointsTarget", kPoints[Wrap(pi + d, 8)]); }),
                      nullptr});
        CrewLines(it, host);
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
    if (gS.pinDigit >= 0)
    {
        // the PIN's padlock: up/down the digit's value, left/right the digit, A done, B back
        std::string& pin = gS.config->lanPin;
        char& d = pin[static_cast<size_t>(gS.pinDigit)];
        if (action == 0)
            d = d == '9' ? '0' : static_cast<char>(d + 1);
        else if (action == 1)
            d = d == '0' ? '9' : static_cast<char>(d - 1);
        else if (action == 2)
            gS.pinDigit = std::max(0, gS.pinDigit - 1);
        else if (action == 3)
            gS.pinDigit = std::min(3, gS.pinDigit + 1);
        else if (action == 4)
        {
            gS.pinDigit = -1;
            SaveNetConfig(*gS.config);
            Notice("PIN " + pin + ".", false);
        }
        else if (action == 5)
        {
            pin = gS.pinBefore;
            gS.pinDigit = -1;
        }
        return;
    }
    BuildItems();
    const int n = static_cast<int>(gS.items.size());
    if (n == 0)
        return;
    gS.line = std::min(gS.line, n - 1);
    Item& item = gS.items[static_cast<size_t>(gS.line)];
    switch (action)
    {
        case 0:
        case 1:
            // the next line that can be chosen (lines to read are passed over)
            for (int k = 0; k < n; ++k)
            {
                gS.line = Wrap(gS.line + (action == 0 ? -1 : 1), n);
                if (!gS.items[static_cast<size_t>(gS.line)].info)
                    break;
            }
            gS.chosen = gS.phase == kSetup ? gS.items[static_cast<size_t>(gS.line)].label : "";
            break;
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
        case 6:
            if (item.remove)
                item.remove();
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
#if defined(GTA2_NET_LAN)
        Gta2Native_OverlayText(x0, y, px, kWhite, "GTA2 on the local network");
        y += lineH;
        Gta2Native_OverlayText(x0, y, px, kGrey, Discovery()->Ok() ? "No server: the consoles play together directly"
                                                                    : "Cannot listen for games (UDP 27900 in use?)");
        y += 2 * lineH;
#else
        Gta2Native_OverlayText(x0, y, px, kWhite, "GTA2 multiplayer");
        y += lineH;
        const NetConfig& c = *gS.config;
        if (c.connected >= 0)
            snprintf(buf, sizeof(buf), "Server: %s", c.server.url.c_str());
        else
            snprintf(buf, sizeof(buf), "%s", "Not connected: pick a server");
        Gta2Native_OverlayText(x0, y, px, kGrey, buf);
        y += 2 * lineH;
#endif
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
                // a crew is for Gang Wars only (the one joined last is kept, for when the host
                // turns to Gang Wars, but not shown in the other modes)
                if (!p["crew"].Str().empty() && gLobby->Settings()["mode"].Str() == "wars")
                    t += " [" + p["crew"].Str() + "]";
                const bool me = p["id"].Str() == gLobby->MyId();
                Gta2Native_OverlayTextExact(x0 + col * (kCols / 2) * cell, y + r * lineH, px, me ? kGreen : kWhite,
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
        while (gS.line < n - 1 && gS.items[static_cast<size_t>(gS.line)].info)
            ++gS.line; // never on a line to read
        // as many lines as fit above the hints; scrolled to keep the chosen one in view
        const float listBottom = (dh + kRows * lineH) / 2 - 4 * lineH;
        const int fits = std::max(3, static_cast<int>((listBottom - y) / lineH));
        if (gS.line < gS.scroll)
            gS.scroll = gS.line;
        if (gS.line >= gS.scroll + fits)
            gS.scroll = gS.line - fits + 1;
        gS.scroll = std::max(0, std::min(gS.scroll, std::max(0, n - fits)));
        if (gS.scroll > 0)
            Gta2Native_OverlayText(x0 + width - cell, y - lineH, px, kGrey, "^");
        for (int i = gS.scroll; i < n && i < gS.scroll + fits; ++i, y += lineH)
        {
            const Item& item = gS.items[static_cast<size_t>(i)];
            const bool sel = i == gS.line;
            if (sel)
                Gta2Native_OverlayRect(x0 - cell, y - 3 * px, width + 2 * cell, lineH, kBar);
            const uint32_t colour = item.info ? kGrey
                : sel                         ? kYellow
                : item.off                    ? kGrey
                : item.colour                 ? item.colour
                : item.change || item.act     ? kWhite
                                              : kGrey;
            // crew lines carry players' names: as typed
            (item.colour || item.info || item.exact ? Gta2Native_OverlayTextExact : Gta2Native_OverlayText)(
                x0, y, px, colour, item.label.substr(0, item.value ? 30 : kCols).c_str());
            const std::string value = item.value ? item.value() : "";
            if (!value.empty())
            {
                snprintf(buf, sizeof(buf), sel && item.change ? "< %s >" : "%s", value.c_str());
                // a name as typed (lower case too); the other values in the screen's capitals
                (item.label == "Name" || item.label == "New crew" ? Gta2Native_OverlayTextExact : Gta2Native_OverlayText)(
                    x0 + width - static_cast<float>(strlen(buf)) * cell, y, px, colour, buf);
            }
        }
        if (gS.scroll + fits < n)
            Gta2Native_OverlayText(x0 + width - cell, y, px, kGrey, "v");
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
    {
        // in the room, one line (the hints are above it); before, up to three, ending on it
        std::vector<std::string> lines;
        std::string rest = gS.message;
        const size_t most = gS.phase == kRoom ? 1 : 3;
        while (!rest.empty() && lines.size() < most)
        {
            size_t cut = rest.size() <= static_cast<size_t>(kCols) ? rest.size() : rest.rfind(' ', kCols);
            if (cut == std::string::npos || cut == 0)
                cut = kCols;
            lines.push_back(rest.substr(0, cut));
            rest = cut < rest.size() ? rest.substr(cut + (rest[cut] == ' ' ? 1 : 0)) : "";
        }
        for (size_t i = 0; i < lines.size(); ++i)
            Gta2Native_OverlayText(x0, bottom - static_cast<float>(lines.size() - 1 - i) * lineH, px,
                                   gS.messageError ? kRed : kGrey, lines[i].c_str());
    }
    const std::string& lineHelp =
        gS.line < static_cast<int>(gS.items.size()) ? gS.items[static_cast<size_t>(gS.line)].help : std::string();
    if (gS.pinDigit >= 0)
        Gta2Native_OverlayText(x0, bottom + lineH, px, kGrey, "Up/down: digit  Left/right: next  A: done");
    else if (!lineHelp.empty() && gS.phase == kRoom)
        Gta2Native_OverlayText(x0, bottom + lineH, px, kGrey, (lineHelp + "  B: leave").substr(0, kCols).c_str());
    else if (!lineHelp.empty())
        Gta2Native_OverlayText(x0, bottom + lineH, px, kGrey, lineHelp.substr(0, kCols).c_str());
    else
    Gta2Native_OverlayText(x0, bottom + lineH, px, kGrey,
                           gS.phase == kRoom ? "A: choose  Left/right: change  B: leave"
                                              : "A: choose/type  Left/right: change  X: delete");
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
        else if (keys[next] == 7)
            Input(6); // x: delete a list entry
        ++next;
    }
}

// One frame of the screen: events, the transport, the host's pings, drawing.
bool Frame()
{
#if !defined(GTA2_NET_LAN)
    if (gS.askPassword >= 0 && !Gta2Native_OskOpen())
    {
        const int i = gS.askPassword;
        gS.askPassword = -1;
        AskPassword(i);
    }
#endif
    ScriptedKeys();
#if defined(GTA2_NET_LAN)
    if (gS.phase == kSetup)
        Discovery()->Poll(); // the games heard (the transport polls it once in a room)
#endif
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
    if (gLobby && gS.phase == kRoom && !gS.crewSent && !gS.config->crew.empty() &&
        (gLobby->IsHost() || !gLobby->HostId().empty()))
    {
        gS.crewSent = true;
        gLobby->SetCrew(gS.config->crew); // the crew joined last, as the browser does
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
#if defined(GTA2_NET_LAN)
    gS.chosen = "Host a game"; // (the games heard come above it)
#else
    gS.line = config.selected; // on the server connected last
    // back from a match: connected again to its server (net_main.cpp's Gta2Net_AfterMatch)
    if (const char* again = getenv("GTA2_NET_RECONNECT"))
    {
        const std::string name = again;
        unsetenv("GTA2_NET_RECONNECT");
        for (int i = 0; i < static_cast<int>(config.servers.size()); ++i)
        {
            if (config.servers[static_cast<size_t>(i)].name == name)
                Connect(i, false);
        }
    }
#endif
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
