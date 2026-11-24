// soh/soh/Enhancements/randomizer/ComboTireMark.c — ComboShip: MM's EffectTireMark in Hyrule (forms audit 2026-11-13,
// owner: "deku spin ... doesn't ... put trails on the ground"). The ground trail MM's Goron roll and Deku spin leave
// (mm z_player.c meleeWeaponEffectIndex[2], EFFECT_TIRE_MARK), ported from mm/src/code/z_eff_tire_mark.c: up to 64
// segments stuck 2 units above the scene floor polygon they were laid on, split at shared polygon edges, each fading
// out over its life (MM clamps 63 to 62 frames), drawn with MM's trail texture (gameplay_keep_Tex_014570, routed to
// MM's archive). OOT's effect table has no tire marks, so this is one self-contained trail for Link, updated and drawn
// by the player (z_player.c). Colour per MM's player: Goron { 0, 0, 15, 100 }, everyone else { 0, 0, 0, 150 }.
#include "global.h"
#include "ComboTireMark.h"

#define COMBO_TIRE_MARK_FLAG_1 (1 << 0) // MM EFFECT_TIRE_MARK_ELEMENT_FLAG_1: joined to the previous segment
#define COMBO_TIRE_MARK_FLAG_2 (1 << 1) // MM EFFECT_TIRE_MARK_ELEMENT_FLAG_2: the trail broke after this segment

typedef struct {
    u16 flags;
    Vec3s p1;
    Vec3s p2;
    s16 life;
    CollisionPoly* colPoly;
} ComboTireMarkElement;

typedef struct {
    ComboTireMarkElement elements[64];
    s16 numElements;
    s16 elemDuration;
    Color_RGBA8 color;
} ComboTireMark;

static ComboTireMark sMark = { .elemDuration = 62 };

// MM func_800BFD84: the floor polygon's height at (x, z).
static f32 Combo_PolyY(CollisionPoly* poly, f32 x, f32 z) {
    return (COLPOLY_GET_NORMAL(poly->normal.x * x + poly->normal.z * z) + poly->dist) /
           COLPOLY_GET_NORMAL(-poly->normal.y);
}

// MM func_800BFDEC: how many vertices two polygons share (and which).
static s32 Combo_PolySharedEdge(CollisionPoly* a, CollisionPoly* b, u32* v0, u32* v1) {
    s32 va[3], vb[3], i, j, count = 0;

    *v0 = *v1 = 0;
    va[0] = COLPOLY_VTX_INDEX(a->flags_vIA);
    va[1] = COLPOLY_VTX_INDEX(a->flags_vIB);
    va[2] = a->vIC;
    vb[0] = COLPOLY_VTX_INDEX(b->flags_vIA);
    vb[1] = COLPOLY_VTX_INDEX(b->flags_vIB);
    vb[2] = b->vIC;
    for (i = 0; i < 2; i++) {
        for (j = i + 1; j < 3; j++) {
            if (va[i] == vb[j]) {
                if (count == 0) {
                    *v0 = va[i];
                } else if (count == 1) {
                    *v1 = va[i];
                }
                count++;
            }
        }
    }
    return count;
}

// MM Math3D_LineSegMakePerpLineSeg (sys_math3d.c).
static s32 Combo_LineSegMakePerp(Vec3f* aA, Vec3f* aB, Vec3f* bA, Vec3f* bB, Vec3f* aClosest, Vec3f* bClosest) {
    f32 ax = aB->x - aA->x, ay = aB->y - aA->y, az = aB->z - aA->z;
    f32 bx = bB->x - bA->x, by = bB->y - bA->y, bz = bB->z - bA->z;
    f32 magSq = SQ(bx) + SQ(by) + SQ(bz);
    f32 scaleB, compA, compBA, tA, tB;
    Vec3f aPerp, baPerp;

    if (IS_ZERO(magSq)) {
        return false;
    }
    scaleB = 1.0f / magSq;
    compA = ((ax * bx) + (ay * by) + (az * bz)) * scaleB;
    compBA = ((bx * (aA->x - bA->x)) + (by * (aA->y - bA->y)) + (bz * (aA->z - bA->z))) * scaleB;
    aPerp.x = ax - (bx * compA);
    aPerp.y = ay - (by * compA);
    aPerp.z = az - (bz * compA);
    magSq = SQ(aPerp.x) + SQ(aPerp.y) + SQ(aPerp.z);
    if (IS_ZERO(magSq)) {
        return false;
    }
    baPerp.x = (aA->x - bA->x) - (bx * compBA);
    baPerp.y = (aA->y - bA->y) - (by * compBA);
    baPerp.z = (aA->z - bA->z) - (bz * compBA);
    tA = -((aPerp.x * baPerp.x) + (aPerp.y * baPerp.y) + (aPerp.z * baPerp.z)) / magSq;
    aClosest->x = (ax * tA) + aA->x;
    aClosest->y = (ay * tA) + aA->y;
    aClosest->z = (az * tA) + aA->z;
    tB = (compA * tA) + compBA;
    bClosest->x = (bx * tB) + bA->x;
    bClosest->y = (by * tB) + bA->y;
    bClosest->z = (bz * tB) + bA->z;
    return true;
}

