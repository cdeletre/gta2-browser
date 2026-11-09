// The game server's relay (server/server.mjs, /relay) over a WebSocket: the native
// ServerTransport of web/net.js, so that native players and browsers share rooms.
// ws:// only for now (no TLS). A password-protected server is joined with its password
// (POST /login) or an invite code (/?invite=...), which give the session cookie the relay
// asks for, as the browser gets it on the login page.
// Multiplayer build only (gta2-mp).
#pragma once

#include <map>
#include <memory>
#include <string>

#include "transport.h"

namespace gta2net
{

struct ServerOptions
{
    std::string url;      // ws://host[:port][/path]; http:// works too; the path defaults to /relay
    std::string room;     // the room's name (32 characters at most, as the relay keeps)
    std::string password; // the server's password, if it has one
    std::string invite;   // or the code of an invite link
    double timeoutMs = 8000;
};

class WsTransport : public Transport
{
  public:
    // Logs in if needed and opens the WebSocket; nullptr (and why in *error) on failure.
    static std::unique_ptr<WsTransport> Connect(const ServerOptions& options, std::string* error);
    ~WsTransport() override;

    void Join() override;
    void Send(const std::string& to, const Message& m) override;
    void Ping(const std::string& id, std::function<void(double)> ok, std::function<void()> fail) override;
    void Leave() override;
    void Poll() override;
    bool Connected() const override { return mFd >= 0; }

  private:
    WsTransport(int fd, std::string room, std::string leftover);
    void SendFrame(int opcode, const void* data, size_t len);
    void Flush();
    void Close(const char* why);
    void OnText(const std::string& text);
    void OnBinary(const Bytes& data);
    void PeerJoined(const std::string& id);

    int mFd;
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
};

} // namespace gta2net
