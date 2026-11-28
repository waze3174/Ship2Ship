// combo/net/ComboArchipelago.cpp — see ComboArchipelago.h. The one translation unit that includes the network
// libraries: WinSock2 has to come before anything that includes windows.h, and the template stack needs /bigobj and no
// precompiled header (combo/CMake/ComboArchipelago.cmake).
//
// Two threads. The worker owns the connection (apclientpp is single-threaded) and talks to the rest only through two
// queues: events it saw, and commands it is given. The game thread calls Tick, applies the events to the ledgers of
// ComboApSync.h and acts on them. The worker never calls the game.
//
// The shape of the receive side follows junkx420/Updated-2S2H-Archipelago (client branch, 69cc318, CC0) and
// junkx420/Updated-SoH-Archipelago (client branch, c0e2cc3, no license; read for the protocol, nothing copied).
#define AP_NO_DEFAULT_DATA_PACKAGE_STORE // the store below keeps the packages in Ship2Ship's own folder
#include <apclient.hpp>

#include "ComboArchipelago.h"

#include "ComboApConsoleInput.h"
#ifdef SHIP2SHIP_AP_TLS
#include "ComboApTls.h"
#endif

#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <thread>
#include <unordered_map>
#include <utility>
#include <variant>

namespace ComboArchipelago {

namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
using ComboApSync::ReceivedItem;

constexpr auto kTickInterval = 100ms;
constexpr auto kLocationPollInterval = 500ms;
constexpr auto kGrantRetryInterval = 3s; // after a grant the game could not make
constexpr auto kWorkerStep = 20ms;
constexpr size_t kMaxQueuedInputs = 64; // lines typed into the console and not run yet
constexpr auto kTlsProbeTimeout = 15s;
// Exit codes of the TLS probe (RunTlsProbe), which the CI scripts read.
constexpr int kProbeVerified = 0;
constexpr int kProbeRefused = 1;
constexpr int kProbeUnreachable = 2;
constexpr int kProbeUsage = 3;
constexpr int kProbeNotBuilt = 4;

// The room's data package, kept per game and checksum so a room already visited costs no download.
class FileDataPackageStore final : public APDataPackageStore {
  public:
    explicit FileDataPackageStore(std::string directory) : directory_(std::move(directory)) {
    }

    bool load(const std::string& game, const std::string& checksum, json& data) override {
        if (directory_.empty()) {
            return false;
        }
        std::ifstream in(PathFor(game, checksum));
        if (!in.is_open()) {
            return false;
        }
        data = json::parse(in, nullptr, false);
        const bool usable = !data.is_discarded() && data.is_object();
        if (usable) {
            NoteGame(game, data);
        }
        return usable;
    }

    bool save(const std::string& game, const json& data) override {
        NoteGame(game, data);
        if (directory_.empty()) {
            return false;
        }
        std::error_code ec;
        std::filesystem::create_directories(directory_, ec);
        std::ofstream out(PathFor(game, data.value("checksum", std::string())), std::ios::trunc);
        out << data.dump();
        return out.good();
    }

    // The item names of Ship2Ship's own game, as the data package last handed over or loaded them (for /items).
    const std::vector<std::string>& ItemNames() const {
        return itemNames_;
    }

  private:
    void NoteGame(const std::string& game, const json& data) {
        const auto names = data.find("item_name_to_id");
        if (game != ComboApSync::kShip2ShipGame || names == data.end() || !names->is_object()) {
            return;
        }
        itemNames_.clear();
        for (const auto& entry : names->items()) {
            itemNames_.push_back(entry.key());
        }
    }

