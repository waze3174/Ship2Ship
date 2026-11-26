// combo/gui/ComboCrossGamePanels.h
//
// ComboShip: the pages of Both games > Cross-game. They hold the switches that work on both games while you play: not
// seed settings, so they are changed at any time and need no new seed.
#pragma once

#include <vector>

namespace ComboRando {

enum class CrossPage { ItemsAndForms, SharedHealthAndAmmo, Look };

// One checkbox bound to a CVar both games read while playing.
struct LiveSwitch {
    CrossPage page;
    const char* cvar;
    int defaultValue;
    const char* label;
    const char* hint;    // disabled text beside the checkbox; nullptr for none
    const char* tooltip; // shown on hover; nullptr for none
    void (*onChange)();  // extra work after a change; nullptr for none
};

// Every live switch in display order.
const std::vector<LiveSwitch>& LiveSwitches();

// Pages in display order, and the sidebar label of each.
const std::vector<CrossPage>& CrossPages();
const char* CrossPageLabel(CrossPage page);

// The hint sits beside the checkbox, or under it when `hintBeside` is false (the search's narrow columns).
void DrawLiveSwitch(const LiveSwitch& sw, bool hintBeside = true);
void DrawCrossGamePage(CrossPage page);

// One combo setting drives both games' D-pad equips (SoH and 2S2H keep their own CVars).
void ApplyDpadEquips();

} // namespace ComboRando
