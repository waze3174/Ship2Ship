// combo/net/ComboApSync.h
// Ship2Ship + Archipelago: the bookkeeping of the online client, with no socket and no game. Header-only and pure, so
// the launcher, the headless test harness and the unit tests share it. combo/net/ComboArchipelago.cpp feeds it what the
// server says and what the games report, and acts on what it returns.
//
//   SendLedger     which obtained checks the server still has to hear about. The save stores no "sent" list: the
//                  server's own checked list, read on every connect, is the record.
//   ReceiveLedger  which received items to grant. The save stores one number, receivedIndex, the count of the server's
//                  items already given. The server resends its whole list on every connect, so a restart, a reconnect
//                  and a server restart all land here as the same packet and are told apart by index alone.
//   JudgeRoom      a file only syncs with the room it was generated for.
//   ApGrantTarget  which game an item belongs to, and its native name.
//   PlanToasts     what a burst of received items shows.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "rando/ComboApSeed.h"

namespace ComboApSync {

// The game name Ship2Ship's apworld registers (apworld/ship2ship/__init__.py GAME_NAME).
inline constexpr const char* kShip2ShipGame = "Ship2Ship";

// The location of an item the server gives at the start (a start inventory item), in Archipelago's own numbering.
inline constexpr int64_t kStartInventoryLocation = -2;

// The server's own name for the sender of a start inventory item or a command's item.
inline constexpr const char* kServerPlayerName = "Server";

// items_handling: items of other worlds (0b001) and the start inventory (0b100). The slot's own items are granted when
// their check is opened, by the game, so the server must not send them again (0b010 would).
inline constexpr int kItemsHandling = 0b101;

// A LocationChecks packet carries at most this many ids; a larger backlog goes out in several.
inline constexpr size_t kMaxLocationsPerPacket = 500;

// Items granted per game frame: one save batch each, so a start inventory of hundreds does not stall a frame.
inline constexpr size_t kMaxGrantsPerTick = 8;

// Toasts shown for one batch of grants; the rest is one summary line.
inline constexpr size_t kMaxItemToastsPerBatch = 4;

// ---- rooms ----

enum class RoomVerdict { kOk, kWrongSeed, kWrongGame };

// A file syncs only with the room it was generated for (the seed name of its AP block), and only as a Ship2Ship slot.
inline RoomVerdict JudgeRoom(const std::string& expectedSeedName, const std::string& serverSeedName) {
    return (expectedSeedName.empty() || expectedSeedName == serverSeedName) ? RoomVerdict::kOk
                                                                            : RoomVerdict::kWrongSeed;
}

inline RoomVerdict JudgeSlotGame(const std::string& slotGame) {
    return slotGame == kShip2ShipGame ? RoomVerdict::kOk : RoomVerdict::kWrongGame;
}

// ---- the server's address ----

inline std::string TrimmedAddress(const std::string& input) {
    constexpr const char* kWhitespace = " \t\r\n";
    const size_t first = input.find_first_not_of(kWhitespace);
    if (first == std::string::npos) {
        return "";
    }
    return input.substr(first, input.find_last_not_of(kWhitespace) - first + 1);
}

// The address as the client connects to it: a bare "host:port" is plain ws:// unless wss:// is built in, in which case
// the client tries the encrypted connection first and falls back to plain, as Archipelago's own clients do.
inline std::string NormalizeServerAddress(const std::string& input, bool tlsAvailable) {
    const std::string address = TrimmedAddress(input);
    if (address.empty() || address.find("://") != std::string::npos || tlsAvailable) {
        return address;
    }
    return "ws://" + address;
}

// Why the address cannot be used, or "".
inline std::string ServerAddressProblem(const std::string& normalized, bool tlsAvailable) {
    if (normalized.empty()) {
        return "enter the server address";
    }
    const bool plain = normalized.rfind("ws://", 0) == 0;
    const bool secure = normalized.rfind("wss://", 0) == 0;
    const bool schemeless = normalized.find("://") == std::string::npos;
    if (!plain && !secure && !schemeless) {
        return "the address must start with ws:// or wss://";
    }
    if (secure && !tlsAvailable) {
        return "this build has no TLS: use ws://, or a build with wss:// support";
    }
    return "";
}

// ---- a refused certificate ----

// What a wss:// connection says when the server's certificate is not accepted: "<prefix>: <reason><roots note><roots>)"
// (written by combo/net/ComboApTls.h). The status line shows the reason; the console has the whole text.
inline constexpr const char* kCertificateRefusalPrefix = "certificate could not be verified";
inline constexpr const char* kTrustedRootsNote = " (trusted roots: ";

inline bool IsCertificateRefusal(const std::string& socketError) {
    return socketError.rfind(kCertificateRefusalPrefix, 0) == 0;
}

inline std::string CertificateRefusalReason(const std::string& refusal) {
    return refusal.substr(0, refusal.find(kTrustedRootsNote));
}

// ---- send ----

class SendLedger {
  public:
    // A connection was made: `serverChecked` is what the server already holds for this slot. What an earlier connection
    // sent but the server never got is therefore sent again.
    void ResetForConnection(std::set<std::string> serverChecked) {
        known_ = std::move(serverChecked);
    }

