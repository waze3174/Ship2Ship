// combo/discord/tests/DiscordActivityTest.cpp
//
// Tests for the pure parts of Discord Rich Presence: what the activity says and drops, the IPC framing and the socket
// paths. No Discord, no sockets. Build and run: `cmake --build <dir> --target comboDiscordTest`, then run the exe;
// it exits 0 when every check passes.
#include <algorithm>
#include <array>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "discord/DiscordActivity.h"
#include "discord/DiscordFrame.h"
#include "discord/DiscordIpcPaths.h"

namespace {

int gFailures = 0;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++gFailures;                                                       \
        }                                                                      \
    } while (0)

using namespace ComboDiscord;

constexpr uint32_t kAllOptional = kSettingArea | kSettingChecks | kSettingLink | kSettingClock | kSettingCoop |
                                  kSettingSessionTimer | kSettingPlayTime;
constexpr const char* kVersion = "0.2.0";
constexpr int64_t kStart = 1700000000;

PresenceState RandoMmInPlay() {
    PresenceState s;
    s.inPlay = true;
    s.game = Game::kMm;
    s.mode = "Randomizer";
    s.area = "Clock Town";
    s.checksGotten = 312;
    s.checksTotal = 1450;
    s.form = 1;
    s.maxHearts = 12;
    s.day = 2;
    s.minuteOfDay = 14 * 60 + 25;
    s.coopPlayers = 2;
    s.playMinutes = 34 * 60 + 12;
    return s;
}

std::string StateOf(const nlohmann::json& activity) {
    return activity.value("state", std::string());
}

std::string HoverOf(const nlohmann::json& activity) {
    return activity["assets"]["large_text"].get<std::string>();
}

void TestEverythingOn() {
    const auto activity = FormatActivity(RandoMmInPlay(), kAllOptional, kVersion, kStart);
    CHECK(activity["details"] == "Majora's Mask: Clock Town");
    CHECK(StateOf(activity) == "Randomizer \xC2\xB7 312 / 1450 checks \xC2\xB7 Goron Link \xC2\xB7 12 hearts \xC2\xB7 "
                               "Day 2, 14:00 \xC2\xB7 Co-op (2 players) \xC2\xB7 34h 12m played");
    CHECK(activity["timestamps"]["start"] == kStart);
    CHECK(activity["assets"]["large_image"] == "ship2ship");
    CHECK(HoverOf(activity) == "Ship2Ship v0.2.0");
    CHECK(activity["buttons"].size() == 1);
    CHECK(activity["buttons"][0]["label"] == "Get Ship2Ship");
    CHECK(activity["buttons"][0]["url"] == "https://github.com/waze3174/Ship2Ship/releases");
}

void TestGameAndModeAreAlwaysShown() {
    const auto activity = FormatActivity(RandoMmInPlay(), 0, kVersion, kStart);
    CHECK(activity["details"] == "Majora's Mask");
    CHECK(StateOf(activity) == "Randomizer");
    CHECK(!activity.contains("timestamps"));
}

void TestEachSwitchDropsItsField() {
    const PresenceState s = RandoMmInPlay();
    const std::vector<std::pair<uint32_t, std::string>> dropped = {
        { kSettingChecks, "checks" }, { kSettingLink, "Goron Link" }, { kSettingLink, "hearts" },
        { kSettingClock, "Day 2" },   { kSettingCoop, "Co-op" },      { kSettingPlayTime, "played" },
    };
    for (const auto& [bit, text] : dropped) {
        const auto activity = FormatActivity(s, kAllOptional & ~bit, kVersion, kStart);
        CHECK(StateOf(activity).find(text) == std::string::npos);
        CHECK(HoverOf(activity).find(text) == std::string::npos);
    }
    CHECK(FormatActivity(s, kAllOptional & ~kSettingArea, kVersion, kStart)["details"] == "Majora's Mask");
    CHECK(!FormatActivity(s, kAllOptional & ~kSettingSessionTimer, kVersion, kStart).contains("timestamps"));
}

void TestAreaHiddenByAnchorRoom() {
    PresenceState s = RandoMmInPlay();
    s.locationsHidden = true;
    CHECK(FormatActivity(s, kAllOptional, kVersion, kStart)["details"] == "Majora's Mask");
}

void TestAreaNames() {
    CHECK(CleanAreaName("Market (Child - Day)") == "Market");
    CHECK(CleanAreaName("Hyrule Field") == "Hyrule Field");
    CHECK(CleanAreaName("Unknown").empty());
    PresenceState s = RandoMmInPlay();
    s.game = Game::kOot;
    s.area = "Market (Ruins)";
    CHECK(FormatActivity(s, kAllOptional, kVersion, kStart)["details"] == "Ocarina of Time: Market");
    s.area = "Unknown";
    CHECK(FormatActivity(s, kAllOptional, kVersion, kStart)["details"] == "Ocarina of Time");
}

