// soh/soh/Enhancements/randomizer/ComboReleaseChecks.cpp — ComboShip: OOT's side of "Release checks on boss kill".
// Combo-owned file (soh GLOB_RECURSE). The launcher (Combo_ReleaseGameChecks) calls SOH_ReleaseRemainingChecks once
// Ganon is beaten; this marks every uncollected shuffled OOT check collected and hands its item over with no get-item
// cutscene and no textbox. Deviation record: docs/deviations/rando.md "Release checks on boss kill".
#ifdef COMBO_BUILD

#include <string>
#include <vector>

#include <libultraship/libultraship.h>

#include "ComboExport.h"
#include "ComboRelease.h"
#include "soh/OTRGlobals.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/randomizer/hook_handlers.h"
#include "soh/Enhancements/randomizer/randomizer.h"
#include "soh/Enhancements/randomizer/randomizer_check_tracker.h"
#include "soh/Enhancements/randomizer/static_data.h"
#include "soh/Network/Anchor/Anchor.h"
#include "soh/Notification/Notification.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
}

// OTRGlobals.cpp: the save-direct give of a resolved OOT item (what a cross-game grant and Anchor use) and the save it
// owes; inside SOH_ComboSaveBatchBegin/End the save is made once, when the batch closes.
void Combo_GiveResolvedOOT(const GetItemEntry& gie);
void Combo_PersistOOT();
extern "C" void SOH_ComboSaveBatchBegin(void);
extern "C" void SOH_ComboSaveBatchEnd(void);
// OTRGlobals.cpp: routes a foreign item to the other game's resident save; Anchor.cpp shares it with teammates.
extern "C" void (*gComboCrossDeliver)(int targetGame, const char* itemName, const char* srcCheckName);
extern "C" void Anchor_BroadcastCrossItem(int targetGame, const char* itemName, const char* srcCheckName);
// Shared Items: a poke that has the launcher reconcile the two games' tiers on its next tick.
extern "C" void (*gComboSharedChanged)(int game, int fileNum);

