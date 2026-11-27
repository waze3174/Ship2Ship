// combo/rando/ComboOracleServer.h
// ComboShip: the Archipelago oracle protocol, spoken by `comborando --oracle`. One request per line in, one response
// per line out, both JSON objects. It answers one closed question: "with exactly these items owned, which checks are
// reachable". The cross-game latches and each game's own pre-placed items run inside it, through the same
// CrossWorldSession the fill uses, so the answer is the one the game's own fill and playthrough would give.
//
// Calls (every response carries "ok", "ms" and the request's "id" when it had one; a failure has "error"):
//   hello     -> protocol version, the games, the library identity the executable was built with.
//   open      masterSeed + the seed's settings, as the consolidated seed stores them (see OpenWorld in
//             ComboRandoHeadless.cpp). Runs everything that happens before the fill: entrances, Master Quest rolls,
//             prices, own-dungeon items, songs and the other fixed placements, the shop minimum set, Shared Items
//             trimming and the pool balance. Returns:
//               locations      [[game, name], ...]  every fillable check; bit i of a reach mask is locations[i]
//               neverReachable [i, ...]             locations nothing can reach even with every item owned
//               items          [{game, name, count, class, category, advancement}]  the balanced pool; class is
//                              progression, useful, filler or trap
//               fixed          [{game, check, item, advancement}]  confined pre-placements, credited inside the
//                              oracle when their check is reached (never AP locations)
//               forced         [{game, check, item}]  placements owned from the start (Link's Pocket)
//               startingOwned  {oot: [...], mm: [...]}  items the oracle owns from the start
//               effective      the settings after the oracle applied what it forces
//               win            how the seed is won
//   reach     items {oot: {name: count}, mm: {name: count}}. Returns mask (hex, locations[i] is bit i), fixedMask (the
//             same over `fixed` then `forced`, for tests), win {ganon, majora, pieces, won} and unknown (names the pool
//             does not hold, which the oracle still owned).
//   finalize  placements [{game, check, item: {game, name}} or {game, check, other: {player, name, advancement,
//   class}}], and
//             archipelago {slotName, seedName}, which another player's item requires. "class" is the item's Archipelago
//             classification (progression, useful, filler or trap), optional. Every location that is not
//             neverReachable needs a placement. Returns the consolidated seed ("seed"), or writes it to "out" when
//             given. Nothing else is ever written to disk. With "archipelago" the seed is the slot's Archipelago seed
//             (ComboApSeed.h); without it, the plain one.
//   close     drops the session. The process lives until stdin closes.
#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <functional>
#include <istream>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <nlohmann/json.hpp>

#include "ComboApSeed.h"
#include "ComboAttempt.h"
#include "CrossWorldSession.h"

