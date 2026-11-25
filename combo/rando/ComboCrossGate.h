// combo/rando/ComboCrossGate.h
// ComboShip: which ways between the two games are open, as a bit mask the launcher pushes to both DLLs when a slot is
// loaded and again whenever a final boss is recorded. Header-only, compiled into the exe, soh.dll and 2ship.dll.
// See docs/deviations/back2back.md.
//
// Every slot but a back-to-back one (ComboMode::IsBackToBack) has kAllOpen, which gates nothing. A back-to-back slot
// plays vanilla Ocarina of Time, then vanilla Majora's Mask, so:
//   neither boss beaten        nothing: no crossing at all, and the Lost Woods link is closed (OoT's Lost Woods is
//                              vanilla there: no stump, the north wrong turn loops back to Kokiri Forest)
//   Ganon beaten               Hyrule to Termina through the Lost Woods, for child Link only
//   Majora beaten              Termina to Hyrule too
//   both beaten                every way open, as in a combo slot
// "The link exists" (Combo_LostWoodsLinkOn) stays true throughout, so the Happy Mask Shop door never reverts to the
// portal into Termina; "the link is open" is this mask.
#pragma once

#include "ComboMode.h"

namespace ComboRando::CrossGate {

inline constexpr int kLostWoodsToTermina = 1 << 0; // the Lost Woods path into Termina, for child Link
inline constexpr int kToTermina = 1 << 1;          // any way into Termina, for either age
inline constexpr int kToHyrule = 1 << 2;           // any way into Hyrule
inline constexpr int kBackToBack = 1 << 3;         // the slot is a back-to-back one, so the bits above are in force
inline constexpr int kAllOpen = kLostWoodsToTermina | kToTermina | kToHyrule;

// ootBeaten / mmBeaten: the slot's combo.completion record of each game's final boss.
inline int Mask(const ComboMode& mode, bool ootBeaten, bool mmBeaten) {
    if (!mode.IsBackToBack())
        return kAllOpen;
    int mask = kBackToBack;
    if (ootBeaten)
        mask |= kLostWoodsToTermina;
    if (mmBeaten)
        mask |= kToHyrule;
    if (ootBeaten && mmBeaten)
        mask |= kToTermina;
    return mask;
}

inline bool ToTerminaOpen(int mask) {
    return (mask & kToTermina) != 0;
}

inline bool ToHyruleOpen(int mask) {
    return (mask & kToHyrule) != 0;
}

inline bool LostWoodsToTerminaOpen(int mask, bool linkIsChild) {
    return ToTerminaOpen(mask) || (linkIsChild && (mask & kLostWoodsToTermina) != 0);
}

// OoT's credits have ended in a back-to-back slot where Ganon is beaten and Majora is not: the slot goes on in the
// Temple of Time instead of returning to the title.
inline bool OotCreditsContinueInTemple(int mask) {
    return (mask & kBackToBack) != 0 && (mask & kLostWoodsToTermina) != 0 && !ToTerminaOpen(mask);
}

// MM's credits have ended in a back-to-back slot: the slot returns to Ocarina of Time's title (MM's own title and file
// select are unreachable inside the launcher).
inline bool MmCreditsReturnToOot(int mask) {
    return (mask & kBackToBack) != 0;
}

} // namespace ComboRando::CrossGate
