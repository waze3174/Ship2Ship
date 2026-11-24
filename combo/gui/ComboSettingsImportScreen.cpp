// combo/gui/ComboSettingsImportScreen.cpp
// ComboShip-owned first-launch settings-import screen (see ComboSettingsImportScreen.h). Structurally
// a trimmed ComboExtractScreen: one PICK phase, three optional slots (SoH / 2Ship / ComboShip), Browse +
// auto-detect, Import / Skip. The launcher does the JSON parse + merge + apply; this file only renders and reports
// the chosen paths.
#include "ComboSettingsImportScreen.h"
#include "ComboConfigImport.h" // ComboPresets::DescribeConfigCheck, shared with the menu import
#include "ComboConfigTree.h"   // kConfigFileName / kLegacyConfigFileName: the auto-detect file names
#include "ComboExport.h"
#include "ComboFilePicker.h"  // native Browse dialog (comdlg32 / pfd), shared with the extraction screen
#include "ComboWidgetStyle.h" // ComboMenu_ThemeColor / PushButton / PopButton

#include <libultraship/libultraship.h>
#include <fast/Fast3dWindow.h>
#include <imgui.h>

#include <string>
#include <vector>
#include <filesystem>
#include <cstring>
#include <cstdlib>

using ComboRando::ComboMenu_PopButton;
using ComboRando::ComboMenu_PopCheckbox;
using ComboRando::ComboMenu_PushButton;
using ComboRando::ComboMenu_PushCheckbox;
using ComboRando::ComboMenu_ThemeColor;

namespace {

constexpr int kSlotCount = 3;
constexpr int kSohSlot = 0;
constexpr int kMmSlot = 1;
constexpr int kComboSlot = 2;

constexpr const char* kNotImportedNote = "This file will not be imported.";
constexpr const char* kUseGameSlotNote =
    "If it is a Ship of Harkinian or 2 Ship 2 Harkinian config, choose it in that game's slot instead.";

struct ConfigSlot {
    const char* label = "";
    std::vector<const char*> fileNames; // expected filenames for auto-detect, in order
    const char* macAppSupportDir = "";  // that game's bundle id, for the macOS auto-detect below
    ComboFnCheckConfig check = nullptr;
    std::string path;
    ComboConfigCheck result = COMBO_CONFIG_OK;
};

ComboConfigCheck CheckSlotFile(const ConfigSlot& slot, const std::string& path) {
    return slot.check ? slot.check(path.c_str()) : COMBO_CONFIG_OK;
}

// Red, wrapped: why the chosen file is refused. Only OK files are imported (see ComboSettingsImport.h).
void DrawRefusal(ComboConfigCheck result, const ImVec4& red) {
    ImGui::PushStyleColor(ImGuiCol_Text, red);
    ImGui::TextWrapped("%s %s", ComboPresets::DescribeConfigCheck(result), kNotImportedNote);
    if (result == COMBO_CONFIG_NO_COMBO) {
        ImGui::TextWrapped("%s", kUseGameSlotNote);
    }
    ImGui::PopStyleColor();
}

// Directories to probe for an existing install's config, most likely first.
// Windows and Linux installs keep the config beside the executable, so the working directory covers
// those. macOS does NOT: libultraship puts config under ~/Library/Application Support/<bundle id>,
// which Finder hides by default — so without this probe Browse is the only route and the user has no
// reasonable way to find the file.
std::vector<std::filesystem::path> ConfigSearchDirs(const char* macAppSupportDir) {
    std::vector<std::filesystem::path> dirs;
    std::error_code ec;
    if (auto cwd = std::filesystem::current_path(ec); !ec) {
        dirs.push_back(cwd);
    }
#ifdef __APPLE__
    if (macAppSupportDir != nullptr && *macAppSupportDir != '\0') {
        if (const char* home = std::getenv("HOME")) {
            dirs.push_back(std::filesystem::path(home) / "Library" / "Application Support" / macAppSupportDir);
        }
    }
#else
    (void)macAppSupportDir;
#endif
    return dirs;
}

// Native open-file dialog filtered to .json. Returns "" if cancelled.
std::string PickConfigFile() {
    return ComboFilePicker::PickFile("Select settings file", "Settings (*.json)\0*.json\0All Files (*.*)\0*.*\0",
                                     { "Settings (*.json)", "*.json", "All Files", "*" });
}

void CopyPath(char* dst, size_t cap, const std::string& src) {
    std::strncpy(dst, src.c_str(), cap - 1);
    dst[cap - 1] = '\0';
}

void UseSharedImGuiContext() {
    auto ctx = Ship::Context::GetRawInstance();
    if (ctx && ctx->GetWindow() && ctx->GetWindow()->GetGui()) {
        ImGui::SetCurrentContext(ctx->GetWindow()->GetGui()->GetImGuiContext());
    }
}

} // namespace