namespace ComboRando {

inline constexpr int kOracleProtocolVersion = 1;

// How a seed is won, as `open` reports it and `reach` evaluates it.
inline constexpr const char* kWinGanonAndMajora = "ganon+majora";
inline constexpr const char* kWinGanon = "ganon";
inline constexpr const char* kWinMajora = "majora";
inline constexpr const char* kWinPieces = "pieces";

// The win-marker checks both oracles answer with (friendly names, see CrossWorldRando.h).
inline constexpr const char* kOotGanonCheck = "Ganon";
inline constexpr const char* kMmMajoraCheck = "Moon Majora Pot 01";

// One opened seed: what the setup was told and decided, and the session that answers reach calls.
struct OracleWorld {
    AttemptConfig config;
    AttemptSetup setup;
    uint32_t masterSeed = 0;
    bool mmStart = false;
    OotAccess ootAccess = OotAccess::ALL_REACHABLE;
    std::string seedLabel; // the seed's own label for the consolidated file
    std::unique_ptr<CrossWorldSession> session;
};

// What the executable supplies: it owns the game modules, so it opens and closes worlds and writes the seed file.
struct OracleBackend {
    nlohmann::json identity = nlohmann::json::object(); // hello's "library"
    // Fills `world` from the request (settings, setup, session); returns "" or why it cannot.
    std::function<std::string(const nlohmann::json& request, OracleWorld& world)> open;
    // Builds the consolidated seed from the placements (confined and forced ones included); returns "" or why not.
    std::function<std::string(OracleWorld& world, std::vector<CwPlacement>& placements, nlohmann::json& seedOut)>
        finalize;
    std::function<void()> close; // restores the game-side state
};

inline const char* OracleGameTag(GameId g) {
    return g == GAME_OOT ? "oot" : "mm";
}

inline bool OracleGameFromTag(const std::string& tag, GameId& out) {
    if (tag == "oot")
        out = GAME_OOT;
    else if (tag == "mm")
        out = GAME_MM;
    else
        return false;
    return true;
}

// Bit i of the mask is set when bits[i] is. Hex, most significant digit first, so int(mask, 16) works in Python.
inline std::string OracleMaskToHex(const std::vector<bool>& bits) {
    static const char* kDigits = "0123456789abcdef";
    const size_t nibbles = std::max<size_t>(1, (bits.size() + 3) / 4);
    std::string out(nibbles, '0');
    for (size_t i = 0; i < bits.size(); ++i) {
        if (!bits[i])
            continue;
        const size_t digit = nibbles - 1 - i / 4;
        const int value = (out[digit] <= '9' ? out[digit] - '0' : out[digit] - 'a' + 10) | (1 << (i % 4));
        out[digit] = kDigits[value];
    }
    return out;
}

inline const char* OracleOotAccessName(OotAccess a) {
    switch (a) {
        case OotAccess::ALL_REACHABLE:
            return "allReachable";
        case OotAccess::BEATABLE_ONLY:
            return "beatableOnly";
        case OotAccess::NO_LOGIC:
            return "noLogic";
    }
    return "allReachable";
}

// What an item is to Archipelago: a trap, progression, a useful non-progression item, or filler.
inline const char* OracleItemClass(const CwItem& item, bool trap) {
    if (trap)
        return "trap";
    if (item.advancement)
        return "progression";
    return item.cat == CwCat::JUNK ? "filler" : "useful";
}

class OracleServer {
  public:
    explicit OracleServer(OracleBackend backend) : backend_(std::move(backend)) {
    }

    // One request in, one response out. Never throws.
    nlohmann::json Handle(const nlohmann::json& request) {
        const auto t0 = std::chrono::steady_clock::now();
        nlohmann::json response;
        try {
            response = Dispatch(request);
        } catch (const std::exception& e) { response = Fail(std::string("internal error: ") + e.what()); }
        response["ok"] = !response.contains("error");
        if (request.is_object() && request.contains("id"))
            response["id"] = request["id"];
        response["ms"] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        return response;
    }

    // Reads request lines until `in` closes; `write` receives each response line without its newline.
    void Serve(std::istream& in, const std::function<void(const std::string&)>& write) {
        std::string line;
        while (std::getline(in, line)) {
            if (line.find_first_not_of(" \t\r") == std::string::npos)
                continue;
            nlohmann::json request = nlohmann::json::parse(line, nullptr, false);
            write(request.is_discarded() ? FailLine("request is not valid JSON") : Handle(request).dump());
        }
        CloseWorld();
    }

  private:
    // Everything about the open world that the calls need to look up by name.
    struct Index {
        std::unordered_map<std::string, size_t> location[2]; // by game, name -> bit
        std::vector<bool> neverReachable;
        std::unordered_set<std::string> knownItem[2];
        std::unordered_map<std::string, CwItem> itemByName[2]; // first pool entry of that name, for its flags
        const CwItem* filler[2] = { nullptr, nullptr };        // native junk that pads a location nothing can fill
        std::string winCondition = kWinGanonAndMajora;
    };

    static nlohmann::json Fail(const std::string& message) {
        return { { "error", message } };
    }
    static std::string FailLine(const std::string& message) {
        nlohmann::json j = Fail(message);
        j["ok"] = false;
        return j.dump();
    }

