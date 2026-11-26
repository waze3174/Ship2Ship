/*
 * File: z_kaleido_combo.c
 * Overlay: ovl_kaleido_scope
 * Description: ComboShip merged pause menus — the Hyrule items and Hyrule equipment faces of MM's pause prism (OOT's
 * pages, OOT's live inventory). Logic + exports: mm/2s2h/Rando/MiscBehavior/ComboPause.cpp; encoding:
 * combo/menu/ComboPauseABI.h; deviation record: docs/deviations/ui-menu.md "Merged pause menus".
 *
 * A Hyrule face is OOT's page as OOT draws and drives it: OOT's own page tiles ("SELECT ITEM"), OOT's icons, name
 * plates and ammo digits, and OOT's cursor rules (the cursor only ever rests on an owned item, skipping empty slots).
 * Only the cube, the frame around the page and the buttons are the host's.
 */
#ifdef COMBO_BUILD
#include "z_kaleido_scope.h"
#include "interface/parameter_static/parameter_static.h"
#include "2s2h/Rando/MiscBehavior/ComboPause.h"
#include <libultraship/bridge/consolevariablebridge.h>

extern s16 sEquipState;
extern s16 sEquipMagicArrowSlotHoldTimer;
extern s16 sEquipAnimTimer;
extern u8 sPlayerFormItems[];
extern const char* gAmmoDigitTextures[10];
Gfx* KaleidoScope_DrawPageSections(Gfx* gfx, Vtx* vertices, TexturePtr* textures); // z_kaleido_scope_NES.c

// MM's page tiles are the same 3x5 80x32 IA8 layout as OOT's; the Hyrule tab draws OOT's own.
Gfx* KaleidoScope_ComboDrawItemPageSections(Gfx* gfx, Vtx* vertices, TexturePtr* nativeTextures) {
    const char* const* ootTexs = Combo_MmPausePageTexs();
    if (ootTexs == NULL) {
        return KaleidoScope_DrawPageSections(gfx, vertices, nativeTextures);
    }
    {
        // OOT colours its pages with per-column vertex gradients (TEXEL0 * SHADE; soh func_80823A0C pageColors): a
        // Hyrule page in Termina wears OOT's own colours (item page blue, equipment page green).
        static const u8 sOotPageColors[2][4][3] = {
            { { 10, 50, 80 }, { 70, 100, 130 }, { 70, 100, 130 }, { 10, 50, 80 } },
            { { 10, 50, 40 }, { 90, 100, 60 }, { 90, 100, 60 }, { 10, 50, 40 } },
        };
        s32 page = (Combo_MmPauseTab() == COMBO_MM_PAUSE_TAB_HYRULE_EQUIP) ? 1 : 0;
        Vtx* v = GRAPH_ALLOC(gGameState->gfxCtx, 15 * 4 * sizeof(Vtx));
        s32 i;
        for (i = 0; i < 15 * 4; i++) {
            s32 col = i / 20;
            s32 right = (i & 1);
            v[i] = vertices[i];
            v[i].v.cn[0] = sOotPageColors[page][col + right][0];
            v[i].v.cn[1] = sOotPageColors[page][col + right][1];
            v[i].v.cn[2] = sOotPageColors[page][col + right][2];
        }
        gDPPipeSync(gfx++);
        gDPSetCombineMode(gfx++, G_CC_MODULATEIA, G_CC_MODULATEIA);
        gSPComboRMPush(gfx++, "oot");
        gfx = KaleidoScope_DrawPageSections(gfx, v, (TexturePtr*)ootTexs);
        gSPComboRMPop(gfx++);
        gDPPipeSync(gfx++);
        gDPSetCombineLERP(gfx++, TEXEL0, 0, PRIMITIVE, 0, TEXEL0, 0, SHADE, 0, TEXEL0, 0, PRIMITIVE, 0, TEXEL0, 0,
                          SHADE, 0);
    }
    return gfx;
}

