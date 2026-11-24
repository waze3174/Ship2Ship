#include "HudEditor.h"
#include <string>
#include "macros.h"
#include "2s2h/ShipInit.hpp"

extern "C" int16_t OTRGetRectDimensionFromLeftEdge(float v);
extern "C" int16_t OTRGetRectDimensionFromRightEdge(float v);

HudEditorElementID hudEditorActiveElement = HUD_EDITOR_ELEMENT_NONE;
HudEditorElementMode hudEditorOverrideNextElemMode = HUD_EDITOR_ELEMENT_MODE_NONE;

// clang-format off
HudEditorElement hudEditorElements[HUD_EDITOR_ELEMENT_MAX] = {
    HUD_EDITOR_ELEMENT(HUD_EDITOR_ELEMENT_B, "B Button", "B", 167, 17, 100, 255, 120, 255, "Buttons.B"),
    HUD_EDITOR_ELEMENT(HUD_EDITOR_ELEMENT_C_LEFT, "C-Left Button", "CLeft", 227, 18, 255, 240, 0, 255, "Buttons.CLeft"),
    HUD_EDITOR_ELEMENT(HUD_EDITOR_ELEMENT_C_DOWN, "C-Down Button", "CDown", 249, 34, 255, 240, 0, 255, "Buttons.CDown"),
    HUD_EDITOR_ELEMENT(HUD_EDITOR_ELEMENT_C_RIGHT, "C-Right Button", "CRight", 271, 18, 255, 240, 0, 255, "Buttons.CRight"),
    HUD_EDITOR_ELEMENT(HUD_EDITOR_ELEMENT_A, "A Button", "A", 191, 18, 100, 200, 255, 255, "Buttons.A"),
    HUD_EDITOR_ELEMENT(HUD_EDITOR_ELEMENT_C_UP, "C-Up Button", "CUp", 254, 16, 255, 240, 0, 255, HUD_EDITOR_NO_COSMETIC),
    HUD_EDITOR_ELEMENT(HUD_EDITOR_ELEMENT_D_PAD, "D-Pad", "DPad", 271, 55, 255, 255, 255, 255, "Buttons.DPad"),
    HUD_EDITOR_ELEMENT(HUD_EDITOR_ELEMENT_MINIMAP, "Minimap", "Minimap", 295, 220, 0, 255, 255, 160, "HUD.Minimap"),
    HUD_EDITOR_ELEMENT(HUD_EDITOR_ELEMENT_START, "Start Button", "Start", 136, 17, 255, 130, 60, 255, "Buttons.Start"),
    HUD_EDITOR_ELEMENT(HUD_EDITOR_ELEMENT_CARROT, "Horse Carrots", "Carrots", 160, 64, 236, 92, 41, 255, HUD_EDITOR_NO_COSMETIC),
    HUD_EDITOR_ELEMENT(HUD_EDITOR_ELEMENT_CLOCK, "Three Day Clock", "Clock", 160, 206, 255, 255, 255, 255, HUD_EDITOR_NO_COSMETIC),
    HUD_EDITOR_ELEMENT(HUD_EDITOR_ELEMENT_HEARTS, "Hearts", "Hearts", 30, 26, 255, 70, 50, 255, "HUD.Hearts"),
    HUD_EDITOR_ELEMENT(HUD_EDITOR_ELEMENT_MAGIC_METER, "Magic", "Magic", 18, 34, 0, 200, 0, 255, "HUD.Magic"),
    HUD_EDITOR_ELEMENT(HUD_EDITOR_ELEMENT_TIMERS, "Timers", "Timers", 26, 46, 255, 255, 255, 255, HUD_EDITOR_NO_COSMETIC),
    HUD_EDITOR_ELEMENT(HUD_EDITOR_ELEMENT_TIMERS_MOON_CRASH, "Timer - Skull Kid", "SkullKidTimer", 115, 200, 255, 255, 255, 255, HUD_EDITOR_NO_COSMETIC),
    HUD_EDITOR_ELEMENT(HUD_EDITOR_ELEMENT_MINIGAME_COUNTER, "Minigames", "Minigames", 20, 67, 255, 255, 255, 255, HUD_EDITOR_NO_COSMETIC),
    HUD_EDITOR_ELEMENT(HUD_EDITOR_ELEMENT_RUPEE_COUNTER, "Rupees", "Rupees", 26, 206, 200, 255, 100, 255, "HUD.RupeeIcon"),
    HUD_EDITOR_ELEMENT(HUD_EDITOR_ELEMENT_KEY_COUNTER, "Keys", "Keys", 26, 190, 255, 255, 255, 255, "HUD.SmallKey"),
    HUD_EDITOR_ELEMENT(HUD_EDITOR_ELEMENT_SKULLTULA_COUNTER, "Skulltulas", "Skulltulas", 26, 190, 255, 255, 255, 255, HUD_EDITOR_NO_COSMETIC),
};
// clang-format on

