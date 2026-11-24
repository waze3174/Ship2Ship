// combo/gui/ComboBottleShelfWindow.cpp — ComboShip Bottle Shelf (owner ruling 2026-11-12: 10 bottles, all usable
// in both games). Shows the FOREGROUND game's native bottle slots (OOT 4, MM 6) and its shelf (the rest of the 10).
// Pick a shelved bottle, then a slot: they swap (an empty slot just takes it). Buttons pointing at that slot update
// on the spot. Toggled from the combo menu (gCombo.BottleShelf.Open); controller navigation works like any ImGui
// window.

#include <string>

#include <imgui.h>
#include <libultraship/libultraship.h>
#include <nlohmann/json.hpp>
#include <ship/Context.h>
#include <ship/window/gui/GuiWindow.h>

#include "ComboForeground.h"
#include "ComboResolve.h"

namespace {

constexpr const char* kOpenCvar = "gCombo.BottleShelf.Open";

typedef const char* (*FnDescribe)(void);
typedef int (*FnSwap)(int, int);

struct GameSyms {
    FnDescribe describe = nullptr;
    FnSwap swap = nullptr;
};

GameSyms Syms(int game) {
    static GameSyms oot, mm;
    if (game == 1) {
        if (!mm.describe) {
            mm.describe = (FnDescribe)Combo_ResolveSym("2ship", "MM_BottleShelfDescribe");
            mm.swap = (FnSwap)Combo_ResolveSym("2ship", "MM_BottleShelfSwap");
        }
        return mm;
    }
    if (!oot.describe) {
        oot.describe = (FnDescribe)Combo_ResolveSym("soh", "SOH_BottleShelfDescribe");
        oot.swap = (FnSwap)Combo_ResolveSym("soh", "SOH_BottleShelfSwap");
    }
    return oot;
}

class ComboBottleShelfWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    void Draw() override {
        if (!IsVisible())
            return;
        auto ctx = Ship::Context::GetRawInstance();
        if (!ctx || !ctx->GetWindow() || !ctx->GetWindow()->GetGui())
            return;
        ImGui::SetCurrentContext(ctx->GetWindow()->GetGui()->GetImGuiContext());
        ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_FirstUseEver);
        bool open = true;
        if (ImGui::Begin("Bottle Shelf", &open, ImGuiWindowFlags_AlwaysAutoResize))
            DrawElement();
        ImGui::End();
        if (!open)
            CVarSetInteger(kOpenCvar, 0);
        SyncVisibilityConsoleVariable();
    }

  protected:
    void InitElement() override {
    }
    void UpdateElement() override {
    }

    void DrawElement() override {
        const int game = ComboUI::GetForegroundGame();
        GameSyms s = Syms(game);
        if (!s.describe || !s.swap) {
            ImGui::TextDisabled("Bottle Shelf unavailable (rebuild the game modules).");
            return;
        }
        nlohmann::json d;
        try {
            d = nlohmann::json::parse(s.describe());
        } catch (...) {}
        const auto native = d.value("native", nlohmann::json::array());
        const auto shelf = d.value("shelf", nlohmann::json::array());
        ImGui::TextDisabled("%s: pick a shelved bottle, then a slot to swap it in.",
                            game == 1 ? "Majora's Mask" : "Ocarina of Time");

        ImGui::SeparatorText("Shelf");
        if (shelf.empty())
            ImGui::TextDisabled("(empty)");
        if (mPicked >= static_cast<int>(shelf.size()))
            mPicked = -1;
        for (int i = 0; i < static_cast<int>(shelf.size()); ++i) {
            ImGui::PushID(i);
            std::string label = shelf[i].value("name", std::string("?"));
            if (ImGui::Selectable(label.c_str(), mPicked == i))
                mPicked = (mPicked == i) ? -1 : i;
            ImGui::PopID();
        }

        ImGui::SeparatorText("Bottle slots");
        for (int i = 0; i < static_cast<int>(native.size()); ++i) {
            ImGui::PushID(1000 + i);
            std::string label = "Slot " + std::to_string(i + 1) + ": " + native[i].value("name", std::string("?"));
            ImGui::BeginDisabled(mPicked < 0);
            if (ImGui::Button(label.c_str(), ImVec2(-1, 0)) && mPicked >= 0) {
                if (s.swap(mPicked, i))
                    mPicked = -1;
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
    }

  private:
    int mPicked = -1;
};

std::shared_ptr<ComboBottleShelfWindow> sWindow;

} // namespace

namespace ComboRando {
void RegisterBottleShelfWindow() {
    auto ctx = Ship::Context::GetRawInstance();
    if (!ctx || !ctx->GetWindow() || !ctx->GetWindow()->GetGui())
        return;
    if (!sWindow)
        sWindow = std::make_shared<ComboBottleShelfWindow>(kOpenCvar, "Bottle Shelf");
    ctx->GetWindow()->GetGui()->AddGuiWindow(sWindow);
}
} // namespace ComboRando