    // The server announced more checked locations (RoomUpdate): a release, a collect or another client of this slot.
    void NoteServerChecked(const std::vector<std::string>& names) {
        known_.insert(names.begin(), names.end());
    }

    // The obtained checks the server does not hold yet, in the order the games listed them; they count as sent.
    std::vector<std::string> Plan(const std::vector<std::string>& obtained) {
        std::vector<std::string> fresh;
        for (const std::string& name : obtained) {
            if (known_.insert(name).second) {
                fresh.push_back(name);
            }
        }
        return fresh;
    }

    size_t knownCount() const {
        return known_.size();
    }

    const std::set<std::string>& known() const {
        return known_;
    }

  private:
    std::set<std::string> known_;
};

// A list cut into packets of at most kMaxLocationsPerPacket.
template <class T>
std::vector<std::vector<T>> SplitIntoPackets(const std::vector<T>& all, size_t perPacket = kMaxLocationsPerPacket) {
    std::vector<std::vector<T>> packets;
    for (size_t at = 0; at < all.size(); at += perPacket) {
        const size_t end = std::min(all.size(), at + perPacket);
        packets.emplace_back(all.begin() + static_cast<std::ptrdiff_t>(at),
                             all.begin() + static_cast<std::ptrdiff_t>(end));
    }
    return packets;
}

// ---- receive ----

struct ReceivedItem {
    int index = -1;           // its place in this slot's list on the server, from 0
    std::string itemName;     // the Archipelago name, e.g. "Progressive Hookshot (OoT)"
    std::string playerName;   // who found it
    int64_t location = 0;     // where, or kStartInventoryLocation
    std::string locationName; // the name of that location in the finder's game; "" for the start inventory
    unsigned flags = 0;

    bool IsStartInventory() const {
        return location == kStartInventoryLocation;
    }
};

class ReceiveLedger {
  public:
    explicit ReceiveLedger(int granted = 0) : granted_(granted) {
    }

    // A file was loaded: `granted` is its saved receivedIndex.
    void Reset(int granted) {
        granted_ = std::max(0, granted);
        DropBuffered();
    }

    // A connection ended or began: the server sends its whole list again on the next one.
    void DropBuffered() {
        pending_.clear();
        fullListSize_ = -1;
        syncAsked_ = false;
    }

    // Takes what a ReceivedItems packet carried. Items below the granted count are ones already given (the whole list
    // comes again on every connect) and are dropped; a repeated index keeps its first item.
    void Accept(const std::vector<ReceivedItem>& items) {
        int smallest = -1;
        int end = 0;
        for (const ReceivedItem& item : items) {
            if (item.index < 0) {
                continue;
            }
            smallest = smallest < 0 ? item.index : std::min(smallest, item.index);
            end = std::max(end, item.index + 1);
            if (item.index >= granted_) {
                pending_.emplace(item.index, item);
            }
        }
        if (smallest == 0) {
            fullListSize_ = end;
        }
        if (!pending_.empty()) {
            syncAsked_ = false;
        }
    }

