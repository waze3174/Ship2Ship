// ComboShip: what OoT's half of a back-to-back slot hands to Majora's Mask (combo/rando/ComboBackToBackCarry.h).
// Combo-owned file (soh GLOB_RECURSE). OoT's save stays resident while MM runs, so MM reads these when it applies a
// carried hearts, wallet, magic meter or song. See docs/deviations/back2back.md.
#ifdef COMBO_BUILD

#include "ComboExport.h"
#include "rando/ComboBackToBackCarry.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
}

namespace {

namespace Carry = ComboRando::BackToBack;

bool SaveResident() {
    return gSaveContext.fileNum <= 2; // 0xFF = no OoT save loaded
}

int MagicLevel() {
    if (!gSaveContext.isMagicAcquired)
        return 0;
    return gSaveContext.isDoubleMagicAcquired ? 2 : 1;
}

int Songs() {
    int songs = 0;
    if (CHECK_QUEST_ITEM(QUEST_SONG_TIME))
        songs |= Carry::kCarrySongTime;
    if (CHECK_QUEST_ITEM(QUEST_SONG_EPONA))
        songs |= Carry::kCarrySongEpona;
    if (CHECK_QUEST_ITEM(QUEST_SONG_STORMS))
        songs |= Carry::kCarrySongStorms;
    return songs;
}

} // namespace

// One of OoT's numbers for MM (a Carry::CarryField); 0 when no OoT save is resident or the field is unknown.
extern "C" COMBO_EXPORT int SOH_ComboCarry_Get(int field) {
    if (!SaveResident())
        return 0;
    switch (field) {
        case Carry::kCarryHealthCapacity:
            return gSaveContext.healthCapacity;
        case Carry::kCarryHeartPieces:
            return (gSaveContext.inventory.questItems >> Carry::kHeartPiecesQuestShift) & Carry::kHeartPiecesMask;
        case Carry::kCarryWalletTier:
            return CUR_UPG_VALUE(UPG_WALLET);
        case Carry::kCarryRupees:
            return gSaveContext.rupees;
        case Carry::kCarryMagicLevel:
            return MagicLevel();
        case Carry::kCarrySongs:
            return Songs();
        default:
            return 0;
    }
}

#endif // COMBO_BUILD
