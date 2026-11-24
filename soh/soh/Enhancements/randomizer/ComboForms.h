// soh/soh/Enhancements/randomizer/ComboForms.h — ComboShip: MM transformations in Hyrule. C API for the fenced seams in
// z_player.c / z_player_lib.c. See ComboForms.cpp.
#ifndef COMBO_FORMS_H
#define COMBO_FORMS_H

#ifdef __cplusplus
extern "C" {
#endif

enum ComboForm {
    COMBO_FORM_HUMAN = 0,
    COMBO_FORM_GORON,
    COMBO_FORM_ZORA,
    COMBO_FORM_DEKU,
    COMBO_FORM_FIERCE_DEITY,
    COMBO_FORM_MAX,
};

int Combo_OotForm(void);
void Combo_OotSetForm(int form);
int Combo_OotFormDrawActive(void* actor);
void** Combo_OotFormLimbs(int* dListCount);
void* Combo_OotFormWrapOverride(void* inner);
void* Combo_OotFormDrawPrep(void* gfx, int eyeIndex, int mouthIndex);
int Combo_OotFormBlocksItem(int item);
int Combo_OotFormForMask(int itemAction);
int Combo_ZoraFinsActive(void);
int Combo_DekuBubbleActive(void);
int Combo_FormHeldBItem(void);
const void* Combo_MmDL(const char* mmPath);
struct PlayState;
// The Goron's ball, drawn as MM draws it (state from Player_Action_ComboGoronRoll: gComboGoronBall* in z_player.c).
void Combo_OotFormDrawBall(struct PlayState* play, short shapeYaw);
void Combo_MmAnimatedMat(struct PlayState* play, const char* mmOtrPath, float alphaRatio, unsigned int flags);
void Combo_MmAnimatedMatStep(struct PlayState* play, const char* mmOtrPath, float alphaRatio, unsigned int flags,
                             unsigned int step);
void Combo_GoronGuardStart(struct PlayState* play);
void Combo_GoronGuardUpdate(struct PlayState* play);
int Combo_GoronGuardDraw(struct PlayState* play);
void Combo_ZoraBarrierDraw(struct PlayState* play, short strength);
void Combo_GoronGhostRequest(int kind);
void Combo_GoronGhostFade(void);
void Combo_GoronGhostClear(void);
void Combo_GoronGhostDraw(struct PlayState* play);
void Combo_GoronPunchFist(struct PlayState* play, int alpha);
void Combo_GoronBallExtras(struct PlayState* play, int spikes, int charge, int phase);
void Combo_DebugLog(const char* fmt, ...);
// MM sounds (ComboGuestAudio.cpp): the forms' MM voices and move sounds, played natively by OOT's audio engine (a
// native MM sfx player running MM's Sequence_0 — audio_seqplayer_mm.c / audio_mm_sfx.c).
void Combo_GuestAudioEnd(void);
void Combo_MmAudioDebugAfterRender(short* samples, unsigned int numSamples);
unsigned int Combo_MmAudioDebugSampleCount(unsigned int numSamples);
int Combo_GuestVoice(unsigned short ootSfxId);
void Combo_GuestVoiceStop(unsigned short ootSfxId);
int Combo_GuestPlaySfx(unsigned short mmSfxId);
void Combo_GuestStopSfx(unsigned short mmSfxId);
// Positions are Vec3f (x, y, z floats), passed as float pointers so this header needs no z64 types.
int Combo_GuestAllocSlot(void);
void Combo_GuestPlaySfxAt(unsigned short mmSfxId, int slot, const float* projectedPos);
void Combo_GuestPlaySfxAtWorld(struct PlayState* play, unsigned short mmSfxId, const float* worldPos);
void Combo_GuestStopSlot(int slot);
// MM music as a fanfare (ComboGuestAudio.cpp): MM's own sequence (MM seq id, e.g. NA_BGM_BREMEN_MARCH 0x53,
// NA_BGM_KAMARO_DANCE 0x71) on OOT's fanfare player with MM script semantics, BGM ducked as MM's Audio_PlayFanfare
// does. Play returns 1 when started, 0 when unavailable (gCombo.Audio.MmSfx off / not registered): fall back.
int Combo_MmPlayFanfare(unsigned short mmSeqId);
void Combo_MmStopFanfare(void); // MM func_80831454: SEQCMD_STOP_SEQUENCE(SEQ_PLAYER_FANFARE, 0)
int Combo_MmFanfareIsPlaying(void);
int Combo_DebugEnv(const char* name);   // log-only traces (env var or a line of combo-debug.txt)
int Combo_HarnessEnv(const char* name); // behaviour-changing test switches (env var at launch only)

#ifdef __cplusplus
}
#endif

#endif
