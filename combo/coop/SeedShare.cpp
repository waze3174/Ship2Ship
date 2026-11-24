// combo/coop/SeedShare.cpp — see SeedShare.h. Launcher-only TU (compiled into ComboShip).
#include "SeedShare.h"

#include <chrono>
#include <deque>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>

#include <zlib.h>

#include "CoopBase64.h"
#include "SeedIdentity.h"
#include "SeedShareCore.h"

namespace SeedShare {
namespace {

HostApi sApi;

std::mutex sInMutex;
std::deque<nlohmann::json> sInbox; // receive thread -> Pump

// ── host side ──
struct OfferCache {
    uint64_t seedId = 0;
    uint32_t displaySeed = 0;
    uint32_t masterSeed = 0;
    nlohmann::json fileHash = nlohmann::json::array();
    size_t rawBytes = 0;
    std::vector<std::string> chunks; // base64(zlib(compact json)) split
};
OfferCache sOffer;
std::set<uint32_t> sOfferedPeers; // team peers that have seen the current offer
bool sShareRequested = false;

// ── joiner side ──
Status sStatus;
Coop::SeedAssembler sAssembler;
uint64_t sOfferRawBytes = 0;
std::filesystem::path sRoomSeedPath;
nlohmann::json sRoomSeed; // verified, parsed (kept so Apply needs no re-read)
uint64_t sRequestedSeedId = 0;
std::chrono::steady_clock::time_point sRequestTime;
std::chrono::steady_clock::time_point sRetryAt{}; // auto-apply retry while a local generation is running

void Log(const std::string& s) {
    std::cout << "[ComboShip][SeedShare] " << s << std::endl;
}

bool Deflate(const std::string& in, std::string& out) {
    uLongf cap = compressBound(static_cast<uLong>(in.size()));
    out.resize(cap);
    if (compress2(reinterpret_cast<Bytef*>(out.data()), &cap, reinterpret_cast<const Bytef*>(in.data()),
                  static_cast<uLong>(in.size()), Z_BEST_COMPRESSION) != Z_OK)
        return false;
    out.resize(cap);
    return true;
}

bool Inflate(const std::vector<uint8_t>& in, size_t rawBytes, std::string& out) {
    if (rawBytes == 0 || rawBytes > 64u * 1024u * 1024u)
        return false;
    out.resize(rawBytes);
    uLongf len = static_cast<uLongf>(rawBytes);
    if (uncompress(reinterpret_cast<Bytef*>(out.data()), &len, in.data(), static_cast<uLong>(in.size())) != Z_OK)
        return false;
    out.resize(len);
    return len == rawBytes;
}

nlohmann::json Envelope(const char* type, uint32_t ownId) {
    nlohmann::json p;
    p["type"] = type;
    p["clientId"] = ownId;     // the relay skips the sender when clientId is present
    p["originGame"] = "combo"; // neither game's origin filter forwards these (launcher-owned anyway)
    return p;
}

std::string PeerName(const RosterView& r, uint32_t id) {
    for (auto& p : r.peers)
        if (p.clientId == id)
            return p.name;
    return "host";
}

bool BuildOfferCache(uint64_t seedId) {
    std::string json = sApi.hostSeedJson ? sApi.hostSeedJson() : std::string();
    if (json.empty())
        return false;
    nlohmann::json seed;
    try {
        seed = nlohmann::json::parse(json);
    } catch (...) { return false; }
    // Identity is recomputed from content, never trusted from the file.
    if (Coop::ComputeSeedIdentity(seed).seedId != seedId) {
        Log("host seed content does not hash to the advertised seedId; not offering");
        return false;
    }
    std::string compact = seed.dump();
    std::string z;
    if (!Deflate(compact, z))
        return false;
    sOffer = OfferCache{};
    sOffer.seedId = seedId;
    sOffer.displaySeed = seed.value("displaySeed", 0u);
    sOffer.masterSeed = seed.value("masterSeed", 0u);
    sOffer.fileHash = seed.value("file_hash", nlohmann::json::array());
    sOffer.rawBytes = compact.size();
    sOffer.chunks = Coop::SplitChunks(Coop::Base64Encode(z));
    if (static_cast<int>(sOffer.chunks.size()) > Coop::kSeedMaxChunks) {
        Log("seed too large to share (" + std::to_string(compact.size()) + " bytes)");
        sOffer = OfferCache{};
        return false;
    }
    Log("prepared offer " + Coop::HexU64(seedId) + ": " + std::to_string(compact.size()) + " bytes -> " +
        std::to_string(z.size()) + " zlib, " + std::to_string(sOffer.chunks.size()) + " chunk(s)");
    return true;
}

void SendOffer(const RosterView& r) {
    auto p = Envelope(Coop::kPktSeedOffer, r.ownClientId);
    p["seedId"] = Coop::HexU64(sOffer.seedId);
    p["masterSeed"] = sOffer.masterSeed;
    p["displaySeed"] = sOffer.displaySeed;
    p["file_hash"] = sOffer.fileHash;
    p["bytes"] = sOffer.rawBytes;
    p["chunks"] = sOffer.chunks.size();
    p["protocol"] = Coop::kCoopProtocol;
    p["targetTeamId"] = r.ownTeam;
    sApi.send(p.dump());
}

void HostPump(const RosterView& r) {
    uint64_t seedId = sApi.hostSeedId ? sApi.hostSeedId() : 0;
    if (seedId == 0) {
        sOffer = OfferCache{};
        sOfferedPeers.clear();
        return;
    }
    bool rebuilt = false;
    if (sOffer.seedId != seedId) {
        if (!BuildOfferCache(seedId))
            return;
        sOfferedPeers.clear();
        rebuilt = true;
    }
    std::set<uint32_t> team;
    for (auto& p : r.peers)
        if (p.online && p.teamId == r.ownTeam)
            team.insert(p.clientId);
    // Forget peers that went offline, so a reconnect (same clientId: LastClientId is reused) is a newcomer.
    for (auto it = sOfferedPeers.begin(); it != sOfferedPeers.end();)
        it = team.count(*it) ? std::next(it) : sOfferedPeers.erase(it);
    bool newcomer = false;
    for (uint32_t id : team)
        if (!sOfferedPeers.count(id))
            newcomer = true;
    if ((rebuilt || newcomer || sShareRequested) && !team.empty()) {
        SendOffer(r);
        sOfferedPeers = team;
    }
    sShareRequested = false;
}

void HandleRequest(const nlohmann::json& p, const RosterView& r) {
    uint64_t want = Coop::JsonU64(p, "seedId");
    uint32_t from = p.value("clientId", 0u);
    if (want == 0 || from == 0 || want != sOffer.seedId || sOffer.chunks.empty())
        return;
    int n = static_cast<int>(sOffer.chunks.size());
    for (int i = 0; i < n; ++i) {
        auto c = Envelope(Coop::kPktSeedChunk, r.ownClientId);
        c["seedId"] = Coop::HexU64(sOffer.seedId);
        c["i"] = i;
        c["n"] = n;
        c["bytes"] = sOffer.rawBytes;
        c["blob"] = sOffer.chunks[static_cast<size_t>(i)];
        c["targetClientId"] = from;
        c["quiet"] = true;
        sApi.send(c.dump());
    }
    Log("sent " + std::to_string(n) + " chunk(s) of " + Coop::HexU64(sOffer.seedId) + " to client " +
        std::to_string(from));
}

void SetState(JoinerState s, const std::string& msg = {}) {
    sStatus.state = s;
    sStatus.message = msg;
}

// Called whenever we hold a verified room seed: decide between auto-apply, "ready" and "mismatch".
void SettleRoomSeed() {
    if (sRoomSeed.is_null())
        return;
    uint64_t active = sApi.activeSeedId ? sApi.activeSeedId() : 0;
    uint64_t staged = sApi.stagedSeedId ? sApi.stagedSeedId() : 0;
    if (active != 0) {
        if (active == sStatus.roomSeedId)
            SetState(JoinerState::Applied);
        else
            SetState(JoinerState::SaveMismatch,
                     "This save is on a different seed than the room. Return to file select to start a file on "
                     "the room seed, or disconnect.");
        return;
    }
    if (staged == sStatus.roomSeedId) {
        SetState(JoinerState::Applied);
        return;
    }
    if (sApi.autoAccept && sApi.autoAccept()) {
        int rc = sApi.applyRoomSeed ? sApi.applyRoomSeed(sRoomSeed) : 0;
        if (rc == 1) {
            SetState(JoinerState::Applied, "Loaded " + sStatus.hostName + "'s room seed");
            Log("applied room seed " + Coop::HexU64(sStatus.roomSeedId));
        } else if (rc == 2) {
            SetState(JoinerState::Ready, "Waiting for the seed generation in progress to finish");
            sRetryAt = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        } else {
            SetState(JoinerState::Failed, "Could not apply the room seed (see log)");
        }
        return;
    }
    SetState(JoinerState::Ready, "Room seed downloaded. Use it from the room window.");
}

bool AdoptVerifiedSeed(nlohmann::json seed, uint64_t expected, bool writeFile) {
    auto id = Coop::ComputeSeedIdentity(seed);
    if (id.seedId != expected) {
        SetState(JoinerState::Failed, "Room seed failed verification (hash mismatch)");
        Log("verification failed: got " + Coop::HexU64(id.seedId) + " want " + Coop::HexU64(expected));
        return false;
    }
    if (writeFile && sApi.writeSeedFile) {
        sRoomSeedPath = sApi.writeSeedFile(seed);
        if (sRoomSeedPath.empty())
            Log("warning: could not persist the room seed file (kept in memory)");
    }
    sRoomSeed = std::move(seed);
    sStatus.received = sStatus.total;
    SettleRoomSeed();
    return true;
}

void HandleOffer(const nlohmann::json& p, const RosterView& r) {
    uint32_t from = p.value("clientId", 0u);
    if (from == 0 || from != r.EffectiveHost())
        return; // only the room host's offer counts
    if (p.value("protocol", 0) != Coop::kCoopProtocol) {
        SetState(JoinerState::Failed, "Host runs an incompatible co-op protocol");
        return;
    }
    uint64_t seedId = Coop::JsonU64(p, "seedId");
    if (seedId == 0)
        return;
    sStatus.hostName = PeerName(r, from);
    sStatus.displaySeed = p.value("displaySeed", 0u);
    sStatus.fileHash = p.value("file_hash", nlohmann::json::array());
    if (seedId == sStatus.roomSeedId && !sRoomSeed.is_null()) {
        SettleRoomSeed(); // re-offer of what we already hold (new peer joined): just re-evaluate
        return;
    }
    sStatus.roomSeedId = seedId;
    sRoomSeed = nlohmann::json();
    sRoomSeedPath.clear();
    sStatus.total = p.value("chunks", 0);
    sStatus.received = 0;
    sOfferRawBytes = p.value("bytes", static_cast<uint64_t>(0));

    // Seed index: a local copy skips the transfer.
    if (sApi.findLocalSeed) {
        auto local = sApi.findLocalSeed(seedId);
        if (!local.empty()) {
            try {
                std::ifstream f(local);
                nlohmann::json seed = nlohmann::json::parse(f);
                sRoomSeedPath = local;
                if (AdoptVerifiedSeed(std::move(seed), seedId, false)) {
                    Log("room seed " + Coop::HexU64(seedId) + " found locally: " + local.string());
                    return;
                }
            } catch (...) {}
        }
    }
    sAssembler.Begin(seedId, sStatus.total);
    if (!sAssembler.Active()) {
        SetState(JoinerState::Failed, "Host offered an invalid seed size");
        return;
    }
    auto q = Envelope(Coop::kPktSeedRequest, r.ownClientId);
    q["seedId"] = Coop::HexU64(seedId);
    q["targetClientId"] = from;
    sApi.send(q.dump());
    sRequestedSeedId = seedId;
    sRequestTime = std::chrono::steady_clock::now();
    SetState(JoinerState::Downloading, "Downloading " + sStatus.hostName + "'s room seed");
}

void HandleChunk(const nlohmann::json& p) {
    uint64_t seedId = Coop::JsonU64(p, "seedId");
    if (!sAssembler.Add(seedId, p.value("i", -1), p.value("n", -1), p.value("blob", std::string())))
        return;
    sStatus.received = sAssembler.Received();
    if (!sAssembler.Complete())
        return;
    std::vector<uint8_t> z;
    std::string raw;
    std::string b64 = sAssembler.Join();
    sAssembler.Reset();
    if (!Coop::Base64Decode(b64, z) || !Inflate(z, static_cast<size_t>(sOfferRawBytes), raw)) {
        SetState(JoinerState::Failed, "Room seed transfer was corrupt");
        return;
    }
    try {
        AdoptVerifiedSeed(nlohmann::json::parse(raw), seedId, true);
    } catch (...) { SetState(JoinerState::Failed, "Room seed transfer was not valid JSON"); }
}

} // namespace

void SetHostApi(HostApi api) {
    sApi = std::move(api);
}

void OnNetworkPacket(const nlohmann::json& packet) {
    std::lock_guard<std::mutex> lk(sInMutex);
    if (sInbox.size() < 512)
        sInbox.push_back(packet);
}

void Pump() try {
    if (!sApi.send || !sApi.roster)
        return;
    std::deque<nlohmann::json> inbox;
    {
        std::lock_guard<std::mutex> lk(sInMutex);
        inbox.swap(sInbox);
    }
    if (!sApi.connected || !sApi.connected()) {
        if (sStatus.state != JoinerState::NoRoomSeed)
            OnDisconnected();
        return;
    }
    RosterView r = sApi.roster();
    if (r.ownClientId == 0)
        return;
    bool isHost = r.EffectiveHost() == r.ownClientId;

    for (auto& p : inbox) {
        std::string type = p.value("type", std::string());
        if (type == Coop::kPktSeedRequest && isHost)
            HandleRequest(p, r);
        else if (type == Coop::kPktSeedOffer && !isHost)
            HandleOffer(p, r);
        else if (type == Coop::kPktSeedChunk && !isHost)
            HandleChunk(p);
    }

    if (isHost) {
        if (sStatus.state != JoinerState::IsHost) {
            sStatus = Status{};
            SetState(JoinerState::IsHost);
            sRoomSeed = nlohmann::json();
        }
        HostPump(r);
        sStatus.roomSeedId = sOffer.seedId;
        sStatus.displaySeed = sOffer.displaySeed;
        sStatus.fileHash = sOffer.fileHash;
        return;
    }
    if (sStatus.state == JoinerState::IsHost) {
        sStatus = Status{}; // lost ownership
        sOffer = OfferCache{};
        sOfferedPeers.clear();
    }
    // A stalled transfer (host left mid-send, relay dropped us) re-requests after 10 s.
    if (sStatus.state == JoinerState::Downloading &&
        std::chrono::steady_clock::now() - sRequestTime > std::chrono::seconds(10)) {
        auto q = Envelope(Coop::kPktSeedRequest, r.ownClientId);
        q["seedId"] = Coop::HexU64(sRequestedSeedId);
        q["targetClientId"] = r.EffectiveHost();
        sApi.send(q.dump());
        sRequestTime = std::chrono::steady_clock::now();
        sAssembler.Begin(sRequestedSeedId, sStatus.total);
    }
    // Save state can change at any frame (file select -> loaded save): keep the verdict current.
    if (!sRoomSeed.is_null() && (sStatus.state == JoinerState::Ready || sStatus.state == JoinerState::Applied ||
                                 sStatus.state == JoinerState::SaveMismatch)) {
        uint64_t active = sApi.activeSeedId ? sApi.activeSeedId() : 0;
        uint64_t staged = sApi.stagedSeedId ? sApi.stagedSeedId() : 0;
        bool ok = active != 0 ? active == sStatus.roomSeedId : staged == sStatus.roomSeedId;
        JoinerState want = ok ? JoinerState::Applied : (active != 0 ? JoinerState::SaveMismatch : JoinerState::Ready);
        if (want != sStatus.state || (want == JoinerState::Ready && sRetryAt.time_since_epoch().count() != 0 &&
                                      std::chrono::steady_clock::now() >= sRetryAt)) {
            sRetryAt = {};
            SettleRoomSeed(); // e.g. back at file select with another seed staged: auto-apply again
        }
    }
} catch (const std::exception& e) {
    std::cerr << "[ComboShip][SeedShare] Pump threw: " << e.what() << std::endl;
} catch (...) { std::cerr << "[ComboShip][SeedShare] Pump threw a non-std exception" << std::endl; }

void RequestShare() {
    sShareRequested = true;
}

void UseRoomSeed() {
    if (sRoomSeed.is_null() || !sApi.applyRoomSeed)
        return;
    uint64_t active = sApi.activeSeedId ? sApi.activeSeedId() : 0;
    if (active != 0)
        return; // never rebinds a loaded save (§1.7: blocked, never auto-switched)
    int rc = sApi.applyRoomSeed(sRoomSeed);
    if (rc == 1)
        SetState(JoinerState::Applied, "Loaded " + sStatus.hostName + "'s room seed");
    else if (rc == 2)
        SetState(JoinerState::Ready, "Waiting for the seed generation in progress to finish");
    else
        SetState(JoinerState::Failed, "Could not apply the room seed (see log)");
}

void OnDisconnected() {
    sStatus = Status{};
    sAssembler.Reset();
    sRoomSeed = nlohmann::json();
    sRoomSeedPath.clear();
    sOffer = OfferCache{};
    sOfferedPeers.clear();
    sRequestedSeedId = 0;
}

Status GetStatus() {
    return sStatus;
}

nlohmann::json StatusJson() {
    static const char* kNames[] = { "none", "host", "downloading", "ready", "applied", "mismatch", "failed" };
    nlohmann::json j;
    j["state"] = kNames[static_cast<int>(sStatus.state)];
    j["roomSeedId"] = Coop::HexU64(sStatus.roomSeedId);
    j["displaySeed"] = sStatus.displaySeed;
    j["fileHash"] = sStatus.fileHash;
    j["host"] = sStatus.hostName;
    j["received"] = sStatus.received;
    j["total"] = sStatus.total;
    j["message"] = sStatus.message;
    return j;
}

} // namespace SeedShare
