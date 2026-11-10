// Tests of the multiplayer code without the engine: the JSON, the lobby's rules and a whole
// lobby (host and two players) over in-memory transports. Run: build/native/<preset>/gta2-net-test
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "json.h"
#include "lobby.h"
#include "memory_transport.h"

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
        R"("minutes":5,"killLimit":10,"pointsTarget":0,"sides":{},"lang":"fr","future":{"a":1}},"v":3})";
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
} // namespace

int main()
{
    TestJson();
    TestRules();
    TestLobby();
    TestLeaveBeforeStart();
    TestBrowserRoster();
    TestVersion();
    printf("gta2-net-test: %d checks, %d failed\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
