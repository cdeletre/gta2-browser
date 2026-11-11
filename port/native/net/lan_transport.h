// gta2-lan: multiplayer on the local network with no server. Every player of a room is
// connected to every other one over TCP (a mesh: 15 connections for 6 players); the lobby, the
// lockstep and the rest run over it as over the game server's relay (transport.h).
//
//   - Discovery: every player in a room says so once a second in a UDP broadcast (port 27900):
//     the room, its id, this player's id, name and TCP port, and for the host the room's mode,
//     player count and state. LanDiscovery lists the rooms heard in the last 5 s.
//   - Joining: a player connects to the members it knows (from the beacons, or one typed
//     address); each member answers with the members it is connected to, which the newcomer
//     connects to in turn. Two players who connect to each other at once keep the connection
//     made by the lower id.
//   - The room's PIN (4 digits, 0000 by default) is checked on every connection: a wrong one is
//     refused ("pin").
//   Frames on a connection: [length (4 bytes, big-endian)][type (1 byte)][payload].
//   - The game messages (the lockstep's inputs) go over UDP instead, when both sides have it
//     (the hello says "udp": its port; [lan] udp = 0 in lan.ini, or GTA2_LAN_UDP=0: none): over TCP, a lost segment held
//     every player up 200 ms or more (the kernel's retransmission timer, doubling at each loss).
//     They stay reliable and in order: numbered, kept until acknowledged, and every datagram
//     carries all those not acknowledged yet (so a lost one comes again with the next, or after
//     30 ms at most); the receiver takes them in order and acknowledges in its own datagrams.
//     A player leaving says so over TCP ("bye": its last number), and the others let the lobby
//     know only once they have every game message before it (the departure lands after its last
//     inputs everywhere), or after 3 s.
//   - GTA2_LAN_DROP=percent (tests): that share of game messages is lost on the way out: a UDP
//     datagram is dropped; over TCP, the connection is held 200 ms, as a TCP retransmission does.
// Multiplayer build for the local network only (gta2-lan).
#pragma once

#include <stdint.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "transport.h"

namespace gta2net
{

constexpr int kLanBeaconPort = 27900; // UDP: the beacons
constexpr int kLanPort = 27901;       // TCP: the first port tried (the next ones if taken)

// A room heard on the network.
struct LanGame
{
    std::string roomId;     // unique: its first host's id
    std::string room;       // its name, as the host typed it
    std::string hostName;   // "" until the host's own beacon is heard
    std::string mode;       // the host's setting (roam, wars, ffa)
    std::string state;      // lobby or playing
    int players = 0;        // the host's count (else the members heard)
    int version = 0;        // the co-op version (kCoopVersion)
    double seenAt = 0;
    struct Member
    {
        std::string id, name, address; // address: ip:port
        double seenAt;
    };
    std::vector<Member> members;
};

class LanDiscovery
{
  public:
    explicit LanDiscovery(int port = kLanBeaconPort);
    ~LanDiscovery();
    bool Ok() const { return mFd >= 0; }
    // Reads the beacons that came, forgets the rooms not heard for 5 s.
    void Poll();
    const std::map<std::string, LanGame>& Games() const { return mGames; }
    // Sends one beacon now (LanTransport paces them: one a second).
    void Announce(const Json& beacon);

  private:
    int mFd = -1;
    int mPort;
    std::map<std::string, LanGame> mGames; // by room id
};

struct LanOptions
{
    std::string roomId;   // the room to enter (a new one when creating: this player's id)
    std::string room;     // its name
    std::string name;     // this player's name (in the beacons)
    std::string pin = "0000";
    bool udp = true;      // the game messages over UDP when the others have it too (GTA2_LAN_UDP overrides)
    bool host = false;    // created it: announces its settings
    std::vector<std::string> seeds; // ip:port of members to connect to first
    int port = kLanPort;  // the first TCP port to try
};

class LanTransport : public Transport
{
  public:
    // Opens the listening port; nullptr (and why) on failure. discovery may be null (tests).
    static std::unique_ptr<LanTransport> Create(const LanOptions& options, LanDiscovery* discovery,
                                                std::string* error);
    ~LanTransport() override;

    void Join() override;
    void Send(const std::string& to, const Message& m) override;
    void Ping(const std::string& id, std::function<void(double)> ok, std::function<void()> fail) override;
    void Leave() override;
    void Poll() override;
    bool Connected() const override { return mListen >= 0; }
    bool LinkInfo(const std::string& id, LinkStats* out) const override;

    int Port() const { return mPort; }
    int UdpPort() const { return mUdpPort; } // 0: none
    const std::string& RoomId() const { return mOpt.roomId; }
    // What the host's beacon says of the room: {mode, players, state} (net_main.cpp sets it).
    std::function<Json()> beaconInfo;

    struct Peer;

  private:
    LanTransport(const LanOptions& options, LanDiscovery* discovery, int listenFd, int port);
    void Dial(const std::string& address, const std::string& expect = "");
    void Accept();
    void Handle(Peer& p, int type, const std::string& payload);
    void SendFrame(Peer& p, int type, const std::string& payload);
    void Drop(Peer& p, const char* why);
    void SendPeers(Peer& p);
    Json Hello() const;
    void OpenUdp();
    void SendUdp(const std::string& id, double now);
    void PollUdp(double now);
    void Departed(const std::string& id, const char* why);

    LanOptions mOpt;
    LanDiscovery* mDiscovery;
    int mListen;
    int mPort;
    bool mJoined = false;
    double mLastBeacon = 0;
    std::vector<std::unique_ptr<Peer>> mPeers;
    std::set<std::string> mMembers; // the players the lobby was told of (joined, not left)
    std::map<std::string, double> mDialed; // address -> when it was dialed (tried again after 5 s)
    struct PendingPing
    {
        double start;
        std::function<void(double)> ok;
        std::function<void()> fail;
    };
    std::map<std::string, PendingPing> mPings; // by n
    // UDP for the game messages, per player (not per connection: a connection can be swapped)
    struct UdpLink;
    int mUdp = -1;
    int mUdpPort = 0;
    std::map<std::string, std::unique_ptr<UdpLink>> mLinks; // by player id
    struct Leaving
    {
        uint32_t lastSeq;
        double since;
        std::string why;
    };
    std::map<std::string, Leaving> mLeaving; // gone over TCP, their last game messages awaited
};

} // namespace gta2net