    std::filesystem::path PathFor(const std::string& game, const std::string& checksum) const {
        std::string name;
        for (const char c : game) {
            name.push_back(std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
        }
        const size_t hash = std::hash<std::string>{}(game);
        return std::filesystem::path(directory_) /
               (name + "-" + std::to_string(hash % 100000) + "-" + checksum + ".json");
    }

    std::string directory_;
    std::vector<std::string> itemNames_;
};

// What the worker saw.
struct LogEvent {
    LogLevel level;
    std::string line;
    bool toConsole; // the console shows it too (some lines only repeat a status)
};
struct PhaseEvent {
    Phase phase;
    std::string text;
};
struct ConnectedEvent {
    std::set<std::string> serverChecked;
    std::vector<std::string> allLocations; // every location of the slot, by name
    std::vector<std::string> itemNames;    // every item name of the slot's game
};
struct ItemsEvent {
    std::vector<ReceivedItem> items;
};
struct CheckedEvent {
    std::vector<std::string> names;
};
struct ClosedEvent {};
struct UnmappedEvent {
    int count;
};
struct ConsoleEvent {
    std::vector<ComboApConsole::Line> lines; // a PrintJSON message, already rendered
};
using EventBody = std::variant<LogEvent, PhaseEvent, ConnectedEvent, ItemsEvent, CheckedEvent, ClosedEvent,
                               UnmappedEvent, ConsoleEvent>;
struct Event {
    uint64_t session;
    EventBody body;
};

// What the worker is asked to do once connected.
struct SendCommand {
    std::vector<std::string> names;
};
struct StatusCommand {
    APClient::ClientStatus status;
};
struct SyncCommand {};
struct SayCommand {
    std::string text;
};
using Command = std::variant<SendCommand, StatusCommand, SyncCommand, SayCommand>;

std::string RefusalText(const std::list<std::string>& errors, const std::string& slotName) {
    std::string text;
    for (const std::string& error : errors) {
        std::string one = error;
        if (error == "InvalidSlot") {
            one = "no slot named \"" + slotName + "\" in this room";
        } else if (error == "InvalidPassword") {
            one = "wrong password";
        } else if (error == "InvalidGame") {
            one = "that slot is not a Ship2Ship slot";
        } else if (error == "IncompatibleVersion") {
            one = "the server and this client are incompatible versions";
        } else if (error == "InvalidItemsHandling") {
            one = "the server rejected the item settings";
        }
        text += (text.empty() ? "" : ", ") + one;
    }
    return text.empty() ? "refused by the server" : text;
}

std::string RandomUuid() {
    std::random_device device;
    std::mt19937_64 engine((static_cast<uint64_t>(device()) << 32) ^ device());
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "ship2ship-%016llx", static_cast<unsigned long long>(engine()));
    return buffer;
}

// The room's names for the console's renderer.
class RoomNames final : public ComboApConsole::NameSource {
  public:
    explicit RoomNames(const APClient& client) : client_(client) {
    }

    std::optional<std::string> PlayerName(int slot) const override {
        return Known(client_.get_player_alias(slot));
    }
    std::optional<std::string> ItemName(int64_t id, int ownerSlot) const override {
        const std::string& game = client_.get_player_game(ownerSlot);
        return game.empty() ? std::nullopt : Known(client_.get_item_name(id, game));
    }
    std::optional<std::string> LocationName(int64_t id, int ownerSlot) const override {
        const std::string& game = client_.get_player_game(ownerSlot);
        return game.empty() ? std::nullopt : Known(client_.get_location_name(id, game));
    }
    bool ConcernsSelf(int slot) const override {
        return client_.slot_concerns_self(slot);
    }

  private:
    // apclientpp answers "Unknown" for an id or slot it has no name for.
    static std::optional<std::string> Known(const std::string& name) {
        return name == "Unknown" ? std::nullopt : std::optional<std::string>(name);
    }

    const APClient& client_;
};

// The game thread's view of the console's own text: the tag the launcher's log lines carry is left off.
constexpr const char* kLogTag = "[Archipelago] ";

ComboApConsole::Level ConsoleLevel(LogLevel level) {
    switch (level) {
        case LogLevel::kError:
            return ComboApConsole::Level::kError;
        case LogLevel::kWarn:
            return ComboApConsole::Level::kWarn;
        case LogLevel::kInfo:
            break;
    }
    return ComboApConsole::Level::kInfo;
}

std::string WithoutLogTag(const std::string& line) {
    const size_t tag = std::strlen(kLogTag);
    return line.compare(0, tag, kLogTag) == 0 ? line.substr(tag) : line;
}

} // namespace

// ---- the shared state and the game-thread half ----

struct Client::Impl {
    Impl(GameLayer& gameLayer, Config cfg) : game(gameLayer), config(std::move(cfg)) {
    }

    GameLayer& game;
    const Config config;

    // Guarded by mutex.
    mutable std::mutex mutex;
    std::deque<Event> events;
    std::deque<Command> commands;
    Target target;
    bool wanted = false;
    uint64_t session = 0; // changes with every Connect and Disconnect
    Status status;
    std::string baseText;

    std::deque<std::string> inputs; // typed into the console, not run yet

    std::thread worker;
    std::atomic<bool> quit{ false };

