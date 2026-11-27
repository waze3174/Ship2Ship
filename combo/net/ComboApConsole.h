// combo/net/ComboApConsole.h
// Ship2Ship + Archipelago: the console's text model, with no socket, no game and no ImGui. Header-only and pure, so the
// client, comboui and the unit tests share it.
//
//   Line, Span     one console line: coloured pieces of text.
//   RenderParts    a PrintJSON message's parts as lines, ids resolved through a NameSource, coloured the way
//   Archipelago's
//                  own text client does (NetUtils.JSONtoTextParser and its color_codes, ArchipelagoMW/Archipelago at
//                  the pinned core).
//   Log            the scrollback: a capped, thread-safe list of lines.
//   WrapLine       where a line breaks for a given width, for any text measure.
//
// Everything from the server goes through SafeText first: control characters, escape sequences and text-direction
// overrides never reach the screen.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ComboApConsole {

// ---- colours ----

// 0xAARRGGBB; kDefaultColor (0) means the window's own text colour.
using Color = uint32_t;
inline constexpr Color kDefaultColor = 0;
constexpr Color Rgb(uint32_t rgb) {
    return 0xFF000000u | rgb;
}

// Archipelago's palette (NetUtils.JSONtoTextParser.color_codes, the values kvui's kv file uses).
inline constexpr Color kBlack = Rgb(0x000000);
inline constexpr Color kRed = Rgb(0xEE0000);
inline constexpr Color kGreen = Rgb(0x00FF7F);
inline constexpr Color kYellow = Rgb(0xFAFAD2);
inline constexpr Color kBlue = Rgb(0x6495ED);
inline constexpr Color kMagenta = Rgb(0xEE00EE);
inline constexpr Color kCyan = Rgb(0x00EEEE);
inline constexpr Color kSlateBlue = Rgb(0x6D8BE8);
inline constexpr Color kPlum = Rgb(0xAF99EF);
inline constexpr Color kSalmon = Rgb(0xFA8072);
inline constexpr Color kWhite = Rgb(0xFFFFFF);
inline constexpr Color kOrange = Rgb(0xFF7700);

// The client's own lines.
inline constexpr Color kNoticeColor = Rgb(0xA8A8A8);
inline constexpr Color kEchoColor = Rgb(0x7F8FA6);

// Item flags (NetworkItem.flags).
inline constexpr unsigned kFlagProgression = 0b001;
inline constexpr unsigned kFlagUseful = 0b010;
inline constexpr unsigned kFlagTrap = 0b100;

// Hint statuses (HintStatus).
inline constexpr unsigned kHintUnspecified = 0;
inline constexpr unsigned kHintNoPriority = 10;
inline constexpr unsigned kHintAvoid = 20;
inline constexpr unsigned kHintPriority = 30;
inline constexpr unsigned kHintFound = 40;

// An item's colour by its class: progression plum, useful slate blue, trap salmon, anything else cyan. Several flags at
// once take the first of that order, as the text client does.
inline Color ItemColor(unsigned flags) {
    if ((flags & kFlagProgression) != 0) {
        return kPlum;
    }
    if ((flags & kFlagUseful) != 0) {
        return kSlateBlue;
    }
    if ((flags & kFlagTrap) != 0) {
        return kSalmon;
    }
    return kCyan;
}

inline Color HintStatusColor(unsigned status) {
    switch (status) {
        case kHintFound:
            return kGreen;
        case kHintUnspecified:
            return kWhite;
        case kHintNoPriority:
            return kSlateBlue;
        case kHintAvoid:
            return kSalmon;
        case kHintPriority:
            return kPlum;
        default:
            return kRed;
    }
}

// The first colour of a "red;bold" style list that the palette knows; the rest (bold, underline, backgrounds) is not
// drawn. Unknown names give the default colour.
inline Color ColorByNames(const std::string& names) {
    struct Named {
        const char* name;
        Color color;
    };
    static constexpr Named kNamed[] = {
        { "black", kBlack }, { "red", kRed },         { "green", kGreen }, { "yellow", kYellow },
        { "blue", kBlue },   { "magenta", kMagenta }, { "cyan", kCyan },   { "slateblue", kSlateBlue },
        { "plum", kPlum },   { "salmon", kSalmon },   { "white", kWhite }, { "orange", kOrange },
    };
    size_t at = 0;
    while (at <= names.size()) {
        const size_t end = std::min(names.find(';', at), names.size());
        const std::string one = names.substr(at, end - at);
        for (const Named& named : kNamed) {
            if (one == named.name) {
                return named.color;
            }
        }
        at = end + 1;
    }
    return kDefaultColor;
}

