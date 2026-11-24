// soh/soh/Enhancements/randomizer/ComboPicto.cpp — ComboShip: MM's Pictograph Box in Hyrule (owner rule: MM items in
// Hyrule are a faithful PORT of MM's real code: models, sounds, HUD).
//
// Ported from (2S2H, mm/):
//  - src/code/z_parameter.c  PictoBoxState, sPictoState / sPictoPhotoBeingTaken, the pictograph branches of
//                            Interface_UpdateButtonsPart1 (shutter, keep / discard prompt, re-showing a held photo)
//                            and Interface_Draw's pictograph parts (focus frame, photo strips).
//  - src/code/z_play.c       Play_TakePictoPhoto, Play_ConvertRgba16ToIntensityImage (8-bit path),
//                            Play_CompressI8ToI5, Play_DecompressI5ToI8, the R_PICTO_PHOTO_STATE machine and 2S2H's
//                            CPU capture (FB_WriteFramebufferSliceToCPU, the same helper soh/framebuffer_effects.c
//                            has).
//  - 2s2h/Enhancements/Items/ColorPictograph.cpp  VB_PICTO_TAKE / VB_PICTO_ACTIVATE / VB_PICTO_DISPLAY, through MM's
//                            own buffer and CVar (MM_ComboPicto_Color* exports appended to that file).
//
// The photo IS MM's photo: it is stored in MM's resident save (gSaveContext.pictoPhotoI5 + QUEST_PICTOGRAPH) through
// MM_ComboPicto_* exports, so a photo taken in Hyrule is the one Termina shows and vice versa. The focus-frame
// textures come from MM's ResourceManager ("mm" bracket); the sounds play through MM's audio engine.
//
// Differences that are not 1:1 (OOT has no equivalent; docs in the commit / report):
//  - the keep / discard prompt is MM's message 0xF8 (read from MM's table by Combo_MmTextPrepare) in OOT's textbox;
//  - Snap_RecordPictographedActors: no MM quest subject exists in Hyrule, so it reduces to clearing pictoFlags0/1;
//  - MM's swamp-cruise cutscene gate (EVENTINF_41) and the VRU "cheese" shutter word have no Hyrule counterpart;
//  - MM's PLAYER_STATE1_200 actor-freeze bit is not set (OOT's bit 9 means something else); haltAllActors, which MM
//    also sets, already freezes every actor;
//  - MM's A-button "Snap" label: OOT's do_action_static has no such texture (OOT's A label is left as is).
#ifdef COMBO_BUILD

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory> // z64.h pulls <memory> in C++; include it before the extern "C" block

#include "ComboResolve.h"
#include "ComboForms.h" // Combo_GuestPlaySfx, Combo_DebugEnv, Combo_DebugLog
#include "ComboPicto.h"
#include "soh/frame_interpolation.h" // OPEN_DISPS

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
#include "functions.h"
#include "soh/framebuffer_effects.h"
u16 Combo_MmTextPrepare(u16 mmTextId); // soh/soh/Enhancements/combo/ComboMmText.cpp
}