    nlohmann::json Dispatch(const nlohmann::json& request) {
        if (!request.is_object() || !request.contains("cmd") || !request["cmd"].is_string())
            return Fail("a request is an object with a \"cmd\" string");
        const std::string cmd = request["cmd"].get<std::string>();
        if (cmd == "hello")
            return Hello();
        if (cmd == "open")
            return Open(request);
        if (cmd == "reach")
            return Reach(request);
        if (cmd == "finalize")
            return Finalize(request);
        if (cmd == "close") {
            CloseWorld();
            return nlohmann::json::object();
        }
        return Fail("unknown cmd '" + cmd + "'");
    }

    nlohmann::json Hello() const {
        return { { "protocol", kOracleProtocolVersion },
                 { "games", { "oot", "mm" } },
                 { "library", backend_.identity } };
    }

    void CloseWorld() {
        if (!world_)
            return;
        world_.reset();
        index_ = Index{};
        if (backend_.close)
            backend_.close();
    }

    static std::string WinConditionFor(const OracleWorld& w) {
        if (w.config.goal.hunt)
            return kWinPieces;
        if (w.config.games == COMBO_GAMES_OOT)
            return kWinGanon;
        if (w.config.games == COMBO_GAMES_MM)
            return kWinMajora;
        return kWinGanonAndMajora;
    }

    static std::unordered_set<std::string> TrapNames(const std::string& dumpJson) {
        std::unordered_set<std::string> traps;
        try {
            for (const auto& it : nlohmann::json::parse(dumpJson).value("items", nlohmann::json::array()))
                if (it.value("trap", false))
                    traps.insert(it.value("name", std::string()));
        } catch (...) {}
        return traps;
    }

    nlohmann::json Open(const nlohmann::json& request) {
        CloseWorld();
        auto world = std::make_unique<OracleWorld>();
        const std::string error = backend_.open ? backend_.open(request, *world) : "this oracle cannot open a seed";
        if (!error.empty()) {
            if (backend_.close)
                backend_.close();
            return Fail(error);
        }
        world_ = std::move(world);
        BuildIndex();
        return Describe();
    }

    void BuildIndex() {
        const CrossWorldSession& s = *world_->session;
        index_ = Index{};
        for (size_t i = 0; i < s.allChecks.size(); ++i)
            index_.location[s.allChecks[i].game].emplace(s.allChecks[i].name, i);
        for (const auto* pool : { &s.advItems, &s.junkItems })
            for (const CwItem& item : *pool) {
                index_.knownItem[item.game].insert(item.name);
                index_.itemByName[item.game].emplace(item.name, item);
                if (!index_.filler[item.game] && item.discardable())
                    index_.filler[item.game] = &item;
            }
        for (const auto* placements : { &s.lockedPlacements, &s.forcedPlacements })
            for (const CwPlacement& p : *placements) {
                index_.knownItem[p.item.game].insert(p.item.name);
                index_.itemByName[p.item.game].emplace(p.item.name, p.item);
            }
        for (const std::string& n : s.sharedStartingMm)
            index_.knownItem[GAME_MM].insert(n);
        index_.winCondition = WinConditionFor(*world_);
        index_.neverReachable.assign(s.allChecks.size(), false);
        std::vector<std::string> every[2];
        for (const auto* pool : { &s.advItems, &s.junkItems })
            for (const CwItem& item : *pool)
                every[item.game].push_back(item.name);
        const CwFixResult all = world_->session->ReachableFixpoint(s.lockedPlacements, every[GAME_OOT], every[GAME_MM]);
        for (size_t i = 0; i < s.allChecks.size(); ++i) {
            const auto& reach = s.allChecks[i].game == GAME_OOT ? all.ootReachable : all.mmReachable;
            index_.neverReachable[i] = reach.count(s.allChecks[i].name) == 0;
        }
    }

    static nlohmann::json GoalJson(const CwGoal& goal) {
        return { { "type", goal.hunt ? "triforceHunt" : "bosses" },
                 { "requiredPieces", goal.required },
                 { "totalPieces", goal.total } };
    }

