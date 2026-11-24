#pragma once
// ComboShip co-op combat: the "mirror" section of COMBO_ACTOR_SNAP (wire v3+). The owner of an enemy is its single
// authority; every other client in the room shows a MIRROR of the owner's copy: the mirror does not run its AI
// (no update), it is posed and collided from this record every logic frame:
//   - skels: every SkelAnime inside the actor instance (byte offset), animation resource path + playback state, and
//     the raw jointTable (the exact pose the owner drew).
//   - cols:  every collider the owner's update registered in the AC / OC lists (byte offset in the instance), with its
//     on/off flags, per-element flags and world geometry. Mirrors re-register them (never AT: attacks are the
//     owner's).
//   - base:  focus (lock-on point), scale, shape.yOffset, a masked subset of actor flags.
//   - child: an actor an owned enemy spawned (rock, nut, egg, boss spawn) carries its parent's key + spawn params, so
//     receivers spawn the same actor as a mirror (they never spawn it themselves: their parent mirror never updates).
// Header-only, game-agnostic, unit tested in coop_tests. Byte layout is little-endian (every shipping target).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace Coop::Mirror {

constexpr uint8_t kBlobVersion = 2; // v2: parentKey + params (owner-spawned children)
constexpr size_t kMaxSkels = 8;
constexpr size_t kMaxCols = 16;
constexpr size_t kMaxElems = 64;
constexpr size_t kMaxLimbs = 128;
constexpr size_t kMaxPath = 512;

// Collider shapes (same numbering in both games; MM adds SPHERE = 4).
enum Shape : uint8_t { kJntSph = 0, kCylinder = 1, kTris = 2, kQuad = 3, kSphere = 4 };
// Which lists the owner registered the collider in after the actor's update.
enum Lists : uint8_t { kInAC = 1 << 0, kInOC = 1 << 1 };

// Geometry floats per element / collider, by shape.
inline size_t ElemGeom(uint8_t shape) {
    switch (shape) {
        case kJntSph: return 4;  // world sphere center xyz, radius
        case kTris: return 9;    // 3 vertices
        default: return 0;
    }
}
inline size_t ColGeom(uint8_t shape) {
    switch (shape) {
        case kCylinder: return 6; // radius, height, yShift, pos xyz
        case kQuad: return 12;    // 4 vertices
        case kSphere: return 4;   // world sphere
        default: return 0;
    }
}
inline bool HasElemList(uint8_t shape) {
    return shape == kJntSph || shape == kTris;
}

struct Skel {
    uint32_t off = 0;     // byte offset of the SkelAnime in the actor instance
    std::string anim;     // resource path ("__OTR__...") of skelAnime->animation, "" if not a path
    float curFrame = 0, playSpeed = 0, startFrame = 0, endFrame = 0, morphWeight = 0, animLength = 0;
    uint8_t mode = 0;
    std::vector<int16_t> joints; // limbCount * 3 (Vec3s jointTable)
};

struct Elem {
    uint8_t acElemFlags = 0; // OOT bumperFlags / MM acElemFlags (HIT bit cleared)
    uint8_t ocElemFlags = 0;
    uint32_t acDmgFlags = 0; // bumper dmgFlags (actors change their weaknesses at runtime)
    std::vector<float> g;    // ElemGeom(shape)
};

struct Col {
    uint32_t off = 0;
    uint8_t shape = 0;
    uint8_t lists = 0;
    uint8_t acFlags = 0;  // base.acFlags (HIT/BOUNCED cleared)
    uint8_t ocFlags1 = 0; // base.ocFlags1 (HIT cleared)
    uint8_t ocFlags2 = 0;
    std::vector<float> g; // ColGeom(shape)
    Elem single;          // the one element of cylinder / quad / sphere
    std::vector<Elem> elems; // jntSph / tris
};

struct Rec {
    uint64_t key = 0;
    float focus[3] = {};
    int16_t focusRot[3] = {};
    float scale[3] = {};
    float yOffset = 0;
    uint32_t flags = 0; // actor flags, already masked by the sender
    uint64_t parentKey = 0; // != 0: an owner-spawned child; receivers spawn it from these fields
    int16_t params = 0;
    std::vector<Skel> skels;
    std::vector<Col> cols;
};

