// combo/net/ComboArchipelago.h
// Ship2Ship + Archipelago: the online client. One connection for the whole launcher, next to Anchor's, on its own
// thread; the game side is reached only through GameLayer, which the launcher implements over the two games and the
// tests (combo/tests/ComboApClientHarness.cpp) over a fake. Only combo/net/ComboArchipelago.cpp includes the network
// libraries.
//
// What it does for the loaded file, in Tick (the game thread):
//   - sends every obtained check the server does not hold yet (ComboApSync::SendLedger);
//   - grants the items the server sends, in the server's order, from the file's saved receivedIndex, the start
//   inventory
//     among them (ComboApSync::ReceiveLedger), and writes the index with the grant (GameLayer::GrantBatch);
//   - reports the goal once per connection.
// It syncs only the file whose slot and room it was connected for, and only while the games can take the work.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ComboApConsole.h"
#include "ComboApSync.h"

namespace ComboArchipelago {

// Where to connect and as whom. The password is kept in memory and never written anywhere.
struct Target {
    std::string server;   // "host", "host:port", "ws://host:port" or "wss://host:port"
    std::string slotName; // the slot to connect as
    std::string password;
    std::string seedName; // the room the file was generated for: any other room is refused; "" accepts any
};

// The loaded file's Archipelago record (combo.archipelago in its save), or !valid when no such file is loaded.
struct SaveRecord {
    bool valid = false;
    int fileKey = -1; // which file: loading another one starts the receive count over
    std::string slotName;
    std::string seedName;
    int receivedIndex = 0;
};

struct Grant {
    ComboRando::GameId game = ComboRando::GAME_OOT;
    std::string nativeName;
};

enum class LogLevel { kInfo, kWarn, kError };

// Everything the client needs from the game. Called on the game thread, from Tick only.
class GameLayer {
  public:
    virtual ~GameLayer() = default;

    // The loaded file's record.
    virtual SaveRecord LoadedRecord() = 0;

    // True when the played game is in play, with the file's save of the other game resident too: checks can be read
    // and items given.
    virtual bool Ready() = 0;

    // Every obtained check of both games under its Archipelago location name.
    virtual std::vector<std::string> ObtainedLocations() = 0;

    // True once the file's goal is reached (both final bosses, or the Triforce Piece count).
    virtual bool GoalReached() = 0;

    // Gives the items and saves `newReceivedIndex` in the same save write. Returns false when it could not; nothing is
    // then counted as given and the same items come again.
    virtual bool GrantBatch(const std::vector<Grant>& grants, int newReceivedIndex) = 0;

    virtual void Toast(const ComboApSync::ToastText& toast) = 0;
    virtual void Log(LogLevel level, const std::string& line) = 0;

    // The console's /connect and /disconnect: the game side saves the connection with the file and calls
    // Client::Connect or Disconnect, as the menu's buttons do. `address` is empty to use the one saved with the file.
    // Returns why it could not connect, or "".
    virtual std::string ConsoleConnect(const std::string& address, const std::string& password) = 0;
    virtual void ConsoleDisconnect() = 0;
};

enum class Phase {
    kOff,        // no connection wanted
    kConnecting, // connecting, or waiting to retry
    kConnected,  // in the room as the slot
    kRefused,    // the server refused the slot (name, password or version); not retried
    kWrongRoom,  // the server's room is not the one the file was generated for; not retried
};

struct Status {
    Phase phase = Phase::kOff;
    std::string text;       // one line for the menu
    int receivedIndex = 0;  // items given from the server's list
    int itemsWaiting = 0;   // received and not given yet
    int checksKnown = 0;    // checks the server holds for the slot, sent or not
    int checksUnmapped = 0; // obtained checks that are not Archipelago locations (pre-placed items)
};

struct Config {
    std::string cacheDirectory; // the data packages of the rooms visited are kept here
    std::string certStorePath;  // a CA bundle for wss://; empty uses the system's
};

class Client {
  public:
    Client(GameLayer& game, Config config);
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    // Starts, or restarts, the connection. Thread-safe.
    void Connect(const Target& target);
    // Closes it. Thread-safe.
    void Disconnect();

    // Once per game frame, on the game thread: cheap between its own intervals.
    void Tick();

    // The console's scrollback: what the server prints and what the client says. Thread-safe.
    ComboApConsole::Log& Console();

    // A line the player typed into the console ("!hint Bow", chat, or a "/" command). Thread-safe; it is run by the
    // next Tick, on the game thread.
    void SubmitInput(const std::string& text);

    Status GetStatus() const;
    Target GetTarget() const;

    // True when wss:// is built in.
    static bool TlsAvailable();

    struct Impl; // opaque: the worker half in ComboArchipelago.cpp reaches it

  private:
    std::unique_ptr<Impl> impl_;
};

} // namespace ComboArchipelago
