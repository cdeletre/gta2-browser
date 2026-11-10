// Tests of the multiplayer code without the engine: the JSON, the lobby's rules and a whole
// lobby (host and two players) over in-memory transports. Run: build/native/<preset>/gta2-net-test
// With GTA2_NET_TEST_TLS=<certificates>:<first port> (scripts/native/test_tls.sh sets it and
// starts the relays), also the WebSocket transport in ws:// and wss:// with each certificate case.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <memory>
#include <string>
#include <vector>

#include "json.h"
#include "lobby.h"
#include "lan_transport.h"
#include "memory_transport.h"
#include "net_session.h"
#include "ws_transport.h"

using namespace gta2net;

namespace
{
int gChecks = 0;
int gFailures = 0;

#define CHECK(cond)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        ++gChecks;                                                                                                     \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            ++gFailures;                                                                                               \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                            \
        }                                                                                                              \
    } while (0)

#define CHECK_EQ(a, b)                                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        ++gChecks;                                                                                                     \
        const auto va = (a);                                                                                           \
        const auto vb = (b);                                                                                           \
        if (!(va == vb))                                                                                               \
        {                                                                                                              \
            ++gFailures;                                                                                               \
            fprintf(stderr, "FAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b);                                     \
        }                                                                                                              \
    } while (0)

Json J(const char* text)
{
    Json j;
    if (!Json::Parse(text, &j))
    {
        fprintf(stderr, "bad JSON in a test: %s\n", text);
    }
    return j;
}

void TestJson()
{
    // round trips keep key order and the browser's number format (JSON.stringify)
    const char* text = R"({"lobby":"roster","players":[{"id":"a1b2c3d4e5f6","name":"Zoé","crew":"","ping":12}],)"
                       R"("settings":{"mode":"roam","killLimit":10,"x":1.5,"neg":-3,"t":true,"n":null}})";
    Json j = J(text);
    CHECK(j.IsObject());
    CHECK_EQ(j.Dump(), std::string(text));
    CHECK_EQ(j["players"][0]["name"].Str(), std::string("Zoé"));
    CHECK_EQ(j["players"][0]["ping"].Int(), 12);
    CHECK(j["settings"]["x"].IsNumber() && !j["settings"]["x"].IsInteger());
    CHECK(j["settings"]["killLimit"].IsInteger());
    CHECK(j["missing"]["deeper"].IsNull());
    CHECK(j["players"][5].IsNull());

    // escapes, \u sequences (a surrogate pair), whitespace
    Json e = J(" { \"s\" : \"a\\\"b\\\\c\\n\\u00e9\\ud83d\\ude00\" } ");
    CHECK_EQ(e["s"].Str(), std::string("a\"b\\c\n\xc3\xa9\xf0\x9f\x98\x80"));
    CHECK_EQ(e.Dump(), std::string("{\"s\":\"a\\\"b\\\\c\\n\xc3\xa9\xf0\x9f\x98\x80\"}"));
    CHECK_EQ(Json("\x01").Dump(), std::string("\"\\u0001\""));

    // errors
    Json bad;
    CHECK(!Json::Parse("{\"a\":}", &bad));
    CHECK(!Json::Parse("[1,2", &bad));
    CHECK(!Json::Parse("{} x", &bad));
    CHECK(!Json::Parse("\"unterminated", &bad));
    CHECK(bad.IsNull());

    // truthiness and String() as in JavaScript
    CHECK(!Json(0).Truthy() && Json(2).Truthy() && !Json("").Truthy() && Json("x").Truthy() && !Json().Truthy());
    CHECK_EQ(Json(42).ToText(), std::string("42"));
    CHECK_EQ(Json().ToText(), std::string("null"));

    // equality ignores key order
    CHECK(J("{\"a\":1,\"b\":[1,2]}") == J("{\"b\":[1,2],\"a\":1}"));
    CHECK(J("{\"a\":1}") != J("{\"a\":2}"));
}

void TestRules()
{
    // inputDelayFor: ceil((rtt / 2 + 10) / 33.3), between 1 and 8
    CHECK_EQ(InputDelayFor(0), 1);
    CHECK_EQ(InputDelayFor(13), 1);
    CHECK_EQ(InputDelayFor(50), 2);
    CHECK_EQ(InputDelayFor(120), 3);
    CHECK_EQ(InputDelayFor(1000), 8);
    // cleanCrew
    CHECK_EQ(CleanCrew("  The <Crew>!  "), std::string("The Crew"));
    CHECK_EQ(CleanCrew("abcdefghijklmnopqrstuvwxyz"), std::string("abcdefghijklmnop"));
    CHECK_EQ(CleanCrew("Les Bérets"), std::string("Les Bérets"));
    CHECK_EQ(CleanCrew("o'neil_crew-2.0"), std::string("o'neil_crew-2.0"));
    CHECK_EQ(std::string(GangsOf("industrial")[1].code), std::string("rus"));
    CHECK_EQ(std::string(GangsOf("nowhere")[2].code), std::string("yak"));
}

// A room: a host and players over one hub; Poll() runs every transport until nothing moves.
struct Room
{
    MemoryHub hub;
    std::vector<std::unique_ptr<MemoryTransport>> transports;
    std::vector<std::unique_ptr<Lobby>> lobbies;
    std::vector<Json> starts;
    std::vector<int> changes;
    std::vector<std::vector<std::pair<int, Bytes>>> game;
    std::vector<std::vector<int>> left;

    Lobby& Add(const char* name, bool host)
    {
        transports.push_back(hub.Connect());
        const size_t i = lobbies.size();
        starts.emplace_back();
        changes.push_back(0);
        game.emplace_back();
        left.emplace_back();
        lobbies.push_back(std::make_unique<Lobby>(transports.back().get(), name, host));
        Lobby& l = *lobbies.back();
        l.onStart = [this, i](const Json& m) { starts[i] = m; };
        l.onChange = [this, i] { changes[i]++; };
        l.onGameMessage = [this, i](int slot, const Bytes& b) { game[i].emplace_back(slot, b); };
        l.onPlayerLeft = [this, i](int slot) { left[i].push_back(slot); };
        Poll();
        return l;
    }

    void Poll()
    {
        for (int round = 0; round < 20; ++round)
        {
            for (auto& t : transports)
            {
                if (t)
                    t->Poll();
            }
        }
    }
};

void TestLobby()
{
    Room room;
    Lobby& host = room.Add("Host", true);
    Lobby& b = room.Add("Bea", false);
    Lobby& c = room.Add("Cy", false);

    // hello -> roster: everyone sees the same three players in join order, the host first
    CHECK_EQ(host.Players().size(), size_t(3));
    CHECK(b.Players() == host.Players());
    CHECK(c.Players() == host.Players());
    CHECK_EQ(host.Players()[0]["name"].Str(), std::string("Host"));
    CHECK_EQ(host.Players()[1]["name"].Str(), std::string("Bea"));
    CHECK_EQ(host.Players()[2]["id"].Str(), c.MyId());
    CHECK_EQ(b.HostId(), host.MyId());
    CHECK(room.changes[1] > 0);
    CHECK_EQ(host.StartProblem(), std::string(""));

    // settings from the host reach everyone; a player cannot change them
    Json change = Json::Object();
    change.Set("district", "industrial");
    host.SetSettings(change);
    Json other = Json::Object();
    other.Set("district", "residential");
    b.SetSettings(other);
    room.Poll();
    CHECK_EQ(b.Settings()["district"].Str(), std::string("industrial"));
    CHECK_EQ(host.Settings()["district"].Str(), std::string("industrial"));

    // Gang Wars: crews and gangs, with the start problems in order
    Json wars = Json::Object();
    wars.Set("mode", "wars");
    host.SetSettings(wars);
    room.Poll();
    CHECK_EQ(host.StartProblem(), std::string("needCrew"));
    host.SetCrew("Red <Team>");
    b.SetCrew("Red Team");
    c.SetCrew("Blue");
    room.Poll();
    CHECK_EQ(host.Crews().size(), size_t(2));
    CHECK_EQ(host.Crews()[0]["name"].Str(), std::string("Red Team"));
    CHECK_EQ(host.Crews()[0]["members"].size(), size_t(2));
    CHECK_EQ(host.StartProblem(), std::string("waitingGang:Red Team and Blue"));
    b.PickSide("rus");
    c.PickSide("rus"); // taken by the other crew: refused
    room.Poll();
    CHECK_EQ(host.Settings()["sides"]["Red Team"].Str(), std::string("rus"));
    CHECK(host.Settings()["sides"]["Blue"].IsNull());
    c.PickSide("yak"); // not an Industrial gang: refused
    room.Poll();
    CHECK(host.Settings()["sides"]["Blue"].IsNull());
    c.PickSide("kri");
    room.Poll();
    CHECK_EQ(host.StartProblem(), std::string(""));
    CHECK_EQ(c.Crews()[1]["gang"].Str(), std::string("Hare Krishnas"));
    b.PickSide("");
    room.Poll();
    CHECK(!host.Settings()["sides"].Has("Red Team"));
    b.PickSide("rus");
    room.Poll();
    // the host sets a guest crew's gang (a taken one is refused), then gives it back
    host.HostSetSide("Blue", "rus");
    CHECK_EQ(host.Settings()["sides"]["Blue"].Str(), std::string("kri"));
    host.HostSetSide("Blue", "");
    room.Poll();
    CHECK(!c.Settings()["sides"].Has("Blue"));
    host.HostSetSide("Blue", "zai");
    room.Poll();
    CHECK_EQ(c.Crews()[1]["gang"].Str(), std::string("Zaibatsu"));
    b.HostSetSide("Reds", ""); // not the host: nothing
    room.Poll();
    CHECK_EQ(host.Settings()["sides"]["Red Team"].Str(), std::string("rus"));
    // a player switching crews: the crew left empty is gone
    c.SetCrew("Red Team");
    room.Poll();
    CHECK_EQ(host.Crews().size(), size_t(1));
    c.SetCrew("Blue");
    room.Poll();
    CHECK_EQ(host.Crews().size(), size_t(2));
    host.HostSetSide("Blue", "kri");
    room.Poll();

    // pings set the input delay (40 ms of latency: about 80 ms round trip -> 2 ticks)
    room.hub.latencyMs = 40;
    host.MeasurePings();
    for (int i = 0; i < 30; ++i)
    {
        room.Poll();
        struct timespec ts = {0, 5 * 1000 * 1000};
        nanosleep(&ts, nullptr);
    }
    CHECK(host.Players()[1]["ping"].Int() >= 80);
    CHECK(b.Players()[2]["ping"].IsNumber());

    // start: everyone gets the same match, with their own slot
    host.Start();
    for (int i = 0; i < 30 && room.starts[2].IsNull(); ++i)
    {
        room.Poll();
        struct timespec ts = {0, 5 * 1000 * 1000};
        nanosleep(&ts, nullptr);
    }
    room.hub.latencyMs = 0;
    room.Poll();
    CHECK(host.Started() && b.Started() && c.Started());
    CHECK_EQ(room.starts[0]["mySlot"].Int(), 0);
    CHECK_EQ(room.starts[1]["mySlot"].Int(), 1);
    CHECK_EQ(room.starts[2]["mySlot"].Int(), 2);
    CHECK_EQ(room.starts[0]["delay"].Int(), 2);
    CHECK_EQ(room.starts[2]["delay"].Int(), 2);
    CHECK_EQ(room.starts[1]["district"].Str(), std::string("industrial"));
    CHECK_EQ(room.starts[1]["crews"].size(), size_t(2));
    CHECK_EQ(room.starts[0]["id"].Str(), room.starts[2]["id"].Str());
    CHECK_EQ(room.starts[0]["lang"].Str(), std::string("en"));

    // game messages sent before a player's game is ready wait for it
    b.SendGame(-1, Bytes{1, 0, 9, 9});
    room.Poll();
    CHECK(room.game[0].empty());
    CHECK_EQ(host.stats.early, 1);
    host.GameReady();
    CHECK_EQ(room.game[0].size(), size_t(1));
    CHECK_EQ(room.game[0][0].first, 1);
    // Cy kept Bea's message for everyone too, until its game was ready
    c.GameReady();
    b.GameReady();
    CHECK_EQ(room.game[2].size(), size_t(1));
    CHECK_EQ(room.game[2][0].first, 1);
    // to one slot only
    host.SendGame(2, Bytes{3, 1, 7});
    room.Poll();
    CHECK_EQ(room.game[2].size(), size_t(2));
    CHECK(room.game[1].empty());
    CHECK(room.game[2][1].second == (Bytes{3, 1, 7}));
    CHECK_EQ(room.game[2][1].first, 0);

    // everyone loaded: go reaches every player once
    int goes = 0;
    b.AllLoaded([&] { goes++; });
    c.AllLoaded([&] { goes++; });
    room.Poll();
    CHECK_EQ(goes, 0); // the host is not loaded yet
    host.AllLoaded([&] { goes++; });
    room.Poll();
    CHECK_EQ(goes, 3);

    // sync and chat by slot
    int syncSlot = -1;
    std::string syncH;
    host.onSync = [&](int slot, int tick, const std::string& h) {
        syncSlot = slot;
        syncH = std::to_string(tick) + "=" + h;
    };
    c.ShareSync(90, "1:2:3:4:5");
    room.Poll();
    CHECK_EQ(syncSlot, 2);
    CHECK_EQ(syncH, std::string("90=1:2:3:4:5"));

    // a player leaving the match: the others' games are told the slot
    room.transports[1]->Leave();
    room.Poll();
    CHECK_EQ(room.left[0].size(), size_t(1));
    CHECK_EQ(room.left[0][0], 1);
    CHECK_EQ(room.left[2][0], 1);
}

void TestLeaveBeforeStart()
{
    Room room;
    Lobby& host = room.Add("Host", true);
    Lobby& b = room.Add("Bea", false);
    room.Add("Cy", false);
    CHECK_EQ(b.Players().size(), size_t(3));
    room.transports[2]->Leave();
    room.Poll();
    CHECK_EQ(host.Players().size(), size_t(2));
    CHECK_EQ(b.Players().size(), size_t(2));
    CHECK(room.left[0].empty());
    // a room of one cannot start
    room.transports[1]->Leave();
    room.Poll();
    CHECK_EQ(host.StartProblem(), std::string("waitingPlayer"));
}

// A roster as a browser host sends it (fields this port does not use are kept).
void TestBrowserRoster()
{
    MemoryHub hub;
    auto t = hub.Connect();
    Lobby lobby(t.get(), "Native", false);
    auto browser = hub.Connect();
    Json got;
    browser->onMessage = [&](const std::string&, const Message& m) { got = m.json; };
    browser->Join();
    t->Poll();
    browser->Poll();
    CHECK_EQ(got["lobby"].Str(), std::string("hello"));
    CHECK_EQ(got["name"].Str(), std::string("Native"));
    const std::string roster = std::string(R"({"lobby":"roster","players":[{"id":")") + browser->Id() +
        R"(","name":"Web","crew":"","colour":7},{"id":")" + t->Id() +
        R"(","name":"Native","crew":""}],"settings":{"mode":"ffa","district":"downtown","friendlyFire":false,)"
        R"("minutes":5,"killLimit":10,"pointsTarget":0,"sides":{},"lang":"fr","future":{"a":1}},"v":4})";
    browser->Send(t->Id(), Message::Of(J(roster.c_str())));
    t->Poll();
    CHECK_EQ(lobby.Players()[0]["colour"].Int(), 7);
    CHECK_EQ(lobby.Settings()["future"]["a"].Int(), 1);
    CHECK_EQ(lobby.Settings()["mode"].Str(), std::string("ffa"));
    // a start from someone other than the host is ignored
    auto stranger = hub.Connect();
    stranger->Join();
    t->Poll();
    stranger->Send(t->Id(), Message::Of(J(R"({"lobby":"start","match":{"players":[]}})")));
    t->Poll();
    CHECK(!lobby.Started());
    const std::string start = std::string(R"({"lobby":"start","match":{"mode":"ffa","lang":"de","delay":0,"players":[{"id":")") +
        browser->Id() + R"(","name":"Web"},{"id":")" + t->Id() + R"(","name":"Native"}]}})";
    Json match;
    lobby.onStart = [&](const Json& m) { match = m; };
    browser->Send(t->Id(), Message::Of(J(start.c_str())));
    t->Poll();
    CHECK(lobby.Started());
    CHECK_EQ(match["mySlot"].Int(), 1);
    CHECK_EQ(match["delay"].Int(), 1);           // match.delay || 1
    CHECK_EQ(match["lang"].Str(), std::string("en")); // not a language of the port: English for everyone
}
// A player or a host of another co-op version (or of a build from before the check).
void TestVersion()
{
    MemoryHub hub;
    auto ht = hub.Connect();
    Lobby host(ht.get(), "Host", true);
    auto old = hub.Connect();
    Json got;
    old->onMessage = [&](const std::string&, const Message& m) { got = m.json; };
    old->Join();
    ht->Poll();
    old->Poll();
    // an old player's hello (no v): turned away, not in the roster
    old->Send(ht->Id(), Message::Of(J(R"({"lobby":"hello","name":"Old"})")));
    ht->Poll();
    old->Poll();
    CHECK_EQ(host.Players().size(), size_t(1));
    CHECK_EQ(got["lobby"].Str(), std::string("refused"));
    CHECK_EQ(got["v"].Int(), kCoopVersion);
    // our hello carries the version; a roster from an old host makes us leave
    auto ct = hub.Connect();
    Lobby guest(ct.get(), "Guest", false);
    std::string why;
    guest.onRefused = [&](const std::string& w) { why = w; };
    old->Poll();
    ct->Poll();
    old->Send(ct->Id(), Message::Of(J(R"({"lobby":"roster","players":[],"settings":{}})")));
    ct->Poll();
    CHECK_EQ(why, std::string("version:1"));
    CHECK(guest.Players().size() == 0);
}

