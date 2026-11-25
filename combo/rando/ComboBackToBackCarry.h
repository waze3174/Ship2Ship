// combo/rando/ComboBackToBackCarry.h
// ComboShip: what a back-to-back slot carries from Ocarina of Time into Majora's Mask. Ocarina of Time's save stays
// resident while MM runs, so MM asks it for these numbers when it applies them (SOH_ComboCarry_Get, soh.dll). Header-
// only and std only, shared by soh.dll and 2ship.dll. See docs/deviations/back2back.md.
#pragma once

namespace ComboRando::BackToBack {

enum CarryField : int {
    kCarryHealthCapacity, // OoT's max health, in hearts * 0x10
    kCarryHeartPieces,    // the heart pieces toward the next heart, 0-3
    kCarryWalletTier,     // OoT's wallet: 0 child, 1 adult, 2 giant, 3 tycoon
    kCarryRupees,
    kCarryMagicLevel, // 0 none, 1 magic, 2 double magic
    kCarrySongs,      // the CarrySong bits OoT has learned
};

enum CarrySong : int {
    kCarrySongTime = 1 << 0,
    kCarrySongEpona = 1 << 1,
    kCarrySongStorms = 1 << 2,
};

inline constexpr int kHeartPiecesQuestShift = 28; // both games keep the heart pieces in questItems bits 28-31
inline constexpr int kHeartPiecesMask = 0xF;
inline constexpr int kMmMaxWalletTier = 2; // MM's wallet stops at 500 rupees

} // namespace ComboRando::BackToBack
