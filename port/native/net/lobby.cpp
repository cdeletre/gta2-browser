// See lobby.h. Each function says which CoopLobby method (web/net.js) it follows.
#include "lobby.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <time.h>

#include <algorithm>

namespace gta2net
{

namespace
{
const Gang kGangs[3][3] = {
    {{"zai", "Zaibatsu", "#c8c8c8"}, {"loo", "Loonies", "#3fd16a"}, {"yak", "Yakuza", "#4fa8ff"}},
    {{"zai", "Zaibatsu", "#c8c8c8"}, {"red", "Rednecks", "#ff8a3d"}, {"sci", "SRS Scientists", "#b38bff"}},
    {{"zai", "Zaibatsu", "#c8c8c8"}, {"rus", "Russians", "#ff4d4d"}, {"kri", "Hare Krishnas", "#ffd23f"}},
};
const char* const kLangs[] = {"en", "es", "fr"}; // LANGS in web/strings.js

// Number of UTF-8 code points kept: the first `max` (the browser counts UTF-16 units; the
// same for every name without characters outside the BMP).
std::string FirstCodePoints(const std::string& s, size_t max)
{
    size_t n = 0;
    size_t i = 0;
    while (i < s.size() && n < max)
    {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
        i += len;
        ++n;
    }
    return s.substr(0, std::min(i, s.size()));
}

std::string Trim(const std::string& s)
{
    size_t a = 0;
    size_t b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\n' || s[a] == '\r'))
        ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\n' || s[b - 1] == '\r'))
        --b;
    return s.substr(a, b - a);
}
} // namespace

int InputDelayFor(double rttMs)
{
    return std::min(8, std::max(1, static_cast<int>(ceil((rttMs / 2 + 10) / kTickMs))));
}

// cleanCrew: removes everything but letters, digits, space, _ . ' -. Non-ASCII characters
// are kept (taken as letters: the browser keeps letters of every script).
std::string CleanCrew(const std::string& name)
{
    std::string out;
    for (unsigned char c : name)
    {
        if (c >= 0x80 || isalnum(c) || c == ' ' || c == '_' || c == '.' || c == '\'' || c == '-')
        {
            out += static_cast<char>(c);
        }
    }
    return FirstCodePoints(Trim(out), 16);
}

const Gang* GangsOf(const std::string& district)
{
    if (district == "residential")
        return kGangs[1];
    if (district == "industrial")
        return kGangs[2];
    return kGangs[0];
}

Lobby::Lobby(Transport* transport, const std::string& name, bool host) : mT(transport), mName(name), mHost(host)
{
    if (host)
    {
        mHostId = mT->Id();
        Json me = Json::Object();
        me.Set("id", mT->Id());
        me.Set("name", name);
        mPlayers.Push(me);
    }
    mSettings = Json::Object();
    mSettings.Set("mode", "roam");
    mSettings.Set("district", "downtown");
    mSettings.Set("friendlyFire", false);
    mSettings.Set("minutes", 0);
    mSettings.Set("killLimit", 10);
    mSettings.Set("pointsTarget", 0);
    mSettings.Set("sides", Json::Object());
    mSettings.Set("lang", "en"); // the game's language for the match: the host's (English natively for now)

    mT->onPeerJoined = [this](const std::string& id) {
        if (mHost && !mStarted)
        {
            mSilent[id] = NowMs();
        }
        if (!mHost)
        {
            Json hello = Json::Object();
            hello.Set("lobby", "hello");
            hello.Set("name", mName);
            mT->Send(id, Message::Of(hello));
        }
    };
    mT->onPeerLeft = [this](const std::string& id) { Left(id); };
    mT->onMessage = [this](const std::string& from, const Message& m) { OnMessage(from, m); };
    mT->onRefused = [this](const std::string& why) {
        if (onRefused)
            onRefused(why);
    };
    mT->Join();
}

int Lobby::SlotOf(const std::string& id) const
{
    auto it = mSlots.find(id);
    return it == mSlots.end() ? -1 : it->second;
}

Json* Lobby::FindPlayer(const std::string& id)
{
    for (size_t i = 0; i < mPlayers.size(); ++i)
    {
        if (mPlayers[i]["id"].Str() == id)
        {
            return &mPlayers.At(i);
        }
    }
    return nullptr;
}

// message()
void Lobby::OnMessage(const std::string& from, const Message& m)
{
    if (m.binary)
    {
        const int slot = SlotOf(from);
        if (mSlots.empty() || slot < 0)
        {
            return;
        }
        // another player's game can start sending before ours has loaded: keep those for later
        if (!mReady)
        {
            stats.early++;
            mEarly.emplace_back(slot, m.bytes);
            return;
        }
        stats.received++;
        if (onGameMessage)
            onGameMessage(slot, m.bytes);
        return;
    }
    const Json& data = m.json;
    const std::string& kind = data["lobby"].Str();
    if (kind.empty())
    {
        return;
    }
    const int fromSlot = SlotOf(from);
    if (kind == "hello" && mHost && !mStarted)
    {
        mSilent.erase(from);
        if (!FindPlayer(from) && static_cast<int>(mPlayers.size()) < kMaxPlayers)
        {
            Json p = Json::Object();
            p.Set("id", from);
            p.Set("name", FirstCodePoints(data["name"].Truthy() ? data["name"].ToText() : "Player", 16));
            p.Set("crew", "");
            mPlayers.Push(p);
        }
        BroadcastRoster();
    }
    else if (kind == "crew" && mHost && !mStarted)
    {
        if (Json* p = FindPlayer(from))
        {
            p->Set("crew", CleanCrew(data["crew"].Truthy() ? data["crew"].ToText() : ""));
            BroadcastRoster();
        }
    }
    else if (kind == "side" && mHost && !mStarted && data["side"].IsString())
    {
        ApplySide(from, data["side"].Str());
    }
    else if (kind == "roster" && !mHost)
    {
        mHostId = from;
        mPlayers = data["players"].IsArray() ? data["players"] : Json::Array();
        if (data["settings"].Truthy())
        {
            mSettings = data["settings"];
        }
        Changed();
    }
    else if (kind == "roster" && mHost)
    {
        if (onOtherHost)
            onOtherHost(from); // another host in this room (the browser ignores it too)
    }
    else if (kind == "start" && !mHost && from == mHostId)
    {
        Begin(data["match"]);
    }
    else if (kind == "loaded" && mHost)
    {
        mLoaded.insert(from);
        CheckLoaded();
    }
    else if (kind == "go" && !mHost && from == mHostId)
    {
        if (mGo)
        {
            auto go = mGo;
            mGo = nullptr;
            go();
        }
    }
    else if (kind == "chat" && fromSlot >= 0 && data["text"].IsString())
    {
        if (onChat)
            onChat(fromSlot, FirstCodePoints(data["text"].Str(), 160));
    }
    else if (kind == "sync" && fromSlot >= 0 && data["tick"].IsInteger())
    {
        if (onSync)
            onSync(fromSlot, data["tick"].Int(), data["h"].ToText());
    }
    else if (kind == "wp" && fromSlot >= 0)
    {
        const Json& wp = data["wp"];
        const bool set = wp.IsObject() && wp["x"].IsNumber() && wp["y"].IsNumber();
        if (onWaypoint)
            onWaypoint(fromSlot, set, set ? wp["x"].Number() : 0, set ? wp["y"].Number() : 0);
    }
}

// broadcastRoster()
void Lobby::BroadcastRoster()
{
    Json msg = Json::Object();
    msg.Set("lobby", "roster");
    msg.Set("players", mPlayers);
    msg.Set("settings", mSettings);
    for (const Json& p : mPlayers.Items())
    {
        if (p["id"].Str() != mT->Id())
        {
            mT->Send(p["id"].Str(), Message::Of(msg));
        }
    }
    Changed();
}

// setCrew()
void Lobby::SetCrew(const std::string& name)
{
    const std::string crew = CleanCrew(name);
    if (mHost)
    {
        if (Json* me = FindPlayer(mT->Id()))
        {
            me->Set("crew", crew);
        }
        BroadcastRoster();
    }
    else if (!mHostId.empty())
    {
        Json msg = Json::Object();
        msg.Set("lobby", "crew");
        msg.Set("crew", crew);
        mT->Send(mHostId, Message::Of(msg));
    }
}

// setSettings()
void Lobby::SetSettings(const Json& changes)
{
    if (!mHost)
    {
        return;
    }
    for (const auto& f : changes.Fields())
    {
        mSettings.Set(f.first, f.second);
    }
    BroadcastRoster();
}

// crews(): the crews in the room, with the gang each picked (null until it picks one; never
// assigned for it). Two crews cannot hold the same gang: the first keeps it.
Json Lobby::Crews() const
{
    const Gang* gangs = GangsOf(mSettings["district"].Str());
    std::vector<std::string> names;
    for (const Json& p : mPlayers.Items())
    {
        const std::string& crew = p["crew"].Str();
        if (!crew.empty() && std::find(names.begin(), names.end(), crew) == names.end())
        {
            names.push_back(crew);
        }
    }
    std::set<std::string> taken;
    Json out = Json::Array();
    for (const std::string& name : names)
    {
        std::string side = mSettings["sides"][name].Str();
        const Gang* gang = nullptr;
        for (int g = 0; g < 3 && !side.empty(); ++g)
        {
            gang = side == gangs[g].code ? &gangs[g] : gang;
        }
        if (!side.empty() && (!gang || taken.count(side)))
        {
            side.clear();
            gang = nullptr;
        }
        if (!side.empty())
        {
            taken.insert(side);
        }
        Json c = Json::Object();
        c.Set("name", name);
        c.Set("side", side.empty() ? Json() : Json(side));
        c.Set("gang", gang ? Json(gang->name) : Json());
        c.Set("colour", gang ? gang->colour : "#8a8a8a");
        Json members = Json::Array();
        for (const Json& p : mPlayers.Items())
        {
            if (p["crew"].Str() == name)
            {
                members.Push(p["id"]);
            }
        }
        c.Set("members", members);
        out.Push(c);
    }
    return out;
}

// pickSide()
void Lobby::PickSide(const std::string& side)
{
    Json* me = FindPlayer(mT->Id());
    if (!me || (*me)["crew"].Str().empty())
    {
        return;
    }
    if (mHost)
    {
        ApplySide(mT->Id(), side);
    }
    else if (!mHostId.empty())
    {
        Json msg = Json::Object();
        msg.Set("lobby", "side");
        msg.Set("side", side);
        mT->Send(mHostId, Message::Of(msg));
    }
}

// applySide(): a crew member's choice, if the gang is in this district and no other crew has it.
void Lobby::ApplySide(const std::string& from, const std::string& side)
{
    Json* p = FindPlayer(from);
    if (!p || (*p)["crew"].Str().empty())
    {
        return;
    }
    const std::string crew = (*p)["crew"].Str();
    const Gang* gangs = GangsOf(mSettings["district"].Str());
    bool valid = side.empty();
    for (int g = 0; g < 3; ++g)
    {
        valid = valid || side == gangs[g].code;
    }
    bool heldByOther = false;
    const Json crews = Crews(); // kept: a range-for over Crews().Items() would read a destroyed temporary
    for (const Json& c : crews.Items())
    {
        heldByOther = heldByOther || (c["name"].Str() != crew && c["side"].Str() == side && c["side"].IsString());
    }
    if (!valid || (!side.empty() && heldByOther))
    {
        return;
    }
    Json sides = mSettings["sides"].IsObject() ? mSettings["sides"] : Json::Object();
    if (side.empty())
    {
        sides.Erase(crew); // {[crew]: undefined} drops the key in JSON
    }
    else
    {
        sides.Set(crew, side);
    }
    Json changes = Json::Object();
    changes.Set("sides", sides);
    SetSettings(changes);
}

// startProblem()
std::string Lobby::StartProblem() const
{
    if (mPlayers.size() < 2)
        return "waitingPlayer";
    if (mSettings["mode"].Str() != "wars")
        return "";
    for (const Json& p : mPlayers.Items())
    {
        if (p["crew"].Str().empty())
            return "needCrew";
    }
    const Json crews = Crews();
    if (crews.size() < 2)
        return "twoCrews";
    if (crews.size() > 3)
        return "threeCrews";
    std::string unpicked;
    for (const Json& c : crews.Items())
    {
        if (!c["side"].IsString())
        {
            unpicked += (unpicked.empty() ? "" : " and ") + c["name"].Str();
        }
    }
    return unpicked.empty() ? "" : "waitingGang:" + unpicked;
}

int Lobby::SilentPeers(double ms) const
{
    int n = 0;
    const double now = NowMs();
    for (const auto& [id, at] : mSilent)
    {
        n += now - at > ms ? 1 : 0;
    }
    return n;
}

// left()
void Lobby::Left(const std::string& id)
{
    mSilent.erase(id);
    const int slot = SlotOf(id);
    if (slot >= 0)
    {
        if (onPlayerLeft)
            onPlayerLeft(slot);
        mGone.insert(id);
        CheckLoaded();
        return;
    }
    Json kept = Json::Array();
    for (const Json& p : mPlayers.Items())
    {
        if (p["id"].Str() != id)
        {
            kept.Push(p);
        }
    }
    mPlayers = kept;
    if (mHost)
        BroadcastRoster();
    else
        Changed();
}

// measurePings(): the roster goes out once every answer (or failure) is in.
void Lobby::MeasurePings()
{
    if (!mHost || mPingsOut > 0)
    {
        return;
    }
    std::vector<std::string> ids;
    for (const Json& p : mPlayers.Items())
    {
        if (p["id"].Str() != mT->Id())
        {
            ids.push_back(p["id"].Str());
        }
    }
    if (ids.empty())
    {
        return;
    }
    mPingsOut = static_cast<int>(ids.size());
    auto done = [this] {
        if (--mPingsOut == 0 && !mStarted)
        {
            BroadcastRoster();
        }
    };
    for (const std::string& id : ids)
    {
        mT->Ping(
            id,
            [this, id, done](double ms) {
                if (Json* p = FindPlayer(id))
                {
                    p->Set("ping", static_cast<int>(lround(ms)));
                }
                done();
            },
            done);
    }
}

// start()
void Lobby::Start()
{
    if (!mHost || !StartProblem().empty())
    {
        return;
    }
    double slowest = 0;
    for (const Json& p : mPlayers.Items())
    {
        slowest = std::max(slowest, p["ping"].Number(0));
    }
    Json match = mSettings;
    char stamp[16];
    snprintf(stamp, sizeof(stamp), "%lx", static_cast<long>(time(nullptr)));
    match.Set("id", RandomId() + stamp);
    match.Set("players", mPlayers);
    match.Set("delay", InputDelayFor(slowest));
    match.Set("crews", mSettings["mode"].Str() == "wars" ? Crews() : Json::Array());
    Json msg = Json::Object();
    msg.Set("lobby", "start");
    msg.Set("match", match);
    for (const Json& p : mPlayers.Items())
    {
        if (p["id"].Str() != mT->Id())
        {
            mT->Send(p["id"].Str(), Message::Of(msg));
        }
    }
    Begin(match);
}

// begin()
void Lobby::Begin(Json match)
{
    bool known = false;
    for (const char* l : kLangs)
    {
        known = known || match["lang"].Str() == l;
    }
    if (!known)
    {
        match.Set("lang", "en"); // the same fallback for everyone
    }
    mStarted = true;
    mPlayers = match["players"].IsArray() ? match["players"] : Json::Array();
    mSlots.clear();
    for (size_t i = 0; i < mPlayers.size(); ++i)
    {
        mSlots[mPlayers[i]["id"].Str()] = static_cast<int>(i);
    }
    mMySlot = SlotOf(mT->Id());
    match.Set("mySlot", mMySlot);
    match.Set("delay", match["delay"].Truthy() ? match["delay"].Int() : 1);
    if (onStart)
        onStart(match);
}

// allLoaded(): every player enters the match together (the game gives up on a player who
// is not there within about 25 seconds, and loading speeds differ a lot).
void Lobby::AllLoaded(std::function<void()> go)
{
    mGo = std::move(go);
    if (mHost)
    {
        mLoaded.insert(mT->Id());
        CheckLoaded();
    }
    else
    {
        Json msg = Json::Object();
        msg.Set("lobby", "loaded");
        mT->Send(mHostId, Message::Of(msg));
    }
}

// checkLoaded()
void Lobby::CheckLoaded()
{
    if (!mHost || mSlots.empty() || !mLoaded.count(mT->Id()))
    {
        return;
    }
    for (const Json& p : mPlayers.Items())
    {
        const std::string& id = p["id"].Str();
        if (!mLoaded.count(id) && !mGone.count(id))
        {
            return;
        }
    }
    Json msg = Json::Object();
    msg.Set("lobby", "go");
    for (const Json& p : mPlayers.Items())
    {
        const std::string& id = p["id"].Str();
        if (id != mT->Id() && !mGone.count(id))
        {
            mT->Send(id, Message::Of(msg));
        }
    }
    if (mGo)
    {
        auto go = mGo;
        mGo = nullptr;
        go();
    }
}

// gameReady()
void Lobby::GameReady()
{
    mReady = true;
    std::vector<std::pair<int, Bytes>> early;
    early.swap(mEarly);
    for (auto& [slot, bytes] : early)
    {
        stats.received++;
        if (onGameMessage)
            onGameMessage(slot, bytes);
    }
}

void Lobby::SendChat(const std::string& text)
{
    Json msg = Json::Object();
    msg.Set("lobby", "chat");
    msg.Set("text", text);
    for (const auto& [id, slot] : mSlots)
    {
        if (id != mT->Id())
            mT->Send(id, Message::Of(msg));
    }
}

void Lobby::ShareSync(int tick, const std::string& h)
{
    Json msg = Json::Object();
    msg.Set("lobby", "sync");
    msg.Set("tick", tick);
    msg.Set("h", h);
    for (const auto& [id, slot] : mSlots)
    {
        if (id != mT->Id())
            mT->Send(id, Message::Of(msg));
    }
}

void Lobby::ShareWaypoint(bool set, double x, double y)
{
    Json msg = Json::Object();
    msg.Set("lobby", "wp");
    if (set)
    {
        Json wp = Json::Object();
        wp.Set("x", x);
        wp.Set("y", y);
        msg.Set("wp", wp);
    }
    else
    {
        msg.Set("wp", Json());
    }
    for (const auto& [id, slot] : mSlots)
    {
        if (id != mT->Id())
            mT->Send(id, Message::Of(msg));
    }
}

// sendGame()
void Lobby::SendGame(int slot, const Bytes& bytes)
{
    for (const auto& [id, s] : mSlots)
    {
        if (s == mMySlot || (slot >= 0 && s != slot))
        {
            continue;
        }
        mT->Send(id, Message::Of(bytes));
        stats.sent++;
    }
}

} // namespace gta2net