// ---- gta2-lan: the mesh of LanTransport (127.0.0.1), the beacons, a lobby over it ----

bool PumpAll(const std::vector<Transport*>& ts, const std::function<bool()>& done, double ms = 3000)
{
    const double end = NowMs() + ms;
    while (!done() && NowMs() < end)
    {
        for (Transport* t : ts)
            t->Poll();
        struct timespec ts2 = {0, 2000000};
        nanosleep(&ts2, nullptr);
    }
    return done();
}

void TestLan()
{
    LanOptions o;
    o.room = "LANTEST";
    o.name = "Host";
    o.host = true;
    o.port = 27931;
    std::string error;
    auto a = LanTransport::Create(o, nullptr, &error);
    CHECK(a != nullptr);
    if (!a)
    {
        fprintf(stderr, "  lan: %s\n", error.c_str());
        return;
    }
    const std::string seed = "127.0.0.1:" + std::to_string(a->Port());
    LanOptions g = o;
    g.host = false;
    g.roomId = a->RoomId();
    g.seeds = {seed};
    g.name = "B";
    auto b = LanTransport::Create(g, nullptr, &error);
    g.name = "C";
    auto c = LanTransport::Create(g, nullptr, &error);
    CHECK(b && c && b->Port() != a->Port() && c->Port() != b->Port());
    std::map<std::string, std::vector<std::string>> joined;
    std::map<std::string, std::vector<std::string>> left;
    std::map<std::string, std::vector<Message>> got;
    for (LanTransport* t : {a.get(), b.get(), c.get()})
    {
        const std::string me = t->Id();
        t->onPeerJoined = [&joined, me](const std::string& id) { joined[me].push_back(id); };
        t->onPeerLeft = [&left, me](const std::string& id) { left[me].push_back(id); };
        t->onMessage = [&got, me](const std::string&, const Message& m) { got[me].push_back(m); };
    }
    a->Join();
    b->Join();
    c->Join(); // C knows only A: B comes from A's list of members
    const std::vector<Transport*> all = {a.get(), b.get(), c.get()};
    CHECK(PumpAll(all, [&] { return joined[a->Id()].size() == 2 && joined[b->Id()].size() == 2 && joined[c->Id()].size() == 2; }));
    fprintf(stderr, "  lan mesh: joined %zu %zu %zu\n", joined[a->Id()].size(), joined[b->Id()].size(), joined[c->Id()].size());
    // to everyone, to one, binary, a large one, a ping
    a->Send("", Message::Of(J(R"({"hello":"lan"})")));
    Bytes every(256);
    for (int i = 0; i < 256; ++i)
        every[i] = static_cast<uint8_t>(i);
    b->Send(c->Id(), Message::Of(every));
    a->Send("@server", Message::Of(J(R"({"lobby":"score"})"))); // no one here
    CHECK(PumpAll(all, [&] { return got[b->Id()].size() == 1 && got[c->Id()].size() == 2; }));
    CHECK(got[c->Id()].size() == 2 && got[c->Id()][1].binary && got[c->Id()][1].bytes == every);
    double rtt = -1;
    c->Ping(a->Id(), [&](double ms) { rtt = ms; }, [] {});
    CHECK(PumpAll(all, [&] { return rtt >= 0; }));
    // a wrong PIN is refused
    LanOptions w = g;
    w.pin = "1234";
    w.name = "Wrong";
    auto x = LanTransport::Create(w, nullptr, &error);
    std::string why;
    x->onRefused = [&](const std::string& r) { why = r; };
    x->Join();
    CHECK(PumpAll({a.get(), b.get(), c.get(), x.get()}, [&] { return !why.empty(); }));
    CHECK_EQ(why, std::string("pin"));
    CHECK(joined[a->Id()].size() == 2);
    x.reset();
    // C leaves: A and B see it go
    c->Leave();
    CHECK(PumpAll({a.get(), b.get()}, [&] { return left[a->Id()].size() == 1 && left[b->Id()].size() == 1; }));
    c.reset();
    b.reset();
    a.reset();

    // the beacons (over loopback here) list a room with its members
    setenv("GTA2_LAN_LOOPBACK", "1", 1);
    LanDiscovery listen(27935);
    LanDiscovery d(27935);
    CHECK(listen.Ok() && d.Ok());
    LanOptions h = o;
    h.port = 27941;
    auto host = LanTransport::Create(h, &d, &error);
    host->beaconInfo = [] {
        Json i = Json::Object();
        i.Set("mode", "ffa");
        i.Set("state", "lobby");
        i.Set("players", 1);
        return i;
    };
    host->Join();
    CHECK(PumpAll({host.get()}, [&] {
        listen.Poll();
        return listen.Games().count(host->RoomId()) > 0;
    }));
    if (listen.Games().count(host->RoomId()))
    {
        const LanGame& lg = listen.Games().at(host->RoomId());
        CHECK_EQ(lg.room, std::string("LANTEST"));
        CHECK_EQ(lg.hostName, std::string("Host"));
        CHECK_EQ(lg.mode, std::string("ffa"));
        CHECK_EQ(lg.members.size(), size_t(1));
        fprintf(stderr, "  lan beacon: %s by %s at %s\n", lg.room.c_str(), lg.hostName.c_str(), lg.members[0].address.c_str());
    }
    // a guest found from the beacons alone (no seed), then a lobby over the mesh
    LanOptions gg = o;
    gg.host = false;
    gg.port = 27945;
    gg.roomId = host->RoomId();
    gg.name = "Guest";
    LanDiscovery d2(27935);
    auto guest = LanTransport::Create(gg, &d2, &error);
    Lobby hostLobby(host.get(), "Host", true);
    Lobby guestLobby(guest.get(), "Guest", false);
    guest->Join();
    CHECK(PumpAll({host.get(), guest.get()}, [&] { return hostLobby.Players().size() == 2 && guestLobby.Players().size() == 2; }, 6000));
    fprintf(stderr, "  lan lobby: host sees %zu players, guest %zu\n", hostLobby.Players().size(), guestLobby.Players().size());
    unsetenv("GTA2_LAN_LOOPBACK");
}