// ---- lines ----

struct Span {
    Color color = kDefaultColor;
    std::string text;
};

struct Line {
    std::vector<Span> spans;

    std::string PlainText() const {
        std::string text;
        for (const Span& span : spans) {
            text += span.text;
        }
        return text;
    }
};

inline Line PlainLine(const std::string& text, Color color = kDefaultColor) {
    return Line{ { { color, text } } };
}

// A line the client itself says (status, errors, command output).
enum class Level { kInfo, kWarn, kError };

inline Line NoticeLine(Level level, const std::string& text) {
    switch (level) {
        case Level::kError:
            return PlainLine(text, kRed);
        case Level::kWarn:
            return PlainLine(text, kOrange);
        case Level::kInfo:
            break;
    }
    return PlainLine(text, kNoticeColor);
}

// ---- safe text ----

// A cut that stops short of the end of a longer text ends in this.
inline constexpr const char* kCutMarker = "...";
// One line never holds more than this many bytes; a server cannot make one line take the whole scrollback.
inline constexpr size_t kMaxLineBytes = 4096;

// The text with everything that is not printable removed: C0 and C1 controls (a tab becomes a space), terminal escape
// sequences, and the text-direction overrides that can make a line read backwards. UTF-8 text is kept as it is.
inline std::string SafeText(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    const auto at = [&](size_t i) { return static_cast<unsigned char>(i < text.size() ? text[i] : 0); };
    for (size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = at(i);
        if (c == 0x1B) { // ESC: a CSI sequence ("ESC [ ... final byte") goes whole
            if (at(i + 1) == '[') {
                i += 2;
                while (i < text.size() && !(at(i) >= 0x40 && at(i) <= 0x7E)) {
                    ++i;
                }
            }
            continue;
        }
        if (c == '\t') {
            out.push_back(' ');
        } else if (c < 0x20 || c == 0x7F) {
            continue;
        } else if (c == 0xC2 && at(i + 1) >= 0x80 && at(i + 1) <= 0x9F) { // U+0080..U+009F
            ++i;
        } else if (c == 0xE2 && at(i + 1) == 0x80 && at(i + 2) >= 0xAA && at(i + 2) <= 0xAE) { // U+202A..U+202E
            i += 2;
        } else if (c == 0xE2 && at(i + 1) == 0x81 && at(i + 2) >= 0xA6 && at(i + 2) <= 0xA9) { // U+2066..U+2069
            i += 2;
        } else {
            out.push_back(static_cast<char>(c));
        }
    }
    return out;
}

// The longest prefix of at most `limit` bytes that does not end inside a UTF-8 character.
inline size_t Utf8Prefix(const std::string& text, size_t limit) {
    if (text.size() <= limit) {
        return text.size();
    }
    size_t end = limit;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
        --end;
    }
    return end;
}

// A raw line (spans that may hold newlines and control characters) as the lines to show: split at every newline, each
// span made safe, empty spans dropped, and no line longer than kMaxLineBytes.
inline std::vector<Line> Normalize(const Line& raw) {
    std::vector<Line> lines(1);
    for (const Span& span : raw.spans) {
        size_t start = 0;
        while (start <= span.text.size()) {
            const size_t newline = std::min(span.text.find('\n', start), span.text.size());
            const std::string piece = SafeText(span.text.substr(start, newline - start));
            if (!piece.empty()) {
                lines.back().spans.push_back({ span.color, piece });
            }
            if (newline == span.text.size()) {
                break;
            }
            lines.emplace_back();
            start = newline + 1;
        }
    }
    for (Line& line : lines) {
        size_t used = 0;
        for (size_t i = 0; i < line.spans.size(); ++i) {
            const size_t room = used < kMaxLineBytes ? kMaxLineBytes - used : 0;
            std::string& text = line.spans[i].text;
            if (text.size() > room) {
                text = text.substr(0, Utf8Prefix(text, room)) + kCutMarker;
                line.spans.resize(i + 1);
                break;
            }
            used += text.size();
        }
    }
    return lines;
}

