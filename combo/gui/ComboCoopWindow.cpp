// combo/gui/ComboCoopWindow.cpp — ComboShip co-op world sync UI (full-coop-sync plan tasks 0.5, 1.7, 1.9).
//
//  * Co-op Sync debug window (CVar gRemote.Anchor.Coop.Debug): bytes/s in and out by packet type,
//    SessionGuard drops, the peer table with seed/protocol/world-settings status, room-seed transfer.
//    Actor rows (key, owner, epoch, snapshot age, HP, scale k) join in milestone A-M3.
//  * Different-seed modal: shown whenever a save on another seed is loaded while in a room (SessionGuard
//    is already blocking sync; this makes it visible).
//  * Room-seed toast: "Loaded <host>'s room seed" with the seed's hash icons as numbers.
//
// Draw() runs every frame whatever the visibility CVar says, and drives the launcher's idleTick, which is
// how leaving a room restores the user's own world settings even after the game-side pump has stopped.

#include <cstdio>
#include <set>
#include <string>

#include <imgui.h>
#include <libultraship/libultraship.h>
#include <nlohmann/json.hpp>
#include <ship/Context.h>
#include <ship/window/gui/GuiWindow.h>

#include "ComboExport.h"
#include "coop/CoopUiAbi.h"

namespace {

constexpr const char* kDebugCvar = "gRemote.Anchor.Coop.Debug";
constexpr const char* kAutoAcceptCvar = "gRemote.Anchor.Coop.AutoAcceptRoomSeed";

const ComboCoopHooks* sHooks = nullptr;

nlohmann::json ParseOr(const char* s) {
    try {
        return s ? nlohmann::json::parse(s) : nlohmann::json::object();
    } catch (...) { return nlohmann::json::object(); }
}

std::string HashIcons(const nlohmann::json& fh) {
    std::string out;
    if (!fh.is_array())
        return out;
    for (auto& v : fh) {
        if (!out.empty())
            out += "-";
        out += std::to_string(v.is_number() ? v.get<int>() : 0);
    }
    return out;
}

std::string HumanBytes(double b) {
    char buf[32];
    if (b >= 1024.0 * 1024.0)
        std::snprintf(buf, sizeof(buf), "%.1f MB", b / (1024.0 * 1024.0));
    else if (b >= 1024.0)
        std::snprintf(buf, sizeof(buf), "%.1f KB", b / 1024.0);
    else
        std::snprintf(buf, sizeof(buf), "%.0f B", b);
    return buf;
}

class ComboCoopWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    void Draw() override {
        auto ctx = Ship::Context::GetRawInstance();
        if (!ctx || !ctx->GetWindow() || !ctx->GetWindow()->GetGui())
            return;
        ImGui::SetCurrentContext(ctx->GetWindow()->GetGui()->GetImGuiContext());
        if (!sHooks)
            return;
        if (sHooks->idleTick)
            sHooks->idleTick();

        double now = ImGui::GetTime();
        if (now - mLastPoll > 0.25) {
            mLastPoll = now;
            mSeed = ParseOr(sHooks->seedStatusJson ? sHooks->seedStatusJson() : nullptr);
            mPrevStats = mStats;
            mStats = ParseOr(sHooks->statsJson ? sHooks->statsJson() : nullptr);
        }
        DrawToast(now);
        DrawMismatchModal();
        if (IsVisible()) {
            ImGui::SetNextWindowSize(ImVec2(560, 420), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Co-op Sync", nullptr)) {
                DrawElement();
            }
            ImGui::End();
            SyncVisibilityConsoleVariable();
        }
    }

  protected:
    void InitElement() override {
    }
    void UpdateElement() override {
    }

    void DrawElement() override {
        bool autoAccept = CVarGetInteger(kAutoAcceptCvar, 1) != 0;
        if (ImGui::Checkbox("Auto-load the room host's seed at file select", &autoAccept)) {
            CVarSetInteger(kAutoAcceptCvar, autoAccept ? 1 : 0);
            CVarSave();
        }
        DrawSeedSection();
        if (ImGui::CollapsingHeader("Traffic", ImGuiTreeNodeFlags_DefaultOpen))
            DrawTraffic();
        if (ImGui::CollapsingHeader("This client"))
            DrawSelf();
    }

  private:
    double mLastPoll = -1.0;
    nlohmann::json mSeed = nlohmann::json::object();
    nlohmann::json mStats = nlohmann::json::object();
    nlohmann::json mPrevStats = nlohmann::json::object();
    std::string mToastText;
    double mToastUntil = 0.0;
    std::string mLastMessage;
    bool mModalDismissed = false;
    std::string mModalSeed;

