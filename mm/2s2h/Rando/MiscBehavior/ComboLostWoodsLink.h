// mm/2s2h/Rando/MiscBehavior/ComboLostWoodsLink.h — ComboShip: the Lost Woods link, MM side. C API for the fenced
// seams in z_eventmgr.c, ComboEntrances.cpp and BenPort.cpp. See ComboLostWoodsLink.cpp and
// combo/rando/ComboLostWoodsLink.h.
#ifndef COMBO_MM_LOST_WOODS_LINK_H
#define COMBO_MM_LOST_WOODS_LINK_H

#ifdef __cplusplus
extern "C" {
#endif

// An exit Link walked through (Combo_MM_RedirectExit). Returns the entrance the link wants instead, or `entrance`
// unchanged when the link has no say. The Clock Tower corridor's far end returns it unchanged too, after asking for
// the switch to OoT.
int Combo_MM_LostWoodsLinkExit(int entrance);

// 1 while the entrance cutscene of the arrival spawn must not play: Link just arrived at Lost Woods from OoT, or at
// the Clock Tower corridor's far end from Lost Woods.
int Combo_MM_LostWoodsLinkSkipsEntranceCs(void);

// 1 when walking into the Clock Tower from Clock Town keeps Link in MM instead of going straight to OoT.
int Combo_MM_ClockTowerEntryStaysInMm(void);

#ifdef __cplusplus
}
#endif

#endif // COMBO_MM_LOST_WOODS_LINK_H
