// combo/rando/ComboAttempt.h
// ComboShip: the data the headless tool's per-attempt setup takes and produces. The setup runs in front of every
// fill attempt and in front of an Archipelago oracle session: it pushes the seed's settings into both games, dumps
// their pools and shuffles the entrances. comborando's generate mode and its --oracle mode share it.
#pragma once

#include <string>
#include <nlohmann/json.hpp>

#include "ComboGames.h"
#include "CrossEntrance.h"
#include "CrossWorldSession.h"

namespace ComboRando {

// What the setup is told. These are the seed's own choices, never read from the menu CVars.
struct AttemptConfig {
    CwGoal goal;
    ComboGames games = COMBO_GAMES_BOTH;
    uint32_t sharedMask = 0;
    bool unified = false;
    bool formLogicCredit = false;
    bool lostWoodsLink = false;
    EntranceSettings entMm;
    CrossSettings entCross;
    // A stored seed's Master Quest rolls (the seed object carrying kMasterQuestKey). Null lets the seed roll its own.
    nlohmann::json storedMasterQuest;
};

// What the setup decided: both games' dumps, the forced OOT placements and the entrance layouts.
struct AttemptSetup {
    std::string sohDump, mmDump, forced;
    MmEntranceAttempt entResult;
    CrossAttempt crossResult;
};

enum class AttemptStatus {
    Ready,
    Fatal,                 // the seed's own settings cannot work (a hunt needing more pieces than the pool holds)
    OotShuffleFailed,      // OOT's entrance shuffle found no valid layout
    EntranceShuffleFailed, // MM's or the cross-game entrance shuffle failed
};

} // namespace ComboRando