// ---- conf/online.ini and conf/lan.ini: [server] sections, the older net.ini, the environment ----

std::string WriteFile(const std::string& dir, const std::string& text, const char* file = "online.ini")
{
    std::ofstream(dir + "/" + file) << text;
    return dir;
}

std::string ReadFile(const std::string& path)
{
    std::ifstream in(path);
    std::stringstream all;
    all << in.rdbuf();
    return all.str();
}

void TestNetConfig()
{
    char tmpl[] = "/tmp/gta2-net-test-XXXXXX";
    const std::string dir = mkdtemp(tmpl);
    const std::string online = dir + "/online.ini";
    unsetenv("GTA2_NET_SERVER");
    unsetenv("GTA2_NET_PASSWORD");

    // the new format: order kept, a section without url left out, names made one each
    WriteFile(dir, "[net]\nserver = Home\nname = Zed\n"
                   "[server]\nname = Main\nurl = wss://gta2.example.com\npassword = pw\n"
                   "[server]\nname = Home\nurl = ws://10.0.0.2:8790\ninvite = abc\nca_file = home.pem\n"
                   "[server]\nname = Broken\n"
                   "[server]\nurl = wss://other.example.org:8443/relay\ntls_fingerprint = sha256:00\n"
                   "[server]\nname = Main\nurl = wss://second.example.com\n");
    NetConfig c = LoadNetConfig(dir, NetKind::kOnline);
    CHECK_EQ(c.servers.size(), size_t(4));
    if (c.servers.size() == 4)
    {
        CHECK_EQ(c.servers[0].name, std::string("Main"));
        CHECK_EQ(c.servers[0].password, std::string("pw"));
        CHECK_EQ(c.servers[1].name, std::string("Home"));
        CHECK_EQ(c.servers[2].name, std::string("other.example.org")); // no name: the url's host
        CHECK_EQ(c.servers[3].name, std::string("Main 2"));
        CHECK_EQ(c.selected, 1);
        CHECK_EQ(c.server.url, std::string("ws://10.0.0.2:8790"));
        CHECK_EQ(c.server.invite, std::string("abc"));
        CHECK_EQ(c.server.tls.caFile, dir + "/home.pem"); // relative: in conf/
        CHECK_EQ(OptionsOf(c, 2).tls.fingerprint, std::string("sha256:00"));
    }
    CHECK_EQ(c.name, std::string("Zed"));

    // written back and read again: the same servers, the one picked; no [lan] in online.ini
    c.servers[0].password = "typed";
    c.selected = 0;
    SaveNetConfig(c);
    CHECK(ReadFile(online).find("[lan]") == std::string::npos);
    NetConfig again = LoadNetConfig(dir, NetKind::kOnline);
    CHECK_EQ(again.servers.size(), size_t(4));
    CHECK_EQ(again.selected, 0);
    CHECK_EQ(again.servers[0].password, std::string("typed"));
    CHECK_EQ(again.servers[3].name, std::string("Main 2"));

    // the older one-server format: its server = <url> and login in [net], as one server
    WriteFile(dir, "[net]\nserver = wss://gta2.example.com\npassword = old\ninvite = \nca_file = \nname = Kasia\n");
    NetConfig old = LoadNetConfig(dir, NetKind::kOnline);
    CHECK_EQ(old.servers.size(), size_t(1));
    CHECK_EQ(old.servers[0].name, std::string("gta2.example.com"));
    CHECK_EQ(old.server.password, std::string("old"));
    SaveNetConfig(old);
    const std::string written = ReadFile(online);
    CHECK(written.find("[server]\nname = gta2.example.com\nurl = wss://gta2.example.com\npassword = old\n") != std::string::npos);
    CHECK(written.find("server = gta2.example.com\n") != std::string::npos);

    // the environment: a server of its own, chosen, never written; a password for a file's
    // server is for this run only
    setenv("GTA2_NET_SERVER", "ws://127.0.0.1:9", 1);
    setenv("GTA2_NET_PASSWORD", "envpw", 1);
    NetConfig env = LoadNetConfig(dir, NetKind::kOnline);
    CHECK_EQ(env.servers.size(), size_t(2));
    CHECK(env.servers[0].fromEnv);
    CHECK_EQ(env.server.url, std::string("ws://127.0.0.1:9"));
    CHECK_EQ(env.server.password, std::string("envpw"));
    SaveNetConfig(env);
    CHECK(ReadFile(online).find("127.0.0.1:9") == std::string::npos);
    unsetenv("GTA2_NET_SERVER");
    NetConfig over = LoadNetConfig(dir, NetKind::kOnline);
    CHECK_EQ(over.server.password, std::string("envpw"));
    SaveNetConfig(over);
    CHECK(ReadFile(online).find("envpw") == std::string::npos);
    unsetenv("GTA2_NET_PASSWORD");

    // no file: written with one server, this computer's
    unlink(online.c_str());
    NetConfig fresh = LoadNetConfig(dir, NetKind::kOnline);
    CHECK_EQ(fresh.servers.size(), size_t(1));
    CHECK_EQ(fresh.server.url, std::string("ws://127.0.0.1:8790"));
    CHECK(ReadFile(online).find("[server]\nname = This computer\n") != std::string::npos);
    unlink(online.c_str());

    // the net.ini both binaries shared: each takes its part, then it is renamed net.ini.old
    WriteFile(dir,
              "[net]\nserver = Main\nname = Kdog\nrooms = A1, B2\ncrew = Wolf Pack\n"
              "[lan]\npin = 4321\n"
              "[server]\nname = Main\nurl = wss://gta2.example.com\npassword = pw\n",
              "net.ini");
    NetConfig mOnline = LoadNetConfig(dir, NetKind::kOnline);
    CHECK_EQ(mOnline.servers.size(), size_t(1));
    CHECK_EQ(mOnline.server.password, std::string("pw"));
    CHECK_EQ(mOnline.name, std::string("Kdog"));
    CHECK(ReadFile(online).find("[lan]") == std::string::npos);
    CHECK(access((dir + "/net.ini").c_str(), F_OK) == 0); // gta2-lan has not taken its part yet
    NetConfig mLan = LoadNetConfig(dir, NetKind::kLan);
    CHECK_EQ(mLan.lanPin, std::string("4321"));
    CHECK_EQ(mLan.name, std::string("Kdog"));
    CHECK_EQ(mLan.rooms.size(), size_t(2));
    const std::string lanText = ReadFile(dir + "/lan.ini");
    CHECK(lanText.find("pin = 4321") != std::string::npos);
    CHECK(lanText.find("[server]") == std::string::npos && lanText.find("server =") == std::string::npos &&
          lanText.find("password") == std::string::npos);
    CHECK(access((dir + "/net.ini").c_str(), F_OK) != 0 && access((dir + "/net.ini.old").c_str(), F_OK) == 0);
    for (const char* f : {"/online.ini", "/lan.ini", "/net.ini.old"})
        unlink((dir + f).c_str());
    rmdir(dir.c_str());
}

