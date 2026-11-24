#pragma once
// ComboShip co-op world-CVar registry (full-coop-sync plan task 1.2).
//
// WORLD = a non-randomizer CVar that changes what spawns, drops, how actors behave, damage, timers or
// puzzle state. Two players in one room must agree on every WORLD value or their worlds diverge (the
// "crate drops desync" when enemy randomizer / drop tweaks differ). These travel:
//   - baked into the consolidated seed at generation (`worldCvars`, part of seedId), and
//   - live from the room host (COMBO_WORLD_CVARS), applied on joiners as a session overlay that is never
//     persisted (CVarShadowPersist) and is restored when they leave.
// Everything not listed is LOCAL (cosmetics, audio, UI, controls, camera, local-only QoL, cheats).
// Randomizer generation options are NOT here: they already travel as oot/mm `settings` in the seed.
//
// Only keys in this registry are ever written from the network: a room host cannot set arbitrary CVars.
// The audit method and the full classification notes are in docs/deviations/anchor.md (A-M1 section).

#include <cstddef>

namespace Coop {

struct WorldCvar {
    const char* key;
    char type; // 'i' integer, 'f' float, 's' string
    const char* note;
};

// clang-format off
inline constexpr WorldCvar kWorldCvarsOot[] = {
#include "WorldCvarsOot.inl"
};

inline constexpr WorldCvar kWorldCvarsMm[] = {
#include "WorldCvarsMm.inl"
};
// clang-format on

inline constexpr size_t kWorldCvarsOotCount = sizeof(kWorldCvarsOot) / sizeof(kWorldCvarsOot[0]);
inline constexpr size_t kWorldCvarsMmCount = sizeof(kWorldCvarsMm) / sizeof(kWorldCvarsMm[0]);

} // namespace Coop
