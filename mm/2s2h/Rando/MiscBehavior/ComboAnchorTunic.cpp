// mm/2s2h/Rando/MiscBehavior/ComboAnchorTunic.cpp — ComboShip: "Anchor color on every tunic", MM side (rationale:
// combo/ComboAnchorTunic.h). While MM is the foreground game it keeps all eight tunic slots on the Anchor color and
// re-applies MM's tunic patches (and OOT's through SOH_ComboApplyCosmetics). MM_ComboApplyCosmetics lets OOT re-apply
// MM's patches when it does the same while MM is dormant (the forms in Hyrule draw MM's patched models).
#include <libultraship/libultraship.h>
#include <optional>
#include <ship/resource/CrossRMRegistry.h>
#include <ship/resource/ResourceManagerScope.h>

#include "ComboAnchorTunic.h"
#include "ComboExport.h"
#include "ComboResolve.h"
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/ShipInit.hpp"

// Re-run MM's cosmetic patch functions for the tunic slots (they are ShipInit funcs keyed by their CVars).
extern "C" COMBO_EXPORT void MM_ComboApplyCosmetics(void) {
    // MM's patch functions (ResourceMgr_PatchGfxByName / the palette shaders) patch through the ACTIVE resource
    // manager; called from Hyrule that is OOT's, which has no object_link_goron etc., so the forms in Hyrule never got
    // the color. Run them against MM's own (the same resources the forms draw in Hyrule).
    std::shared_ptr<Ship::ResourceManager> mmRm = Ship::CrossRMRegistry::Get("mm");
    std::optional<Ship::ResourceManagerScope> scope;
    if (mmRm != nullptr) {
        scope.emplace(mmRm);
    }
    for (const auto& slot : ComboAnchorTunic::kMmSlots) {
        ShipInit::Init(slot.colorCVar);
        ShipInit::Init(slot.changedCVar);
    }
}

static void ApplyOotCosmetics() {
    typedef void (*FnVoid)(void);
    static FnVoid sFn = (FnVoid)Combo_ResolveSym("soh", "SOH_ComboApplyCosmetics");
    if (sFn != nullptr) {
        sFn();
    }
}

static void RegisterComboAnchorTunic() {
    COND_HOOK(OnGameStateUpdate, true, []() {
        if (ComboAnchorTunic::Sync(ComboAnchorTunic::kMmSlots)) {
            MM_ComboApplyCosmetics();
        }
        if (ComboAnchorTunic::Sync(ComboAnchorTunic::kOotSlots)) {
            ApplyOotCosmetics();
        }
    });
}
static RegisterShipInitFunc sComboAnchorTunicInit(RegisterComboAnchorTunic, {});