namespace detail {
struct W {
    std::vector<uint8_t>& b;
    template <typename T> void put(const T& v) {
        uint8_t tmp[sizeof(T)];
        std::memcpy(tmp, &v, sizeof(T));
        b.insert(b.end(), tmp, tmp + sizeof(T));
    }
    void str(const std::string& s) {
        uint16_t n = (uint16_t)std::min(s.size(), kMaxPath);
        put(n);
        b.insert(b.end(), s.begin(), s.begin() + n);
    }
};
struct R {
    const uint8_t* p;
    const uint8_t* end;
    bool ok = true;
    template <typename T> T get() {
        T v{};
        if (!ok || (size_t)(end - p) < sizeof(T)) {
            ok = false;
            return v;
        }
        std::memcpy(&v, p, sizeof(T));
        p += sizeof(T);
        return v;
    }
    float f() {
        float v = get<float>();
        if (!std::isfinite(v))
            ok = false;
        return v;
    }
    std::string str() {
        uint16_t n = get<uint16_t>();
        if (!ok || n > kMaxPath || (size_t)(end - p) < n) {
            ok = false;
            return {};
        }
        std::string s((const char*)p, n);
        p += n;
        return s;
    }
};

inline void PutElem(W& w, const Elem& e, size_t geom) {
    w.put(e.acElemFlags);
    w.put(e.ocElemFlags);
    w.put(e.acDmgFlags);
    for (size_t i = 0; i < geom; ++i)
        w.put(i < e.g.size() ? e.g[i] : 0.0f);
}
inline Elem GetElem(R& r, size_t geom) {
    Elem e;
    e.acElemFlags = r.get<uint8_t>();
    e.ocElemFlags = r.get<uint8_t>();
    e.acDmgFlags = r.get<uint32_t>();
    e.g.resize(geom);
    for (auto& v : e.g)
        v = r.f();
    return e;
}
} // namespace detail

inline void EncodeRec(std::vector<uint8_t>& out, const Rec& rec) {
    detail::W w{ out };
    w.put(rec.key);
    for (float v : rec.focus)
        w.put(v);
    for (int16_t v : rec.focusRot)
        w.put(v);
    for (float v : rec.scale)
        w.put(v);
    w.put(rec.yOffset);
    w.put(rec.flags);
    w.put(rec.parentKey);
    w.put(rec.params);
    uint8_t ns = (uint8_t)std::min(rec.skels.size(), kMaxSkels);
    w.put(ns);
    for (size_t i = 0; i < ns; ++i) {
        const Skel& s = rec.skels[i];
        w.put(s.off);
        w.str(s.anim);
        w.put(s.curFrame);
        w.put(s.playSpeed);
        w.put(s.startFrame);
        w.put(s.endFrame);
        w.put(s.morphWeight);
        w.put(s.animLength);
        w.put(s.mode);
        uint8_t limbs = (uint8_t)std::min(s.joints.size() / 3, kMaxLimbs);
        w.put(limbs);
        for (size_t j = 0; j < (size_t)limbs * 3; ++j)
            w.put(s.joints[j]);
    }
    uint8_t nc = (uint8_t)std::min(rec.cols.size(), kMaxCols);
    w.put(nc);
    for (size_t i = 0; i < nc; ++i) {
        const Col& c = rec.cols[i];
        w.put(c.off);
        w.put(c.shape);
        w.put(c.lists);
        w.put(c.acFlags);
        w.put(c.ocFlags1);
        w.put(c.ocFlags2);
        size_t cg = ColGeom(c.shape);
        for (size_t j = 0; j < cg; ++j)
            w.put(j < c.g.size() ? c.g[j] : 0.0f);
        if (HasElemList(c.shape)) {
            uint8_t ne = (uint8_t)std::min(c.elems.size(), kMaxElems);
            w.put(ne);
            for (size_t j = 0; j < ne; ++j)
                detail::PutElem(w, c.elems[j], ElemGeom(c.shape));
        } else {
            detail::PutElem(w, c.single, 0);
        }
    }
}

