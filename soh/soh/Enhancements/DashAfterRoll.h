#pragma once

#include "soh/Enhancements/enhancementTypes.h"
#include "soh/cvar_prefixes.h"

typedef struct Player Player;

// Same CVar string as 2S2H's, so both games share one setting.
#define CVAR_DASH_AFTER_ROLL_NAME CVAR_ENHANCEMENT("Player.DashAfterRoll")

// Extra run speed multiplier while dashing after a roll, 1.0f when not dashing.
float Ship_GetDashAfterRollRunFactor(Player* player);
