// combo/rando/ComboBackToBackSettings.h
// ComboShip: the CVars of the back-to-back mode's settings page (Settings > Back to back). Each one is read once, when
// a back-to-back file is created, and baked into that file; changing it later never touches an existing file.
// Header-only and std only, so comboui, soh.dll, 2ship.dll and the launcher share it. See docs/deviations/back2back.md.
#pragma once

#include <cstddef>

namespace ComboRando {

// 1: a new back-to-back file plays Ocarina of Time's Master Quest, when oot-mq.o2r is loaded. 0 or unset: Normal Quest.
inline constexpr const char* kBackToBackMasterQuestCVar = "gCombo.BackToBack.MasterQuest";

// 1 or unset: the file select offers the back-to-back entry for new files. 0: it is hidden. Slots that already exist
// load and show their badge either way; only the new-file choice is hidden.
inline constexpr const char* kBackToBackShowCVar = "gCombo.BackToBack.Show";
inline constexpr int kBackToBackShowDefault = 1;

namespace BackToBack {

// The page's choices about what goes from Hyrule into Termina, as the bits of one int stored in the file's combo.mode
// ("options") when it is created. Every item that has a copy in Majora's Mask has a switch for whether it waits for
// that copy: a clear bit waits (the default), a set bit lets the item be used in Termina before Link finds Majora's
// Mask's own. The rest of the mode's rules are not choices and are in ComboBackToBackLocks.h.
enum Option : int {
    kHeartsCarry = 1 << 0,  // Termina starts with Ocarina of Time's hearts instead of three
    kMagicCarry = 1 << 1,   // OoT's double magic meter carries, once Majora's Mask's Great Fairy gives magic
    kBottlesCarry = 1 << 2, // OoT's bottles are usable in Termina
    kWalletCarry = 1 << 3,  // the wallet's size and the rupees carry
    kSongsCarry = 1 << 4,   // the songs OoT has learned are learned in Termina, once Link has MM's Ocarina
    kDinsFire = 1 << 5,     // the OoT-only items, one switch each
    kFaroresWind = 1 << 6,
    kNayrusLove = 1 << 7,
    kBoomerang = 1 << 8,
    kSlingshot = 1 << 9,
    kHammer = 1 << 10,
    kBoots = 1 << 11,
    kTunics = 1 << 12,
    kEarlySticks = 1 << 13, // usable before Majora's Mask's own copy is found, one switch each
    kEarlyNuts = 1 << 14,
    kEarlyBombs = 1 << 15,
    kEarlyBow = 1 << 16,
    kEarlyFireArrows = 1 << 17,
    kEarlyIceArrows = 1 << 18,
    kEarlyLightArrows = 1 << 19,
    kEarlyOcarina = 1 << 20, // both of OoT's ocarinas
    kEarlyBombchus = 1 << 21,
    kEarlyHookshot = 1 << 22, // the Hookshot and the Longshot
    kEarlyLens = 1 << 23,
    kEarlyBeans = 1 << 24,
    kEarlyBottles = 1 << 25, // with kBottlesCarry: bottles before Majora's Mask's first bottle
    kEarlyKeatonMask = 1 << 26,
    kEarlyBunnyHood = 1 << 27,
    kEarlyMaskOfTruth = 1 << 28,
    kEarlyGoronMask = 1 << 29, // OoT's Goron Mask transforms Link in Termina from the start, not worn plain first
    kEarlyZoraMask = 1 << 30,  // the same for the Zora Mask. Bit 31, the sign bit, stays unused.
};

// The layout of the options int, stored beside it in the slot's combo.mode (kOptionsLayoutKey). A mode without the key
// holds the first layout (bits 0 to 12), where bit 4 also meant "the masks both games have are usable at once";
// MigrateStoredOptions turns that into the mask bits.
inline constexpr int kOptionsLayout = 2;
inline constexpr int kFirstOptionsLayout = 1;

inline constexpr int kEarlyUseMasks =
    kEarlyKeatonMask | kEarlyBunnyHood | kEarlyMaskOfTruth | kEarlyGoronMask | kEarlyZoraMask;

enum class OptionGroup { Carry, OotOnlyItems, EarlyUse };

struct OptionDef {
    int bit;
    const char* cvar;
    bool defaultOn;
    OptionGroup group;
    const char* label;
    const char* tooltip;
};

inline constexpr const char* kOotOnlyTooltip = "Usable in Termina. Off: it stays in Hyrule.";
inline constexpr const char* kEarlyUseTooltip =
    "Usable in Termina from the start. Off: it waits until Link finds Majora's Mask's own.";
inline constexpr const char* kEarlyTransformMaskTooltip =
    "Transforms Link in Termina from the start. Off: Link can wear it there as a plain mask, and it transforms him "
    "only once he has Majora's Mask's own.";

inline constexpr OptionDef kOptions[] = {
    { kHeartsCarry, "gCombo.BackToBack.Hearts", false, OptionGroup::Carry, "Hearts",
      "Termina starts with the hearts Link has at the end of Ocarina of Time. Off: Termina starts with three." },
    { kMagicCarry, "gCombo.BackToBack.Magic", false, OptionGroup::Carry, "Double magic meter",
      "If Link has the double magic meter at the end of Ocarina of Time, Majora's Mask's meter becomes double too, "
      "once Majora's Mask's own Great Fairy gives Link magic. Off: Majora's Mask's meter is its own." },
    { kBottlesCarry, "gCombo.BackToBack.Bottles", true, OptionGroup::Carry, "Bottles",
      "Ocarina of Time's bottles and what is in them are usable in Termina. Off: they stay in Hyrule. Whether they "
      "wait for Majora's Mask's first bottle is set below." },
    { kWalletCarry, "gCombo.BackToBack.Wallet", true, OptionGroup::Carry, "Wallet and rupees",
      "Termina starts with the wallet's size and the rupees Link has at the end of Ocarina of Time." },
    { kSongsCarry, "gCombo.BackToBack.Songs", false, OptionGroup::Carry, "Songs",
      "The songs Link learned in Ocarina of Time (Song of Time, Epona's Song, Song of Storms) are learned in Termina "
      "too, once Link has Majora's Mask's Ocarina. Off: he learns each there." },
    { kDinsFire, "gCombo.BackToBack.Items.DinsFire", true, OptionGroup::OotOnlyItems, "Din's Fire", kOotOnlyTooltip },
    { kFaroresWind, "gCombo.BackToBack.Items.FaroresWind", true, OptionGroup::OotOnlyItems, "Farore's Wind",
      kOotOnlyTooltip },
    { kNayrusLove, "gCombo.BackToBack.Items.NayrusLove", true, OptionGroup::OotOnlyItems, "Nayru's Love",
      kOotOnlyTooltip },
    { kBoomerang, "gCombo.BackToBack.Items.Boomerang", true, OptionGroup::OotOnlyItems, "Boomerang", kOotOnlyTooltip },
    { kSlingshot, "gCombo.BackToBack.Items.Slingshot", true, OptionGroup::OotOnlyItems, "Slingshot", kOotOnlyTooltip },
    { kHammer, "gCombo.BackToBack.Items.Hammer", true, OptionGroup::OotOnlyItems, "Megaton Hammer", kOotOnlyTooltip },
    { kBoots, "gCombo.BackToBack.Items.Boots", true, OptionGroup::OotOnlyItems, "Iron Boots and Hover Boots",
      "Wearable in Termina. Off: Link wears Kokiri boots there." },
    { kTunics, "gCombo.BackToBack.Items.Tunics", true, OptionGroup::OotOnlyItems, "Goron Tunic and Zora Tunic",
      "Wearable in Termina. Off: Link wears the Kokiri tunic there." },
    { kEarlySticks, "gCombo.BackToBack.Early.Sticks", false, OptionGroup::EarlyUse, "Deku Sticks", kEarlyUseTooltip },
    { kEarlyNuts, "gCombo.BackToBack.Early.Nuts", false, OptionGroup::EarlyUse, "Deku Nuts", kEarlyUseTooltip },
    { kEarlyBombs, "gCombo.BackToBack.Early.Bombs", false, OptionGroup::EarlyUse, "Bombs", kEarlyUseTooltip },
    { kEarlyBow, "gCombo.BackToBack.Early.Bow", false, OptionGroup::EarlyUse, "Bow", kEarlyUseTooltip },
    { kEarlyFireArrows, "gCombo.BackToBack.Early.FireArrows", false, OptionGroup::EarlyUse, "Fire Arrows",
      kEarlyUseTooltip },
    { kEarlyIceArrows, "gCombo.BackToBack.Early.IceArrows", false, OptionGroup::EarlyUse, "Ice Arrows",
      kEarlyUseTooltip },
    { kEarlyLightArrows, "gCombo.BackToBack.Early.LightArrows", false, OptionGroup::EarlyUse, "Light Arrows",
      kEarlyUseTooltip },
    { kEarlyOcarina, "gCombo.BackToBack.Early.Ocarina", false, OptionGroup::EarlyUse,
      "Fairy Ocarina and Ocarina of Time", kEarlyUseTooltip },
    { kEarlyBombchus, "gCombo.BackToBack.Early.Bombchus", false, OptionGroup::EarlyUse, "Bombchus", kEarlyUseTooltip },
    { kEarlyHookshot, "gCombo.BackToBack.Early.Hookshot", false, OptionGroup::EarlyUse, "Hookshot and Longshot",
      kEarlyUseTooltip },
    { kEarlyLens, "gCombo.BackToBack.Early.Lens", false, OptionGroup::EarlyUse, "Lens of Truth", kEarlyUseTooltip },
    { kEarlyBeans, "gCombo.BackToBack.Early.Beans", false, OptionGroup::EarlyUse, "Magic Beans", kEarlyUseTooltip },
    { kEarlyBottles, "gCombo.BackToBack.Early.Bottles", false, OptionGroup::EarlyUse, "Bottles",
      "Usable in Termina from the start. Off: they wait until Link finds Majora's Mask's first bottle. Needs the "
      "Bottles switch above." },
    { kEarlyKeatonMask, "gCombo.BackToBack.Early.KeatonMask", false, OptionGroup::EarlyUse, "Keaton Mask",
      kEarlyUseTooltip },
    { kEarlyBunnyHood, "gCombo.BackToBack.Early.BunnyHood", false, OptionGroup::EarlyUse, "Bunny Hood",
      kEarlyUseTooltip },
    { kEarlyMaskOfTruth, "gCombo.BackToBack.Early.MaskOfTruth", false, OptionGroup::EarlyUse, "Mask of Truth",
      kEarlyUseTooltip },
    { kEarlyGoronMask, "gCombo.BackToBack.Early.GoronMask", false, OptionGroup::EarlyUse, "Goron Mask",
      kEarlyTransformMaskTooltip },
    { kEarlyZoraMask, "gCombo.BackToBack.Early.ZoraMask", false, OptionGroup::EarlyUse, "Zora Mask",
      kEarlyTransformMaskTooltip },
};
inline constexpr std::size_t kOptionCount = sizeof(kOptions) / sizeof(kOptions[0]);

inline constexpr int DefaultOptions() {
    int options = 0;
    for (const OptionDef& option : kOptions) {
        if (option.defaultOn)
            options |= option.bit;
    }
    return options;
}

// The options a new file takes from the page. getInt(cvarName, fallback) reads one CVar (CVarGetInteger).
template <typename GetInt> inline int OptionsFromCVars(GetInt getInt) {
    int options = 0;
    for (const OptionDef& option : kOptions) {
        if (getInt(option.cvar, option.defaultOn ? 1 : 0) != 0)
            options |= option.bit;
    }
    return options;
}

// A stored options int in the current layout. In the first layout the songs switch also decided the masks: with it on,
// the masks both games have (the Goron and Zora masks transforming at once among them) were usable at once; with it
// off they waited.
inline constexpr int MigrateStoredOptions(int stored, int layout) {
    if (layout >= kOptionsLayout)
        return stored;
    const int masks = (stored & kSongsCarry) != 0 ? kEarlyUseMasks : 0;
    return stored | masks;
}

} // namespace BackToBack

} // namespace ComboRando
