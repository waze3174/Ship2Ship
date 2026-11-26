// mm/2s2h/Rando/MiscBehavior/ComboBackToBack.cpp — ComboShip: what Ocarina of Time carries into a back-to-back slot's
// Majora's Mask save. Combo-owned file (2ship GLOB_RECURSE). See docs/deviations/back2back.md.
//
//   At the opening   The hearts, the wallet with its rupees and, with the Magic early-use switch, the magic meter go
//   into
//                    MM's save as the opening starts (title_setup.c),
//                    whenever the slot has not yet played the opening, so what MM saves from then on holds them.
//   Once found       The rest waits for the MM item it belongs to, because MM's own give code reads some of these as
//                    ownership. The magic meter carries once MM's Great Fairy has given magic, the songs once Link has
//                    an ocarina to play in Termina (Combo_MM_LinkHasUsableOcarina), and the quiver, bomb bag, Deku
//                    stick and Deku nut levels once Link has the bow, the bombs, the sticks or the nuts. The tick
//                    below applies each the frame it can.
// Ocarina of Time's numbers come from its resident save (SOH_ComboCarry_Get).
#ifdef COMBO_BUILD

#include <algorithm>

#include <spdlog/spdlog.h>

#include "ComboResolve.h"
#include "ComboPause.h"
#include "rando/ComboBackToBackCarry.h"
#include "rando/ComboBackToBackLocks.h"
#include "rando/ComboBackToBackSettings.h"
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/ShipInit.hpp"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
int Combo_MM_BackToBackOpeningPending(void);                         // BenPort.cpp
int Combo_MM_BackToBackLocksActive(void);                            // BenPort.cpp
ComboRando::BackToBack::OptionBits Combo_MM_BackToBackOptions(void); // BenPort.cpp
extern int gMMComboCrossGate;            // BenPort.cpp: the loaded slot's ways between the games
int Combo_MM_LinkHasUsableOcarina(void); // below
int Combo_MM_FirstCycleOver(void);       // below
}

