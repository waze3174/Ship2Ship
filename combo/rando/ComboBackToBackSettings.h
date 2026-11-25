// combo/rando/ComboBackToBackSettings.h
// ComboShip: the CVars of the back-to-back mode's settings page (Settings > Back to back). Each one is read once, when
// a back-to-back file is created, and baked into that file; changing it later never touches an existing file.
// Header-only and std only, so comboui, soh.dll and the launcher share it. See docs/deviations/back2back.md.
#pragma once

namespace ComboRando {

// 1: a new back-to-back file plays Ocarina of Time's Master Quest, when oot-mq.o2r is loaded. 0 or unset: Normal Quest.
inline constexpr const char* kBackToBackMasterQuestCVar = "gCombo.BackToBack.MasterQuest";

} // namespace ComboRando
