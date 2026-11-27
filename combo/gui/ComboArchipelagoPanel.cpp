// combo/gui/ComboArchipelagoPanel.cpp — see ComboArchipelagoPanel.h
#include "ComboArchipelagoPanel.h"

#include <cstdio>
#include <cstring>
#include <string>

#include <imgui.h>

#include "ComboApMenuApi.h"
#include "ComboExport.h"
#include "ComboWidgetStyle.h"

namespace ComboRando {

namespace {

const ComboApMenuApi* sApi = nullptr;

constexpr size_t kServerChars = COMBO_AP_TEXT_MAX;
constexpr size_t kSlotChars = 128;
constexpr size_t kPasswordChars = 128;
constexpr const char* kServerHint = "localhost:38281";

// What the player is typing. The password lives only here, and in the launcher's memory once connected.
char sServer[kServerChars] = "";
char sSlot[kSlotChars] = "";
char sPassword[kPasswordChars] = "";
std::string sEditedFile; // which file the fields were filled from

void Fill(char* into, size_t size, const char* text) {
    std::snprintf(into, size, "%s", text);
}

// The fields show the loaded file's saved connection; loading another file starts them over.
void FollowLoadedFile(const ComboApMenuState& state) {
    const std::string file = state.fileSlot;
    if (file == sEditedFile) {
        return;
    }
    sEditedFile = file;
    Fill(sServer, sizeof(sServer), state.server);
    Fill(sSlot, sizeof(sSlot), state.slot[0] != '\0' ? state.slot : state.fileSlot);
    sPassword[0] = '\0';
}

ImVec4 PhaseColor(int phase) {
    switch (phase) {
        case COMBO_AP_PHASE_CONNECTED:
            return ImVec4(0.4f, 0.9f, 0.4f, 1.0f);
        case COMBO_AP_PHASE_REFUSED:
        case COMBO_AP_PHASE_WRONG_ROOM:
            return ImVec4(0.95f, 0.4f, 0.4f, 1.0f);
        case COMBO_AP_PHASE_CONNECTING:
            return ImVec4(0.95f, 0.85f, 0.4f, 1.0f);
        default:
            break;
    }
    return ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
}

void DrawStatus(const ComboApMenuState& state) {
    const int phase = state.phase;
    ImGui::SeparatorText("Status");
    if (phase == COMBO_AP_PHASE_OFF) {
        ImGui::TextDisabled("Not connected");
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, PhaseColor(phase));
    ImGui::TextWrapped("%s", state.status);
    ImGui::PopStyleColor();
    if (phase == COMBO_AP_PHASE_CONNECTED) {
        ImGui::Text("Items received from the server: %d", state.receivedIndex);
        if (state.itemsWaiting > 0) {
            ImGui::TextDisabled("%d more are waiting for the game to take them.", state.itemsWaiting);
        }
        ImGui::Text("Checks the server holds for this slot: %d", state.checksKnown);
        if (state.checksUnmapped > 0) {
            ImGui::TextDisabled("%d opened checks hold pre-placed items and are not Archipelago locations.",
                                state.checksUnmapped);
        }
    }
}

} // namespace

bool ArchipelagoFileLoaded() {
    if (sApi == nullptr || sApi->getState == nullptr) {
        return false;
    }
    ComboApMenuState state{};
    sApi->getState(&state);
    return state.fileLoaded != 0;
}

void DrawArchipelagoSharedPanel() {
    if (sApi == nullptr || sApi->getState == nullptr) {
        ImGui::TextUnformatted("Archipelago is not available yet (the launcher is still starting).");
        return;
    }
    ComboApMenuState state{};
    sApi->getState(&state);

    ImGui::TextWrapped("Play a Ship2Ship Archipelago file online: the checks you open are sent to the server and the "
                       "items other players find for you arrive in game.");
    if (state.fileLoaded == 0) {
        ImGui::Spacing();
        ImGui::TextDisabled("No Archipelago file is loaded. Load an Archipelago seed file (Randomizer > Combo > "
                            "Generate > Load seed file...), start a file from it, then connect here.");
        return;
    }
    FollowLoadedFile(state);

    const ImVec4 theme = ComboMenu_ThemeColor();
    const bool active = state.phase != COMBO_AP_PHASE_OFF;

    ImGui::SeparatorText("Connection");
    ImGui::BeginDisabled(active);
    ComboMenu_PushInput(theme);
    ImGui::Text("Server");
    ImGui::InputTextWithHint("##ApServer", kServerHint, sServer, sizeof(sServer));
    ImGui::Text("Slot name");
    ImGui::InputText("##ApSlot", sSlot, sizeof(sSlot));
    ImGui::Text("Password (kept in memory only, never saved)");
    ImGui::InputText("##ApPassword", sPassword, sizeof(sPassword), ImGuiInputTextFlags_Password);
    ComboMenu_PopInput();
    ImGui::EndDisabled();
    ImGui::TextDisabled("This file is slot \"%s\". %s", state.fileSlot,
                        state.tlsAvailable != 0
                            ? "A bare host:port tries wss:// first, then ws://."
                            : "This build connects with ws:// only; archipelago.gg rooms need wss://.");

    ImGui::Spacing();
    const bool canConnect = sServer[0] != '\0' && sSlot[0] != '\0';
    ImGui::BeginDisabled(!active && !canConnect);
    const ImVec4 red = { 0.66f, 0.13f, 0.13f, 1.0f };
    const ImVec4 green = { 0.15f, 0.55f, 0.20f, 1.0f };
    ComboMenu_PushButton(active ? red : green);
    if (ImGui::Button(active ? "Disconnect" : "Connect", ImVec2(-1.0f, 0.0f))) {
        if (active) {
            sApi->disconnect();
        } else {
            sApi->connect(sServer, sSlot, sPassword);
        }
    }
    ComboMenu_PopButton();
    ImGui::EndDisabled();

    DrawStatus(state);
}

} // namespace ComboRando

// The launcher hands comboui its client's menu table at startup, right before ComboUI_Register.
extern "C" COMBO_EXPORT void ComboUI_SetArchipelagoApi(const ComboApMenuApi* api) {
    ComboRando::sApi = api;
}
