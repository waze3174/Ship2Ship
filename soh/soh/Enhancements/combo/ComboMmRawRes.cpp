// soh/soh/Enhancements/combo/ComboMmRawRes.cpp — ComboShip: raw MM resources (vertex arrays and the like) for C actor
// ports in Hyrule. A display list can be routed to MM's ResourceManager by path ("__OTR__@mm:", Combo_MmDL), but
// OOT's gSPVertex wrapper resolves a vertex path at record time against OOT's own ResourceManager, so an MM vertex
// array has to be handed over as data. Loaded once through MM's resident ResourceManager and kept for the session;
// dropped when that ResourceManager is torn down. Used by the Powder Keg's fuse (z_en_bom_combo_keg.c).
#ifdef COMBO_BUILD
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>

#include <libultraship/libultraship.h>
#include <ship/resource/CrossRMRegistry.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/ResourceManagerScope.h>

namespace {
// Never destroyed: the resources belong to 2ship's ResourceManager, torn down before static destructors run.
std::unordered_map<std::string, std::shared_ptr<Ship::IResource>>& Cache() {
    static auto& sCache = *new std::unordered_map<std::string, std::shared_ptr<Ship::IResource>>();
    return sCache;
}

void ClearCache() {
    Cache().clear();
}
} // namespace

extern "C" void* Combo_MmRawResource(const char* otrPath) {
    static bool sHooked = (Ship::CrossRMRegistry::RegisterTeardownListener(&ClearCache), true);
    (void)sHooked;
    if (otrPath == nullptr) {
        return nullptr;
    }
    auto& cache = Cache();
    auto it = cache.find(otrPath);
    if (it == cache.end()) {
        std::shared_ptr<Ship::IResource> res;
        auto rm = Ship::CrossRMRegistry::Get("mm");
        if (rm != nullptr) {
            try {
                Ship::ResourceManagerScope scope(rm);
                res = rm->LoadResource(otrPath);
            } catch (...) { res.reset(); }
        }
        if (res == nullptr) {
            fprintf(stderr, "[ComboShip] MM resource %s unavailable\n", otrPath);
            if (rm == nullptr) {
                return nullptr; // MM's ResourceManager is not up yet: try again next time, don't cache the miss
            }
        }
        it = cache.emplace(otrPath, res).first;
    }
    return it->second ? it->second->GetRawPointer() : nullptr;
}
#endif
