// combo/gui/ComboExtractScreen.cpp
// ComboShip-owned ROM-extraction screen (see ComboExtractScreen.h).
//
// Why this lives in comboui.dll: the friendly progress UI needs a window + ImGui context, and the
// only window in the process is libultraship's, created by SOH_InitWindowOnly() (the OoT OTRGlobals
// ctor, from soh.o2r) before any ROM exists. comboui already drives ImGui against that shared context
// (ComboMenu.cpp), so it owns the frame loop here. The actual ROM validation + ZAPD work happens in
// soh.dll / 2ship.dll via the function pointers in ComboExtractCallbacks; this file only renders.
#include "ComboExtractScreen.h"
#include <cstdlib>
#include "ComboExport.h"
#include "ComboFilePicker.h"  // native Browse dialog (comdlg32 / pfd), shared with the settings-import screen
#include "ComboWidgetStyle.h" // ComboShip port style: ComboMenu_ThemeColor / PushButton / PopButton

#include <libultraship/libultraship.h>
#include <ship/window/FileDropMgr.h> // only forward-declared by the umbrella header
#include <fast/Fast3dWindow.h>
#include <imgui.h>

#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <cstdio>
#include <cctype>

using ComboRando::ComboMenu_PopButton;
using ComboRando::ComboMenu_PushButton;
using ComboRando::ComboMenu_ThemeColor; // port style helpers (combo/gui/ComboWidgetStyle.h)

namespace {

// Slot order on screen, and the order the ROMs are extracted in.
enum SlotIndex { kSlotOot, kSlotMm, kSlotOotMq, kSlotCount };

// How a ROM relates to the slot it was put in.
enum class RomFit {
    Fits,
    NotARom,
    WrongQuest, // a real OoT ROM, but the other quest's: Master Quest vs vanilla
};

constexpr const char* kMasterQuestInVanillaSlot = "This is a Master Quest ROM; put it in the Master Quest slot";
constexpr const char* kVanillaInMasterQuestSlot = "This is not a Master Quest ROM; put it in the Ocarina of Time slot";

struct RomSlot {
    const char* label = "";
    // archive missing -> must extract
    bool needed = false;
    // may stay empty; an empty optional slot is skipped
    bool optional = false;
    // takes the Master Quest OoT ROM (and only that)
    bool masterQuest = false;
    ComboFnValidateRom validate = nullptr;
    // header-only version check for the auto-scan
    ComboFnValidateRom classify = nullptr;
    // splits vanilla from Master Quest; set on the two OoT slots only
    ComboFnRomQuest quest = nullptr;
    ComboFnStartExtraction start = nullptr;
    ComboFnGetProgress progress = nullptr;
    std::string path;
    RomFit fit = RomFit::NotARom;
    // When the ROM doesn't fit: the slot it belongs in instead (the other game or quest), or -1.
    int owner = -1;
    // extraction runtime state
    bool started = false;
    bool done = false;
    bool success = false;