// ---------------------------------------------------------------------------------------------------------------
// ComboShip: MM's HUD laid out like OOT's (gCombo.MmHud.OotStyle, on by default). The owner wants Termina's HUD to
// be OOT's for everything but the three-day clock. Every MM HUD draw already routes its position through this
// editor, so when the toggle is on the effective mode / position / scale of each element come from OOT's own HUD
// Placement settings (the shared gCosmetics.HUD.<Elem>.PosType/PosX/PosY/Scale/UseMargins and gCosmetics.HUD.Margin.*)
// with OOT's rules, instead of from MM's gHudEditor.* keys:
//   ORIGINAL_LOCATION  OOT's own coordinate, pinned to its screen edge (OOT does this by default, MM does not)
//   ANCHOR_LEFT/RIGHT  PosX/PosY from that edge;  ANCHOR_NONE  PosX/PosY in the 4:3 box;  HIDDEN  not drawn
// So OOT's HUD Placement editor moves both games' HUDs. The clock has no entry here and is never touched.
// The editor window below still shows and edits MM's own keys; they apply again when the toggle is off.
// ---------------------------------------------------------------------------------------------------------------
namespace {

enum OotPosType { OOT_ORIGINAL_LOCATION, OOT_ANCHOR_LEFT, OOT_ANCHOR_RIGHT, OOT_ANCHOR_NONE, OOT_HIDDEN };

struct OotHudSpec {
    const char* oot; // OOT's gCosmetics.HUD.<oot>.* element, or nullptr when OOT has none (MM-only element)
    int8_t side;     // where OOT pins it at ORIGINAL_LOCATION: -1 left edge, 1 right edge, 0 the 4:3 box
    bool bottom;     // bottom of the screen: OOT applies the bottom margin instead of the top one
    float ootScale;  // OOT's default scale for the element (its Scale CVar is absolute; MM's is relative)
    int16_t x, y;    // OOT's original coordinate
};

const OotHudSpec* OotSpec(HudEditorElementID id) {
    // OOT originals: soh/include/z64interface.h (buttons), soh/src/code/z_parameter.c (defaults per element).
    static const OotHudSpec kB = { "BButton", 1, false, 0.95f, 160, 17 };
    static const OotHudSpec kCL = { "CLeftButton", 1, false, 0.87f, 227, 18 };
    static const OotHudSpec kCD = { "CDownButton", 1, false, 0.87f, 249, 34 };
    static const OotHudSpec kCR = { "CRightButton", 1, false, 0.87f, 271, 18 };
    // A: MM draws it through a perspective viewport anchored at (191, 18); OOT's flat button sits ~4 px left and
    // ~13 px higher (measured side by side), so its anchor here is OOT's spot.
    static const OotHudSpec kA = { "AButton", 1, false, 1.0f, 187, 5 };
    static const OotHudSpec kCU = { "CUpButton", 1, false, 0.5f, 254, 16 };
    static const OotHudSpec kDpad = { "Dpad", 1, false, 1.0f, 271, 55 };
    static const OotHudSpec kMap = { "Minimap", 1, true, 1.0f, 295, 220 };
    static const OotHudSpec kStart = { "StartButton", 1, false, 0.75f, 132, 16 };
    static const OotHudSpec kCarrot = { "Carrots", 0, false, 1.0f, 160, 64 };
    static const OotHudSpec kHearts = { "HeartsCount", -1, false, 0.7f, 30, 26 };
    static const OotHudSpec kMagic = { "MagicBar", -1, false, 1.0f, 18, 34 };
    static const OotHudSpec kTimers = { "Timers", -1, false, 1.0f, 26, 46 };
    static const OotHudSpec kMoon = { nullptr, 0, true, 1.0f, 115, 200 };     // MM only: centred, as MM's preset
    static const OotHudSpec kMinigame = { nullptr, -1, false, 1.0f, 20, 67 }; // MM only: left, with the hearts
    static const OotHudSpec kRupees = { "Rupees", -1, true, 1.0f, 26, 206 };
    static const OotHudSpec kKeys = { "SmallKey", -1, true, 1.0f, 26, 190 };
    static const OotHudSpec kSkulls = { nullptr, -1, true, 1.0f, 26, 190 }; // MM only: where OOT's key counter sits
    switch (id) {
        case HUD_EDITOR_ELEMENT_B:
            return &kB;
        case HUD_EDITOR_ELEMENT_C_LEFT:
            return &kCL;
        case HUD_EDITOR_ELEMENT_C_DOWN:
            return &kCD;
        case HUD_EDITOR_ELEMENT_C_RIGHT:
            return &kCR;
        case HUD_EDITOR_ELEMENT_A:
            return &kA;
        case HUD_EDITOR_ELEMENT_C_UP:
            return &kCU;
        case HUD_EDITOR_ELEMENT_D_PAD:
            return &kDpad;
        case HUD_EDITOR_ELEMENT_MINIMAP:
            return &kMap;
        case HUD_EDITOR_ELEMENT_START:
            return &kStart;
        case HUD_EDITOR_ELEMENT_CARROT:
            return &kCarrot;
        case HUD_EDITOR_ELEMENT_HEARTS:
            return &kHearts;
        case HUD_EDITOR_ELEMENT_MAGIC_METER:
            return &kMagic;
        case HUD_EDITOR_ELEMENT_TIMERS:
            return &kTimers;
        case HUD_EDITOR_ELEMENT_TIMERS_MOON_CRASH:
            return &kMoon;
        case HUD_EDITOR_ELEMENT_MINIGAME_COUNTER:
            return &kMinigame;
        case HUD_EDITOR_ELEMENT_RUPEE_COUNTER:
            return &kRupees;
        case HUD_EDITOR_ELEMENT_KEY_COUNTER:
            return &kKeys;
        case HUD_EDITOR_ELEMENT_SKULLTULA_COUNTER:
            return &kSkulls;
        default:
            return nullptr; // HUD_EDITOR_ELEMENT_CLOCK: MM's own, untouched
    }
}

// The OOT-style spec for the active element, or nullptr when the toggle is off / the element is the clock.
const OotHudSpec* ActiveOot() {
    if (hudEditorActiveElement == HUD_EDITOR_ELEMENT_NONE || !CVarGetInteger("gCombo.MmHud.OotStyle", 1)) {
        return nullptr;
    }
    return OotSpec(hudEditorActiveElement);
}

int OotInt(const OotHudSpec* o, const char* leaf, int def) {
    if (o->oot == nullptr) {
        return def;
    }
    return CVarGetInteger((std::string("gCosmetics.HUD.") + o->oot + leaf).c_str(), def);
}

int OotPosType(const OotHudSpec* o) {
    return OotInt(o, ".PosType", OOT_ORIGINAL_LOCATION);
}

// OOT: X_Margins = Left/Right_HUD_Margin (added inside the edge helper), Y_Margins = -Top or +Bottom, both only
// with UseMargins on; at ORIGINAL_LOCATION OOT applies the X margin only for the edge it is pinned to.
int OotMarginX(const OotHudSpec* o, int side) {
    if (!OotInt(o, ".UseMargins", 0)) {
        return 0;
    }
    return (side < 0)   ? CVarGetInteger("gCosmetics.HUD.Margin.L", 0)
           : (side > 0) ? CVarGetInteger("gCosmetics.HUD.Margin.R", 0)
                        : 0;
}

int OotMarginY(const OotHudSpec* o) {
    if (!OotInt(o, ".UseMargins", 0)) {
        return 0;
    }
    return o->bottom ? CVarGetInteger("gCosmetics.HUD.Margin.B", 0) : -CVarGetInteger("gCosmetics.HUD.Margin.T", 0);
}

// MM's own gHudEditor.<Elem>.Mode, ignoring any one-off override (what the draw sites used to read directly).
int RawMode() {
    return CVarGetInteger(hudEditorElements[hudEditorActiveElement].modeCvar, HUD_EDITOR_ELEMENT_MODE_VANILLA);
}

// The active element's effective mode. OOT style never yields VANILLA: OOT pins even its original layout to the
// screen edges, which is the whole difference the owner is after at widescreen.
int EffMode() {
    const OotHudSpec* o = ActiveOot();
    if (o == nullptr) {
        return RawMode();
    }
    switch (OotPosType(o)) {
        case OOT_ANCHOR_LEFT:
            return HUD_EDITOR_ELEMENT_MODE_MOVABLE_LEFT;
        case OOT_ANCHOR_RIGHT:
            return HUD_EDITOR_ELEMENT_MODE_MOVABLE_RIGHT;
        case OOT_ANCHOR_NONE:
            return HUD_EDITOR_ELEMENT_MODE_MOVABLE_43;
        case OOT_HIDDEN:
            return HUD_EDITOR_ELEMENT_MODE_HIDDEN;
        default:
            return (o->side < 0)   ? HUD_EDITOR_ELEMENT_MODE_MOVABLE_LEFT
                   : (o->side > 0) ? HUD_EDITOR_ELEMENT_MODE_MOVABLE_RIGHT
                                   : HUD_EDITOR_ELEMENT_MODE_MOVABLE_43;
    }
}

// Where the element's anchor point goes (its "default" coordinate, which MM's transform offsets every rect from).
float EffX() {
    const HudEditorElement& e = hudEditorElements[hudEditorActiveElement];
    const OotHudSpec* o = ActiveOot();
    if (o == nullptr) {
        return (float)CVarGetInteger(e.xCvar, e.defaultX);
    }
    const int type = OotPosType(o);
    if (type == OOT_ORIGINAL_LOCATION) {
        return (float)(o->x + OotMarginX(o, o->side));
    }
    const int side = (type == OOT_ANCHOR_LEFT) ? -1 : (type == OOT_ANCHOR_RIGHT) ? 1 : 0;
    return (float)(OotInt(o, ".PosX", o->x) + OotMarginX(o, side));
}

float EffY() {
    const HudEditorElement& e = hudEditorElements[hudEditorActiveElement];
    const OotHudSpec* o = ActiveOot();
    if (o == nullptr) {
        return (float)CVarGetInteger(e.yCvar, e.defaultY);
    }
    const int y = (OotPosType(o) == OOT_ORIGINAL_LOCATION) ? o->y : OotInt(o, ".PosY", o->y);
    return (float)(y + OotMarginY(o));
}

// MM's scale multiplies MM's own size; OOT's Scale CVar is absolute against OOT's default, so the ratio is the
// relative size (1.0 at OOT's default and always at ORIGINAL_LOCATION, where OOT ignores its Scale CVar too).
float EffScale() {
    const OotHudSpec* o = ActiveOot();
    if (o == nullptr) {
        return CVarGetFloat(hudEditorElements[hudEditorActiveElement].scaleCvar, 1.0f);
    }
    if (o->oot == nullptr || OotPosType(o) == OOT_ORIGINAL_LOCATION) {
        return 1.0f;
    }
    const float s = CVarGetFloat((std::string("gCosmetics.HUD.") + o->oot + ".Scale").c_str(), o->ootScale);
    return (o->ootScale > 0.0f) ? (s / o->ootScale) : s;
}

} // namespace

