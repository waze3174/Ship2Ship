// combo/tests/ComboApConsoleTest.cpp
//
// Tests for the Archipelago console's text model (combo/net/ComboApConsole.h and ComboApConsoleInput.h): how a server
// message becomes coloured lines, what the scrollback keeps, where a line wraps, and what typed input means. Build and
// run: `cmake --build <dir> --target comboApConsoleTest`, then run the exe; it exits 0 when every check passes.
#include <map>
#include <set>
#include <string>
#include <vector>

#include "net/ComboApConsole.h"
#include "net/ComboApConsoleInput.h"
#include "ComboTestCheck.h"

namespace {

using namespace ComboApConsole;

constexpr int kSelf = 1;
constexpr int kOther = 2;
constexpr int kGroupWithSelf = 3;

// A room with two players (slot 1 is us) and a group that includes us.
class FakeRoom final : public NameSource {
  public:
    std::optional<std::string> PlayerName(int slot) const override {
        const auto it = players.find(slot);
        return it == players.end() ? std::nullopt : std::optional<std::string>(it->second);
    }
    std::optional<std::string> ItemName(int64_t id, int owner) const override {
        const auto it = items.find({ owner, id });
        return it == items.end() ? std::nullopt : std::optional<std::string>(it->second);
    }
    std::optional<std::string> LocationName(int64_t id, int owner) const override {
        const auto it = locations.find({ owner, id });
        return it == locations.end() ? std::nullopt : std::optional<std::string>(it->second);
    }
    bool ConcernsSelf(int slot) const override {
        return slot == kSelf || slot == kGroupWithSelf;
    }