    ComboApConsole::Log console;

    // Game thread only.
    uint64_t seenSession = 0;
    ComboApSync::SendLedger sends;
    ComboApSync::ReceiveLedger receives;
    bool connected = false;
    bool recordTracked = false;
    SaveRecord trackedRecord;
    bool askSync = false;
    enum class Reported { kNone, kPlaying, kGoal } reported = Reported::kNone;
    int unmapped = 0;
    std::set<std::string> warnedItems;
    std::vector<std::string> allLocations; // the slot's locations by name, sorted
    std::vector<std::string> itemNames;
    std::map<int, ReceivedItem> receivedList; // everything the server has sent, by index, for /received
    std::string lastPhaseNote;
    bool readyAsked = false;
    Clock::time_point lastTick{};
    Clock::time_point lastLocationPoll{};
    Clock::time_point grantRetryAt{};
    std::string note;

    void WorkerMain();
    void PushEvent(uint64_t forSession, EventBody body) {
        std::lock_guard<std::mutex> lock(mutex);
        events.push_back({ forSession, std::move(body) });
    }
    void Tick();
    void DrainEvents();
    void DrainInputs();
    void RunInput(const std::string& text);
    void RunClientCommand(const ComboApConsole::ParsedInput& input);
    void ListReceived(const std::string& filter);
    void Report(LogLevel level, const std::string& text);
    void Show(LogLevel level, const std::string& taggedLine);
    void Apply(const EventBody& body);
    bool RecordMatchesTarget(const SaveRecord& record);
    void TrackRecord(const SaveRecord& record);
    void GrantItems(Clock::time_point now);
    void SendChecks(Clock::time_point now);
    void ReportStatus();
    void PublishStatus();
    void QueueCommand(Command command) {
        std::lock_guard<std::mutex> lock(mutex);
        commands.push_back(std::move(command));
    }
};

// ---- the worker half: one connection attempt of one target ----

namespace {

class Session {
  public:
    Session(Client::Impl& owner, uint64_t id, const Target& target, const Config& config)
        : owner_(owner), id_(id), target_(target) {
        const std::string address = ComboApSync::NormalizeServerAddress(target.server, Client::TlsAvailable());
        const std::string problem = ComboApSync::ServerAddressProblem(address, Client::TlsAvailable());
        if (!problem.empty()) {
            Halt(Phase::kRefused, problem);
            return;
        }
        store_ = std::make_unique<FileDataPackageStore>(config.cacheDirectory);
        ap_ = std::make_unique<APClient>(RandomUuid(), ComboApSync::kShip2ShipGame, address, config.certStorePath,
                                         store_.get());
        ap_->set_socket_connected_handler([this] { Log(LogLevel::kInfo, "socket connected", false); });
        ap_->set_socket_error_handler([this](const std::string& message) { OnSocketError(message); });
        ap_->set_socket_disconnected_handler([this] { OnSocketClosed(); });
        ap_->set_room_info_handler([this] { OnRoomInfo(); });
        ap_->set_slot_connected_handler([this](const nlohmann::json&) { OnSlotConnected(); });
        ap_->set_slot_refused_handler([this](const std::list<std::string>& errors) { OnRefused(errors); });
        ap_->set_items_received_handler([this](const std::list<APClient::NetworkItem>& items) { OnItems(items); });
        ap_->set_location_checked_handler([this](const std::list<int64_t>& ids) { OnLocationsChecked(ids); });
        ap_->set_print_json_handler(std::function<void(const APClient::PrintJSONArgs&)>(
            [this](const APClient::PrintJSONArgs& args) { OnPrintJson(args); }));
        ap_->set_print_handler([this](const std::string& text) { OnPrint(text); });
        Announce(Phase::kConnecting, "Connecting to " + address + "...");
    }

    void Step(std::vector<Command>& commands) {
        if (halted_ || !ap_) {
            return;
        }
        try {
            ap_->poll();
            if (halted_) {
                ap_.reset();
                return;
            }
            RequestSlotWhenReady();
            if (ap_->get_state() == APClient::State::SLOT_CONNECTED) {
                for (Command& command : commands) {
                    Run(command);
                }
            }
        } catch (const std::exception& e) {
            Log(LogLevel::kError, std::string("connection error: ") + e.what());
            OnSocketClosed();
        }
    }