// The active element's effective mode for the draw sites (z_parameter.c, z_lifemeter.c, kaleido...), which used to
// read gHudEditor.<Elem>.Mode directly and so would have ignored the OOT-style layout.
extern "C" int HudEditor_GetActiveElementMode() {
    return (hudEditorActiveElement == HUD_EDITOR_ELEMENT_NONE) ? HUD_EDITOR_ELEMENT_MODE_VANILLA : EffMode();
}

// Allows specifying an override mode to the next active element.
// Must be called again with HUD_EDITOR_ELEMENT_MODE_NONE when done overriding.
extern "C" void HudEditor_OverrideNextElementMode(HudEditorElementMode mode) {
    hudEditorOverrideNextElemMode = mode;
}

extern "C" bool HudEditor_ShouldOverrideDraw() {
    return hudEditorActiveElement != HUD_EDITOR_ELEMENT_NONE &&
           (hudEditorOverrideNextElemMode != HUD_EDITOR_ELEMENT_MODE_NONE
                ? hudEditorOverrideNextElemMode
                : EffMode()) != HUD_EDITOR_ELEMENT_MODE_VANILLA;
}

extern "C" void HudEditor_SetActiveElement(HudEditorElementID id) {
    hudEditorActiveElement = id;
}

extern "C" bool HudEditor_IsActiveElementHidden() {
    return hudEditorActiveElement != HUD_EDITOR_ELEMENT_NONE &&
           (hudEditorOverrideNextElemMode != HUD_EDITOR_ELEMENT_MODE_NONE
                ? hudEditorOverrideNextElemMode
                : EffMode()) == HUD_EDITOR_ELEMENT_MODE_HIDDEN;
}

