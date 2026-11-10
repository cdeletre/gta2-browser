// gta2-mp before and during the game: joins a room on the game server, runs the lobby
// until the host starts the match and everyone is loaded, then lets main.cpp start the game
// as a network game (-N), as web/shell.js does before gta2_start; while the match runs,
// polls the transport and the sync checks once per main-loop iteration.
//
// With a window, the lobby is a screen for the controller (lobby_screen.cpp). Headless
// builds, or GTA2_NET_HOST set, run it from the environment instead (tests):
//   GTA2_NET_HOST=1                  this player creates the room and starts the match...
//   GTA2_NET_PLAYERS=n               ...once n players are in it (default 2)
//   GTA2_NET_MODE=roam|wars|ffa, GTA2_NET_DISTRICT=downtown|residential|industrial,
//   GTA2_NET_FRIENDLY_FIRE=1, GTA2_NET_MINUTES=m, GTA2_NET_KILLS=n   the host's settings
//   GTA2_NET_CREW=name, GTA2_NET_SIDE=gang code   this player's Gang Wars crew and gang
//   GTA2_NET_WAIT=s                  how long to wait in the lobby (default 300)
// Both read conf/net.ini, which these override:
//   GTA2_NET_SERVER=ws://host:port   the game server
//   GTA2_NET_PASSWORD=..., GTA2_NET_INVITE=<code>   its password, or an invite code
//   GTA2_NET_ROOM=name, GTA2_NET_NAME=name
// Multiplayer build only (gta2-mp).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>

#include "coop_native.h"
#include "net_session.h"