extern "C" COMBO_EXPORT int ComboUI_RunSettingsImport(const ComboSettingsImportCallbacks* cb,
                                                      ComboSettingsImportResult* out) {
    if (!out) {
        return 0;
    }
    out->action = 0;
    out->sohPath[0] = '\0';
    out->mmPath[0] = '\0';
    out->comboPath[0] = '\0';
    out->comboIncludeModsAndControls = 0;

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

    ConfigSlot slots[kSlotCount];
    slots[kSohSlot].label = "Ship of Harkinian (OoT)";
    slots[kSohSlot].fileNames = { "shipofharkinian.json" };
    slots[kSohSlot].macAppSupportDir = "com.shipofharkinian.soh";
    slots[kSohSlot].check = cb ? cb->sohCheck : nullptr;
    slots[kMmSlot].label = "2 Ship 2 Harkinian (MM)";
    slots[kMmSlot].fileNames = { "2ship2harkinian.json" };
    slots[kMmSlot].macAppSupportDir = "com.2ship2harkinian.2s2h";
    slots[kMmSlot].check = cb ? cb->mmCheck : nullptr;
    slots[kComboSlot].label = "Ship2Ship or ComboShip";
    slots[kComboSlot].fileNames = { ComboConfigTree::kConfigFileName, ComboConfigTree::kLegacyConfigFileName };
    slots[kComboSlot].check = cb ? cb->comboCheck : nullptr;
    bool includeModsAndControls = true;

    // Auto-detect each game's config (same convenience as the ROM auto-scan). First hit wins; a
    // rejected file keeps searching, so a stale copy in the working dir can't mask a real install.
    // This install's own ship2ship.json (the window init has already saved it) is rejected by the check.
    for (auto& s : slots) {
        std::error_code ec;
        for (const auto& dir : ConfigSearchDirs(s.macAppSupportDir)) {
            for (const char* fileName : s.fileNames) {
                const auto candidate = dir / fileName;
                if (!std::filesystem::exists(candidate, ec)) {
                    continue;
                }
                const std::string found = candidate.string();
                if (CheckSlotFile(s, found) == COMBO_CONFIG_OK) {
                    s.path = found;
                    s.result = COMBO_CONFIG_OK;
                    break;
                }
            }
            if (!s.path.empty()) {
                break;
            }
        }
    }

    bool finished = false;
    while (!finished) {
        if (!ctx->GetWindow()->IsRunning()) {
            return 0; // window closed -> launcher treats as Skip
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
        ImGui::GetBackgroundDrawList()->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y),
                                                      IM_COL32(0, 0, 0, 210));

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
        bool open = ImGui::Begin("Ship2Ship - Import Settings", nullptr,
                                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        if (open) {
            ImGui::SetWindowFontScale(1.4f);
            ImGui::TextColored(theme, "Ship2Ship Settings Import");
            ImGui::SetWindowFontScale(1.0f);
            ImGui::Separator();

            ImGui::TextWrapped("Optional: bring over your settings from an existing Ship of Harkinian and/or "
                               "2 Ship 2 Harkinian install, or from a Ship2Ship or ComboShip install (its "
                               "ship2ship.json or comboship.json). "
                               "Pick any of the files, or Skip to start fresh. Where they set the same option "
                               "(e.g. graphics or controls), OoT wins, then MM, then Ship2Ship or ComboShip.");
            if (!ComboFilePicker::Available()) {
                // Wrapped text in the disabled color (TextDisabled doesn't wrap), ASCII only —
                // same treatment as the extraction screen's hint.
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                ImGui::TextWrapped("No file-picker helper found (install zenity or kdialog); only "
                                   "auto-detected configs can be imported here, or Skip to start fresh.");
                ImGui::PopStyleColor();
            }
            ImGui::Spacing();

            for (int i = 0; i < kSlotCount; i++) {
                ConfigSlot& s = slots[i];
                ImGui::PushID(i);
                ImGui::TextColored(theme, "%s", s.label);
                ImGui::TextWrapped("%s", s.path.empty() ? "(none selected)" : s.path.c_str());
                ComboMenu_PushButton(theme);
                if (!ComboFilePicker::Available()) {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button(s.path.empty() ? "Browse..." : "Change")) {
                    std::string picked = PickConfigFile();
                    if (!picked.empty()) {
                        s.path = picked;
                        s.result = CheckSlotFile(s, picked);
                    }
                }
                if (!ComboFilePicker::Available()) {
                    ImGui::EndDisabled();
                }
                ComboMenu_PopButton();
                if (!s.path.empty()) {
                    if (s.result == COMBO_CONFIG_OK) {
                        ImGui::SameLine();
                        ImGui::TextColored(kGreen, "Looks like a %s config", s.label);
                    } else {
                        DrawRefusal(s.result, kRed);
                    }
                }
                if (i == kComboSlot) {
                    ComboMenu_PushCheckbox(theme);
                    ImGui::Checkbox("Also import mods list and controls", &includeModsAndControls);
                    ComboMenu_PopCheckbox();
                }
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::PopID();
            }

            ComboMenu_PushButton(theme);
            if (ImGui::Button("Import", ImVec2(150.0f, 0.0f))) {
                out->action = 1;
                CopyPath(out->sohPath, sizeof(out->sohPath), slots[kSohSlot].path);
                CopyPath(out->mmPath, sizeof(out->mmPath), slots[kMmSlot].path);
                CopyPath(out->comboPath, sizeof(out->comboPath), slots[kComboSlot].path);
                out->comboIncludeModsAndControls = includeModsAndControls ? 1 : 0;
                finished = true;
            }
            ComboMenu_PopButton();
            ImGui::SameLine();
            ComboMenu_PushButton(theme);
            if (ImGui::Button("Skip", ImVec2(150.0f, 0.0f))) {
                out->action = 0;
                finished = true;
            }
            ComboMenu_PopButton();
        }
        ImGui::End();

        gui->EndDraw();
        wnd->EndFrame();
    }

    return 1;
}