void TestOutsidePlay() {
    PresenceState s = RandoMmInPlay();
    s.inPlay = false;
    const auto activity = FormatActivity(s, kAllOptional, kVersion, kStart);
    CHECK(activity["details"] == "In the menus");
    CHECK(!activity.contains("state"));
    CHECK(HoverOf(activity) == "Ship2Ship v0.2.0");
}

void TestClockOnlyInTermina() {
    PresenceState s = RandoMmInPlay();
    CHECK(ClockText(2, 14 * 60 + 59) == "Day 2, 14:00");
    CHECK(ClockText(3, 6 * 60) == "Day 3, 06:00");
    s.game = Game::kOot;
    CHECK(StateOf(FormatActivity(s, kAllOptional, kVersion, kStart)).find("Day") == std::string::npos);
    s.game = Game::kMm;
    s.day = 0; // not a day of the cycle
    CHECK(StateOf(FormatActivity(s, kAllOptional, kVersion, kStart)).find("Day") == std::string::npos);
    s.day = 4; // the moon has fallen
    CHECK(StateOf(FormatActivity(s, kAllOptional, kVersion, kStart)).find("Day") == std::string::npos);
}

void TestLinkText() {
    PresenceState s = RandoMmInPlay();
    s.form = 0;
    s.adult = true;
    CHECK(StateOf(FormatActivity(s, kAllOptional, kVersion, kStart)).find("Adult Link") != std::string::npos);
    s.adult = false;
    CHECK(StateOf(FormatActivity(s, kAllOptional, kVersion, kStart)).find("Child Link") != std::string::npos);
    s.form = 4;
    CHECK(StateOf(FormatActivity(s, kAllOptional, kVersion, kStart)).find("Fierce Deity Link") != std::string::npos);
    s.maxHearts = -1;
    CHECK(StateOf(FormatActivity(s, kAllOptional, kVersion, kStart)).find("heart") == std::string::npos);
}

void TestCoopWording() {
    PresenceState s = RandoMmInPlay();
    s.coopPlayers = 1;
    CHECK(StateOf(FormatActivity(s, kAllOptional, kVersion, kStart)).find("Co-op (1 player)") != std::string::npos);
    s.coopPlayers = 0;
    CHECK(StateOf(FormatActivity(s, kAllOptional, kVersion, kStart)).find("Co-op") == std::string::npos);
}

void TestVanillaHasNoChecks() {
    PresenceState s = RandoMmInPlay();
    s.mode = "Majora's Mask";
    s.checksGotten = -1;
    s.checksTotal = -1;
    CHECK(StateOf(FormatActivity(s, kAllOptional, kVersion, kStart)).find("checks") == std::string::npos);
}

void TestPlayTimeText() {
    CHECK(PlayTimeText(0) == "0m played");
    CHECK(PlayTimeText(59) == "59m played");
    CHECK(PlayTimeText(60) == "1h 0m played");
    CHECK(PlayTimeText(34 * 60 + 12) == "34h 12m played");
}

void TestOverflowGoesToHoverText() {
    PresenceState s = RandoMmInPlay();
    s.mode = std::string(100, 'x'); // leaves room for the checks only
    const auto activity = FormatActivity(s, kAllOptional, kVersion, kStart);
    CHECK(StateOf(activity).size() <= kMaxFieldBytes);
    CHECK(StateOf(activity).find("312 / 1450 checks") != std::string::npos);
    CHECK(StateOf(activity).find("played") == std::string::npos);
    CHECK(HoverOf(activity).find("34h 12m played") != std::string::npos);
    CHECK(HoverOf(activity).find("Ship2Ship v0.2.0") == 0);
    CHECK(HoverOf(activity).size() <= kMaxFieldBytes);
}

void TestTrimming() {
    CHECK(TrimToBytes("short", kMaxFieldBytes) == "short");
    const std::string longText(300, 'a');
    CHECK(TrimToBytes(longText, kMaxFieldBytes).size() <= kMaxFieldBytes);
    CHECK(TrimToBytes(longText, kMaxFieldBytes).size() >= kMaxFieldBytes - 3);
    // Two-byte characters must not be cut in half.
    std::string accents;
    for (int i = 0; i < 100; ++i)
        accents += "\xC3\xA9";
    const std::string trimmed = TrimToBytes(accents, kMaxFieldBytes);
    CHECK(trimmed.size() <= kMaxFieldBytes);
    CHECK(trimmed.substr(trimmed.size() - 3) == kEllipsis);
    CHECK((trimmed.size() - 3) % 2 == 0);
    PresenceState s = RandoMmInPlay();
    s.area = longText;
    CHECK(FormatActivity(s, kAllOptional, kVersion, kStart)["details"].get<std::string>().size() <= kMaxFieldBytes);
}