    std::map<int, std::string> players = { { kSelf, "Link" }, { kOther, "Zelda" }, { kGroupWithSelf, "Everyone" } };
    std::map<std::pair<int, int64_t>, std::string> items = { { { kSelf, 100 }, "Hookshot" },
                                                             { { kOther, 200 }, "Bomb Bag" } };
    std::map<std::pair<int, int64_t>, std::string> locations = { { { kSelf, 300 }, "Deku Tree Chest" },
                                                                 { { kOther, 400 }, "Kakariko Roof" } };
};

Part Text(const std::string& text) {
    Part part;
    part.text = text;
    return part;
}
Part Typed(const std::string& type, const std::string& text, int player = 0, unsigned flags = 0) {
    Part part;
    part.type = type;
    part.text = text;
    part.player = player;
    part.flags = flags;
    return part;
}

// The one line a message renders to.
Line Render(const std::vector<Part>& parts) {
    const std::vector<Line> lines = RenderParts(parts, FakeRoom());
    return lines.empty() ? Line() : lines[0];
}

// The colour of the span whose text is `text`, or kDefaultColor + 1 when there is none.
Color ColorOf(const Line& line, const std::string& text) {
    for (const Span& span : line.spans) {
        if (span.text == text) {
            return span.color;
        }
    }
    return kDefaultColor + 1;
}

// ---- colours ----

void TestItemColorsFollowTheFlags() {
    CHECK(ItemColor(0) == kCyan);
    CHECK(ItemColor(kFlagProgression) == kPlum);
    CHECK(ItemColor(kFlagUseful) == kSlateBlue);
    CHECK(ItemColor(kFlagTrap) == kSalmon);
    CHECK(ItemColor(kFlagProgression | kFlagUseful) == kPlum);
    CHECK(ItemColor(kFlagUseful | kFlagTrap) == kSlateBlue);
    CHECK(ItemColor(0b1000) == kCyan);
}

void TestHintStatusColors() {
    CHECK(HintStatusColor(kHintFound) == kGreen);
    CHECK(HintStatusColor(kHintUnspecified) == kWhite);
    CHECK(HintStatusColor(kHintNoPriority) == kSlateBlue);
    CHECK(HintStatusColor(kHintAvoid) == kSalmon);
    CHECK(HintStatusColor(kHintPriority) == kPlum);
    CHECK(HintStatusColor(55) == kRed);
}

void TestNamedColors() {
    CHECK(ColorByNames("red") == kRed);
    CHECK(ColorByNames("bold;green") == kGreen);
    CHECK(ColorByNames("salmon;bold") == kSalmon);
    CHECK(ColorByNames("underline") == kDefaultColor);
    CHECK(ColorByNames("") == kDefaultColor);
    CHECK(ColorByNames("chartreuse") == kDefaultColor);
}

// ---- rendering ----

void TestEveryPartTypeRendersWithItsColor() {
    const Line line = Render({ Text("a "), Typed("player_id", "1"), Text(" b "), Typed("player_id", "2"), Text(" c "),
                               Typed("item_id", "100", kSelf, kFlagProgression), Text(" d "),
                               Typed("location_id", "400", kOther), Text(" e "), Typed("entrance_name", "Door"),
                               Typed("player_name", "Ganon"), Typed("item_name", "Rupee", 0, kFlagTrap),
                               Typed("location_name", "Spot"), Typed("color", "alert", 0, 0) });
    CHECK(line.PlainText() == "a Link b Zelda c Hookshot d Kakariko Roof e DoorGanonRupeeSpotalert");
    CHECK(ColorOf(line, "a ") == kDefaultColor);
    CHECK(ColorOf(line, "Link") == kMagenta);
    CHECK(ColorOf(line, "Zelda") == kYellow);
    CHECK(ColorOf(line, "Hookshot") == kPlum);
    CHECK(ColorOf(line, "Kakariko Roof") == kGreen);
    CHECK(ColorOf(line, "Door") == kBlue);
    CHECK(ColorOf(line, "Ganon") == kYellow);
    CHECK(ColorOf(line, "Rupee") == kSalmon);
    CHECK(ColorOf(line, "Spot") == kGreen);
}

void TestAColorPartHonoursItsColor() {
    Part part = Typed("color", "careful");
    part.color = "red;bold";
    CHECK(Render({ part }).spans[0].color == kRed);
    part.color = "mystery";
    CHECK(Render({ part }).spans[0].color == kDefaultColor);
}

void TestPlainTextAndUnknownTypesAreUncolored() {
    Part colored = Text("hello");
    colored.color = "red"; // only a "color" part is colored, as in the text client
    const Line line = Render({ colored, Typed("future_type", " world") });
    CHECK(line.PlainText() == "hello world");
    CHECK(line.spans[0].color == kDefaultColor);
    CHECK(line.spans[1].color == kDefaultColor);
}

void TestAGroupConcernsUsTooAndOthersDoNot() {
    CHECK(Render({ Typed("player_id", "3") }).spans[0].color == kMagenta);
    CHECK(Render({ Typed("player_id", "2") }).spans[0].color == kYellow);
}

void TestAnItemOfAnotherPlayersGameIsNamedInThatGame() {
    // Item 200 exists in slot 2's game only: the owner slot of the part picks the name.
    CHECK(Render({ Typed("item_id", "200", kOther) }).PlainText() == "Bomb Bag");
    CHECK(Render({ Typed("item_id", "200", kSelf) }).PlainText() == "Unknown item (ID:200)");
}

void TestUnknownIdsAreNamedByTheirId() {
    CHECK(Render({ Typed("player_id", "9") }).PlainText() == "Player 9");
    CHECK(Render({ Typed("item_id", "777", kSelf) }).PlainText() == "Unknown item (ID:777)");
    CHECK(Render({ Typed("location_id", "888", kOther) }).PlainText() == "Unknown location (ID:888)");
    CHECK(Render({ Typed("item_id", "777", kSelf, kFlagTrap) }).spans[0].color == kSalmon);
    CHECK(Render({ Typed("location_id", "888", kOther) }).spans[0].color == kGreen);
}

void TestAnIdThatIsNotANumberIsShownAsGiven() {
    CHECK(Render({ Typed("player_id", "abc") }).PlainText() == "abc");
    CHECK(Render({ Typed("item_id", "12x", kSelf) }).PlainText() == "Unknown item (ID:12x)");
    CHECK(Render({ Typed("location_id", "", kSelf) }).PlainText() == "Unknown location (ID:)");
    CHECK(Render({ Typed("item_id", "-5", kSelf) }).PlainText() == "Unknown item (ID:-5)");
}

void TestHintStatusPartsAreColoredByStatus() {
    Part found = Typed("hint_status", "(found)");
    found.hintStatus = kHintFound;
    Part priority = Typed("hint_status", "(priority)");
    priority.hintStatus = kHintPriority;
    const Line line = Render({ found, Text(" "), priority });
    CHECK(ColorOf(line, "(found)") == kGreen);
    CHECK(ColorOf(line, "(priority)") == kPlum);
}

// One realistic message of every PrintJSON type the server sends, as the text it must read.
void TestEveryMessageTypeReadsLikeTheTextClient() {
    const struct {
        const char* type;
        std::vector<Part> parts;
        const char* text;
    } kCases[] = {
        { "ItemSend",
          { Typed("player_id", "2"), Text(" sent "), Typed("item_id", "100", kSelf, kFlagUseful), Text(" to "),
            Typed("player_id", "1"), Text(" ("), Typed("location_id", "400", kOther), Text(")") },
          "Zelda sent Hookshot to Link (Kakariko Roof)" },
        { "ItemCheat",
          { Typed("player_id", "1"), Text(" was given "), Typed("item_id", "100", kSelf), Text(" by the server") },
          "Link was given Hookshot by the server" },
        { "Hint",
          { Text("[Hint]: "), Typed("player_id", "1"), Text("'s "), Typed("item_id", "100", kSelf, kFlagProgression),
            Text(" is at "), Typed("location_id", "400", kOther), Text(" in "), Typed("player_id", "2"),
            Text("'s World. ") },
          "[Hint]: Link's Hookshot is at Kakariko Roof in Zelda's World. " },
        { "Join",
          { Typed("player_id", "2"), Text(" (Team #1) has joined the game") },
          "Zelda (Team #1) has joined the game" },
        { "Part",
          { Typed("player_id", "2"), Text(" (Team #1) has left the game") },
          "Zelda (Team #1) has left the game" },
        { "Chat", { Text("Zelda: hello there") }, "Zelda: hello there" },
        { "ServerChat", { Text("[Server]: restarting soon") }, "[Server]: restarting soon" },
        { "Tutorial", { Text("Welcome to Archipelago!") }, "Welcome to Archipelago!" },
        { "TagsChanged",
          { Typed("player_id", "2"), Text(" added the tag DeathLink") },
          "Zelda added the tag DeathLink" },
        { "CommandResult", { Text("5 items remaining.") }, "5 items remaining." },
        { "AdminCommandResult",
          { Text("Forced "), Typed("player_id", "2"), Text(" to release") },
          "Forced Zelda to release" },
        { "Goal", { Typed("player_id", "2"), Text(" has completed their goal.") }, "Zelda has completed their goal." },
        { "Release",
          { Typed("player_id", "2"), Text(" has released all remaining items from their world.") },
          "Zelda has released all remaining items from their world." },
        { "Collect",
          { Typed("player_id", "1"), Text(" has collected their items from other worlds.") },
          "Link has collected their items from other worlds." },
        { "Countdown", { Text("[Server]: 3") }, "[Server]: 3" },
    };
    for (const auto& one : kCases) {
        const std::vector<Line> lines = RenderParts(one.parts, FakeRoom());
        CHECK(lines.size() == 1);
        CHECK(lines.size() == 1 && lines[0].PlainText() == one.text);
    }
}

void TestAnItemSendColorsEachPartLikeTheTextClient() {
    const Line line =
        Render({ Typed("player_id", "1"), Text(" sent "), Typed("item_id", "100", kSelf, kFlagProgression),
                 Text(" to "), Typed("player_id", "2"), Text(" ("), Typed("location_id", "300", kSelf), Text(")") });
    CHECK(ColorOf(line, "Link") == kMagenta);
    CHECK(ColorOf(line, "Hookshot") == kPlum);
    CHECK(ColorOf(line, "Zelda") == kYellow);
    CHECK(ColorOf(line, "Deku Tree Chest") == kGreen);
}

void TestAMultiLineResultBecomesSeveralLines() {
    const std::vector<Line> lines = RenderParts({ Text("first\nsecond\n\nfourth") }, FakeRoom());
    CHECK(lines.size() == 4);
    CHECK(lines.size() == 4 && lines[0].PlainText() == "first");
    CHECK(lines.size() == 4 && lines[1].PlainText() == "second");
    CHECK(lines.size() == 4 && lines[2].spans.empty());
    CHECK(lines.size() == 4 && lines[3].PlainText() == "fourth");
}

void TestANewlineInsideAColoredPartKeepsItsColorOnBothLines() {
    Part part = Typed("color", "one\ntwo");
    part.color = "green";
    const std::vector<Line> lines = RenderParts({ part }, FakeRoom());
    CHECK(lines.size() == 2);
    CHECK(lines.size() == 2 && lines[0].spans[0].color == kGreen && lines[1].spans[0].color == kGreen);
}

// ---- safety ----

void TestControlCharactersAreRemoved() {
    CHECK(SafeText(std::string("a\x01"
                               "b\x07"
                               "c\x7F"
                               "d")) == "abcd");
    CHECK(SafeText("tab\there") == "tab here");
    CHECK(SafeText(std::string("nul\0inside", 10)) == "nulinside");
    CHECK(SafeText("cr\rlf") == "crlf");
}

void TestEscapeSequencesAreRemovedWhole() {
    CHECK(SafeText("\x1b[31mred\x1b[0m") == "red");
    CHECK(SafeText("\x1b[2J\x1b[Hscreen") == "screen");
    CHECK(SafeText("a\x1b"
                   "b") == "ab");
    CHECK(SafeText("trailing\x1b[") == "trailing");
}

void TestC1ControlsAndTextDirectionOverridesAreRemoved() {
    CHECK(SafeText("a\xC2\x85"
                   "b") == "ab"); // U+0085
    CHECK(SafeText("a\xC2\x9F"
                   "b") == "ab"); // U+009F
    CHECK(SafeText("a\xE2\x80\xAE"
                   "b") == "ab"); // U+202E right-to-left override
    CHECK(SafeText("a\xE2\x81\xA7"
                   "b") == "ab"); // U+2067
}

void TestPrintableUtf8IsKept() {
    CHECK(SafeText("Zelda\xC3\xA9 \xE3\x83\xAA\xE3\x83\xB3\xE3\x82\xAF") ==
          "Zelda\xC3\xA9 \xE3\x83\xAA\xE3\x83\xB3\xE3\x82\xAF");
    CHECK(SafeText("\xC2\xA0nbsp") == "\xC2\xA0nbsp"); // U+00A0 is not a control
    CHECK(SafeText("\xE2\x80\x94 dash") == "\xE2\x80\x94 dash");
}

void TestARenderedLineHasNoControlCharacters() {
    const std::vector<Line> lines = RenderParts({ Text("a\x1b[31mb"), Typed("player_name", "x\x01y") }, FakeRoom());
    CHECK(lines.size() == 1 && lines[0].PlainText() == "abxy");
}

void TestALongLineIsCutAtACharacterBoundary() {
    const std::string unit = "\xE3\x83\xAA"; // 3 bytes
    std::string text;
    while (text.size() < kMaxLineBytes + 10) {
        text += unit;
    }
    const std::vector<Line> lines = Normalize(PlainLine(text));
    CHECK(lines.size() == 1);
    const std::string shown = lines[0].PlainText();
    CHECK(shown.size() <= kMaxLineBytes + 3);
    CHECK(shown.size() >= 3 && shown.substr(shown.size() - 3) == kCutMarker);
    const std::string kept = shown.substr(0, shown.size() - 3);
    CHECK(kept.size() % 3 == 0); // never half a character
}

void TestAShortLineIsNotCut() {
    const std::vector<Line> lines = Normalize(PlainLine(std::string(kMaxLineBytes, 'x')));
    CHECK(lines[0].PlainText().size() == kMaxLineBytes);
}

// ---- the scrollback ----

std::vector<std::string> Texts(const Log& log) {
    std::vector<std::string> texts;
    log.Visit(0, [&](uint64_t, const Line& line) { texts.push_back(line.PlainText()); });
    return texts;
}

void TestTheLogKeepsLinesInOrder() {
    Log log(10);
    log.Append(PlainLine("one"));
    log.Append(std::vector<Line>{ PlainLine("two"), PlainLine("three") });
    CHECK((Texts(log) == std::vector<std::string>{ "one", "two", "three" }));
    CHECK(log.Size() == 3);
}

void TestTheLogDropsTheOldestPastItsCap() {
    Log log(3);
    for (int i = 1; i <= 5; ++i) {
        log.Append(PlainLine(std::to_string(i)));
    }
    CHECK((Texts(log) == std::vector<std::string>{ "3", "4", "5" }));
    CHECK(log.Size() == 3);
}

void TestTheDefaultCapIsTwoThousandLines() {
    Log log;
    for (size_t i = 0; i < kMaxLines + 500; ++i) {
        log.Append(PlainLine("line " + std::to_string(i)));
    }
    CHECK(log.Size() == kMaxLines);
    CHECK(Texts(log).front() == "line 500");
    CHECK(Texts(log).back() == "line " + std::to_string(kMaxLines + 499));
}

void TestVisitGivesOnlyTheLinesAfterASequence() {
    Log log(10);
    std::vector<uint64_t> sequences;
    log.Append({ PlainLine("a"), PlainLine("b"), PlainLine("c") });
    log.Visit(0, [&](uint64_t sequence, const Line&) { sequences.push_back(sequence); });
    CHECK(sequences.size() == 3);
    std::vector<std::string> later;
    log.Append(PlainLine("d"));
    const bool dropped =
        log.Visit(sequences.back(), [&](uint64_t, const Line& line) { later.push_back(line.PlainText()); });
    CHECK(!dropped);
    CHECK((later == std::vector<std::string>{ "d" }));
}

void TestTheRevisionChangesWithEveryChange() {
    Log log(10);
    const uint64_t start = log.Revision();
    log.Append(PlainLine("a"));
    const uint64_t added = log.Revision();
    log.Clear();
    CHECK(added != start);
    CHECK(log.Revision() != added);
}

void TestClearEmptiesTheLogAndTellsAReaderToDropWhatItHolds() {
    Log log(10);
    log.Append({ PlainLine("a"), PlainLine("b") });
    uint64_t last = 0;
    log.Visit(0, [&](uint64_t sequence, const Line&) { last = sequence; });
    log.Clear();
    CHECK(log.Size() == 0);
    log.Append(PlainLine("c"));
    std::vector<std::string> seen;
    const bool dropped = log.Visit(last, [&](uint64_t, const Line& line) { seen.push_back(line.PlainText()); });
    CHECK(dropped);
    CHECK((seen == std::vector<std::string>{ "c" }));
    // A reader that has caught up with the new line is not told to drop again.
    uint64_t now = 0;
    log.Visit(0, [&](uint64_t sequence, const Line&) { now = sequence; });
    CHECK(!log.Visit(now, [](uint64_t, const Line&) {}));
}

void TestAReaderWithNothingIsNeverToldToDrop() {
    Log log(10);
    log.Append(PlainLine("a"));
    log.Clear();
    CHECK(!log.Visit(0, [](uint64_t, const Line&) {}));
}

void TestPlainTextJoinsTheLines() {
    Log log(10);
    log.Append({ Line{ { { kRed, "a" }, { kGreen, "b" } } }, PlainLine("c") });
    CHECK(log.PlainText() == "ab\nc\n");
}

// ---- wrapping ----

// One pixel per byte: a monospace font of width 1.
float OnePerByte(const char* begin, const char* end) {
    return static_cast<float>(end - begin);
}

std::string RowText(const Line& line, const Row& row) {
    std::string text;
    for (const Piece& piece : row) {
        text += line.spans[piece.span].text.substr(piece.begin, piece.length);
    }
    return text;
}

std::vector<std::string> Wrapped(const Line& line, float width) {
    std::vector<std::string> out;
    for (const Row& row : WrapLine(line, width, OnePerByte)) {
        out.push_back(RowText(line, row));
    }
    return out;
}

using Texts_ = std::vector<std::string>;

void TestALineThatFitsIsOneRow() {
    CHECK((Wrapped(PlainLine("hello world"), 20) == Texts_{ "hello world" }));
}

void TestAnEmptyLineIsOneEmptyRow() {
    CHECK(WrapLine(Line(), 10, OnePerByte).size() == 1);
    CHECK(WrapLine(Line(), 10, OnePerByte)[0].empty());
}

void TestWordsWrapWhole() {
    CHECK((Wrapped(PlainLine("aaa bbb ccc"), 7) == Texts_{ "aaa bbb", "ccc" }));
    CHECK((Wrapped(PlainLine("aaa bbb ccc"), 5) == Texts_{ "aaa", "bbb", "ccc" }));
}

void TestTheSpacesAtABreakAreDropped() {
    CHECK((Wrapped(PlainLine("aaa    bbb"), 4) == Texts_{ "aaa", "bbb" }));
}

void TestAWordWiderThanTheRowIsCutBetweenCharacters() {
    CHECK((Wrapped(PlainLine("abcdefghij"), 4) == Texts_{ "abcd", "efgh", "ij" }));
    CHECK((Wrapped(PlainLine("ab abcdefghij"), 4) == Texts_{ "ab", "abcd", "efgh", "ij" }));
}

void TestACutNeverSplitsAUtf8Character() {
    const std::string line = "\xE3\x83\xAA\xE3\x83\xB3\xE3\x82\xAF\xE3\x83\xAA"; // four 3-byte characters
    const auto rows = Wrapped(PlainLine(line), 6); // 1 pixel per byte: two characters a row
    CHECK(rows.size() == 2);
    CHECK(rows.size() == 2 && rows[0].size() == 6 && rows[1].size() == 6);
}

void TestPiecesKeepTheirSpansAndPositions() {
    const Line line{ { { kRed, "ab " }, { kGreen, "cd" } } };
    const std::vector<Row> rows = WrapLine(line, 20, OnePerByte);
    CHECK(rows.size() == 1);
    CHECK(rows[0].size() == 2);
    CHECK(rows[0][0].span == 0 && rows[0][0].x == 0.0f && rows[0][0].length == 3);
    CHECK(rows[0][1].span == 1 && rows[0][1].x == 3.0f && rows[0][1].length == 2);
}

void TestAWordContinuesAcrossSpansWithoutAGap() {
    const Line line{ { { kRed, "ab" }, { kGreen, "cd" } } }; // "abcd" in two colours
    const std::vector<Row> rows = WrapLine(line, 20, OnePerByte);
    CHECK(rows.size() == 1 && rows[0].size() == 2 && rows[0][1].x == 2.0f);
}

void TestEveryByteIsShownExactlyOnceWhenNothingIsDropped() {
    const Line line{ { { kRed, "the quick brown " }, { kGreen, "fox jumps over the lazy dog" } } };
    for (float width : { 5.0f, 8.0f, 13.0f, 21.0f, 100.0f }) {
        std::string joined;
        for (const Row& row : WrapLine(line, width, OnePerByte)) {
            joined += RowText(line, row);
        }
        std::string expected;
        for (char c : line.PlainText()) {
            if (c != ' ') {
                expected.push_back(c);
            }
        }
        std::string shown;
        for (char c : joined) {
            if (c != ' ') {
                shown.push_back(c);
            }
        }
        CHECK(shown == expected);
    }
}

void TestNoRowIsWiderThanTheWidth() {
    const Line line{ { { kRed, "the quick brown fox " }, { kGreen, "jumps over the lazy dog and keeps running" } } };
    for (float width : { 4.0f, 9.0f, 16.0f, 30.0f }) {
        for (const Row& row : WrapLine(line, width, OnePerByte)) {
            float right = 0.0f;
            for (const Piece& piece : row) {
                right = std::max(right, piece.x + static_cast<float>(piece.length));
            }
            CHECK(right <= width);
        }
    }
}

// ---- input ----

void TestEmptyInputDoesNothing() {
    CHECK(ParseInput("").kind == InputKind::kEmpty);
    CHECK(ParseInput("   \t ").kind == InputKind::kEmpty);
}

void TestBangCommandsAndChatAreSentAsSaid() {
    const ParsedInput hint = ParseInput("  !hint Hookshot  ");
    CHECK(hint.kind == InputKind::kSay && hint.say == "!hint Hookshot");
    const ParsedInput chat = ParseInput("hello everyone");
    CHECK(chat.kind == InputKind::kSay && chat.say == "hello everyone");
    CHECK(ParseInput("!release").say == "!release");
    CHECK(ParseInput("!").kind == InputKind::kSay);
}

void TestSlashCommandsAreClientCommands() {
    const ParsedInput received = ParseInput("/received");
    CHECK(received.kind == InputKind::kClient && received.command == "received" && received.args.empty());
    const ParsedInput connect = ParseInput("/CONNECT  localhost:38281   secret ");
    CHECK(connect.kind == InputKind::kClient && connect.command == "connect");
    CHECK(connect.args == "localhost:38281   secret");
    const ParsedInput missing = ParseInput("/missing Deku Tree");
    CHECK(missing.command == "missing" && missing.args == "Deku Tree");
    const ParsedInput bare = ParseInput("/");
    CHECK(bare.kind == InputKind::kClient && bare.command.empty());
}

void TestTypedControlCharactersNeverReachTheServer() {
    // A server rejects a Say that is not printable text.
    CHECK(ParseInput("hello[31m world").say == "hello world");
    CHECK(ParseInput("").kind == InputKind::kEmpty);
    CHECK(ParseInput("/received").command == "received");
}

void TestAChatLineThatMentionsASlashIsStillChat() {
    CHECK(ParseInput("go left/right").kind == InputKind::kSay);
}

void TestFirstWordSplit() {
    const auto both = SplitWord("host:1 pass word");
    CHECK(both.first == "host:1" && both.second == "pass word");
    const auto one = SplitWord("host:1");
    CHECK(one.first == "host:1" && one.second.empty());
    CHECK(SplitWord("").first.empty());
}

void TestOnlyConnectionCommandsWorkOffline() {
    CHECK(WorksOffline(ParseInput("/connect host")));
    CHECK(WorksOffline(ParseInput("/disconnect")));
    CHECK(WorksOffline(ParseInput("/help")));
    CHECK(WorksOffline(ParseInput("/clear")));
    CHECK(WorksOffline(ParseInput("")));
    CHECK(!WorksOffline(ParseInput("!hint x")));
    CHECK(!WorksOffline(ParseInput("hello")));
    CHECK(!WorksOffline(ParseInput("/received")));
    CHECK(!WorksOffline(ParseInput("/unknown")));
}

// ---- history ----

void TestUpGoesBackAndDownForward() {
    InputHistory history;
    history.Push("one");
    history.Push("two");
    history.Push("three");
    CHECK(history.Older("") == "three");
    CHECK(history.Older("") == "two");
    CHECK(history.Older("") == "one");
    CHECK(history.Older("") == "one"); // stays on the oldest
    CHECK(history.Newer("") == "two");
    CHECK(history.Newer("") == "three");
}

void TestGoingPastTheNewestGivesBackTheDraft() {
    InputHistory history;
    history.Push("sent");
    CHECK(history.Older("half typed") == "sent");
    CHECK(history.Newer("sent") == "half typed");
    CHECK(history.Newer("half typed") == "half typed"); // not browsing: unchanged
}

void TestSendingStopsBrowsing() {
    InputHistory history;
    history.Push("a");
    history.Push("b");
    history.Older("");
    history.Push("c");
    CHECK(history.Older("") == "c");
}

void TestHistoryIgnoresEmptyAndRepeatedLines() {
    InputHistory history;
    history.Push("");
    history.Push("same");
    history.Push("same");
    CHECK(history.size() == 1);
    history.Push("other");
    history.Push("same");
    CHECK(history.size() == 3);
}

void TestHistoryWithNothingLeavesTheBoxAlone() {
    InputHistory history;
    CHECK(history.Older("typing") == "typing");
    CHECK(history.Newer("typing") == "typing");
}

void TestHistoryDropsTheOldestPastItsCap() {
    InputHistory history(2);
    history.Push("a");
    history.Push("b");
    history.Push("c");
    CHECK(history.size() == 2);
    CHECK(history.Older("") == "c");
    CHECK(history.Older("") == "b");
    CHECK(history.Older("") == "b");
}

// ---- the answers ----

void TestHelpNamesEveryCommand() {
    std::string text;
    for (const Line& line : FormatHelp()) {
        text += line.PlainText() + "\n";
    }
    for (const char* command : { "/help", "/connect", "/disconnect", "/received", "/missing", "/checked", "/items",
                                 "/locations", "/ready", "/clear", "!hint", "!remaining" }) {
        CHECK(text.find(command) != std::string::npos);
    }
}

void TestReceivedListsEachItemWithItsSenderAndPlace() {
    const std::vector<ReceivedEntry> entries = {
        { 0, "Hookshot (OoT)", kFlagProgression, "Kakariko Roof", "Zelda" },
        { 1, "Rupee (5) (MM)", 0, "", "Server" },
    };
    const std::vector<Line> lines = FormatReceived(entries, "");
    CHECK(lines.size() == 3);
    CHECK(lines.size() == 3 && lines[0].PlainText() == "2 received items, sorted by time:");
    CHECK(lines.size() == 3 && lines[1].PlainText() == "1. Hookshot (OoT) from Kakariko Roof by Zelda");
    CHECK(lines.size() == 3 && lines[2].PlainText() == "2. Rupee (5) (MM) by Server");
    CHECK(lines.size() == 3 && ColorOf(lines[1], "Hookshot (OoT)") == kPlum);
    CHECK(lines.size() == 3 && ColorOf(lines[1], "Kakariko Roof") == kGreen);
    CHECK(lines.size() == 3 && ColorOf(lines[1], "Zelda") == kYellow);
}

void TestReceivedFiltersByItemNameIgnoringCase() {
    const std::vector<ReceivedEntry> entries = { { 0, "Hookshot (OoT)", 0, "A", "P" },
                                                 { 1, "Bow (OoT)", 0, "B", "P" } };
    const std::vector<Line> lines = FormatReceived(entries, "hook");
    CHECK(lines.size() == 2);
    CHECK(lines.size() == 2 && lines[0].PlainText() == "1 received items, sorted by time:");
}

void TestReceivedWithNothingSaysSo() {
    const std::vector<Line> lines = FormatReceived({}, "");
    CHECK(lines.size() == 1 && lines[0].PlainText() == "0 received items, sorted by time:");
}

void TestMissingAndCheckedSplitTheLocations() {
    const std::vector<std::string> all = { "Chest B", "Chest A", "Chest C" };
    const std::set<std::string> checked = { "Chest B" };
    const std::vector<Line> missing = FormatChecks(false, all, checked, "");
    CHECK(missing.size() == 3);
    CHECK(missing.size() == 3 && missing[0].PlainText() == "Missing locations (2)");
    CHECK(missing.size() == 3 && missing[1].PlainText() == "Chest A"); // sorted
    CHECK(missing.size() == 3 && missing[2].PlainText() == "Chest C");
    const std::vector<Line> done = FormatChecks(true, all, checked, "");
    CHECK(done.size() == 2);
    CHECK(done.size() == 2 && done[0].PlainText() == "Checked locations (1)");
    CHECK(done.size() == 2 && done[1].PlainText() == "Chest B");
}

void TestMissingWithAFilter() {
    const std::vector<std::string> all = { "Deku Chest", "Dodongo Chest", "Deku Heart" };
    const std::vector<Line> lines = FormatChecks(false, all, {}, "deku");
    CHECK(lines.size() == 3 && lines[0].PlainText() == "Missing locations (2)");
}

void TestNoLocationListSaysToConnect() {
    const std::vector<Line> lines = FormatChecks(false, {}, {}, "");
    CHECK(lines.back().PlainText().find("connect") != std::string::npos);
}

void TestALongListIsCutWithAHowToNarrowIt() {
    std::vector<std::string> names;
    for (size_t i = 0; i < kMaxListedLines + 25; ++i) {
        names.push_back("Name " + std::to_string(1000 + i));
    }
    const std::vector<Line> lines = FormatNames("Locations", names, "", "/locations", kGreen);
    CHECK(lines.size() == 1 + kMaxListedLines + 1);
    CHECK(lines.back().PlainText().find("25 more") != std::string::npos);
    CHECK(lines.back().PlainText().find("/locations") != std::string::npos);
    CHECK(lines[0].PlainText() == "Locations (" + std::to_string(kMaxListedLines + 25) + ")");
}

} // namespace

