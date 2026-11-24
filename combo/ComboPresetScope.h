// combo/ComboPresetScope.h
//
// ComboShip: which CVars a preset or a ComboShip settings import may carry. CVar names are
// "<root>.<path>"; the two games and the combo layer share one CVar store, so the roots below are the
// union of what soh, 2ship and combo read (found by scanning each tree's CVar literals and prefix macros).
//
//   preset keys        play preferences: combo options, rando options, enhancements, cosmetics,
//                      cheats, audio, camera/ocarina/D-pad preferences, trackers, Anchor settings.
//   mods + controls    the mod lists and controller input. Never in a preset (they are per install and
//                      per pad); a settings import may bring them along when the player ticks the box.
//   everything else    never carried: window and graphics state (resolution, vsync, MSAA, ...), LED
//                      hardware, debug and developer state, per-run state (seed, save context) and
//                      the transient markers listed in kDeniedPrefixes / kDeniedFragments.
//
// Header-only and std only, so comboui and the launcher share it.
#pragma once

#include <cstddef>
#include <string>

namespace ComboPresetScope {

template <size_t N> bool StartsWithAny(const std::string& key, const char* const (&prefixes)[N]) {
    for (const char* prefix : prefixes) {
        if (key.rfind(prefix, 0) == 0) {
            return true;
        }
    }
    return false;
}

template <size_t N> bool ContainsAny(const std::string& key, const char* const (&fragments)[N]) {
    for (const char* fragment : fragments) {
        if (key.find(fragment) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// Roots whose every key is a preference. gSettings and gRemote are mixed bags, so they are listed
// key group by key group instead.
constexpr const char* kAllowedPrefixes[] = {
    // ComboShip's own options
    "gCombo.",
    // SoH randomizer (gRandoSettings, gRandoEnhancements) and 2S2H randomizer (gRando)
    "gRandoSettings.",
    "gRandoEnhancements.",
    "gRando.",
    // Both games' enhancements, cosmetics (SoH "gCosmetics", 2S2H "gCosmetic"), cheats and audio editor
    "gEnhancements.",
    "gCosmetics.",
    "gCosmetic.",
    "gCheats.",
    "gAudioEditor.",
    "gAddTraps.",
    // 2S2H preference roots
    "gModes.",
    "gFixes.",
    "gNotifications.",
    "gHudEditor.",
    "gDisplayOverlay.",
    // trackers and overlays
    "gTrackers.",
    "gGameplayStats.",
    "gTimeDisplay.",
    // gSettings: camera, ocarina, D-pad, audio levels, accessibility, language, menu theme
    "gSettings.Volume.",
    "gSettings.Audio",
    "gSettings.FreeLook.",
    "gSettings.FirstPersonCameraSensitivity.",
    "gSettings.MoveInFirstPerson",
    "gSettings.DisableFirstPersonAutoCenterView",
    "gSettings.CustomOcarina.",
    "gSettings.OcarinaControl",
    "gSettings.Controls.",
    "gSettings.WalkModifier.",
    "gSettings.Dpad",
    "gSettings.DPad",
    "gSettings.ZTargetSetting",
    "gSettings.Notifications.",
    "gSettings.ItemTracker.",
    "gSettings.TimeSplits.",
    "gSettings.A11y",
    "gSettings.Language",
    "gSettings.Menu.Theme",
    // Anchor co-op: connection, identity and room preferences (not Enabled or the last client id)
    "gRemote.Anchor.Host",
    "gRemote.Anchor.Port",
    "gRemote.Anchor.Name",
    "gRemote.Anchor.RoomId",
    "gRemote.Anchor.TeamId",
    "gRemote.Anchor.Color.",
    "gRemote.Anchor.PartnerFeed.",
    "gRemote.Anchor.RoomSettings.",
    "gRemote.Anchor.ShowOtherPlayersOnMinimap",
};

// Inside gCombo: one-shot "seeded" markers, window-open state and debug switches. Carrying them would
// re-trigger first-run seeding or reopen windows.
constexpr const char* kDeniedPrefixes[] = {
    "gCombo.Debug.",            // debug switches
    "gCombo.Anchor.",           // room window open state, toast timing
    "gCombo.BottleShelf.",      // window open state
    "gCombo.Rando.GenRollSeed", // per-run seed roll
    "gCombo.DpadEquips.Init",   // one-shot init markers
    "gCombo.DpadEquipsInit",
};

constexpr const char* kDeniedFragments[] = {
    "Seeded",
    "MmPlaced",
    "NotesWindow",
};

// Options the randomizer reads when a seed is generated or a save is bound. Writing them while a
// save is loaded would not change that save, and the launcher's reload path snapshots and restores
// these CVars around a seed, so they are only applied at the file-select screen.
constexpr const char* kSeedBoundPrefixes[] = {
    "gRandoSettings.",
    "gRando.",
    "gCombo.Rando.",
};

constexpr const char* kModListPrefixes[] = {
    "gSettings.EnabledMods", // also matches gSettings.EnabledModsMM
};

constexpr const char* kControlPrefixes[] = {
    "gSettings.Controllers.", // pad bindings, HasConfig per port
    "gSettings.ControlNav",
    "gSettings.EnableMouse",
    "gSettings.CursorVisibility",
    "gSettings.AutoCaptureMouse",
    "gSettings.DisableThirdPersonMouse",
    "gSettings.LED",
};

inline bool IsPresetKey(const std::string& key) {
    return StartsWithAny(key, kAllowedPrefixes) && !StartsWithAny(key, kDeniedPrefixes) &&
           !ContainsAny(key, kDeniedFragments);
}

inline bool IsModListKey(const std::string& key) {
    return StartsWithAny(key, kModListPrefixes);
}

inline bool IsControllerBindingKey(const std::string& key) {
    return key.rfind("gSettings.Controllers.", 0) == 0;
}

inline bool IsModsOrControlsKey(const std::string& key) {
    return IsModListKey(key) || StartsWithAny(key, kControlPrefixes);
}

inline bool IsSeedBoundKey(const std::string& key) {
    return StartsWithAny(key, kSeedBoundPrefixes);
}

// What a ComboShip settings import carries: everything a preset can, plus mods and controls on request.
inline bool IsImportKey(const std::string& key, bool includeModsAndControls) {
    return IsPresetKey(key) || (includeModsAndControls && IsModsOrControlsKey(key));
}

} // namespace ComboPresetScope