void TestNothingPrivateIsSent() {
    PresenceState s = RandoMmInPlay();
    const auto activity = FormatActivity(s, kAllOptional, kVersion, kStart);
    for (const auto& item : activity.items())
        CHECK(item.key() == "details" || item.key() == "state" || item.key() == "timestamps" ||
              item.key() == "assets" || item.key() == "buttons");
    CHECK(activity["assets"].size() == 2); // the large image and its text; no small image
}

void TestModeLabels() {
    using ComboRando::ComboMode;
    CHECK(ModeLabel(ComboMode::Rando(ComboRando::COMBO_GAMES_BOTH)) == "Randomizer");
    CHECK(ModeLabel(ComboMode::Rando(ComboRando::COMBO_GAMES_OOT)) == "Randomizer");
    CHECK(ModeLabel(ComboMode::Rando(ComboRando::COMBO_GAMES_MM)) == "Randomizer");
    CHECK(ModeLabel(ComboMode::VanillaOot(ComboRando::OOT_QUEST_NORMAL)) == "Ocarina of Time");
    CHECK(ModeLabel(ComboMode::VanillaOot(ComboRando::OOT_QUEST_MQ)) == "Ocarina of Time");
    CHECK(ModeLabel(ComboMode::VanillaMm()) == "Majora's Mask");
    const int options = ComboRando::BackToBack::DefaultOptions();
    CHECK(ModeLabel(ComboMode::BackToBack(ComboRando::OOT_QUEST_NORMAL, options)) == "Back to back");
    CHECK(ModeLabel(ComboMode::BackToBack(ComboRando::OOT_QUEST_MQ, options)) == "Back to back");
}

void TestFrames() {
    const std::string frame = EncodeFrame(kOpFrame, "{}");
    CHECK(frame.size() == kFrameHeaderBytes + 2);
    CHECK(frame.substr(0, kFrameHeaderBytes) == std::string("\x01\x00\x00\x00\x02\x00\x00\x00", kFrameHeaderBytes));
    CHECK(frame.substr(kFrameHeaderBytes) == "{}");

    std::array<unsigned char, kFrameHeaderBytes> header = { 0x04, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00 };
    FrameHeader decoded;
    CHECK(DecodeFrameHeader(header, &decoded));
    CHECK(decoded.opcode == kOpPong);
    CHECK(decoded.length == 256);

    header = { 0x01, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF };
    CHECK(!DecodeFrameHeader(header, &decoded));
}

void TestIpcPaths() {
    CHECK(WindowsPipeName(0) == "\\\\.\\pipe\\discord-ipc-0");
    CHECK(WindowsPipeName(9) == "\\\\.\\pipe\\discord-ipc-9");

    const std::map<std::string, std::string> env = { { "XDG_RUNTIME_DIR", "/run/user/1000/" }, { "TMPDIR", "/tmp" } };
    const auto lookup = [&env](const char* name) -> const char* {
        const auto it = env.find(name);
        return it == env.end() ? nullptr : it->second.c_str();
    };
    const auto paths = UnixSocketPaths(lookup);
    CHECK(paths.front() == "/run/user/1000/discord-ipc-0");
    const auto has = [&paths](const std::string& path) {
        return std::find(paths.begin(), paths.end(), path) != paths.end();
    };
    CHECK(has("/run/user/1000/app/com.discordapp.Discord/discord-ipc-3"));
    CHECK(has("/run/user/1000/snap.discord/discord-ipc-9"));
    CHECK(has("/tmp/discord-ipc-0"));
    CHECK(has("/tmp/snap.discord/discord-ipc-0"));
    CHECK(!has("/run/user/1000/discord-ipc-10"));
    // TMPDIR and the /tmp fallback name the same place: listed once.
    CHECK(std::count(paths.begin(), paths.end(), std::string("/tmp/discord-ipc-0")) == 1);
    CHECK(paths.size() == 2 * 3 * kIpcSlots);

    const auto none = UnixSocketPaths([](const char*) -> const char* { return nullptr; });
    CHECK(none.size() == 3 * kIpcSlots);
    CHECK(none.front() == "/tmp/discord-ipc-0");
}

} // namespace

int main() {
    TestEverythingOn();
    TestGameAndModeAreAlwaysShown();
    TestEachSwitchDropsItsField();
    TestAreaHiddenByAnchorRoom();
    TestAreaNames();
    TestOutsidePlay();
    TestClockOnlyInTermina();
    TestLinkText();
    TestCoopWording();
    TestVanillaHasNoChecks();
    TestPlayTimeText();
    TestOverflowGoesToHoverText();
    TestTrimming();
    TestNothingPrivateIsSent();
    TestModeLabels();
    TestFrames();
    TestIpcPaths();
    if (gFailures == 0)
        std::printf("all Discord presence checks passed\n");
    return gFailures == 0 ? 0 : 1;
}
