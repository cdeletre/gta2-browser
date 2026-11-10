// The game server's relay (server/server.mjs, /relay) over a WebSocket: the native
// ServerTransport of web/net.js, so that native players and browsers share rooms.
// ws:// or wss:// (TLS, tls_conn.h). A password-protected server is joined with its password
// (POST /login) or an invite code (/?invite=...), which give the session cookie the relay
// asks for, as the browser gets it on the login page.
// Multiplayer build only (gta2-online).
#pragma once

#include <deque>
#include <map>
#include <memory>
#include <string>

#include "tls_conn.h"
#include "transport.h"

namespace gta2net
{

struct ServerOptions
{
    std::string url;      // ws:// or wss://host[:port][/path] (http://, https:// too); the path defaults to /relay
    std::string room;     // the room's name (32 characters at most, as the relay keeps)
    std::string password; // the server's password, if it has one
    std::string invite;   // or the code of an invite link
    TlsOptions tls;       // wss://: the authorities, or the certificate's fingerprint
    std::string cookie;   // a session from Check() (then no login again)
    double timeoutMs = 8000;
};

// What Check() found out about a server.
struct ServerCheck
{
    bool ok = false;            // the relay lets us in (with *cookie, if it needed one)
    bool needsPassword = false; // it needs a password (none given, or a wrong one)
    std::string cookie;         // the session to join rooms with ("" if the server has no password)
    std::string error;          // why not, for the player
};

class WsTransport : public Transport
{
  public:
    // Logs in if needed and opens the WebSocket; nullptr (and why in *error) on failure.
    static std::unique_ptr<WsTransport> Connect(const ServerOptions& options, std::string* error);
    // Logs in (password or invite, if given) and asks the relay to let us in, without
    // joining a room: the lobby's "connected to a server".
    static ServerCheck Check(const ServerOptions& options);
    ~WsTransport() override;

    void Join() override;
    void Send(const std::string& to, const Message& m) override;
    void Ping(const std::string& id, std::function<void(double)> ok, std::function<void()> fail) override;
    void Leave() override;
    void Poll() override;
    bool Connected() const override { return mConn != nullptr; }
    bool LinkInfo(const std::string& id, LinkStats* out) const override;

  private:
    WsTransport(std::unique_ptr<Conn> conn, std::string room, std::string leftover);
    void SendFrame(int opcode, const void* data, size_t len);
    void Flush();
    void Close(const char* why);
    void OnText(const std::string& text);
    void OnBinary(const Bytes& data);
    void PeerJoined(const std::string& id);

    std::unique_ptr<Conn> mConn; // nullptr once closed
    std::string mRoom;
    std::string mIn;  // received bytes not yet parsed into frames
    std::string mOut; // frames not yet written
    std::string mFragments;
    int mFragmentOpcode = 0;
    std::map<std::string, bool> mPeers;
    struct PendingPing
    {
        double start;
        std::function<void(double)> ok;
        std::function<void()> fail;
    };
    std::map<std::string, PendingPing> mPings; // by n (as text)
    struct Delayed
    {
        double at;
        bool text;
        std::string data;
    };
    std::deque<Delayed> mDelayed; // received messages not handed on yet (GTA2_NET_LAG_MS)
};

} // namespace gta2net
