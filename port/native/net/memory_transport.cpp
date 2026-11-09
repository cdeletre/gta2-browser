// See memory_transport.h.
#include "memory_transport.h"

#include <stdio.h>
#include <time.h>

#include <random>

namespace gta2net
{

std::string RandomId()
{
    static std::random_device device;
    static std::mt19937 engine(device());
    std::uniform_int_distribution<int> byte(0, 255);
    char out[13];
    for (int i = 0; i < 6; ++i)
    {
        snprintf(out + 2 * i, 3, "%02x", byte(engine));
    }
    return out;
}

double NowMs()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

std::unique_ptr<MemoryTransport> MemoryHub::Connect()
{
    return std::unique_ptr<MemoryTransport>(new MemoryTransport(this));
}

MemoryTransport::MemoryTransport(MemoryHub* hub) : mHub(hub)
{
    mId = RandomId();
}

MemoryTransport::~MemoryTransport()
{
    Leave();
}

void MemoryTransport::Deliver(Event e)
{
    e.at = NowMs() + mHub->latencyMs;
    // JSON goes through its text, as over a real connection
    if (e.kind == Event::kMessage && !e.message.binary)
    {
        Json copy;
        Json::Parse(e.message.json.Dump(), &copy);
        e.message.json = copy;
    }
    mInbox.push_back(std::move(e));
}

void MemoryTransport::Join()
{
    if (mJoinedRoom)
    {
        return;
    }
    mJoinedRoom = true;
    for (auto& [id, peer] : mHub->mJoined)
    {
        Deliver({Event::kJoined, id, {}, 0, nullptr});
        peer->Deliver({Event::kJoined, mId, {}, 0, nullptr});
    }
    mHub->mJoined[mId] = this;
}

void MemoryTransport::Send(const std::string& to, const Message& m)
{
    if (!mJoinedRoom)
    {
        return;
    }
    for (auto& [id, peer] : mHub->mJoined)
    {
        if (id != mId && (to.empty() || to == id))
        {
            peer->Deliver({Event::kMessage, mId, m, 0, nullptr});
        }
    }
}

void MemoryTransport::Ping(const std::string& id, std::function<void(double)> ok, std::function<void()> fail)
{
    auto it = mHub->mJoined.find(id);
    if (!mJoinedRoom || it == mHub->mJoined.end())
    {
        if (fail)
        {
            fail();
        }
        return;
    }
    const double start = NowMs();
    // the answer comes back after a round trip (twice the latency)
    Event e{Event::kPong, id, {}, 0, [ok, start] {
                if (ok)
                {
                    ok(NowMs() - start);
                }
            }};
    e.at = start + 2 * mHub->latencyMs;
    mInbox.push_back(std::move(e));
}

void MemoryTransport::Leave()
{
    if (!mJoinedRoom)
    {
        return;
    }
    mJoinedRoom = false;
    mHub->mJoined.erase(mId);
    for (auto& [id, peer] : mHub->mJoined)
    {
        peer->Deliver({Event::kLeft, mId, {}, 0, nullptr});
    }
}

void MemoryTransport::Poll()
{
    const double now = NowMs();
    while (!mInbox.empty() && mInbox.front().at <= now)
    {
        Event e = std::move(mInbox.front());
        mInbox.pop_front();
        switch (e.kind)
        {
            case Event::kJoined:
                if (onPeerJoined)
                    onPeerJoined(e.from);
                break;
            case Event::kLeft:
                if (onPeerLeft)
                    onPeerLeft(e.from);
                break;
            case Event::kMessage:
                if (onMessage)
                    onMessage(e.from, e.message);
                break;
            case Event::kPong:
                e.pong();
                break;
        }
    }
}

} // namespace gta2net