    // Up to `most` items, contiguous from the first one not granted: the ones that can be given now, in order.
    std::vector<ReceivedItem> NextGrants(size_t most) const {
        std::vector<ReceivedItem> ready;
        int expect = granted_;
        for (auto it = pending_.find(expect); it != pending_.end() && it->first == expect && ready.size() < most;
             ++it, ++expect) {
            ready.push_back(it->second);
        }
        return ready;
    }

    // `count` items from NextGrants were given and saved.
    void MarkGranted(int count) {
        granted_ += std::max(0, count);
        pending_.erase(pending_.begin(), pending_.lower_bound(granted_));
    }

    // Items arrived but not the one that is next in line: a packet was lost or came out of order. The client asks the
    // server for its list again, once per arrival (a Sync is answered with the whole list).
    bool NeedsSync() const {
        return !syncAsked_ && !pending_.empty() && pending_.begin()->first > granted_;
    }
    void NoteSyncAsked() {
        syncAsked_ = true;
    }

    // The server's whole list is shorter than what this file already holds: the room was restored from an older state
    // or is another one. Nothing is given twice; items found from now on are still granted by index.
    bool ServerBehind() const {
        return fullListSize_ >= 0 && fullListSize_ < granted_;
    }

    int granted() const {
        return granted_;
    }
    size_t pending() const {
        return pending_.size();
    }

  private:
    int granted_ = 0;
    std::map<int, ReceivedItem> pending_;
    int fullListSize_ = -1; // the size of the last full list (a packet starting at index 0), or -1
    bool syncAsked_ = false;
};

// ---- grants ----

struct GrantTarget {
    bool valid = false;
    ComboRando::GameId game = ComboRando::GAME_OOT;
    std::string nativeName;
};

inline bool EndsWith(const std::string& text, const char* tail) {
    const size_t n = std::strlen(tail);
    return text.size() > n && text.compare(text.size() - n, n, tail) == 0;
}

// An item of Ship2Ship's own world: its game by the tag the apworld puts on every name, and the name the games grant
// by. An item of another game, or one with no tag, is not valid.
inline GrantTarget ApGrantTarget(const std::string& apName) {
    GrantTarget target;
    if (EndsWith(apName, ComboRando::kApOotTag)) {
        target.game = ComboRando::GAME_OOT;
        target.nativeName = apName.substr(0, apName.size() - std::strlen(ComboRando::kApOotTag));
    } else if (EndsWith(apName, ComboRando::kApMmTag)) {
        target.game = ComboRando::GAME_MM;
        target.nativeName = apName.substr(0, apName.size() - std::strlen(ComboRando::kApMmTag));
    } else {
        return target;
    }
    target.valid = !target.nativeName.empty();
    return target;
}

// ---- toasts ----

using ToastText = ComboRando::ApToastText;

// "<Item> from <Player>" for an item the server sent.
inline ToastText ReceivedToast(const ReceivedItem& item) {
    return ComboRando::ApReceivedToast(item.itemName, item.playerName);
}

// What a batch of grants shows: no toast for a start inventory item (the file starts with them), one for each of the
// first few other items, then one line for the rest.
inline std::vector<ToastText> PlanToasts(const std::vector<ReceivedItem>& batch) {
    std::vector<ToastText> toasts;
    size_t more = 0;
    for (const ReceivedItem& item : batch) {
        if (item.IsStartInventory()) {
            continue;
        }
        if (toasts.size() < kMaxItemToastsPerBatch) {
            toasts.push_back(ReceivedToast(item));
        } else {
            ++more;
        }
    }
    if (more > 0) {
        toasts.push_back({ "Received", std::to_string(more) + " more items", "" });
    }
    return toasts;
}

} // namespace ComboApSync
