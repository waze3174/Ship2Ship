// soh/soh/Enhancements/randomizer/ComboPicto.h — ComboShip: MM's Pictograph Box in Hyrule (OOT gameplay), ported
// from MM's own code. C API for the fenced seams in z_player.c, z_parameter.c and z_play.c. See ComboPicto.cpp.
//
// MM keeps one bit, ACTORCTX_FLAG_PICTO_BOX_ON, that the player sets (Player_ActionHandler_13), the HUD clears
// (B = put the box away, keep the photo) and Player_SetAction clears on every action change. OOT's ActorContext has
// no such flag, so it lives here: Combo_PictoBegin / Combo_PictoEnd / Combo_PictoActive are that bit.
#ifndef COMBO_PICTO_H
#define COMBO_PICTO_H

#ifdef __cplusplus
extern "C" {
#endif

struct PlayState;
struct GraphicsContext;

// ---- Player side (z_player.c) ---------------------------------------------------------------------------------------
// MM: play->actorCtx.flags |= ACTORCTX_FLAG_PICTO_BOX_ON. Call right AFTER Player_SetupAction(..first-person..)
// (MM's Player_SetAction clears the flag, so MM sets it after). Does not play a sound.
void Combo_PictoBegin(struct PlayState* play);
// MM: play->actorCtx.flags &= ~ACTORCTX_FLAG_PICTO_BOX_ON (Player_SetAction does this on every action change).
void Combo_PictoEnd(struct PlayState* play);
// MM: (play->actorCtx.flags & ACTORCTX_FLAG_PICTO_BOX_ON) != 0. The picto first-person action exits when this is 0.
int Combo_PictoActive(void);
// 2S2H's Pictograph Box on C-Up: 1 when the option is on, MM's inventory holds the Box and no photo is kept.
int Combo_PictoOnCUpWanted(void);
// 1 while the pictograph HUD owns A/B (lens with the shutter armed, the photo + keep/discard prompt). The player's
// first-person action must not react to A/B/R/C while this is 1 (MM's Player_Action_43 for unk_AA5 == 2 only
// watches the flag).
int Combo_PictoOwnsInput(void);
// MM's system sounds, played through MM's engine (mm/include/sfx.h).
void Combo_PictoZoomSfx(void);  // NA_SE_SY_CAMERA_ZOOM_UP (entering and leaving the lens)
void Combo_PictoErrorSfx(void); // NA_SE_SY_ERROR (func_80831814 refused)

// ---- HUD side (z_parameter.c) ---------------------------------------------------------------------------------------
// MM Interface_UpdateButtonsPart1's pictograph branches. Returns 1 when it handled the frame (the rest of the button
// logic, OOT's func_80083108, must not run, as MM skips Interface_UpdateButtonsPart2).
int Combo_PictoInterfaceUpdate(struct PlayState* play);
// The keep / discard answer (MM reads it in Interface_UpdateButtonsPart1). Call right BEFORE Message_Update: OOT's
// message system closes a finished choice box on A by itself, so the choice must be taken first.
void Combo_PictoMessageUpdate(struct PlayState* play);
// MM Interface_Draw's pictograph parts (focus frame, photo + strips, first-draw prompt).
void Combo_PictoInterfaceDraw(struct PlayState* play);

// ---- Capture (z_play.c) ---------------------------------------------------------------------------------------------
// MM Play_UpdateMain: R_PICTO_PHOTO_STATE == PROCESS -> Play_TakePictoPhoto.
void Combo_PictoPlayUpdate(struct PlayState* play);
// MM Play_Draw: R_PICTO_PHOTO_STATE == SETUP. Returns 1 when the capture was queued this frame (append the capture
// into *gfxp; the caller then skips the post-world draw as 2S2H does).
int Combo_PictoPlayDrawCapture(struct PlayState* play, void** gfxp);
// MM Play_Init / Play_Destroy / Interface_Init reset the photo state.
void Combo_PictoReset(void);

#ifdef __cplusplus
}
#endif

#endif