  private:
    void Push(EventBody body) {
        owner_.PushEvent(id_, std::move(body));
    }
    void Log(LogLevel level, const std::string& line, bool toConsole = true) {
        Push(LogEvent{ level, kLogTag + line, toConsole });
    }
    void Announce(Phase phase, const std::string& text) {
        Push(PhaseEvent{ phase, text });
    }
    void Halt(Phase phase, const std::string& text) {
        halted_ = true;
        Announce(phase, text);
        Log(LogLevel::kWarn, text);
    }

    void RequestSlotWhenReady() {
        if (slotRequested_ || ap_->get_state() != APClient::State::ROOM_INFO || !ap_->is_data_package_valid()) {
            return;
        }
        slotRequested_ = true;
        Announce(Phase::kConnecting, "Joining the room as " + target_.slotName + "...");
        ap_->ConnectSlot(target_.slotName, target_.password, ComboApSync::kItemsHandling, {});
    }

    void OnRoomInfo() {
        const std::string room = ap_->get_seed();
        if (ComboApSync::JudgeRoom(target_.seedName, room) != ComboApSync::RoomVerdict::kOk) {
            Halt(Phase::kWrongRoom,
                 "This file was made for room " + target_.seedName + ", but the server hosts " + room);
        }
    }

    void OnSlotConnected() {
        if (ComboApSync::JudgeSlotGame(ap_->get_game()) != ComboApSync::RoomVerdict::kOk) {
            Halt(Phase::kRefused, "slot \"" + target_.slotName + "\" plays " + ap_->get_game() + ", not Ship2Ship");
            return;
        }
        locationIds_.clear();
        ConnectedEvent connected;
        const std::set<int64_t> checked = ap_->get_checked_locations();
        const std::set<int64_t> missing = ap_->get_missing_locations();
        for (const std::set<int64_t>* ids : { &checked, &missing }) {
            for (const int64_t id : *ids) {
                const std::string name = ap_->get_location_name(id, ap_->get_game());
                locationIds_.emplace(name, id);
                connected.allLocations.push_back(name);
                if (ids == &checked) {
                    connected.serverChecked.insert(name);
                }
            }
        }
        std::sort(connected.allLocations.begin(), connected.allLocations.end());
        connected.itemNames = store_->ItemNames();
        Log(LogLevel::kInfo, "connected as " + target_.slotName + ": " + std::to_string(locationIds_.size()) +
                                 " locations, " + std::to_string(checked.size()) + " already checked");
        Push(std::move(connected));
        Announce(Phase::kConnected, "Connected as " + target_.slotName);
    }

    void OnRefused(const std::list<std::string>& errors) {
        Halt(Phase::kRefused, "Refused: " + RefusalText(errors, target_.slotName));
    }

    // A certificate that is not accepted ends the attempt: retrying would only ask the same server again, and
    // apclientpp's next try for an address without a scheme is plain ws://, which would send the password unencrypted
    // to whoever answered. The status says why; the console has the full reason, with the roots that were trusted.
    void RefuseCertificate(const std::string& message) {
        halted_ = true;
        Announce(Phase::kRefused,
                 "The server's " + ComboApSync::CertificateRefusalReason(message) + ". Not connecting.");
        Log(LogLevel::kError,
            "The server's " + message + ". Not connecting: type ws:// before the address to connect without TLS.");
    }

    void OnSocketError(const std::string& message) {
        if (message == lastError_) {
            return;
        }
        lastError_ = message;
        if (ComboApSync::IsCertificateRefusal(message)) {
            RefuseCertificate(message);
            return;
        }
        Announce(Phase::kConnecting, "Cannot reach the server (" + message + "); retrying");
        Log(LogLevel::kWarn, "socket error: " + message, false);
    }

    void OnSocketClosed() {
        slotRequested_ = false;
        Push(ClosedEvent{});
        Announce(Phase::kConnecting, "Connection lost; reconnecting");
    }

    void OnItems(const std::list<APClient::NetworkItem>& items) {
        ItemsEvent event;
        const std::string game = ap_->get_game();
        for (const APClient::NetworkItem& item : items) {
            ReceivedItem received;
            received.index = item.index;
            received.itemName = ap_->get_item_name(item.item, game);
            received.playerName = item.location == ComboApSync::kStartInventoryLocation
                                      ? ComboApSync::kServerPlayerName
                                      : ap_->get_player_alias(item.player);
            received.location = item.location;
            received.locationName = item.location == ComboApSync::kStartInventoryLocation
                                        ? std::string()
                                        : ap_->get_location_name(item.location, ap_->get_player_game(item.player));
            received.flags = item.flags;
            event.items.push_back(std::move(received));
        }
        Push(std::move(event));
    }

