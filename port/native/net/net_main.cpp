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
//   GTA2_NET_FRIENDLY_FIRE=1, GTA2_NET_MINUTES=m, GTA2_NET_KILLS=n, GTA2_NET_POINTS=n
//                                    the host's settings (points: Gang Wars' target)
//   GTA2_NET_CREW=name, GTA2_NET_SIDE=gang code   this player's Gang Wars crew and gang
//   GTA2_NET_WAIT=s                  how long to wait in the lobby (default 300)
// Both read conf/net.ini, which these override:
//   GTA2_NET_SERVER=ws://host:port   the game server (wss://host[:port] in TLS)
//   GTA2_NET_CA_FILE=pem, GTA2_NET_TLS_FINGERPRINT=sha256:...   wss://: as ca_file, tls_fingerprint
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

#if !defined(GTA2_NET_LAN)
ServerCheck ConnectServer(NetConfig& config, int index)
{
    DisconnectServer(config);
    ServerOptions o = OptionsOf(config, index);
    fprintf(stderr, "gta2: net: connecting to %s (%s)\n", config.servers[static_cast<size_t>(index)].name.c_str(),
            o.url.c_str());
    ServerCheck r = WsTransport::Check(o);
    if (!r.ok)
    {
        fprintf(stderr, "gta2: net: %s\n", r.error.c_str());
        return r;
    }
    o.cookie = r.cookie;
    config.server = o;
    config.connected = index;
    config.selected = index;
    fprintf(stderr, "gta2: net: connected to %s\n", config.servers[static_cast<size_t>(index)].name.c_str());
    return r;
}

void DisconnectServer(NetConfig& config)
{
    LeaveRoom();
    if (config.connected >= 0)
        fprintf(stderr, "gta2: net: disconnected from %s\n", config.servers[static_cast<size_t>(config.connected)].name.c_str());
    config.connected = -1;
    config.server.cookie.clear();
}

#endif

LanDiscovery* Discovery()
{
    static std::unique_ptr<LanDiscovery> discovery;
    if (!discovery)
        discovery = std::make_unique<LanDiscovery>(atoi(Env("GTA2_LAN_BEACON_PORT", "27900").c_str()));
    return discovery.get();
}

bool JoinLanRoom(const NetConfig& config, const std::string& roomId, const std::vector<std::string>& seeds,
                 const std::string& room, bool host, std::string* error)
{
    LeaveRoom();
    LanOptions o;
    o.roomId = roomId;
    o.room = room;
    o.name = config.name;
    o.pin = config.lanPin;
    o.host = host;
    o.seeds = seeds;
    o.port = atoi(Env("GTA2_LAN_PORT", std::to_string(kLanPort).c_str()).c_str());
    fprintf(stderr, "gta2: lan: %s room \"%s\" as \"%s\"\n", host ? "creating" : "joining", room.c_str(), config.name.c_str());
    std::unique_ptr<LanTransport> t = LanTransport::Create(o, Discovery(), error);
    if (!t)
    {
        fprintf(stderr, "gta2: lan: %s\n", error->c_str());
        return false;
    }
    // the host's beacon: what the room's lobby shows of it
    t->beaconInfo = [] {
        Json i = Json::Object();
        if (gLobby)
        {
            i.Set("mode", gLobby->Settings()["mode"].Str());
            i.Set("players", static_cast<int>(gLobby->Players().size()));
            i.Set("state", gLobby->Started() ? "playing" : "lobby");
        }
        return i;
    };
    gTransport = std::move(t);
    gLobby = std::make_unique<Lobby>(gTransport.get(), config.name, host);
    return true;
}

#if !defined(GTA2_NET_LAN)
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

#endif

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
#if defined(GTA2_NET_LAN)
    // the local network: the host creates the room; a guest joins GTA2_LAN_JOIN (an address) or
    // the room of that name it hears of (in the lobby)
    bool joined = false;
    if (host)
    {
        joined = JoinLanRoom(config, "", {}, config.room, true, &error);
    }
    else if (!Env("GTA2_LAN_JOIN", "").empty())
    {
        joined = JoinLanRoom(config, "", {Env("GTA2_LAN_JOIN", "")}, config.room, false, &error);
    }
    else
    {
        const double until = NowMs() + waitMs;
        while (!joined && NowMs() < until)
        {
            Discovery()->Poll();
            for (const auto& [id, g] : Discovery()->Games())
            {
                if (g.room == config.room && g.state != "playing" && !g.hostName.empty())
                {
                    std::vector<std::string> seeds;
                    for (const LanGame::Member& m : g.members)
                        seeds.push_back(m.address);
                    joined = JoinLanRoom(config, g.roomId, seeds, g.room, false, &error);
                    break;
                }
            }
            Sleep10ms();
        }
        if (!joined)
            fprintf(stderr, "gta2: lan: no room \"%s\" heard of on the network\n", config.room.c_str());
    }
    if (!joined)
        return 2;
#else
    if (!JoinRoom(config, config.name, config.room, host, &error))
        return 2;
#endif

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
        s.Set("pointsTarget", atoi(Env("GTA2_NET_POINTS", "0").c_str()));
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

// main.cpp: the match should end now (a Gang Wars crew reached the points target, 6 s ago).
extern "C" bool Gta2Net_EndRequested()
{
    return gInMatch && CoopEndRequested();
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
    // the lobby connects again to the server of this match (lobby_screen.cpp)
    if (gConfig && gConfig->connected >= 0)
        setenv("GTA2_NET_RECONNECT", gConfig->servers[static_cast<size_t>(gConfig->connected)].name.c_str(), 1);
    fflush(stderr);
    execv("/proc/self/exe", argv);
    fprintf(stderr, "gta2: net: could not restart for the lobby\n");
#else
    (void)argv;
#endif
}
