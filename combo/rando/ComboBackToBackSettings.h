// combo/rando/ComboBackToBackSettings.h
// ComboShip: the CVars of the back-to-back mode's settings page (Both games > Back to back). Each one is read once,
// when a back-to-back file is created, and baked into that file; changing it later never touches an existing file.
// The page's This file view edits the loaded file's own item switches (EditableOptionsMask) instead.
// Header-only and std only, so comboui, soh.dll, 2ship.dll and the launcher share it. See docs/deviations/back2back.md.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ComboRando {

// 1: a new back-to-back file plays Ocarina of Time's Master Quest, when oot-mq.o2r is loaded. 0 or unset: Normal Quest.
inline constexpr const char* kBackToBackMasterQuestCVar = "gCombo.BackToBack.MasterQuest";

// 1 or unset: the file select offers the back-to-back entry for new files. 0: it is hidden. Slots that already exist
// load and show their badge either way; only the new-file choice is hidden.
inline constexpr const char* kBackToBackShowCVar = "gCombo.BackToBack.Show";
inline constexpr int kBackToBackShowDefault = 1;

namespace BackToBack {

// The page's choices about what goes from Hyrule into Termina, as the bits of one 64-bit value stored in the file's
// combo.mode ("options") when it is created. Every item that has a copy in Majora's Mask has a switch for whether it
// waits for that copy: a clear bit waits (the default), a set bit lets the item be used in Termina before Link finds
// Majora's Mask's own. The rest of the mode's rules are not choices and are in ComboBackToBackLocks.h.
using OptionBits = std::uint64_t;

enum Option : OptionBits {
    kHeartsCarry = 1ULL << 0,  // Termina starts with Ocarina of Time's hearts instead of three
    kMagicCarry = 1ULL << 1,   // OoT's double magic meter carries, once Majora's Mask's Great Fairy gives magic
    kBottlesCarry = 1ULL << 2, // OoT's bottles are usable in Termina
    kWalletCarry = 1ULL << 3,  // the wallet's size and the rupees carry
    kSongsCarry = 1ULL << 4,   // the songs OoT has learned are learned in Termina, once Link has an ocarina there
    kDinsFire = 1ULL << 5,     // the OoT-only items, one switch each
    kFaroresWind = 1ULL << 6,
    kNayrusLove = 1ULL << 7,
    kBoomerang = 1ULL << 8,
    kSlingshot = 1ULL << 9,
    kHammer = 1ULL << 10,
    kBoots = 1ULL << 11,
    kTunics = 1ULL << 12,
    kEarlySticks = 1ULL << 13, // usable before Majora's Mask's own copy is found, one switch each
    kEarlyNuts = 1ULL << 14,
    kEarlyBombs = 1ULL << 15,
    kEarlyBow = 1ULL << 16,
    kEarlyFireArrows = 1ULL << 17,
    kEarlyIceArrows = 1ULL << 18,
    kEarlyLightArrows = 1ULL << 19,
    kEarlyOcarina = 1ULL << 20, // both of OoT's ocarinas
    kEarlyBombchus = 1ULL << 21,
    kEarlyHookshot = 1ULL << 22, // the Hookshot and the Longshot
    kEarlyLens = 1ULL << 23,
    kEarlyBeans = 1ULL << 24,
    kEarlyBottles = 1ULL << 25, // with kBottlesCarry: bottles before Majora's Mask's first bottle
    kEarlyKeatonMask = 1ULL << 26,
    kEarlyBunnyHood = 1ULL << 27,
    kEarlyMaskOfTruth = 1ULL << 28,
    kEarlyGoronMask = 1ULL << 29, // OoT's Goron Mask transforms Link in Termina from the start, not worn plain first
    kEarlyZoraMask = 1ULL << 30,  // the same for the Zora Mask
    kEarlyMagic = 1ULL << 31, // OoT's magic meter is in Termina from the start, not only once MM's Great Fairy gives it
    kResetOotConsumables = 1ULL << 32, // MM's cycle reset also empties the consumables OoT carried into Termina
};

// The layout of the options value, stored beside it in the slot's combo.mode (kOptionsLayoutKey). A mode without the
// key holds the first layout (bits 0 to 12), where bit 4 also meant "the masks both games have are usable at once";
// MigrateStoredOptions turns that into the mask bits. A mode without the key but with kMarkedLayoutBit set was made by
// 0.2.2, which marked its layout with that bit instead of the key: bits 0 to 28 as now, no Goron or Zora switch (both
// masks were worn plain until Majora's Mask's own), and bit 30 only the marker. Layout 2 has the Goron and Zora bits
// (29 and 30) in a 31-bit int; layout 3 is the same bits in a 64-bit value, with the Magic switch (31) added.
inline constexpr int kOptionsLayout = 3;
inline constexpr int kFirstOptionsLayout = 1;
inline constexpr int kFirstKeyedOptionsLayout = 2;
inline constexpr OptionBits kMarkedLayoutBit = 1ULL << 30;

inline constexpr OptionBits kEarlyUseMasks =
    kEarlyKeatonMask | kEarlyBunnyHood | kEarlyMaskOfTruth | kEarlyGoronMask | kEarlyZoraMask;

enum class OptionGroup { Carry, OotOnlyItems, EarlyUse, CycleReset };

struct OptionDef {
    OptionBits bit;
    const char* cvar;
    bool defaultOn;
    OptionGroup group;
    const char* label;
    const char* tooltip;
};

inline constexpr const char* kOotOnlyTooltip = "Usable in Termina. Off: it stays in Hyrule.";
inline constexpr const char* kEarlyUseTooltip =
    "Usable in Termina from the start. Off: it waits until Link finds Majora's Mask's own.";
inline constexpr const char* kResetOotConsumablesTooltip =
    "When the Song of Time takes Link back to the first day, Majora's Mask empties its own ammo and bottles. With this "
    "on it empties the ones Ocarina of Time carried into Termina the same way: arrows, seeds, bombs, bombchus, sticks, "
    "nuts and beans to zero, and the contents of its bottles. Off: it leaves them as they are.";
inline constexpr const char* kEarlyMagicTooltip =
    "Link has Ocarina of Time's magic meter in Termina from the start, full: single, or double if the double magic "
    "meter switch above is on and he had it. Off: he has none until Majora's Mask's own Great Fairy gives him magic.";
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
      "too, once Link has an ocarina he can play there: Majora's Mask's, or Ocarina of Time's when the ocarina switch "
      "below lets it in from the start. Off: he learns each there." },
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
      "Fairy Ocarina and Ocarina of Time",
      "Usable in Termina from the start: Link can play it before he gets Majora's Mask's back from the Skull Kid, and "
      "the Happy Mask Salesman teaches him the Song of Healing. With the Songs switch on, the Song of Time can then "
      "end the first cycle early. Off: it waits until Link finds Majora's Mask's own." },
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
    { kEarlyMagic, "gCombo.BackToBack.Early.Magic", false, OptionGroup::EarlyUse, "Magic", kEarlyMagicTooltip },
    { kResetOotConsumables, "gCombo.BackToBack.ResetOotConsumables", false, OptionGroup::CycleReset,
      "Song of Time also empties Ocarina of Time's ammo and bottles", kResetOotConsumablesTooltip },
};
inline constexpr std::size_t kOptionCount = sizeof(kOptions) / sizeof(kOptions[0]);

