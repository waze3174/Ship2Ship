#pragma once

// combo/ComboAnchorTunic.h — ComboShip: "Anchor color on every tunic" (owner ask 2026-11-12). Set your color once in
// the Anchor (co-op) panel and it becomes every tunic and transformation tunic in both games:
//   OOT: Kokiri / Goron / Zora Tunic (also what adult Link wears in Termina);
//   MM:  human tunic and the Deku / Goron / Zora / Fierce Deity tunic slots (also what the forms wear in Hyrule,
//        which draw MM's own patched models).
// Both games keep their cosmetics as shared CVars, so either game's frame hook can write all eight slots; each game
// then re-applies its own model patches (the other game's through its combo export). Header-only; used by soh and
// 2ship.

#include <cstddef>
#include <libultraship/libultraship.h>

namespace ComboAnchorTunic {

inline constexpr const char* kToggleCVar = "gCombo.Cosmetics.AnchorTunicSync";
inline constexpr const char* kAnchorColorCVar = "gRemote.Anchor.Color.Value";

struct Slot {
    const char* colorCVar;
    const char* changedCVar;
};

inline constexpr Slot kOotSlots[] = {
    { "gCosmetics.Link.KokiriTunic.Value", "gCosmetics.Link.KokiriTunic.Changed" },
    { "gCosmetics.Link.GoronTunic.Value", "gCosmetics.Link.GoronTunic.Changed" },
    { "gCosmetics.Link.ZoraTunic.Value", "gCosmetics.Link.ZoraTunic.Changed" },
};

// 2ship's cosmetic CVars are "gCosmetic.<id>" (singular; CosmeticEditor.h CVAR_PREFIX_COSMETIC), OOT's "gCosmetics.".
inline constexpr Slot kMmSlots[] = {
    { "gCosmetic.Player.HumanTunic.Color", "gCosmetic.Player.HumanTunic.Changed" },
    { "gCosmetic.Player.DekuTunic.Color", "gCosmetic.Player.DekuTunic.Changed" },
    { "gCosmetic.Player.GoronTunic.Color", "gCosmetic.Player.GoronTunic.Changed" },
    { "gCosmetic.Player.ZoraTunic.Color", "gCosmetic.Player.ZoraTunic.Changed" },
    { "gCosmetic.Player.FierceDeityTunic.Color", "gCosmetic.Player.FierceDeityTunic.Changed" },
};

inline bool Enabled() {
    return CVarGetInteger(kToggleCVar, 0) != 0;
}

// Writes the Anchor color into the slots that differ. Returns true if anything changed (re-apply that game's patches).
template <size_t N> inline bool Sync(const Slot (&slots)[N]) {
    if (!Enabled()) {
        return false;
    }
    Color_RGBA8 want = CVarGetColor(kAnchorColorCVar, { 100, 255, 100, 255 });
    want.a = 255;
    bool changed = false;
    for (const Slot& slot : slots) {
        const Color_RGBA8 cur = CVarGetColor(slot.colorCVar, { 0, 0, 0, 0 });
        if ((cur.r != want.r) || (cur.g != want.g) || (cur.b != want.b) || (cur.a != want.a) ||
            (CVarGetInteger(slot.changedCVar, 0) == 0)) {
            CVarSetColor(slot.colorCVar, want);
            CVarSetInteger(slot.changedCVar, 1);
            changed = true;
        }
    }
    return changed;
}

} // namespace ComboAnchorTunic
