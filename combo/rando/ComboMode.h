// combo/rando/ComboMode.h
// ComboShip: the mode of one save slot, stored in the slot's .combosav container as combo.mode:
//   { "game": "ootmm"|"oot"|"mm", "kind": "rando"|"vanilla", "quest": "normal"|"mq" }
// game is the game(s) the slot plays, kind is whether a randomizer seed is bound to it, and quest is the OoT quest
// of a vanilla OoT slot. An absent key (every save before this setting) is a combo randomizer slot.
// Header-only, compiled into the exe and soh.dll. See docs/deviations/rando.md "Save modes: vanilla OoT".
#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "ComboGames.h"

namespace ComboRando {

enum ComboKind {
    COMBO_KIND_RANDO = 0,
    COMBO_KIND_VANILLA = 1,
};

enum OotQuest {
    OOT_QUEST_NORMAL = 0,
    OOT_QUEST_MQ = 1,
};

// SoH's QUEST_NORMAL / QUEST_MASTER / QUEST_RANDOMIZER (soh/include/z64save.h), as SOH_GetNewSaveQuest reports them;
// OTRGlobals.cpp static_asserts the values.
inline constexpr int kSohQuestNormal = 0;
inline constexpr int kSohQuestMaster = 1;
inline constexpr int kSohQuestRandomizer = 2;

inline constexpr const char* kComboModeKey = "mode";

inline const char* ComboKindKey(ComboKind k) {
    return k == COMBO_KIND_VANILLA ? "vanilla" : "rando";
}
inline const char* OotQuestKey(OotQuest q) {
    return q == OOT_QUEST_MQ ? "mq" : "normal";
}

struct ComboMode {
    ComboGames game = COMBO_GAMES_BOTH;
    ComboKind kind = COMBO_KIND_RANDO;
    OotQuest quest = OOT_QUEST_NORMAL;

    bool IsVanilla() const {
        return kind == COMBO_KIND_VANILLA;
    }
    bool IsVanillaOot() const {
        return IsVanilla() && game == COMBO_GAMES_OOT;
    }

    static ComboMode Rando(ComboGames game) {
        return { game, COMBO_KIND_RANDO, OOT_QUEST_NORMAL };
    }
    static ComboMode VanillaOot(OotQuest quest) {
        return { COMBO_GAMES_OOT, COMBO_KIND_VANILLA, quest };
    }

    nlohmann::json ToJson() const {
        return { { "game", ComboGamesKey(game) }, { "kind", ComboKindKey(kind) }, { "quest", OotQuestKey(quest) } };
    }

    // Anything but a well-formed object is a combo randomizer slot. A vanilla slot needs one game, so a vanilla
    // "ootmm" is read as a randomizer slot rather than as a game with no save to play.
    static ComboMode FromJson(const nlohmann::json& j) {
        ComboMode m;
        if (!j.is_object())
            return m;
        m.game = ComboGamesFromKey(j.value("game", std::string("ootmm")));
        m.quest = j.value("quest", std::string("normal")) == "mq" ? OOT_QUEST_MQ : OOT_QUEST_NORMAL;
        if (j.value("kind", std::string("rando")) == "vanilla" && ComboGamesSolo(m.game))
            m.kind = COMBO_KIND_VANILLA;
        return m;
    }
};

} // namespace ComboRando
