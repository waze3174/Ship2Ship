// combo/rando/ComboMode.h
// ComboShip: the mode of one save slot, stored in the slot's .combosav container as combo.mode:
//   { "game": "ootmm"|"oot"|"mm", "kind": "rando"|"vanilla", "quest": "normal"|"mq", "sequential": true }
// game is the game(s) the slot plays, kind is whether a randomizer seed is bound to it, and quest is the OoT quest
// of a vanilla OoT slot or of the Ocarina of Time half of a back-to-back slot. An absent key (every save before this
// setting) is a combo randomizer slot. A vanilla Majora's Mask slot is { "game": "mm", "kind": "vanilla" }. A
// back-to-back slot (vanilla Ocarina of Time, then vanilla Majora's Mask, in one slot) is
// { "game": "ootmm", "kind": "vanilla", "sequential": true }; "sequential" is written only there, and a back-to-back
// slot without a "quest" key (made before the Master Quest choice) is a Normal Quest one.
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

// SoH's QUEST_NORMAL / QUEST_MASTER / QUEST_RANDOMIZER / QUEST_COMBO_MM / QUEST_COMBO_BACK2BACK
// (soh/include/z64save.h), as SOH_GetNewSaveQuest reports them; OTRGlobals.cpp static_asserts the values.
// QUEST_COMBO_MM and QUEST_COMBO_BACK2BACK are the file select's fourth and fifth choices and never a quest an OoT
// save holds.
inline constexpr int kSohQuestNormal = 0;
inline constexpr int kSohQuestMaster = 1;
inline constexpr int kSohQuestRandomizer = 2;
inline constexpr int kSohQuestComboMm = 6;
inline constexpr int kSohQuestComboBackToBack = 7;

inline constexpr const char* kComboModeKey = "mode";

inline constexpr const char* kSequentialKey = "sequential";

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
    bool sequential = false; // both games, vanilla, one after the other (see the header comment)

    bool IsVanilla() const {
        return kind == COMBO_KIND_VANILLA;
    }
    bool IsVanillaOot() const {
        return IsVanilla() && game == COMBO_GAMES_OOT;
    }
    bool IsVanillaMm() const {
        return IsVanilla() && game == COMBO_GAMES_MM;
    }
    bool IsBackToBack() const {
        return sequential;
    }
    // The slot's Majora's Mask save is a vanilla one: a vanilla Majora's Mask slot, or the second half of a
    // back-to-back slot.
    bool HasVanillaMmSave() const {
        return IsVanillaMm() || IsBackToBack();
    }

    static ComboMode Rando(ComboGames game) {
        return { game, COMBO_KIND_RANDO, OOT_QUEST_NORMAL, false };
    }
    static ComboMode VanillaOot(OotQuest quest) {
        return { COMBO_GAMES_OOT, COMBO_KIND_VANILLA, quest, false };
    }
    static ComboMode VanillaMm() {
        return { COMBO_GAMES_MM, COMBO_KIND_VANILLA, OOT_QUEST_NORMAL, false };
    }
    static ComboMode BackToBack(OotQuest quest) {
        return { COMBO_GAMES_BOTH, COMBO_KIND_VANILLA, quest, true };
    }

    nlohmann::json ToJson() const {
        nlohmann::json j = { { "game", ComboGamesKey(game) },
                             { "kind", ComboKindKey(kind) },
                             { "quest", OotQuestKey(quest) } };
        if (sequential)
            j[kSequentialKey] = true;
        return j;
    }

    // Anything but a well-formed object is a combo randomizer slot. A vanilla slot needs one game, so a vanilla
    // "ootmm" is read as a randomizer slot rather than as a game with no save to play, unless it is marked sequential.
    static ComboMode FromJson(const nlohmann::json& j) {
        ComboMode m;
        if (!j.is_object())
            return m;
        m.game = ComboGamesFromKey(j.value("game", std::string("ootmm")));
        m.quest = j.value("quest", std::string("normal")) == "mq" ? OOT_QUEST_MQ : OOT_QUEST_NORMAL;
        if (j.value("kind", std::string("rando")) != "vanilla")
            return m;
        if (ComboGamesSolo(m.game)) {
            m.kind = COMBO_KIND_VANILLA;
        } else if (j.value(kSequentialKey, false)) {
            m.kind = COMBO_KIND_VANILLA;
            m.sequential = true;
        }
        return m;
    }
};

// The label the OoT file select draws on a slot, in its small quest label box (uppercase letters, digits and spaces
// only, at most kSlotLabelMaxLen of them). nullptr: no label, a plain Normal Quest slot.
//   COMBO: Combo Randomizer   OOTR: OoT Randomizer   MMR: MM Randomizer
//   MQ: OoT Master Quest      MM: Majora's Mask (vanilla)
//   S2S: both games back to back (vanilla)      S2S MQ: the same, with Ocarina of Time's Master Quest
inline constexpr int kSlotLabelMaxLen = 8;
inline const char* ComboModeLabel(const ComboMode& m) {
    if (m.IsBackToBack())
        return m.quest == OOT_QUEST_MQ ? "S2S MQ" : "S2S";
    if (m.IsVanillaMm())
        return "MM";
    if (m.IsVanillaOot())
        return m.quest == OOT_QUEST_MQ ? "MQ" : nullptr;
    switch (m.game) {
        case COMBO_GAMES_OOT:
            return "OOTR";
        case COMBO_GAMES_MM:
            return "MMR";
        default:
            return "COMBO";
    }
}

} // namespace ComboRando
