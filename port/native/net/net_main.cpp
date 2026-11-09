// gta2-mp before and during the game: joins a room on the game server, runs the lobby
// until the host starts the match and everyone is loaded, then lets main.cpp start the game
// as a network game (-N), as web/shell.js does before gta2_start; while the match runs,
// polls the transport and the sync checks once per main-loop iteration.
//
// For now (A1) the lobby is driven by the environment, for tests; the lobby screen for the
// controller comes later (A4):
//   GTA2_NET_SERVER=ws://host:port   the game server (default ws://127.0.0.1:8790)
//   GTA2_NET_PASSWORD=...            its password, or GTA2_NET_INVITE=<invite code>
//   GTA2_NET_ROOM=name               the room (default "gta2")
//   GTA2_NET_NAME=name               this player's name (default "Player")
//   GTA2_NET_HOST=1                  this player creates the room and starts the match...
//   GTA2_NET_PLAYERS=n               ...once n players are in it (default 2)
//   GTA2_NET_MODE=roam|wars|ffa, GTA2_NET_DISTRICT=downtown|residential|industrial,
//   GTA2_NET_FRIENDLY_FIRE=1, GTA2_NET_MINUTES=m, GTA2_NET_KILLS=n   the host's settings
//   GTA2_NET_CREW=name, GTA2_NET_SIDE=gang code   this player's Gang Wars crew and gang
//   GTA2_NET_WAIT=s                  how long to wait in the lobby (default 300)
// Multiplayer build only (gta2-mp).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <memory>
#include <string>

#include "coop_native.h"
#include "lobby.h"
#include "ws_transport.h"

namespace
{
using namespace gta2net;

std::unique_ptr<Transport> gTransport;
std::unique_ptr<Lobby> gLobby;
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

// Returns 0 once the match can start (cmd then has -N), or the exit code.
extern "C" int Gta2Net_PreGame(char* cmd, int size)
{
    ServerOptions o;
    o.url = Env("GTA2_NET_SERVER", "ws://127.0.0.1:8790");
    o.room = Env("GTA2_NET_ROOM", "gta2");
    o.password = Env("GTA2_NET_PASSWORD", "");
    o.invite = Env("GTA2_NET_INVITE", "");
    const std::string name = Env("GTA2_NET_NAME", "Player");
    const bool host = atoi(Env("GTA2_NET_HOST", "0").c_str()) != 0;
    const int wanted = atoi(Env("GTA2_NET_PLAYERS", "2").c_str());
    const double waitMs = atof(Env("GTA2_NET_WAIT", "300").c_str()) * 1000.0;

    std::string error;
    fprintf(stderr, "gta2: net: joining room \"%s\" on %s as \"%s\"%s\n", o.room.c_str(), o.url.c_str(), name.c_str(),
            host ? " (host)" : "");
    std::unique_ptr<WsTransport> ws = WsTransport::Connect(o, &error);
    if (!ws)
    {
        fprintf(stderr, "gta2: net: %s\n", error.c_str());
        return 2;
    }
    gTransport = std::move(ws);
    gLobby = std::make_unique<Lobby>(gTransport.get(), name, host);

    Json match;
    bool refused = false;
    bool go = false;
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
        fprintf(stderr, "gta2: net: the server refused us: %s\n", why == "full" ? "the room is full" : "id taken");
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
                    fprintf(stderr, "gta2: net: cannot start yet: %s\n", problem.c_str());
                    lastProblem = problem;
                    lastProblemAt = now;
                }
            }
        }
        Sleep10ms();
    }

    SetupCoop(gLobby.get(), match);
    gLobby->AllLoaded([&] { go = true; });
    const double loadedAt = NowMs();
    while (!go)
    {
        gTransport->Poll();
        if (!gTransport->Connected())
            return 2;
        if (NowMs() - loadedAt > 60000)
        {
            fprintf(stderr, "gta2: net: not everyone was ready within 60 s\n");
            return 3;
        }
        Sleep10ms();
    }
    fprintf(stderr, "gta2: net: everyone is ready, starting the match\n");
    gInMatch = true;
    const size_t len = strlen(cmd);
    snprintf(cmd + len, static_cast<size_t>(size) - len, "%s-N", len ? " " : "");
    return 0;
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
