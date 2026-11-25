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
// ("options") when it is created. The rest of the mode's rules (what is locked until Majora's Mask has its own copy)
// are not choices and are in ComboBackToBackLocks.h.
enum Option : int {
    kHeartsCarry = 1 << 0,      // Termina starts with Ocarina of Time's hearts instead of three
    kMagicCarry = 1 << 1,       // OoT's magic meter carries, once Majora's Mask's Great Fairy gives magic
    kBottlesCarry = 1 << 2,     // OoT's bottles are usable in Termina, once MM's first bottle is found
    kWalletCarry = 1 << 3,      // the wallet's size and the rupees carry
    kSongsMasksShared = 1 << 4, // songs and masks MM has too are usable in Termina without finding MM's own
    kDinsFire = 1 << 5,         // the OoT-only items, one switch each
    kFaroresWind = 1 << 6,
    kNayrusLove = 1 << 7,
    kBoomerang = 1 << 8,
    kSlingshot = 1 << 9,
    kHammer = 1 << 10,
    kBoots = 1 << 11,
    kTunics = 1 << 12,
};

enum class OptionGroup { Carry, OotOnlyItems };

struct OptionDef {
    int bit;
    const char* cvar;
    bool defaultOn;
    OptionGroup group;
    const char* label;
    const char* tooltip;
};

inline constexpr OptionDef kOptions[] = {
    { kHeartsCarry, "gCombo.BackToBack.Hearts", false, OptionGroup::Carry, "Hearts",
      "Termina starts with the hearts Link has at the end of Ocarina of Time. Off: Termina starts with three." },
    { kMagicCarry, "gCombo.BackToBack.Magic", true, OptionGroup::Carry, "Magic meter",
      "The magic meter carries over, once Majora's Mask's own Great Fairy gives Link magic. Off: Majora's Mask's "
      "magic is its own." },
    { kBottlesCarry, "gCombo.BackToBack.Bottles", true, OptionGroup::Carry, "Bottles",
      "Ocarina of Time's bottles and what is in them are usable in Termina, once Link has found Majora's Mask's "
      "first bottle. Off: they stay in Hyrule." },
    { kWalletCarry, "gCombo.BackToBack.Wallet", true, OptionGroup::Carry, "Wallet and rupees",
      "Termina starts with the wallet's size and the rupees Link has at the end of Ocarina of Time." },
    { kSongsMasksShared, "gCombo.BackToBack.SongsMasks", false, OptionGroup::Carry, "Songs and masks",
      "The songs and masks both games have are usable in Termina from the start. Off: each stays locked until Link "
      "finds Majora's Mask's own." },
    { kDinsFire, "gCombo.BackToBack.Items.DinsFire", false, OptionGroup::OotOnlyItems, "Din's Fire",
      "Usable in Termina. Off: it stays in Hyrule." },
    { kFaroresWind, "gCombo.BackToBack.Items.FaroresWind", false, OptionGroup::OotOnlyItems, "Farore's Wind",
      "Usable in Termina. Off: it stays in Hyrule." },
    { kNayrusLove, "gCombo.BackToBack.Items.NayrusLove", false, OptionGroup::OotOnlyItems, "Nayru's Love",
      "Usable in Termina. Off: it stays in Hyrule." },
    { kBoomerang, "gCombo.BackToBack.Items.Boomerang", false, OptionGroup::OotOnlyItems, "Boomerang",
      "Usable in Termina. Off: it stays in Hyrule." },
    { kSlingshot, "gCombo.BackToBack.Items.Slingshot", false, OptionGroup::OotOnlyItems, "Slingshot",
      "Usable in Termina. Off: it stays in Hyrule." },
    { kHammer, "gCombo.BackToBack.Items.Hammer", false, OptionGroup::OotOnlyItems, "Megaton Hammer",
      "Usable in Termina. Off: it stays in Hyrule." },
    { kBoots, "gCombo.BackToBack.Items.Boots", false, OptionGroup::OotOnlyItems, "Iron Boots and Hover Boots",
      "Wearable in Termina. Off: Link wears Kokiri boots there." },
    { kTunics, "gCombo.BackToBack.Items.Tunics", false, OptionGroup::OotOnlyItems, "Goron Tunic and Zora Tunic",
      "Wearable in Termina. Off: Link wears the Kokiri tunic there." },
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

} // namespace BackToBack

} // namespace ComboRando