// ---- ws:// and wss:// against scripts/native/test_relay.py (scripts/native/test_tls.sh) ----

std::string gTlsDir;
int gTlsBase = 0;

ServerOptions Relay(const std::string& scheme, int port, const std::string& host = "localhost")
{
    ServerOptions o;
    o.url = scheme + "://" + host + ":" + std::to_string(gTlsBase + port);
    o.room = "tls-test-" + std::to_string(port);
    o.password = "pw";
    o.timeoutMs = 3000;
    return o;
}

// Polls both until done() or 3 s
bool PumpUntil(Transport* a, Transport* b, const std::function<bool()>& done)
{
    const double end = NowMs() + 3000;
    while (!done() && NowMs() < end)
    {
        a->Poll();
        if (b)
            b->Poll();
        struct timespec ts = {0, 2000000};
        nanosleep(&ts, nullptr);
    }
    return done();
}

// Two players through the relay: join, a text message, a binary one of every byte value, a large
// one (several TLS records), a ping, and the other leaving.
void Exchange(const ServerOptions& o, const char* what)
{
    std::string error;
    auto a = WsTransport::Connect(o, &error);
    auto b = a ? WsTransport::Connect(o, &error) : nullptr;
    CHECK(a && b);
    if (!a || !b)
    {
        fprintf(stderr, "  %s: %s\n", what, error.c_str());
        return;
    }
    std::vector<std::string> joinedA, leftA;
    std::vector<Message> gotA, gotB;
    a->onPeerJoined = [&](const std::string& id) { joinedA.push_back(id); };
    a->onPeerLeft = [&](const std::string& id) { leftA.push_back(id); };
    a->onMessage = [&](const std::string&, const Message& m) { gotA.push_back(m); };
    b->onMessage = [&](const std::string&, const Message& m) { gotB.push_back(m); };
    a->Join(); // (either order: "peers" names the other one as "joined" does)
    b->Join();
    CHECK(PumpUntil(a.get(), b.get(), [&] { return joinedA.size() == 1; }));
    Bytes every(256);
    for (int i = 0; i < 256; ++i)
        every[i] = static_cast<uint8_t>(i);
    std::string big(50000, 'x');
    for (size_t i = 0; i < big.size(); ++i)
        big[i] = static_cast<char>('a' + i % 26);
    Json large = Json::Object();
    large.Set("big", big);
    a->Send("", Message::Of(J(R"({"hello":"wss"})")));
    a->Send(b->Id(), Message::Of(every));
    a->Send("", Message::Of(large));
    CHECK(PumpUntil(a.get(), b.get(), [&] { return gotB.size() == 3; }));
    if (gotB.size() == 3)
    {
        CHECK_EQ(gotB[0].json["hello"].Str(), std::string("wss"));
        CHECK(gotB[1].binary && gotB[1].bytes == every);
        CHECK(gotB[2].json["big"].Str() == big);
    }
    b->Send(a->Id(), Message::Of(J(R"({"back":1})")));
    CHECK(PumpUntil(a.get(), b.get(), [&] { return gotA.size() == 1; }));
    double rtt = -1;
    a->Ping(b->Id(), [&](double ms) { rtt = ms; }, [] {});
    CHECK(PumpUntil(a.get(), b.get(), [&] { return rtt >= 0; }));
    b->Leave();
    CHECK(PumpUntil(a.get(), nullptr, [&] { return leftA.size() == 1; }));
    CHECK(a->Connected());
    a->Leave();
    CHECK(!a->Connected());
    fprintf(stderr, "  %s: ok (ping %.1f ms)\n", what, rtt);
}

