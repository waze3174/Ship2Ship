// mm/2s2h/Rando/MiscBehavior/ComboBackToBack.cpp — ComboShip: what Ocarina of Time carries into a back-to-back slot's
// Majora's Mask save. Combo-owned file (2ship GLOB_RECURSE). See docs/deviations/back2back.md.
//
//   At the opening   The hearts and the wallet with its rupees go into MM's save as the opening starts (title_setup.c),
//                    whenever the slot has not yet played the opening, so what MM saves from then on holds them.
//   Once found       The rest waits for the MM item it belongs to, because MM's own give code reads some of these as
//                    ownership. The magic meter carries once MM's Great Fairy has given magic, and the songs once Link
//                    has MM's Ocarina. The tick below applies each the frame it can.
// Ocarina of Time's numbers come from its resident save (SOH_ComboCarry_Get).
#ifdef COMBO_BUILD

#include <algorithm>

#include <spdlog/spdlog.h>

#include "ComboResolve.h"
#include "rando/ComboBackToBackCarry.h"
#include "rando/ComboBackToBackSettings.h"
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/ShipInit.hpp"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
int Combo_MM_BackToBackOpeningPending(void); // BenPort.cpp
int Combo_MM_BackToBackLocksActive(void);    // BenPort.cpp
int Combo_MM_BackToBackOptions(void);        // BenPort.cpp
}

namespace {

namespace B2B = ComboRando::BackToBack;

constexpr int kMaxHealthCapacity = 0x140; // 20 hearts, as MM's own cap
constexpr int kHealthPerHeart = 0x10;

using FnCarryGet = int (*)(int);

int CarriedFromOot(B2B::CarryField field) {
    static FnCarryGet sGet = reinterpret_cast<FnCarryGet>(Combo_ResolveSym("soh", "SOH_ComboCarry_Get"));
    return sGet != nullptr ? sGet(field) : 0;
}

bool OptionOn(B2B::Option option) {
    return (Combo_MM_BackToBackOptions() & option) != 0;
}

void CarryHearts() {
    const int capacity = std::clamp(CarriedFromOot(B2B::kCarryHealthCapacity), 0, kMaxHealthCapacity);
    if (capacity <= gSaveContext.save.saveInfo.playerData.healthCapacity)
        return; // nothing carried: MM keeps its three hearts
    gSaveContext.save.saveInfo.playerData.healthCapacity = static_cast<s16>(capacity);
    gSaveContext.save.saveInfo.playerData.health = static_cast<s16>(capacity);
    const uint32_t pieces = static_cast<uint32_t>(CarriedFromOot(B2B::kCarryHeartPieces)) & B2B::kHeartPiecesMask;
    gSaveContext.save.saveInfo.inventory.questItems =
        (GET_SAVE_INVENTORY_QUEST_ITEMS &
         ~(static_cast<uint32_t>(B2B::kHeartPiecesMask) << B2B::kHeartPiecesQuestShift)) |
        (pieces << B2B::kHeartPiecesQuestShift);
    SPDLOG_INFO("[ComboBackToBack] hearts carried into Termina: {} hearts", capacity / kHealthPerHeart);
}

void CarryWallet() {
    const int tier = std::min(CarriedFromOot(B2B::kCarryWalletTier), B2B::kMmMaxWalletTier);
    Inventory_ChangeUpgrade(UPG_WALLET, tier);
    gSaveContext.save.saveInfo.playerData.rupees =
        static_cast<s16>(std::min<int>(CarriedFromOot(B2B::kCarryRupees), CAPACITY(UPG_WALLET, tier)));
    SPDLOG_INFO("[ComboBackToBack] wallet carried into Termina: tier {}, {} rupees", tier,
                gSaveContext.save.saveInfo.playerData.rupees);
}

// MM's magic meter, once its own Great Fairy has given it, grows to double when OoT's was double. Waits for MM's own
// grant to finish growing the meter, as a second grant in the same frames would restart it.
void CarryMagic() {
    SavePlayerData& player = gSaveContext.save.saveInfo.playerData;
    if (!OptionOn(B2B::kMagicCarry) || !player.isMagicAcquired || player.isDoubleMagicAcquired ||
        gSaveContext.magicState != MAGIC_STATE_IDLE || player.magicLevel == 0 ||
        CarriedFromOot(B2B::kCarryMagicLevel) < 2) {
        return;
    }
    player.isDoubleMagicAcquired = true;
    player.magic = MAGIC_DOUBLE_METER;
    player.magicLevel = 0; // Interface_Update grows the meter to its new size
    SPDLOG_INFO("[ComboBackToBack] double magic carried into Termina");
}

// The songs OoT has learned, once Link has MM's Ocarina to play them on.
void CarrySongs() {
    if (!OptionOn(B2B::kSongsMasksShared) || INV_CONTENT(ITEM_OCARINA_OF_TIME) != ITEM_OCARINA_OF_TIME)
        return;
    const int songs = CarriedFromOot(B2B::kCarrySongs);
    if ((songs & B2B::kCarrySongTime) && !CHECK_QUEST_ITEM(QUEST_SONG_TIME))
        SET_QUEST_ITEM(QUEST_SONG_TIME);
    if ((songs & B2B::kCarrySongEpona) && !CHECK_QUEST_ITEM(QUEST_SONG_EPONA))
        SET_QUEST_ITEM(QUEST_SONG_EPONA);
    if ((songs & B2B::kCarrySongStorms) && !CHECK_QUEST_ITEM(QUEST_SONG_STORMS))
        SET_QUEST_ITEM(QUEST_SONG_STORMS);
}

void TickCarryOnceFound() {
    if (gPlayState == nullptr || !Combo_MM_BackToBackLocksActive())
        return;
    CarryMagic();
    CarrySongs();
}

void RegisterComboBackToBack() {
    COND_HOOK(OnGameStateUpdate, true, TickCarryOnceFound);
}

RegisterShipInitFunc sBackToBackCarryInit(RegisterComboBackToBack, {});

} // namespace

// Called as MM's opening starts in a back-to-back slot's first entry (title_setup.c): the carried hearts and wallet.
extern "C" void Combo_MM_ApplyBackToBackStart(void) {
    if (!Combo_MM_BackToBackOpeningPending())
        return;
    if (OptionOn(B2B::kHeartsCarry))
        CarryHearts();
    if (OptionOn(B2B::kWalletCarry))
        CarryWallet();
}

#endif // COMBO_BUILD
