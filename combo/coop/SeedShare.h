#pragma once
// ComboShip co-op SeedShare (plan tasks 1.6/1.7): the room host's seed travels to every joiner over
// Anchor, so "same seed + same world settings" no longer depends on passing spoiler files around.
//
// Launcher-owned (compiled into ComboShip). Threading (plan §0.8): OnNetworkPacket runs on the
// ComboAnchor receive thread and only enqueues; Pump runs on the game (main) thread, driven by the
// launcher's per-frame PumpDormant seam, and is the only place state changes or callbacks fire.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "CoopProtocol.h"

namespace SeedShare {

struct Peer {
    uint32_t clientId = 0;
    std::string name;
    std::string teamId;
    bool online = false;
    Coop::PeerMeta meta;
};

struct RosterView {
    uint32_t ownClientId = 0;
    uint32_t ownerClientId = 0;
    std::string ownTeam = "default";
    std::vector<Peer> peers; // excludes self

    // The room's effective host: the room owner while online, else the lowest online clientId on our
    // team (self included). Every client computes the same answer from the same roster, so a room whose
    // creator left still has exactly one host. (The relay keeps a departed owner's id forever.)
    uint32_t EffectiveHost() const {
        bool ownerOnline = ownerClientId != 0 && ownerClientId == ownClientId;
        uint32_t lowest = ownClientId;
        for (const auto& p : peers) {
            if (!p.online)
                continue;
            if (p.clientId == ownerClientId)
                ownerOnline = true;
            if (p.teamId == ownTeam && p.clientId < lowest)
                lowest = p.clientId;
        }
        return ownerOnline ? ownerClientId : lowest;
    }
};

// Everything SeedShare needs from the launcher. All callbacks run on the main thread (from Pump).
struct HostApi {
    std::function<void(const std::string& json)> send; // ComboAnchor::Send (stamps nothing itself)
    std::function<bool()> connected;
    std::function<RosterView()> roster;
    // The seed this client would share as host: the loaded slot's bound seed, else the staged seed at
    // file select. 0 / "" when there is none.
    std::function<uint64_t()> hostSeedId;
    std::function<std::string()> hostSeedJson; // compact consolidated JSON for hostSeedId()
    std::function<uint64_t()> activeSeedId;    // seed of the LOADED save (0 = no save loaded)
    std::function<uint64_t()> stagedSeedId;    // seed staged at file select (0 = none)
    // Persist a received, verified seed under the normal spoiler name; returns the path ("" on failure).
    std::function<std::filesystem::path(const nlohmann::json& seed)> writeSeedFile;
    // Index of local seed files by seedId (lets a joiner skip the transfer). May be slow: called on offer.
    std::function<std::filesystem::path(uint64_t seedId)> findLocalSeed;
    // Apply the seed as the session's room seed (Combo_ApplySeedJson RoomShare). 1 = ok, 2 = busy (retry).
    std::function<int(const nlohmann::json& seed)> applyRoomSeed;
    std::function<bool()> autoAccept; // local setting: auto-apply a received room seed at file select
};

void SetHostApi(HostApi api);

// Receive thread: hand every COMBO_SEED_* packet here (already parsed). Never blocks on game state.
void OnNetworkPacket(const nlohmann::json& packet);

// Main thread, once per frame.
void Pump();

// UI actions (main thread).
void RequestShare();   // host: re-offer now
void UseRoomSeed();    // joiner: apply the downloaded room seed
void OnDisconnected(); // clears room-seed state; the launcher restores the world-CVar overlay itself

enum class JoinerState {
    NoRoomSeed,   // not connected, or the host has nothing to share
    IsHost,       // we are the room owner
    Downloading,  // REQUEST sent, chunks arriving
    Ready,        // verified + written, not applied (auto-accept off, or a save is loaded)
    Applied,      // staged at file select / matches the loaded save
    SaveMismatch, // a save on a different seed is loaded: SessionGuard blocks sync, UI shows a modal
    Failed        // transfer or verification failed (see status text)
};

struct Status {
    JoinerState state = JoinerState::NoRoomSeed;
    uint64_t roomSeedId = 0;
    uint32_t displaySeed = 0;
    nlohmann::json fileHash = nlohmann::json::array();
    std::string hostName;
    int received = 0, total = 0;
    std::string message;
};

Status GetStatus();
nlohmann::json StatusJson(); // for comboui (room window / modal)

} // namespace SeedShare