static void Combo_TireMark_InitElement(ComboTireMarkElement* elem) {
    elem->flags = 0;
    elem->life = 0;
    elem->colPoly = NULL;
    elem->p1.x = elem->p1.y = elem->p1.z = elem->p2.x = elem->p2.y = elem->p2.z = 0;
}

static void Combo_TireMark_Push(Vec3s p1, Vec3s p2, CollisionPoly* poly) {
    ComboTireMarkElement* elem = &sMark.elements[sMark.numElements];

    elem->flags = 0;
    elem->p1 = p1;
    elem->p2 = p2;
    elem->life = sMark.elemDuration;
    elem->colPoly = poly;
    sMark.numElements++;
}

// MM func_800AEF44.
void Combo_TireMark_Break(void) {
    if (sMark.numElements > 0) {
        sMark.elements[sMark.numElements - 1].flags |= COMBO_TIRE_MARK_FLAG_2;
    }
}

// MM func_800AE930.
void Combo_TireMark_Add(PlayState* play, Vec3f* pos, f32 halfWidth, s16 angle, CollisionPoly* colPoly, s32 bgId,
                        s32 goron) {
    Vec3s left, right;
    ComboTireMarkElement* prev;
    u32 v0, v1;
    Vec3s* vtxList = play->colCtx.colHeader->vtxList;

    sMark.color = goron ? (Color_RGBA8){ 0, 0, 15, 100 } : (Color_RGBA8){ 0, 0, 0, 150 };
    if ((bgId != BGCHECK_SCENE) || (sMark.numElements >= (ARRAY_COUNT(sMark.elements) - 1)) || (colPoly == NULL)) {
        Combo_TireMark_Break();
        return;
    }

    left.x = (Math_SinS(angle - 0x4000) * halfWidth) + pos->x;
    left.z = (Math_CosS(angle - 0x4000) * halfWidth) + pos->z;
    left.y = Combo_PolyY(colPoly, left.x, left.z) + 2.0f;
    right.x = (Math_SinS(angle + 0x4000) * halfWidth) + pos->x;
    right.z = (Math_CosS(angle + 0x4000) * halfWidth) + pos->z;
    right.y = Combo_PolyY(colPoly, right.x, right.z) + 2.0f;

    if (sMark.numElements == 0) {
        Combo_TireMark_Push(right, left, colPoly);
        return;
    }
    prev = &sMark.elements[sMark.numElements - 1];
    if ((prev->colPoly != NULL) && (colPoly != prev->colPoly) &&
        (Combo_PolySharedEdge(prev->colPoly, colPoly, &v0, &v1) == 2)) {
        Vec3f e1 = { vtxList[v1].x, vtxList[v1].y, vtxList[v1].z };
        Vec3f e0 = { vtxList[v0].x, vtxList[v0].y, vtxList[v0].z };
        Vec3f a1 = { prev->p1.x, prev->p1.y, prev->p1.z };
        Vec3f b1 = { right.x, right.y, right.z };
        Vec3f a2 = { prev->p2.x, prev->p2.y, prev->p2.z };
        Vec3f b2 = { left.x, left.y, left.z };
        Vec3f c1, c2, unused;

        if (Combo_LineSegMakePerp(&e0, &e1, &a1, &b1, &c1, &unused) &&
            Combo_LineSegMakePerp(&e0, &e1, &a2, &b2, &c2, &unused)) {
            Vec3s s1 = { c1.x, c1.y, c1.z };
            Vec3s s2 = { c2.x, c2.y, c2.z };

            if (!(prev->flags & COMBO_TIRE_MARK_FLAG_2)) {
                prev->flags |= COMBO_TIRE_MARK_FLAG_1;
            }
            Combo_TireMark_Push(s1, s2, NULL); // the seam across the shared edge
            prev = &sMark.elements[sMark.numElements - 1];
            if (!(prev->flags & COMBO_TIRE_MARK_FLAG_2)) {
                prev->flags |= COMBO_TIRE_MARK_FLAG_1;
            }
            Combo_TireMark_Push(right, left, colPoly);
            return;
        }
    }
    if (!(prev->flags & COMBO_TIRE_MARK_FLAG_2)) {
        prev->flags |= COMBO_TIRE_MARK_FLAG_1;
    }
    Combo_TireMark_Push(right, left, colPoly);
}

// MM EffectTireMark_Update (once per game frame).
void Combo_TireMark_Update(void) {
    s32 i;

    if (sMark.numElements == 0) {
        return;
    }
    for (i = 0; i < sMark.numElements; i++) {
        sMark.elements[i].life--;
    }
    if (sMark.elements[0].life <= 0) {
        for (i = 0; i < ARRAY_COUNT(sMark.elements) - 1; i++) {
            sMark.elements[i] = sMark.elements[i + 1];
        }
        Combo_TireMark_InitElement(&sMark.elements[i]);
        sMark.numElements--;
        if (sMark.numElements < 0) {
            sMark.numElements = 0;
        }
    }
}