    nlohmann::json Describe() const {
        const OracleWorld& w = *world_;
        const CrossWorldSession& s = *w.session;
        const auto traps =
            std::array<std::unordered_set<std::string>, 2>{ TrapNames(w.setup.sohDump), TrapNames(w.setup.mmDump) };
        nlohmann::json locations = nlohmann::json::array();
        for (const CwCheck& c : s.allChecks)
            locations.push_back({ OracleGameTag(c.game), c.name });
        nlohmann::json never = nlohmann::json::array();
        for (size_t i = 0; i < index_.neverReachable.size(); ++i)
            if (index_.neverReachable[i])
                never.push_back(i);

        struct PoolKey {
            GameId game;
            std::string name;
            bool advancement;
            CwCat cat;
            bool operator<(const PoolKey& o) const {
                return std::tie(game, name, advancement, cat) < std::tie(o.game, o.name, o.advancement, o.cat);
            }
        };
        std::map<PoolKey, int> pool;
        for (const auto* items : { &s.advItems, &s.junkItems })
            for (const CwItem& item : *items)
                ++pool[{ item.game, item.name, item.advancement, item.cat }];
        nlohmann::json items = nlohmann::json::array();
        for (const auto& [key, count] : pool) {
            const CwItem item{ key.game, key.name, key.advancement, key.cat };
            items.push_back({ { "game", OracleGameTag(key.game) },
                              { "name", key.name },
                              { "count", count },
                              { "class", OracleItemClass(item, traps[key.game].count(key.name) > 0) },
                              { "category", CwCatName(key.cat) },
                              { "advancement", key.advancement } });
        }
        auto placementJson = [](const CwPlacement& p) {
            return nlohmann::json{ { "game", OracleGameTag(p.check.game) },
                                   { "check", p.check.name },
                                   { "item", p.item.name },
                                   { "itemGame", OracleGameTag(p.item.game) },
                                   { "advancement", p.item.advancement } };
        };
        nlohmann::json fixed = nlohmann::json::array(), forced = nlohmann::json::array();
        for (const CwPlacement& p : s.lockedPlacements)
            fixed.push_back(placementJson(p));
        for (const CwPlacement& p : s.forcedPlacements)
            forced.push_back(placementJson(p));

        nlohmann::json effective = {
            { "games", ComboGamesKey(w.config.games) },  { "startingGame", w.mmStart ? "MM" : "OOT" },
            { "goal", GoalJson(w.config.goal) },         { "sharedItems", SharedKeysFromMask(s.effectiveSharedMask) },
            { "unifiedInventory", w.config.unified },    { "formLogicCredit", w.config.formLogicCredit },
            { "lostWoodsLink", w.config.lostWoodsLink }, { "ootAccess", OracleOotAccessName(w.ootAccess) },
            { "poolTrimmed", s.trimmedTotal },           { "poolPadded", s.paddedTotal },
        };
        return { { "masterSeed", w.masterSeed },
                 { "effective", std::move(effective) },
                 { "locations", std::move(locations) },
                 { "neverReachable", std::move(never) },
                 { "items", std::move(items) },
                 { "fixed", std::move(fixed) },
                 { "forced", std::move(forced) },
                 { "startingOwned", { { "oot", s.ootForcedOwned }, { "mm", s.mmForcedOwned } } },
                 { "win", { { "condition", index_.winCondition }, { "requiredPieces", w.config.goal.required } } } };
    }

    // Item counts of one game -> the repeated names the oracle takes. Names the pool does not hold are collected.
    std::string OwnedFromCounts(const nlohmann::json& request, GameId game, std::vector<std::string>& owned,
                                nlohmann::json& unknown) const {
        const auto& items = request.value("items", nlohmann::json::object());
        const std::string tag = OracleGameTag(game);
        if (!items.is_object() || !items.contains(tag))
            return "";
        if (!items[tag].is_object())
            return "items." + tag + " must map names to counts";
        for (const auto& [name, count] : items[tag].items()) {
            if (!count.is_number_integer() || count.get<int>() < 0)
                return "items." + tag + "." + name + " must be a count of zero or more";
            if (!index_.knownItem[game].count(name))
                unknown.push_back({ tag, name });
            owned.insert(owned.end(), static_cast<size_t>(count.get<int>()), name);
        }
        return "";
    }