    void OnLocationsChecked(const std::list<int64_t>& ids) {
        CheckedEvent event;
        for (const int64_t id : ids) {
            event.names.push_back(ap_->get_location_name(id, ap_->get_game()));
        }
        Push(std::move(event));
    }

    // A PrintJSON message of any type (item sends, hints, chat, joins, command results, ...): every one is parts to
    // render, so the type itself changes nothing.
    void OnPrintJson(const APClient::PrintJSONArgs& args) {
        std::vector<ComboApConsole::Part> parts;
        for (const APClient::TextNode& node : args.data) {
            parts.push_back({ node.type, node.text, node.color, node.player, node.flags, node.hintStatus });
        }
        Push(ConsoleEvent{ ComboApConsole::RenderParts(parts, RoomNames(*ap_)) });
    }

    // The older plain-text message.
    void OnPrint(const std::string& text) {
        Push(ConsoleEvent{ ComboApConsole::Normalize(ComboApConsole::PlainLine(text)) });
    }

    void Run(const Command& command) {
        if (const auto* send = std::get_if<SendCommand>(&command)) {
            SendLocations(send->names);
        } else if (const auto* say = std::get_if<SayCommand>(&command)) {
            ap_->Say(say->text);
        } else if (const auto* status = std::get_if<StatusCommand>(&command)) {
            ap_->StatusUpdate(status->status);
        } else {
            ap_->Sync();
        }
    }

    void SendLocations(const std::vector<std::string>& names) {
        std::vector<int64_t> ids;
        int unmapped = 0;
        for (const std::string& name : names) {
            const auto it = locationIds_.find(name);
            if (it == locationIds_.end()) {
                ++unmapped;
            } else {
                ids.push_back(it->second);
            }
        }
        for (const std::vector<int64_t>& packet : ComboApSync::SplitIntoPackets(ids)) {
            ap_->LocationChecks(std::list<int64_t>(packet.begin(), packet.end()));
        }
        if (unmapped > 0) {
            Push(UnmappedEvent{ unmapped });
        }
        if (!ids.empty()) {
            Log(LogLevel::kInfo, "sent " + std::to_string(ids.size()) + " checks");
        }
    }

    Client::Impl& owner_;
    const uint64_t id_;
    const Target target_;
    std::unique_ptr<FileDataPackageStore> store_;
    std::unique_ptr<APClient> ap_;
    std::unordered_map<std::string, int64_t> locationIds_;
    std::string lastError_;
    bool slotRequested_ = false;
    bool halted_ = false;
};

} // namespace

void Client::Impl::WorkerMain() {
    std::unique_ptr<Session> current;
    uint64_t running = 0;
    while (!quit.load()) {
        std::vector<Command> toRun;
        bool restart = false;
        Target next;
        bool wantsNext = false;
        uint64_t nextId = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (session != running) {
                restart = true;
                running = session;
                next = target;
                wantsNext = wanted;
                nextId = session;
            }
            toRun.assign(std::make_move_iterator(commands.begin()), std::make_move_iterator(commands.end()));
            commands.clear();
        }
        if (restart) {
            current.reset();
            if (wantsNext) {
                current = std::make_unique<Session>(*this, nextId, next, config);
            }
        }
        if (current) {
            current->Step(toRun);
        }
        std::this_thread::sleep_for(kWorkerStep);
    }
}

// ---- the game-thread half ----

// One line for the launcher's log and for the console.
void Client::Impl::Show(LogLevel level, const std::string& taggedLine) {
    game.Log(level, taggedLine);
    console.Append(ComboApConsole::NoticeLine(ConsoleLevel(level), WithoutLogTag(taggedLine)));
}

void Client::Impl::Report(LogLevel level, const std::string& text) {
    Show(level, kLogTag + text);
}