extern "C" f32 HudEditor_GetActiveElementScale() {
    return (hudEditorActiveElement != HUD_EDITOR_ELEMENT_NONE &&
            hudEditorOverrideNextElemMode == HUD_EDITOR_ELEMENT_MODE_NONE)
               ? EffScale()
               : 1.0f;
}

extern "C" void HudEditor_ModifyRectPosValuesFromBase(s16 baseX, s16 baseY, s16* rectLeft, s16* rectTop) {
    s16 offsetFromBaseX = *rectLeft - baseX;
    s16 offsetFromBaseY = *rectTop - baseY;
    *rectLeft = baseX + (offsetFromBaseX * EffScale());
    *rectTop = baseY + (offsetFromBaseY * EffScale());
}

void HudEditor_ModifyRectPosValuesFloat(f32* rectLeft, f32* rectTop) {
    f32 offsetFromBaseX = *rectLeft - hudEditorElements[hudEditorActiveElement].defaultX;
    f32 offsetFromBaseY = *rectTop - hudEditorElements[hudEditorActiveElement].defaultY;
    *rectLeft = EffX() + (offsetFromBaseX * EffScale());
    *rectTop = EffY() + (offsetFromBaseY * EffScale());

    if (EffMode() == HUD_EDITOR_ELEMENT_MODE_MOVABLE_LEFT) {
        *rectLeft = OTRGetRectDimensionFromLeftEdge(*rectLeft);
    } else if (EffMode() == HUD_EDITOR_ELEMENT_MODE_MOVABLE_RIGHT) {
        *rectLeft = OTRGetRectDimensionFromRightEdge(*rectLeft);
    }
}

