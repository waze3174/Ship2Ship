// combo/rando/SharedItems.h
// ComboShip: OoTMM-style Shared Items — one item counts for both games. Header-only table + mask
// helpers, compiled into exe, soh.dll, 2ship.dll, comborando. See docs/deviations/rando.md.
#pragma once

#include <algorithm>
#include <cstdint>
#include <set>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace ComboRando {

enum SharedFamily {
    SF_BOW = 0,
    SF_BOMB_BAG,
    SF_BOMBCHU_BAG,
    SF_MAGIC,
    SF_WALLET,
    SF_HOOKSHOT,
    SF_FIRE_ARROWS,
    SF_ICE_ARROWS,
    SF_LIGHT_ARROWS,
    SF_LENS,
    SF_EPONAS_SONG,
    SF_SONG_OF_STORMS,
    SF_GORON_MASK,
    SF_ZORA_MASK,
    SF_KEATON_MASK,
    SF_BUNNY_HOOD,
    SF_MASK_OF_TRUTH,
    // Cross-game teleport songs (see "Cross-game teleport songs" in docs/deviations/rando.md).
    SF_SONG_OF_SOARING,
    SF_MINUET_OF_FOREST, // the six warp songs stay contiguous and in OOT warp-index order
    SF_BOLERO_OF_FIRE,
    SF_SERENADE_OF_WATER,
    SF_REQUIEM_OF_SPIRIT,
    SF_NOCTURNE_OF_SHADOW,
    SF_PRELUDE_OF_LIGHT,
    // Owner ruling 2026-11-12: POOLED families (see SharedFamilyDef::pooled). Bottles: all 4 OOT + all 6 MM
    // bottles are placed, and every one found in either game counts in both (10 per game).
    SF_BOTTLES,
    // Owner ruling 2026-11-12 (every item in both games): equivalents that map one-to-one.
    SF_OCARINA,
    SF_SONG_OF_TIME,
    SF_COUNT
};

inline bool IsTeleportSong(SharedFamily f) {
    return f >= SF_SONG_OF_SOARING && f <= SF_PRELUDE_OF_LIGHT;
}
// Guest = the game that only has the song through ComboShip. Soaring is MM's; the warp songs are OOT's.
inline bool TeleportSongGuestIsOot(SharedFamily f) {
    return f == SF_SONG_OF_SOARING;
}

struct SharedFamilyDef {
    SharedFamily family;
    const char* key;     // spoiler/CLI key
    const char* cvar;    // menu CVar
    const char* label;   // menu checkbox label
    const char* tooltip; // OoTMM-style tooltip
    const char* ootName; // OOT pool item name
    const char* mmName;  // MM pool item name (trimmed / oracle-credited)
    int mmTierCap;       // max tiers MM can hold
    bool isMask;         // requires OOT Shuffle Masks
    bool mmHasItem;      // false = MM has no pool copy of this item (no trim, no oracle mirror)
    bool ootToMmOnly;    // true = one-way OOT->MM; MM's tier signal isn't a reliable OOT source
    // Pooled (not progressive): both games keep ALL their pool copies; a copy found in either game counts in
    // both, so the shared amount is the SUM of what each game found (not the max tier). The runtime reports
    // what was found in that game (GetSharedTier) and raises to "hold this many" (RaiseSharedTier).
    bool pooled = false;
};

// The six warp-song rows share these (one menu toggle for the group).
inline constexpr const char* kSharedWarpSongsCVar = "gCombo.Rando.Shared.WarpSongs";
inline constexpr const char* kSharedWarpSongsLabel = "Shared Warp Songs";
inline constexpr const char* kSharedWarpSongsTooltip =
    "One copy of each Ocarina of Time warp song counts for both games. Requires MM's OoT Warp Songs.";