namespace {

// mm/include/z64save.h
constexpr s32 PICTO_PHOTO_WIDTH = 160;
constexpr s32 PICTO_PHOTO_HEIGHT = 112;
constexpr s32 PICTO_PHOTO_TOPLEFT_X = (SCREEN_WIDTH - PICTO_PHOTO_WIDTH) / 2;
constexpr s32 PICTO_PHOTO_TOPLEFT_Y = (SCREEN_HEIGHT - PICTO_PHOTO_HEIGHT) / 2;
constexpr s32 PICTO_PHOTO_SIZE = PICTO_PHOTO_WIDTH * PICTO_PHOTO_HEIGHT;
constexpr s32 PICTO_PHOTO_COMPRESSED_SIZE = PICTO_PHOTO_SIZE * 5 / 8;

// mm/src/code/z_parameter.c
enum PictoBoxState {
    PICTO_BOX_STATE_OFF,         // Not using the pictograph
    PICTO_BOX_STATE_LENS,        // Looking through the lens of the pictograph
    PICTO_BOX_STATE_SETUP_PHOTO, // Looking at the photo currently taken
    PICTO_BOX_STATE_PHOTO
};
// mm/include/z64play.h (R_PICTO_PHOTO_STATE)
enum PictoPhotoState {
    PICTO_PHOTO_STATE_OFF,
    PICTO_PHOTO_STATE_SETUP,
    PICTO_PHOTO_STATE_PROCESS,
    PICTO_PHOTO_STATE_READY
};

// mm/include/sfx.h
constexpr u16 MM_NA_SE_SY_ERROR = 0x4806;
constexpr u16 MM_NA_SE_SY_DECIDE = 0x4808;
constexpr u16 MM_NA_SE_SY_CANCEL = 0x480A;
constexpr u16 MM_NA_SE_SY_CAMERA_ZOOM_UP = 0x4813;
constexpr u16 MM_NA_SE_SY_CAMERA_SHUTTER = 0x4850;

constexpr u16 MM_TEXT_PICTO_KEEP = 0xF8; // "Keep this picture?" two-choice

// mm/src/code/title_setup.c (R_PICTO_FOCUS_* defaults)
constexpr s32 R_PICTO_FOCUS_BORDER_TOPLEFT_X = 80;
constexpr s32 R_PICTO_FOCUS_BORDER_TOPLEFT_Y = 60;
constexpr s32 R_PICTO_FOCUS_BORDER_TOPRIGHT_X = 220;
constexpr s32 R_PICTO_FOCUS_BORDER_TOPRIGHT_Y = 60;
constexpr s32 R_PICTO_FOCUS_BORDER_BOTTOMLEFT_X = 80;
constexpr s32 R_PICTO_FOCUS_BORDER_BOTTOMLEFT_Y = 160;
constexpr s32 R_PICTO_FOCUS_BORDER_BOTTOMRIGHT_X = 220;
constexpr s32 R_PICTO_FOCUS_BORDER_BOTTOMRIGHT_Y = 160;
constexpr s32 R_PICTO_FOCUS_ICON_X = 142;
constexpr s32 R_PICTO_FOCUS_ICON_Y = 108;
constexpr s32 R_PICTO_FOCUS_TEXT_X = 204;
constexpr s32 R_PICTO_FOCUS_TEXT_Y = 177;

s16 sPictoState = PICTO_BOX_STATE_OFF;
s16 sPictoPhotoBeingTaken = false;
s32 sPictoPhotoState = PICTO_PHOTO_STATE_OFF; // R_PICTO_PHOTO_STATE
bool sPictoBoxOn = false;                     // ACTORCTX_FLAG_PICTO_BOX_ON
bool sPromptOpen = false;                     // our 0xF8 textbox is up and unanswered

u16 sFbufSave[SCREEN_WIDTH * SCREEN_HEIGHT];    // prerender->fbufSave (MM: gfxCtx->zbuffer)
alignas(64) u8 sPictoPhotoI8[PICTO_PHOTO_SIZE]; // MM: gHiBuffer.pictoPhotoI8 (play->pictoPhotoI8)
u8 sLocalPhotoI5[PICTO_PHOTO_COMPRESSED_SIZE];  // only when MM's exports are missing
bool sLocalHasPhoto = false;

// ---- MM exports (mm/2s2h/Enhancements/Items/ColorPictograph.cpp, COMBO_BUILD block) --------------------------------
typedef uint8_t* (*FnPhotoI5)(void);
typedef int (*FnGetInt)(void);
typedef void (*FnSetInt)(int);
typedef void (*FnVoid)(void);
typedef const char* (*FnTex)(int);
typedef void (*FnColorTake)(const uint16_t*);
typedef const uint16_t* (*FnColorPhoto)(void);

FnPhotoI5 sMmPhotoI5 = nullptr;
FnGetInt sMmHasPhoto = nullptr;
FnSetInt sMmSetHasPhoto = nullptr;
FnVoid sMmRecordNoSubjects = nullptr;
FnTex sMmFocusTex = nullptr;
FnColorTake sMmColorTake = nullptr;
FnVoid sMmColorLoad = nullptr;
FnColorPhoto sMmColorPhoto = nullptr;

void Resolve() {
    static bool sTried = false;
    if (sTried) {
        return;
    }
    sTried = true;
    sMmPhotoI5 = (FnPhotoI5)Combo_ResolveSym("2ship", "MM_ComboPicto_PhotoI5");
    sMmHasPhoto = (FnGetInt)Combo_ResolveSym("2ship", "MM_ComboPicto_HasPhoto");
    sMmSetHasPhoto = (FnSetInt)Combo_ResolveSym("2ship", "MM_ComboPicto_SetHasPhoto");
    sMmRecordNoSubjects = (FnVoid)Combo_ResolveSym("2ship", "MM_ComboPicto_RecordNoSubjects");
    sMmFocusTex = (FnTex)Combo_ResolveSym("2ship", "MM_ComboPicto_FocusTex");
    sMmColorTake = (FnColorTake)Combo_ResolveSym("2ship", "MM_ComboPicto_ColorTake");
    sMmColorLoad = (FnVoid)Combo_ResolveSym("2ship", "MM_ComboPicto_ColorLoad");
    sMmColorPhoto = (FnColorPhoto)Combo_ResolveSym("2ship", "MM_ComboPicto_ColorPhoto");
    if (sMmPhotoI5 == nullptr || sMmHasPhoto == nullptr || sMmSetHasPhoto == nullptr) {
        fprintf(stderr, "[ComboPicto] MM picto exports missing: the photo stays in Hyrule only\n");
    }
}

void Log(const char* what) {
    static const int sDbg = Combo_DebugEnv("COMBO_DEBUG_PICTO");
    if (sDbg) {
        Combo_DebugLog("[ComboPicto] %s (box=%d state=%d photo=%d)\n", what, sPictoState, (int)sPictoBoxOn,
                       sPictoPhotoState);
    }
}

// MM's gSaveContext.pictoPhotoI5 / CHECK|SET|REMOVE_QUEST_ITEM(QUEST_PICTOGRAPH), in MM's resident save.
u8* PhotoI5() {
    Resolve();
    u8* p = (sMmPhotoI5 != nullptr) ? sMmPhotoI5() : nullptr;
    return (p != nullptr) ? p : sLocalPhotoI5;
}
bool HasPhoto() {
    Resolve();
    return (sMmHasPhoto != nullptr) ? (sMmHasPhoto() != 0) : sLocalHasPhoto;
}
void SetHasPhoto(bool has) {
    Resolve();
    if (sMmSetHasPhoto != nullptr) {
        sMmSetHasPhoto(has ? 1 : 0);
    } else {
        sLocalHasPhoto = has;
    }
}
// Snap_RecordPictographedActors: clears pictoFlags0/1, then flags the MM quest subjects in view. None exist in Hyrule.
void RecordPictographedActors() {
    Resolve();
    if (sMmRecordNoSubjects != nullptr) {
        sMmRecordNoSubjects();
    }
}

// ---- mm/src/code/z_play.c ------------------------------------------------------------------------------------------
#define RGBA16_GET_R(pixel) (((pixel) >> 11) & 0x1F)
#define RGBA16_GET_G(pixel) (((pixel) >> 6) & 0x1F)
#define RGBA16_GET_B(pixel) (((pixel) >> 1) & 0x1F)
#define PLAY_INTENSITY_RED 2
#define PLAY_INTENSITY_GREEN 4
#define PLAY_INTENSITY_BLUE 1
#define PLAY_INTENSITY_NORM (0x1F * PLAY_INTENSITY_RED + 0x1F * PLAY_INTENSITY_GREEN + 0x1F * PLAY_INTENSITY_BLUE)
#define PLAY_INTENSITY_MIX(r, g, b, m) \
    ((((r)*PLAY_INTENSITY_RED + (g)*PLAY_INTENSITY_GREEN + (b)*PLAY_INTENSITY_BLUE) * (m)) / PLAY_INTENSITY_NORM)

// Play_ConvertRgba16ToIntensityImage, bitDepth 8 (the only depth MM's pictograph uses).
void ConvertRgba16ToI8(u8* destI8, const u16* srcRgba16, s32 rgba16Width, s32 pixelLeft, s32 pixelTop, s32 pixelRight,
                       s32 pixelBottom) {
    for (s32 i = pixelTop; i <= pixelBottom; i++) {
        for (s32 j = pixelLeft; j <= pixelRight; j++) {
            u32 pixel = srcRgba16[i * rgba16Width + j];
            u32 r = RGBA16_GET_R(pixel);
            u32 g = RGBA16_GET_G(pixel);
            u32 b = RGBA16_GET_B(pixel);

            *(destI8++) = PLAY_INTENSITY_MIX(r, g, b, 255);
        }
    }
}

#define PLAY_COMPRESS_BITS 5
#define PLAY_DECOMPRESS_BITS 8

void CompressI8ToI5(void* srcI8, void* destI5, size_t size) {
    u32 i;
    u8* src = (u8*)srcI8;
    s8* dest = (s8*)destI5;
    s32 bitsLeft = PLAY_DECOMPRESS_BITS; // Bits left in the current dest pixel left to compress into
    u32 destPixel = 0;
    s32 shift;
    u32 srcPixel;

    for (i = 0; i < size; i++) {
        srcPixel = *src++;
        srcPixel = (srcPixel * 0x1F + 0x80) / 0xFF;
        shift = bitsLeft - PLAY_COMPRESS_BITS;
        if (shift > 0) {
            destPixel |= srcPixel << shift;
        } else {
            destPixel |= srcPixel >> -shift;
            *dest++ = destPixel;
            shift += PLAY_DECOMPRESS_BITS;
            destPixel = srcPixel << shift;
        }
        bitsLeft = shift;
    }

    if (bitsLeft < PLAY_DECOMPRESS_BITS) {
        *dest = destPixel;
    }
}

void DecompressI5ToI8(void* srcI5, void* destI8, size_t size) {
    u32 i;
    u8* src = (u8*)srcI5;
    s8* dest = (s8*)destI8;
    s32 bitsLeft = PLAY_DECOMPRESS_BITS; // Bits left in the current src pixel left to decompress
    u32 destPixel;
    s32 shift;
    u32 srcPixel = *src++;

    for (i = 0; i < size; i++) {
        shift = bitsLeft - PLAY_COMPRESS_BITS;
        if (shift > 0) {
            destPixel = 0;
            destPixel |= srcPixel >> shift;
        } else {
            destPixel = 0;
            destPixel |= srcPixel << -shift;
            srcPixel = *src++;
            shift += PLAY_DECOMPRESS_BITS;
            destPixel |= srcPixel >> shift;
        }
        destPixel = (destPixel & 0x1F) * 0xFF / 0x1F;
        *dest++ = destPixel;
        bitsLeft = shift;
    }
}

void SavePhoto() {
    CompressI8ToI5(sPictoPhotoI8, PhotoI5(), PICTO_PHOTO_SIZE);
}

// Play_TakePictoPhoto (PreRender_ApplyFilters is a no-op here: 2S2H passes cvgSave = NULL).
void TakePictoPhoto() {
    ConvertRgba16ToI8(sPictoPhotoI8, sFbufSave, SCREEN_WIDTH, PICTO_PHOTO_TOPLEFT_X, PICTO_PHOTO_TOPLEFT_Y,
                      (PICTO_PHOTO_TOPLEFT_X + PICTO_PHOTO_WIDTH) - 1,
                      (PICTO_PHOTO_TOPLEFT_Y + PICTO_PHOTO_HEIGHT) - 1);
    // GameInteractor_Should(VB_PICTO_TAKE, true, prerender): ColorPictograph keeps the colour copy + its PNG.
    Resolve();
    if (sMmColorTake != nullptr) {
        sMmColorTake(sFbufSave);
    }
}

// ---- HUD helpers (MM -> OOT equivalents) ---------------------------------------------------------------------------
// MM Interface_SetBButtonInterfaceDoAction(play, DO_ACTION_STOP)
void SetBStop(PlayState* play) {
    Interface_LoadActionLabelB(play, DO_ACTION_STOP);
}
// MM interfaceCtx->bButtonInterfaceDoActionActive = interfaceCtx->bButtonInterfaceDoAction = 0
void ClearB(PlayState* play) {
    play->interfaceCtx.unk_1FA = play->interfaceCtx.unk_1FC = 0;
}

// The "keep" answer (and the keep-by-default fallback when MM's text is unavailable).
void Keep(PlayState* play, bool closeText) {
    play->haltAllActors = false;
    if (closeText) {
        Message_CloseTextbox(play);
        // OOT's textbox restores the HUD mode it saw at open when it finishes closing; MM sets ALL here.
        gSaveContext.prevHudVisibilityMode = HUD_VISIBILITY_ALL;
    }
    Combo_GuestPlaySfx(MM_NA_SE_SY_DECIDE); // Audio_PlaySfx_MessageDecide
    ClearB(play);
    Interface_ChangeHudVisibilityMode(HUD_VISIBILITY_ALL);
    sPictoState = PICTO_BOX_STATE_OFF;
    if (sPictoPhotoBeingTaken) {
        SavePhoto();
        RecordPictographedActors();
    }
    sPictoBoxOn = false;
    SetHasPhoto(true);
    sPictoPhotoBeingTaken = false;
    Log("keep");
}

void RestoreHudVisibility(PlayState* play) {
    gSaveContext.hudVisibilityMode = HUD_VISIBILITY_NO_CHANGE;
    if ((play->transitionTrigger == TRANS_TRIGGER_OFF) && (play->transitionMode == TRANS_MODE_OFF)) {
        Interface_ChangeHudVisibilityMode(HUD_VISIBILITY_ALL);
    }
}

// MM's answer to the keep / discard prompt (Interface_UpdateButtonsPart1 ~3907). closeText = the textbox is still
// waiting on the choice (we close it, as MM does); false = OOT's message system already closed it.
void Answer(PlayState* play, u8 choiceIndex, bool closeText) {
    sPromptOpen = false;
    if (choiceIndex != 0) {
        play->haltAllActors = false;
        // MM: player->stateFlags1 &= ~PLAYER_STATE1_200 (never set in Hyrule; see the file header)
        if (closeText) {
            Message_CloseTextbox(play);
        }
        gSaveContext.prevHudVisibilityMode = HUD_VISIBILITY_A_B_MINIMAP; // see Keep()
        Combo_GuestPlaySfx(MM_NA_SE_SY_CANCEL);                          // Audio_PlaySfx_MessageCancel
        SetBStop(play);
        Interface_ChangeHudVisibilityMode(HUD_VISIBILITY_A_B_MINIMAP); // MM: HUD_VISIBILITY_A_B
        sPictoState = PICTO_BOX_STATE_LENS;
        SetHasPhoto(false);
        Log("discard");
    } else {
        Keep(play, closeText);
        RestoreHudVisibility(play); // MM: restoreHudVisibility = true
    }
}

} // namespace