extern "C" void HudEditor_ModifyRectPosValues(s16* rectLeft, s16* rectTop) {
    f32 newLeft = *rectLeft;
    f32 newTop = *rectTop;

    HudEditor_ModifyRectPosValuesFloat(&newLeft, &newTop);

    *rectLeft = (s16)newLeft;
    *rectTop = (s16)newTop;
}

extern "C" void HudEditor_ModifyRectSizeValues(s16* rectWidth, s16* rectHeight) {
    *rectWidth *= EffScale();
    *rectHeight *= EffScale();
}

extern "C" void HudEditor_ModifyTextureStepValues(s16* dsdx, s16* dtdy) {
    *dsdx /= EffScale();
    *dtdy /= EffScale();
}

// Modify matrix values based on the identity matrix (0,0) centered on the screen
extern "C" void HudEditor_ModifyMatrixValues(f32* transX, f32* transY) {
    *transX = ((f32)SCREEN_WIDTH / 2) + *transX;
    *transY = ((f32)SCREEN_HEIGHT / 2) - *transY;

    HudEditor_ModifyRectPosValuesFloat(transX, transY);

    *transX = *transX - ((f32)SCREEN_WIDTH / 2);
    *transY = ((f32)SCREEN_HEIGHT / 2) - *transY;
}

extern "C" void HudEditor_ModifyKaleidoEquipAnimValues(s16* ulx, s16* uly, s16* shrinkRate) {
    // Kaleido values are a multiple of 10 on the identity matrix
    // Normalize them before passing to the modify matrix
    f32 transX = *ulx / 10;
    f32 transY = *uly / 10;

    HudEditor_ModifyMatrixValues(&transX, &transY);

    *ulx = transX * 10;
    *uly = transY * 10;

    float scale = EffScale();
    // 320 is the vanilla start size, and 280 is the vanilla end size (or 160 for dpad)
    // So we apply the scale to 280 and subtract to get the shrink rate
    int16_t endAnimSize = hudEditorActiveElement == HUD_EDITOR_ELEMENT_D_PAD ? 160 : 280;
    *shrinkRate = 320 - (s16)(endAnimSize * scale);
}

