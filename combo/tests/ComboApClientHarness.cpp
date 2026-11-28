// combo/tests/ComboApClientHarness.cpp
//
// The real Archipelago client (combo/net/ComboArchipelago.cpp) over a fake game: no game DLL, no window. A script
// (scripts/ap/client_e2e.py) starts one of these per slot against a local Archipelago server and drives it through
// stdin, one command per line:
//
//   open <check>|<check>|...   the checks (Archipelago location names) the fake player opens
//   ready 0|1                  whether the games can take work (the played game in play, the other save resident)
//   goal 0|1                   whether the file's goal is reached
//   failgrants 0|1             make the next grants fail, as a game that cannot take them
//   file <key>                 load another file: its saved count is then the harness's state of that key
//   connect | disconnect       the menu's buttons
//   say <text>                 a line typed into the console: chat, a "!" server command or a "/" client command
//   dump                       print one STATE line: the save, the grants and the client's status as JSON
//   console                    print one CONSOLE line: the console's scrollback, each line as its coloured spans
//   quit
//
// `comboApClientHarness --tls-probe <host:port> [--verify-host <name>] [--cert-store ca.pem]` instead runs one TLS
// handshake with the client's own certificate checks (RunTlsProbe) and exits with its code: the CI's certificate test.
//
// The fake save is a JSON file (--state), written with each grant exactly as the launcher writes the received index
// with a grant, so a second run of the harness on the same file is a restart of the game.
#include "net/ComboArchipelago.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>

#include <nlohmann/json.hpp>

namespace {

using nlohmann::json;
using namespace ComboArchipelago;

constexpr auto kTickPeriod = std::chrono::milliseconds(10);
constexpr const char* kGameKeys[] = { "oot", "mm" };

struct Options {
    std::string server = "ws://127.0.0.1:38281";
    std::string slot;
    std::string seedName;
    std::string password;
    std::string statePath;
    std::string cacheDirectory;
    std::string certStorePath;
    std::string tlsProbe;
    std::string verifyHost;
    bool connectAtStart = true;
};

class FakeGame final : public GameLayer {
  public:
    FakeGame(const Options& options) : options_(options) {
        std::ifstream in(options.statePath);
        if (in.is_open()) {
            state_ = json::parse(in, nullptr, false);
        }
        if (!state_.is_object()) {
            state_ = json::object();
        }
        state_["files"] = state_.value("files", json::object());
    }

    SaveRecord LoadedRecord() override {
        SaveRecord record;
        record.valid = true;
        record.fileKey = fileKey_;
        record.slotName = options_.slot;
        record.seedName = options_.seedName;
        record.receivedIndex = File()["receivedIndex"].get<int>();
        return record;
    }
    bool Ready() override {
        return ready_.load();
    }
    std::vector<std::string> ObtainedLocations() override {
        std::vector<std::string> names;
        for (const auto& name : File()["obtained"]) {
            names.push_back(name.get<std::string>());
        }
        return names;
    }
    bool GoalReached() override {
        return goal_.load();
    }
    bool GrantBatch(const std::vector<Grant>& grants, int newReceivedIndex) override {
        if (failGrants_.load()) {
            return false;
        }
        json& file = File();
        for (const Grant& grant : grants) {
            file["granted"].push_back({ { "game", kGameKeys[grant.game] }, { "name", grant.nativeName } });
        }
        file["receivedIndex"] = newReceivedIndex;
        Save();
        return true;
    }
    void Toast(const ComboApSync::ToastText& toast) override {
        File()["toasts"].push_back(toast.prefix + " " + toast.message + " " + toast.suffix);
        std::cout << "TOAST " << toast.prefix << " " << toast.message << " " << toast.suffix << std::endl;
    }
    std::string ConsoleConnect(const std::string& address, const std::string& password) override {
        Target target = target_;
        if (!address.empty()) {
            target.server = address;
        }
        if (!password.empty()) {
            target.password = password;
        }
        client_->Connect(target);
        return "";
    }
    void ConsoleDisconnect() override {
        client_->Disconnect();
    }
    void Log(LogLevel level, const std::string& line) override {
        std::cout << "LOG "
                  << (level == LogLevel::kError  ? "error "
                      : level == LogLevel::kWarn ? "warn "
                                                 : "info ")
                  << line << std::endl;
    }

    void Open(const std::vector<std::string>& names) {
        json& obtained = File()["obtained"];
        for (const std::string& name : names) {
            if (std::find(obtained.begin(), obtained.end(), name) == obtained.end()) {
                obtained.push_back(name);
            }
        }
        Save();
    }
    void SetFile(int key) {
        fileKey_ = key;
    }
    // The client and target the console's /connect and /disconnect act on.
    void Attach(Client* client, const Target& target) {
        client_ = client;
        target_ = target;
    }
    void SetReady(bool ready) {
        ready_.store(ready);
    }
    void SetGoal(bool goal) {
        goal_.store(goal);
    }
    void SetFailGrants(bool fail) {
        failGrants_.store(fail);
    }
    json Dump() {
        json out = File();
        out["fileKey"] = fileKey_;
        return out;
    }

  private:
    json& File() {
        json& file = state_["files"][std::to_string(fileKey_)];
        if (!file.is_object()) {
            file = { { "receivedIndex", 0 },
                     { "obtained", json::array() },
                     { "granted", json::array() },
                     { "toasts", json::array() } };
        }
        return file;
    }
    void Save() {
        if (options_.statePath.empty()) {
            return;
        }
        const std::string temp = options_.statePath + ".tmp";
        {
            std::ofstream out(temp, std::ios::trunc);
            out << state_.dump();
        }
        std::filesystem::rename(temp, options_.statePath);
    }