// ---- PrintJSON ----

// One part of a PrintJSON message, as the protocol sends it (apclientpp's TextNode).
struct Part {
    std::string type; // "", "text", "color", "player_id", "player_name", "item_id", "item_name", "location_id",
                      // "location_name", "entrance_name", "hint_status"
    std::string text;
    std::string color; // for a "color" part
    int player = 0;    // the slot an item or location id belongs to
    unsigned flags = 0;
    unsigned hintStatus = 0;
};

// The names of the connected room: what the data package and the player list say. An empty result is an id the room
// does not know.
class NameSource {
  public:
    virtual ~NameSource() = default;
    virtual std::optional<std::string> PlayerName(int slot) const = 0;
    virtual std::optional<std::string> ItemName(int64_t id, int ownerSlot) const = 0;
    virtual std::optional<std::string> LocationName(int64_t id, int ownerSlot) const = 0;
    // True for the player's own slot and for a group the slot belongs to.
    virtual bool ConcernsSelf(int slot) const = 0;
};

// A number from a part's text, as Archipelago writes ids (a decimal, possibly negative). False when it is not one.
inline bool ParseId(const std::string& text, int64_t& id) {
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    const long long value = std::strtoll(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0') {
        return false;
    }
    id = value;
    return true;
}

inline Span RenderPart(const Part& part, const NameSource& names) {
    int64_t id = 0;
    const bool numeric = ParseId(part.text, id);
    if (part.type == "color") {
        return { ColorByNames(part.color), part.text };
    }
    if (part.type == "player_id") {
        if (!numeric) {
            return { kYellow, part.text };
        }
        const std::optional<std::string> name = names.PlayerName(static_cast<int>(id));
        return { names.ConcernsSelf(static_cast<int>(id)) ? kMagenta : kYellow, name ? *name : "Player " + part.text };
    }
    if (part.type == "player_name") {
        return { kYellow, part.text };
    }
    if (part.type == "item_id") {
        const std::optional<std::string> name = numeric ? names.ItemName(id, part.player) : std::nullopt;
        return { ItemColor(part.flags), name ? *name : "Unknown item (ID:" + part.text + ")" };
    }
    if (part.type == "item_name") {
        return { ItemColor(part.flags), part.text };
    }
    if (part.type == "location_id") {
        const std::optional<std::string> name = numeric ? names.LocationName(id, part.player) : std::nullopt;
        return { kGreen, name ? *name : "Unknown location (ID:" + part.text + ")" };
    }
    if (part.type == "location_name") {
        return { kGreen, part.text };
    }
    if (part.type == "entrance_name") {
        return { kBlue, part.text };
    }
    if (part.type == "hint_status") {
        return { HintStatusColor(part.hintStatus), part.text };
    }
    return { kDefaultColor, part.text };
}

// The parts of one PrintJSON message as the lines to show.
inline std::vector<Line> RenderParts(const std::vector<Part>& parts, const NameSource& names) {
    Line raw;
    for (const Part& part : parts) {
        raw.spans.push_back(RenderPart(part, names));
    }
    return Normalize(raw);
}

// ---- the scrollback ----

// The scrollback holds at most this many lines; the oldest go first.
inline constexpr size_t kMaxLines = 2000;

class Log {
  public:
    explicit Log(size_t capacity = kMaxLines) : capacity_(std::max<size_t>(1, capacity)) {
    }