inline const SharedFamilyDef* SharedFamilyTable() {
    static const SharedFamilyDef table[SF_COUNT] = {
        { SF_BOW, "bows", "gCombo.Rando.Shared.Bows", "Shared Bows", "One Progressive Bow counts for both games.",
          "Progressive Bow", "Progressive Bow", 3, false, true, false },
        { SF_BOMB_BAG, "bombBags", "gCombo.Rando.Shared.BombBags", "Shared Bomb Bags",
          "One Progressive Bomb Bag counts for both games.", "Progressive Bomb Bag", "Progressive Bomb Bag", 3, false,
          true, false },
        { SF_BOMBCHU_BAG, "bombchuBags", "gCombo.Rando.Shared.BombchuBags", "Shared Bombchu Bags",
          "One Bombchu Bag counts for both games. Requires OOT Bombchu Bag != None.", "Bombchu Bag", "", 1, false,
          false, true },
        { SF_MAGIC, "magic", "gCombo.Rando.Shared.Magic", "Shared Magic",
          "One Progressive Magic Meter counts for both games.", "Progressive Magic Meter", "Progressive Magic", 2,
          false, true, false },
        { SF_WALLET, "wallets", "gCombo.Rando.Shared.Wallets", "Shared Wallets",
          "One Progressive Wallet counts for both games. Forces Shuffle Child Wallet off.", "Progressive Wallet",
          "Progressive Wallet", 3, false, true, false },
        { SF_HOOKSHOT, "hookshot", "gCombo.Rando.Shared.Hookshot", "Shared Hookshot",
          "One Progressive Hookshot counts for both games (MM only needs tier 1).", "Progressive Hookshot", "Hookshot",
          1, false, true, false },
        { SF_FIRE_ARROWS, "fireArrows", "gCombo.Rando.Shared.FireArrows", "Shared Fire Arrows",
          "One Fire Arrows counts for both games.", "Fire Arrows", "Fire Arrows", 1, false, true, false },
        { SF_ICE_ARROWS, "iceArrows", "gCombo.Rando.Shared.IceArrows", "Shared Ice Arrows",
          "One Ice Arrows counts for both games.", "Ice Arrows", "Ice Arrows", 1, false, true, false },
        { SF_LIGHT_ARROWS, "lightArrows", "gCombo.Rando.Shared.LightArrows", "Shared Light Arrows",
          "One Light Arrows counts for both games.", "Light Arrows", "Light Arrows", 1, false, true, false },
        { SF_LENS, "lens", "gCombo.Rando.Shared.Lens", "Shared Lens of Truth",
          "One Lens of Truth counts for both games.", "Lens of Truth", "Lens of Truth", 1, false, true, false },
        { SF_EPONAS_SONG, "eponasSong", "gCombo.Rando.Shared.EponasSong", "Shared Epona's Song",
          "One Epona's Song counts for both games.", "Epona's Song", "Epona's Song", 1, false, true, false },
        { SF_SONG_OF_STORMS, "songOfStorms", "gCombo.Rando.Shared.SongOfStorms", "Shared Song of Storms",
          "One Song of Storms counts for both games.", "Song of Storms", "Song of Storms", 1, false, true, false },
        { SF_GORON_MASK, "goronMask", "gCombo.Rando.Shared.GoronMask", "Shared Goron Mask",
          "One Goron Mask counts for both games. Requires OOT Shuffle Masks.", "Goron Mask", "Goron Mask", 1, true,
          true, false },
        { SF_ZORA_MASK, "zoraMask", "gCombo.Rando.Shared.ZoraMask", "Shared Zora Mask",
          "One Zora Mask counts for both games. Requires OOT Shuffle Masks.", "Zora Mask", "Zora Mask", 1, true, true,
          false },
        { SF_KEATON_MASK, "keatonMask", "gCombo.Rando.Shared.KeatonMask", "Shared Keaton Mask",
          "One Keaton Mask counts for both games. Requires OOT Shuffle Masks.", "Keaton Mask", "Keaton Mask", 1, true,
          true, false },
        { SF_BUNNY_HOOD, "bunnyHood", "gCombo.Rando.Shared.BunnyHood", "Shared Bunny Hood",
          "One Bunny Hood counts for both games. Requires OOT Shuffle Masks.", "Bunny Hood", "Bunny Hood", 1, true,
          true, false },
        { SF_MASK_OF_TRUTH, "maskOfTruth", "gCombo.Rando.Shared.MaskOfTruth", "Shared Mask of Truth",
          "One Mask of Truth counts for both games. Requires OOT Shuffle Masks.", "Mask of Truth", "Mask of Truth", 1,
          true, true, false },
        // Teleport songs need their feature on in both games; the six warp rows share one CVar.
        { SF_SONG_OF_SOARING, "songOfSoaring", "gCombo.Rando.Shared.SongOfSoaring", "Shared Song of Soaring",
          "One Song of Soaring counts for both games. Requires OOT's Song of Soaring (soar to Termina).",
          "Song of Soaring", "Song of Soaring", 1, false, true, false },
        { SF_MINUET_OF_FOREST, "minuetOfForest", kSharedWarpSongsCVar, kSharedWarpSongsLabel, kSharedWarpSongsTooltip,
          "Minuet of Forest", "Minuet of Forest", 1, false, true, false },
        { SF_BOLERO_OF_FIRE, "boleroOfFire", kSharedWarpSongsCVar, kSharedWarpSongsLabel, kSharedWarpSongsTooltip,
          "Bolero of Fire", "Bolero of Fire", 1, false, true, false },
        { SF_SERENADE_OF_WATER, "serenadeOfWater", kSharedWarpSongsCVar, kSharedWarpSongsLabel, kSharedWarpSongsTooltip,
          "Serenade of Water", "Serenade of Water", 1, false, true, false },
        { SF_REQUIEM_OF_SPIRIT, "requiemOfSpirit", kSharedWarpSongsCVar, kSharedWarpSongsLabel, kSharedWarpSongsTooltip,
          "Requiem of Spirit", "Requiem of Spirit", 1, false, true, false },
        { SF_NOCTURNE_OF_SHADOW, "nocturneOfShadow", kSharedWarpSongsCVar, kSharedWarpSongsLabel,
          kSharedWarpSongsTooltip, "Nocturne of Shadow", "Nocturne of Shadow", 1, false, true, false },
        { SF_PRELUDE_OF_LIGHT, "preludeOfLight", kSharedWarpSongsCVar, kSharedWarpSongsLabel, kSharedWarpSongsTooltip,
          "Prelude of Light", "Prelude of Light", 1, false, true, false },
        { SF_BOTTLES, "bottles", "gCombo.Rando.Shared.Bottles", "Shared Bottles",
          "All 4 OOT and all 6 MM bottles are placed; each one counts in both games (10 bottles each). Bottles past "
          "a game's own slots wait on the Bottle Shelf.",
          "Empty Bottle", "Empty Bottle", 10, false, true, false, /*pooled*/ true },
        { SF_OCARINA, "ocarina", "gCombo.Rando.Shared.Ocarina", "Shared Ocarina",
          "Any ocarina counts for both games (OOT's Fairy Ocarina or Ocarina of Time, MM's Ocarina of Time).",
          "Progressive Ocarina", "Ocarina of Time", 1, false, true, false },
        { SF_SONG_OF_TIME, "songOfTime", "gCombo.Rando.Shared.SongOfTime", "Shared Song of Time",
          "One Song of Time counts for both games.", "Song of Time", "Song of Time", 1, false, true, false },
    };
    return table;
}