namespace {

namespace B2B = ComboRando::BackToBack;

constexpr int kMaxHealthCapacity = 0x140; // 20 hearts, as MM's own cap
constexpr int kHealthPerHeart = 0x10;

using FnCarryGet = int (*)(int);
using FnVoid = void (*)();

int CarriedFromOot(B2B::CarryField field) {
    static FnCarryGet sGet = reinterpret_cast<FnCarryGet>(Combo_ResolveSym("soh", "SOH_ComboCarry_Get"));
    return sGet != nullptr ? sGet(field) : 0;
}

bool OptionOn(B2B::Option option) {
    return (Combo_MM_BackToBackOptions() & option) != 0;
}

void CarryHearts() {
    const int capacity = std::clamp(CarriedFromOot(B2B::kCarryHealthCapacity), 0, kMaxHealthCapacity);
    if (capacity == 0)
        return; // no OoT save is resident: MM keeps its three hearts
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

// With the Magic early-use switch, Link starts Termina with Ocarina of Time's meter, full, as MM's Great Fairy would
// have given it (its gift skips what he already has, so her story is unchanged): single, or double when the double
// magic meter carry is on and OoT's was. None without a meter in OoT. Interface_Update grows the meter once the opening
// runs.
void CarryEarlyMagic() {
    const int level = CarriedFromOot(B2B::kCarryMagicLevel);
    if (level == 0)
        return;
    SavePlayerData& player = gSaveContext.save.saveInfo.playerData;
    const bool doubleMeter = level >= 2 && OptionOn(B2B::kMagicCarry);
    player.isMagicAcquired = true;
    player.isDoubleMagicAcquired = doubleMeter;
    player.magic = doubleMeter ? MAGIC_DOUBLE_METER : MAGIC_NORMAL_METER;
    player.magicLevel = 0;
    SPDLOG_INFO("[ComboBackToBack] magic meter in Termina from the start: {}", doubleMeter ? "double" : "single");
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

// An upgrade level carried over, once MM has the item it enlarges. MM's own give code treats a nonzero level as "the
// item is already here" (Item_Give: the first bow only adds an arrow when the quiver level is not 0), so a level
// written before Link finds the item would leave MM's bow, bombs, sticks or nuts ungiven. MM's own upgrades set the
// level outright, so a lower one is raised again here.
struct CapacityCarry {
    B2B::CarryField field;
    int upgrade;
    int baseItem;
};

constexpr CapacityCarry kCapacityCarries[] = {
    { B2B::kCarryQuiverTier, UPG_QUIVER, ITEM_BOW },
    { B2B::kCarryBombBagTier, UPG_BOMB_BAG, ITEM_BOMB },
    { B2B::kCarryStickTier, UPG_DEKU_STICKS, ITEM_DEKU_STICK },
    { B2B::kCarryNutTier, UPG_DEKU_NUTS, ITEM_DEKU_NUT },
};

void CarryCapacities() {
    for (const CapacityCarry& carry : kCapacityCarries) {
        if (INV_CONTENT(carry.baseItem) != carry.baseItem)
            continue;
        const int carried = CarriedFromOot(carry.field);
        if (carried > CUR_UPG_VALUE(carry.upgrade)) {
            Inventory_ChangeUpgrade(carry.upgrade, carried);
            SPDLOG_INFO("[ComboBackToBack] upgrade {} carried into Termina: level {}", carry.upgrade, carried);
        }
    }
}

// The songs OoT has learned, once Link has an ocarina to play them on in Termina: MM's, or OoT's when the slot lets it
// in early. MM's own lessons and the Song of Time's flashback set the same bits again, which changes nothing.
void CarrySongs() {
    if (!OptionOn(B2B::kSongsCarry) || !Combo_MM_LinkHasUsableOcarina())
        return;
    const int songs = CarriedFromOot(B2B::kCarrySongs);
    if ((songs & B2B::kCarrySongTime) && !CHECK_QUEST_ITEM(QUEST_SONG_TIME))
        SET_QUEST_ITEM(QUEST_SONG_TIME);
    if ((songs & B2B::kCarrySongEpona) && !CHECK_QUEST_ITEM(QUEST_SONG_EPONA))
        SET_QUEST_ITEM(QUEST_SONG_EPONA);
    if ((songs & B2B::kCarrySongStorms) && !CHECK_QUEST_ITEM(QUEST_SONG_STORMS))
        SET_QUEST_ITEM(QUEST_SONG_STORMS);
}

// The Song of Time's cycle reset (Sram_SaveEndOfCycle) empties MM's ammo and bottles; with the setting on it empties
// the consumables Ocarina of Time carried into Termina by the same rule.
void ResetOotConsumablesAtEndOfCycle() {
    if (!ComboRando::CrossGate::IsBackToBack(gMMComboCrossGate) || !OptionOn(B2B::kResetOotConsumables))
        return;
    static FnVoid sReset = reinterpret_cast<FnVoid>(Combo_ResolveSym("soh", "SOH_ComboTermina_ResetConsumables"));
    if (sReset == nullptr)
        return;
    sReset();
    SPDLOG_INFO("[ComboBackToBack] the cycle reset emptied Ocarina of Time's carried consumables");
}

void TickCarryOnceFound() {
    if (gPlayState == nullptr || !Combo_MM_BackToBackLocksActive())
        return;
    CarryMagic();
    CarrySongs();
    CarryCapacities();
}

// What MM keeps for its first cycle (the ocarina is still Skull Kid's) stops once the cycle is over: time speeds back
// up to normal, Termina Field is no longer empty, the Clock Tower opens on the last night, and the Scrub in the field
// is there. The Skull Kid on the Clock Tower roof is not among them: he still holds MM's ocarina until Link takes it.
void EndFirstCycleBehavior(bool* should) {
    if (Combo_MM_FirstCycleOver())
        *should = false;
}

void RegisterFirstCycleEnd() {
    COND_VB_SHOULD(VB_FASTER_FIRST_CYCLE, true, { EndFirstCycleBehavior(should); });
    COND_VB_SHOULD(VB_TERMINA_FIELD_BE_EMPTY, true, { EndFirstCycleBehavior(should); });
    COND_VB_SHOULD(VB_SCOPENUTS_CONSIDER_FIRST_CYCLE, true, { EndFirstCycleBehavior(should); });
    COND_VB_SHOULD(VB_CLOCK_TOWER_OPENING_CONSIDER_THIS_FIRST_CYCLE, true, { EndFirstCycleBehavior(should); });
}

void RegisterComboBackToBack() {
    RegisterFirstCycleEnd();
    COND_HOOK(OnGameStateUpdate, true, TickCarryOnceFound);
    COND_HOOK(BeforeEndOfCycleSave, true, ResetOotConsumablesAtEndOfCycle);
}

RegisterShipInitFunc sBackToBackCarryInit(RegisterComboBackToBack, {});

} // namespace

// Link has an ocarina he can play in Termina: MM's own, or in a back-to-back slot Ocarina of Time's, once the slot lets
// it into Termina (its early-use switch, or MM's own found; Combo_MmPauseOotHolds applies the locks). MM's story code
// that asks for this (the Happy Mask Salesman's Song of Healing, the songs carried over) reads it instead of MM's
// ocarina slot; the code that means "the Skull Kid still has MM's ocarina" keeps reading the slot. Every other slot:
// MM's own only, as before.
extern "C" int Combo_MM_LinkHasUsableOcarina(void) {
    if (INV_CONTENT(ITEM_OCARINA_OF_TIME) == ITEM_OCARINA_OF_TIME)
        return 1;
    namespace Oot = ComboRando::BackToBack::OotItem;
    return ComboRando::CrossGate::IsBackToBack(gMMComboCrossGate) &&
                   (Combo_MmPauseOotHolds(Oot::kOotOcarinaFairy) || Combo_MmPauseOotHolds(Oot::kOotOcarinaTime))
               ? 1
               : 0;
}

// Majora's Mask's first cycle is over when its ocarina is back in the inventory, as in the game. A back-to-back slot's
// Link can also end it with Ocarina of Time's ocarina (its early switch): the Song of Time that resets the cycle then
// leaves MM's ocarina with Skull Kid, and the reset itself (Sram_SaveEndOfCycle counts it in threeDayResetCount, which
// no other way into a first-cycle reset leaves nonzero) is the story's record that the cycle is over.
extern "C" int Combo_MM_FirstCycleOver(void) {
    if (INV_CONTENT(ITEM_OCARINA_OF_TIME) == ITEM_OCARINA_OF_TIME)
        return 1;
    return ComboRando::CrossGate::IsBackToBack(gMMComboCrossGate) &&
                   gSaveContext.save.saveInfo.playerData.threeDayResetCount > 0
               ? 1
               : 0;
}

// Called as MM's opening starts in a back-to-back slot's first entry (title_setup.c): the carried hearts and wallet,
// and the magic meter when it is usable from the start.
extern "C" void Combo_MM_ApplyBackToBackStart(void) {
    if (!Combo_MM_BackToBackOpeningPending())
        return;
    if (OptionOn(B2B::kHeartsCarry))
        CarryHearts();
    if (OptionOn(B2B::kWalletCarry))
        CarryWallet();
    if (OptionOn(B2B::kEarlyMagic))
        CarryEarlyMagic();
}

#endif // COMBO_BUILD
