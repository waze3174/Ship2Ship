// combo/net/ComboApConsoleInput.h
// Ship2Ship + Archipelago: what the console does with the text the player types, and the text of its own commands'
// answers. Header-only and pure (no socket, no game, no ImGui).
//
//   ParseInput    "!hint Bow" and plain text are sent to the server as a Say; "/received" is a client command.
//   InputHistory  the Up and Down arrows.
//   Format*       the answers of /help, /received, /missing, /checked, /items and /locations.
//
// The client commands follow ArchipelagoMW/Archipelago's CommonClient.py (ClientCommandProcessor, at the pinned core);
// the listings take an optional filter text and are cut off at kMaxListedLines.
#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ComboApConsole.h"

namespace ComboApConsole {

// ---- parsing ----

enum class InputKind {
    kEmpty,  // nothing to do
    kSay,    // sent to the server as typed: a "!" command, or chat
    kClient, // a "/" command of this client
};

struct ParsedInput {
    InputKind kind = InputKind::kEmpty;
    std::string say;     // kSay: the text to send
    std::string command; // kClient: the name after the slash, in lower case; may be empty
    std::string args;    // kClient: the rest of the line
};

inline std::string Trimmed(const std::string& text) {
    constexpr const char* kWhitespace = " \t\r\n";
    const size_t first = text.find_first_not_of(kWhitespace);
    if (first == std::string::npos) {
        return "";
    }
    return text.substr(first, text.find_last_not_of(kWhitespace) - first + 1);
}

inline std::string Lowered(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

inline ParsedInput ParseInput(const std::string& raw) {
    ParsedInput parsed;
    const std::string text = Trimmed(SafeText(raw)); // a server rejects a Say that is not printable
    if (text.empty()) {
        return parsed;
    }
    if (text[0] != '/') {
        parsed.kind = InputKind::kSay;
        parsed.say = text;
        return parsed;
    }
    parsed.kind = InputKind::kClient;
    const size_t space = text.find_first_of(" \t");
    parsed.command = Lowered(text.substr(1, space == std::string::npos ? std::string::npos : space - 1));
    parsed.args = space == std::string::npos ? std::string() : Trimmed(text.substr(space));
    return parsed;
}

// The first word of `args` and what follows it.
inline std::pair<std::string, std::string> SplitWord(const std::string& args) {
    const size_t space = args.find_first_of(" \t");
    if (space == std::string::npos) {
        return { args, "" };
    }
    return { args.substr(0, space), Trimmed(args.substr(space)) };
}

// The commands that work with no connection: those that make one, and the ones that need none.
inline bool WorksOffline(const ParsedInput& input) {
    if (input.kind == InputKind::kEmpty) {
        return true;
    }
    return input.kind == InputKind::kClient && (input.command == "connect" || input.command == "disconnect" ||
                                                input.command == "help" || input.command == "clear");
}

// ---- history ----

// What was sent before, newest last. Up goes back, Down forward; going past the newest gives back what was being typed.
class InputHistory {
  public:
    explicit InputHistory(size_t capacity = 100) : capacity_(std::max<size_t>(1, capacity)) {
    }

    // A line was sent. An empty line, and the line just sent before, are not kept twice.
    void Push(const std::string& line) {
        if (!line.empty() && (entries_.empty() || entries_.back() != line)) {
            entries_.push_back(line);
            if (entries_.size() > capacity_) {
                entries_.erase(entries_.begin());
            }
        }
        StopBrowsing();
    }

    // The next older line; `current` is what is in the box now (kept for the way back). Stays on the oldest.
    std::string Older(const std::string& current) {
        if (entries_.empty()) {
            return current;
        }
        if (cursor_ == kNotBrowsing) {
            draft_ = current;
            cursor_ = entries_.size();
        }
        cursor_ = cursor_ > 0 ? cursor_ - 1 : 0;
        return entries_[cursor_];
    }

    // The next newer line, or the draft after the newest.
    std::string Newer(const std::string& current) {
        if (cursor_ == kNotBrowsing) {
            return current;
        }
        if (cursor_ + 1 < entries_.size()) {
            return entries_[++cursor_];
        }
        const std::string draft = draft_;
        StopBrowsing();
        return draft;
    }

    void StopBrowsing() {
        cursor_ = kNotBrowsing;
        draft_.clear();
    }

    size_t size() const {
        return entries_.size();
    }

  private:
    static constexpr size_t kNotBrowsing = static_cast<size_t>(-1);
    std::vector<std::string> entries_;
    size_t capacity_;
    size_t cursor_ = kNotBrowsing;
    std::string draft_;
};

// ---- the answers ----

// A listing shows at most this many lines, then says how many it left out.
inline constexpr size_t kMaxListedLines = 300;

struct CommandHelp {
    const char* usage;
    const char* summary;
};

inline const std::vector<CommandHelp>& ClientCommands() {
    static const std::vector<CommandHelp> kCommands = {
        { "/help", "this list" },
        { "/connect [address] [password]", "connect to a server; with no address, the one saved with this file" },
        { "/disconnect", "disconnect, and stay disconnected until you connect again" },
        { "/received [filter]", "the items received so far, with who sent them and from where" },
        { "/missing [filter]", "the checks this slot has not opened, by the server's record and this game's" },
        { "/checked [filter]", "the checks this slot has opened" },
        { "/items [filter]", "the item names of this slot's game" },
        { "/locations [filter]", "the location names of this slot's game" },
        { "/ready", "tell the server you are ready; again to take it back" },
        { "/clear", "empty this console" },
    };
    return kCommands;
}

inline std::vector<Line> FormatHelp() {
    std::vector<Line> lines;
    lines.push_back(PlainLine("Console commands:", kWhite));
    for (const CommandHelp& command : ClientCommands()) {
        lines.push_back({ { { kCyan, std::string("  ") + command.usage },
                            { kNoticeColor, std::string("  ") + command.summary } } });
    }
    lines.push_back(PlainLine("Anything starting with ! is a server command (try !help, !hint <item>, !remaining, "
                              "!release, !collect, !countdown). Other text is chat.",
                              kNoticeColor));
    return lines;
}

inline bool ContainsIgnoringCase(const std::string& haystack, const std::string& needle) {
    return needle.empty() || Lowered(haystack).find(Lowered(needle)) != std::string::npos;
}

// "N more ..." when a listing was cut.
inline Line MoreLine(size_t left, const char* command) {
    return NoticeLine(Level::kInfo, "... and " + std::to_string(left) +
                                        " more. Add some of the name to narrow it, e.g. " + command + " Deku");
}

struct ReceivedEntry {
    int index = 0; // from 0, the server's order
    std::string item;
    unsigned flags = 0;
    std::string location; // where the sender found it; "" when it is not a location (a start inventory item)
    std::string player;
};

// The answer of /received: one line per item, oldest first.
inline std::vector<Line> FormatReceived(const std::vector<ReceivedEntry>& entries, const std::string& filter) {
    std::vector<const ReceivedEntry*> shown;
    for (const ReceivedEntry& entry : entries) {
        if (ContainsIgnoringCase(entry.item, filter)) {
            shown.push_back(&entry);
        }
    }
    std::vector<Line> lines;
    lines.push_back(NoticeLine(Level::kInfo, std::to_string(shown.size()) + " received items, sorted by time:"));
    for (size_t i = 0; i < shown.size() && i < kMaxListedLines; ++i) {
        const ReceivedEntry& entry = *shown[i];
        Line line;
        line.spans.push_back({ kDefaultColor, std::to_string(entry.index + 1) + ". " });
        line.spans.push_back({ ItemColor(entry.flags), entry.item });
        if (!entry.location.empty()) {
            line.spans.push_back({ kDefaultColor, " from " });
            line.spans.push_back({ kGreen, entry.location });
        }
        line.spans.push_back({ kDefaultColor, " by " });
        line.spans.push_back({ kYellow, entry.player });
        lines.push_back(std::move(line));
    }
    if (shown.size() > kMaxListedLines) {
        lines.push_back(MoreLine(shown.size() - kMaxListedLines, "/received"));
    }
    return lines;
}

// A sorted list of names under a heading (/items, /locations).
inline std::vector<Line> FormatNames(const std::string& heading, const std::vector<std::string>& names,
                                     const std::string& filter, const char* command, Color color) {
    std::vector<std::string> shown;
    for (const std::string& name : names) {
        if (ContainsIgnoringCase(name, filter)) {
            shown.push_back(name);
        }
    }
    std::sort(shown.begin(), shown.end());
    std::vector<Line> lines;
    lines.push_back(NoticeLine(Level::kInfo, heading + " (" + std::to_string(shown.size()) + ")"));
    for (size_t i = 0; i < shown.size() && i < kMaxListedLines; ++i) {
        lines.push_back(PlainLine(shown[i], color));
    }
    if (shown.size() > kMaxListedLines) {
        lines.push_back(MoreLine(shown.size() - kMaxListedLines, command));
    }
    return lines;
}

// The answer of /missing and /checked: the room's locations of this slot split by what is known to be opened.
inline std::vector<Line> FormatChecks(bool wantChecked, const std::vector<std::string>& allLocations,
                                      const std::set<std::string>& checked, const std::string& filter) {
    std::vector<std::string> wanted;
    for (const std::string& name : allLocations) {
        if ((checked.count(name) != 0) == wantChecked) {
            wanted.push_back(name);
        }
    }
    const char* command = wantChecked ? "/checked" : "/missing";
    const std::string heading = wantChecked ? "Checked locations" : "Missing locations";
    std::vector<Line> lines = FormatNames(heading, wanted, filter, command, wantChecked ? kNoticeColor : kGreen);
    if (allLocations.empty()) {
        lines.push_back(NoticeLine(Level::kWarn, "No location list yet: connect first."));
    }
    return lines;
}

} // namespace ComboApConsole
