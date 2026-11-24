// mm/2s2h/Rando/MiscBehavior/ComboOotHud.cpp — see ComboOotHud.h.
#include "ComboOotHud.h"
#include "2s2h/BenGui/HudEditor.h"
#include <libultraship/bridge/consolevariablebridge.h>
#include <cstring>
#include <string>
#include <unordered_map>

namespace {

// OOT's soh/soh/Enhancements/cosmetics/cosmeticsTypes.h ColorScheme.
constexpr int kOotColorSchemeGameCube = 1;

struct OotColorRule {
    const char* mmId;       // MM's cosmetic id at the draw site
    const char* ootKey;     // gCosmetics.<ootKey>.{Changed,Value}
    const char* ootFallback; // read first, like OOT's HUD.CButtons seed for each C button (nullptr = none)
    Color_RGB8 n64;         // OOT's default
    Color_RGB8 gamecube;    // OOT's default with the GameCube scheme
};

// Defaults and resolution order copied from OOT: soh/src/code/z_parameter.c (Interface_DrawItemButtons,
// Interface_DrawActionButton, the counters) and soh/src/code/z_lifemeter.c.
const OotColorRule kRules[] = {
    { "Buttons.A", "HUD.AButton", nullptr, { 90, 90, 255 }, { 0, 200, 50 } },
    { "Buttons.B", "HUD.BButton", nullptr, { 0, 150, 0 }, { 255, 30, 30 } },
    { "Buttons.CLeft", "HUD.CLeftButton", "HUD.CButtons", { 255, 160, 0 }, { 255, 160, 0 } },
    { "Buttons.CDown", "HUD.CDownButton", "HUD.CButtons", { 255, 160, 0 }, { 255, 160, 0 } },
    { "Buttons.CRight", "HUD.CRightButton", "HUD.CButtons", { 255, 160, 0 }, { 255, 160, 0 } },
    { "Buttons.CUp", "HUD.CUpButton", "HUD.CButtons", { 255, 160, 0 }, { 255, 160, 0 } },
    { "Buttons.Start", "HUD.StartButton", nullptr, { 200, 0, 0 }, { 120, 120, 120 } },
    { "Buttons.DPad", "HUD.Dpad", nullptr, { 255, 255, 255 }, { 255, 255, 255 } },
    { "HUD.Hearts", "Consumable.Hearts", nullptr, { 255, 70, 50 }, { 255, 70, 50 } },
    { "HUD.Magic", "Consumable.Magic", nullptr, { 0, 200, 0 }, { 0, 200, 0 } },
    { "HUD.Minimap", "HUD.Minimap", nullptr, { 0, 255, 255 }, { 0, 255, 255 } },
    { "HUD.SmallKey", "HUD.KeyCount", nullptr, { 200, 230, 255 }, { 200, 230, 255 } },
    // Double-defense heart border: MM sets it with a plain (default-mode) env override.
    { "HUD.HeartsDoubleDefense", "Consumable.DDHeartBorder", nullptr, { 200, 0, 0 }, { 200, 0, 0 } },
};

// Colours MM derives from an id with a non-default mode; OOT keeps a separate option for each
// (soh/src/code/z_lifemeter.c HealthMeter_Init, soh/src/code/z_parameter.c Interface_DrawMagicBar).
const OotColorRule kDerivedRules[] = {
    { "HUD.Hearts", "Consumable.HeartBorder", nullptr, { 50, 40, 60 }, { 50, 40, 60 } },
    { "HUD.Magic", "Consumable.MagicActive", nullptr, { 250, 250, 0 }, { 250, 250, 0 } },
};

bool Changed(const char* key) {
    return CVarGetInteger((std::string("gCosmetics.") + key + ".Changed").c_str(), 0) != 0;
}

Color_RGB8 Value(const char* key, Color_RGB8 def) {
    return CVarGetColor24((std::string("gCosmetics.") + key + ".Value").c_str(), def);
}

// OOT's resolution order for one colour: the element's fallback key (HUD.CButtons for each C button) when Changed,
// then the element's own key when Changed, else OOT's default for the active scheme.
int Resolve(const OotColorRule* rules, size_t count, const char* mmId, Color_RGB8* out) {
    for (size_t i = 0; i < count; ++i) {
        const OotColorRule& r = rules[i];
        if (std::strcmp(r.mmId, mmId) != 0) {
            continue;
        }
        Color_RGB8 c = (CVarGetInteger("gCosmetics.DefaultColorScheme", 0) == kOotColorSchemeGameCube) ? r.gamecube
                                                                                                         : r.n64;
        if (r.ootFallback != nullptr && Changed(r.ootFallback)) {
            c = Value(r.ootFallback, c);
        }
        if (Changed(r.ootKey)) {
            c = Value(r.ootKey, c);
        }
        *out = c;
        return 1;
    }
    return 0;
}

} // namespace