    Options options_;
    Client* client_ = nullptr;
    Target target_;
    json state_;
    int fileKey_ = 0;
    std::atomic<bool> ready_{ true };
    std::atomic<bool> goal_{ false };
    std::atomic<bool> failGrants_{ false };
};

std::vector<std::string> Split(const std::string& text, char separator) {
    std::vector<std::string> parts;
    std::stringstream stream(text);
    std::string part;
    while (std::getline(stream, part, separator)) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }
    return parts;
}

json StatusJson(const Status& status) {
    return { { "phase", static_cast<int>(status.phase) }, { "text", status.text },
             { "receivedIndex", status.receivedIndex },   { "itemsWaiting", status.itemsWaiting },
             { "checksKnown", status.checksKnown },       { "checksUnmapped", status.checksUnmapped } };
}

// The console's scrollback as JSON: each line's plain text and its coloured spans.
json ConsoleJson(Client& client) {
    json lines = json::array();
    client.Console().Visit(0, [&](uint64_t, const ComboApConsole::Line& line) {
        json spans = json::array();
        for (const ComboApConsole::Span& span : line.spans) {
            spans.push_back({ { "color", span.color }, { "text", span.text } });
        }
        lines.push_back({ { "text", line.PlainText() }, { "spans", spans } });
    });
    return { { "lines", lines } };
}

bool ParseOptions(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (arg == "--server") {
            options.server = next();
        } else if (arg == "--slot") {
            options.slot = next();
        } else if (arg == "--seed-name") {
            options.seedName = next();
        } else if (arg == "--password") {
            options.password = next();
        } else if (arg == "--state") {
            options.statePath = next();
        } else if (arg == "--cache") {
            options.cacheDirectory = next();
        } else if (arg == "--cert-store") {
            options.certStorePath = next();
        } else if (arg == "--tls-probe") {
            options.tlsProbe = next();
        } else if (arg == "--verify-host") {
            options.verifyHost = next();
        } else if (arg == "--no-connect") {
            options.connectAtStart = false;
        } else {
            std::cerr << "unknown argument " << arg << "\n";
            return false;
        }
    }
    if (options.slot.empty() && options.tlsProbe.empty()) {
        std::cerr
            << "usage: comboApClientHarness --slot <name> --seed-name <room> [--server ws://127.0.0.1:38281] "
               "[--state file] [--cache dir] [--password p] [--cert-store ca.pem] [--no-connect]\n"
               "       comboApClientHarness --tls-probe <host:port> [--verify-host <name>] [--cert-store ca.pem]\n";
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!ParseOptions(argc, argv, options)) {
        return 2;
    }
    if (!options.tlsProbe.empty()) {
        return RunTlsProbe(options.tlsProbe, options.verifyHost, options.certStorePath, std::cout);
    }
    FakeGame game(options);
    Config config;
    config.cacheDirectory = options.cacheDirectory;
    config.certStorePath = options.certStorePath;
    Client client(game, config);
    const Target target{ options.server, options.slot, options.password, options.seedName };
    game.Attach(&client, target);
    if (options.connectAtStart) {
        client.Connect(target);
    }

    std::mutex lock;
    std::deque<std::string> lines;
    std::atomic<bool> inputClosed{ false };
    std::thread reader([&] {
        std::string line;
        while (std::getline(std::cin, line)) {
            std::lock_guard<std::mutex> guard(lock);
            lines.push_back(line);
        }
        inputClosed = true;
    });

    bool quit = false;
    while (!quit) {
        client.Tick();
        std::deque<std::string> commands;
        {
            std::lock_guard<std::mutex> guard(lock);
            commands.swap(lines);
        }
        for (const std::string& line : commands) {
            const size_t space = line.find(' ');
            const std::string verb = line.substr(0, space);
            const std::string rest = space == std::string::npos ? std::string() : line.substr(space + 1);
            if (verb == "open") {
                game.Open(Split(rest, '|'));
            } else if (verb == "ready") {
                game.SetReady(rest == "1");
            } else if (verb == "goal") {
                game.SetGoal(rest == "1");
            } else if (verb == "failgrants") {
                game.SetFailGrants(rest == "1");
            } else if (verb == "file") {
                game.SetFile(std::stoi(rest));
            } else if (verb == "connect") {
                client.Connect(target);
            } else if (verb == "disconnect") {
                client.Disconnect();
            } else if (verb == "say") {
                client.SubmitInput(rest);
            } else if (verb == "console") {
                std::cout << "CONSOLE " << ConsoleJson(client).dump(-1, ' ', false, json::error_handler_t::replace)
                          << std::endl;
            } else if (verb == "dump") {
                json out = { { "game", game.Dump() }, { "status", StatusJson(client.GetStatus()) } };
                std::cout << "STATE " << out.dump() << std::endl;
            } else if (verb == "quit") {
                quit = true;
            }
        }
        if (inputClosed && commands.empty()) {
            quit = true;
        }
        std::this_thread::sleep_for(kTickPeriod);
    }
    client.Disconnect();
    // The reader may be blocked in getline; the process ends here.
    std::cout.flush();
    std::_Exit(0);
}