    void DrawSeedSection() {
        std::string state = mSeed.value("state", std::string("none"));
        ImGui::SeparatorText("Room seed");
        if (state == "none") {
            ImGui::TextDisabled("No room seed (the host has none loaded, or not connected)");
            return;
        }
        ImGui::Text("Seed %s   icons %s", mSeed.value("roomSeedId", std::string()).c_str(),
                    HashIcons(mSeed.value("fileHash", nlohmann::json::array())).c_str());
        if (state == "host") {
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "You are the room host: joiners receive this seed.");
            if (sHooks->shareSeed && ImGui::Button("Share my seed again"))
                sHooks->shareSeed();
        } else if (state == "downloading") {
            ImGui::Text("Downloading from %s: %d / %d", mSeed.value("host", std::string()).c_str(),
                        mSeed.value("received", 0), mSeed.value("total", 0));
        } else if (state == "ready") {
            ImGui::TextColored(ImVec4(0.6f, 1, 0.6f, 1), "Downloaded from %s.",
                               mSeed.value("host", std::string()).c_str());
            if (sHooks->useRoomSeed && ImGui::Button("Use room seed"))
                sHooks->useRoomSeed();
        } else if (state == "applied") {
            ImGui::TextColored(ImVec4(0.6f, 1, 0.6f, 1), "Playing the room seed.");
        } else if (state == "mismatch") {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Your save is on a different seed: sync is blocked.");
        } else if (state == "failed") {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", mSeed.value("message", std::string()).c_str());
        }
    }

    void DrawTraffic() {
        double secs = mStats.value("seconds", 0.0), prevSecs = mPrevStats.value("seconds", 0.0);
        double dt = secs - prevSecs;
        if (ImGui::BeginTable("coop_traffic", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Packet");
            ImGui::TableSetupColumn("In/s");
            ImGui::TableSetupColumn("Out/s");
            ImGui::TableSetupColumn("In total");
            ImGui::TableSetupColumn("Out total");
            ImGui::TableHeadersRow();
            std::set<std::string> types;
            for (const char* side : { "in", "out" }) {
                const nlohmann::json block = mStats.value(side, nlohmann::json::object()); // keep alive for items()
                for (auto& [k, v] : block.items())
                    types.insert(k);
            }
            for (auto& t : types) {
                auto rate = [&](const char* side) {
                    double now = mStats.value(side, nlohmann::json::object())
                                     .value(t, nlohmann::json::object())
                                     .value("bytes", 0.0);
                    double before = mPrevStats.value(side, nlohmann::json::object())
                                        .value(t, nlohmann::json::object())
                                        .value("bytes", 0.0);
                    return dt > 0 ? (now - before) / dt : 0.0;
                };
                auto total = [&](const char* side) {
                    return mStats.value(side, nlohmann::json::object())
                        .value(t, nlohmann::json::object())
                        .value("bytes", 0.0);
                };
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(t.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(HumanBytes(rate("in")).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(HumanBytes(rate("out")).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(HumanBytes(total("in")).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(HumanBytes(total("out")).c_str());
            }
            ImGui::EndTable();
        }
        auto dropped = mStats.value("dropped", nlohmann::json::object());
        if (!dropped.empty()) {
            ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "Dropped by SessionGuard:");
            for (auto& [k, v] : dropped.items())
                ImGui::BulletText("%s x%llu", k.c_str(), (unsigned long long)v.get<uint64_t>());
        }
    }

    void DrawSelf() {
        auto self = mStats.value("self", nlohmann::json::object());
        ImGui::Text("Loaded-save seed: %s", self.value("boundSeedId", std::string()).c_str());
        ImGui::Text("Staged seed:      %s", self.value("stagedSeedId", std::string()).c_str());
        ImGui::Text("World settings:   %s%s", self.value("worldCvarsHash", std::string()).c_str(),
                    self.value("overlay", false) ? "  (room overlay active)" : "");
    }

    void DrawToast(double now) {
        std::string state = mSeed.value("state", std::string());
        std::string msg = mSeed.value("message", std::string());
        if (state == "applied" && !msg.empty() && msg != mLastMessage) {
            mToastText = msg + "  [" + HashIcons(mSeed.value("fileHash", nlohmann::json::array())) + "]";
            mToastUntil = now + 6.0;
        }
        mLastMessage = msg;
        if (now > mToastUntil)
            return;
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + 40.0f), ImGuiCond_Always,
                                ImVec2(0.5f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.8f);
        ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
        if (ImGui::Begin("##coop_toast", nullptr, f))
            ImGui::TextUnformatted(mToastText.c_str());
        ImGui::End();
    }

    void DrawMismatchModal() {
        std::string state = mSeed.value("state", std::string());
        std::string seed = mSeed.value("roomSeedId", std::string());
        if (state != "mismatch") {
            mModalDismissed = false;
            return;
        }
        if (seed != mModalSeed) {
            mModalSeed = seed;
            mModalDismissed = false;
        }
        if (mModalDismissed)
            return;
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.4f),
                                ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGuiWindowFlags f =
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
        if (ImGui::Begin("Different seed than the room", nullptr, f)) {
            ImGui::TextWrapped("%s", mSeed.value("message", std::string()).c_str());
            ImGui::TextDisabled(
                "Item, flag and world sync with this room is paused; nothing is written into this save.");
            if (ImGui::Button("OK"))
                mModalDismissed = true;
        }
        ImGui::End();
    }
};

std::shared_ptr<ComboCoopWindow> sWindow;

} // namespace

namespace ComboRando {
const ComboCoopHooks* CoopHooks() {
    return sHooks;
}

// Registered from ComboUI_Register next to the room window.
void RegisterCoopWindow() {
    auto ctx = Ship::Context::GetRawInstance();
    if (!ctx || !ctx->GetWindow() || !ctx->GetWindow()->GetGui())
        return;
    if (!sWindow)
        sWindow = std::make_shared<ComboCoopWindow>(kDebugCvar, "Co-op Sync");
    ctx->GetWindow()->GetGui()->AddGuiWindow(sWindow);
}
} // namespace ComboRando

extern "C" COMBO_EXPORT void ComboUI_SetCoopHooks(const ComboCoopHooks* hooks) {
    sHooks = hooks;
}

extern "C" COMBO_EXPORT int ComboUI_CoopAutoAccept(void) {
    return CVarGetInteger(kAutoAcceptCvar, 1);
}