    bool valid() const {
        return fit == RomFit::Fits;
    }
    // Empty optional slots are skipped; everything else that is needed gets extracted.
    bool willExtract() const {
        return needed && !(optional && path.empty());
    }
    // Required slots need a valid ROM. Optional ones may be empty, but not hold a bad ROM.
    bool satisfied() const {
        return !willExtract() || valid();
    }
};

// The header-only check that routes auto-scanned and dropped ROMs; falls back to the full one.
ComboFnValidateRom RoutingCheck(const RomSlot& s) {
    return s.classify ? s.classify : s.validate;
}

// Whether the ROM at path belongs in slot s, judged with `check` (header-only to route, full on Browse).
RomFit JudgeRom(const RomSlot& s, ComboFnValidateRom check, const char* path) {
    if (!check || !check(path)) {
        return RomFit::NotARom;
    }
    if (!s.quest) {
        return RomFit::Fits;
    }
    const int quest = s.quest(path);
    if (s.masterQuest) {
        if (quest == COMBO_ROM_QUEST_MASTER) {
            return RomFit::Fits;
        }
        return quest == COMBO_ROM_QUEST_VANILLA ? RomFit::WrongQuest : RomFit::NotARom;
    }
    return quest == COMBO_ROM_QUEST_MASTER ? RomFit::WrongQuest : RomFit::Fits;
}

// Puts the ROM in the slot only if it fits there. True if it was placed.
bool ClaimRom(RomSlot& s, const std::string& path) {
    const RomFit fit = JudgeRom(s, RoutingCheck(s), path.c_str());
    if (fit != RomFit::Fits) {
        return false;
    }
    s.path = path;
    s.fit = fit;
    s.owner = -1;
    return true;
}

// The slot other than `except` that the ROM at path fits, judged header-only; -1 if none.
int FindOwnerSlot(const RomSlot* slots, int except, const std::string& path) {
    for (int i = 0; i < kSlotCount; i++) {
        if (i != except && JudgeRom(slots[i], RoutingCheck(slots[i]), path.c_str()) == RomFit::Fits) {
            return i;
        }
    }
    return -1;
}

// Puts the ROM in slot `index` whether or not it fits, so the slot can say what is wrong with it.
void AssignRom(RomSlot* slots, int index, const std::string& path, ComboFnValidateRom check) {
    RomSlot& s = slots[index];
    s.path = path;
    s.fit = JudgeRom(s, check, path.c_str());
    s.owner = s.valid() ? -1 : FindOwnerSlot(slots, index, path);
}

void DrawRomProblem(const RomSlot* slots, const RomSlot& s, const ImVec4& color) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    if (s.owner >= 0 && !slots[s.owner].needed) {
        ImGui::TextWrapped("This is the %s ROM, which is already extracted", slots[s.owner].label);
    } else if (s.fit == RomFit::WrongQuest) {
        ImGui::TextWrapped("%s", s.masterQuest ? kVanillaInMasterQuestSlot : kMasterQuestInVanillaSlot);
    } else if (s.owner >= 0) {
        ImGui::TextWrapped("This is the %s ROM, not a %s ROM", slots[s.owner].label, s.label);
    } else {
        ImGui::TextWrapped("Not a valid %s ROM", s.label);
    }
    ImGui::PopStyleColor();
}

// Native open-file dialog filtered to N64 ROM extensions. Returns "" if cancelled. The Linux/macOS patterns list
// both cases, since those dialogs may match case-sensitively; the Windows dialog never does.
std::string PickRomFile() {
    return ComboFilePicker::PickFile(
        "Select ROM", "N64 ROMs (*.z64;*.n64;*.v64)\0*.z64;*.n64;*.v64\0All Files (*.*)\0*.*\0",
        { "N64 ROMs (*.z64 *.n64 *.v64)", "*.z64 *.n64 *.v64 *.Z64 *.N64 *.V64", "All Files", "*" });
}

// The UI disables Browse and points at drag & drop when no dialog helper exists (Linux only).
bool PickerAvailable() {
    return ComboFilePicker::Available();
}

// Dropped-file hand-off from the gfx backend's event pump (via FileDropMgr) to the PICK loop
// below. Same thread — the pump runs inside wnd->HandleEvents() — so a plain vector suffices.
// FileDropMgr calls the handler once per file, so a multi-file drop arrives as several calls in one
// frame. Registered only while the extraction screen is up; returns true so FileDropMgr doesn't emit
// its "unsupported file" overlay for what is actually the expected input here.
std::vector<std::string> sDroppedRomPaths;
bool HandleRomDrop(char* path) {
    if (path != nullptr) {
        sDroppedRomPaths.emplace_back(path);
    }
    return true;
}

// Non-recursive scan of a directory for candidate ROM files.
void ScanForRoms(const std::filesystem::path& dir, std::vector<std::string>& out) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        return;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        if (ext == ".z64" || ext == ".n64" || ext == ".v64") {
            out.push_back(entry.path().string());
        }
    }
}

// ImGui's GImGui is a per-module global; point comboui's at the shared libultraship context
// (same pattern as ComboMenu.cpp) before any ImGui call.
void UseSharedImGuiContext() {
    auto ctx = Ship::Context::GetRawInstance();
    if (ctx && ctx->GetWindow() && ctx->GetWindow()->GetGui()) {
        ImGui::SetCurrentContext(ctx->GetWindow()->GetGui()->GetImGuiContext());
    }
}

} // namespace