extern "C" void HudEditor_ModifyDrawValuesFromBase(s16 baseX, s16 baseY, s16* rectLeft, s16* rectTop, s16* rectWidth,
                                                   s16* rectHeight, s16* dsdx, s16* dtdy) {
    HudEditor_ModifyRectPosValuesFromBase(baseX, baseY, rectLeft, rectTop);

    *rectWidth *= EffScale();
    *rectHeight *= EffScale();
    *dsdx /= EffScale();
    *dtdy /= EffScale();
}

extern "C" void HudEditor_ModifyDrawValues(s16* rectLeft, s16* rectTop, s16* rectWidth, s16* rectHeight, s16* dsdx,
                                           s16* dtdy) {
    HudEditor_ModifyRectPosValues(rectLeft, rectTop);

    *rectWidth *= EffScale();
    *rectHeight *= EffScale();
    *dsdx /= EffScale();
    *dtdy /= EffScale();
}

const char* modeNames[] = {
    "Vanilla (4:3)", "Hidden", "Movable (Align Center)", "Movable (Align Left)", "Movable (Align Right)",
};

const char* presetNames[] = {
    "Vanilla (4:3)",
    "Hidden",
    "Widescreen",
};

static CosmeticOption& HudEditor_GetCosmeticOption(const char* cosmeticOptionId) {
    return cosmeticOptions.at(cosmeticOptionId);
}

namespace HudEditor {
enum Presets {
    VANILLA,
    HIDDEN,
    WIDESCREEN,
};
};