    void Append(const std::vector<Line>& lines) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const Line& line : lines) {
            lines_.push_back({ nextSeq_++, line });
            if (lines_.size() > capacity_) {
                lines_.pop_front();
            }
        }
        ++revision_;
    }
    void Append(const Line& line) {
        Append(std::vector<Line>{ line });
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        clearedThrough_ = nextSeq_ - 1;
        lines_.clear();
        ++revision_;
    }

    // Changes whenever lines were added or the log was cleared: a cheap way to ask "anything new?".
    uint64_t Revision() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return revision_;
    }

    size_t Size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return lines_.size();
    }

    // Calls `visit(sequence, line)` for every line numbered after `afterSequence` (0 for all), oldest first. Returns
    // true when lines the caller already holds (numbered `afterSequence` or less) were cleared since: it must drop them
    // first. The calls are made under the log's lock: `visit` must be quick and must not call back into the log.
    bool Visit(uint64_t afterSequence, const std::function<void(uint64_t, const Line&)>& visit) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const Entry& entry : lines_) {
            if (entry.sequence > afterSequence) {
                visit(entry.sequence, entry.line);
            }
        }
        return afterSequence > 0 && clearedThrough_ >= afterSequence;
    }

    std::string PlainText() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string text;
        for (const Entry& entry : lines_) {
            text += entry.line.PlainText();
            text.push_back('\n');
        }
        return text;
    }

  private:
    struct Entry {
        uint64_t sequence;
        Line line;
    };
    mutable std::mutex mutex_;
    std::deque<Entry> lines_;
    size_t capacity_;
    uint64_t nextSeq_ = 1;
    uint64_t clearedThrough_ = 0;
    uint64_t revision_ = 0;
};

// ---- wrapping ----

// A stretch of one span, placed on a row.
struct Piece {
    size_t span = 0;
    size_t begin = 0;
    size_t length = 0;
    float x = 0.0f;
};
using Row = std::vector<Piece>;

// The width in pixels of the bytes [begin, end).
using Measure = std::function<float(const char* begin, const char* end)>;

namespace detail {

inline bool IsContinuation(char c) {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

// One run of a span: either all spaces or all non-spaces.
inline size_t RunEnd(const std::string& text, size_t begin) {
    const bool space = text[begin] == ' ';
    size_t end = begin;
    while (end < text.size() && (text[end] == ' ') == space) {
        ++end;
    }
    return end;
}

inline void AddPiece(Row& row, size_t span, size_t begin, size_t length, float x) {
    if (!row.empty()) {
        Piece& last = row.back();
        if (last.span == span && last.begin + last.length == begin) {
            last.length += length;
            return;
        }
    }
    row.push_back({ span, begin, length, x });
}

} // namespace detail

// The rows a line takes in `width` pixels. Words stay whole; a word longer than the width is cut between characters.
// The spaces at a break are dropped, and so are the ones that end the line. A line always has at least one row.
inline std::vector<Row> WrapLine(const Line& line, float width, const Measure& measure) {
    struct Run {
        size_t span;
        size_t begin;
        size_t length;
        float width;
    };
    std::vector<Row> rows(1);
    float x = 0.0f;
    std::vector<Run> spaces; // seen since the last word, placed only if the next word fits on the row
    float spacesWidth = 0.0f;
    const auto newRow = [&] {
        rows.emplace_back();
        x = 0.0f;
    };
    const auto place = [&](const Run& run) {
        detail::AddPiece(rows.back(), run.span, run.begin, run.length, x);
        x += run.width;
    };
    for (size_t s = 0; s < line.spans.size(); ++s) {
        const std::string& text = line.spans[s].text;
        const char* base = text.data();
        for (size_t begin = 0; begin < text.size();) {
            const size_t end = detail::RunEnd(text, begin);
            const Run run = { s, begin, end - begin, measure(base + begin, base + end) };
            begin = end;
            if (text[run.begin] == ' ') {
                spaces.push_back(run);
                spacesWidth += run.width;
                continue;
            }
            if (x > 0.0f && x + spacesWidth + run.width > width) {
                newRow();
                spaces.clear();
            }
            for (const Run& space : spaces) {
                place(space);
            }
            spaces.clear();
            spacesWidth = 0.0f;
            if (run.width <= width) {
                place(run);
                continue;
            }
            // A word wider than a row: fill rows character by character.
            size_t start = run.begin;
            float used = 0.0f;
            for (size_t at = run.begin; at < end;) {
                size_t next = at + 1;
                while (next < end && detail::IsContinuation(text[next])) {
                    ++next;
                }
                const float charWidth = measure(base + at, base + next);
                if (x + used + charWidth > width && at > start) {
                    detail::AddPiece(rows.back(), s, start, at - start, x);
                    newRow();
                    start = at;
                    used = 0.0f;
                }
                used += charWidth;
                at = next;
            }
            detail::AddPiece(rows.back(), s, start, end - start, x);
            x += used;
        }
    }
    return rows;
}

} // namespace ComboApConsole
