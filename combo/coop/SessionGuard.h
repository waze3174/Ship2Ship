#pragma once
// ComboShip co-op SessionGuard (plan task 1.8). Pure decision functions, unit tested in coop_tests.
//
// Placement (Findings log, 2026-11-11): the guard runs in the LAUNCHER's ComboAnchor receive path,
// before a packet is forwarded to either DLL, instead of inside Anchor::OnIncomingJson and
// MMAnchor::OnIncomingJson. Every inbound packet already passes through ComboAnchor::ReceiveLoop, and
// the launcher is the only place that holds both the roster (with each peer's state.combo) and our
// own bound seed, so one check covers both games with zero vendored churn.

#include <string>

#include "CoopProtocol.h"

namespace Coop {

enum class GuardClass {
    Ungated,   // presence, room, puppets, sfx, server traffic: always delivered
    SeedGated, // save-mutating progression: requires the same seed (legacy peers: delivered)
    WorldGated // actor/room world sync: requires same protocol + seed + actor layout (legacy: dropped)
};

inline GuardClass ClassifyPacket(const std::string& type) {
    static const char* kSeedGated[] = {
        "GIVE_ITEM",          "SET_FLAG",         "UNSET_FLAG",           "SET_CHECK_STATUS",
        "UPDATE_TEAM_STATE",  "COMBO_CROSS_ITEM", "UPDATE_DUNGEON_ITEMS", "UPDATE_BEANS_COUNT",
        "ENTRANCE_DISCOVERED"
    };
    for (const char* t : kSeedGated)
        if (type == t)
            return GuardClass::SeedGated;
    if (type.rfind("COMBO_ACTOR_", 0) == 0 || type.rfind("COMBO_ROOM_", 0) == 0)
        return GuardClass::WorldGated;
    return GuardClass::Ungated;
}

enum class GuardVerdict { Accept, DropSeed, DropProtocol, DropLegacy, DropLayout, DropNoSeed };

inline const char* VerdictName(GuardVerdict v) {
    switch (v) {
        case GuardVerdict::Accept:
            return "accept";
        case GuardVerdict::DropSeed:
            return "seed mismatch";
        case GuardVerdict::DropProtocol:
            return "protocol mismatch";
        case GuardVerdict::DropLegacy:
            return "peer has no combo metadata";
        case GuardVerdict::DropLayout:
            return "actor layout mismatch";
        case GuardVerdict::DropNoSeed:
            return "no seed loaded";
    }
    return "?";
}

// `peerKnown` = the sender is in our roster (packets without a clientId — server-originated — and
// packets from peers we have no state for yet are not judged here).
// `game` = which game the packet targets (actor/room packets carry it; 0/1).
inline GuardVerdict Judge(const std::string& type, bool peerKnown, const PeerMeta& peer, const PeerMeta& self,
                          int game = 0) {
    GuardClass cls = ClassifyPacket(type);
    if (cls == GuardClass::Ungated || !peerKnown)
        return cls == GuardClass::WorldGated && !peerKnown ? GuardVerdict::DropLegacy : GuardVerdict::Accept;

    if (!peer.present)
        return cls == GuardClass::WorldGated ? GuardVerdict::DropLegacy : GuardVerdict::Accept;
    if (peer.protocol != self.protocol)
        return GuardVerdict::DropProtocol;

    if (cls == GuardClass::SeedGated) {
        // Only two LOADED saves can disagree; a side with no save loaded applies nothing anyway (and a
        // REQUEST_TEAM_STATE reply must reach a joiner at file select — UPDATE_TEAM_STATE is judged when
        // both report a seed).
        if (peer.seedId != 0 && self.seedId != 0 && peer.seedId != self.seedId)
            return GuardVerdict::DropSeed;
        return GuardVerdict::Accept;
    }

    // WorldGated: same seed (or both unseeded: two vanilla saves share the vanilla world; co-op combat
    // 2026-11-13) and the same actor layout. A seeded save never pairs with an unseeded one.
    if ((peer.seedId == 0) != (self.seedId == 0))
        return GuardVerdict::DropNoSeed;
    if (peer.seedId != self.seedId)
        return GuardVerdict::DropSeed;
    uint64_t a = peer.LayoutHash(game), b = self.LayoutHash(game);
    if (a == 0 || b == 0 || a != b)
        return GuardVerdict::DropLayout;
    return GuardVerdict::Accept;
}

} // namespace Coop