extern "C" {

// ---- Player side ---------------------------------------------------------------------------------------------------
void Combo_PictoBegin(PlayState* play) {
    (void)play;
    sPictoBoxOn = true;
    Log("begin");
}

void Combo_PictoEnd(PlayState* play) {
    (void)play;
    if (sPictoBoxOn) {
        sPictoBoxOn = false;
        Log("end");
    }
}

int Combo_PictoActive(void) {
    return sPictoBoxOn ? 1 : 0;
}

int Combo_PictoOwnsInput(void) {
    return (sPictoBoxOn || (sPictoState != PICTO_BOX_STATE_OFF)) ? 1 : 0;
}

void Combo_PictoZoomSfx(void) {
    Combo_GuestPlaySfx(MM_NA_SE_SY_CAMERA_ZOOM_UP);
}

void Combo_PictoErrorSfx(void) {
    Combo_GuestPlaySfx(MM_NA_SE_SY_ERROR);
}

// ---- HUD: MM Interface_UpdateButtonsPart1, pictograph branches -----------------------------------------------------
int Combo_PictoInterfaceUpdate(PlayState* play) {
    Input* input = &play->state.input[0];
    s32 restoreHudVisibility = false;

    if (sPictoState != PICTO_BOX_STATE_OFF) {
        // Related to pictograph
        if (sPictoState == PICTO_BOX_STATE_LENS) {
            if (!sPictoBoxOn) {
                SavePhoto();
                ClearB(play);
                restoreHudVisibility = true;
                sPictoState = PICTO_BOX_STATE_OFF;
                Log("lens closed");
            } else if (CHECK_BTN_ALL(input->press.button, BTN_B)) {
                sPictoBoxOn = false;
                ClearB(play);
                restoreHudVisibility = true;
                sPictoState = PICTO_BOX_STATE_OFF;
                Log("B: put away");
            } else if (CHECK_BTN_ALL(input->press.button, BTN_A)) {
                // MM: || AudioVoice_GetWord() == VOICE_WORD_ID_CHEESE (VRU), gated by the swamp cruise (EVENTINF_41)
                // cutscene check; neither exists in Hyrule.
                Combo_GuestPlaySfx(MM_NA_SE_SY_CAMERA_SHUTTER);
                sPictoPhotoState = PICTO_PHOTO_STATE_SETUP;
                play->haltAllActors = true;
                sPictoState = PICTO_BOX_STATE_SETUP_PHOTO;
                sPictoPhotoBeingTaken = true;
                Log("shutter");
            }
        } else if ((sPictoState == PICTO_BOX_STATE_PHOTO) && sPromptOpen && (play->msgCtx.msgMode == MSGMODE_NONE)) {
            // The answer itself is taken in Combo_PictoMessageUpdate (before OOT's Message_Update, which would
            // otherwise close the choice box on its own). Safety net: the box went away some other way.
            Log("prompt closed without an answer");
            Answer(play, play->msgCtx.choiceIndex, false);
        }
    } else if (sPictoBoxOn) {
        // GameInteractor_Should(VB_PICTO_ACTIVATE, ...): ColorPictograph reloads its colour copy from the PNG.
        Resolve();
        if (sMmColorLoad != nullptr) {
            sMmColorLoad();
        }
        if (!HasPhoto()) {
            SetBStop(play);
            Interface_ChangeHudVisibilityMode(HUD_VISIBILITY_A_B_MINIMAP); // MM: HUD_VISIBILITY_A_B
            sPictoState = PICTO_BOX_STATE_LENS;
            Log("activate: lens");
        } else {
            DecompressI5ToI8(PhotoI5(), sPictoPhotoI8, PICTO_PHOTO_SIZE);
            play->haltAllActors = true;
            sPictoState = PICTO_BOX_STATE_SETUP_PHOTO;
            Log("activate: show held photo");
        }
    } else {
        return 0;
    }

    if (restoreHudVisibility) {
        RestoreHudVisibility(play);
    }
    return 1;
}

// MM Interface_UpdateButtonsPart1 ~3907: the keep / discard answer. Runs right before OOT's Message_Update: OOT's
// message system closes any finished non-event textbox on A/B itself (with OOT's decide sound), so the choice must
// be read and the box closed here first, as MM's HUD does.
void Combo_PictoMessageUpdate(PlayState* play) {
    if ((sPictoState >= PICTO_BOX_STATE_SETUP_PHOTO) && sPromptOpen &&
        (Message_GetState(&play->msgCtx) == TEXT_STATE_CHOICE) && Message_ShouldAdvanceSilent(play)) {
        Answer(play, play->msgCtx.choiceIndex, true);
    }
}

// ---- HUD: MM Interface_Draw, pictograph parts ----------------------------------------------------------------------
void Combo_PictoInterfaceDraw(PlayState* play) {
    if (sPictoState == PICTO_BOX_STATE_OFF) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx);

    // Draw pictograph focus icons
    if (sPictoState == PICTO_BOX_STATE_LENS) {
        Resolve();
        const char* borderTex = (sMmFocusTex != nullptr) ? sMmFocusTex(0) : nullptr;
        const char* iconTex = (sMmFocusTex != nullptr) ? sMmFocusTex(1) : nullptr;
        const char* textTex = (sMmFocusTex != nullptr) ? sMmFocusTex(2) : nullptr;

        if ((borderTex != nullptr) && (iconTex != nullptr) && (textTex != nullptr)) {
            Gfx_SetupDL_39Overlay(play->state.gfxCtx);

            gDPSetAlphaCompare(OVERLAY_DISP++, G_AC_THRESHOLD);
            gDPSetRenderMode(OVERLAY_DISP++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
            gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 155, 255);
            gSPComboRMPush(OVERLAY_DISP++, "mm");
            gDPLoadTextureBlock_4b(OVERLAY_DISP++, (void*)borderTex, G_IM_FMT_IA, 16, 16, 0, G_TX_MIRROR | G_TX_WRAP,
                                   G_TX_MIRROR | G_TX_WRAP, 4, 4, G_TX_NOLOD, G_TX_NOLOD);
            gSPComboRMPop(OVERLAY_DISP++);

            gSPTextureRectangle(OVERLAY_DISP++, R_PICTO_FOCUS_BORDER_TOPLEFT_X << 2,
                                R_PICTO_FOCUS_BORDER_TOPLEFT_Y << 2, (R_PICTO_FOCUS_BORDER_TOPLEFT_X << 2) + (16 << 2),
                                (R_PICTO_FOCUS_BORDER_TOPLEFT_Y << 2) + (16 << 2), G_TX_RENDERTILE, 0, 0, 1 << 10,
                                1 << 10);
            gSPTextureRectangle(
                OVERLAY_DISP++, R_PICTO_FOCUS_BORDER_TOPRIGHT_X << 2, R_PICTO_FOCUS_BORDER_TOPRIGHT_Y << 2,
                (R_PICTO_FOCUS_BORDER_TOPRIGHT_X << 2) + (16 << 2), (R_PICTO_FOCUS_BORDER_TOPRIGHT_Y << 2) + (16 << 2),
                G_TX_RENDERTILE, 512, 0, 1 << 10, 1 << 10);
            gSPTextureRectangle(
                OVERLAY_DISP++, R_PICTO_FOCUS_BORDER_BOTTOMLEFT_X << 2, R_PICTO_FOCUS_BORDER_BOTTOMLEFT_Y << 2,
                (R_PICTO_FOCUS_BORDER_BOTTOMLEFT_X << 2) + (16 << 2),
                (R_PICTO_FOCUS_BORDER_BOTTOMLEFT_Y << 2) + (16 << 2), G_TX_RENDERTILE, 0, 512, 1 << 10, 1 << 10);
            gSPTextureRectangle(
                OVERLAY_DISP++, R_PICTO_FOCUS_BORDER_BOTTOMRIGHT_X << 2, R_PICTO_FOCUS_BORDER_BOTTOMRIGHT_Y << 2,
                (R_PICTO_FOCUS_BORDER_BOTTOMRIGHT_X << 2) + (16 << 2),
                (R_PICTO_FOCUS_BORDER_BOTTOMRIGHT_Y << 2) + (16 << 2), G_TX_RENDERTILE, 512, 512, 1 << 10, 1 << 10);

            gSPComboRMPush(OVERLAY_DISP++, "mm");
            gDPLoadTextureBlock_4b(OVERLAY_DISP++, (void*)iconTex, G_IM_FMT_I, 32, 16, 0, G_TX_NOMIRROR | G_TX_WRAP,
                                   G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            gSPComboRMPop(OVERLAY_DISP++);

            gSPTextureRectangle(OVERLAY_DISP++, R_PICTO_FOCUS_ICON_X << 2, R_PICTO_FOCUS_ICON_Y << 2,
                                (R_PICTO_FOCUS_ICON_X << 2) + (32 << 2), (R_PICTO_FOCUS_ICON_Y << 2) + (16 << 2),
                                G_TX_RENDERTILE, 0, 0, 1 << 10, 1 << 10);

            gSPComboRMPush(OVERLAY_DISP++, "mm");
            gDPLoadTextureBlock_4b(OVERLAY_DISP++, (void*)textTex, G_IM_FMT_I, 32, 8, 0, G_TX_NOMIRROR | G_TX_WRAP,
                                   G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            gSPComboRMPop(OVERLAY_DISP++);

            gSPTextureRectangle(OVERLAY_DISP++, R_PICTO_FOCUS_TEXT_X << 2, R_PICTO_FOCUS_TEXT_Y << 2,
                                (R_PICTO_FOCUS_TEXT_X << 2) + (32 << 2), (R_PICTO_FOCUS_TEXT_Y << 2) + (8 << 2),
                                G_TX_RENDERTILE, 0, 0, 1 << 10, 1 << 10);
        }
    }

    // Draw pictograph photo
    if (sPictoState >= PICTO_BOX_STATE_SETUP_PHOTO) {
        if (!sPictoBoxOn) {
            SavePhoto();
            ClearB(play);
            sPictoState = PICTO_BOX_STATE_OFF;
            sPromptOpen = false;
            gSaveContext.hudVisibilityMode = HUD_VISIBILITY_NO_CHANGE; // MM: HUD_VISIBILITY_IDLE
            Interface_ChangeHudVisibilityMode(HUD_VISIBILITY_ALL);
            Log("photo: box closed");
        } else {
            bool show = true;

            if (sPictoState == PICTO_BOX_STATE_SETUP_PHOTO) {
                sPictoState = PICTO_BOX_STATE_PHOTO;
                // MM: Message_StartTextbox(play, 0xF8, NULL) — MM's own line, in OOT's textbox.
                const u16 textId = Combo_MmTextPrepare(MM_TEXT_PICTO_KEEP);
                if (textId != 0) {
                    Message_StartTextbox(play, textId, NULL);
                    sPromptOpen = true;
                    Interface_ChangeHudVisibilityMode(HUD_VISIBILITY_NOTHING); // MM: HUD_VISIBILITY_NONE
                    // MM: player->stateFlags1 |= PLAYER_STATE1_200 (see the file header)
                } else {
                    // MM's message table is unavailable: no prompt can be asked, keep the photo (MM's default
                    // answer is "Yes").
                    fprintf(stderr, "[ComboPicto] MM message 0xF8 unavailable: photo kept without a prompt\n");
                    Keep(play, false);
                    RestoreHudVisibility(play);
                    show = false;
                }
            }

            if (show) {
                s16 pictoRectTop;
                s16 pictoRectLeft;
                s16 strip;

                Resolve();
                // VB_PICTO_DISPLAY: ColorPictograph (CVar gEnhancements.Items.ColorPictograph, MM's) draws its colour
                // copy instead of the I8 strips.
                const uint16_t* color = (sMmColorPhoto != nullptr) ? sMmColorPhoto() : nullptr;

                gDPPipeSync(OVERLAY_DISP++);
                gDPSetRenderMode(OVERLAY_DISP++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
                gDPSetCombineMode(OVERLAY_DISP++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
                gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 200, 200, 200, 250);
                gDPFillRectangle(OVERLAY_DISP++, 70, 22, 251, 151);

                Gfx_SetupDL_39Overlay(play->state.gfxCtx);

                gDPSetRenderMode(OVERLAY_DISP++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
                gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEI_PRIM, G_CC_MODULATEI_PRIM);
                gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 250, 160, 160, 255);

                // Picture is offset up by 33 pixels to give room for the message box at the bottom
                pictoRectTop = PICTO_PHOTO_TOPLEFT_Y - 33;
                for (strip = 0; strip < (PICTO_PHOTO_HEIGHT / 8); strip++, pictoRectTop += 8) {
                    pictoRectLeft = PICTO_PHOTO_TOPLEFT_X;

                    if (color == nullptr) {
                        u8* stripTex = sPictoPhotoI8 + (0x500 * strip);
                        // 2S2H [Port] Invalidate each section
                        gSPInvalidateTexCache(OVERLAY_DISP++, (uintptr_t)stripTex);
                        gDPLoadTextureBlock(OVERLAY_DISP++, stripTex, G_IM_FMT_I, G_IM_SIZ_8b, PICTO_PHOTO_WIDTH, 8, 0,
                                            G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK,
                                            G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
                    } else {
                        // ColorPictograph DrawPicto: get rid of the sepia tone, RGBA16 strip.
                        u16* stripTex = (u16*)color + (0x500 * strip);
                        gDPSetCombineMode(OVERLAY_DISP++, G_CC_DECALRGBA, G_CC_DECALRGBA);
                        gSPInvalidateTexCache(OVERLAY_DISP++, (uintptr_t)stripTex);
                        gDPLoadTextureBlock(OVERLAY_DISP++, stripTex, G_IM_FMT_RGBA, G_IM_SIZ_16b, PICTO_PHOTO_WIDTH, 8,
                                            0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK,
                                            G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
                    }
                    gSPTextureRectangle(OVERLAY_DISP++, pictoRectLeft << 2, pictoRectTop << 2,
                                        (pictoRectLeft + PICTO_PHOTO_WIDTH) << 2, (pictoRectTop << 2) + (8 << 2),
                                        G_TX_RENDERTILE, 0, 0, 1 << 10, 1 << 10);
                }
            }
        }
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

// ---- Capture: MM z_play.c ------------------------------------------------------------------------------------------
void Combo_PictoPlayUpdate(PlayState* play) {
    (void)play;
    if (sPictoPhotoState == PICTO_PHOTO_STATE_PROCESS) {
        sPictoPhotoState = PICTO_PHOTO_STATE_READY;
        TakePictoPhoto();
        sPictoPhotoState = PICTO_PHOTO_STATE_OFF;
        Log("photo taken");
    }
}

int Combo_PictoPlayDrawCapture(PlayState* play, void** gfxp) {
    (void)play;
    if (sPictoPhotoState != PICTO_PHOTO_STATE_SETUP) {
        return 0;
    }
    sPictoPhotoState = PICTO_PHOTO_STATE_PROCESS;
    // 2S2H [Port] Custom handling for picto box capture to CPU (the world as drawn so far, 4:3 slice at 320x240,
    // RGBA16 native-endian, read back when this frame's display list runs).
    Gfx* gfx = (Gfx*)*gfxp;
    FB_WriteFramebufferSliceToCPU(&gfx, sFbufSave, false);
    *gfxp = gfx;
    return 1;
}

void Combo_PictoReset(void) {
    sPictoState = PICTO_BOX_STATE_OFF;
    sPictoPhotoBeingTaken = false;
    sPictoPhotoState = PICTO_PHOTO_STATE_OFF;
    sPictoBoxOn = false;
    sPromptOpen = false;
}

} // extern "C"

#endif // COMBO_BUILD
