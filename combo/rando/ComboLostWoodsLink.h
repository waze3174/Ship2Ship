// combo/rando/ComboLostWoodsLink.h
// ComboShip: the Lost Woods link, an optional walk between the two games. Header-only, compiled into soh.dll and
// 2ship.dll. See docs/deviations/rando.md "Lost Woods link".
//
// The route, both ways:
//   OoT Lost Woods, the north wrong turn in room 0 -> MM Lost Woods (spawn 0)
//   MM Lost Woods, east end -> Before Clock Town (the Butler area, not the sky drop)
//   Before Clock Town, beside the studded door -> OoT Lost Woods (the north tunnel mouth)
//   Clock Tower: the stone doors stay open, and the Happy Mask Salesman leads to OoT's Mask Shop
//
// It is a play-time shortcut: the CVar is not stored in the seed and the fill does not model it, so reachability only
// grows. It is active only in a combo save where both games are played (Combo_GamesSolo() refuses a one-game seed
// and every vanilla slot).
#pragma once

namespace ComboRando::LostWoodsLink {

inline constexpr const char* kCVar = "gCombo.LostWoodsLink";
inline constexpr int kDefault = 1; // on unless the player turns it off

// OoT scene SCENE_LOST_WOODS (spot10): the collision polygon of the north wrong turn in room 0. Its surface type is
// shared with a second polygon, so the polygon index is the key.
inline constexpr int kOotNorthWrongTurnPoly = 48;

// MM entrance for Z2_LOST_WOODS spawn 0: ENTRANCE(LOST_WOODS, 0), checked against the macro in the MM files.
inline constexpr int kMmLostWoodsSpawn0 = 0x62 << 9;

// OoT entrance 0x1B1, Lost Woods spawn 3 at the north tunnel mouth. Passed with kOotEntranceFinalBit so OoT keeps it.
inline constexpr int kOotLostWoodsFromTermina = 0x1B1;
inline constexpr int kOotEntranceFinalBit = 0x10000; // Combo_RequestCrossSwitch: arrive here as named, keep the age

} // namespace ComboRando::LostWoodsLink