extern "C" int ComboOotHud_Enabled(void) {
    return CVarGetInteger("gCombo.MmHud.OotStyle", 1) != 0;
}

extern "C" int ComboOotHud_ColorFor(const char* mmCosmeticId, Color_RGB8* out) {
    if (mmCosmeticId == nullptr || !ComboOotHud_Enabled()) {
        return 0;
    }
    // MM colours its rupee icon by wallet; OOT keeps it green unless Dynamic Wallet Icon is on, in which case the
    // wallet colour MM already passes in is the OOT behaviour too.
    if (std::strcmp(mmCosmeticId, "HUD.RupeeIcon") == 0) {
        if (CVarGetInteger("gEnhancements.DynamicWalletIcon", 0)) {
            return 0;
        }
        *out = Changed("Consumable.GreenRupee") ? Value("Consumable.GreenRupee", { 200, 255, 100 })
                                                : Color_RGB8{ 200, 255, 100 };
        return 1;
    }
    return Resolve(kRules, sizeof(kRules) / sizeof(kRules[0]), mmCosmeticId, out);
}

extern "C" int ComboOotHud_ColorForEx(const char* mmCosmeticId, int mode, Color_RGB8* out) {
    if (mode == 0) {
        return ComboOotHud_ColorFor(mmCosmeticId, out);
    }
    if (mmCosmeticId == nullptr || !ComboOotHud_Enabled()) {
        return 0;
    }
    return Resolve(kDerivedRules, sizeof(kDerivedRules) / sizeof(kDerivedRules[0]), mmCosmeticId, out);
}

extern "C" int ComboOotHud_FlatActive(void) {
    return ComboOotHud_Enabled() && (hudEditorActiveElement != HUD_EDITOR_ELEMENT_NONE) &&
           (hudEditorActiveElement != HUD_EDITOR_ELEMENT_CLOCK);
}

extern "C" const char* ComboOotHud_ActionLabel(const char* mmTex) {
    if (mmTex == nullptr || !ComboOotHud_Enabled()) {
        return nullptr;
    }
    // MM: ".../gDoAction<Name>ENGTex"  ->  OOT: "__OTR__textures/do_action_static/g<Name>DoActionENGTex"
    static const char* kOotHas[] = { "Attack", "Check",  "Enter", "Return", "Open", "Jump",  "Decide", "Dive",
                                     "Faster", "Throw",  "Climb", "Drop",   "Down", "Speak", "Next",   "Grab",
                                     "Stop",   "PutAway", "Reel", "Num1",   "Num2", "Num3",  "Num4",   "Num5",
                                     "Num6",   "Num7",   "Num8" };
    static std::unordered_map<std::string, std::string> sCache;
    const char* p = std::strstr(mmTex, "gDoAction");
    if (p == nullptr) {
        return nullptr;
    }
    p += 9;
    const char* end = std::strstr(p, "ENGTex");
    if (end == nullptr || end[6] != '\0') {
        return nullptr; // only the English set has OOT twins
    }
    const std::string name(p, end - p);
    for (const char* n : kOotHas) {
        if (name == n) {
            auto it = sCache.find(name);
            if (it == sCache.end()) {
                it = sCache.emplace(name, "__OTR__textures/do_action_static/g" + name + "DoActionENGTex").first;
            }
            return it->second.c_str();
        }
    }
    return nullptr;
}
