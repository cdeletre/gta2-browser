// The co-op lobby, ported from CoopLobby in web/net.js so that native players and browsers
// can share a room: the same JSON messages (hello, crew, side, roster, start, loaded, go;
// chat, sync, wp in a match) and the same rules. The host is whoever created the room and
// is the authority: it owns the roster, the settings and the crews' gangs. Slots follow
// join order (host = 0). Game messages are binary and go to onGameMessage by slot.
//
// The players and the settings are kept as JSON, as the browser keeps them, so that a
// roster from a browser host comes back unchanged (fields this port does not know about
// are kept).
// Multiplayer build only (gta2-mp).
#pragma once

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "transport.h"

namespace gta2net
{

constexpr int kMaxPlayers = 6;
constexpr double kTickMs = 1000.0 / 30;

// The input delay (ticks) the host picks for the slowest player's round trip (inputDelayFor).
int InputDelayFor(double rttMs);
// A Gang Wars crew name, cleaned as the browser does (letters, digits, space _ . ' -; 16 at most).
std::string CleanCrew(const std::string& name);

// One district's three street gangs: code, name, colour (GANGS in web/net.js).
struct Gang
{
    const char* code;
    const char* name;
    const char* colour;
};
const Gang* GangsOf(const std::string& district); // 3 entries; Downtown's for an unknown district

class Lobby
{
  public:
    // name: this player's name; host: this player creates the room.
    Lobby(Transport* transport, const std::string& name, bool host);

    // Called whenever the roster or the settings change.
    std::function<void()> onChange;
    // The match starts: the host's match object, with mySlot and delay (ticks, at least 1).
    std::function<void(const Json& match)> onStart;
    std::function<void(int slot, const Bytes& bytes)> onGameMessage;
    std::function<void(int slot)> onPlayerLeft;
    std::function<void(int slot, bool set, double x, double y)> onWaypoint;
    std::function<void(int slot, int tick, const std::string& h)> onSync;
    std::function<void(int slot, const std::string& text)> onChat;
    // Host: another player in the room also hosts it (sent a roster): two hosts in one room.
    std::function<void(const std::string& id)> onOtherHost;
    // The relay refused us ("full" or "taken").
    std::function<void(const std::string& why)> onRefused;

    bool IsHost() const { return mHost; }
    bool Started() const { return mStarted; }
    const std::string& HostId() const { return mHostId; }
    const std::string& MyId() const { return mT->Id(); }
    // [{id, name, crew, ping?}] in slot order
    const Json& Players() const { return mPlayers; }
    // {mode, district, friendlyFire, minutes, killLimit, pointsTarget, sides, lang}
    const Json& Settings() const { return mSettings; }
    int MySlot() const { return mMySlot; }
    // Host: players in the room who have not said hello for more than ms (another host, most
    // likely: hosts do not send hello, so two hosts in one room never see each other's roster).
    int SilentPeers(double ms) const;
    // The slot of a player id in the match, or -1.
    int SlotOf(const std::string& id) const;

    void SetCrew(const std::string& name);
    // Host: changes some settings (an object of the fields to change); everyone sees them.
    void SetSettings(const Json& changes);
    // The crews in the room: [{name, side, gang, colour, members: [id]}].
    Json Crews() const;
    // This player's crew sides with a gang ("" to clear).
    void PickSide(const std::string& side);
    // Host: why the match cannot start yet ("" when it can): waitingPlayer, needCrew,
    // twoCrews, threeCrews, or "waitingGang:" followed by the crews' names.
    std::string StartProblem() const;
    // Host: measures the round trip to every player (sets their ping, and so the input delay).
    void MeasurePings();
    // Host: starts the match for everyone in the room.
    void Start();
    // Every player's game is loaded: go() runs when all can enter the match together.
    void AllLoaded(std::function<void()> go);
    // This player's game is ready for game messages: delivers those that came early.
    void GameReady();

    void SendChat(const std::string& text);
    void ShareSync(int tick, const std::string& h);
    void ShareWaypoint(bool set, double x, double y);
    // Game messages from the core: to one slot, or -1 for everyone.
    void SendGame(int slot, const Bytes& bytes);

    struct Stats
    {
        int sent = 0;
        int received = 0;
        int early = 0;
    } stats;

  private:
    void OnMessage(const std::string& from, const Message& m);
    void BroadcastRoster();
    void ApplySide(const std::string& from, const std::string& side);
    void Left(const std::string& id);
    void Begin(Json match);
    void CheckLoaded();
    Json* FindPlayer(const std::string& id);
    void Changed()
    {
        if (onChange)
            onChange();
    }

    Transport* mT;
    std::string mName;
    bool mHost;
    std::string mHostId;
    Json mPlayers = Json::Array();
    Json mSettings;
    bool mStarted = false;
    bool mReady = false;
    std::map<std::string, int> mSlots; // id -> slot once the match starts
    int mMySlot = -1;
    std::set<std::string> mLoaded; // host: players whose game files are ready
    std::set<std::string> mGone;   // players who left after the start
    std::function<void()> mGo;
    std::vector<std::pair<int, Bytes>> mEarly;
    int mPingsOut = 0;
    std::map<std::string, double> mSilent; // host: peer id -> when it joined, until its hello
};

} // namespace gta2net
