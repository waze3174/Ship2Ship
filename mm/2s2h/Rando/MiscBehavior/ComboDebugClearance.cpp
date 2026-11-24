// mm/2s2h/Rando/MiscBehavior/ComboDebugClearance.cpp — ComboShip debug (launch env only): the adult-in-Termina audit.
// COMBO_DEBUG_MM_CLEARANCE=<first entrance scene, hex> walks every MM entrance scene from there to ENTR_SCENE_MAX and,
// in each, samples every walkable floor of the scene's static collision for the ceiling above it. Spots whose
// clearance is above human Link's ceilingCheckHeight (40) but at or under adult Link's (56, z_player.c
// Combo_AdultAgeProperties) are where the child body fits and the adult body does not; they are logged in 60-unit
// clusters. Static collision only (BgActors such as doors and moving platforms are not scanned).
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <utility>
#include <deque>
#include <set>
#include <tuple>
#include <vector>

#include <libultraship/libultraship.h>

#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/ShipInit.hpp"

extern "C" {
#include "z64.h"
#include "variables.h"
#include "functions.h"
extern SceneEntranceTableEntry sSceneEntranceTable[];
}

namespace {
struct Cell {
    int n = 0;
    float sx = 0, sy = 0, sz = 0, minC = 1e9f, maxC = 0;
};

void ScanScene(PlayState* play) {
    CollisionHeader* col = play->colCtx.colHeader;
    Player* player = GET_PLAYER(play);
    if (col == nullptr || player == nullptr) {
        return;
    }
    std::map<std::pair<int, int>, Cell> cells;
    std::map<std::pair<int, int>, float> cellY;
    int floors = 0, samples = 0, hits = 0, under40 = 0, over56 = 0;
    for (int i = 0; i < col->numPolygons; i++) {
        CollisionPoly* poly = &col->polyList[i];
        if (COLPOLY_GET_NORMAL(poly->normal.y) < 0.7f) {
            continue;
        }
        floors++;
        Vec3s* v[3];
        for (int k = 0; k < 3; k++) {
            v[k] = &col->vtxList[COLPOLY_VTX_INDEX(poly->vtxData[k])];
        }
        float ex = std::fabs((float)v[1]->x - v[0]->x) + std::fabs((float)v[2]->x - v[0]->x);
        float ez = std::fabs((float)v[1]->z - v[0]->z) + std::fabs((float)v[2]->z - v[0]->z);
        int steps = (int)(std::fmax(ex, ez) / 15.0f) + 1;
        if (steps > 24) {
            steps = 24;
        }
        for (int a = 0; a <= steps; a++) {
            for (int b = 0; a + b <= steps; b++) {
                // barycentric sample pulled 10% toward the centroid so edges shared with walls are skipped
                float u = (a + 0.33f) / (steps + 1.0f), w = (b + 0.33f) / (steps + 1.0f);
                float t = 1.0f - u - w;
                Vec3f p;
                p.x = t * v[0]->x + u * v[1]->x + w * v[2]->x;
                p.y = t * v[0]->y + u * v[1]->y + w * v[2]->y;
                p.z = t * v[0]->z + u * v[1]->z + w * v[2]->z;
                // the real floor at this point (the top one, in case floors stack)
                CollisionPoly* fp;
                Vec3f q = p;
                q.y += 2.0f;
                f32 fy = BgCheck_EntityRaycastFloor1(&play->colCtx, &fp, &q);
                if (fy < p.y - 4.0f) {
                    continue;
                }
                p.y = fy + 1.0f;
                samples++;
                f32 cy;
                CollisionPoly* cp;
                s32 bgId;
                if (!BgCheck_EntityCheckCeiling(&play->colCtx, &cy, &p, 70.0f, &cp, &bgId, &player->actor) ||
                    (bgId != BGCHECK_SCENE)) {
                    continue;
                }
                float c = (cy + 70.0f) - fy; // outY is the ceiling minus checkHeight (BgCheck_CheckStaticCeiling)
                hits++;
                under40 += (c <= 40.0f);
                over56 += (c > 56.0f);
                if (c <= 40.0f || c > 56.0f) {
                    continue;
                }
                auto key = std::make_pair((int)std::floor(p.x / 60.0f), (int)std::floor(p.z / 60.0f));
                Cell& cell = cells[key];
                cell.n++;
                cell.sx += p.x;
                cell.sy += fy;
                cell.sz += p.z;
                cell.minC = std::fmin(cell.minC, c);
                cell.maxC = std::fmax(cell.maxC, c);
            }
        }
    }
    SPDLOG_INFO("[ComboShip] clearance: scene {:#x} entrance-scene {:#x} floors {} samples {} ceiling hits {} (<=40 "
                "{}, >56 {}) tight cells {}",
                (int)play->sceneId, (int)(gSaveContext.save.entrance >> 9), floors, samples, hits, under40, over56,
                cells.size());
    for (auto& [key, c] : cells) {
        SPDLOG_INFO("[ComboShip] clearance:   at {:.0f} {:.0f} {:.0f} samples {} clearance {:.0f}..{:.0f}", c.sx / c.n,
                    c.sy / c.n, c.sz / c.n, c.n, c.minC, c.maxC);
    }
}

// Walkable reach from every spawn of the scene, for one body. A step A -> B (grid neighbours) is taken the way MM's
// Actor_UpdateBgCheckInfo resolves it for the player: the wall sphere (radius, 26.8 up) must not push B back, a
// ceiling under (ceilingCheckHeight - 4) above A + 4 sends x/z back to the previous frame (z_actor.c: ceiling and
// floor on the same bg), and the floor is found from 50 above. Drops are taken; climbing and jumping are not.
struct Body {
    float radius, ceiling;
};
using NodeKey = std::tuple<int, int, int>;
static constexpr float kGrid = 12.0f;

// 0 = the step is taken (out->y is the new floor), 1 = wall, 2 = ceiling, 3 = no floor
static int Step(PlayState* play, const Vec3f& a, Vec3f next, Body body, Vec3f* out) {
    Player* player = GET_PLAYER(play);
    Vec3f res;
    CollisionPoly* wp;
    s32 wb;
    if (BgCheck_EntitySphVsWall3(&play->colCtx, &res, &next, (Vec3f*)&a, body.radius, &wp, &wb, &player->actor,
                                 26.8f) &&
        (std::fabs(res.x - next.x) + std::fabs(res.z - next.z) > 1.0f)) {
        return 1;
    }
    Vec3f cpos = { next.x, a.y + 4.0f, next.z };
    f32 cy;
    CollisionPoly* cp;
    s32 cb;
    if (BgCheck_EntityCheckCeiling(&play->colCtx, &cy, &cpos, body.ceiling - 4.0f, &cp, &cb, &player->actor)) {
        return 2;
    }
    Vec3f fpos = { next.x, a.y + 50.0f, next.z };
    CollisionPoly* fp;
    f32 fy = BgCheck_EntityRaycastFloor1(&play->colCtx, &fp, &fpos);
    if (fy <= BGCHECK_Y_MIN + 1.0f || fy < a.y - 400.0f) {
        return 3;
    }
    *out = next;
    out->y = fy;
    return 0;
}
static NodeKey KeyOf(const Vec3f& p) {
    return NodeKey((int)std::lround(p.x / kGrid), (int)std::lround(p.z / kGrid), (int)std::lround(p.y / 25.0f));
}

static std::set<NodeKey> Reach(PlayState* play, const std::vector<Vec3f>& seeds, Body body, int* outCapped,
                               std::map<NodeKey, Vec3f>* parents = nullptr) {
    std::set<NodeKey> seen;
    std::deque<Vec3f> open;
    auto keyOf = KeyOf;
    for (const Vec3f& s : seeds) {
        if (seen.insert(keyOf(s)).second) {
            open.push_back(s);
        }
    }
    static const int dx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, dz[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
    while (!open.empty()) {
        if (seen.size() > 1500000) {
            *outCapped = 1;
            break;
        }
        Vec3f a = open.front();
        open.pop_front();
        for (int d = 0; d < 8; d++) {
            Vec3f next = { a.x + dx[d] * kGrid, a.y, a.z + dz[d] * kGrid };
            if (Step(play, a, next, body, &next) != 0) {
                continue;
            }
            if (seen.insert(keyOf(next)).second) {
                open.push_back(next);
                if (parents != nullptr) {
                    (*parents)[keyOf(next)] = a;
                }
            }
        }
    }
    return seen;
}

static void ReachScene(PlayState* play) {
    std::vector<Vec3f> seeds;
    SceneEntranceTableEntry* ste = &sSceneEntranceTable[gSaveContext.save.entrance >> 9];
    if (play->setupEntranceList != nullptr && play->linkActorEntry != nullptr && ste->table != nullptr) {
        ActorEntry* base = play->linkActorEntry - play->setupEntranceList[play->curSpawn].spawn;
        for (u32 e = 0; e < ste->tableCount; e++) {
            EntranceTableEntry* t = ste->table[e];
            if (t == nullptr || std::abs((int)t->sceneId) != (int)play->sceneId) {
                continue;
            }
            ActorEntry* a = &base[play->setupEntranceList[t->spawnNum].spawn];
            Vec3f p = { (f32)a->pos.x, (f32)a->pos.y, (f32)a->pos.z };
            CollisionPoly* fp;
            Vec3f q = { p.x, p.y + 30.0f, p.z };
            f32 fy = BgCheck_EntityRaycastFloor1(&play->colCtx, &fp, &q);
            if (fy > BGCHECK_Y_MIN + 1.0f) {
                p.y = fy;
                seeds.push_back(p);
            }
        }
    }
    seeds.push_back(GET_PLAYER(play)->actor.world.pos);
    int capC = 0, capA = 0;
    std::map<NodeKey, Vec3f> parents;
    auto child = Reach(play, seeds, { 14.0f, 40.0f }, &capC, &parents);
    auto adult = Reach(play, seeds, { 18.0f, 56.0f }, &capA);
    std::map<std::pair<int, int>, std::pair<int, Vec3f>> lost;
    std::map<std::pair<int, int>, NodeKey> lostRep;
    int adultOnly = 0, deep = 0;
    for (const NodeKey& k : child) {
        if (adult.count(k)) {
            continue;
        }
        // the 4-unit strip along walls the wider body can't hug is not a lost place: only count nodes at least
        // three grid steps from anything the adult body reaches
        bool nearAdult = false;
        for (int ox = -3; ox <= 3 && !nearAdult; ox++) {
            for (int oz = -3; oz <= 3 && !nearAdult; oz++) {
                for (int oy = -1; oy <= 1 && !nearAdult; oy++) {
                    nearAdult =
                        adult.count(NodeKey(std::get<0>(k) + ox, std::get<1>(k) + oz, std::get<2>(k) + oy)) != 0;
                }
            }
        }
        if (nearAdult) {
            continue;
        }
        deep++;
        auto ck = std::make_pair((int)std::floor(std::get<0>(k) * kGrid / 120.0f),
                                 (int)std::floor(std::get<1>(k) * kGrid / 120.0f));
        lostRep.emplace(ck, k);
        auto& c = lost[ck];
        c.first++;
        c.second.x += std::get<0>(k) * kGrid;
        c.second.y += std::get<2>(k) * 25.0f;
        c.second.z += std::get<1>(k) * kGrid;
    }
    for (const NodeKey& k : adult) {
        adultOnly += !child.count(k);
    }
    SPDLOG_INFO("[ComboShip] reach: scene {:#x} entrance-scene {:#x} seeds {} child {}{} adult {}{} child-only {} "
                "(deep {}) adult-only {}",
                (int)play->sceneId, (int)(gSaveContext.save.entrance >> 9), seeds.size(), child.size(), capC ? "+" : "",
                adult.size(), capA ? "+" : "", child.size() - (adult.size() - adultOnly), deep, adultOnly);
    for (auto& [key, c] : lost) {
        if (c.first >= 4) {
            // the gate: walk the child's path back from this cluster to the seed; the step after the last node the
            // adult body also reaches is where it is stopped, and Step() says why
            std::vector<Vec3f> path;
            NodeKey cur = lostRep[key];
            Vec3f curPos = { std::get<0>(cur) * kGrid, std::get<2>(cur) * 25.0f, std::get<1>(cur) * kGrid };
            for (int guard = 0; guard < 2000000; guard++) {
                path.push_back(curPos);
                auto it = parents.find(KeyOf(curPos));
                if (it == parents.end()) {
                    break;
                }
                curPos = it->second;
            }
            int reason = -1;
            Vec3f gate = { 0, 0, 0 }, gateFrom = { 0, 0, 0 };
            for (int i = (int)path.size() - 1; i > 0; i--) {
                if (adult.count(KeyOf(path[i])) && !adult.count(KeyOf(path[i - 1]))) {
                    Vec3f o;
                    reason = Step(play, path[i], { path[i - 1].x, path[i].y, path[i - 1].z }, { 18.0f, 56.0f }, &o);
                    gate = path[i - 1];
                    gateFrom = path[i];
                }
            }
            SPDLOG_INFO("[ComboShip] reach:   child-only at {:.0f} {:.0f} {:.0f} nodes {} gate {:.0f} {:.0f} {:.0f} "
                        "from {:.0f} {:.0f} {:.0f} "
                        "({})",
                        c.second.x / c.first, c.second.y / c.first, c.second.z / c.first, c.first, gate.x, gate.y,
                        gate.z, gateFrom.x, gateFrom.y, gateFrom.z,
                        reason == 1   ? "wall"
                        : reason == 2 ? "ceiling"
                        : reason == 3 ? "floor"
                        : reason == 0 ? "step ok"
                                      : "?");
        }
    }
}
} // namespace

static void RegisterComboDebugClearance() {
    const char* env = std::getenv("COMBO_DEBUG_MM_CLEARANCE");
    if (env == nullptr) {
        return;
    }
    static int sNext = (int)std::strtol(env, nullptr, 16);
    COND_HOOK(OnGameStateUpdate, true, []() {
        static int sFrames = 0;
        static s16 sScene = -1;
        if (gPlayState == nullptr || gGameState != &gPlayState->state || GET_PLAYER(gPlayState) == nullptr) {
            return;
        }
        if (gPlayState->sceneId != sScene) {
            sScene = gPlayState->sceneId;
            sFrames = 0;
        }
        if (gPlayState->transitionTrigger != TRANS_TRIGGER_OFF || ++sFrames != 20) {
            return;
        }
        static bool sFirst = true;
        if (sNext == 0x7FFF) {
            return;
        }
        if (!sFirst) {
            ScanScene(gPlayState);
            ReachScene(gPlayState);
        }
        sFirst = false;
        while (sNext < ENTR_SCENE_MAX) {
            int s = sNext++;
            // "<first>,only=<hex>,<hex>,...": just those entrance scenes
            static const char* sOnly = std::strstr(std::getenv("COMBO_DEBUG_MM_CLEARANCE"), "only=");
            if (sOnly != nullptr) {
                bool listed = false;
                for (const char* q = sOnly + 5; *q != '\0';) {
                    char* end;
                    long v = std::strtol(q, &end, 16);
                    if (end == q) {
                        break;
                    }
                    listed |= (v == s);
                    q = (*end == ',') ? end + 1 : end;
                }
                if (!listed) {
                    continue;
                }
            }
            if (s == 0x08 || s == 0x09 || s == 0x0B || s == 0x0C || s == 0x0D || s == 0x0E || s == 0x0F || s == 0x2E ||
                s == 0x37) {
                continue;
            }
            SPDLOG_INFO("[ComboShip] clearance: going to entrance scene {:#x}", s);
            gPlayState->nextEntrance = (u16)(s << 9);
            gPlayState->transitionTrigger = TRANS_TRIGGER_START;
            gPlayState->transitionType = TRANS_TYPE_INSTANT;
            return;
        }
        SPDLOG_INFO("[ComboShip] clearance: done");
        sNext = 0x7FFF;
    });
}

static RegisterShipInitFunc sComboDebugClearanceInit(RegisterComboDebugClearance, {});

// COMBO_DEBUG_MM_WALK="<entrance scene hex>,x,y,z,tx,tz[,frames]": the in-game check of a gate the reach scan found.
// Once MM play runs, go to that entrance scene, put Link at x,y,z, and hold the stick toward tx,tz (camera-relative,
// the way Player reads it) for `frames` frames (default 150); log where he gets to every 10 frames and at the end.
static void RegisterComboDebugWalk() {
    const char* env = std::getenv("COMBO_DEBUG_MM_WALK");
    if (env == nullptr) {
        return;
    }
    static int sScene;
    static float sX, sY, sZ, sTx, sTz;
    static int sFramesWanted = 150;
    if (std::sscanf(env, "%x,%f,%f,%f,%f,%f,%d", &sScene, &sX, &sY, &sZ, &sTx, &sTz, &sFramesWanted) < 6) {
        return;
    }
    static int sPhase = 0; // 0 = go to scene, 1 = wait for it, 2 = walking, 3 = done
    static int sFrames = 0;
    COND_HOOK(OnGameStateMainStart, true, []() {
        if (gPlayState == nullptr || gGameState != &gPlayState->state || GET_PLAYER(gPlayState) == nullptr ||
            sPhase != 2) {
            return;
        }
        Player* player = GET_PLAYER(gPlayState);
        float dx = sTx - player->actor.world.pos.x;
        float dz = sTz - player->actor.world.pos.z;
        s16 want = Math_Atan2S_XY(dz, dx);
        s16 a = want - Camera_GetInputDirYaw(GET_ACTIVE_CAM(gPlayState));
        Input* in = &gPlayState->state.input[0];
        in->cur.stick_x = in->rel.stick_x = (s8)(-Math_SinS(a) * 70.0f);
        in->cur.stick_y = in->rel.stick_y = (s8)(Math_CosS(a) * 70.0f);
    });
    COND_HOOK(OnGameStateUpdate, true, []() {
        if (gPlayState == nullptr || gGameState != &gPlayState->state || GET_PLAYER(gPlayState) == nullptr) {
            return;
        }
        Player* player = GET_PLAYER(gPlayState);
        sFrames++;
        if (sPhase == 0 && sFrames > 20 && gPlayState->transitionTrigger == TRANS_TRIGGER_OFF) {
            gPlayState->nextEntrance = (u16)(sScene << 9);
            gPlayState->transitionTrigger = TRANS_TRIGGER_START;
            gPlayState->transitionType = TRANS_TYPE_INSTANT;
            sPhase = 1;
            sFrames = 0;
        } else if (sPhase == 1 && (gSaveContext.save.entrance >> 9) == sScene && sFrames > 30 &&
                   gPlayState->transitionTrigger == TRANS_TRIGGER_OFF) {
            player->actor.world.pos.x = sX;
            player->actor.world.pos.y = sY + 2.0f;
            player->actor.world.pos.z = sZ;
            player->actor.prevPos = player->actor.world.pos;
            player->actor.home.pos = player->actor.world.pos;
            player->actor.velocity.y = 0.0f;
            sPhase = 2;
            sFrames = 0;
            SPDLOG_INFO(
                "[ComboShip] walk: scene {:#x} start {:.0f} {:.0f} {:.0f} toward {:.0f} {:.0f} form {} adult {}",
                (int)gPlayState->sceneId, sX, sY, sZ, sTx, sTz, (int)player->transformation,
                (int)(player->ageProperties->wallCheckRadius > 15.0f));
        } else if (sPhase == 2) {
            float d = std::sqrt(SQ(sTx - player->actor.world.pos.x) + SQ(sTz - player->actor.world.pos.z));
            if ((sFrames % 10) == 0 || sFrames >= sFramesWanted || d < 8.0f) {
                SPDLOG_INFO("[ComboShip] walk: f{} at {:.0f} {:.0f} {:.0f} dist {:.0f} speed {:.1f} wall {} ceil {}",
                            sFrames, player->actor.world.pos.x, player->actor.world.pos.y, player->actor.world.pos.z, d,
                            player->speedXZ, (player->actor.bgCheckFlags & BGCHECKFLAG_WALL) != 0,
                            (player->actor.bgCheckFlags & BGCHECKFLAG_CEILING) != 0);
            }
            if (sFrames >= sFramesWanted || d < 8.0f) {
                SPDLOG_INFO("[ComboShip] walk: {} after {} frames", d < 8.0f ? "REACHED" : "STOPPED", sFrames);
                sPhase = 3;
            }
        }
    });
}

static RegisterShipInitFunc sComboDebugWalkInit(RegisterComboDebugWalk, {});