namespace {

constexpr int kOotGameId = 0; // the GameId convention: 0 = OOT, 1 = MM
// Collectible flags below this are saved with the scene; the rest (tempCollect) last one visit, as in the game's own
// Flags_SetCollectible.
constexpr uint16_t kSavedCollectFlagCount = 0x20;
constexpr int kGsFlagSceneShift = 2; // four scenes' Skulltula flags share a byte (GET_GS_FLAGS)

struct SaveBatch {
    SaveBatch() {
        SOH_ComboSaveBatchBegin();
    }
    ~SaveBatch() {
        SOH_ComboSaveBatchEnd();
    }
    SaveBatch(const SaveBatch&) = delete;
    SaveBatch& operator=(const SaveBatch&) = delete;
};

// A check still owed to the player: shuffled and shown by the tracker, with an item placed, not yet collected. The one
// the item queue is giving this very frame is left to it, rather than given twice.
bool IsRemaining(RandomizerCheck rc) {
    if (rc == RC_UNKNOWN_CHECK || rc == RC_MAX || !CheckTracker::IsCheckShuffled(rc)) {
        return false;
    }
    Rando::ItemLocation* location = Rando::Context::GetInstance()->GetItemLocation(rc);
    return !location->IsExcluded() && location->GetPlacedRandomizerGet() != RG_NONE && !location->HasObtained() &&
           rc != OOT_GetQueuedDrawCheck();
}

bool IsCurrentScene(uint8_t scene) {
    return gPlayState != nullptr && gPlayState->sceneNum == scene;
}

// The scene flags are saved per scene, and the scene being played keeps its live copy in the play state, which the game
// writes back on leaving: so the live flag is set too, through the game's own setter.
void SpendChest(uint8_t scene, uint16_t flag) {
    gSaveContext.sceneFlags[scene].chest |= 1u << flag;
    if (IsCurrentScene(scene)) {
        Flags_SetTreasure(gPlayState, flag);
    }
}

void SpendCollectible(uint8_t scene, uint16_t flag) {
    if (flag == 0) {
        return; // the game ignores collectible flag 0
    }
    if (flag < kSavedCollectFlagCount) {
        gSaveContext.sceneFlags[scene].collect |= 1u << flag;
    }
    if (IsCurrentScene(scene)) {
        Flags_SetCollectible(gPlayState, flag);
    }
}

bool SpendRandomizerInf(RandomizerCheck rc) {
    const RandomizerInf inf = OTRGlobals::Instance->gRandomizer->GetRandomizerInfFromCheck(rc);
    if (inf == RAND_INF_MAX) {
        return false;
    }
    Flags_SetRandomizerInf(inf);
    return true;
}

// Sets the world flag the check's own pickup tests (the one the check tracker reads back in ComboCheckFlagObtained), so
// a chest, collectible, Skulltula, NPC gift, scrub, cow or shop slot in the world counts as spent and gives nothing
// again. Returns false for a check whose flag kind is not handled.
bool SpendWorldPickup(RandomizerCheck rc) {
    const Rando::SpoilerCollectionCheck check = Rando::StaticData::GetLocation(rc)->GetCollectionCheck();
    switch (check.type) {
        case SPOILER_CHK_CHEST:
            if (check.scene >= ARRAY_COUNT(gSaveContext.sceneFlags)) {
                return false;
            }
            SpendChest(check.scene, check.flag);
            return true;
        case SPOILER_CHK_COLLECTABLE:
            if (check.scene >= ARRAY_COUNT(gSaveContext.sceneFlags)) {
                return false;
            }
            SpendCollectible(check.scene, check.flag);
            return true;
        case SPOILER_CHK_GOLD_SKULLTULA:
            if ((check.scene >> kGsFlagSceneShift) >= ARRAY_COUNT(gSaveContext.gsFlags)) {
                return false;
            }
            SET_GS_FLAGS(check.scene, check.flag);
            return true;
        case SPOILER_CHK_EVENT_CHK_INF:
            Flags_SetEventChkInf(check.flag);
            return true;
        case SPOILER_CHK_ITEM_GET_INF:
            Flags_SetItemGetInf(check.flag);
            return true;
        case SPOILER_CHK_INF_TABLE:
            Flags_SetInfTable(check.flag);
            return true;
        case SPOILER_CHK_RANDOMIZER_INF:
            return SpendRandomizerInf(rc);
        default:
            return false;
    }
}

// A check holding a Majora's Mask item: the item goes to MM's save like any pickup's, and to teammates. A trap is never
// sent.
void ReleaseForeign(RandomizerCheck rc) {
    const std::string& checkName = Rando::StaticData::GetLocation(rc)->GetName();
    const ComboRando::ForeignItem* foreign = OOT_LookupForeign(gSaveContext.fileNum, checkName);
    if (foreign == nullptr) {
        SPDLOG_WARN("[ComboShip] release: OOT foreign sentinel at '{}' has no foreign-map entry; dropping", checkName);
        return;
    }
    if (foreign->trap) {
        return;
    }
    if (gComboCrossDeliver != nullptr) {
        gComboCrossDeliver(static_cast<int>(foreign->itemGame), foreign->itemName.c_str(), checkName.c_str());
    }
    Anchor_BroadcastCrossItem(static_cast<int>(foreign->itemGame), foreign->itemName.c_str(), checkName.c_str());
}

// An OOT item: resolved against the save as it stands now (so progressive items come out in order), given, shared.
void ReleaseNative(RandomizerCheck rc) {
    const GetItemEntry entry = Rando::Context::GetInstance()->GetFinalGIEntry(rc, true, GI_NONE, false);
    Combo_GiveResolvedOOT(entry);
    if (Anchor::Instance != nullptr) {
        Anchor::Instance->SendPacket_GiveItem(entry.tableId, entry.getItemId);
    }
}

void GiveCheckItem(RandomizerCheck rc, RandomizerGet placed) {
    if (placed == RG_ICE_TRAP) {
        return;
    }
    if (placed == RG_COMBO_FOREIGN) {
        ReleaseForeign(rc);
        return;
    }
    ReleaseNative(rc);
}

// The check is marked first, so nothing can queue it again, and its world pickup is spent whatever it held. A trap is
// collected and never sprung. Returns whether the world pickup could be spent.
bool ReleaseCheck(RandomizerCheck rc) {
    Rando::ItemLocation* location = Rando::Context::GetInstance()->GetItemLocation(rc);
    const RandomizerGet placed = location->GetPlacedRandomizerGet();
    location->SetCheckStatus(RCSHOW_COLLECTED);
    const bool worldPickupSpent = SpendWorldPickup(rc);
    GiveCheckItem(rc, placed);
    return worldPickupSpent;
}

std::vector<RandomizerCheck> ReleaseAll() {
    std::vector<RandomizerCheck> released;
    for (const Rando::Location& location : Rando::StaticData::GetLocationTable()) {
        const RandomizerCheck rc = location.GetRandomizerCheck();
        if (!IsRemaining(rc)) {
            continue;
        }
        if (!ReleaseCheck(rc)) {
            SPDLOG_WARN("[ComboShip] release: '{}' has no world flag to spend; its pickup may give again",
                        location.GetName());
        }
        released.push_back(rc);
    }
    return released;
}

} // namespace

extern "C" COMBO_EXPORT int SOH_ComboReleaseOnBossKillEnabled(void) {
    return CVarGetInteger(ComboRelease::kSettingCVar, 0) != 0 ? 1 : 0;
}

// Releases every uncollected shuffled OOT check to the player. Returns how many, or ComboRelease::kCannotRelease when
// no rando save is loaded. One save for the lot (SaveBatch); the tracker marks them collected.
extern "C" COMBO_EXPORT int SOH_ReleaseRemainingChecks(void) try {
    if (!IS_RANDO || !GameInteractor::IsSaveLoaded()) {
        return ComboRelease::kCannotRelease;
    }
    std::vector<RandomizerCheck> released;
    {
        SaveBatch saveBatch;
        released = ReleaseAll();
        CheckTracker::SpoilAreasFromChecks(released);
        CheckTracker::RecalculateAllAreaTotals();
        CheckTracker::RecalculateAvailableChecks();
        Combo_PersistOOT();
    }
    if (gComboSharedChanged != nullptr) {
        gComboSharedChanged(kOotGameId, gSaveContext.fileNum);
    }
    const int count = static_cast<int>(released.size());
    Notification::Emit({ .message = ComboRelease::ReleasedMessage(count), .suffix = ComboRelease::kOotName });
    SPDLOG_INFO("[ComboShip] SOH_ReleaseRemainingChecks: released {} checks", count);
    return count;
} catch (const std::exception& e) {
    SPDLOG_ERROR("[ComboShip] SOH_ReleaseRemainingChecks threw: {}", e.what());
    return ComboRelease::kCannotRelease;
} catch (...) {
    SPDLOG_ERROR("[ComboShip] SOH_ReleaseRemainingChecks threw a non-std exception");
    return ComboRelease::kCannotRelease;
}

#endif // COMBO_BUILD
