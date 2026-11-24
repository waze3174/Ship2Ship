#pragma once
// ComboShip co-op combat: enemy/boss sync + HP scaling (OOT). See CoopEnemySync.cpp.
#ifdef COMBO_BUILD
#include <nlohmann/json.hpp>

void CoopEnemySync_Init();                                   // once; called from Anchor::RegisterHooks
void CoopEnemySync_OnPacket(const nlohmann::json& payload); // COMBO_ACTOR_* (game thread)
bool CoopEnemySync_IsRemoteKill(); // true inside Actor_Kill when a teammate's copy died (no local rewards)
// Co-op (PvP off / same team): the puppet takes ENEMY attacks only, and only to relay them to its player
// (enemy attacks hit players on the enemy's owner). Called from DummyPlayer_Update every frame.
void CoopEnemySync_PuppetCoop(struct Player* puppet, uint32_t clientId);
#endif