void Client::Impl::Apply(const EventBody& body) {
    if (const auto* log = std::get_if<LogEvent>(&body)) {
        if (log->toConsole) {
            Show(log->level, log->line);
        } else {
            game.Log(log->level, log->line);
        }
    } else if (const auto* phase = std::get_if<PhaseEvent>(&body)) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            status.phase = phase->phase;
            baseText = phase->text;
        }
        // A refusal and a join are also logged, with more detail.
        const bool saidElsewhere =
            phase->phase == Phase::kRefused || phase->phase == Phase::kWrongRoom || phase->phase == Phase::kConnected;
        if (!saidElsewhere && phase->text != lastPhaseNote) {
            console.Append(ComboApConsole::NoticeLine(ComboApConsole::Level::kInfo, phase->text));
        }
        lastPhaseNote = phase->text;
    } else if (const auto* joined = std::get_if<ConnectedEvent>(&body)) {
        connected = true;
        sends.ResetForConnection(joined->serverChecked);
        allLocations = joined->allLocations;
        itemNames = joined->itemNames;
        receivedList.clear();
        readyAsked = false;
        receives.DropBuffered();
        reported = Reported::kNone;
        unmapped = 0;
        askSync = false;
    } else if (const auto* items = std::get_if<ItemsEvent>(&body)) {
        receives.Accept(items->items);
        for (const ReceivedItem& item : items->items) {
            if (item.index >= 0) {
                receivedList.emplace(item.index, item);
            }
        }
    } else if (const auto* printed = std::get_if<ConsoleEvent>(&body)) {
        console.Append(printed->lines);
    } else if (const auto* checked = std::get_if<CheckedEvent>(&body)) {
        sends.NoteServerChecked(checked->names);
    } else if (std::holds_alternative<ClosedEvent>(body)) {
        connected = false;
    } else if (const auto* missing = std::get_if<UnmappedEvent>(&body)) {
        unmapped += missing->count;
    }
}

void Client::Impl::DrainEvents() {
    std::deque<Event> taken;
    uint64_t current = 0;
    {
        std::lock_guard<std::mutex> lock(mutex);
        taken.swap(events);
        current = session;
    }
    if (seenSession != current) {
        seenSession = current;
        connected = false;
        reported = Reported::kNone;
        unmapped = 0;
        receives.DropBuffered();
        askSync = false;
        lastPhaseNote.clear();
    }
    for (const Event& event : taken) {
        if (event.session == current) {
            Apply(event.body);
        }
    }
}

void Client::Impl::DrainInputs() {
    std::deque<std::string> taken;
    {
        std::lock_guard<std::mutex> lock(mutex);
        taken.swap(inputs);
    }
    for (const std::string& text : taken) {
        RunInput(text);
    }
}

// One line the player typed: echoed when it is a command, then run or refused.
void Client::Impl::RunInput(const std::string& text) {
    using namespace ComboApConsole;
    const ParsedInput input = ParseInput(text);
    if (input.kind == InputKind::kEmpty) {
        return;
    }
    if (input.kind == InputKind::kClient || input.say[0] == '!') {
        console.Append(PlainLine("> " + (input.kind == InputKind::kClient ? Trimmed(text) : input.say), kEchoColor));
    }
    if (!connected && !WorksOffline(input)) {
        console.Append(
            NoticeLine(Level::kWarn, "Not connected. Use /connect [address] first; /help lists the commands."));
        return;
    }
    if (input.kind == InputKind::kSay) {
        QueueCommand(SayCommand{ input.say });
        return;
    }
    RunClientCommand(input);
}

void Client::Impl::ListReceived(const std::string& filter) {
    std::vector<ComboApConsole::ReceivedEntry> entries;
    for (const auto& [index, item] : receivedList) {
        entries.push_back({ index, item.itemName, item.flags, item.locationName, item.playerName });
    }
    console.Append(ComboApConsole::FormatReceived(entries, filter));
}

// The "/" commands, after Archipelago's CommonClient.py.
void Client::Impl::RunClientCommand(const ComboApConsole::ParsedInput& input) {
    using namespace ComboApConsole;
    const std::string& name = input.command;
    if (name == "help") {
        console.Append(FormatHelp());
    } else if (name == "connect") {
        const auto [address, password] = SplitWord(input.args);
        const std::string problem = game.ConsoleConnect(address, password);
        if (!problem.empty()) {
            console.Append(NoticeLine(Level::kWarn, problem));
        }
    } else if (name == "disconnect") {
        game.ConsoleDisconnect();
    } else if (name == "received") {
        ListReceived(input.args);
    } else if (name == "missing" || name == "checked") {
        console.Append(FormatChecks(name == "checked", allLocations, sends.known(), input.args));
    } else if (name == "items") {
        console.Append(FormatNames("Item names for Ship2Ship", itemNames, input.args, "/items", kCyan));
    } else if (name == "locations") {
        console.Append(FormatNames("Location names for Ship2Ship", allLocations, input.args, "/locations", kGreen));
    } else if (name == "ready") {
        readyAsked = !readyAsked;
        QueueCommand(StatusCommand{ readyAsked ? APClient::ClientStatus::READY : APClient::ClientStatus::CONNECTED });
        console.Append(NoticeLine(Level::kInfo, readyAsked ? "Readied up." : "Unreadied."));
    } else if (name == "clear") {
        console.Clear();
    } else {
        console.Append(NoticeLine(Level::kWarn, "Unknown command /" + name + ". /help lists the commands."));
    }
}