    nlohmann::json Reach(const nlohmann::json& request) {
        if (!world_)
            return Fail("no seed is open: call open first");
        std::vector<std::string> owned[2];
        nlohmann::json unknown = nlohmann::json::array();
        for (GameId g : { GAME_OOT, GAME_MM }) {
            const std::string error = OwnedFromCounts(request, g, owned[g], unknown);
            if (!error.empty())
                return Fail(error);
        }
        const CrossWorldSession& s = *world_->session;
        const CwFixResult fr = world_->session->ReachableFixpoint(s.lockedPlacements, owned[GAME_OOT], owned[GAME_MM]);

        std::vector<bool> reached(s.allChecks.size());
        for (size_t i = 0; i < reached.size(); ++i)
            reached[i] =
                (s.allChecks[i].game == GAME_OOT ? fr.ootReachable : fr.mmReachable).count(s.allChecks[i].name) > 0;
        // The credited-inside checks: fixed[] first, then forced[].
        std::vector<bool> fixedReached;
        for (const auto* credited : { &s.lockedPlacements, &s.forcedPlacements })
            for (const CwPlacement& p : *credited)
                fixedReached.push_back(
                    (p.check.game == GAME_OOT ? fr.ootReachable : fr.mmReachable).count(p.check.name) > 0);

        const bool ganon = fr.ootReachable.count(kOotGanonCheck) > 0;
        const bool majora = fr.mmReachable.count(kMmMajoraCheck) > 0;
        int pieces = 0;
        for (GameId g : { GAME_OOT, GAME_MM })
            for (const std::string& name : owned[g])
                pieces += CwIsTriforcePiece(g, name) ? 1 : 0;
        bool won = ganon && majora;
        if (index_.winCondition == kWinPieces)
            won = pieces >= world_->config.goal.required;
        else if (index_.winCondition == kWinGanon)
            won = ganon;
        else if (index_.winCondition == kWinMajora)
            won = majora;
        return { { "mask", OracleMaskToHex(reached) },
                 { "fixedMask", OracleMaskToHex(fixedReached) },
                 { "win", { { "ganon", ganon }, { "majora", majora }, { "pieces", pieces }, { "won", won } } },
                 { "unknown", std::move(unknown) } };
    }