int main() {
    TestItemColorsFollowTheFlags();
    TestHintStatusColors();
    TestNamedColors();
    TestEveryPartTypeRendersWithItsColor();
    TestAColorPartHonoursItsColor();
    TestPlainTextAndUnknownTypesAreUncolored();
    TestAGroupConcernsUsTooAndOthersDoNot();
    TestAnItemOfAnotherPlayersGameIsNamedInThatGame();
    TestUnknownIdsAreNamedByTheirId();
    TestAnIdThatIsNotANumberIsShownAsGiven();
    TestHintStatusPartsAreColoredByStatus();
    TestEveryMessageTypeReadsLikeTheTextClient();
    TestAnItemSendColorsEachPartLikeTheTextClient();
    TestAMultiLineResultBecomesSeveralLines();
    TestANewlineInsideAColoredPartKeepsItsColorOnBothLines();
    TestControlCharactersAreRemoved();
    TestEscapeSequencesAreRemovedWhole();
    TestC1ControlsAndTextDirectionOverridesAreRemoved();
    TestPrintableUtf8IsKept();
    TestARenderedLineHasNoControlCharacters();
    TestALongLineIsCutAtACharacterBoundary();
    TestAShortLineIsNotCut();
    TestTheLogKeepsLinesInOrder();
    TestTheLogDropsTheOldestPastItsCap();
    TestTheDefaultCapIsTwoThousandLines();
    TestVisitGivesOnlyTheLinesAfterASequence();
    TestTheRevisionChangesWithEveryChange();
    TestClearEmptiesTheLogAndTellsAReaderToDropWhatItHolds();
    TestAReaderWithNothingIsNeverToldToDrop();
    TestPlainTextJoinsTheLines();
    TestALineThatFitsIsOneRow();
    TestAnEmptyLineIsOneEmptyRow();
    TestWordsWrapWhole();
    TestTheSpacesAtABreakAreDropped();
    TestAWordWiderThanTheRowIsCutBetweenCharacters();
    TestACutNeverSplitsAUtf8Character();
    TestPiecesKeepTheirSpansAndPositions();
    TestAWordContinuesAcrossSpansWithoutAGap();
    TestEveryByteIsShownExactlyOnceWhenNothingIsDropped();
    TestNoRowIsWiderThanTheWidth();
    TestEmptyInputDoesNothing();
    TestBangCommandsAndChatAreSentAsSaid();
    TestSlashCommandsAreClientCommands();
    TestTypedControlCharactersNeverReachTheServer();
    TestAChatLineThatMentionsASlashIsStillChat();
    TestFirstWordSplit();
    TestOnlyConnectionCommandsWorkOffline();
    TestUpGoesBackAndDownForward();
    TestGoingPastTheNewestGivesBackTheDraft();
    TestSendingStopsBrowsing();
    TestHistoryIgnoresEmptyAndRepeatedLines();
    TestHistoryWithNothingLeavesTheBoxAlone();
    TestHistoryDropsTheOldestPastItsCap();
    TestHelpNamesEveryCommand();
    TestReceivedListsEachItemWithItsSenderAndPlace();
    TestReceivedFiltersByItemNameIgnoringCase();
    TestReceivedWithNothingSaysSo();
    TestMissingAndCheckedSplitTheLocations();
    TestMissingWithAFilter();
    TestNoLocationListSaysToConnect();
    TestALongListIsCutWithAHowToNarrowIt();
    if (ComboTest::gFailures != 0) {
        std::cerr << ComboTest::gFailures << " of " << ComboTest::gChecks << " checks failed\n";
        return 1;
    }
    std::cout << "all " << ComboTest::gChecks << " checks passed\n";
    return 0;
}