static u16 Combo_EquipTargetForPress(PlayState* play) {
    Input* input = CONTROLLER1(&play->state);
    if (CHECK_BTN_ALL(input->press.button, BTN_CLEFT)) {
        return PAUSE_EQUIP_C_LEFT;
    } else if (CHECK_BTN_ALL(input->press.button, BTN_CDOWN)) {
        return PAUSE_EQUIP_C_DOWN;
    } else if (CHECK_BTN_ALL(input->press.button, BTN_CRIGHT)) {
        return PAUSE_EQUIP_C_RIGHT;
    } else if (CVarGetInteger("gEnhancements.Dpad.DpadEquips", 0)) {
        if (CHECK_BTN_ALL(input->press.button, BTN_DRIGHT)) {
            return PAUSE_EQUIP_D_RIGHT;
        } else if (CHECK_BTN_ALL(input->press.button, BTN_DLEFT)) {
            return PAUSE_EQUIP_D_LEFT;
        } else if (CHECK_BTN_ALL(input->press.button, BTN_DDOWN)) {
            return PAUSE_EQUIP_D_DOWN;
        } else if (CHECK_BTN_ALL(input->press.button, BTN_DUP)) {
            return PAUSE_EQUIP_D_UP;
        }
    }
    return 0xFFFF;
}

// The item on an equip target (human layout, as MM's own equip code reads it).
static u8 Combo_ItemOnEquipTarget(u16 target) {
    static const u8 sDpad[] = { EQUIP_SLOT_D_RIGHT, EQUIP_SLOT_D_LEFT, EQUIP_SLOT_D_DOWN, EQUIP_SLOT_D_UP };
    if (target <= PAUSE_EQUIP_C_RIGHT) {
        return BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_C_LEFT + target);
    }
    return DPAD_BUTTON_ITEM_EQUIP(0, sDpad[target - PAUSE_EQUIP_D_RIGHT]);
}

// MM: the mask in use (a transformation, or the mask Link wears) can't be swapped off its button.
static s32 Combo_EquipTargetLocked(PlayState* play, u16 target) {
    u8 onTarget = Combo_ItemOnEquipTarget(target);
    return ((GET_PLAYER_FORM != PLAYER_FORM_HUMAN) && (sPlayerFormItems[GET_PLAYER_FORM] == onTarget)) ||
           ((Player_GetCurMaskItemId(play) != ITEM_NONE) && (Player_GetCurMaskItemId(play) == onTarget));
}

// MM's equip start (KaleidoScope_UpdateItemCursor): the icon at (fromX, fromY) on the page flies to `target`, and
// KaleidoScope_UpdateItemEquip puts `item` / `slot` on it.
static void Combo_StartEquipAnim(PlayState* play, u16 target, u8 item, u8 slot, s16 fromX, s16 fromY) {
    PauseContext* pauseCtx = &play->pauseCtx;

    pauseCtx->equipTargetCBtn = target;
    pauseCtx->equipTargetItem = item;
    pauseCtx->equipTargetSlot = slot;
    pauseCtx->mainState = PAUSE_MAIN_STATE_EQUIP_ITEM;
    pauseCtx->equipAnimX = fromX * 10;
    pauseCtx->equipAnimY = fromY * 10;
    pauseCtx->equipAnimAlpha = 255;
    sEquipMagicArrowSlotHoldTimer = 0;
    sEquipState = EQUIP_STATE_MOVE_TO_C_BTN;
    sEquipAnimTimer = 10;
}

// ---- the Hyrule equipment face: OOT's equipment page (KaleidoScope_DrawEquipment's grid), drawn and driven as OOT
// does it: row = sword / shield / tunic / boots, column 0 = that row's upgrade, columns 1-3 = the pieces; the cursor
// rests only on what Link owns; the equipped piece of each row is outlined. A puts a tunic or boots on (OOT's save);
// with SoH's "Assignable Tunics and Boots" a C button or D-pad direction takes it instead. Swords and shields are MM's
// own in Termina (MM's error sound).
static const s16 sComboEquipColX[] = { -114, 12, 44, 76 }; // OOT's D_8082B12C

static s32 Combo_EquipOwned(s32 slot) {
    return Combo_MmPauseEquipItem(slot) != 0xFF;
}

