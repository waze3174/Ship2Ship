/*
 * File: z_kaleido_combo.c
 * Overlay: ovl_kaleido_scope
 * Description: ComboShip merged pause menus — the Termina and Masks faces of OOT's pause prism (MM's item page and MM's
 * mask page, MM's live inventory). Logic + exports: soh/soh/Enhancements/randomizer/ComboPause.cpp; encoding:
 * combo/menu/ComboPauseABI.h; deviation record: docs/deviations/ui-menu.md "Merged pause menus".
 *
 * A foreign face is MM's page as MM draws and drives it: MM's own page tiles (header "SELECT ITEM" / "MASKS"), MM's
 * icons, name plates and ammo digits, and MM's cursor rules (the cursor rests on any slot, empty ones included, and
 * turns white there). Only the cube, the frame around the page and the buttons are the host's.
 */
#ifdef COMBO_BUILD
#include "z_kaleido_scope.h"
#include "textures/parameter_static/parameter_static.h"
#include "soh/Enhancements/randomizer/ComboPause.h"
#include "soh/Enhancements/randomizer/ComboBombArrows.h"
#include <libultraship/bridge/consolevariablebridge.h>

extern const char* _gAmmoDigit0Tex[];
Gfx* KaleidoScope_DrawPageSections(Gfx* gfx, Vtx* vertices, void** textures);      // z_kaleido_scope_PAL.c
void KaleidoScope_ComboDrawBombArrowBadge(PlayState* play, u8 slot, u8 onPreview); // z_kaleido_item.c

#define COMBO_GRID_X(slot) (-96 + ((slot) % 6) * 32 + 2)
#define COMBO_GRID_Y(pauseCtx, slot) (58 - ((slot) / 6) * 32 + (pauseCtx)->offsetY - 2)
// One outline quad per button (B has none), one for the mask worn without a button, one for the persistent hood.
#define COMBO_OUTLINE_QUADS (ARRAY_COUNT(gSaveContext.equips.buttonItems) + 1)

static void Combo_SetQuad(Vtx* v, s16 x, s16 y, s16 w, s16 h, s16 texW, s16 texH) {
    s32 i;
    for (i = 0; i < 4; i++) {
        v[i].v.ob[0] = x + ((i & 1) ? w : 0);
        v[i].v.ob[1] = y - ((i & 2) ? h : 0);
        v[i].v.ob[2] = 0;
        v[i].v.flag = 0;
        v[i].v.tc[0] = (i & 1) ? (texW << 5) : 0;
        v[i].v.tc[1] = (i & 2) ? (texH << 5) : 0;
        v[i].v.cn[0] = v[i].v.cn[1] = v[i].v.cn[2] = v[i].v.cn[3] = 255;
    }
}

// OOT's item page draws its "Select Item" tiles; a foreign tab draws MM's own (same 3x5 80x32 IA8 layout).
Gfx* KaleidoScope_ComboDrawItemPageSections(Gfx* gfx, Vtx* vertices, void** nativeTextures) {
    const char* const* mmTexs = Combo_PausePageTexs();
    if (mmTexs == NULL) {
        return KaleidoScope_DrawPageSections(gfx, vertices, nativeTextures);
    }
    // MM colours its pages with a flat prim colour (180,180,120: TEXEL0 * PRIM, alpha TEXEL0 * SHADE), not OOT's
    // vertex gradients: an MM page in Hyrule wears MM's own colour.
    gDPPipeSync(gfx++);
    gDPSetCombineLERP(gfx++, TEXEL0, 0, PRIMITIVE, 0, TEXEL0, 0, SHADE, 0, TEXEL0, 0, PRIMITIVE, 0, TEXEL0, 0, SHADE,
                      0);
    gDPSetPrimColor(gfx++, 0, 0, 180, 180, 120, 255);
    gSPComboRMPush(gfx++, "mm");
    gfx = KaleidoScope_DrawPageSections(gfx, vertices, (void**)mmTexs);
    gSPComboRMPop(gfx++);
    gDPPipeSync(gfx++);
    gDPSetCombineMode(gfx++, G_CC_MODULATEIA, G_CC_MODULATEIA);
    return gfx;
}

