#pragma once

// combo/ComboSharedHealth.h — ComboShip: one health bar for both games (owner ruling 2026-11-13).
//
// - Current health is shared: it carries exactly across every room and every switch between Hyrule and Termina.
// - Max health is shared and balanced at HALF VALUE: each game's own pickups are counted in its own units (a heart
//   piece = 1 quarter, a heart container = 4 quarters, which is exactly how each game itself grows), and the two
//   games' totals are added as EIGHTHS of a heart. So every piece or container from either game is worth half of what
//   it is at home; both games fully collected (17 extra hearts each) = 3 + 34/2 = 20 hearts, vanilla's maximum.
// - Double Defense is shared: from either game's Great Fairy, it is on in both.
// - Current magic is shared (owner 2026-11-14: "magic meter isn't synced between termina and hyrule"): what is left
//   in the meter carries across every switch, capped to the meter the arriving game has. Both games refill the meter
//   on entry from magicFillTarget (MAGIC_STATE_STEP_CAPACITY / FILL), so the shared amount goes in as that target.
//
// Each game keeps its native fields as the display: healthCapacity = shared whole hearts, the heart-piece counter =
// the shared leftover quarters (0-3). A native pickup changes those fields; the tick sees the difference from what it
// last wrote and adds it to that game's tally. Saves that predate this start from their natives (migration).
// Header-only; each game supplies its fields (soh ComboSharedHealth.cpp, mm ComboSharedHealth.cpp). The state rides
// in the combo container next to the saves (combo.health), like the Bottle Shelf.

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace ComboHealth {

constexpr int kGameOot = 0;
constexpr int kGameMm = 1;
constexpr int16_t kBaseCapacity = 0x30;  // 3 hearts
constexpr int16_t kMaxCapacity = 0x140;  // 20 hearts
constexpr int kHeartPieceShift = 28;     // both games: questItems bits 28-31 = heart pieces toward the next heart
constexpr uint32_t kHeartPieceMask = 0xF0000000u;

struct State {
    int quarters[2] = { 0, 0 }; // each game's own pickups: piece = 1, container = 4
    bool seen[2] = { false, false };
    int health = -1;            // shared current health (OOT/MM units: 0x10 per heart), -1 = not yet known
    bool doubleDefense = false;
    int magic = -1;             // shared current magic (both games: 0x30 per meter level), -1 = not yet known
    // What each game's save last held from us (capacity, pieces), carried with the shared state: on arrival, anything
    // above it was added while that game was dormant (an item delivered into its save from the other game, a
    // teammate's pickup) and is counted. -1 = not yet written.
    int16_t written[2] = { -1, -1 };
    int writtenPieces[2] = { -1, -1 };
    // Shared consumables (owner 2026-11-15: "sync up every consumable between games"), see TickAmmo. -1 = not known.
    int ammo[7] = { -1, -1, -1, -1, -1, -1, -1 };
    int writtenAmmo[2][7] = { { -1, -1, -1, -1, -1, -1, -1 }, { -1, -1, -1, -1, -1, -1, -1 } };
    // MM's "this form's transformation has been watched" weekEventReg (D_8085D908: 30_80 FD, 30_20 Goron, 30_40 Zora,
    // 30_10 Deku), one record for both games: bit (1 << PLAYER_FORM_*), MM's form numbering. MM may skip the
    // transformation cutscene with a button only once it has been watched.
    int seenTransform = 0;
};

// The consumables both games carry, one count for both.
// Seeds: only OOT has a bullet bag, but OOT's Slingshot fires in Termina too (ComboOotItems.cpp), and spends from
// this shared count there (MM_ComboSharedSeeds), so a seed shot in Termina is gone in Hyrule.
enum AmmoKind { kAmmoArrows = 0, kAmmoBombs, kAmmoBombchus, kAmmoSticks, kAmmoNuts, kAmmoBeans, kAmmoSeeds, kAmmoKinds };

struct AmmoSlot {
    int8_t* count = nullptr; // the game's AMMO() cell; nullptr = this game has no such consumable
    bool owned = false;      // the item is in the inventory (the count means nothing without it)
    int cap = 0;             // what this game's bag holds now
};

// The native fields of the running game.
struct Native {
    int16_t* healthCapacity;
    int16_t* health;
    uint32_t* questItems;
    uint8_t* doubleDefenseFlag; // OOT isDoubleDefenseAcquired / MM playerData.doubleDefense
    int8_t* defenseHearts;      // 20 with Double Defense, as both games set it
    // Magic (same fields, same units in both games; nullptr = this game doesn't take part)
    int8_t* magic = nullptr;
    int8_t* magicLevel = nullptr;      // 0 none, 1 normal, 2 double
    uint8_t* isMagicAcquired = nullptr;
    int16_t* magicState = nullptr;
    int16_t* magicFillTarget = nullptr;
    // Where a change in progress is heading (each game has its own fields; nullptr = not this game's):
    int16_t* magicTarget = nullptr;    // OOT: add / spend target (states 1, 2 and OOT's 10 = ADD)
    int16_t* magicToAdd = nullptr;     // MM: queued additions (Magic_UpdateAddRequest pays them in at 4 a frame)
    int16_t* magicToConsume = nullptr; // MM: the spend in progress (states 1, 2)
    AmmoSlot ammo[kAmmoKinds];
};

struct Runtime {
    State s;
    int16_t lastCapacity = -1; // what this game last wrote (to tell a native pickup from our own write)
    int lastPieces = -1;
    int16_t lastHealth = -1;
    bool pendingEntry = true;  // first tick after a save load: shared health goes INTO the game
    bool pendingMagic = true;  // same for magic, which waits for the game's own load refill to start / finish
    int lastAmmo[kAmmoKinds] = { -1, -1, -1, -1, -1, -1, -1 }; // the count this game showed last frame (-1 = not owned)
};

// Shared consumables. One count per kind for both games: whatever this game does to its count (a shot, a pickup, a
// shop, MM's Song of Time reset) moves the shared count by the same amount, and a game arriving -- or taking the item
// for the first time -- shows the shared count, as much of it as its own bag holds. What a smaller bag can't show is
// not thrown away: it is still there in the game with the bigger bag.
// A count that grew in a dormant game's save (an item delivered into it from the other game) is added on arrival, by
// what that save held when we last wrote it (writtenAmmo), the same way as the heart pieces.
inline void TickAmmo(Runtime& rt, int game, const Native& n) {
    State& s = rt.s;
    for (int i = 0; i < kAmmoKinds; i++) {
        const AmmoSlot& a = n.ammo[i];
        if (a.count == nullptr) {
            continue;
        }
        if (!a.owned) {
            rt.lastAmmo[i] = -1;
            continue;
        }
        const int cap = (a.cap > *a.count) ? a.cap : *a.count;
        int v = *a.count;
        if (rt.lastAmmo[i] < 0) {
            const int w = s.writtenAmmo[game][i];
            if (s.ammo[i] < 0) {
                s.ammo[i] = v; // nothing shared yet: this game's count is it
            } else if (w < 0) {
                s.ammo[i] = (v > s.ammo[i]) ? v : s.ammo[i]; // this game's first count (old save / first item)
            } else if (v > w) {
                s.ammo[i] += v - w; // gained while dormant
            }
            v = (s.ammo[i] > cap) ? cap : s.ammo[i];
            *a.count = (int8_t)v;
        } else {
            s.ammo[i] += v - rt.lastAmmo[i];
            if (s.ammo[i] < 0) {
                s.ammo[i] = 0;
            }
        }
        rt.lastAmmo[i] = v;
        s.writtenAmmo[game][i] = v;
    }
}

constexpr int16_t kMagicPerLevel = 0x30;
constexpr int16_t kMagicStateIdle = 0;         // both games' MAGIC_STATE_IDLE
constexpr int16_t kMagicStateStepCapacity = 8; // MAGIC_STATE_STEP_CAPACITY
constexpr int16_t kMagicStateFill = 9;         // MAGIC_STATE_FILL

// Shared current magic: into the arriving game once, then follow what the game does with it.
inline void TickMagic(Runtime& rt, const Native& n) {
    State& s = rt.s;
    if ((n.magic == nullptr) || (*n.isMagicAcquired == 0) || (*n.magicLevel <= 0)) {
        return; // no meter in this game (yet): nothing to show, nothing to learn
    }
    const int cap = *n.magicLevel * kMagicPerLevel;
    const int16_t state = *n.magicState;
    if (rt.pendingMagic) {
        if (s.magic < 0) {
            rt.pendingMagic = false; // nothing shared yet: this game's own amount becomes the shared one below
        } else if ((state == kMagicStateStepCapacity) || (state == kMagicStateFill)) {
            *n.magicFillTarget = (int16_t)((s.magic > cap) ? cap : s.magic); // the load refill stops at the shared amount
            rt.pendingMagic = false;
        } else if (state == kMagicStateIdle) {
            *n.magic = (int8_t)((s.magic > cap) ? cap : s.magic);
            rt.pendingMagic = false;
        }
        return; // (spending / any other state in progress: wait for it)
    }
    // The meter as it is SETTLING, in every state: a fill / add / spend still running counts at its destination. Only
    // reading it while idle left the shared amount stale through a refill or a spell, so a door taken mid-change
    // carried the old amount into the other game (owner 2026-11-14: "magic meter still loses sync between rooms").
    int v = *n.magic;
    if ((state == kMagicStateStepCapacity) || (state == kMagicStateFill)) {
        v = *n.magicFillTarget;
    } else if ((n.magicTarget != nullptr) && ((state == 1) || (state == 2) || (state == 10))) {
        v = *n.magicTarget; // OOT CONSUME_SETUP / CONSUME / ADD
    } else if ((n.magicToConsume != nullptr) && ((state == 1) || (state == 2))) {
        v = *n.magic - *n.magicToConsume; // MM CONSUME_SETUP / CONSUME
    }
    if (n.magicToAdd != nullptr) {
        v += *n.magicToAdd;
    }
    s.magic = (v < 0) ? 0 : ((v > cap) ? cap : v);
}

inline int Pieces(const Native& n) {
    return (int)((*n.questItems & kHeartPieceMask) >> kHeartPieceShift);
}

inline int16_t SharedCapacity(const State& s) {
    const int eighths = s.quarters[kGameOot] + s.quarters[kGameMm];
    int cap = kBaseCapacity + (eighths / 8) * 0x10;
    return (int16_t)((cap > kMaxCapacity) ? kMaxCapacity : cap);
}

inline int SharedPieces(const State& s) {
    const int eighths = s.quarters[kGameOot] + s.quarters[kGameMm];
    return (SharedCapacity(s) >= kMaxCapacity) ? 0 : ((eighths % 8) / 2); // leftover eighths shown as quarters
}

// The shared heart-piece meter in eighths (0-7): each game's pause screen draws it (owner 2026-11-14: "show 1/8ths of
// hearts"); the native counter only holds whole quarters (SharedPieces), so odd eighths would otherwise not show.
inline int LeftoverEighths(const State& s) {
    const int eighths = s.quarters[kGameOot] + s.quarters[kGameMm];
    return (SharedCapacity(s) >= kMaxCapacity) ? 0 : (eighths % 8);
}

// Once per game frame while a save is in play.
inline void Tick(Runtime& rt, int game, const Native& n) {
    State& s = rt.s;
    const int pieces = Pieces(n);
    if (!s.seen[game]) {
        // First sight of this game's save: its native growth so far becomes its tally (pre-existing saves).
        int q = ((*n.healthCapacity - kBaseCapacity) / 0x10) * 4 + pieces;
        s.quarters[game] = (q > 0) ? q : 0;
        s.seen[game] = true;
    } else if ((rt.lastCapacity >= 0) || (s.written[game] >= 0)) {
        // A native pickup since our last write: a piece moves the counter (and rolls a heart at 4), a container adds
        // a heart. Both are exactly "quarters gained" in the game's own units. The first tick after a load or a game
        // switch has no runtime memory (the handoff resets it), so it compares with what the shared state says this
        // game's save last held: a heart delivered while the game was dormant still counts. (Since the B-M35
        // handoff those were dropped and overwritten -- owner 2026-11-14: the meter showed none after a piece and
        // a container.)
        const int16_t baseCap = (rt.lastCapacity >= 0) ? rt.lastCapacity : s.written[game];
        const int basePieces = (rt.lastCapacity >= 0) ? rt.lastPieces : s.writtenPieces[game];
        const int dq = ((*n.healthCapacity - baseCap) / 0x10) * 4 + (pieces - basePieces);
        if (dq > 0) {
            s.quarters[game] += dq;
        }
    }
    if (*n.doubleDefenseFlag) {
        s.doubleDefense = true;
    }

    const int16_t cap = SharedCapacity(s);
    *n.healthCapacity = cap;
    *n.questItems = (*n.questItems & ~kHeartPieceMask) | ((uint32_t)SharedPieces(s) << kHeartPieceShift);
    if (s.doubleDefense) {
        *n.doubleDefenseFlag = 1;
        *n.defenseHearts = 20;
    }

    if (rt.pendingEntry && (s.health >= 0)) {
        *n.health = (int16_t)((s.health > cap) ? cap : s.health); // carried in from the other game / last room
    } else if (*n.health > cap) {
        *n.health = cap;
    }
    rt.pendingEntry = false;
    s.health = *n.health;

    rt.lastCapacity = cap;
    rt.lastPieces = SharedPieces(s);
    s.written[game] = cap;
    s.writtenPieces[game] = rt.lastPieces;
    rt.lastHealth = *n.health;

    TickMagic(rt, n);
    TickAmmo(rt, game, n);
}

inline std::string ToJson(const Runtime& rt) {
    const State& s = rt.s;
    nlohmann::json j = { { "q", { s.quarters[0], s.quarters[1] } },
                         { "seen", { s.seen[0], s.seen[1] } },
                         { "health", s.health },
                         { "dd", s.doubleDefense },
                         { "magic", s.magic },
                         { "w", { s.written[0], s.written[1] } },
                         { "wp", { s.writtenPieces[0], s.writtenPieces[1] } },
                         { "ammo", s.ammo },
                         { "wa", s.writtenAmmo },
                         { "tf", s.seenTransform } };
    return j.dump();
}

// A save is being loaded (or the game is resuming): take the shared state, put it into the game on the next tick.
inline void FromJson(Runtime& rt, const char* json) {
    rt = Runtime{};
    try {
        const nlohmann::json j = nlohmann::json::parse((json != nullptr && json[0] != '\0') ? json : "{}");
        if (j.contains("q")) {
            rt.s.quarters[0] = j["q"][0].get<int>();
            rt.s.quarters[1] = j["q"][1].get<int>();
        }
        if (j.contains("seen")) {
            rt.s.seen[0] = j["seen"][0].get<bool>();
            rt.s.seen[1] = j["seen"][1].get<bool>();
        }
        rt.s.health = j.value("health", -1);
        rt.s.doubleDefense = j.value("dd", false);
        rt.s.magic = j.value("magic", -1);
        if (j.contains("w") && j.contains("wp")) {
            rt.s.written[0] = (int16_t)j["w"][0].get<int>();
            rt.s.written[1] = (int16_t)j["w"][1].get<int>();
            rt.s.writtenPieces[0] = j["wp"][0].get<int>();
            rt.s.writtenPieces[1] = j["wp"][1].get<int>();
        }
        if (j.contains("ammo") && j.contains("wa")) {
            const nlohmann::json& a = j["ammo"];
            const nlohmann::json& w = j["wa"];
            for (int i = 0; i < kAmmoKinds; i++) { // (a state from before seeds were shared has 6 entries)
                rt.s.ammo[i] = (i < (int)a.size()) ? a[i].get<int>() : -1;
                rt.s.writtenAmmo[0][i] = (i < (int)w[0].size()) ? w[0][i].get<int>() : -1;
                rt.s.writtenAmmo[1][i] = (i < (int)w[1].size()) ? w[1][i].get<int>() : -1;
            }
        }
        rt.s.seenTransform = j.value("tf", 0);
    } catch (...) { rt = Runtime{}; }
}

} // namespace ComboHealth
