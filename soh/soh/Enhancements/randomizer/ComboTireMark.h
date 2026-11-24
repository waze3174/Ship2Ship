// soh/soh/Enhancements/randomizer/ComboTireMark.h — ComboShip: MM's ground trail (EffectTireMark) for Link in Hyrule.
#ifndef COMBO_TIRE_MARK_H
#define COMBO_TIRE_MARK_H

#ifdef __cplusplus
extern "C" {
#endif

// Include after global.h (uses its PlayState / CollisionPoly / Vec3f).
void Combo_TireMark_Add(PlayState* play, Vec3f* pos, f32 halfWidth, s16 angle, CollisionPoly* colPoly, s32 bgId,
                        s32 goron);
void Combo_TireMark_Break(void);
void Combo_TireMark_Update(void);
void Combo_TireMark_Clear(void);
void Combo_TireMark_Draw(PlayState* play);

#ifdef __cplusplus
}
#endif

#endif