inline constexpr OptionBits DefaultOptions() {
    OptionBits options = 0;
    for (const OptionDef& option : kOptions) {
        if (option.defaultOn)
            options |= option.bit;
    }
    return options;
}

// The choices a loaded file lets the player change: the live rules, which are every item switch (the OoT-only items and
// the early-use ones). The carries are applied once, as the file's first entry into Termina, and stay as the file was
// made; so does Magic, which is a carry made at that moment.
inline constexpr bool EditableOnLoadedFile(const OptionDef& option) {
    return option.group != OptionGroup::Carry && option.bit != kEarlyMagic;
}

inline constexpr OptionBits EditableOptionsMask() {
    OptionBits mask = 0;
    for (const OptionDef& option : kOptions) {
        if (EditableOnLoadedFile(option))
            mask |= option.bit;
    }
    return mask;
}

// A loaded file's options after the player asked for `requested`: the editable choices follow the request, every other
// bit stays as the file has it.
inline constexpr OptionBits ApplyEditedOptions(OptionBits current, OptionBits requested) {
    return (current & ~EditableOptionsMask()) | (requested & EditableOptionsMask());
}

// The Back to back page's quick-set buttons for new files.
enum class QuickSet {
    Purist,    // every carry off and every item switch off: Ocarina of Time stays in Hyrule, Majora's Mask is its own
    Default,   // the page's defaults
    Everything // every choice on
};

inline constexpr OptionBits QuickSetOptions(QuickSet set) {
    OptionBits options = 0;
    for (const OptionDef& option : kOptions) {
        const bool on = set == QuickSet::Everything || (set == QuickSet::Default && option.defaultOn);
        if (on)
            options |= option.bit;
    }
    return options;
}

// The options a new file takes from the page. getInt(cvarName, fallback) reads one CVar (CVarGetInteger).
template <typename GetInt> inline OptionBits OptionsFromCVars(GetInt getInt) {
    OptionBits options = 0;
    for (const OptionDef& option : kOptions) {
        if (getInt(option.cvar, option.defaultOn ? 1 : 0) != 0)
            options |= option.bit;
    }
    return options;
}

// A stored options value in the current layout. Layouts 2 and 3 hold the same bits (layout 3 only adds Magic, which a
// layout-2 file never set, so it waits as before). In the first layout the songs switch also decided the masks: with it
// on, the masks both games have (the Goron and Zora masks transforming at once among them) were usable at once; with
// it off they waited.
inline constexpr OptionBits MigrateStoredOptions(OptionBits stored, int layout) {
    if (layout >= kFirstKeyedOptionsLayout)
        return stored;
    if ((stored & kMarkedLayoutBit) != 0)
        return stored & ~kMarkedLayoutBit;
    const OptionBits masks = (stored & kSongsCarry) != 0 ? kEarlyUseMasks : 0;
    return stored | masks;
}

} // namespace BackToBack

} // namespace ComboRando
