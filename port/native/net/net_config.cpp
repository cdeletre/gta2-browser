// See net_session.h: conf/net.ini, read and written back.
//
//   [net]
//   server = Main            the server connected last, by its name
//   names = ..., name = ...    the lobby's lists and what was picked last
//   ...
//   [server]                   one section per server, in the lobby's order
//   name = Main
//   url = wss://gta2.example.com
//   password =                 optional: the lobby asks for it
//   invite =                   or an invite code
//   ca_file =                  wss://: its authorities, or
//   tls_fingerprint =          its certificate's sha256
//
// An older file (server = <url>, password, invite, ca_file, tls_fingerprint in [net]) reads
// as one server; it is written in this format next time.
// Multiplayer build only (gta2-online).
#include <stdio.h>
#include <stdlib.h>

#include <fstream>
#include <sstream>
#include <string>

#include "net_session.h"

namespace gta2net
{

namespace
{
std::string Env(const char* name)
{
    const char* v = getenv(name);
    return v ? v : "";
}

std::string Trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::vector<std::string> SplitList(const std::string& s)
{
    std::vector<std::string> out;
    std::stringstream in(s);
    std::string item;
    while (std::getline(in, item, ','))
    {
        item = Trim(item);
        if (!item.empty())
            out.push_back(item);
    }
    return out;
}

std::string JoinList(const std::vector<std::string>& v)
{
    std::string out;
    for (const std::string& s : v)
        out += (out.empty() ? "" : ", ") + s;
    return out;
}

bool On(const std::string& v)
{
    return v != "0" && v != "off" && v != "false";
}

// "wss://gta2.example.com:443/relay" -> "gta2.example.com"
std::string HostOf(const std::string& url)
{
    std::string rest = url;
    const size_t scheme = rest.find("://");
    if (scheme != std::string::npos)
        rest = rest.substr(scheme + 3);
    rest = rest.substr(0, rest.find('/'));
    if (!rest.empty() && rest[0] == '[')
        return rest.substr(1, rest.find(']') - 1);
    if (rest.find(':') == rest.rfind(':'))
        rest = rest.substr(0, rest.find(':'));
    return rest;
}

const char* const kDefaultNames = "Ace, Rusty, Nina, Zed, Kasia, Moose, Lola, Duke";
const char* const kDefaultRooms = "ROOM1, ROOM2, ROOM3, ROOM4, ROOM5, ROOM6";
const char* const kDefaultCrews = "Reds, Blues, Greens";
const char* const kDefaultChat[4] = {"Follow me!", "Wait for me!", "Help!", "Nice one!"};
const char* const kChatKeys[4] = {"chat_up", "chat_down", "chat_left", "chat_right"};
} // namespace

std::vector<std::string> DefaultNames()
{
    return SplitList(kDefaultNames);
}

std::vector<std::string> DefaultRooms()
{
    return SplitList(kDefaultRooms);
}

ServerOptions OptionsOf(const NetConfig& c, int index)
{
    ServerOptions o;
    if (index < 0 || index >= static_cast<int>(c.servers.size()))
        return o;
    const ServerEntry& s = c.servers[static_cast<size_t>(index)];
    o.url = s.url;
    o.password = s.password;
    o.invite = s.invite;
    o.tls.caFile = s.caFile;
    if (!o.tls.caFile.empty() && o.tls.caFile[0] != '/')
        o.tls.caFile = c.confDir + "/" + o.tls.caFile;
    o.tls.fingerprint = s.fingerprint;
    return o;
}

NetConfig LoadNetConfig(const std::string& confDir)
{
    NetConfig c;
    c.confDir = confDir;
    c.path = confDir + "/net.ini";
    std::string names = kDefaultNames, name, rooms = kDefaultRooms, room, crews = kDefaultCrews, chosen;
    ServerEntry legacy; // an older file's one server, in [net]
    std::ifstream in(c.path);
    const bool found = in.is_open(); // (the stream's state after reading to the end is no test)
    enum
    {
        kNet,
        kServer,
        kLan,
        kOther
    } section = kNet;
    std::string line;
    int number = 0;
    while (found && std::getline(in, line))
    {
        ++number;
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;
        if (line[0] == '[')
        {
            const std::string head = Trim(line.substr(1, line.find(']') - 1));
            if (head == "net")
            {
                section = kNet;
            }
            else if (head == "lan")
            {
                section = kLan;
            }
            else if (head == "server")
            {
                section = kServer;
                c.servers.push_back({});
            }
            else
            {
                section = kOther;
                fprintf(stderr, "gta2: net: net.ini line %d: unknown section [%s], skipped\n", number, head.c_str());
            }
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos || section == kOther)
            continue;
        const std::string key = Trim(line.substr(0, eq));
        const std::string value = Trim(line.substr(eq + 1));
        if (section == kLan)
        {
            const bool digits = value.size() == 4 && value.find_first_not_of("0123456789") == std::string::npos;
            if (key == "pin" && digits)
                c.lanPin = value;
            else
                fprintf(stderr, "gta2: net: net.ini line %d: [lan] %s skipped (pin = 4 digits)\n", number, key.c_str());
            continue;
        }
        if (section == kServer)
        {
            ServerEntry& s = c.servers.back();
            if (key == "name")
                s.name = value;
            else if (key == "url")
                s.url = value;
            else if (key == "password")
                s.password = value;
            else if (key == "invite")
                s.invite = value;
            else if (key == "ca_file")
                s.caFile = value;
            else if (key == "tls_fingerprint")
                s.fingerprint = value;
            else
                fprintf(stderr, "gta2: net: net.ini line %d: unknown server setting \"%s\", skipped\n", number,
                        key.c_str());
            continue;
        }
        if (key == "server")
        {
            if (value.find("://") != std::string::npos)
                legacy.url = value; // the older format: the server itself
            else
                chosen = value;
        }
        else if (key == "password")
            legacy.password = value;
        else if (key == "invite")
            legacy.invite = value;
        else if (key == "ca_file")
            legacy.caFile = value;
        else if (key == "tls_fingerprint")
            legacy.fingerprint = value;
        else if (key == "names")
            names = value;
        else if (key == "name")
            name = value;
        else if (key == "rooms")
            rooms = value;
        else if (key == "room")
            room = value;
        else if (key == "crews")
            crews = value;
        else if (key == "crew")
            c.crew = value.substr(0, 16);
        else if (key == "show_chat")
            c.showChat = On(value);
        else if (key == "show_waypoints")
            c.showWaypoints = On(value);
        for (int i = 0; i < 4; ++i)
        {
            if (key == kChatKeys[i] && !value.empty())
                c.chat[i] = value.substr(0, 60);
        }
    }
    // servers: those with an address; the older format's first; one at least
    std::vector<ServerEntry> kept;
    if (!legacy.url.empty())
    {
        legacy.name = HostOf(legacy.url);
        chosen = legacy.name;
        kept.push_back(legacy);
    }
    for (ServerEntry& s : c.servers)
    {
        if (s.url.empty())
        {
            fprintf(stderr, "gta2: net: net.ini: a [server] without url (%s), skipped\n",
                    s.name.empty() ? "no name" : s.name.c_str());
            continue;
        }
        if (s.name.empty())
            s.name = HostOf(s.url);
        kept.push_back(s);
    }
    if (kept.empty())
        kept.push_back({"This computer", "ws://127.0.0.1:8790"});
    // one name, one server: "Main", "Main 2"...
    for (size_t i = 0; i < kept.size(); ++i)
    {
        const std::string base = kept[i].name;
        for (int n = 2;; ++n)
        {
            bool taken = false;
            for (size_t j = 0; j < i; ++j)
                taken = taken || kept[j].name == kept[i].name;
            if (!taken)
                break;
            kept[i].name = base + " " + std::to_string(n);
        }
    }
    c.servers = kept;
    c.selected = 0;
    for (size_t i = 0; i < c.servers.size(); ++i)
    {
        if (c.servers[i].name == chosen)
            c.selected = static_cast<int>(i);
    }
    // the environment (tests): a server of its own, or the chosen one's login and TLS
    if (!Env("GTA2_NET_SERVER").empty())
    {
        ServerEntry e;
        e.name = "GTA2_NET_SERVER";
        e.url = Env("GTA2_NET_SERVER");
        e.fromEnv = true;
        c.servers.insert(c.servers.begin(), e);
        c.selected = 0;
    }
    ServerEntry& s = c.servers[static_cast<size_t>(c.selected)];
    if (s.fromEnv)
    {
        s.password = Env("GTA2_NET_PASSWORD");
        s.invite = Env("GTA2_NET_INVITE");
        s.caFile = Env("GTA2_NET_CA_FILE");
        s.fingerprint = Env("GTA2_NET_TLS_FINGERPRINT");
    }
    c.server = OptionsOf(c, c.selected);
    if (!s.fromEnv)
    {
        // overrides for this run only (the file keeps its own values)
        if (!Env("GTA2_NET_PASSWORD").empty())
            c.server.password = Env("GTA2_NET_PASSWORD");
        if (!Env("GTA2_NET_INVITE").empty())
            c.server.invite = Env("GTA2_NET_INVITE");
        if (!Env("GTA2_NET_CA_FILE").empty())
            c.server.tls.caFile = Env("GTA2_NET_CA_FILE");
        if (!Env("GTA2_NET_TLS_FINGERPRINT").empty())
            c.server.tls.fingerprint = Env("GTA2_NET_TLS_FINGERPRINT");
    }
    for (int i = 0; i < 4; ++i)
    {
        if (c.chat[i].empty())
            c.chat[i] = kDefaultChat[i];
    }
    c.names = SplitList(names);
    c.rooms = SplitList(rooms);
    c.crews = SplitList(crews);
    if (c.names.empty())
        c.names = SplitList(kDefaultNames);
    if (c.rooms.empty())
        c.rooms = SplitList(kDefaultRooms);
    c.name = Env("GTA2_NET_NAME").empty() ? (name.empty() ? c.names[0] : name) : Env("GTA2_NET_NAME");
    c.room = Env("GTA2_NET_ROOM").empty() ? (room.empty() ? c.rooms[0] : room) : Env("GTA2_NET_ROOM");
    if (!found)
    {
        SaveNetConfig(c);
        fprintf(stderr, "gta2: net: wrote the default settings to %s\n", c.path.c_str());
    }
    return c;
}

void SaveNetConfig(const NetConfig& c)
{
    std::ofstream out(c.path);
    if (!out)
        return;
    // the server chosen last, as the file knows it (a GTA2_NET_SERVER of tests is not one)
    std::string chosen;
    const ServerEntry& sel = c.servers[static_cast<size_t>(c.selected)];
    if (!sel.fromEnv)
        chosen = sel.name;
    out << "# GTA2 multiplayer (gta2-online): the names and rooms the lobby offers (comma-separated;\n"
           "# the browser's lobby can join the same rooms by name), then the game servers.\n"
           "[net]\n"
        << "# the server connected last (its name, from the [server] sections below)\n"
        << "server = " << chosen << "\n"
        << "names = " << JoinList(c.names) << "\n"
        << "name = " << c.name << "\n"
        << "rooms = " << JoinList(c.rooms) << "\n"
        << "room = " << c.room << "\n"
        << "crews = " << JoinList(c.crews) << "\n"
        << "# the crew joined last, joined again on entering a room (as the browser does)\n"
        << "crew = " << c.crew << "\n"
        << "# in a match: Select + up, down, left, right sends one of these\n"
        << "chat_up = " << c.chat[0] << "\n"
        << "chat_down = " << c.chat[1] << "\n"
        << "chat_left = " << c.chat[2] << "\n"
        << "chat_right = " << c.chat[3] << "\n"
        << "# show the other players' chat messages and waypoints (also in Options)\n"
        << "show_chat = " << (c.showChat ? 1 : 0) << "\n"
        << "show_waypoints = " << (c.showWaypoints ? 1 : 0) << "\n"
        << "\n"
           "# gta2-lan (the local network, no server): the room PIN typed last (4 digits)\n"
           "[lan]\n"
        << "pin = " << c.lanPin << "\n"
        << "\n"
           "# One [server] section per game server, in the lobby's order. url: ws:// or wss://\n"
           "# (TLS). password: optional, the lobby asks for it (and keeps it here); or the code of\n"
           "# an invite link (?invite=...). wss:// checks the certificate against the console's\n"
           "# authorities; for a certificate of the server's own: ca_file (its authority, a PEM\n"
           "# file in conf/ unless absolute) or tls_fingerprint (sha256:..., the log shows it).\n";
    for (const ServerEntry& s : c.servers)
    {
        if (s.fromEnv)
            continue;
        out << "\n[server]\n"
            << "name = " << s.name << "\n"
            << "url = " << s.url << "\n"
            << "password = " << s.password << "\n"
            << "invite = " << s.invite << "\n"
            << "ca_file = " << s.caFile << "\n"
            << "tls_fingerprint = " << s.fingerprint << "\n";
    }
}

} // namespace gta2net