// A connection that must fail, with *part in the reason.
void Refused(const ServerOptions& o, const char* part, const char* what)
{
    std::string error;
    auto t = WsTransport::Connect(o, &error);
    CHECK(!t);
    const bool said = error.find(part) != std::string::npos;
    CHECK(said);
    fprintf(stderr, "  %s: %s%s\n", what, said ? "" : "UNEXPECTED: ", error.c_str());
}

void TestWebSocket()
{
    const char* env = getenv("GTA2_NET_TEST_TLS");
    if (!env || !strchr(env, ':'))
    {
        printf("gta2-net-test: ws:// and wss:// not tried (scripts/native/test_tls.sh runs them)\n");
        return;
    }
    gTlsDir = std::string(env, strchr(env, ':'));
    gTlsBase = atoi(strchr(env, ':') + 1);
    const std::string ca = gTlsDir + "/ca.pem";
    std::string selfPrint;
    std::ifstream(gTlsDir + "/self.sha256") >> selfPrint;

    // the lobby's "connect": login and the relay, without a room
    ServerOptions plain = Relay("ws", 0);
    plain.password.clear();
    ServerCheck k = WsTransport::Check(plain);
    CHECK(!k.ok && k.needsPassword);
    fprintf(stderr, "  check, no password: %s\n", k.error.c_str());
    plain.password = "wrong";
    k = WsTransport::Check(plain);
    CHECK(!k.ok && k.needsPassword);
    plain.password = "pw";
    k = WsTransport::Check(plain);
    CHECK(k.ok && !k.cookie.empty());
    {
        // a room joined with that session, no login again
        ServerOptions withCookie = Relay("ws", 0);
        withCookie.password.clear();
        withCookie.cookie = k.cookie;
        std::string error;
        auto t = WsTransport::Connect(withCookie, &error);
        CHECK(t && t->Connected());
    }
    plain.password.clear();
    plain.invite = "test-invite";
    CHECK(WsTransport::Check(plain).ok);
    ServerOptions open = Relay("ws", 7);
    open.password.clear();
    k = WsTransport::Check(open);
    CHECK(k.ok && k.cookie.empty());
    ServerOptions secure = Relay("wss", 1);
    secure.tls.caFile = ca;
    CHECK(WsTransport::Check(secure).ok);
    ServerOptions nobody = Relay("ws", 9);
    k = WsTransport::Check(nobody);
    CHECK(!k.ok && !k.needsPassword && k.error.find("cannot connect") != std::string::npos);
    fprintf(stderr, "  check, nobody there: %s\n", k.error.c_str());
    Exchange(Relay("ws", 0), "ws:// with the password");
    ServerOptions o = Relay("wss", 1);
    o.tls.caFile = ca;
    Exchange(o, "wss:// signed by ca_file");
    o.password.clear();
    o.invite = "test-invite";
    Exchange(o, "wss:// with an invite code");
    o = Relay("https", 1, "127.0.0.1");
    o.tls.caFile = ca;
    Exchange(o, "https:// to 127.0.0.1 (an IP in the certificate)");
    o = Relay("wss", 5);
    o.tls.fingerprint = "SHA256:" + selfPrint;
    Exchange(o, "wss:// self-signed, tls_fingerprint");
    for (size_t i = 2; i < selfPrint.size(); i += 3)
        selfPrint.insert(i, ":"); // as openssl prints it, with colons
    o.tls.fingerprint = "sha256:" + selfPrint;
    Exchange(o, "wss:// tls_fingerprint with colons");

    o = Relay("wss", 1);
    o.password = "wrong";
    o.tls.caFile = ca;
    Refused(o, "wrong server password", "wrong password over TLS");
    o = Relay("wss", 1);
    o.tls.caFile = gTlsDir + "/other-ca.pem";
    Refused(o, "not from a known authority", "another authority");
    o.tls.caFile = gTlsDir + "/none.pem";
    Refused(o, "cannot read ca_file", "a ca_file that is not there");
    o = Relay("wss", 1);
    Refused(o, "not from a known authority", "the console's authorities (a test CA is not one)");
    o = Relay("wss", 2);
    o.tls.caFile = ca;
    Refused(o, "for another name", "a certificate for another name");
    o = Relay("wss", 3);
    o.tls.caFile = ca;
    Refused(o, "out of date", "an expired certificate");
    o = Relay("wss", 4);
    o.tls.caFile = ca;
    Refused(o, "not valid yet", "a certificate not valid yet");
    o = Relay("wss", 5);
    o.tls.caFile = ca;
    Refused(o, "not from a known authority", "self-signed without tls_fingerprint");
    o.tls.fingerprint = "sha256:" + std::string(64, '0');
    Refused(o, "does not match tls_fingerprint", "self-signed, another tls_fingerprint");
    o.tls.fingerprint = "sha256:1234";
    Refused(o, "is not sha256: and 64 hex", "a tls_fingerprint too short");
    o = Relay("wss", 0);
    o.tls.caFile = ca;
    Refused(o, "TLS", "wss:// to a ws:// server");
    o = Relay("ws", 1);
    Refused(o, "did not answer", "ws:// to a wss:// server");
    o = Relay("wss", 6);
    o.tls.caFile = ca;
    o.password.clear();
    o.timeoutMs = 1000;
    Refused(o, "timed out", "a server that never answers");
    o = Relay("wss", 0);
    o.tls.caFile = ca;
    o.password.clear();
    o.timeoutMs = 1000;
    Refused(o, "is the server in TLS", "wss:// to a ws:// server without login");
}
} // namespace

int main()
{
    TestJson();
    TestRules();
    TestLobby();
    TestLeaveBeforeStart();
    TestBrowserRoster();
    TestVersion();
    TestNetConfig();
    TestLan();
    TestWebSocket();
    printf("gta2-net-test: %d checks, %d failed\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