namespace gta2net
{

std::unique_ptr<Transport> gTransport;
std::unique_ptr<Lobby> gLobby;
NetConfig* gConfig = nullptr;

namespace
{
bool gInMatch = false;
bool gLostServer = false;

std::string Env(const char* name, const char* fallback)
{
    const char* v = getenv(name);
    return v && *v ? v : fallback;
}

std::string Trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::vector<std::string> SplitList(const std::string& s)
{
    std::vector<std::string> out;
    std::stringstream in(s);
    std::string item;
    while (std::getline(in, item, ','))
    {
        item = Trim(item);
        if (!item.empty())
            out.push_back(item);
    }
    return out;
}

std::string JoinList(const std::vector<std::string>& v)
{
    std::string out;
    for (const std::string& s : v)
        out += (out.empty() ? "" : ", ") + s;
    return out;
}

const char* const kDefaultNames = "Ace, Rusty, Nina, Zed, Kasia, Moose, Lola, Duke";
const char* const kDefaultRooms = "ROOM1, ROOM2, ROOM3, ROOM4, ROOM5, ROOM6";
const char* const kDefaultCrews = "Reds, Blues, Greens";
const char* const kDefaultChat[4] = {"Follow me!", "Wait for me!", "Help!", "Nice one!"};
const char* const kChatKeys[4] = {"chat_up", "chat_down", "chat_left", "chat_right"};

void Sleep10ms()
{
    timespec ts = {0, 10 * 1000 * 1000};
    nanosleep(&ts, nullptr);
}

std::string RosterText(const Json& players)
{
    std::string out;
    for (const Json& p : players.Items())
    {
        out += (out.empty() ? "" : ", ") + p["name"].Str();
        if (p["ping"].IsNumber())
            out += " (" + std::to_string(p["ping"].Int()) + " ms)";
        if (!p["crew"].Str().empty())
            out += " [" + p["crew"].Str() + "]";
    }
    return out;
}
} // namespace

std::vector<std::string> DefaultNames()
{
    return SplitList(kDefaultNames);
}

std::vector<std::string> DefaultRooms()
{
    return SplitList(kDefaultRooms);
}

NetConfig LoadNetConfig(const std::string& confDir)
{
    NetConfig c;
    c.path = confDir + "/net.ini";
    std::string server = "ws://127.0.0.1:8790", password, invite, names = kDefaultNames, name, rooms = kDefaultRooms,
                room, crews = kDefaultCrews;
    std::ifstream in(c.path);
    const bool found = in.is_open(); // (the stream's state after reading to the end is no test)
    if (found)
    {
        std::string line;
        while (std::getline(in, line))
        {
            line = Trim(line);
            if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[')
                continue;
            const size_t eq = line.find('=');
            if (eq == std::string::npos)
                continue;
            const std::string key = Trim(line.substr(0, eq));
            const std::string value = Trim(line.substr(eq + 1));
            if (key == "server")
                server = value;
            else if (key == "password")
                password = value;
            else if (key == "invite")
                invite = value;
            else if (key == "names")
                names = value;
            else if (key == "name")
                name = value;
            else if (key == "rooms")
                rooms = value;
            else if (key == "room")
                room = value;
            else if (key == "crews")
                crews = value;
            else if (key == "show_chat")
                c.showChat = value != "0" && value != "off" && value != "false";
            else if (key == "show_waypoints")
                c.showWaypoints = value != "0" && value != "off" && value != "false";
            for (int i = 0; i < 4; ++i)
            {
                if (key == kChatKeys[i] && !value.empty())
                    c.chat[i] = value.substr(0, 60);
            }
        }
    }
    for (int i = 0; i < 4; ++i)
    {
        if (c.chat[i].empty())
            c.chat[i] = kDefaultChat[i];
    }
    c.fileUrl = server;
    c.filePassword = password;
    c.fileInvite = invite;
    c.server.url = Env("GTA2_NET_SERVER", server.c_str());
    c.server.password = Env("GTA2_NET_PASSWORD", password.c_str());
    c.server.invite = Env("GTA2_NET_INVITE", invite.c_str());
    c.names = SplitList(names);
    c.rooms = SplitList(rooms);
    c.crews = SplitList(crews);
    if (c.names.empty())
        c.names = SplitList(kDefaultNames);
    if (c.rooms.empty())
        c.rooms = SplitList(kDefaultRooms);
    c.name = Env("GTA2_NET_NAME", name.empty() ? c.names[0].c_str() : name.c_str());
    c.room = Env("GTA2_NET_ROOM", room.empty() ? c.rooms[0].c_str() : room.c_str());
    if (!found)
    {
        SaveNetConfig(c);
        fprintf(stderr, "gta2: net: wrote the default settings to %s\n", c.path.c_str());
    }
    return c;
}

void SaveNetConfig(const NetConfig& c)
{
    std::ofstream out(c.path);
    if (!out)
        return;
    out << "# GTA2 multiplayer (gta2-mp). The game server, and the names and rooms the lobby\n"
           "# offers (comma-separated; the browser's lobby can join the same rooms by name).\n"
           "[net]\n"
        << "server = " << c.fileUrl << "\n"
        << "# the server's password, or the code of an invite link (?invite=...)\n"
        << "password = " << c.filePassword << "\n"
        << "invite = " << c.fileInvite << "\n"
        << "names = " << JoinList(c.names) << "\n"
        << "name = " << c.name << "\n"
        << "rooms = " << JoinList(c.rooms) << "\n"
        << "room = " << c.room << "\n"
        << "crews = " << JoinList(c.crews) << "\n"
        << "# in a match: Select + up, down, left, right sends one of these\n"
        << "chat_up = " << c.chat[0] << "\n"
        << "chat_down = " << c.chat[1] << "\n"
        << "chat_left = " << c.chat[2] << "\n"
        << "chat_right = " << c.chat[3] << "\n"
        << "# show the other players' chat messages and waypoints (also in Options)\n"
        << "show_chat = " << (c.showChat ? 1 : 0) << "\n"
        << "show_waypoints = " << (c.showWaypoints ? 1 : 0) << "\n";
}

bool JoinRoom(const NetConfig& config, const std::string& name, const std::string& room, bool host, std::string* error)
{
    LeaveRoom();
    ServerOptions o = config.server;
    o.room = room;
    fprintf(stderr, "gta2: net: joining room \"%s\" on %s as \"%s\"%s\n", room.c_str(), o.url.c_str(), name.c_str(),
            host ? " (host)" : "");
    std::unique_ptr<WsTransport> ws = WsTransport::Connect(o, error);
    if (!ws)
    {
        fprintf(stderr, "gta2: net: %s\n", error->c_str());
        return false;
    }
    gTransport = std::move(ws);
    gLobby = std::make_unique<Lobby>(gTransport.get(), name, host);
    return true;
}

void LeaveRoom()
{
    gLobby.reset();
    if (gTransport)
    {
        gTransport->Leave();
        gTransport.reset();
    }
}

std::string ProblemText(const std::string& problem)
{
    if (problem == "waitingPlayer")
        return "Waiting for at least one more player.";
    if (problem == "needCrew")
        return "Everyone needs a crew for Gang Wars.";
    if (problem == "twoCrews")
        return "Gang Wars needs at least two crews.";
    if (problem == "threeCrews")
        return "Gang Wars has room for three crews.";
    if (problem.compare(0, 12, "waitingGang:") == 0)
        return "Waiting for " + problem.substr(12) + " to pick a gang.";
    return problem;
}

// Every player loaded: the match starts together. Returns false if it cannot.
bool EnterMatch(const Json& match, const std::function<bool()>& eachLoop)
{
    SetupCoop(gLobby.get(), match);
    bool go = false;
    gLobby->AllLoaded([&] { go = true; });
    const double loadedAt = NowMs();
    while (!go)
    {
        gTransport->Poll();
        if (!gTransport->Connected())
            return false;
        if (NowMs() - loadedAt > 60000)
        {
            fprintf(stderr, "gta2: net: not everyone was ready within 60 s\n");
            return false;
        }
        if (eachLoop && !eachLoop())
            return false;
        Sleep10ms();
    }
    fprintf(stderr, "gta2: net: everyone is ready, starting the match\n");
    gInMatch = true;
    return true;
}

// The lobby from the environment, for tests. Returns 0 with the match entered.
int AutoLobby(const NetConfig& config)
{
    const bool host = atoi(Env("GTA2_NET_HOST", "0").c_str()) != 0;
    const int wanted = atoi(Env("GTA2_NET_PLAYERS", "2").c_str());
    const double waitMs = atof(Env("GTA2_NET_WAIT", "300").c_str()) * 1000.0;
    std::string error;
    if (!JoinRoom(config, config.name, config.room, host, &error))
        return 2;

    Json match;
    bool refused = false;
    std::string lastRoster;
    gLobby->onChange = [&] {
        const std::string r = RosterText(gLobby->Players());
        if (r != lastRoster)
        {
            lastRoster = r;
            fprintf(stderr, "gta2: net: players: %s\n", r.c_str());
        }
    };
    gLobby->onStart = [&](const Json& m) { match = m; };
    gLobby->onRefused = [&](const std::string& why) {
        fprintf(stderr, "gta2: net: refused: %s\n",
                why == "full"                          ? "the room is full"
                    : why.compare(0, 8, "version:") == 0 ? ("the room runs another co-op version (" + why.substr(8) +
                                                            ", this one " + std::to_string(kCoopVersion) + ")")
                                                               .c_str()
                                                         : "id taken");
        refused = true;
    };
    if (host)
    {
        Json s = Json::Object();
        s.Set("mode", Env("GTA2_NET_MODE", "roam"));
        s.Set("district", Env("GTA2_NET_DISTRICT", "downtown"));
        s.Set("friendlyFire", atoi(Env("GTA2_NET_FRIENDLY_FIRE", "0").c_str()) != 0);
        s.Set("minutes", atoi(Env("GTA2_NET_MINUTES", "0").c_str()));
        s.Set("killLimit", atoi(Env("GTA2_NET_KILLS", "10").c_str()));
        gLobby->SetSettings(s);
    }
    const std::string crew = Env("GTA2_NET_CREW", "");
    const std::string side = Env("GTA2_NET_SIDE", "");
    bool crewSent = false;
    bool sideSent = false;
    double pingsAt = -1;
    double startAt = -1;
    double lastProblemAt = -1;
    std::string lastProblem;
    const double begin = NowMs();
    while (match.IsNull())
    {
        gTransport->Poll();
        const double now = NowMs();
        if (refused || !gTransport->Connected())
            return 2;
        if (now - begin > waitMs)
        {
            fprintf(stderr, "gta2: net: no match after %.0f s in the lobby\n", waitMs / 1000);
            return 3;
        }
        // this player's crew once the host knows us (the host itself at once)
        if (!crew.empty() && !crewSent && (host || !gLobby->HostId().empty()))
        {
            gLobby->SetCrew(crew);
            crewSent = true;
        }
        if (!side.empty() && crewSent && !sideSent)
        {
            for (const Json& p : gLobby->Players().Items())
            {
                if (p["id"].Str() == gLobby->MyId() && !p["crew"].Str().empty())
                {
                    gLobby->PickSide(side);
                    sideSent = true;
                }
            }
        }
        if (host && static_cast<int>(gLobby->Players().size()) >= wanted)
        {
            if (pingsAt < 0)
            {
                gLobby->MeasurePings();
                pingsAt = now;
            }
            bool allPinged = true;
            for (const Json& p : gLobby->Players().Items())
                allPinged = allPinged && (p["id"].Str() == gLobby->MyId() || p["ping"].IsNumber());
            if (startAt < 0 && (allPinged || now - pingsAt > 6000))
                startAt = now + 500; // a moment for the last roster to reach everyone
            if (startAt >= 0 && now >= startAt)
            {
                const std::string problem = gLobby->StartProblem();
                if (problem.empty())
                {
                    gLobby->Start();
                }
                else if (problem != lastProblem || now - lastProblemAt > 5000)
                {
                    fprintf(stderr, "gta2: net: cannot start yet: %s\n", ProblemText(problem).c_str());
                    lastProblem = problem;
                    lastProblemAt = now;
                }
            }
        }
        Sleep10ms();
    }
    return EnterMatch(match, nullptr) ? 0 : 2;
}

#if !defined(GTA2_NATIVE_HEADLESS)
int RunLobbyScreen(NetConfig& config); // lobby_screen.cpp
#endif

} // namespace gta2net

using namespace gta2net;

// Returns 0 once the match can start (cmd then has -N), or the exit code (1: the player quit).
extern "C" int Gta2Net_PreGame(char* cmd, int size, const char* confDir)
{
    static NetConfig config;
    config = LoadNetConfig(confDir);
    gConfig = &config;
    int r;
#if defined(GTA2_NATIVE_HEADLESS)
    r = AutoLobby(config);
#else
    r = getenv("GTA2_NET_HOST") ? AutoLobby(config) : RunLobbyScreen(config);
#endif
    if (r != 0)
        return r;
    const size_t len = strlen(cmd);
    snprintf(cmd + len, static_cast<size_t>(size) - len, "%s-N", len ? " " : "");
    return 0;
}

// True while a match runs (input_sdl.cpp: Select's multiplayer gestures).
extern "C" bool Gta2Net_InMatch()
{
    return gInMatch && gLobby != nullptr;
}

// Once per main-loop iteration.
extern "C" void Gta2Net_Frame(double nowMs)
{
    if (!gTransport || !gInMatch)
        return;
    gTransport->Poll();
    if (!gTransport->Connected() && !gLostServer)
    {
        gLostServer = true;
        fprintf(stderr, "gta2: net: lost the server: the other players are gone\n");
    }
    WatchCoop(nowMs);
}

// For the status line: the last checksum shared, whether a drift was seen, the messages.
extern "C" void Gta2Net_Status(char* out, int size)
{
    if (!gLobby || !gInMatch)
    {
        snprintf(out, static_cast<size_t>(size), "%s", "");
        return;
    }
    int tick = -1;
    bool drifted = false;
    CoopStatus(&tick, &drifted);
    snprintf(out, static_cast<size_t>(size), ", co-op sync %d%s, %d sent, %d received", tick,
             drifted ? " DRIFTED" : " ok", gLobby->stats.sent, gLobby->stats.received);
}

// After the match: back to the lobby, in a fresh process (the engine starts clean, as the
// browser reloads its page). Only when the lobby screen started it.
extern "C" void Gta2Net_AfterMatch(char** argv)
{
    if (getenv("GTA2_NET_HOST"))
        return;
#if !defined(GTA2_NATIVE_HEADLESS)
    LeaveRoom();
    fprintf(stderr, "gta2: net: match over, back to the lobby\n");
    fflush(stderr);
    execv("/proc/self/exe", argv);
    fprintf(stderr, "gta2: net: could not restart for the lobby\n");
#else
    (void)argv;
#endif
}