inline bool DecodeRec(const uint8_t* p, size_t n, Rec& rec) {
    detail::R r{ p, p + n };
    rec = Rec{};
    rec.key = r.get<uint64_t>();
    for (float& v : rec.focus)
        v = r.f();
    for (int16_t& v : rec.focusRot)
        v = r.get<int16_t>();
    for (float& v : rec.scale)
        v = r.f();
    rec.yOffset = r.f();
    rec.flags = r.get<uint32_t>();
    rec.parentKey = r.get<uint64_t>();
    rec.params = r.get<int16_t>();
    uint8_t ns = r.get<uint8_t>();
    if (ns > kMaxSkels)
        return false;
    for (size_t i = 0; r.ok && i < ns; ++i) {
        Skel s;
        s.off = r.get<uint32_t>();
        s.anim = r.str();
        s.curFrame = r.f();
        s.playSpeed = r.f();
        s.startFrame = r.f();
        s.endFrame = r.f();
        s.morphWeight = r.f();
        s.animLength = r.f();
        s.mode = r.get<uint8_t>();
        uint8_t limbs = r.get<uint8_t>();
        if (limbs > kMaxLimbs)
            return false;
        s.joints.resize((size_t)limbs * 3);
        for (auto& j : s.joints)
            j = r.get<int16_t>();
        rec.skels.push_back(std::move(s));
    }
    uint8_t nc = r.get<uint8_t>();
    if (nc > kMaxCols)
        return false;
    for (size_t i = 0; r.ok && i < nc; ++i) {
        Col c;
        c.off = r.get<uint32_t>();
        c.shape = r.get<uint8_t>();
        if (c.shape > kSphere)
            return false;
        c.lists = r.get<uint8_t>();
        c.acFlags = r.get<uint8_t>();
        c.ocFlags1 = r.get<uint8_t>();
        c.ocFlags2 = r.get<uint8_t>();
        c.g.resize(ColGeom(c.shape));
        for (auto& v : c.g)
            v = r.f();
        if (HasElemList(c.shape)) {
            uint8_t ne = r.get<uint8_t>();
            if (ne > kMaxElems)
                return false;
            for (size_t j = 0; r.ok && j < ne; ++j)
                c.elems.push_back(detail::GetElem(r, ElemGeom(c.shape)));
        } else {
            c.single = detail::GetElem(r, 0);
        }
        rec.cols.push_back(std::move(c));
    }
    return r.ok && r.p == r.end && rec.key != 0;
}

// Blob: [u8 version][u16 count] then per record [u32 length][record bytes].
inline void EncodeBlob(std::vector<uint8_t>& out, const std::vector<Rec>& recs) {
    out.clear();
    detail::W w{ out };
    w.put(kBlobVersion);
    w.put((uint16_t)recs.size());
    for (const Rec& rec : recs) {
        size_t at = out.size();
        w.put((uint32_t)0);
        EncodeRec(out, rec);
        uint32_t len = (uint32_t)(out.size() - at - 4);
        std::memcpy(out.data() + at, &len, 4);
    }
}

// All-or-nothing: false (and no records) on a truncated / garbage blob.
inline bool DecodeBlob(const std::vector<uint8_t>& blob, std::vector<Rec>& out) {
    out.clear();
    detail::R r{ blob.data(), blob.data() + blob.size() };
    if (r.get<uint8_t>() != kBlobVersion || !r.ok)
        return false;
    uint16_t n = r.get<uint16_t>();
    for (size_t i = 0; r.ok && i < n; ++i) {
        uint32_t len = r.get<uint32_t>();
        if (!r.ok || (size_t)(r.end - r.p) < len) {
            out.clear();
            return false;
        }
        Rec rec;
        if (!DecodeRec(r.p, len, rec)) {
            out.clear();
            return false;
        }
        r.p += len;
        out.push_back(std::move(rec));
    }
    if (!r.ok || r.p != r.end) {
        out.clear();
        return false;
    }
    return true;
}

// The SkelAnimes that belong to an actor: registered SkelAnime addresses inside [base, base + size), by offset.
inline std::vector<uint32_t> SkelOffsets(const std::vector<uintptr_t>& registered, uintptr_t base, size_t size,
                                         size_t skelSize) {
    std::vector<uint32_t> offs;
    for (uintptr_t a : registered)
        if (a >= base && a + skelSize <= base + size)
            offs.push_back((uint32_t)(a - base));
    std::sort(offs.begin(), offs.end());
    offs.erase(std::unique(offs.begin(), offs.end()), offs.end());
    return offs;
}

// A collider at `off` with `bytes` of struct fits inside an instance of `size`.
inline bool FitsInstance(uint32_t off, size_t bytes, size_t size) {
    return (size_t)off + bytes <= size;
}

} // namespace Coop::Mirror