void HudEditorWindow::DrawElement() {
    if (CVarGetInteger("gCombo.MmHud.OotStyle", 1)) {
        // ComboShip: the OOT-style HUD takes its layout from OOT's HUD Placement settings instead (see the layer
        // above).
        ImGui::TextWrapped(
            "The Ocarina of Time-style HUD is on (Settings tab): Termina's HUD follows Ocarina of Time's "
            "HUD Placement and colour settings, so the positions below apply only when it is off. "
            "The Three Day Clock still uses its settings here.");
        ImGui::Separator();
    }
    static HudEditor::Presets preset = HudEditor::Presets::VANILLA;
    if (UIWidgets::Combobox("Preset", &preset, presetNames)) {
        for (int i = HUD_EDITOR_ELEMENT_B; i < HUD_EDITOR_ELEMENT_MAX; i++) {
            CVarClear(hudEditorElements[i].xCvar);
            CVarClear(hudEditorElements[i].yCvar);
            CVarClear(hudEditorElements[i].scaleCvar);
            CVarClear(hudEditorElements[i].modeCvar);
            // Also clear cosmetic colors for elements with mappings
            if (hudEditorElements[i].cosmeticOptionId != nullptr) {
                CosmeticOption& cosmeticElement = HudEditor_GetCosmeticOption(hudEditorElements[i].cosmeticOptionId);
                CVarClear(cosmeticElement.colorCvar);
                CVarClear(cosmeticElement.colorChangedCvar);
                ShipInit::Init(cosmeticElement.colorCvar);
                ShipInit::Init(cosmeticElement.colorChangedCvar);
            }
        }

        switch (preset) {
            case HudEditor::Presets::HIDDEN: {
                for (int i = HUD_EDITOR_ELEMENT_B; i < HUD_EDITOR_ELEMENT_MAX; i++) {
                    CVarSetInteger(hudEditorElements[i].modeCvar, HUD_EDITOR_ELEMENT_MODE_HIDDEN);
                }
                break;
            }
            case HudEditor::Presets::WIDESCREEN: {
                CVarSetInteger(hudEditorElements[HUD_EDITOR_ELEMENT_B].modeCvar, HUD_EDITOR_ELEMENT_MODE_MOVABLE_RIGHT);
                CVarSetInteger(hudEditorElements[HUD_EDITOR_ELEMENT_C_LEFT].modeCvar,
                               HUD_EDITOR_ELEMENT_MODE_MOVABLE_RIGHT);
                CVarSetInteger(hudEditorElements[HUD_EDITOR_ELEMENT_C_DOWN].modeCvar,
                               HUD_EDITOR_ELEMENT_MODE_MOVABLE_RIGHT);
                CVarSetInteger(hudEditorElements[HUD_EDITOR_ELEMENT_C_RIGHT].modeCvar,
                               HUD_EDITOR_ELEMENT_MODE_MOVABLE_RIGHT);
                CVarSetInteger(hudEditorElements[HUD_EDITOR_ELEMENT_A].modeCvar, HUD_EDITOR_ELEMENT_MODE_MOVABLE_RIGHT);
                CVarSetInteger(hudEditorElements[HUD_EDITOR_ELEMENT_C_UP].modeCvar,
                               HUD_EDITOR_ELEMENT_MODE_MOVABLE_RIGHT);
                CVarSetInteger(hudEditorElements[HUD_EDITOR_ELEMENT_D_PAD].modeCvar,
                               HUD_EDITOR_ELEMENT_MODE_MOVABLE_RIGHT);
                CVarSetInteger(hudEditorElements[HUD_EDITOR_ELEMENT_MINIMAP].modeCvar,
                               HUD_EDITOR_ELEMENT_MODE_MOVABLE_RIGHT);
                CVarSetInteger(hudEditorElements[HUD_EDITOR_ELEMENT_START].modeCvar,
                               HUD_EDITOR_ELEMENT_MODE_MOVABLE_RIGHT);
                CVarSetInteger(hudEditorElements[HUD_EDITOR_ELEMENT_CARROT].modeCvar,
                               HUD_EDITOR_ELEMENT_MODE_MOVABLE_43);
                CVarSetInteger(hudEditorElements[HUD_EDITOR_ELEMENT_CLOCK].modeCvar,
                               HUD_EDITOR_ELEMENT_MODE_MOVABLE_43);
                CVarSetInteger(hudEditorElements[HUD_EDITOR_ELEMENT_HEARTS].modeCvar,
                               HUD_EDITOR_ELEMENT_MODE_MOVABLE_LEFT);
                CVarSetInteger(hudEditorElements[HUD_EDITOR_ELEMENT_MAGIC_METER].modeCvar,
                               HUD_EDITOR_ELEMENT_MODE_MOVABLE_LEFT);
                CVarSetInteger(hudEditorElements[HUD_EDITOR_ELEMENT_TIMERS].modeCvar,
                               HUD_EDITOR_ELEMENT_MODE_MOVABLE_LEFT);
                CVarSetInteger(hudEditorElements[HUD_EDITOR_ELEMENT_TIMERS_MOON_CRASH].modeCvar,
                               HUD_EDITOR_ELEMENT_MODE_MOVABLE_43);
                CVarSetInteger(hudEditorElements[HUD_EDITOR_ELEMENT_MINIGAME_COUNTER].modeCvar,
                               HUD_EDITOR_ELEMENT_MODE_MOVABLE_LEFT);
                CVarSetInteger(hudEditorElements[HUD_EDITOR_ELEMENT_RUPEE_COUNTER].modeCvar,
                               HUD_EDITOR_ELEMENT_MODE_MOVABLE_LEFT);
                CVarSetInteger(hudEditorElements[HUD_EDITOR_ELEMENT_KEY_COUNTER].modeCvar,
                               HUD_EDITOR_ELEMENT_MODE_MOVABLE_LEFT);
                CVarSetInteger(hudEditorElements[HUD_EDITOR_ELEMENT_SKULLTULA_COUNTER].modeCvar,
                               HUD_EDITOR_ELEMENT_MODE_MOVABLE_LEFT);
                break;
            }
        }
        Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }

    for (int i = HUD_EDITOR_ELEMENT_B; i < HUD_EDITOR_ELEMENT_MAX; i++) {
        ImGui::PushID(hudEditorElements[i].name);
        ImGui::SeparatorText(hudEditorElements[i].name);

        // Color picker - only enabled if this element has a cosmetic counterpart
        bool hasCosmeticMapping = hudEditorElements[i].cosmeticOptionId != nullptr;

        if (hasCosmeticMapping) {
            CosmeticOption& cosmeticElement = HudEditor_GetCosmeticOption(hudEditorElements[i].cosmeticOptionId);
            bool colorChanged = CVarGetInteger(cosmeticElement.colorChangedCvar, false);
            float defaultColor[4] = { cosmeticElement.defaultR / 255.0f, cosmeticElement.defaultG / 255.0f,
                                      cosmeticElement.defaultB / 255.0f, cosmeticElement.defaultA / 255.0f };
            float color[4] = { defaultColor[0], defaultColor[1], defaultColor[2], defaultColor[3] };

            if (colorChanged) {
                Color_RGBA8 changedColor = CVarGetColor(cosmeticElement.colorCvar, {});
                color[0] = (float)changedColor.r / 255;
                color[1] = (float)changedColor.g / 255;
                color[2] = (float)changedColor.b / 255;
                color[3] = (float)changedColor.a / 255;
            }

            if (ImGui::ColorEdit3("Color", color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
                Color_RGBA8 colorSelected;
                colorSelected.r = static_cast<uint8_t>(color[0] * 255.0f);
                colorSelected.g = static_cast<uint8_t>(color[1] * 255.0f);
                colorSelected.b = static_cast<uint8_t>(color[2] * 255.0f);
                colorSelected.a = static_cast<uint8_t>(255.0f);

                CVarSetColor(cosmeticElement.colorCvar, colorSelected);
                CVarSetInteger(cosmeticElement.colorChangedCvar, true);
                ShipInit::Init(cosmeticElement.colorCvar);
                ShipInit::Init(cosmeticElement.colorChangedCvar);
                Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_REFRESH)) {
                CVarClear(cosmeticElement.colorCvar);
                CVarClear(cosmeticElement.colorChangedCvar);
                ShipInit::Init(cosmeticElement.colorCvar);
                ShipInit::Init(cosmeticElement.colorChangedCvar);
                Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
            }
        } else {
            // Disabled color picker for elements without cosmetic mappings
            ImGui::BeginDisabled();
            float defaultColor[4] = { hudEditorElements[i].defaultR / 255.0f, hudEditorElements[i].defaultG / 255.0f,
                                      hudEditorElements[i].defaultB / 255.0f, hudEditorElements[i].defaultA / 255.0f };
            ImGui::ColorEdit3("Color", defaultColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("%s", "Color customization is not yet available for this element.");
            }
            ImGui::SameLine();
            ImGui::Button(ICON_FA_REFRESH);
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (UIWidgets::CVarCombobox("Mode", hudEditorElements[i].modeCvar, modeNames,
                                    { .labelPosition = UIWidgets::LabelPosition::None })) {
            CVarClear(hudEditorElements[i].xCvar);
            CVarClear(hudEditorElements[i].yCvar);
            CVarClear(hudEditorElements[i].scaleCvar);
        }
        if (CVarGetInteger(hudEditorElements[i].modeCvar, HUD_EDITOR_ELEMENT_MODE_VANILLA) >=
            HUD_EDITOR_ELEMENT_MODE_MOVABLE_43) {
            if (ImGui::BeginTable("##table", 3,
                                  ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_NoBordersInBody |
                                      ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextColumn();
                UIWidgets::CVarSliderInt("X", hudEditorElements[i].xCvar,
                                         {
                                             .showAdjustmentButtons = false,
                                             .format = "X: %d",
                                             .min = -10,
                                             .max = 330,
                                             .defaultValue = hudEditorElements[i].defaultX,
                                             .labelPosition = UIWidgets::LabelPosition::None,
                                         });
                ImGui::TableNextColumn();
                UIWidgets::CVarSliderInt("Y", hudEditorElements[i].yCvar,
                                         {
                                             .showAdjustmentButtons = false,
                                             .format = "Y: %d",
                                             .min = -10,
                                             .max = 250,
                                             .defaultValue = hudEditorElements[i].defaultY,
                                             .labelPosition = UIWidgets::LabelPosition::None,
                                         });
                ImGui::TableNextColumn();
                UIWidgets::CVarSliderFloat("Scale", hudEditorElements[i].scaleCvar,
                                           {
                                               .showAdjustmentButtons = false,
                                               .format = "Scale: %.2f",
                                               .min = 0.25f,
                                               .max = 4.0f,
                                               .defaultValue = 1.0f,
                                               .labelPosition = UIWidgets::LabelPosition::None,
                                           });
                ImGui::EndTable();
            }
        }
        ImGui::PopID();
    }
}
