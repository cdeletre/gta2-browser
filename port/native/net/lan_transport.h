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

    int Port() const { return mPort; }
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
};

} // namespace gta2net
