// mm/2s2h/Rando/MiscBehavior/ComboReleaseChecks.cpp — ComboShip: MM's side of "Release checks on boss kill".
// Combo-owned file (2ship GLOB_RECURSE). The launcher (Combo_ReleaseGameChecks) calls MM_ReleaseRemainingChecks once
// Majora is beaten; this marks every uncollected shuffled MM check collected and hands its item over with no get-item
// cutscene and no textbox. Deviation record: docs/deviations/rando.md "Release checks on boss kill".
#ifdef COMBO_BUILD

#include <string>

#include <libultraship/libultraship.h>

#include "ComboExport.h"
#include "ComboRelease.h"
#include "2s2h/BenGui/Notification.h"
#include "MiscBehavior.h"
#include "2s2h/Rando/StaticData/StaticData.h"
#include "2s2h/SaveManager/SaveManager.h"

extern "C" {
#include "variables.h"
}

// BenPort.cpp: the save-direct give of a resolved item (the one the cross-game delivery and Anchor use).
void Combo_MM_GiveDormantResolved(RandoItemId rid);
// BenPort.cpp: routes a foreign item to the other game's resident save; MMAnchor.cpp shares it with teammates.
extern "C" void (*gMMComboCrossDeliver)(int targetGame, const char* itemName, const char* srcCheckName);
extern "C" void MMAnchor_BroadcastCrossItem(int targetGame, const char* itemName, const char* srcCheckName);

namespace {

// A check still owed to the player. One the CheckQueue has flagged eligible is already on its way through the normal
// give, so it is left to that, rather than given twice.
bool IsRemaining(const RandoSaveCheck& check) {
    return check.shuffled && !check.obtained && !check.eligible;
}

void MarkCollected(RandoSaveCheck& check) {
    check.obtained = true;
    check.cycleObtained = true;
    check.eligible = false;
}

// A check holding an Ocarina of Time item: the item goes to OoT's save like any pickup's, and to teammates. A trap is
// never sent.
void ReleaseForeign(RandoCheckId rc) {
    const std::string& checkName = Rando::StaticData::GetCheckDisplayName(rc);
    const ComboRando::ForeignItem* foreign = Rando::MiscBehavior::MM_LookupForeign(rc);
    if (foreign == nullptr) {
        SPDLOG_WARN("[ComboShip] release: MM foreign sentinel at '{}' has no foreign-map entry; dropping", checkName);
        return;
    }
    if (foreign->trap) {
        return;
    }
    if (gMMComboCrossDeliver != nullptr) {
        gMMComboCrossDeliver(static_cast<int>(foreign->itemGame), foreign->itemName.c_str(), checkName.c_str());
    }
    MMAnchor_BroadcastCrossItem(static_cast<int>(foreign->itemGame), foreign->itemName.c_str(), checkName.c_str());
}

// An MM item: resolved against the save as it stands now (so progressive items come out in order), given, shared.
void ReleaseNative(RandoCheckId rc, RandoItemId placed) {
    RandoItemId item = Rando::ConvertItem(placed, rc);
    if (item == RI_JUNK) {
        item = Rando::CurrentJunkItem(rc);
    }
    Combo_MM_GiveDormantResolved(item);
    Rando::MiscBehavior::BroadcastCheckObtainedIfFirst(rc, placed, false);
}

// The check is marked first, so nothing can queue it again. A trap is collected and never sprung.
void ReleaseCheck(RandoCheckId rc) {
    RandoSaveCheck& check = RANDO_SAVE_CHECKS[rc];
    const RandoItemId placed = check.randoItemId;
    MarkCollected(check);
    if (placed == RI_TRAP) {
        return;
    }
    if (placed == RI_COMBO_FOREIGN) {
        ReleaseForeign(rc);
        return;
    }
    ReleaseNative(rc, placed);
}

} // namespace

// Releases every uncollected shuffled MM check to the player. Returns how many, or ComboRelease::kCannotRelease when no
// rando save is loaded. One container write for the lot (ComboSaveBatch). The tracker reads RANDO_SAVE_CHECKS live, so
// it shows them collected on its next frame.
extern "C" COMBO_EXPORT int MM_ReleaseRemainingChecks(void) try {
    if (!IS_RANDO || gSaveContext.fileNum == 0xFF) {
        return ComboRelease::kCannotRelease;
    }
    int released = 0;
    {
        ComboSaveBatch saveBatch;
        for (const auto& [rc, _] : Rando::StaticData::Checks) {
            if (!IsRemaining(RANDO_SAVE_CHECKS[rc])) {
                continue;
            }
            ReleaseCheck(rc);
            ++released;
        }
        // The batch writes only a save something asked for, and a trap or an Ocarina of Time item asks for none.
        if (released > 0) {
            SaveManager_SaveCurrentForCombo();
        }
    }
    Notification::Emit({ .message = ComboRelease::ReleasedMessage(released), .suffix = ComboRelease::kMmName });
    SPDLOG_INFO("[ComboShip] MM_ReleaseRemainingChecks: released {} checks", released);
    return released;
} catch (const std::exception& e) {
    SPDLOG_ERROR("[ComboShip] MM_ReleaseRemainingChecks threw: {}", e.what());
    return ComboRelease::kCannotRelease;
} catch (...) {
    SPDLOG_ERROR("[ComboShip] MM_ReleaseRemainingChecks threw a non-std exception");
    return ComboRelease::kCannotRelease;
}

#endif // COMBO_BUILD
