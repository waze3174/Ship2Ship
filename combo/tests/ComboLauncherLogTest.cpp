// combo/tests/ComboLauncherLogTest.cpp
//
// Tests for the launcher's log buffer (combo/ComboLauncherLog.h): lines wait until the game's logger exists, arrive in
// order and tagged, the buffer stays bounded, and nothing is delivered after the sink is detached. No game DLL. Build
// and run: `cmake --build <dir> --target comboLauncherLogTest`, then run the exe; it exits 0 when every check passes.
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "ComboLauncherLog.h"
#include "ComboTestCheck.h"

namespace {

using ComboLauncherLog::Level;
using ComboLauncherLog::Line;
using ComboLauncherLog::Log;

struct Recorder {
    bool ready = false;
    std::vector<Line> lines;

    ComboLauncherLog::Sink AsSink() {
        return [this](Level level, const std::string& text) {
            if (!ready)
                return false;
            lines.push_back({ level, text });
            return true;
        };
    }
};

void TestLinesWaitForTheGameLogger() {
    Log log;
    Recorder game;
    log.Write(Level::Info, "first");
    log.Attach(game.AsSink());
    log.Write(Level::Error, "second");
    CHECK(game.lines.empty());

    game.ready = true;
    log.Flush();
    CHECK(game.lines.size() == 2);
    CHECK(game.lines[0].text == "[Launcher] first");
    CHECK(game.lines[0].level == Level::Info);
    CHECK(game.lines[1].text == "[Launcher] second");
    CHECK(game.lines[1].level == Level::Error);
    CHECK(log.Pending().empty());
}

void TestLaterLinesGoStraightThrough() {
    Log log;
    Recorder game;
    game.ready = true;
    log.Attach(game.AsSink());
    log.Write(Level::Warn, "a");
    log.Write(Level::Warn, "b");
    CHECK(game.lines.size() == 2);
    CHECK(log.Pending().empty());
}

void TestBufferIsBounded() {
    Log log;
    for (std::size_t i = 0; i < ComboLauncherLog::kMaxPendingLines + 10; i++)
        log.Write(Level::Info, std::to_string(i));
    CHECK(log.Pending().size() == ComboLauncherLog::kMaxPendingLines);
    CHECK(log.Pending().front().text == std::string(ComboLauncherLog::kTag) + "10");

    Recorder game;
    game.ready = true;
    log.Attach(game.AsSink());
    CHECK(game.lines.size() == ComboLauncherLog::kMaxPendingLines + 1);
    CHECK(game.lines.front().level == Level::Warn); // the note that 10 lines were dropped
    CHECK(game.lines.front().text.find("10 earlier line(s)") != std::string::npos);
}

void TestDetachStopsDelivery() {
    Log log;
    Recorder game;
    log.Attach(game.AsSink());
    log.Write(Level::Info, "kept");
    const auto undelivered = log.Detach();
    CHECK(undelivered.size() == 1);

    game.ready = true;
    log.Write(Level::Info, "after");
    log.Flush();
    CHECK(game.lines.empty());
    CHECK(log.Pending().size() == 1);
}

void TestFirstTimeKeys() {
    Log log;
    CHECK(log.FirstTime("a"));
    CHECK(!log.FirstTime("a"));
    CHECK(log.FirstTime("b"));
}

void TestAppendLinesWritesTheFile() {
    const auto dir = std::filesystem::temp_directory_path() / "comboLauncherLogTest";
    std::filesystem::remove_all(dir);
    const auto file = dir / "logs" / "boot.log";

    CHECK(ComboLauncherLog::AppendLines(file, { { Level::Error, "[Launcher] boom" } }));
    CHECK(ComboLauncherLog::AppendLines(file, { { Level::Info, "[Launcher] again" } }));
    std::string first, second;
    {
        std::ifstream in(file);
        std::getline(in, first);
        std::getline(in, second);
    }
    CHECK(first == "[error] [Launcher] boom");
    CHECK(second == "[info] [Launcher] again");
    std::filesystem::remove_all(dir);
}

void TestStreamEndsItsOwnLine() {
    Recorder game;
    game.ready = true;
    ComboLauncherLog::Instance().Attach(game.AsSink());
    ComboLauncherLog::Warn() << "count " << 3 << " hex 0x" << std::hex << 255 << std::dec << "\n";
    ComboLauncherLog::Info() << "done" << std::endl;
    ComboLauncherLog::Instance().Detach();
    CHECK(game.lines.size() == 2);
    CHECK(game.lines[0].text == "[Launcher] count 3 hex 0xff");
    CHECK(game.lines[0].level == Level::Warn);
    CHECK(game.lines[1].text == "[Launcher] done");
}

void TestCaughtNamesTheException() {
    Recorder game;
    game.ready = true;
    ComboLauncherLog::Instance().Attach(game.AsSink());
    try {
        throw std::runtime_error("bad json");
    } catch (...) { ComboLauncherLog::Caught("reading the seed"); }
    ComboLauncherLog::Instance().Detach();
    CHECK(game.lines.size() == 1);
    CHECK(game.lines[0].text.find("reading the seed failed: bad json") != std::string::npos);
}

} // namespace

int main() {
    TestLinesWaitForTheGameLogger();
    TestLaterLinesGoStraightThrough();
    TestBufferIsBounded();
    TestDetachStopsDelivery();
    TestFirstTimeKeys();
    TestAppendLinesWritesTheFile();
    TestStreamEndsItsOwnLine();
    TestCaughtNamesTheException();

    if (ComboTest::gFailures != 0) {
        std::cerr << ComboTest::gFailures << " of " << ComboTest::gChecks << " checks FAILED\n";
        return 1;
    }
    std::cout << "comboLauncherLogTest: all " << ComboTest::gChecks << " checks passed\n";
    return 0;
}
