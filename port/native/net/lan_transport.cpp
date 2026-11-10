// See lan_transport.h.
#include "lan_transport.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <random>

#include "lobby.h" // kCoopVersion

namespace gta2net
{

namespace
{
enum FrameType
{
    kHello = 1,   // {id, room, pin, port, name, v}
    kText = 2,    // a lobby message (JSON)
    kBinary = 3,  // a game message
    kPing = 4,    // n
    kPong = 5,    // n
    kPeers = 6,   // [{id, address}]: the members this one is connected to
    kRefused = 7, // {why}: pin, room, full
};

constexpr size_t kMaxFrame = 1u << 20;
constexpr int kMaxPeers = 11;           // 12 players a room, as the relay
constexpr double kBeaconEveryMs = 1000;
constexpr double kForgetMs = 5000;      // a room or member not heard for this long is gone
constexpr double kRedialMs = 5000;      // an address is tried again after this long

void NonBlocking(int fd)
{
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
}

bool SplitAddress(const std::string& address, sockaddr_in* out)
{
    const size_t colon = address.rfind(':');
    if (colon == std::string::npos)
        return false;
    memset(out, 0, sizeof(*out));
    out->sin_family = AF_INET;
    out->sin_port = htons(static_cast<uint16_t>(atoi(address.c_str() + colon + 1)));
    return inet_pton(AF_INET, address.substr(0, colon).c_str(), &out->sin_addr) == 1;
}

std::string IpOf(const sockaddr_in& a)
{
    char ip[INET_ADDRSTRLEN] = "";
    inet_ntop(AF_INET, &a.sin_addr, ip, sizeof(ip));
    return ip;
}
} // namespace

// ---- discovery ----

LanDiscovery::LanDiscovery(int port) : mPort(port)
{
    mFd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (mFd < 0)
        return;
    const int one = 1;
    setsockopt(mFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    setsockopt(mFd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one)); // several games on one machine (tests)
    setsockopt(mFd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons(static_cast<uint16_t>(port));
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(mFd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0)
    {
        fprintf(stderr, "gta2: lan: cannot listen for games on UDP %d: %s\n", port, strerror(errno));
        close(mFd);
        mFd = -1;
        return;
    }
    NonBlocking(mFd);
}

LanDiscovery::~LanDiscovery()
{
    if (mFd >= 0)
        close(mFd);
}

void LanDiscovery::Announce(const Json& beacon)
{
    if (mFd < 0)
        return;
    const std::string text = beacon.Dump();
    sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = htons(static_cast<uint16_t>(mPort));
    to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    sendto(mFd, text.data(), text.size(), 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
    // GTA2_LAN_LOOPBACK=1 (tests: several games on one machine whose broadcasts do not come back)
    static const bool loopback = getenv("GTA2_LAN_LOOPBACK") && atoi(getenv("GTA2_LAN_LOOPBACK")) != 0;
    if (loopback)
    {
        to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        sendto(mFd, text.data(), text.size(), 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
    }
}

void LanDiscovery::Poll()
{
    const double now = NowMs();
    for (int n = 0; mFd >= 0 && n < 64; ++n)
    {
        char buf[2048];
        sockaddr_in from = {};
        socklen_t len = sizeof(from);
        const ssize_t got = recvfrom(mFd, buf, sizeof(buf) - 1, 0, reinterpret_cast<sockaddr*>(&from), &len);
        if (got <= 0)
            break;
        Json b;
        if (!Json::Parse(std::string(buf, static_cast<size_t>(got)), &b) || !b.IsObject() || b["gta2lan"].Int() != 1)
            continue;
        const std::string roomId = b["roomId"].Str().substr(0, 64);
        const std::string id = b["id"].Str().substr(0, 64);
        const int port = b["port"].Int();
        if (roomId.empty() || id.empty() || port <= 0 || port > 65535)
            continue;
        LanGame& g = mGames[roomId];
        g.roomId = roomId;
        g.room = b["room"].Str().substr(0, 32);
        g.version = b["v"].Int();
        g.seenAt = now;
        const std::string name = b["name"].Str().substr(0, 16);
        if (b["host"].Truthy())
        {
            g.hostName = name;
            g.mode = b["mode"].Str().substr(0, 8);
            g.state = b["state"].Str().substr(0, 8);
            g.players = b["players"].Int();
        }
        // the beacon's own address (loopback: this machine; the others reach it by its own)
        std::string ip = IpOf(from);
        const std::string address = ip + ":" + std::to_string(port);
        bool known = false;
        for (LanGame::Member& m : g.members)
        {
            if (m.id == id)
            {
                // keep a real address over the loopback copy
                if (ip != "127.0.0.1" || m.address.compare(0, 10, "127.0.0.1:") == 0)
                    m.address = address;
                m.name = name;
                m.seenAt = now;
                known = true;
            }
        }
        if (!known)
            g.members.push_back({id, name, address, now});
    }
    for (auto it = mGames.begin(); it != mGames.end();)
    {
        auto& ms = it->second.members;
        ms.erase(std::remove_if(ms.begin(), ms.end(), [&](const LanGame::Member& m) { return now - m.seenAt > kForgetMs; }),
                 ms.end());
        if (ms.empty() || now - it->second.seenAt > kForgetMs)
            it = mGames.erase(it);
        else
        {
            if (it->second.players == 0)
                it->second.players = static_cast<int>(ms.size());
            ++it;
        }
    }
}

// ---- the mesh ----

struct LanTransport::Peer
{
    int fd = -1;
    bool outgoing = false;   // this player connected to it
    bool connecting = false; // the connection is not made yet
    std::string address;     // ip:port it listens on
    std::string id;          // known once its hello came
    std::string expect;      // the player this connection was opened to (from a list), if known
    bool helloSent = false;
    bool joined = false;     // both hellos done: a member of the room
    bool dead = false;       // closed; removed after the poll
    std::string in, out;
};

bool LanTransport::LinkInfo(const std::string& id, LinkStats* out) const
{
    for (const auto& p : mPeers)
    {
        if (p->id == id && p->joined && !p->dead)
            return TcpLinkInfo(p->fd, out);
    }
    return false;
}

std::unique_ptr<LanTransport> LanTransport::Create(const LanOptions& o, LanDiscovery* discovery, std::string* error)
{
    const int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
    {
        *error = std::string("cannot open a socket: ") + strerror(errno);
        return nullptr;
    }
    const int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    int port = 0;
    for (int p = o.port; p < o.port + 10 && !port; ++p)
    {
        sockaddr_in a = {};
        a.sin_family = AF_INET;
        a.sin_port = htons(static_cast<uint16_t>(p));
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0)
            port = p;
    }
    if (!port || listen(fd, 16) != 0)
    {
        *error = "cannot listen on TCP ports " + std::to_string(o.port) + "-" + std::to_string(o.port + 9);
        close(fd);
        return nullptr;
    }
    NonBlocking(fd);
    return std::unique_ptr<LanTransport>(new LanTransport(o, discovery, fd, port));
}

LanTransport::LanTransport(const LanOptions& o, LanDiscovery* discovery, int listenFd, int port)
    : mOpt(o), mDiscovery(discovery), mListen(listenFd), mPort(port)
{
    mId = RandomId();
    if (mOpt.roomId.empty() && mOpt.host)
        mOpt.roomId = mId; // a new room: this player's id
    // (a guest with no room id joins by address: it takes the room of the first that answers)
    fprintf(stderr, "gta2: lan: room \"%s\" (%s) on TCP %d%s\n", mOpt.room.c_str(), mOpt.roomId.c_str(), mPort,
            mOpt.host ? ", hosting" : "");
}

LanTransport::~LanTransport()
{
    Leave();
}

Json LanTransport::Hello() const
{
    Json h = Json::Object();
    h.Set("id", mId);
    h.Set("room", mOpt.roomId);
    h.Set("pin", mOpt.pin);
    h.Set("port", mPort);
    h.Set("name", mOpt.name);
    h.Set("v", kCoopVersion);
    return h;
}

void LanTransport::Join()
{
    mJoined = true;
    for (const std::string& s : mOpt.seeds)
        Dial(s);
    mLastBeacon = 0; // the first beacon on the next poll
}

void LanTransport::Dial(const std::string& address, const std::string& expect)
{
    const double now = NowMs();
    auto when = mDialed.find(address);
    if (when != mDialed.end() && now - when->second < kRedialMs)
        return;
    for (const auto& p : mPeers)
    {
        if (!p->dead && p->address == address)
            return;
    }
    sockaddr_in a;
    if (!SplitAddress(address, &a) || static_cast<int>(mPeers.size()) >= kMaxPeers * 2)
        return;
    mDialed[address] = now;
    const int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return;
    NonBlocking(fd);
    const int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    if (connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 && errno != EINPROGRESS)
    {
        close(fd);
        return;
    }
    auto p = std::make_unique<Peer>();
    p->fd = fd;
    p->outgoing = true;
    p->connecting = true;
    p->address = address;
    p->expect = expect;
    mPeers.push_back(std::move(p));
}

void LanTransport::Accept()
{
    for (;;)
    {
        sockaddr_in from = {};
        socklen_t len = sizeof(from);
        const int fd = accept4(mListen, reinterpret_cast<sockaddr*>(&from), &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0)
            return;
        const int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        auto p = std::make_unique<Peer>();
        p->fd = fd;
        p->address = IpOf(from); // its port comes with its hello
        mPeers.push_back(std::move(p));
    }
}

void LanTransport::SendFrame(Peer& p, int type, const std::string& payload)
{
    if (p.dead)
        return;
    const uint32_t n = static_cast<uint32_t>(payload.size() + 1);
    const char head[5] = {static_cast<char>(n >> 24), static_cast<char>(n >> 16), static_cast<char>(n >> 8),
                          static_cast<char>(n), static_cast<char>(type)};
    p.out.append(head, 5);
    p.out += payload;
    if (!p.connecting)
    {
        // what the socket takes now; the rest on the next poll
        while (!p.out.empty())
        {
            const ssize_t w = send(p.fd, p.out.data(), p.out.size(), MSG_NOSIGNAL);
            if (w <= 0)
                break;
            p.out.erase(0, static_cast<size_t>(w));
        }
    }
}

void LanTransport::Drop(Peer& p, const char* why)
{
    if (p.dead)
        return;
    p.dead = true;
    close(p.fd);
    p.fd = -1;
    const std::string who = p.id.empty() ? p.expect : p.id;
    if (who.empty() || !mMembers.count(who))
        return;
    // gone for the lobby only once no connection to it stays: two made at once (one closed by
    // the other side), or one still being made to it, are no departure
    for (const auto& o : mPeers)
    {
        if (o.get() != &p && !o->dead && (o->id == who || (o->id.empty() && o->expect == who)))
            return;
    }
    mMembers.erase(who);
    fprintf(stderr, "gta2: lan: %s left (%s)\n", who.c_str(), why);
    if (onPeerLeft)
        onPeerLeft(who);
}

void LanTransport::SendPeers(Peer& to)
{
    Json list = Json::Array();
    for (const auto& p : mPeers)
    {
        if (p.get() != &to && !p->dead && p->joined)
        {
            Json e = Json::Object();
            e.Set("id", p->id);
            e.Set("address", p->address);
            list.Push(e);
        }
    }
    SendFrame(to, kPeers, list.Dump());
}

void LanTransport::Handle(Peer& p, int type, const std::string& payload)
{
    if (type == kHello)
    {
        Json h;
        if (!Json::Parse(payload, &h) || !h.IsObject())
            return Drop(p, "bad hello");
        const std::string id = h["id"].Str().substr(0, 64);
        const auto refuse = [&](const char* why) {
            Json r = Json::Object();
            r.Set("why", why);
            SendFrame(p, kRefused, r.Dump());
            Drop(p, why);
        };
        if (p.outgoing && mOpt.roomId.empty() && !h["room"].Str().empty())
        {
            mOpt.roomId = h["room"].Str().substr(0, 64); // joined by address: its room
            fprintf(stderr, "gta2: lan: joining room %s\n", mOpt.roomId.c_str());
        }
        // a guest joining by address does not know the room yet (it sends none)
        if (h["room"].Str() != mOpt.roomId && !(!p.outgoing && h["room"].Str().empty()))
            return refuse("room");
        if (h["pin"].Str() != mOpt.pin)
            return refuse("pin");
        if (id.empty() || id == mId)
            return Drop(p, "itself");
        int members = 0;
        for (const auto& o : mPeers)
            members += !o->dead && o->joined && o->id != id;
        if (members >= kMaxPeers)
            return refuse("full");
        if (!p.outgoing)
            p.address = p.address + ":" + std::to_string(h["port"].Int());
        p.id = id;
        // two connections between the same two players: the one made by the lower id stays
        for (const auto& o : mPeers)
        {
            if (o.get() == &p || o->dead || o->id != id)
                continue;
            const std::string& pDialer = p.outgoing ? mId : id;
            const bool keepP = pDialer == std::min(mId, id);
            Peer& loser = keepP ? *o : p;
            Peer& winner = keepP ? p : *o;
            winner.joined = winner.joined || loser.joined; // still a member: no left/joined for the swap
            loser.joined = false;
            Drop(loser, "duplicate"); // (the winner stays: no departure)
            if (&loser == &p)
                return;
        }
        if (!p.outgoing && !p.helloSent)
        {
            SendFrame(p, kHello, Hello().Dump());
            p.helloSent = true;
        }
        if (!p.outgoing)
            SendPeers(p);
        p.joined = true;
        if (!mMembers.count(id))
        {
            mMembers.insert(id);
            fprintf(stderr, "gta2: lan: %s (%s) joined, at %s\n", h["name"].Str().c_str(), id.c_str(), p.address.c_str());
            if (onPeerJoined)
                onPeerJoined(id);
        }
        return;
    }
    if (!p.joined)
    {
        if (type == kRefused)
        {
            Json r;
            Json::Parse(payload, &r);
            const std::string why = r["why"].Str();
            Drop(p, "refused");
            if (onRefused)
                onRefused(why == "pin" ? "pin" : why == "full" ? "full" : "room");
        }
        return;
    }
    if (type == kText)
    {
        Json data;
        if (Json::Parse(payload, &data) && onMessage)
            onMessage(p.id, Message::Of(data));
    }
    else if (type == kBinary)
    {
        if (onMessage)
            onMessage(p.id, Message::Of(Bytes(payload.begin(), payload.end())));
    }
    else if (type == kPing)
    {
        SendFrame(p, kPong, payload);
    }
    else if (type == kPong)
    {
        auto it = mPings.find(payload);
        if (it != mPings.end())
        {
            PendingPing ping = std::move(it->second);
            mPings.erase(it);
            if (ping.ok)
                ping.ok(NowMs() - ping.start);
        }
    }
    else if (type == kPeers)
    {
        Json list;
        if (!Json::Parse(payload, &list))
            return;
        for (const Json& e : list.Items())
        {
            bool have = e["id"].Str() == mId;
            for (const auto& o : mPeers)
                have = have || (!o->dead && o->id == e["id"].Str());
            if (!have)
                Dial(e["address"].Str(), e["id"].Str());
        }
    }
}

void LanTransport::Send(const std::string& to, const Message& m)
{
    if (to == "@server")
        return; // the game server's (the live score): there is none here
    const std::string payload = m.binary ? std::string(m.bytes.begin(), m.bytes.end()) : m.json.Dump();
    for (const auto& p : mPeers)
    {
        if (!p->dead && p->joined && (to.empty() || p->id == to))
            SendFrame(*p, m.binary ? kBinary : kText, payload);
    }
}

void LanTransport::Ping(const std::string& id, std::function<void(double)> ok, std::function<void()> fail)
{
    static std::mt19937 rng(std::random_device{}());
    const std::string n = std::to_string(rng());
    for (const auto& p : mPeers)
    {
        if (!p->dead && p->joined && p->id == id)
        {
            mPings[n] = {NowMs(), std::move(ok), std::move(fail)};
            SendFrame(*p, kPing, n);
            return;
        }
    }
    if (fail)
        fail();
}

void LanTransport::Leave()
{
    for (auto& p : mPeers)
    {
        if (!p->dead && p->fd >= 0)
        {
            // what is waiting, then closed (no left callbacks: this player is the one leaving)
            if (!p->connecting && !p->out.empty())
                send(p->fd, p->out.data(), p->out.size(), MSG_NOSIGNAL);
            close(p->fd);
        }
    }
    mPeers.clear();
    mMembers.clear();
    if (mListen >= 0)
        close(mListen);
    mListen = -1;
    mJoined = false;
}

void LanTransport::Poll()
{
    if (mListen < 0)
        return;
    const double now = NowMs();
    // the beacon, and the room's members heard but not connected yet
    if (mJoined && mDiscovery && !mOpt.roomId.empty() && now - mLastBeacon >= kBeaconEveryMs)
    {
        mLastBeacon = now;
        Json b = Json::Object();
        b.Set("gta2lan", 1);
        b.Set("v", kCoopVersion);
        b.Set("id", mId);
        b.Set("name", mOpt.name);
        b.Set("room", mOpt.room);
        b.Set("roomId", mOpt.roomId);
        b.Set("port", mPort);
        b.Set("host", mOpt.host);
        if (mOpt.host && beaconInfo)
        {
            const Json info = beaconInfo();
            for (const auto& [k, v] : info.Fields())
                b.Set(k, v);
        }
        mDiscovery->Announce(b);
        mDiscovery->Poll();
        auto g = mDiscovery->Games().find(mOpt.roomId);
        if (g != mDiscovery->Games().end())
        {
            for (const LanGame::Member& m : g->second.members)
            {
                bool have = m.id == mId;
                for (const auto& p : mPeers)
                    have = have || (!p->dead && p->id == m.id);
                if (!have)
                    Dial(m.address, m.id);
            }
        }
    }
    std::vector<pollfd> fds;
    fds.push_back({mListen, POLLIN, 0});
    for (const auto& p : mPeers)
        fds.push_back({p->fd, static_cast<short>(POLLIN | (p->connecting || !p->out.empty() ? POLLOUT : 0)), 0});
    if (poll(fds.data(), fds.size(), 0) > 0)
    {
        if (fds[0].revents & POLLIN)
            Accept();
        // (Accept may have added peers past the polled ones: they wait for the next poll)
        for (size_t i = 1; i < fds.size(); ++i)
        {
            Peer& p = *mPeers[i - 1];
            const short ev = fds[i].revents;
            if (p.dead || !ev)
                continue;
            if (p.connecting && (ev & (POLLOUT | POLLERR | POLLHUP)))
            {
                int err = 0;
                socklen_t len = sizeof(err);
                getsockopt(p.fd, SOL_SOCKET, SO_ERROR, &err, &len);
                if (err)
                {
                    Drop(p, strerror(err));
                    continue;
                }
                p.connecting = false;
                SendFrame(p, kHello, Hello().Dump());
                p.helloSent = true;
            }
            if (!p.out.empty() && (ev & POLLOUT))
            {
                const ssize_t w = send(p.fd, p.out.data(), p.out.size(), MSG_NOSIGNAL);
                if (w > 0)
                    p.out.erase(0, static_cast<size_t>(w));
                else if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                {
                    Drop(p, strerror(errno));
                    continue;
                }
            }
            if (ev & (POLLIN | POLLHUP | POLLERR))
            {
                // read all; a close is acted on after the frames that came before it (its last
                // inputs reach the lobby before its departure)
                std::string closed;
                for (;;)
                {
                    char chunk[8192];
                    const ssize_t r = recv(p.fd, chunk, sizeof(chunk), 0);
                    if (r > 0)
                    {
                        p.in.append(chunk, static_cast<size_t>(r));
                        continue;
                    }
                    if (r == 0)
                        closed = "closed";
                    else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                        closed = strerror(errno);
                    break;
                }
                while (!p.dead && p.in.size() >= 5)
                {
                    const unsigned char* h = reinterpret_cast<const unsigned char*>(p.in.data());
                    const size_t n = (static_cast<size_t>(h[0]) << 24) | (static_cast<size_t>(h[1]) << 16) |
                                     (static_cast<size_t>(h[2]) << 8) | h[3];
                    if (n == 0 || n > kMaxFrame)
                    {
                        Drop(p, "bad frame");
                        p.in.clear();
                        break;
                    }
                    if (p.in.size() < 4 + n)
                        break;
                    const int type = h[4];
                    const std::string payload = p.in.substr(5, n - 1);
                    p.in.erase(0, 4 + n);
                    Handle(p, type, payload);
                }
                if (!closed.empty())
                    Drop(p, closed.c_str());
            }
        }
    }
    mPeers.erase(std::remove_if(mPeers.begin(), mPeers.end(), [](const std::unique_ptr<Peer>& p) { return p->dead; }),
                 mPeers.end());
    // unanswered pings
    for (auto it = mPings.begin(); it != mPings.end();)
    {
        if (now - it->second.start > 5000)
        {
            auto fail = std::move(it->second.fail);
            it = mPings.erase(it);
            if (fail)
                fail();
        }
        else
        {
            ++it;
        }
    }
}

} // namespace gta2net