static void KaleidoScope_ComboUpdateEquipCursor(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    Input* input = CONTROLLER1(&play->state);
    s16 oldCursorPoint = pauseCtx->cursorPoint[PAUSE_ITEM];
    s16 x = pauseCtx->cursorXIndex[PAUSE_ITEM] & 3;
    s16 y = pauseCtx->cursorYIndex[PAUSE_ITEM] & 3;
    s16 i;

    pauseCtx->cursorColorSet = PAUSE_CURSOR_COLOR_SET_WHITE;
    pauseCtx->nameColorSet = PAUSE_NAME_COLOR_SET_WHITE;
    if (!((pauseCtx->state == PAUSE_STATE_MAIN) && (pauseCtx->mainState == PAUSE_MAIN_STATE_IDLE) &&
          (pauseCtx->pageIndex == PAUSE_ITEM))) {
        return;
    }

    if (pauseCtx->cursorSpecialPos == 0) {
        if (!Combo_EquipOwned(y * 4 + x)) {
            pauseCtx->stickAdjX = 40; // OOT: off an empty slot at once
        }
        if (pauseCtx->stickAdjX < -30) {
            for (i = x - 1; i >= 0; i--) {
                if (Combo_EquipOwned(y * 4 + i)) {
                    break;
                }
            }
            if (i >= 0) {
                x = i;
            } else {
                KaleidoScope_MoveCursorToSpecialPos(play, PAUSE_CURSOR_PAGE_LEFT);
            }
        } else if (pauseCtx->stickAdjX > 30) {
            for (i = x + 1; i < 4; i++) {
                if (Combo_EquipOwned(y * 4 + i)) {
                    break;
                }
            }
            if (i < 4) {
                x = i;
            } else {
                KaleidoScope_MoveCursorToSpecialPos(play, PAUSE_CURSOR_PAGE_RIGHT);
            }
        }
        if ((pauseCtx->cursorSpecialPos == 0) && (ABS_ALT(pauseCtx->stickAdjY) > 30)) {
            s16 dir = (pauseCtx->stickAdjY > 30) ? -1 : 1;
            for (i = y + dir; (i >= 0) && (i < 4); i += dir) {
                if (Combo_EquipOwned(i * 4 + x)) {
                    y = i;
                    break;
                }
            }
        }
    } else {
        s16 enterFromLeft = (pauseCtx->cursorSpecialPos == PAUSE_CURSOR_PAGE_LEFT);
        if ((enterFromLeft && (pauseCtx->stickAdjX > 30)) || (!enterFromLeft && (pauseCtx->stickAdjX < -30))) {
            s16 col;
            s16 found = false;
            KaleidoScope_MoveCursorFromSpecialPos(play);
            for (col = enterFromLeft ? 0 : 3; (col >= 0) && (col < 4) && !found; col += enterFromLeft ? 1 : -1) {
                for (i = 0; i < 4; i++) {
                    if (Combo_EquipOwned(i * 4 + col)) {
                        x = col;
                        y = i;
                        found = true;
                        break;
                    }
                }
            }
            if (!found) {
                KaleidoScope_MoveCursorToSpecialPos(play,
                                                    enterFromLeft ? PAUSE_CURSOR_PAGE_RIGHT : PAUSE_CURSOR_PAGE_LEFT);
            }
        }
    }

    if (pauseCtx->cursorSpecialPos == 0) {
        pauseCtx->cursorXIndex[PAUSE_ITEM] = x;
        pauseCtx->cursorYIndex[PAUSE_ITEM] = y;
        pauseCtx->cursorPoint[PAUSE_ITEM] = pauseCtx->cursorSlot[PAUSE_ITEM] = y * 4 + x;
        pauseCtx->cursorColorSet = PAUSE_CURSOR_COLOR_SET_YELLOW;
        pauseCtx->cursorItem[PAUSE_ITEM] =
            Combo_EquipOwned(y * 4 + x) ? (COMBO_MM_PAUSE_EQUIP_CURSOR_BASE + y * 4 + x) : PAUSE_ITEM_NONE;
        if (pauseCtx->cursorItem[PAUSE_ITEM] != PAUSE_ITEM_NONE) {
            u16 target = Combo_EquipTargetForPress(play);
            if ((target != 0xFFFF) && Combo_MmPauseGearOnButtons() && (y >= 2) && (x >= 1)) {
                // SoH's "Assignable Tunics and Boots" (z_kaleido_equipment.c): a C button or D-pad direction takes
                // the tunic or boots, when Link owns it and his age may wear it.
                s32 gear = Combo_MmPauseEquipGearId(y * 4 + x);
                if ((gear == ITEM_NONE) || Combo_EquipTargetLocked(play, target)) {
                    Audio_PlaySfx(NA_SE_SY_ERROR);
                } else {
                    Combo_StartEquipAnim(play, target, gear, gear, sComboEquipColX[x] + 2,
                                         58 - y * 32 + pauseCtx->offsetY - 2);
                    Audio_PlaySfx(NA_SE_SY_DECIDE);
                }
            } else if (CHECK_BTN_ANY(input->press.button, BTN_A | BTN_CLEFT | BTN_CDOWN | BTN_CRIGHT)) {
                Combo_MmPauseEquipUse(y * 4 + x);
            }
        }
    } else {
        pauseCtx->cursorItem[PAUSE_ITEM] = PAUSE_ITEM_NONE;
    }

    if (oldCursorPoint != pauseCtx->cursorPoint[PAUSE_ITEM]) {
        Audio_PlaySfx(NA_SE_SY_CURSOR);
    }
}

