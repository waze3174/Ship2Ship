// combo/rando/ComboLostWoodsLink.h
// ComboShip: the Lost Woods link, an optional walk between the two games. Header-only, compiled into soh.dll and
// 2ship.dll. See docs/deviations/rando.md "Lost Woods link".
//
// The route, both ways:
//   OoT Lost Woods, the north wrong turn in room 0 -> MM Lost Woods (spawn 0)
//   MM Lost Woods, east end -> the Clock Tower's twisting corridor, at its far end (not the sky drop)
//   Clock Tower, the corridor's far end -> OoT Lost Woods (the north tunnel mouth)
//   Clock Tower: the stone doors stay open. The Happy Mask Salesman and OoT's Mask Shop work as in vanilla, so the
//   corridor end is the only way between the games.
//
// It is a seed setting (Generate page, CVar kCVar), stored in the seed as kSeedKey. Logic models it: with the link on,
// OoT->MM needs the OoT Lost Woods hub (RR_THE_LOST_WOODS) instead of the Market's Mask Shop, and the Mask Shop is an
// ordinary shop. It is active only in a combo save where both games are played (a one-game seed and every vanilla slot
// store and play it off).
#pragma once

namespace ComboRando::LostWoodsLink {

inline constexpr const char* kCVar = "gCombo.LostWoodsLink"; // the Generate page's choice for the next seed
inline constexpr int kDefault = 1;                           // on unless the player turns it off

// Seed key (top-level, next to "games"). A seed made before logic knew the link has no key: it is played with the link
// on (the default when the link shipped), but its generation-time shaping (settings.cpp) stays as it was made.
inline constexpr const char* kSeedKey = "lostWoodsLink";
inline constexpr bool kPlayedWhenSeedLacksKey = true;
inline constexpr bool kModelledWhenSeedLacksKey = false;

// OoT scene SCENE_LOST_WOODS (spot10): the collision polygon of the north wrong turn in room 0. Its surface type is
// shared with a second polygon, so the polygon index is the key.
inline constexpr int kOotNorthWrongTurnPoly = 48;

// MM entrance for Z2_LOST_WOODS spawn 0: ENTRANCE(LOST_WOODS, 0), checked against the macro in the MM files.
inline constexpr int kMmLostWoodsSpawn0 = 0x62 << 9;

// OoT entrance 0x1B1, Lost Woods spawn 3 at the north tunnel mouth. Passed with kOotEntranceFinalBit so OoT keeps it.
inline constexpr int kOotLostWoodsFromTermina = 0x1B1;
inline constexpr int kOotEntranceFinalBit = 0x10000; // Combo_RequestCrossSwitch: arrive here as named, keep the age

} // namespace ComboRando::LostWoodsLink