// MM's KaleidoScope_UpdateItemCursor (mm z_kaleido_item.c), on OOT's pause context.
static void KaleidoScope_ComboUpdateForeignCursor(PlayState* play) {
    Input* input = &play->state.input[0];
    PauseContext* pauseCtx = &play->pauseCtx;
    bool dpad = (CVarGetInteger(CVAR_SETTING("DPadOnPause"), 0) && !CHECK_BTN_ALL(input->cur.button, BTN_CUP));
    s16 oldCursorPoint = pauseCtx->cursorPoint[PAUSE_ITEM];
    u16 cursorItem;
    s16 cursorPoint;
    s16 cursorX;
    s16 cursorY;
    s16 moveCursorResult = 0;
    s32 stickX = pauseCtx->stickRelX;
    s32 stickY = pauseCtx->stickRelY;

    if (dpad && CHECK_BTN_ALL(input->press.button, BTN_DLEFT)) {
        stickX = -40;
    } else if (dpad && CHECK_BTN_ALL(input->press.button, BTN_DRIGHT)) {
        stickX = 40;
    }
    if (dpad && CHECK_BTN_ALL(input->press.button, BTN_DUP)) {
        stickY = 40;
    } else if (dpad && CHECK_BTN_ALL(input->press.button, BTN_DDOWN)) {
        stickY = -40;
    }

    if (pauseCtx->cursorSpecialPos == 0) {
        if (ABS(stickX) > 30) {
            cursorPoint = pauseCtx->cursorPoint[PAUSE_ITEM];
            cursorX = pauseCtx->cursorX[PAUSE_ITEM];
            cursorY = pauseCtx->cursorY[PAUSE_ITEM];
            while (moveCursorResult == 0) {
                if (stickX < -30) {
                    if (pauseCtx->cursorX[PAUSE_ITEM] != 0) {
                        pauseCtx->cursorX[PAUSE_ITEM]--;
                        pauseCtx->cursorPoint[PAUSE_ITEM]--;
                        moveCursorResult = 1;
                    } else {
                        pauseCtx->cursorX[PAUSE_ITEM] = cursorX;
                        pauseCtx->cursorY[PAUSE_ITEM]++;
                        if (pauseCtx->cursorY[PAUSE_ITEM] >= 4) {
                            pauseCtx->cursorY[PAUSE_ITEM] = 0;
                        }
                        pauseCtx->cursorPoint[PAUSE_ITEM] =
                            pauseCtx->cursorX[PAUSE_ITEM] + (pauseCtx->cursorY[PAUSE_ITEM] * 6);
                        if (pauseCtx->cursorPoint[PAUSE_ITEM] >= 24) {
                            pauseCtx->cursorPoint[PAUSE_ITEM] = pauseCtx->cursorX[PAUSE_ITEM];
                        }
                        if (cursorY == pauseCtx->cursorY[PAUSE_ITEM]) {
                            pauseCtx->cursorX[PAUSE_ITEM] = cursorX;
                            pauseCtx->cursorPoint[PAUSE_ITEM] = cursorPoint;
                            KaleidoScope_MoveCursorToSpecialPos(play, PAUSE_CURSOR_PAGE_LEFT);
                            moveCursorResult = 2;
                        }
                    }
                } else {
                    if (pauseCtx->cursorX[PAUSE_ITEM] <= 4) {
                        pauseCtx->cursorX[PAUSE_ITEM]++;
                        pauseCtx->cursorPoint[PAUSE_ITEM]++;
                        moveCursorResult = 1;
                    } else {
                        pauseCtx->cursorX[PAUSE_ITEM] = cursorX;
                        pauseCtx->cursorY[PAUSE_ITEM]++;
                        if (pauseCtx->cursorY[PAUSE_ITEM] >= 4) {
                            pauseCtx->cursorY[PAUSE_ITEM] = 0;
                        }
                        pauseCtx->cursorPoint[PAUSE_ITEM] =
                            pauseCtx->cursorX[PAUSE_ITEM] + (pauseCtx->cursorY[PAUSE_ITEM] * 6);
                        if (pauseCtx->cursorPoint[PAUSE_ITEM] >= 24) {
                            pauseCtx->cursorPoint[PAUSE_ITEM] = pauseCtx->cursorX[PAUSE_ITEM];
                        }
                        if (cursorY == pauseCtx->cursorY[PAUSE_ITEM]) {
                            pauseCtx->cursorX[PAUSE_ITEM] = cursorX;
                            pauseCtx->cursorPoint[PAUSE_ITEM] = cursorPoint;
                            KaleidoScope_MoveCursorToSpecialPos(play, PAUSE_CURSOR_PAGE_RIGHT);
                            moveCursorResult = 2;
                        }
                    }
                }
            }
        }
    } else if (pauseCtx->cursorSpecialPos == PAUSE_CURSOR_PAGE_LEFT) {
        if (stickX > 30) {
            pauseCtx->nameDisplayTimer = 0;
            pauseCtx->cursorSpecialPos = 0;
            Audio_PlaySfxGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                 &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            cursorPoint = cursorX = cursorY = 0;
            while (true) {
                if (Combo_PauseSlotItem(cursorPoint) != ITEM_NONE) {
                    pauseCtx->cursorPoint[PAUSE_ITEM] = cursorPoint;
                    pauseCtx->cursorX[PAUSE_ITEM] = cursorX;
                    pauseCtx->cursorY[PAUSE_ITEM] = cursorY;
                    break;
                }
                cursorY++;
                cursorPoint += 6;
                if (cursorY < 4) {
                    continue;
                }
                cursorY = 0;
                cursorPoint = cursorX + 1;
                cursorX = cursorPoint;
                if (cursorX < 6) {
                    continue;
                }
                KaleidoScope_MoveCursorToSpecialPos(play, PAUSE_CURSOR_PAGE_RIGHT);
                break;
            }
        }
    } else {
        if (stickX < -30) {
            pauseCtx->nameDisplayTimer = 0;
            pauseCtx->cursorSpecialPos = 0;
            Audio_PlaySfxGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                 &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            cursorPoint = cursorX = 5;
            cursorY = 0;
            while (true) {
                if (Combo_PauseSlotItem(cursorPoint) != ITEM_NONE) {
                    pauseCtx->cursorPoint[PAUSE_ITEM] = cursorPoint;
                    pauseCtx->cursorX[PAUSE_ITEM] = cursorX;
                    pauseCtx->cursorY[PAUSE_ITEM] = cursorY;
                    break;
                }
                cursorY++;
                cursorPoint += 6;
                if (cursorY < 4) {
                    continue;
                }
                cursorY = 0;
                cursorPoint = cursorX - 1;
                cursorX = cursorPoint;
                if (cursorX >= 0) {
                    continue;
                }
                KaleidoScope_MoveCursorToSpecialPos(play, PAUSE_CURSOR_PAGE_LEFT);
                break;
            }
        }
    }

    if (pauseCtx->cursorSpecialPos == 0) {
        if (ABS(stickY) > 30) {
            if ((stickY > 30) && (pauseCtx->cursorY[PAUSE_ITEM] != 0)) {
                pauseCtx->cursorY[PAUSE_ITEM]--;
                pauseCtx->cursorPoint[PAUSE_ITEM] -= 6;
            } else if ((stickY < -30) && (pauseCtx->cursorY[PAUSE_ITEM] < 3)) {
                pauseCtx->cursorY[PAUSE_ITEM]++;
                pauseCtx->cursorPoint[PAUSE_ITEM] += 6;
            }
        }

        pauseCtx->cursorSlot[PAUSE_ITEM] = pauseCtx->cursorPoint[PAUSE_ITEM];
        cursorItem = Combo_PauseSlotItem(pauseCtx->cursorPoint[PAUSE_ITEM]);
        cursorItem = Combo_BombArrowsMmSlotItem(pauseCtx->cursorSlot[PAUSE_ITEM], cursorItem);
        pauseCtx->cursorColorSet = 4; // yellow on an item
        if (cursorItem == ITEM_NONE) {
            cursorItem = PAUSE_ITEM_NONE;
            pauseCtx->cursorColorSet = 0; // MM: the cursor turns white over an empty slot
        }
        pauseCtx->cursorItem[PAUSE_ITEM] = cursorItem;
        KaleidoScope_SetCursorVtx(pauseCtx, pauseCtx->cursorSlot[PAUSE_ITEM] * 4, pauseCtx->itemVtx);

        if ((cursorItem != PAUSE_ITEM_NONE) && (pauseCtx->debugState == 0)) {
            u16 buttonsToCheck = BTN_CLEFT | BTN_CDOWN | BTN_CRIGHT;
            if (CVarGetInteger(CVAR_ENHANCEMENT("DpadEquips"), 0) &&
                (!CVarGetInteger(CVAR_SETTING("DPadOnPause"), 0) || CHECK_BTN_ALL(input->cur.button, BTN_CUP))) {
                buttonsToCheck |= BTN_DUP | BTN_DDOWN | BTN_DLEFT | BTN_DRIGHT;
            }
            if (CHECK_BTN_ALL(input->press.button, BTN_A) && (Combo_PauseTab() == COMBO_PAUSE_TAB_MASKS)) {
                Combo_PauseMaskAPress(cursorItem);
            }
            // 2S2H's bomb arrows with MM's own bow and bombs: A on the bomb slot flips it between the bombs and the
            // bow.
            if (CHECK_BTN_ALL(input->press.button, BTN_A) && Combo_BombArrowsMmCanCycle() &&
                Combo_BombArrowsMmIsBombSlot(pauseCtx->cursorSlot[PAUSE_ITEM])) {
                Audio_PlaySfxGeneral(NA_SE_SY_DECIDE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                     &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                Combo_BombArrowsMmToggleSlot();
            }
            if (CHECK_BTN_ANY(input->press.button, buttonsToCheck)) {
                u8 equipItem;
                u8 equipSlot;
                u16 index = pauseCtx->cursorSlot[PAUSE_ITEM] * 4;
                // A shared item OOT holds goes on as OOT's own copy (and obeys OOT's age rules); an item OOT lacks
                // goes on as the foreign id.
                if (Combo_PauseResolveEquip(cursorItem, &equipItem, &equipSlot) && !CHECK_AGE_REQ_SLOT(equipSlot)) {
                    Audio_PlaySfxGeneral(NA_SE_SY_ERROR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                         &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                } else {
                    KaleidoScope_SetupItemEquip(play, equipItem, equipSlot, pauseCtx->itemVtx[index].v.ob[0] * 10,
                                                pauseCtx->itemVtx[index].v.ob[1] * 10);
                    Combo_BombArrowsEquipQueued(Combo_BombArrowsMmSlotShowsBow() &&
                                                Combo_BombArrowsMmIsBombSlot(pauseCtx->cursorSlot[PAUSE_ITEM]));
                }
            }
        }
    } else {
        pauseCtx->cursorItem[PAUSE_ITEM] = PAUSE_ITEM_NONE;
    }

    if (oldCursorPoint != pauseCtx->cursorPoint[PAUSE_ITEM]) {
        Audio_PlaySfxGeneral(NA_SE_SY_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                             &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    }
}

// The outline quad (quad index `quad` of `extra`) around the slot holding foreign item `item`, if it is on this page.
static void Combo_DrawSlotOutline(GraphicsContext* gfxCtx, PauseContext* pauseCtx, Vtx* extra, s32 quad, u8 item) {
    GraphicsContext* __gfxCtx = gfxCtx; // the caller's open display lists, for POLY_OPA_DISP
    u16 j;

    if (item == ITEM_NONE) {
        return;
    }
    for (j = 0; j < 24; j++) {
        if (Combo_PauseSlotItem(j) == item) {
            Combo_SetQuad(&extra[quad * 4], COMBO_GRID_X(j) - 2, COMBO_GRID_Y(pauseCtx, j) + 2, 32, 32, 32, 32);
            gSPVertex(POLY_OPA_DISP++, &extra[quad * 4], 4, 0);
            POLY_OPA_DISP = KaleidoScope_QuadTextureIA8(POLY_OPA_DISP, gEquippedItemOutlineTex, 32, 32, 0);
            return;
        }
    }
}

// MM's KaleidoScope_DrawItemSelect (mm z_kaleido_item.c) for the foreign tab, on OOT's pause context.
void KaleidoScope_ComboDrawForeignItemSelect(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    u16 i;
    u16 j;
    u16 cursorSlot = pauseCtx->cursorSlot[PAUSE_ITEM];
    Vtx* extra;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_42Opa(play->state.gfxCtx);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);

    pauseCtx->cursorColorSet = 0;
    pauseCtx->nameColorSet = 0;

    if ((pauseCtx->state == 6) && (pauseCtx->unk_1E4 == 0) && (pauseCtx->pageIndex == PAUSE_ITEM)) {
        KaleidoScope_ComboUpdateForeignCursor(play);
        cursorSlot = pauseCtx->cursorSlot[PAUSE_ITEM];
    } else if ((pauseCtx->unk_1E4 == 3) && (pauseCtx->pageIndex == PAUSE_ITEM)) {
        KaleidoScope_SetCursorVtx(pauseCtx, cursorSlot * 4, pauseCtx->itemVtx);
        pauseCtx->cursorColorSet = 4;
    }

    // White outline around the slots whose item sits on a C button / D-pad direction (MM: gEquippedItemOutlineTex).
    // OOT's own outline vertices are placed from cButtonSlots; a foreign button's lives here.
    extra = Graph_Alloc(play->state.gfxCtx, COMBO_OUTLINE_QUADS * 4 * sizeof(Vtx));
    gDPSetCombineLERP(POLY_OPA_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0, PRIMITIVE,
                      ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha);
    gDPSetEnvColor(POLY_OPA_DISP++, 0, 0, 0, 0);
    for (i = 1; i < ARRAY_COUNT(gSaveContext.equips.buttonItems); i++) {
        u8 item = gSaveContext.equips.buttonItems[i];
        if ((i >= 4) && !CVarGetInteger(CVAR_ENHANCEMENT("DpadEquips"), 0)) {
            break;
        }
        if (Combo_PauseIsForeign(item)) {
            Combo_DrawSlotOutline(play->state.gfxCtx, pauseCtx, extra, i - 1,
                                  Combo_BombArrowsMmOutlineItem(i - 1, item));
        }
    }
    // 2S2H's Easy Mask Equip: the mask worn from this menu sits on no button, and gets the same outline.
    Combo_DrawSlotOutline(play->state.gfxCtx, pauseCtx, extra, ARRAY_COUNT(gSaveContext.equips.buttonItems) - 1,
                          Combo_PauseWornWithoutButtonItem());
    // 2S2H's Persistent Bunny Hood: its state shows as a blue outline (PersistentMasks.cpp).
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 100, 200, 255, pauseCtx->alpha);
    Combo_DrawSlotOutline(play->state.gfxCtx, pauseCtx, extra, ARRAY_COUNT(gSaveContext.equips.buttonItems),
                          Combo_PausePersistentBunnyHoodItem());

    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);

    // MM's icons, resolved against MM's ResourceManager.
    gSPComboRMPush(POLY_OPA_DISP++, "mm");
    for (i = j = 0; i < 24; i++, j += 4) {
        u8 item = Combo_BombArrowsMmSlotItem(i, Combo_PauseSlotItem(i));
        const char* tex;
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha);
        if (item == ITEM_NONE) {
            continue;
        }
        tex = Combo_PauseIconTex(item);
        if (tex == NULL) {
            continue;
        }
        if ((pauseCtx->unk_1E4 == 0) && (pauseCtx->pageIndex == PAUSE_ITEM) && (pauseCtx->cursorSpecialPos == 0) &&
            (i == cursorSlot)) {
            // MM grows the selected icon by 2 on each side.
            pauseCtx->itemVtx[j + 0].v.ob[0] = pauseCtx->itemVtx[j + 2].v.ob[0] = pauseCtx->itemVtx[j + 0].v.ob[0] - 2;
            pauseCtx->itemVtx[j + 1].v.ob[0] = pauseCtx->itemVtx[j + 3].v.ob[0] = pauseCtx->itemVtx[j + 0].v.ob[0] + 32;
            pauseCtx->itemVtx[j + 0].v.ob[1] = pauseCtx->itemVtx[j + 1].v.ob[1] = pauseCtx->itemVtx[j + 0].v.ob[1] + 2;
            pauseCtx->itemVtx[j + 2].v.ob[1] = pauseCtx->itemVtx[j + 3].v.ob[1] = pauseCtx->itemVtx[j + 0].v.ob[1] - 32;
        }
        gSPVertex(POLY_OPA_DISP++, &pauseCtx->itemVtx[j + 0], 4, 0);
        KaleidoScope_DrawQuadTextureRGBA32(play->state.gfxCtx, (void*)tex, 32, 32, 0);
    }
    gSPComboRMPop(POLY_OPA_DISP++);

    if (pauseCtx->cursorSpecialPos == 0) {
        KaleidoScope_DrawCursor(play, PAUSE_ITEM);
    }

    // MM's ammo digits (Termina item page): the same 8x8 digit font both games share, under the slot's left corner.
    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetCombineLERP(POLY_OPA_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0, PRIMITIVE,
                      ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
    for (i = 0; i < 24; i++) {
        s32 ammo = Combo_BombArrowsMmSlotAmmo(i, Combo_PauseSlotAmmo(i));
        s32 tens = 0;
        Vtx* digits;
        if (ammo < 0) {
            continue;
        }
        digits = Graph_Alloc(play->state.gfxCtx, 8 * sizeof(Vtx));
        Combo_SetQuad(&digits[0], COMBO_GRID_X(i), COMBO_GRID_Y(pauseCtx, i) - 22, 8, 8, 8, 8);
        Combo_SetQuad(&digits[4], COMBO_GRID_X(i) + 6, COMBO_GRID_Y(pauseCtx, i) - 22, 8, 8, 8, 8);
        gDPPipeSync(POLY_OPA_DISP++);
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha);
        if (ammo == 0) {
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 130, 130, 130, pauseCtx->alpha);
        }
        while (ammo >= 10) {
            ammo -= 10;
            tens++;
        }
        gSPVertex(POLY_OPA_DISP++, digits, 8, 0);
        if (tens != 0) {
            gDPLoadTextureBlock(POLY_OPA_DISP++, ((u8*)_gAmmoDigit0Tex[tens]), G_IM_FMT_IA, G_IM_SIZ_8b, 8, 8, 0,
                                G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK,
                                G_TX_NOLOD, G_TX_NOLOD);
            gSP1Quadrangle(POLY_OPA_DISP++, 0, 2, 3, 1, 0);
        }
        gDPLoadTextureBlock(POLY_OPA_DISP++, ((u8*)_gAmmoDigit0Tex[ammo]), G_IM_FMT_IA, G_IM_SIZ_8b, 8, 8, 0,
                            G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                            G_TX_NOLOD);
        gSP1Quadrangle(POLY_OPA_DISP++, 4, 6, 7, 5, 0);
    }

    // 2S2H's bomb arrows: the bomb slot showing the bow wears a bomb badge.
    if (Combo_BombArrowsMmSlotShowsBow()) {
        for (i = 0; i < 24; i++) {
            if (Combo_BombArrowsMmIsBombSlot(i)) {
                KaleidoScope_ComboDrawBombArrowBadge(play, i, false);
            }
        }
    }

    CLOSE_DISPS(play->state.gfxCtx);
}
#endif