bool Client::Impl::RecordMatchesTarget(const SaveRecord& record) {
    Target wanted;
    {
        std::lock_guard<std::mutex> lock(mutex);
        wanted = target;
    }
    return record.valid && record.slotName == wanted.slotName &&
           (wanted.seedName.empty() || record.seedName == wanted.seedName);
}

// Another file was loaded (or the first one): its saved count is the new starting point, and the server is asked for
// the list again, because what was buffered belongs to the other file's count.
void Client::Impl::TrackRecord(const SaveRecord& record) {
    const bool same = recordTracked && trackedRecord.valid == record.valid && trackedRecord.fileKey == record.fileKey &&
                      trackedRecord.slotName == record.slotName && trackedRecord.seedName == record.seedName;
    if (same) {
        return;
    }
    recordTracked = true;
    trackedRecord = record;
    if (record.valid) {
        receives.Reset(record.receivedIndex);
        askSync = connected;
    }
    reported = Reported::kNone;
    unmapped = 0;
}

void Client::Impl::GrantItems(Clock::time_point now) {
    if (now < grantRetryAt) {
        return;
    }
    const std::vector<ReceivedItem> items = receives.NextGrants(ComboApSync::kMaxGrantsPerTick);
    if (items.empty()) {
        return;
    }
    std::vector<Grant> grants;
    for (const ReceivedItem& item : items) {
        const ComboApSync::GrantTarget grantTarget = ComboApSync::ApGrantTarget(item.itemName);
        if (grantTarget.valid) {
            grants.push_back({ grantTarget.game, grantTarget.nativeName });
        } else if (warnedItems.insert(item.itemName).second) {
            Report(LogLevel::kWarn, "'" + item.itemName + "' is not a Ship2Ship item; skipped");
        }
    }
    const int newIndex = receives.granted() + static_cast<int>(items.size());
    if (!game.GrantBatch(grants, newIndex)) {
        Report(LogLevel::kWarn, "could not give " + std::to_string(items.size()) + " received items; trying again");
        grantRetryAt = now + kGrantRetryInterval;
        return;
    }
    receives.MarkGranted(static_cast<int>(items.size()));
    for (const ComboApSync::ToastText& toast : ComboApSync::PlanToasts(items)) {
        game.Toast(toast);
    }
    Report(LogLevel::kInfo, "gave " + std::to_string(grants.size()) + " items, now at " +
                                std::to_string(receives.granted()) +
                                (items.front().IsStartInventory() ? " (start inventory)" : ""));
}

void Client::Impl::SendChecks(Clock::time_point now) {
    if (now - lastLocationPoll < kLocationPollInterval) {
        return;
    }
    lastLocationPoll = now;
    std::vector<std::string> fresh = sends.Plan(game.ObtainedLocations());
    if (!fresh.empty()) {
        QueueCommand(SendCommand{ std::move(fresh) });
    }
}

void Client::Impl::ReportStatus() {
    if (game.GoalReached()) {
        if (reported != Reported::kGoal) {
            reported = Reported::kGoal;
            QueueCommand(StatusCommand{ APClient::ClientStatus::GOAL });
            Report(LogLevel::kInfo, "goal reached; telling the server");
        }
    } else if (reported == Reported::kNone) {
        reported = Reported::kPlaying;
        QueueCommand(StatusCommand{ APClient::ClientStatus::PLAYING });
    }
}

void Client::Impl::PublishStatus() {
    std::lock_guard<std::mutex> lock(mutex);
    status.receivedIndex = receives.granted();
    status.itemsWaiting = static_cast<int>(receives.pending());
    status.checksKnown = static_cast<int>(sends.knownCount());
    status.checksUnmapped = unmapped;
    status.text = baseText;
    if (receives.ServerBehind() && status.phase == Phase::kConnected) {
        status.text += "; the server has fewer items than this file received";
    }
    if (!note.empty() && status.phase == Phase::kConnected) {
        status.text += "; " + note;
    }
}