inline const SharedFamilyDef& SharedFamilyByIndex(int i) {
    return SharedFamilyTable()[i];
}

// Owner ruling 2026-11-12 (Unified Inventory): whatever MM starts with, OOT starts with too. MM's starting kit is
// the `gRando.StartingItems` list in its settings dump (which every seed carries), so generation and reload derive
// the same mask. Bits: what OOT's FinalizeSettings forces on (settings.cpp).
enum MirrorStartBit : uint32_t {
    MSB_OCARINA = 1u << 0,      // -> RSK_STARTING_OCARINA = Fairy Ocarina
    MSB_KOKIRI_SWORD = 1u << 1, // -> RSK_STARTING_KOKIRI_SWORD
    MSB_SHIELD = 1u << 2,       // Hero's Shield -> RSK_STARTING_HYLIAN_SHIELD
    MSB_SONG_OF_TIME = 1u << 3, // -> RSK_STARTING_SONG_OF_TIME
};
inline uint32_t MirrorStartMaskFromMmSettings(const nlohmann::json& mmSettings) {
    uint32_t mask = 0;
    if (!mmSettings.is_object())
        return mask;
    for (const auto& n : mmSettings.value("gRando.StartingItems", nlohmann::json::array())) {
        if (!n.is_string())
            continue;
        // Seeds store MM's RandoItemId names (RI_*); display names are accepted too.
        const std::string s = n.get<std::string>();
        if (s == "RI_OCARINA" || s == "Ocarina of Time")
            mask |= MSB_OCARINA;
        else if (s == "RI_PROGRESSIVE_SWORD" || s == "RI_SWORD_KOKIRI" || s == "Progressive Sword" ||
                 s == "Kokiri Sword")
            mask |= MSB_KOKIRI_SWORD;
        else if (s == "RI_SHIELD_HERO" || s == "Hero's Shield")
            mask |= MSB_SHIELD;
        else if (s == "RI_SONG_TIME" || s == "Song of Time")
            mask |= MSB_SONG_OF_TIME;
    }
    return mask;
}

