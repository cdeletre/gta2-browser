// How the co-op lobby reaches the other players: the native counterpart of the transports
// in web/net.js (one interface: id, join(), send(), leave(), onMessage, onPeerJoined,
// onPeerLeft, ping). A transport moves small messages between the players in a room,
// reliably and in order. Lobby messages are JSON, game messages binary (bytes for
// gta2_port_net_receive), exactly as in the browser, so a native player and a browser can
// share a room through the server's relay.
//
// Single-threaded: Poll() is called once per main-loop iteration and runs the callbacks.
// Multiplayer build only (gta2-online).
#pragma once

#include <stdint.h>

#include <functional>
#include <string>
#include <vector>

#include "json.h"

namespace gta2net
{

using Bytes = std::vector<uint8_t>;

// A message: JSON (lobby) or bytes (game).
struct Message
{
    bool binary = false;
    Json json;
    Bytes bytes;

    static Message Of(Json j)
    {
        Message m;
        m.json = std::move(j);
        return m;
    }
    static Message Of(Bytes b)
    {
        Message m;
        m.binary = true;
        m.bytes = std::move(b);
        return m;
    }
};

class Transport
{
  public:
    virtual ~Transport() = default;

    // This player's id in the room (random, 12 hex digits as in the browser).
    const std::string& Id() const { return mId; }
    // Enters the room: the others get onPeerJoined(our id), we get one per player already there.
    virtual void Join() = 0;
    // To one player, or to everyone else when `to` is empty.
    virtual void Send(const std::string& to, const Message& m) = 0;
    // Round trip to a player in milliseconds; fail() after 5 s without an answer.
    virtual void Ping(const std::string& id, std::function<void(double)> ok, std::function<void()> fail) = 0;
    virtual void Leave() = 0;
    // Runs the callbacks for whatever arrived since the last call.
    virtual void Poll() = 0;
    // False once the connection is gone for good.
    virtual bool Connected() const { return true; }

    std::function<void(const std::string& from, const Message&)> onMessage;
    std::function<void(const std::string& id)> onPeerJoined;
    std::function<void(const std::string& id)> onPeerLeft;
    // The room is full, or our id is already taken (the relay's "full" / "taken").
    std::function<void(const std::string& why)> onRefused;

  protected:
    std::string mId;
};

// 12 random hex digits (randomId in web/net.js).
std::string RandomId();
// Milliseconds on a monotonic clock (performance.now()).
double NowMs();

} // namespace gta2net
