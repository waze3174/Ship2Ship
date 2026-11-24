// soh/soh/Enhancements/randomizer/ComboCrater.h — ComboShip: MM's crater marks (mm ovl_En_Test, "Goron Link punch /
// pound") for the Goron in Hyrule. A small pool driven by Player, like ComboTireMark.
#ifndef COMBO_CRATER_H
#define COMBO_CRATER_H

#ifdef __cplusplus
extern "C" {
#endif

// Include after global.h. params as MM's EnTest: > 0 = a wall crater at pos with rot, scale params / 100000 (the punch
// uses 500); 0 = a floor crater, found by raycasting down from pos (the pound).
void Combo_Crater_Spawn(PlayState* play, Vec3f* pos, Vec3s* rot, s16 params);
void Combo_Crater_Update(void);
void Combo_Crater_Draw(PlayState* play);
void Combo_Crater_Clear(void);

#ifdef __cplusplus
}
#endif

#endif