// Every pool name that IS one of a pooled family's copies, per game (bottles come pre-filled in the pools).
inline const std::vector<std::string>& PooledNames(SharedFamily f, bool ootSide) {
    static const std::vector<std::string> kNone;
    static const std::vector<std::string> kOotBottles = {
        "Empty Bottle",
        "Bottle with Milk",
        "Bottle with Red Potion",
        "Bottle with Green Potion",
        "Bottle with Blue Potion",
        "Bottle with Fairy",
        "Bottle with Fish",
        "Bottle with Blue Fire",
        "Bottle with Bugs",
        "Bottle with Poe",
        "Bottle with Big Poe",
        "Bottle with Ruto's Letter",
    };
    static const std::vector<std::string> kMmBottles = {
        "Empty Bottle", "Bottle of Milk", "Bottle of Chateau Romani", "Bottle With Gold Dust", "Bottle with Red Potion",
    };
    if (f == SF_BOTTLES)
        return ootSide ? kOotBottles : kMmBottles;
    return kNone;
}

// Logic mirror for pooled families: every copy owned on one side is credited to the other as its generic
// name. `toOot`/`toMm` remember what was pushed so repeat calls (fixpoint loops) never duplicate, and so
// mirrored copies are never counted as found on the side that received them.
struct PooledMirrorState {
    size_t toOot[SF_COUNT] = { 0 };
    size_t toMm[SF_COUNT] = { 0 };
};

inline void ApplyPooledMirror(uint32_t sharedMask, std::vector<std::string>& ootOwned,
                              std::vector<std::string>& mmOwned, PooledMirrorState& st) {
    if (sharedMask == 0)
        return;
    auto count = [](const std::vector<std::string>& owned, const std::vector<std::string>& names) {
        size_t n = 0;
        for (const auto& o : owned)
            if (std::find(names.begin(), names.end(), o) != names.end())
                ++n;
        return n;
    };
    for (int i = 0; i < SF_COUNT; ++i) {
        if (!(sharedMask & (1u << i)))
            continue;
        const auto& def = SharedFamilyByIndex(i);
        if (!def.pooled)
            continue;
        const auto f = static_cast<SharedFamily>(i);
        const size_t ootFound = count(ootOwned, PooledNames(f, true)) - st.toOot[i];
        const size_t mmFound = count(mmOwned, PooledNames(f, false)) - st.toMm[i];
        for (; st.toMm[i] < ootFound; ++st.toMm[i])
            mmOwned.push_back(def.mmName);
        for (; st.toOot[i] < mmFound; ++st.toOot[i])
            ootOwned.push_back(def.ootName);
    }
}

inline uint32_t SharedMaskFromKeys(const nlohmann::json& keys) {
    uint32_t mask = 0;
    if (!keys.is_array())
        return mask;
    for (const auto& k : keys) {
        if (!k.is_string())
            continue;
        const std::string s = k.get<std::string>();
        for (int i = 0; i < SF_COUNT; ++i)
            if (s == SharedFamilyTable()[i].key)
                mask |= (1u << i);
    }
    return mask;
}

inline uint32_t SharedMaskAll() {
    return (SF_COUNT >= 32) ? 0xFFFFFFFFu : ((1u << SF_COUNT) - 1u);
}

// Names to leave untagged by SuffixCrossGameItems: both games' pool name for every effective family.
inline std::set<std::string> SharedUntaggedNames(uint32_t mask) {
    std::set<std::string> out;
    for (int i = 0; i < SF_COUNT; ++i) {
        if (!(mask & (1u << i)))
            continue;
        out.insert(SharedFamilyTable()[i].ootName);
        if (SharedFamilyTable()[i].mmHasItem)
            out.insert(SharedFamilyTable()[i].mmName);
    }
    return out;
}

inline std::vector<std::string> SharedKeysFromMask(uint32_t mask) {
    std::vector<std::string> out;
    for (int i = 0; i < SF_COUNT; ++i)
        if (mask & (1u << i))
            out.push_back(SharedFamilyTable()[i].key);
    return out;
}

} // namespace ComboRando
