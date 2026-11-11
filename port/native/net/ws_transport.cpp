// See ws_transport.h. A WebSocket client (RFC 6455) small enough for the relay: text and
// binary frames, fragments, ping/pong control frames, close; over TCP or TLS (tls_conn.h).
// The connection is non-blocking once open; Poll() reads what arrived and writes what is waiting.
#include "ws_transport.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include <random>

namespace gta2net
{

namespace
{
struct Url
{
    std::string host;
    std::string port; // 80, 443 with TLS, unless given
    std::string path = "/relay";
    bool tls = false;
};

bool ParseUrl(const std::string& text, Url* out, std::string* error)
{
    std::string rest = text;
    for (const char* scheme : {"ws://", "http://", "wss://", "https://"})
    {
        if (rest.compare(0, strlen(scheme), scheme) == 0)
        {
            rest = rest.substr(strlen(scheme));
            out->tls = strcmp(scheme, "wss://") == 0 || strcmp(scheme, "https://") == 0;
            break;
        }
    }
    out->port = out->tls ? "443" : "80";
    const size_t slash = rest.find('/');
    std::string hostport = rest.substr(0, slash);
    if (slash != std::string::npos && slash + 1 < rest.size())
    {
        out->path = rest.substr(slash);
    }
    if (!hostport.empty() && hostport[0] == '[')
    {
        // [IPv6] or [IPv6]:port
        const size_t close = hostport.find(']');
        if (close == std::string::npos)
        {
            *error = "bad server address \"" + text + "\"";
            return false;
        }
        if (close + 1 < hostport.size() && hostport[close + 1] == ':')
            out->port = hostport.substr(close + 2);
        hostport = hostport.substr(1, close - 1);
    }
    else if (hostport.find(':') != std::string::npos && hostport.find(':') == hostport.rfind(':'))
    {
        out->port = hostport.substr(hostport.find(':') + 1);
        hostport = hostport.substr(0, hostport.find(':'));
    }
    out->host = hostport;
    if (out->host.empty())
    {
        *error = "no host in the server address \"" + text + "\"";
        return false;
    }
    return true;
}

bool SendAll(Conn& conn, const std::string& data, double timeoutMs)
{
    size_t done = 0;
    const double end = NowMs() + timeoutMs;
    while (done < data.size())
    {
        const long n = conn.Write(data.data() + done, data.size() - done);
        if (n > 0)
        {
            done += static_cast<size_t>(n);
        }
        else if (n == Conn::kAgain)
        {
            if (NowMs() > end || WaitFor(conn.Fd(), conn.Waits(), end - NowMs()) <= 0)
                return false;
        }
        else
        {
            return false;
        }
    }
    return true;
}

// Reads an HTTP response's head (up to the blank line); the bytes after it go to *rest.
bool ReadHead(Conn& conn, double timeoutMs, std::string* head, std::string* rest)
{
    std::string buf;
    const double end = NowMs() + timeoutMs;
    for (;;)
    {
        const size_t blank = buf.find("\r\n\r\n");
        if (blank != std::string::npos)
        {
            *head = buf.substr(0, blank + 2);
            *rest = buf.substr(blank + 4);
            return true;
        }
        if (buf.size() > 16384)
            return false;
        char chunk[2048];
        const long n = conn.Read(chunk, sizeof(chunk));
        if (n > 0)
        {
            buf.append(chunk, static_cast<size_t>(n));
        }
        else if (n == Conn::kAgain)
        {
            if (NowMs() > end || WaitFor(conn.Fd(), conn.Waits(), end - NowMs()) <= 0)
                return false;
        }
        else
        {
            return false;
        }
    }
}

int StatusOf(const std::string& head)
{
    const size_t sp = head.find(' ');
    return sp == std::string::npos ? 0 : atoi(head.c_str() + sp + 1);
}

// The value of the gta2_session cookie a response sets, or "".
std::string SessionCookie(const std::string& head)
{
    size_t at = 0;
    for (;;)
    {
        const size_t line = head.find("\r\n", at);
        if (line == std::string::npos)
            return "";
        at = line + 2;
        if (strncasecmp(head.c_str() + at, "set-cookie:", 11) != 0)
            continue;
        const size_t name = head.find("gta2_session=", at);
        const size_t lineEnd = head.find("\r\n", at);
        if (name == std::string::npos || name > lineEnd)
            continue;
        const size_t value = name + strlen("gta2_session=");
        const size_t semi = head.find_first_of(";\r", value);
        return head.substr(value, semi - value);
    }
}

std::string UrlEncode(const std::string& s)
{
    std::string out;
    for (unsigned char c : s)
    {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
        {
            out += static_cast<char>(c);
        }
        else
        {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

std::string HostHeader(const Url& url)
{
    const bool v6 = url.host.find(':') != std::string::npos;
    std::string h = v6 ? "[" + url.host + "]" : url.host;
    return url.port == (url.tls ? "443" : "80") ? h : h + ":" + url.port;
}

// One HTTP exchange on its own connection, for the session cookie.
bool Login(const Url& url, const ServerOptions& o, std::string* cookie, std::string* error)
{
    std::string request;
    if (!o.invite.empty())
    {
        request = "GET /?invite=" + UrlEncode(o.invite) + " HTTP/1.1\r\nHost: " + HostHeader(url) +
            "\r\nConnection: close\r\n\r\n";
    }
    else
    {
        const std::string body = "password=" + UrlEncode(o.password);
        request = "POST /login HTTP/1.1\r\nHost: " + HostHeader(url) +
            "\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: " + std::to_string(body.size()) +
            "\r\nConnection: close\r\n\r\n" + body;
    }
    std::unique_ptr<Conn> conn = Conn::Open(url.host, url.port, url.tls, o.tls, o.timeoutMs, error);
    if (!conn)
        return false;
    std::string head;
    std::string rest;
    const bool ok = SendAll(*conn, request, o.timeoutMs) && ReadHead(*conn, o.timeoutMs, &head, &rest);
    conn.reset();
    if (!ok)
    {
        *error = "the server did not answer the login";
        return false;
    }
    *cookie = SessionCookie(head);
    if (cookie->empty())
    {
        *error = o.invite.empty() ? "wrong server password" : "invalid invite code";
        return false;
    }
    return true;
}

std::string WebSocketKey()
{
    static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::random_device rd;
    unsigned char raw[16];
    for (unsigned char& b : raw)
        b = static_cast<unsigned char>(rd());
    std::string out;
    for (int i = 0; i < 16; i += 3)
    {
        const unsigned v = (raw[i] << 16) | ((i + 1 < 16 ? raw[i + 1] : 0) << 8) | (i + 2 < 16 ? raw[i + 2] : 0);
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
        out += i + 1 < 16 ? kB64[(v >> 6) & 63] : '=';
        out += i + 2 < 16 ? kB64[v & 63] : '=';
    }
    return out;
}
// The relay's WebSocket with the session cookie: the connection (status 101), or null with the
// status (403: no session, or a wrong one) and why.
std::unique_ptr<Conn> OpenRelay(const Url& url, const ServerOptions& o, const std::string& cookie, int* status,
                                std::string* rest, std::string* error)
{
    *status = 0;
    std::unique_ptr<Conn> conn = Conn::Open(url.host, url.port, url.tls, o.tls, o.timeoutMs, error);
    if (!conn)
        return nullptr;
    const std::string request = "GET " + url.path + " HTTP/1.1\r\nHost: " + HostHeader(url) +
        "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + WebSocketKey() +
        "\r\nSec-WebSocket-Version: 13\r\n" + (cookie.empty() ? "" : "Cookie: gta2_session=" + cookie + "\r\n") + "\r\n";
    std::string head;
    if (!SendAll(*conn, request, o.timeoutMs) || !ReadHead(*conn, o.timeoutMs, &head, rest))
    {
        *error = "the server did not answer the WebSocket request";
        return nullptr;
    }
    *status = StatusOf(head);
    if (*status != 101)
    {
        *error = *status == 403 ? "the server refused the relay (403: a password is needed?)"
                                : "the server answered " + std::to_string(*status) + " instead of a WebSocket";
        return nullptr;
    }
    return conn;
}
} // namespace

std::unique_ptr<WsTransport> WsTransport::Connect(const ServerOptions& o, std::string* error)
{
    Url url;
    if (!ParseUrl(o.url, &url, error))
        return nullptr;
    std::string cookie = o.cookie;
    if (cookie.empty() && (!o.password.empty() || !o.invite.empty()) && !Login(url, o, &cookie, error))
        return nullptr;
    int status = 0;
    std::string rest;
    std::unique_ptr<Conn> conn = OpenRelay(url, o, cookie, &status, &rest, error);
    if (!conn)
        return nullptr;
    return std::unique_ptr<WsTransport>(new WsTransport(std::move(conn), o.room.substr(0, 32), rest));
}

ServerCheck WsTransport::Check(const ServerOptions& o)
{
    ServerCheck r;
    Url url;
    if (!ParseUrl(o.url, &url, &r.error))
        return r;
    if (!o.password.empty() || !o.invite.empty())
    {
        if (!Login(url, o, &r.cookie, &r.error))
        {
            // a wrong password (or invite): ask for it; else the server could not be reached
            r.needsPassword = r.error == "wrong server password" || r.error == "invalid invite code";
            return r;
        }
    }
    int status = 0;
    std::string rest;
    std::unique_ptr<Conn> conn = OpenRelay(url, o, r.cookie, &status, &rest, &r.error);
    if (!conn)
    {
        if (status == 403)
        {
            r.needsPassword = true;
            r.error = "This server needs a password.";
        }
        r.cookie.clear();
        return r;
    }
    // in: the relay drops a connection that never joined a room when it closes
    r.ok = true;
    return r;
}

WsTransport::WsTransport(std::unique_ptr<Conn> conn, std::string room, std::string leftover)
    : mConn(std::move(conn)), mRoom(std::move(room)), mIn(std::move(leftover))
{
    mId = RandomId();
}

WsTransport::~WsTransport()
{
    Leave();
}

void WsTransport::SendFrame(int opcode, const void* data, size_t len)
{
    if (!mConn)
        return;
    static std::mt19937 rng(std::random_device{}());
    std::string f;
    f += static_cast<char>(0x80 | opcode);
    if (len < 126)
    {
        f += static_cast<char>(0x80 | len);
    }
    else if (len < 65536)
    {
        f += static_cast<char>(0x80 | 126);
        f += static_cast<char>(len >> 8);
        f += static_cast<char>(len & 0xFF);
    }
    else
    {
        f += static_cast<char>(0x80 | 127);
        for (int s = 56; s >= 0; s -= 8)
            f += static_cast<char>((static_cast<uint64_t>(len) >> s) & 0xFF);
    }
    unsigned char mask[4];
    const uint32_t m = static_cast<uint32_t>(rng());
    memcpy(mask, &m, 4);
    f.append(reinterpret_cast<const char*>(mask), 4);
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < len; ++i)
        f += static_cast<char>(p[i] ^ mask[i & 3]);
    mOut += f;
    Flush();
}

void WsTransport::Flush()
{
    while (mConn && !mOut.empty())
    {
        const long n = mConn->Write(mOut.data(), mOut.size());
        if (n > 0)
        {
            mOut.erase(0, static_cast<size_t>(n));
        }
        else if (n == Conn::kAgain)
        {
            return; // the rest on the next Poll (mOut keeps the bytes, as Write asks)
        }
        else
        {
            const std::string why = mConn->Error();
            Close(why.c_str());
        }
    }
}

void WsTransport::Close(const char* why)
{
    if (!mConn)
        return;
    fprintf(stderr, "gta2: net: connection to the server closed (%s)\n", why);
    mConn.reset();
    // as ws.onclose in the browser: everyone is gone for us
    auto peers = mPeers;
    mPeers.clear();
    for (const auto& [id, joined] : peers)
    {
        if (onPeerLeft)
            onPeerLeft(id);
    }
}

void WsTransport::Join()
{
    Json m = Json::Object();
    m.Set("t", "join");
    m.Set("room", mRoom);
    m.Set("id", mId);
    const std::string text = m.Dump();
    SendFrame(1, text.data(), text.size());
}

void WsTransport::Send(const std::string& to, const Message& msg)
{
    if (msg.binary)
    {
        // [target id length, target id, payload]
        std::string out;
        out += static_cast<char>(to.size());
        out += to;
        out.append(reinterpret_cast<const char*>(msg.bytes.data()), msg.bytes.size());
        SendFrame(2, out.data(), out.size());
        return;
    }
    Json m = Json::Object();
    m.Set("t", "msg");
    m.Set("to", to.empty() ? Json() : Json(to));
    m.Set("data", msg.json);
    const std::string text = m.Dump();
    SendFrame(1, text.data(), text.size());
}

void WsTransport::Ping(const std::string& id, std::function<void(double)> ok, std::function<void()> fail)
{
    static std::mt19937 rng(std::random_device{}());
    // n is a number in the browser (Math.random()); any number comes back as it went
    const double n = std::uniform_real_distribution<double>(0, 1)(rng);
    Json m = Json::Object();
    m.Set("t", "ping");
    m.Set("to", id);
    m.Set("n", n);
    mPings[Json(n).Dump()] = {NowMs(), std::move(ok), std::move(fail)};
    const std::string text = m.Dump();
    SendFrame(1, text.data(), text.size());
}

void WsTransport::Leave()
{
    if (!mConn)
        return;
    const unsigned char code[2] = {0x03, 0xE8}; // 1000: normal closure
    SendFrame(8, code, 2);
    Flush();
    mConn.reset();
    mPeers.clear();
}

void WsTransport::PeerJoined(const std::string& id)
{
    if (mPeers.count(id))
        return;
    mPeers[id] = true;
    if (onPeerJoined)
        onPeerJoined(id);
}

void WsTransport::OnText(const std::string& text)
{
    Json m;
    if (!Json::Parse(text, &m) || !m.IsObject())
        return;
    const std::string& t = m["t"].Str();
    if (t == "peers")
    {
        for (const Json& id : m["ids"].Items())
            PeerJoined(id.Str());
    }
    else if (t == "joined")
    {
        PeerJoined(m["id"].Str());
    }
    else if (t == "left")
    {
        const std::string id = m["id"].Str();
        mPeers.erase(id);
        if (onPeerLeft)
            onPeerLeft(id);
    }
    else if (t == "msg")
    {
        if (onMessage)
            onMessage(m["from"].Str(), Message::Of(m["data"]));
    }
    else if (t == "ping")
    {
        Json pong = Json::Object();
        pong.Set("t", "pong");
        pong.Set("to", m["from"]);
        pong.Set("n", m["n"]);
        const std::string out = pong.Dump();
        SendFrame(1, out.data(), out.size());
    }
    else if (t == "pong")
    {
        auto it = mPings.find(m["n"].Dump());
        if (it != mPings.end())
        {
            PendingPing p = std::move(it->second);
            mPings.erase(it);
            if (p.ok)
                p.ok(NowMs() - p.start);
        }
    }
    else if (t == "full" || t == "taken")
    {
        if (onRefused)
            onRefused(t);
    }
}

void WsTransport::OnBinary(const Bytes& data)
{
    // [sender id length, sender id, payload]
    if (data.empty() || data.size() < 1u + data[0])
        return;
    const std::string from(reinterpret_cast<const char*>(data.data() + 1), data[0]);
    if (onMessage)
        onMessage(from, Message::Of(Bytes(data.begin() + 1 + data[0], data.end())));
}

bool WsTransport::LinkInfo(const std::string& id, LinkStats* out) const
{
    (void)id; // one connection to the server for everyone
    return mConn && TcpLinkInfo(mConn->Fd(), out);
}

void WsTransport::Poll()
{
    Flush();
    while (mConn)
    {
        char chunk[8192];
        const long n = mConn->Read(chunk, sizeof(chunk));
        if (n > 0)
        {
            mIn.append(chunk, static_cast<size_t>(n));
            continue;
        }
        if (n == 0)
        {
            Close("closed by the server");
        }
        else if (n == Conn::kError)
        {
            const std::string why = mConn->Error();
            Close(why.c_str());
        }
        break;
    }
    // frames
    for (;;)
    {
        if (mIn.size() < 2)
            break;
        const unsigned char* p = reinterpret_cast<const unsigned char*>(mIn.data());
        const bool fin = p[0] & 0x80;
        const int opcode = p[0] & 0x0F;
        const bool masked = p[1] & 0x80;
        uint64_t len = p[1] & 0x7F;
        size_t at = 2;
        if (len == 126)
        {
            if (mIn.size() < 4)
                break;
            len = (static_cast<uint64_t>(p[2]) << 8) | p[3];
            at = 4;
        }
        else if (len == 127)
        {
            if (mIn.size() < 10)
                break;
            len = 0;
            for (int i = 0; i < 8; ++i)
                len = (len << 8) | p[2 + i];
            at = 10;
        }
        if (len > 1u << 20)
        {
            Close("frame too large");
            break;
        }
        unsigned char mask[4] = {0, 0, 0, 0};
        if (masked)
        {
            if (mIn.size() < at + 4)
                break;
            memcpy(mask, p + at, 4);
            at += 4;
        }
        if (mIn.size() < at + len)
            break;
        std::string payload = mIn.substr(at, static_cast<size_t>(len));
        mIn.erase(0, at + static_cast<size_t>(len));
        if (masked)
        {
            for (size_t i = 0; i < payload.size(); ++i)
                payload[i] = static_cast<char>(payload[i] ^ mask[i & 3]);
        }
        if (opcode == 8)
        {
            Close("closed by the server");
            break;
        }
        if (opcode == 9)
        {
            SendFrame(10, payload.data(), payload.size()); // pong
            continue;
        }
        if (opcode == 10)
            continue;
        if (opcode == 1 || opcode == 2)
        {
            mFragmentOpcode = opcode;
            mFragments = payload;
        }
        else if (opcode == 0)
        {
            mFragments += payload;
        }
        if (!fin)
            continue;
        if (mFragmentOpcode == 1 || mFragmentOpcode == 2)
        {
            // GTA2_NET_LAG_MS=base[,jitter]: a slower, uneven network for tests; each message is
            // held base + up to jitter ms, never before the one ahead of it (TCP keeps the order)
            static double base = -1;
            static double jitter = 0;
            if (base < 0)
            {
                base = 0;
                if (const char* lag = getenv("GTA2_NET_LAG_MS"))
                {
                    base = atof(lag);
                    if (const char* comma = strchr(lag, ','))
                        jitter = atof(comma + 1);
                }
            }
            double at = NowMs();
            if (base > 0 || jitter > 0)
            {
                static std::mt19937 lagRng(12345);
                at += base + std::uniform_real_distribution<double>(0, jitter)(lagRng);
            }
            if (!mDelayed.empty() && mDelayed.back().at > at)
                at = mDelayed.back().at;
            mDelayed.push_back({at, mFragmentOpcode == 1, mFragments});
        }
        mFragments.clear();
    }
    while (!mDelayed.empty() && mDelayed.front().at <= NowMs())
    {
        Delayed d = std::move(mDelayed.front());
        mDelayed.pop_front();
        if (d.text)
            OnText(d.data);
        else
            OnBinary(Bytes(d.data.begin(), d.data.end()));
    }
    // unanswered pings
    const double now = NowMs();
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