    // The request's placements -> the full placement list a seed is written from, in a fixed order: the confined
    // pre-placements, then every location in `locations` order, then the forced ones.
    std::string CollectPlacements(const nlohmann::json& request, std::vector<CwPlacement>& placements,
                                  nlohmann::json& others) {
        const CrossWorldSession& s = *world_->session;
        if (!request.contains("placements") || !request["placements"].is_array())
            return "finalize needs a \"placements\" array";
        std::vector<CwItem> own(s.allChecks.size(), CwItem{ GAME_OOT, "", false });
        std::vector<char> filledBy(s.allChecks.size(), 0); // 1 = native item, 2 = another player's item
        for (const auto& entry : request["placements"]) {
            GameId game;
            if (!entry.is_object() || !OracleGameFromTag(entry.value("game", std::string()), game))
                return "a placement needs \"game\" (oot or mm)";
            const std::string check = entry.value("check", std::string());
            const auto at = index_.location[game].find(check);
            if (at == index_.location[game].end())
                return std::string("placement at unknown ") + OracleGameTag(game) + " location '" + check + "'";
            const size_t bit = at->second;
            if (filledBy[bit])
                return "location '" + check + "' is placed twice";
            if (entry.contains("other")) {
                const auto& o = entry["other"];
                const CwItem* pad = index_.filler[game];
                if (!pad)
                    return std::string("the ") + OracleGameTag(game) +
                           " pool has no junk to stand in for another player's item";
                own[bit] = *pad;
                filledBy[bit] = 2;
                nlohmann::json other = { { "game", OracleGameTag(game) },
                                         { "check", check },
                                         { "player", o.value("player", std::string()) },
                                         { "item", o.value("name", std::string()) },
                                         { "advancement", o.value("advancement", false) } };
                if (o.contains(kApClassKey)) {
                    other[kApClassKey] = o[kApClassKey];
                }
                others.push_back(std::move(other));
                continue;
            }
            GameId itemGame;
            if (!entry.contains("item") || !OracleGameFromTag(entry["item"].value("game", std::string()), itemGame))
                return "placement at '" + check + "' needs an \"item\" {game, name} or an \"other\"";
            const std::string name = entry["item"].value("name", std::string());
            const auto info = index_.itemByName[itemGame].find(name);
            if (info == index_.itemByName[itemGame].end())
                return std::string("placement at '") + check + "' names an item the " + OracleGameTag(itemGame) +
                       " pool does not hold: '" + name + "'";
            own[bit] = info->second;
            filledBy[bit] = 1;
        }
        placements = s.lockedPlacements;
        std::string missing;
        size_t missingCount = 0;
        for (size_t i = 0; i < s.allChecks.size(); ++i) {
            if (!filledBy[i]) {
                if (!index_.neverReachable[i]) {
                    ++missingCount;
                    if (missingCount <= 5)
                        missing += (missing.empty() ? "" : ", ") + s.allChecks[i].name;
                    continue;
                }
                const CwItem* pad = index_.filler[s.allChecks[i].game];
                if (!pad)
                    return std::string("the ") + OracleGameTag(s.allChecks[i].game) +
                           " pool has no junk to fill a location nothing can reach";
                own[i] = *pad;
            }
            placements.push_back({ s.allChecks[i], own[i] });
        }
        if (missingCount)
            return std::to_string(missingCount) + " reachable location(s) have no placement, e.g. " + missing;
        placements.insert(placements.end(), s.forcedPlacements.begin(), s.forcedPlacements.end());
        return "";
    }

    // The seed's identity on the file select: the slot and the room, so two slots of one room show different hashes.
    static uint32_t ApDisplaySeed(const OracleWorld& world, const nlohmann::json& info) {
        if (!info.is_object())
            return 0; // StampApSeed refuses it
        return CwHashName(info.value(kApSlotNameKey, std::string()) + ":" + info.value(kApSeedNameKey, std::string()) +
                          ":" + std::to_string(world.masterSeed));
    }

    nlohmann::json Finalize(const nlohmann::json& request) {
        if (!world_)
            return Fail("no seed is open: call open first");
        std::vector<CwPlacement> placements;
        nlohmann::json others = nlohmann::json::array();
        std::string error = CollectPlacements(request, placements, others);
        if (!error.empty())
            return Fail(error);
        const bool archipelago = request.contains(kApKey);
        if (!archipelago && !others.empty())
            return Fail(std::string("another player's item needs \"") + kApKey + "\" in the request");
        nlohmann::json seed;
        error = backend_.finalize ? backend_.finalize(*world_, placements, seed) : "this oracle cannot finalize";
        if (!error.empty())
            return Fail(error);
        if (archipelago) {
            error = StampApSeed(seed, request[kApKey], others, ApDisplaySeed(*world_, request[kApKey]));
            if (!error.empty())
                return Fail(error);
        }
        if (!request.contains("out"))
            return { { "seed", std::move(seed) } };
        const std::string path = request["out"].get<std::string>();
        std::ofstream file(path, std::ios::trunc);
        file << seed.dump(2);
        if (!file)
            return Fail("could not write '" + path + "'");
        return { { "path", path } };
    }

    OracleBackend backend_;
    std::unique_ptr<OracleWorld> world_;
    Index index_;
};

} // namespace ComboRando