extern "C" COMBO_EXPORT int ComboUI_RunExtraction(const ComboExtractCallbacks* cb) {
    if (!cb) {
        return 0;
    }
    auto ctx = Ship::Context::GetRawInstance();
    if (!ctx || !ctx->GetWindow() || !ctx->GetWindow()->GetGui()) {
        return 0;
    }
    auto wnd = std::dynamic_pointer_cast<Fast::Fast3dWindow>(ctx->GetWindow());
    if (!wnd) {
        return 0;
    }
    auto gui = wnd->GetGui();
    UseSharedImGuiContext();

    RomSlot slots[kSlotCount];
    RomSlot& oot = slots[kSlotOot];
    oot.label = "Ocarina of Time";
    oot.needed = cb->sohNeeded != 0;
    oot.validate = cb->sohValidate;
    oot.classify = cb->sohClassify;
    oot.quest = cb->sohQuest;
    oot.start = cb->sohStart;
    oot.progress = cb->sohProgress;
    RomSlot& mm = slots[kSlotMm];
    mm.label = "Majora's Mask";
    mm.needed = cb->mmNeeded != 0;
    mm.validate = cb->mmValidate;
    mm.classify = cb->mmClassify;
    mm.start = cb->mmStart;
    mm.progress = cb->mmProgress;
    // Same extraction functions as the OoT slot: the extractor sends a Master Quest ROM to oot-mq.o2r itself.
    RomSlot& ootMq = slots[kSlotOotMq];
    ootMq.label = "OoT Master Quest";
    ootMq.needed = cb->sohMqNeeded != 0;
    ootMq.optional = true;
    ootMq.masterQuest = true;
    ootMq.validate = cb->sohValidate;
    ootMq.classify = cb->sohClassify;
    ootMq.quest = cb->sohQuest;
    ootMq.start = cb->sohStart;
    ootMq.progress = cb->sohProgress;

    // Auto-scan the working directory and classify any ROMs found into the needed slots.
    {
        std::vector<std::string> candidates;
        ScanForRoms(std::filesystem::current_path(), candidates);
        for (const auto& c : candidates) {
            for (auto& s : slots) {
                if (!s.needed || s.valid()) {
                    continue;
                }
                // Route by the header-only classify (fast); fall back to full validate if unavailable.
                if (ClaimRom(s, c)) {
                    break;
                }
            }
        }
    }

    // Accept ROMs dragged onto the window while this screen is up. The gfx backends hand drops
    // to FileDropMgr (created by the window-only boot precisely so it exists here); the handler
    // stashes the path and the PICK phase below routes it into a slot.
    auto fileDropMgr = ctx->GetFileDropMgr();
    sDroppedRomPaths.clear();
    if (fileDropMgr != nullptr) {
        fileDropMgr->RegisterDropHandler(HandleRomDrop);
    }

    enum Phase { PICK, EXTRACTING, FAILED };
    Phase phase = PICK;
    int activeSlot = -1; // slot currently extracting, -1 = pick next
    int result = 0;
    bool finished = false;

    while (!finished) {
        if (!ctx->GetWindow()->IsRunning()) {
            result = 0;
            break;
        }
        wnd->HandleEvents();
        if (!wnd->IsFrameReady()) {
            continue;
        }

        UseSharedImGuiContext();
        gui->StartDraw();
        wnd->StartFrame();
        wnd->RunGuiOnly();

        const ImVec4 theme = ComboMenu_ThemeColor();
        const ImVec4 kGreen(0.5f, 1.0f, 0.5f, 1.0f);
        const ImVec4 kRed(1.0f, 0.4f, 0.4f, 1.0f);

        ImGuiViewport* vp = ImGui::GetMainViewport();
        // Dim the game behind, mirroring the menu's translucent fullscreen overlay (ComboMenu.cpp).
        ImGui::GetBackgroundDrawList()->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y),
                                                      IM_COL32(0, 0, 0, 210));

        // Preferred 680px, but clamp to the main window's work area so we never bleed outside it
        // (a new player's window can launch small/square before any resolution is persisted).
        const float kMargin = 20.0f;
        float panelW = vp->WorkSize.x - kMargin * 2.0f;
        if (panelW > 680.0f) {
            panelW = 680.0f;
        }
        float panelMaxH = vp->WorkSize.y - kMargin * 2.0f;
        ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(panelW, 0.0f), ImVec2(panelW, panelMaxH));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 20.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.09f, 0.09f, 0.11f, 0.98f));
        bool open = ImGui::Begin("Ship2Ship - ROM Setup", nullptr,
                                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        if (open) {
            ImGui::SetWindowFontScale(1.4f);
            ImGui::TextColored(theme, "Ship2Ship ROM Setup");
            ImGui::SetWindowFontScale(1.0f);
            ImGui::Separator();

            if (phase == PICK) {
                // ROMs dropped onto the window: route each by the header-only classify (same
                // routing as the auto-scan above), replacing a prior selection for that game —
                // a drop is an explicit user action. If no slot claims one, park it in the first
                // unfilled slot so the slot's problem line explains what happened.
                if (!sDroppedRomPaths.empty()) {
                    std::vector<std::string> dropped = std::move(sDroppedRomPaths);
                    sDroppedRomPaths.clear();
                    if (fileDropMgr != nullptr) {
                        fileDropMgr->ClearDroppedFile();
                    }
                    for (const auto& path : dropped) {
                        bool placed = false;
                        for (auto& s : slots) {
                            if (s.needed && ClaimRom(s, path)) {
                                placed = true;
                                break;
                            }
                        }
                        for (int i = 0; !placed && i < kSlotCount; i++) {
                            if (slots[i].needed && !slots[i].valid()) {
                                AssignRom(slots, i, path, RoutingCheck(slots[i]));
                                placed = true;
                            }
                        }
                    }
                }

                if (oot.needed && mm.needed) {
                    ImGui::TextWrapped("Ship2Ship needs both an Ocarina of Time ROM and a Majora's Mask ROM. "
                                       "Select each ROM below or drag and drop ROM files onto this window, "
                                       "then click Extract. Both are required.");
                } else {
                    const RomSlot& missing = oot.needed ? oot : mm;
                    const RomSlot& extracted = oot.needed ? mm : oot;
                    ImGui::TextWrapped("Ship2Ship needs your %s ROM (%s is already extracted). Select it below "
                                       "or drag and drop the ROM file onto this window, then click Extract.",
                                       missing.label, extracted.label);
                }
                if (ootMq.needed) {
                    ImGui::TextWrapped("An Ocarina of Time Master Quest ROM is optional. Add it to play "
                                       "Master Quest in addition to the regular game.");
                }
                if (!PickerAvailable()) {
                    // TextDisabled doesn't wrap (ran off the panel), and the game font has no
                    // em-dash glyph — so: wrapped text in the disabled color, ASCII only.
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                    ImGui::TextWrapped("No file-picker helper found (install zenity or kdialog); "
                                       "drag and drop your ROM files onto this window instead.");
                    ImGui::PopStyleColor();
                }
                ImGui::Spacing();

                for (int i = 0; i < kSlotCount; i++) {
                    RomSlot& s = slots[i];
                    ImGui::PushID(i);
                    ImGui::TextColored(theme, "%s%s", s.label, s.optional ? " (optional)" : "");
                    if (!s.needed) {
                        ImGui::SameLine();
                        ImGui::TextColored(kGreen, "(already extracted)");
                    } else {
                        ImGui::TextWrapped("%s", s.path.empty() ? "(no ROM selected)" : s.path.c_str());
                        ComboMenu_PushButton(theme);
                        if (!PickerAvailable()) {
                            ImGui::BeginDisabled();
                        }
                        if (ImGui::Button(s.path.empty() ? "Browse..." : "Change")) {
                            std::string picked = PickRomFile();
                            if (!picked.empty()) {
                                AssignRom(slots, i, picked, s.validate);
                            }
                        }
                        if (!PickerAvailable()) {
                            ImGui::EndDisabled();
                        }
                        ComboMenu_PopButton();
                        if (s.optional && !s.path.empty()) {
                            ImGui::SameLine();
                            ComboMenu_PushButton(theme);
                            if (ImGui::Button("Clear")) {
                                s.path.clear();
                                s.fit = RomFit::NotARom;
                                s.owner = -1;
                            }
                            ComboMenu_PopButton();
                        }
                        ImGui::SameLine();
                        if (s.path.empty()) {
                            ImGui::TextDisabled(s.optional ? "optional" : "required");
                        } else if (s.valid()) {
                            ImGui::TextColored(kGreen, "Valid %s ROM", s.label);
                        } else {
                            DrawRomProblem(slots, s, kRed);
                        }
                    }
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();
                    ImGui::PopID();
                }

                bool ready = true;
                for (const auto& s : slots) {
                    if (!s.satisfied()) {
                        ready = false;
                    }
                }

                if (!ready) {
                    ImGui::BeginDisabled();
                }
                ComboMenu_PushButton(theme);
                // Debug (test harness only): COMBO_AUTO_EXTRACT=1 presses Extract once both ROMs are found.
                if (ImGui::Button("Extract", ImVec2(150.0f, 0.0f)) || (ready && std::getenv("COMBO_AUTO_EXTRACT"))) {
                    phase = EXTRACTING;
                    activeSlot = -1;
                }
                ComboMenu_PopButton();
                if (!ready) {
                    ImGui::EndDisabled();
                }
                ImGui::SameLine();
                ComboMenu_PushButton(theme);
                if (ImGui::Button("Quit", ImVec2(150.0f, 0.0f))) {
                    result = 0;
                    finished = true;
                }
                ComboMenu_PopButton();
            } else if (phase == EXTRACTING) {
                if (activeSlot < 0) {
                    // Pick the next needed-but-unfinished slot, or finish.
                    int next = -1;
                    for (int i = 0; i < kSlotCount; i++) {
                        if (slots[i].willExtract() && !slots[i].done) {
                            next = i;
                            break;
                        }
                    }
                    if (next < 0) {
                        bool allOk = true;
                        for (const auto& s : slots) {
                            if (s.willExtract() && !s.success) {
                                allOk = false;
                            }
                        }
                        if (allOk) {
                            result = 1;
                            finished = true;
                        } else {
                            phase = FAILED;
                        }
                    } else {
                        activeSlot = next;
                        slots[next].started = slots[next].start && slots[next].start(slots[next].path.c_str());
                        if (!slots[next].started) {
                            slots[next].done = true;
                            slots[next].success = false;
                            activeSlot = -1;
                        }
                    }
                } else {
                    RomSlot& s = slots[activeSlot];
                    unsigned long long count = 0, total = 0;
                    int done = 0, success = 0;
                    if (s.progress) {
                        s.progress(&count, &total, &done, &success);
                    }
                    float pct = total > 0 ? (float)count / (float)total : 0.0f;
                    ImGui::Text("Extracting %s...", s.label);
                    ImGui::Spacing();
                    char overlay[32];
                    if (count > 0) {
                        snprintf(overlay, sizeof(overlay), "%.0f%%", pct * 100.0f);
                    } else {
                        snprintf(overlay, sizeof(overlay), "Starting Up");
                    }
                    // Themed progress bar (matches OTRGlobals::RunExtract's "ROM Extraction" modal).
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(theme.x, theme.y, theme.z, 0.5f));
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(theme.x, theme.y, theme.z, 1.0f));
                    ImGui::ProgressBar(pct, ImVec2(ImGui::GetContentRegionAvail().x, 38.0f), overlay);
                    ImGui::PopStyleColor(2);
                    ImGui::PopStyleVar(1);
                    if (done) {
                        s.done = true;
                        s.success = (success != 0);
                        activeSlot = -1;
                    }
                }
            } else { // FAILED
                ImGui::TextColored(kRed, "Extraction failed. Check that your ROM is a supported, uncompressed "
                                         "copy, then relaunch.");
                ImGui::Spacing();
                ComboMenu_PushButton(theme);
                if (ImGui::Button("Quit", ImVec2(150.0f, 0.0f))) {
                    result = 0;
                    finished = true;
                }
                ComboMenu_PopButton();
            }
        }
        ImGui::End();

        gui->EndDraw();
        wnd->EndFrame();
    }

    if (fileDropMgr != nullptr) {
        fileDropMgr->UnregisterDropHandler(HandleRomDrop);
        fileDropMgr->ClearDroppedFile();
    }
    sDroppedRomPaths.clear();

    return result;
}
