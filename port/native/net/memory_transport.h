// Transports for tests: every player of a MemoryHub lives in the same process, as the
// browser's LocalTransport links tabs of one browser. Messages are copied (JSON through
// its text, as a real transport would) and delivered on the receiver's Poll().
#pragma once

#include <deque>
#include <map>
#include <memory>

#include "transport.h"

namespace gta2net
{

class MemoryTransport;

class MemoryHub
{
  public:
    // A new player for this room (not joined yet).
    std::unique_ptr<MemoryTransport> Connect();
    // Delivery delay in ms (0: next Poll).
    double latencyMs = 0;

  private:
    friend class MemoryTransport;
    std::map<std::string, MemoryTransport*> mJoined;
};

class MemoryTransport : public Transport
{
  public:
    ~MemoryTransport() override;
    void Join() override;
    void Send(const std::string& to, const Message& m) override;
    void Ping(const std::string& id, std::function<void(double)> ok, std::function<void()> fail) override;
    void Leave() override;
    void Poll() override;

  private:
    friend class MemoryHub;
    explicit MemoryTransport(MemoryHub* hub);
    struct Event
    {
        enum Kind
        {
            kJoined,
            kLeft,
            kMessage,
            kPong
        } kind;
        std::string from;
        Message message;
        double at;
        std::function<void()> pong;
    };
    void Deliver(Event e);

    MemoryHub* mHub;
    bool mJoinedRoom = false;
    std::deque<Event> mInbox;
};

} // namespace gta2net