static void Combo_EquipQuad(Vtx* v, s16 x, s16 y, s16 size) {
    s32 k;
    for (k = 0; k < 4; k++) {
        v[k].v.ob[0] = x + ((k & 1) ? size : 0);
        v[k].v.ob[1] = y - ((k & 2) ? size : 0);
        v[k].v.ob[2] = 0;
        v[k].v.flag = 0;
        v[k].v.tc[0] = (k & 1) ? (32 << 5) : 0;
        v[k].v.tc[1] = (k & 2) ? (32 << 5) : 0;
        v[k].v.cn[0] = v[k].v.cn[1] = v[k].v.cn[2] = v[k].v.cn[3] = 255;
    }
}

static void KaleidoScope_ComboDrawEquip(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    Vtx* v = GRAPH_ALLOC(play->state.gfxCtx, 16 * 4 * sizeof(Vtx));
    Vtx* outline = GRAPH_ALLOC(play->state.gfxCtx, 4 * 4 * sizeof(Vtx));
    s16 row;
    s16 col;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL42_Opa(play->state.gfxCtx);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha);

    // OOT's equipped outline behind each row's current piece.
    for (row = 0; row < 4; row++) {
        s16 eq = Combo_MmPauseEquipEquipped(row);
        if ((eq >= 1) && (eq <= 3)) {
            Combo_EquipQuad(&outline[row * 4], sComboEquipColX[eq], 58 - row * 32 + pauseCtx->offsetY, 32);
            gSPVertex(POLY_OPA_DISP++, &outline[row * 4], 4, 0);
            POLY_OPA_DISP = Gfx_DrawTexQuadIA8(POLY_OPA_DISP, gEquippedItemOutlineTex, 32, 32, 0);
        }
    }

    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gSPComboRMPush(POLY_OPA_DISP++, "oot");
    for (row = 0; row < 4; row++) {
        for (col = 0; col < 4; col++) {
            s16 slot = row * 4 + col;
            s32 item = Combo_MmPauseEquipItem(slot);
            const char* tex = Combo_MmPauseOotIconTex(item);
            s16 grow = (pauseCtx->mainState == PAUSE_MAIN_STATE_IDLE) && (pauseCtx->pageIndex == PAUSE_ITEM) &&
                       (pauseCtx->cursorSpecialPos == 0) && (slot == pauseCtx->cursorSlot[PAUSE_ITEM]);
            s32 unusable;
            if ((item == 0xFF) || (tex == NULL)) {
                continue;
            }
            Combo_EquipQuad(&v[slot * 4], sComboEquipColX[col] + 2 - (grow ? 2 : 0),
                            58 - row * 32 + pauseCtx->offsetY - 2 + (grow ? 2 : 0), grow ? 32 : 28);
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha);
            // Greyed as MM greys what Link can't use: the swords and shields (MM's own are worn in Termina), and a
            // tunic or boots Link's age may not wear (Combo_MmPauseEquipUse refuses both).
            unusable = (col >= 1) && !Combo_MmPauseEquipUsable(slot);
            if (unusable) {
                gDPSetGrayscaleColor(POLY_OPA_DISP++, 109, 109, 109, 255);
                gSPGrayscale(POLY_OPA_DISP++, true);
            }
            gSPVertex(POLY_OPA_DISP++, &v[slot * 4], 4, 0);
            KaleidoScope_DrawTexQuadRGBA32(play->state.gfxCtx, (TexturePtr)tex, 32, 32, 0);
            if (unusable) {
                gSPGrayscale(POLY_OPA_DISP++, false);
            }
        }
    }
    gSPComboRMPop(POLY_OPA_DISP++);

    CLOSE_DISPS(play->state.gfxCtx);
}

