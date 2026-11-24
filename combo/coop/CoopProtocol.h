#pragma once
// ComboShip co-op protocol (full-coop-sync plan §3, task 0.3). Header-only, shared by the launcher
// (combo/ComboShip.cpp, SeedShare) and both game DLLs. Every combo-private packet type is a
// `COMBO_` string; the public Anchor relay forwards unknown types unchanged (Findings log, 0.1).
//
// Peer identity rides inside the existing UPDATE_CLIENT_STATE as `state.combo` (the packet's own header
// comment requires `contains` checks for added fields, so old clients ignore it). The launcher stamps
// it on the way out (ComboAnchor::Send), because the launcher is the one place that knows the bound
// seed for both games and sees every outbound client state from either DLL.

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include "CoopHash.h"

namespace Coop {

// Bump on any wire-incompatible change to a COMBO_* packet or to state.combo semantics.
constexpr int kCoopProtocol = 1;

// ── packet types (§3 table) ──
inline constexpr const char* kPktSeedOffer = "COMBO_SEED_OFFER";
inline constexpr const char* kPktSeedRequest = "COMBO_SEED_REQUEST";
inline constexpr const char* kPktSeedChunk = "COMBO_SEED_CHUNK";
inline constexpr const char* kPktWorldCvars = "COMBO_WORLD_CVARS";
inline constexpr const char* kPktSyncConfig = "COMBO_SYNC_CONFIG";
inline constexpr const char* kPktActorSnap = "COMBO_ACTOR_SNAP";
inline constexpr const char* kPktActorEvent = "COMBO_ACTOR_EVENT";
inline constexpr const char* kPktActorPlayerHit = "COMBO_ACTOR_PLAYERHIT"; // owner -> the player its enemy hit
inline constexpr const char* kPktRoomRequest = "COMBO_ROOM_REQUEST";
inline constexpr const char* kPktRoomState = "COMBO_ROOM_STATE";

// Launcher-owned packets: handled by SeedShare in ComboShip.exe and never forwarded to the game DLLs.
inline bool IsLauncherOwnedPacket(const std::string& type) {
    return type == kPktSeedOffer || type == kPktSeedRequest || type == kPktSeedChunk || type == kPktWorldCvars ||
           type == kPktSyncConfig;
}

// MM-side time bucket values are halfDay indices (0..5, day 1 day … day 3 night); OOT uses 0 = day, 1 = night.
// -1 = unknown / not in game. Actor sharing pairs only peers with an equal bucket (§11 Option A).
struct PeerMeta {
    bool present = false; // false = peer sent no state.combo (old build): legacy rules apply (§1.8)
    int protocol = 0;
    std::string buildHash;
    uint64_t layoutHashOot = 0; // Phase 3 kActorSyncLayoutHash per game; 0 = actor sync unavailable
    uint64_t layoutHashMm = 0;
    uint64_t seedId = 0;        // seed bound to the LOADED save; 0 = no save loaded
    uint64_t pendingSeedId = 0; // seed staged at file select (what "Start" would bind); 0 = none
    uint64_t settingsHash = 0;
    uint64_t worldCvarsHash = 0;
    int game = 0; // 0 = OOT, 1 = MM
    int sceneId = -1;
    int roomNum = -1;
    int timeBucket = -1;
    uint64_t desyncHash = 0;

    uint64_t LayoutHash(int g) const {
        return g == 1 ? layoutHashMm : layoutHashOot;
    }
};

inline nlohmann::json MetaToJson(const PeerMeta& m) {
    nlohmann::json j;
    j["protocol"] = m.protocol;
    j["buildHash"] = m.buildHash;
    j["layoutHash"] = { { "oot", HexU64(m.layoutHashOot) }, { "mm", HexU64(m.layoutHashMm) } };
    j["seedId"] = HexU64(m.seedId);
    j["pendingSeedId"] = HexU64(m.pendingSeedId);
    j["settingsHash"] = HexU64(m.settingsHash);
    j["worldCvarsHash"] = HexU64(m.worldCvarsHash);
    j["game"] = m.game == 1 ? "mm" : "oot";
    j["sceneId"] = m.sceneId;
    j["roomNum"] = m.roomNum;
    j["timeBucket"] = m.timeBucket;
    j["desyncHash"] = HexU64(m.desyncHash);
    return j;
}

// Never throws: malformed fields read as their defaults.
inline PeerMeta MetaFromJson(const nlohmann::json& j) {
    PeerMeta m;
    if (!j.is_object())
        return m;
    m.present = true;
    try {
        m.protocol = j.value("protocol", 0);
        m.buildHash = j.value("buildHash", std::string());
        if (auto it = j.find("layoutHash"); it != j.end() && it->is_object()) {
            m.layoutHashOot = JsonU64(*it, "oot");
            m.layoutHashMm = JsonU64(*it, "mm");
        }
        m.seedId = JsonU64(j, "seedId");
        m.pendingSeedId = JsonU64(j, "pendingSeedId");
        m.settingsHash = JsonU64(j, "settingsHash");
        m.worldCvarsHash = JsonU64(j, "worldCvarsHash");
        m.game = j.value("game", std::string("oot")) == "mm" ? 1 : 0;
        m.sceneId = j.value("sceneId", -1);
        m.roomNum = j.value("roomNum", -1);
        m.timeBucket = j.value("timeBucket", -1);
        m.desyncHash = JsonU64(j, "desyncHash");
    } catch (...) {}
    return m;
}

} // namespace Coop
