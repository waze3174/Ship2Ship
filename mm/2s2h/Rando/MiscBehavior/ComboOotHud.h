// mm/2s2h/Rando/MiscBehavior/ComboOotHud.h
//
// ComboShip: Termina's HUD drawn as OOT's (Ship of Harkinian's) HUD for everything but MM's three-day clock —
// OOT's button sprite from OOT's own archive, OOT's colour options (the same gCosmetics.* the OOT HUD reads, so one
// colour setting styles both games), no MM drop shadows. Layout comes from OOT's HUD Placement settings through the
// HUD editor (BenGui/HudEditor.cpp). One toggle: gCombo.MmHud.OotStyle (on by default).
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "ultra64.h"
#include "color.h"

// OOT's B/C/Start/A button sprite, resolved against OOT's ResourceManager (wrap loads in gSPComboRMPush "oot").
#define COMBO_OOT_BUTTON_TEX "__OTR__textures/parameter_static/gButtonBackgroundTex"

int ComboOotHud_Enabled(void);
// The OOT colour for an MM cosmetic id (e.g. "Buttons.B"), resolved the way OOT's HUD resolves it: the element's own
// gCosmetics key when Changed, else OOT's fallback key (C buttons -> HUD.CButtons), else OOT's default for the
// active colour scheme (N64 / GameCube). 0 when the toggle is off or the id has no OOT counterpart.
int ComboOotHud_ColorFor(const char* mmCosmeticId, Color_RGB8* out);
// Same, for a colour MM DERIVES from the id with a non-default CosmeticColorMode (mode 0 = default). MM derives the
// heart border/background (HUD.Hearts / 4) and the spent-magic segment (HUD.Magic rotated); OOT has a dedicated
// option for each (Consumable.HeartBorder, Consumable.MagicActive), which is what this returns. Any other derived
// use declines (0), so MM's own derivation runs instead of a flat colour.
int ComboOotHud_ColorForEx(const char* mmCosmeticId, int mode, Color_RGB8* out);
// OOT's label for an MM do-action label texture ("Attack", "Open", "Check"... on A / B / Start), resolved against
// OOT's ResourceManager (wrap the load in gSPComboRMPush "oot"). nullptr when the toggle is off or the action is
// MM-only (Punch, Pound, Curl, Surface, Swim, Shoot, Explode, Dance, March, Hook, Snap, Warp, Info, Quit) -- those
// keep MM's art.
const char* ComboOotHud_ActionLabel(const char* mmTex);
// True while drawing an OOT-styled element (not the clock): the MM drop shadow is suppressed.
int ComboOotHud_FlatActive(void);

#ifdef __cplusplus
}
#endif