// OOT's KaleidoScope_DrawItemSelect cursor half (soh z_kaleido_item.c), on MM's pause context.
void KaleidoScope_ComboUpdateForeignItemCursor(PlayState* play) {
    PauseContext* pauseCtx = &play->pauseCtx;
    Input* input = CONTROLLER1(&play->state);
    u16 cursorItem;
    s16 cursorPoint;
    s16 cursorX;
    s16 cursorY;
    s16 oldCursorPoint;
    s16 moveCursorResult;

    if (Combo_MmPauseTab() == COMBO_MM_PAUSE_TAB_HYRULE_EQUIP) {
        KaleidoScope_ComboUpdateEquipCursor(play);
        return;
    }

    pauseCtx->cursorColorSet = PAUSE_CURSOR_COLOR_SET_WHITE;
    pauseCtx->nameColorSet = PAUSE_NAME_COLOR_SET_WHITE;

    if (!((pauseCtx->state == PAUSE_STATE_MAIN) && (pauseCtx->mainState == PAUSE_MAIN_STATE_IDLE) &&
          (pauseCtx->pageIndex == PAUSE_ITEM) && !pauseCtx->itemDescriptionOn)) {
        if ((pauseCtx->mainState == PAUSE_MAIN_STATE_EQUIP_ITEM) && (pauseCtx->pageIndex == PAUSE_ITEM)) {
            pauseCtx->cursorColorSet = PAUSE_CURSOR_COLOR_SET_YELLOW;
        }
        return;
    }

    moveCursorResult = 0;
    oldCursorPoint = pauseCtx->cursorPoint[PAUSE_ITEM];
    cursorItem = pauseCtx->cursorItem[PAUSE_ITEM];

    if (pauseCtx->cursorSpecialPos == 0) {
        pauseCtx->cursorColorSet = PAUSE_CURSOR_COLOR_SET_YELLOW;
        if (cursorItem == PAUSE_ITEM_NONE) {
            pauseCtx->stickAdjX = 40; // OOT: off an empty slot at once
        }
        if (ABS_ALT(pauseCtx->stickAdjX) > 30) {
            cursorPoint = pauseCtx->cursorPoint[PAUSE_ITEM];
            cursorX = pauseCtx->cursorXIndex[PAUSE_ITEM];
            cursorY = pauseCtx->cursorYIndex[PAUSE_ITEM];
            while (moveCursorResult == 0) {
                if (pauseCtx->stickAdjX < -30) {
                    if (pauseCtx->cursorXIndex[PAUSE_ITEM] != 0) {
                        pauseCtx->cursorXIndex[PAUSE_ITEM]--;
                        pauseCtx->cursorPoint[PAUSE_ITEM]--;
                        if (Combo_MmPauseSlotItem(pauseCtx->cursorPoint[PAUSE_ITEM]) != ITEM_NONE) {
                            moveCursorResult = 1;
                        }
                    } else {
                        pauseCtx->cursorXIndex[PAUSE_ITEM] = cursorX;
                        pauseCtx->cursorYIndex[PAUSE_ITEM]++;
                        if (pauseCtx->cursorYIndex[PAUSE_ITEM] >= 4) {
                            pauseCtx->cursorYIndex[PAUSE_ITEM] = 0;
                        }
                        pauseCtx->cursorPoint[PAUSE_ITEM] =
                            pauseCtx->cursorXIndex[PAUSE_ITEM] + (pauseCtx->cursorYIndex[PAUSE_ITEM] * 6);
                        if (pauseCtx->cursorPoint[PAUSE_ITEM] >= 24) {
                            pauseCtx->cursorPoint[PAUSE_ITEM] = pauseCtx->cursorXIndex[PAUSE_ITEM];
                        }
                        if (cursorY == pauseCtx->cursorYIndex[PAUSE_ITEM]) {
                            pauseCtx->cursorXIndex[PAUSE_ITEM] = cursorX;
                            pauseCtx->cursorPoint[PAUSE_ITEM] = cursorPoint;
                            KaleidoScope_MoveCursorToSpecialPos(play, PAUSE_CURSOR_PAGE_LEFT);
                            moveCursorResult = 2;
                        }
                    }
                } else {
                    if (pauseCtx->cursorXIndex[PAUSE_ITEM] < 5) {
                        pauseCtx->cursorXIndex[PAUSE_ITEM]++;
                        pauseCtx->cursorPoint[PAUSE_ITEM]++;
                        if (Combo_MmPauseSlotItem(pauseCtx->cursorPoint[PAUSE_ITEM]) != ITEM_NONE) {
                            moveCursorResult = 1;
                        }
                    } else {
                        pauseCtx->cursorXIndex[PAUSE_ITEM] = cursorX;
                        pauseCtx->cursorYIndex[PAUSE_ITEM]++;
                        if (pauseCtx->cursorYIndex[PAUSE_ITEM] >= 4) {
                            pauseCtx->cursorYIndex[PAUSE_ITEM] = 0;
                        }
                        pauseCtx->cursorPoint[PAUSE_ITEM] =
                            pauseCtx->cursorXIndex[PAUSE_ITEM] + (pauseCtx->cursorYIndex[PAUSE_ITEM] * 6);
                        if (pauseCtx->cursorPoint[PAUSE_ITEM] >= 24) {
                            pauseCtx->cursorPoint[PAUSE_ITEM] = pauseCtx->cursorXIndex[PAUSE_ITEM];
                        }
                        if (cursorY == pauseCtx->cursorYIndex[PAUSE_ITEM]) {
                            pauseCtx->cursorXIndex[PAUSE_ITEM] = cursorX;
                            pauseCtx->cursorPoint[PAUSE_ITEM] = cursorPoint;
                            KaleidoScope_MoveCursorToSpecialPos(play, PAUSE_CURSOR_PAGE_RIGHT);
                            moveCursorResult = 2;
                        }
                    }
                }
            }
            if (moveCursorResult == 1) {
                cursorItem = Combo_MmPauseSlotItem(pauseCtx->cursorPoint[PAUSE_ITEM]);
            }
        }
    } else if (pauseCtx->cursorSpecialPos == PAUSE_CURSOR_PAGE_LEFT) {
        if (pauseCtx->stickAdjX > 30) {
            KaleidoScope_MoveCursorFromSpecialPos(play);
            cursorPoint = cursorX = cursorY = 0;
            while (true) {
                if (Combo_MmPauseSlotItem(cursorPoint) != ITEM_NONE) {
                    pauseCtx->cursorPoint[PAUSE_ITEM] = cursorPoint;
                    pauseCtx->cursorXIndex[PAUSE_ITEM] = cursorX;
                    pauseCtx->cursorYIndex[PAUSE_ITEM] = cursorY;
                    moveCursorResult = 1;
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
        if (pauseCtx->stickAdjX < -30) {
            KaleidoScope_MoveCursorFromSpecialPos(play);
            cursorPoint = cursorX = 5;
            cursorY = 0;
            while (true) {
                if (Combo_MmPauseSlotItem(cursorPoint) != ITEM_NONE) {
                    pauseCtx->cursorPoint[PAUSE_ITEM] = cursorPoint;
                    pauseCtx->cursorXIndex[PAUSE_ITEM] = cursorX;
                    pauseCtx->cursorYIndex[PAUSE_ITEM] = cursorY;
                    moveCursorResult = 1;
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
        if ((cursorItem != PAUSE_ITEM_NONE) && (ABS_ALT(pauseCtx->stickAdjY) > 30)) {
            moveCursorResult = 0;
            cursorPoint = pauseCtx->cursorPoint[PAUSE_ITEM];
            cursorY = pauseCtx->cursorYIndex[PAUSE_ITEM];
            while (moveCursorResult == 0) {
                if (pauseCtx->stickAdjY > 30) {
                    if (pauseCtx->cursorYIndex[PAUSE_ITEM] != 0) {
                        pauseCtx->cursorYIndex[PAUSE_ITEM]--;
                        pauseCtx->cursorPoint[PAUSE_ITEM] -= 6;
                        if (Combo_MmPauseSlotItem(pauseCtx->cursorPoint[PAUSE_ITEM]) != ITEM_NONE) {
                            moveCursorResult = 1;
                        }
                    } else {
                        pauseCtx->cursorYIndex[PAUSE_ITEM] = cursorY;
                        pauseCtx->cursorPoint[PAUSE_ITEM] = cursorPoint;
                        moveCursorResult = 2;
                    }
                } else {
                    if (pauseCtx->cursorYIndex[PAUSE_ITEM] < 3) {
                        pauseCtx->cursorYIndex[PAUSE_ITEM]++;
                        pauseCtx->cursorPoint[PAUSE_ITEM] += 6;
                        if (Combo_MmPauseSlotItem(pauseCtx->cursorPoint[PAUSE_ITEM]) != ITEM_NONE) {
                            moveCursorResult = 1;
                        }
                    } else {
                        pauseCtx->cursorYIndex[PAUSE_ITEM] = cursorY;
                        pauseCtx->cursorPoint[PAUSE_ITEM] = cursorPoint;
                        moveCursorResult = 2;
                    }
                }
            }
        }

        pauseCtx->cursorColorSet = PAUSE_CURSOR_COLOR_SET_YELLOW;
        cursorItem = Combo_MmPauseSlotItem(pauseCtx->cursorPoint[PAUSE_ITEM]);
        if (cursorItem == ITEM_NONE) {
            cursorItem = PAUSE_ITEM_NONE;
        }
        pauseCtx->cursorItem[PAUSE_ITEM] = cursorItem;
        pauseCtx->cursorSlot[PAUSE_ITEM] = pauseCtx->cursorPoint[PAUSE_ITEM];

        if ((cursorItem != PAUSE_ITEM_NONE) && (pauseCtx->debugEditor == DEBUG_EDITOR_NONE)) {
            u16 target = Combo_EquipTargetForPress(play);
            if (target != 0xFFFF) {
                u8 equipItem;
                u8 equipSlot;
                u16 vtxIndex = pauseCtx->cursorSlot[PAUSE_ITEM] * 4;

                if (Combo_EquipTargetLocked(play, target) || !Combo_MmPauseEquipAllowed(cursorItem)) {
                    Audio_PlaySfx(NA_SE_SY_ERROR);
                    return;
                }
                // A shared item MM holds goes on as MM's own copy; an item MM lacks goes on as the foreign id.
                Combo_MmPauseResolveEquip(cursorItem, &equipItem, &equipSlot);
                Combo_StartEquipAnim(play, target, equipItem, equipSlot, pauseCtx->itemVtx[vtxIndex].v.ob[0],
                                     pauseCtx->itemVtx[vtxIndex].v.ob[1]);
                if ((equipItem == ITEM_ARROW_FIRE) || (equipItem == ITEM_ARROW_ICE) ||
                    (equipItem == ITEM_ARROW_LIGHT)) {
                    u8 magicArrowIndex = (equipItem == ITEM_ARROW_FIRE) ? 0 : ((equipItem == ITEM_ARROW_ICE) ? 1 : 2);
                    Audio_PlaySfx(NA_SE_SY_SET_FIRE_ARROW + magicArrowIndex);
                    pauseCtx->equipTargetItem = 0xB5 + magicArrowIndex;
                    pauseCtx->equipAnimAlpha = sEquipState = EQUIP_STATE_MAGIC_ARROW_GROW_ORB;
                    sEquipAnimTimer = 6;
                } else {
                    Audio_PlaySfx(NA_SE_SY_DECIDE);
                }
            }
        }
    } else {
        pauseCtx->cursorItem[PAUSE_ITEM] = PAUSE_ITEM_NONE;
    }

    if (oldCursorPoint != pauseCtx->cursorPoint[PAUSE_ITEM]) {
        Audio_PlaySfx(NA_SE_SY_CURSOR);
    }
}

// OOT's KaleidoScope_DrawItemSelect draw half, on MM's pause context.
void KaleidoScope_ComboDrawForeignItemSelect(PlayState* play) {
    static const s16 sAmmoRowTop[] = { 85, 117, 150, 182 }; // MM's ammo digit rows (sAmmoRectHeight)
    PauseContext* pauseCtx = &play->pauseCtx;
    u16 i;
    u16 j;

    if (Combo_MmPauseTab() == COMBO_MM_PAUSE_TAB_HYRULE_EQUIP) {
        KaleidoScope_ComboDrawEquip(play);
        return;
    }

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL42_Opa(play->state.gfxCtx);

    // OOT's equipped outline around the slots whose item sits on a C button / D-pad direction.
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha);
    for (i = 0; i < 24; i++) {
        u8 item = Combo_MmPauseSlotItem(i);
        bool equipped = false;
        if (item == ITEM_NONE) {
            continue;
        }
        for (j = EQUIP_SLOT_C_LEFT; j <= EQUIP_SLOT_C_RIGHT; j++) {
            equipped |= (BUTTON_ITEM_EQUIP(0, j) == item);
        }
        if (CVarGetInteger("gEnhancements.Dpad.DpadEquips", 0)) {
            for (j = EQUIP_SLOT_D_RIGHT; j <= EQUIP_SLOT_D_UP; j++) {
                equipped |= (DPAD_BUTTON_ITEM_EQUIP(0, j) == item);
            }
        }
        if (equipped) {
            Vtx* v = GRAPH_ALLOC(play->state.gfxCtx, 4 * sizeof(Vtx));
            s16 x = pauseCtx->itemVtx[i * 4].v.ob[0] - 2;
            s16 y = pauseCtx->itemVtx[i * 4].v.ob[1] + 2;
            s32 k;
            for (k = 0; k < 4; k++) {
                v[k].v.ob[0] = x + ((k & 1) ? 32 : 0);
                v[k].v.ob[1] = y - ((k & 2) ? 32 : 0);
                v[k].v.ob[2] = 0;
                v[k].v.flag = 0;
                v[k].v.tc[0] = (k & 1) ? (32 << 5) : 0;
                v[k].v.tc[1] = (k & 2) ? (32 << 5) : 0;
                v[k].v.cn[0] = v[k].v.cn[1] = v[k].v.cn[2] = v[k].v.cn[3] = 255;
            }
            gSPVertex(POLY_OPA_DISP++, v, 4, 0);
            POLY_OPA_DISP = Gfx_DrawTexQuadIA8(POLY_OPA_DISP, gEquippedItemOutlineTex, 32, 32, 0);
        }
    }

    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);

    // OOT's icons, resolved against OOT's ResourceManager.
    gSPComboRMPush(POLY_OPA_DISP++, "oot");
    for (j = 0, i = 0; i < 24; i++, j += 4) {
        u8 item = Combo_MmPauseSlotItem(i);
        const char* tex;
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha);
        if (item == ITEM_NONE) {
            continue;
        }
        tex = Combo_MmPauseIconTex(item);
        if (tex == NULL) {
            continue;
        }
        if ((pauseCtx->mainState == PAUSE_MAIN_STATE_IDLE) && (pauseCtx->pageIndex == PAUSE_ITEM) &&
            (pauseCtx->cursorSpecialPos == 0) && (i == pauseCtx->cursorSlot[PAUSE_ITEM])) {
            pauseCtx->itemVtx[j + 0].v.ob[0] = pauseCtx->itemVtx[j + 2].v.ob[0] = pauseCtx->itemVtx[j + 0].v.ob[0] - 2;
            pauseCtx->itemVtx[j + 1].v.ob[0] = pauseCtx->itemVtx[j + 3].v.ob[0] = pauseCtx->itemVtx[j + 0].v.ob[0] + 32;
            pauseCtx->itemVtx[j + 0].v.ob[1] = pauseCtx->itemVtx[j + 1].v.ob[1] = pauseCtx->itemVtx[j + 0].v.ob[1] + 2;
            pauseCtx->itemVtx[j + 2].v.ob[1] = pauseCtx->itemVtx[j + 3].v.ob[1] = pauseCtx->itemVtx[j + 0].v.ob[1] - 32;
        }
        gSPVertex(POLY_OPA_DISP++, &pauseCtx->itemVtx[j + 0], 4, 0);
        KaleidoScope_DrawTexQuadRGBA32(play->state.gfxCtx, (TexturePtr)tex, 32, 32, 0);
    }
    gSPComboRMPop(POLY_OPA_DISP++);

    // OOT's ammo digits (the 8x8 digit font both games share), where MM places its own.
    if ((pauseCtx->pageIndex == PAUSE_ITEM) && (pauseCtx->state == PAUSE_STATE_MAIN) &&
        ((pauseCtx->mainState == PAUSE_MAIN_STATE_IDLE) || (pauseCtx->mainState == PAUSE_MAIN_STATE_EQUIP_ITEM))) {
        Gfx_SetupDL39_Opa(play->state.gfxCtx);
        gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
        for (i = 0; i < 24; i++) {
            s32 ammo = Combo_MmPauseSlotAmmo(i);
            s16 left = 62 + (i % 6) * 33;
            s16 top = sAmmoRowTop[i / 6];
            s32 tens = 0;
            if (ammo < 0) {
                continue;
            }
            gDPPipeSync(POLY_OPA_DISP++);
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, pauseCtx->alpha);
            if (ammo == 0) {
                gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 130, 130, 130, pauseCtx->alpha);
            }
            while (ammo >= 10) {
                ammo -= 10;
                tens++;
            }
            if (tens != 0) {
                POLY_OPA_DISP = Gfx_DrawTexRectIA8(POLY_OPA_DISP, gAmmoDigitTextures[tens], 8, 8, left, top, 8, 8,
                                                   1 << 10, 1 << 10);
            }
            POLY_OPA_DISP = Gfx_DrawTexRectIA8(POLY_OPA_DISP, gAmmoDigitTextures[ammo], 8, 8, left + 6, top, 8, 8,
                                               1 << 10, 1 << 10);
        }
        Gfx_SetupDL42_Opa(play->state.gfxCtx);
    }

    CLOSE_DISPS(play->state.gfxCtx);
}
#endif
