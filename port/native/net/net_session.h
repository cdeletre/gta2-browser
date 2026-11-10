// gta2-mp's session before the game: the settings (conf/net.ini, overridden by the
// environment), the connection to the game server and the lobby, shared by the lobby
// screen (lobby_screen.cpp) and the automatic lobby for tests (net_main.cpp).
// Multiplayer build only (gta2-mp).
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "lobby.h"
#include "ws_transport.h"

namespace gta2net
{

struct NetConfig
{
    ServerOptions server;            // url, password or invite (room filled in when joining)
    std::vector<std::string> names;  // the names to pick from (no on-screen keyboard yet)
    std::string name;                // the last one used
    std::vector<std::string> rooms;  // the rooms to pick from
    std::string room;                // the last one used
    std::vector<std::string> crews;  // Gang Wars crew names to pick from
    std::string crew;                // the crew joined last (joined again on entering a room)
    std::string chat[4];             // the quick messages: Select + up, down, left, right
    bool showChat = true;            // other players' chat messages on screen (Options)
    bool showWaypoints = true;       // other players' waypoints on screen (Options)
    std::string path;                // conf/net.ini
    // the file's own server settings, written back as they were (GTA2_NET_* only override)
    std::string fileUrl, filePassword, fileInvite;
};

// Reads conf/net.ini (written with the defaults when missing); GTA2_NET_* override it.
NetConfig LoadNetConfig(const std::string& confDir);
// The lists a new net.ini starts with (the lobby's "DEFAULT" entry brings them back).
std::vector<std::string> DefaultNames();
std::vector<std::string> DefaultRooms();
// Remembers the name and the room picked last.
void SaveNetConfig(const NetConfig& config);

extern NetConfig* gConfig; // the settings in use (set before the lobby)
extern std::unique_ptr<Transport> gTransport;
extern std::unique_ptr<Lobby> gLobby;

// Connects to the server and enters a room (creating it when host); false and why on failure.
bool JoinRoom(const NetConfig& config, const std::string& name, const std::string& room, bool host, std::string* error);
void LeaveRoom();
// The match started (the lobby's onStart): sets up the core and waits until every player is
// loaded; eachLoop runs every 10 ms meanwhile (false gives up). False if the match cannot start.
bool EnterMatch(const Json& match, const std::function<bool()>& eachLoop);
// A start problem from Lobby::StartProblem() in words.
std::string ProblemText(const std::string& problem);

} // namespace gta2net