// A new scene: the old trail belongs to the old floor.
void Combo_TireMark_Clear(void) {
    s32 i;

    for (i = 0; i < ARRAY_COUNT(sMark.elements); i++) {
        Combo_TireMark_InitElement(&sMark.elements[i]);
    }
    sMark.numElements = 0;
}

static void Combo_TireMark_SetVertices(Vtx* vtx, ComboTireMarkElement* elem, s32 i, s32 alpha) {
    s32 i1 = i * 2;
    s32 i2 = i * 2 + 1;

    vtx[i1].v.ob[0] = elem->p1.x;
    vtx[i1].v.ob[1] = elem->p1.y;
    vtx[i1].v.ob[2] = elem->p1.z;
    vtx[i1].v.tc[0] = 2048;
    vtx[i1].v.tc[1] = 0;
    vtx[i1].v.cn[0] = vtx[i1].v.cn[1] = vtx[i1].v.cn[2] = 255;
    vtx[i1].v.cn[3] = alpha;
    vtx[i2].v.ob[0] = elem->p2.x;
    vtx[i2].v.ob[1] = elem->p2.y;
    vtx[i2].v.ob[2] = elem->p2.z;
    vtx[i2].v.tc[0] = 2048;
    vtx[i2].v.tc[1] = 1024;
    vtx[i2].v.cn[0] = vtx[i2].v.cn[1] = vtx[i2].v.cn[2] = 255;
    vtx[i2].v.cn[3] = alpha;
}

// MM EffectTireMark_Draw.
void Combo_TireMark_Draw(PlayState* play) {
    static const char* kTex = "__OTR__@mm:objects/gameplay_keep/gameplay_keep_Tex_014570";
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    f32 invLife = 1.0f / sMark.elemDuration;
    ComboTireMarkElement* elem;
    Vtx* vtx;
    s32 i;

    if (sMark.numElements < 2) {
        return;
    }
    OPEN_DISPS(gfxCtx);
    vtx = Graph_Alloc(gfxCtx, sMark.numElements * (2 * sizeof(Vtx)));
    if (vtx != NULL) {
        gSPMatrix(POLY_OPA_DISP++, &gMtxClear, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        POLY_OPA_DISP = Gfx_SetupDL(POLY_OPA_DISP++, SETUPDL_44);
        gDPSetRenderMode(POLY_OPA_DISP++, G_RM_PASS, G_RM_ZB_CLD_SURF2);
        gDPLoadTextureBlock(POLY_OPA_DISP++, kTex, G_IM_FMT_I, G_IM_SIZ_8b, 64, 32, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                            G_TX_NOMIRROR | G_TX_WRAP, 6, 5, G_TX_NOLOD, G_TX_NOLOD);
        gDPSetCombineLERP(POLY_OPA_DISP++, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, COMBINED, 0, 0, 0,
                          COMBINED);
        gSPSetGeometryMode(POLY_OPA_DISP++, G_SHADE | G_SHADING_SMOOTH);
        gSPClearGeometryMode(POLY_OPA_DISP++, G_CULL_BACK | G_LIGHTING);

        Combo_TireMark_SetVertices(vtx, &sMark.elements[0], 0, 255);
        vtx[0].v.tc[0] = 0;
        vtx[0].v.tc[1] = 0;
        vtx[1].v.tc[0] = 0;
        vtx[1].v.tc[1] = 1024;

        for (i = 1; i < sMark.numElements; i++) {
            elem = &sMark.elements[i];
            Combo_TireMark_SetVertices(vtx, elem, i, 255);
            if ((elem - 1)->flags & COMBO_TIRE_MARK_FLAG_1) {
                if (!(elem->flags & COMBO_TIRE_MARK_FLAG_1)) {
                    vtx[i * 2 + 0].v.tc[0] = 0;
                    vtx[i * 2 + 0].v.tc[1] = 0;
                    vtx[i * 2 + 1].v.tc[0] = 0;
                    vtx[i * 2 + 1].v.tc[1] = 1024;
                } else if ((i >= 2) && !((elem - 2)->flags & COMBO_TIRE_MARK_FLAG_1)) {
                    vtx[i * 2 - 2].v.tc[0] = 0;
                    vtx[i * 2 - 2].v.tc[1] = 0;
                    vtx[i * 2 - 1].v.tc[0] = 0;
                    vtx[i * 2 - 1].v.tc[1] = 1024;
                }
                if (i == (sMark.numElements - 1)) {
                    vtx[i * 2 + 0].v.tc[0] = 0;
                    vtx[i * 2 + 0].v.tc[1] = 0;
                    vtx[i * 2 + 1].v.tc[0] = 0;
                    vtx[i * 2 + 1].v.tc[1] = 1024;
                }
                gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, sMark.color.r, sMark.color.g, sMark.color.b,
                                (s32)((f32)sMark.color.a * (f32)elem->life * invLife));
                gSPVertex(POLY_OPA_DISP++, &vtx[i * 2 - 2], 4, 0);
                gSP2Triangles(POLY_OPA_DISP++, 0, 1, 3, 0, 0, 3, 2, 0);
            }
        }
    }
    CLOSE_DISPS(gfxCtx);
}