void Client::Impl::Tick() {
    DrainEvents();
    DrainInputs();
    const Clock::time_point now = Clock::now();
    if (now - lastTick < kTickInterval) {
        return;
    }
    lastTick = now;
    bool isWanted = false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        isWanted = wanted;
    }
    if (!isWanted) {
        return;
    }
    const SaveRecord record = game.LoadedRecord();
    TrackRecord(record);
    note.clear();
    if (!RecordMatchesTarget(record)) {
        note =
            record.valid ? "the loaded file belongs to slot " + record.slotName : "load the Archipelago file to sync";
    } else if (!game.Ready()) {
        note = "waiting for the game";
    } else {
        GrantItems(now);
        if (connected) {
            if (askSync || receives.NeedsSync()) {
                askSync = false;
                receives.NoteSyncAsked();
                QueueCommand(SyncCommand{});
            }
            SendChecks(now);
            ReportStatus();
        }
    }
    PublishStatus();
}

// ---- the public face ----

Client::Client(GameLayer& game, Config config) : impl_(std::make_unique<Impl>(game, std::move(config))) {
    impl_->console.Append(
        ComboApConsole::NoticeLine(ComboApConsole::Level::kInfo, "Archipelago console. Type /help for the commands."));
    impl_->worker = std::thread([this] { impl_->WorkerMain(); });
}

Client::~Client() {
    impl_->quit.store(true);
    if (impl_->worker.joinable()) {
        impl_->worker.join();
    }
}

void Client::Connect(const Target& target) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->target = target;
    impl_->target.server = ComboApSync::NormalizeServerAddress(target.server, TlsAvailable());
    impl_->wanted = true;
    ++impl_->session;
    impl_->events.clear();
    impl_->commands.clear();
    impl_->status = Status{};
    impl_->status.phase = Phase::kConnecting;
    impl_->baseText = "Connecting to " + impl_->target.server + "...";
    impl_->status.text = impl_->baseText;
}

void Client::Disconnect() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->wanted) {
        impl_->console.Append(ComboApConsole::NoticeLine(ComboApConsole::Level::kInfo, "Disconnected."));
    }
    impl_->wanted = false;
    ++impl_->session;
    impl_->events.clear();
    impl_->commands.clear();
    impl_->status = Status{};
    impl_->baseText.clear();
}

void Client::Tick() {
    impl_->Tick();
}

ComboApConsole::Log& Client::Console() {
    return impl_->console;
}

void Client::SubmitInput(const std::string& text) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->inputs.size() < kMaxQueuedInputs) {
        impl_->inputs.push_back(text);
    }
}

Status Client::GetStatus() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->status;
}

Target Client::GetTarget() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    Target copy = impl_->target;
    copy.password.clear();
    return copy;
}

int RunTlsProbe(const std::string& hostPort, const std::string& verifyHost, const std::string& bundledRoots,
                std::ostream& out) {
#ifdef SHIP2SHIP_AP_TLS
    const size_t colon = hostPort.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 == hostPort.size()) {
        out << "usage: <host>:<port>\n";
        return kProbeUsage;
    }
    const ComboApTls::ProbeResult result = ComboApTls::Probe(hostPort.substr(0, colon), hostPort.substr(colon + 1),
                                                             bundledRoots, verifyHost, kTlsProbeTimeout);
    switch (result.outcome) {
        case ComboApTls::ProbeOutcome::kVerified:
            out << "TLS OK: " << result.text << "\n";
            return kProbeVerified;
        case ComboApTls::ProbeOutcome::kRefused:
            out << "TLS REFUSED: " << result.text << "\n";
            return kProbeRefused;
        case ComboApTls::ProbeOutcome::kUnreachable:
            break;
    }
    out << "TLS UNREACHABLE: " << result.text << "\n";
    return kProbeUnreachable;
#else
    (void)hostPort, (void)verifyHost, (void)bundledRoots;
    out << "this build has no TLS support\n";
    return kProbeNotBuilt;
#endif
}

bool Client::TlsAvailable() {
#ifdef SHIP2SHIP_AP_TLS
    return true;
#else
    return false;
#endif
}

} // namespace ComboArchipelago
