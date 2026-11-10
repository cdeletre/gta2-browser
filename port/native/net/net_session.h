// gta2-online's session before the game: the settings (conf/net.ini, overridden by the
// environment), the connection to the game server and the lobby, shared by the lobby
// screen (lobby_screen.cpp) and the automatic lobby for tests (net_main.cpp).
// Multiplayer build only (gta2-online).
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "lan_transport.h"
#include "lobby.h"
#include "ws_transport.h"

namespace gta2net
{

// One game server of conf/net.ini (a [server] section).
struct ServerEntry
{
    std::string name;        // as the lobby shows it (the url's host when the file has none)
    std::string url;         // ws:// or wss://host[:port][/path]
    std::string password;    // optional: the lobby asks for it when the server wants one
    std::string invite;      // or an invite code
    std::string caFile;      // wss://: its authorities (in conf/ unless absolute)
    std::string fingerprint; // wss://: or its certificate's sha256
    bool fromEnv = false;    // GTA2_NET_SERVER (tests): not written to the file
};

struct NetConfig
{
    std::vector<ServerEntry> servers; // in the file's order
    int selected = 0;                 // the one connected last ([net] server = its name)
    int connected = -1;               // the lobby is connected to this one (-1: none)
    ServerOptions server;             // the one to join rooms on: url, login, TLS, the session
    std::vector<std::string> names;   // the names to pick from
    std::string name;                 // the last one used
    std::vector<std::string> rooms;   // the rooms to pick from
    std::string room;                 // the last one used
    std::vector<std::string> crews;   // Gang Wars crew names to pick from
    std::string crew;                 // the crew joined last (joined again on entering a room)
    std::string chat[4];              // the quick messages: Select + up, down, left, right
    bool showChat = true;             // other players' chat messages on screen (Options)
    bool showWaypoints = true;        // other players' waypoints on screen (Options)
    std::string lanPin = "0000";      // gta2-lan: the PIN typed last ([lan] pin)
    std::string path;                 // conf/net.ini
    std::string confDir;              // conf/ (a relative ca_file is there)
};

// Reads conf/net.ini (written with the defaults when missing). Several [server] sections;
// an older file's server = <url> in [net] reads as one. GTA2_NET_SERVER (with GTA2_NET_PASSWORD,
// _INVITE, _CA_FILE, _TLS_FINGERPRINT) adds a server of its own, chosen, never written;
// without it, those override the chosen server's (tests).
NetConfig LoadNetConfig(const std::string& confDir);
// The lists a new net.ini starts with (the lobby's "DEFAULT" entry brings them back).
std::vector<std::string> DefaultNames();
std::vector<std::string> DefaultRooms();
// Writes the settings back: the lists, what was picked last, the servers (new format).
void SaveNetConfig(const NetConfig& config);
// The connection settings of servers[index] (the session not included).
ServerOptions OptionsOf(const NetConfig& config, int index);

extern NetConfig* gConfig; // the settings in use (set before the lobby)
extern std::unique_ptr<Transport> gTransport;
extern std::unique_ptr<Lobby> gLobby;

// The lobby's connection to servers[index]: logs in and checks the relay lets us in (the
// previous server is let go first). Then config.server is its settings with the session.
ServerCheck ConnectServer(NetConfig& config, int index);
void DisconnectServer(NetConfig& config);

// gta2-lan: the beacons heard (made on first use), and entering a room of the local network:
// roomId "" with host creates one (named room); roomId "" without host joins the room of
// seeds[0] (an address); else the room heard of, with its members' addresses as seeds.
LanDiscovery* Discovery();
bool JoinLanRoom(const NetConfig& config, const std::string& roomId, const std::vector<std::string>& seeds,
                 const std::string& room, bool host, std::string* error);

// Connects to the server and enters a room (creating it when host); false and why on failure.
bool JoinRoom(const NetConfig& config, const std::string& name, const std::string& room, bool host, std::string* error);
void LeaveRoom();
// The match started (the lobby's onStart): sets up the core and waits until every player is
// loaded; eachLoop runs every 10 ms meanwhile (false gives up). False if the match cannot start.
bool EnterMatch(const Json& match, const std::function<bool()>& eachLoop);
// A start problem from Lobby::StartProblem() in words.
std::string ProblemText(const std::string& problem);

} // namespace gta2net
