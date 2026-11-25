// combo/discord/DiscordGameState.h
//
// ComboShip: what one game tells the launcher for Discord Rich Presence. Plain C, so it crosses the DLL boundary
// as-is: soh.dll's SOH_DiscordGetState and 2ship.dll's MM_DiscordGetState fill it, the launcher reads it.
// Included by soh, mm and the launcher. See docs/deviations/discord.md.
#ifndef COMBO_DISCORD_GAME_STATE_H
#define COMBO_DISCORD_GAME_STATE_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COMBO_DISCORD_SCENE_NAME_MAX 64
#define COMBO_DISCORD_UNAVAILABLE (-1)
// Health is counted in 1/16ths of a heart in both games.
#define COMBO_DISCORD_HEALTH_PER_HEART 0x10

// Form ids are soh's ComboForm: 0 human, 1 Goron, 2 Zora, 3 Deku, 4 Fierce Deity.
typedef struct ComboDiscordGameState {
    // Live fields. Only filled when the game is the one being played; its world is not valid while it is dormant.
    int32_t inPlay; // 1 while a save is being played, 0 at the title, file select or credits
    int32_t adult;  // 1 adult, 0 child
    int32_t form;   // the form ids above
    int32_t maxHearts;
    int32_t day;         // Majora's Mask: 1 to 3, 0 when it is not a day of the cycle (always 0 in Ocarina of Time)
    int32_t minuteOfDay; // Majora's Mask: minutes since midnight; COMBO_DISCORD_UNAVAILABLE in Ocarina of Time
    char sceneName[COMBO_DISCORD_SCENE_NAME_MAX]; // UTF-8, empty when the scene is unknown

    // Save fields. Read from the loaded save, so they are valid for a dormant game whose save is resident.
    int32_t checksGotten; // randomizer saves; COMBO_DISCORD_UNAVAILABLE otherwise
    int32_t checksTotal;
    int64_t playSeconds;
} ComboDiscordGameState;

static inline void ComboDiscord_InitState(ComboDiscordGameState* state) {
    memset(state, 0, sizeof(*state));
    state->minuteOfDay = COMBO_DISCORD_UNAVAILABLE;
    state->checksGotten = COMBO_DISCORD_UNAVAILABLE;
    state->checksTotal = COMBO_DISCORD_UNAVAILABLE;
}

// The export both games provide (SOH_DiscordGetState, MM_DiscordGetState): fills `out` and returns 1, or returns 0 and
// leaves it untouched. `live` is 1 for the game being played and 0 for the dormant one, which gets the save fields
// only.
typedef int (*ComboDiscordGetStateFn)(ComboDiscordGameState* out, int live);

#ifdef __cplusplus
}
#endif

#endif
