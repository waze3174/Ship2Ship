#ifdef COMBO_BUILD
#include "soh/Enhancements/randomizer/ComboForms.h" // ComboShip: MM transformations in Hyrule
#endif
#include "global.h"
#include <string.h>
#include "objects/gameplay_keep/gameplay_keep.h"
#include "objects/gameplay_field_keep/gameplay_field_keep.h"
#include "objects/object_link_boy/object_link_boy.h"
#include "objects/object_link_child/object_link_child.h"
#include "overlays/actors/ovl_Demo_Effect/z_demo_effect.h"
#ifdef COMBO_BUILD
#include "overlays/actors/ovl_En_Arrow/z_en_arrow.h" // ComboShip: MM's Deku Bubble held at Deku Link's mouth
#include "overlays/actors/ovl_En_Boom/z_en_boom.h"   // ComboShip: MM's Zora fins (arm fins hide while thrown)
static s32 sComboDekuHeadPushed = false; // head matrix saved before the bubble squash (popped after the head)
#endif
#ifdef COMBO_BUILD
#include "soh/Enhancements/randomizer/ComboForms.h"
#include "soh/Enhancements/randomizer/ComboPause.h"   // ComboShip: merged pause menus (MM masks worn in Hyrule)
#include "soh/Enhancements/randomizer/ComboMmItems.h" // ComboShip: the Bremen march (no sword blade in hand)
// ComboShip: a Goron in Hyrule shrugs off heat and lava like the Goron Tunic (MM Gorons are heat-proof).
#define COMBO_HEAT_PROOF() (Combo_OotForm() == COMBO_FORM_GORON)
// ...and a Zora breathes underwater like the Zora Tunic.
#define COMBO_WATER_PROOF() (Combo_OotForm() == COMBO_FORM_ZORA)
#else
#define COMBO_WATER_PROOF() 0
#define COMBO_HEAT_PROOF() 0
#endif

#include <libultraship/bridge/resourcebridge.h>
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/Enhancements/randomizer/draw.h"
#include "soh/ResourceManagerHelpers.h"

#include <stdlib.h>
#include <libultraship/bridge/consolevariablebridge.h>
#ifdef COMBO_BUILD
static Color_RGB8 sComboTunicEnv = { 30, 105, 27 }; // last tunic env colour Player_DrawImpl set
#endif

typedef struct {
    /* 0x00 */ u8 flag;
    /* 0x02 */ u16 textId;
} TextTriggerEntry; // size = 0x04

typedef struct {
    /* 0x00 */ void* dList;
    /* 0x04 */ Vec3f pos;
} BowStringData; // size = 0x10

FlexSkeletonHeader* gPlayerSkelHeaders[] = { &gLinkAdultSkel, &gLinkChildSkel };

s16 sBootData[PLAYER_BOOTS_MAX][17] = {
    { 200, 1000, 300, 700, 550, 270, 600, 350, 800, 600, -100, 600, 590, 750, 125, 200, 130 },
    { 200, 1000, 300, 700, 550, 270, 1000, 0, 800, 300, -160, 600, 590, 750, 125, 200, 130 },
    { 200, 1000, 300, 700, 550, 270, 600, 600, 800, 550, -100, 600, 540, 270, 25, 0, 130 },
    { 200, 1000, 300, 700, 380, 400, 0, 300, 800, 500, -100, 600, 590, 750, 125, 200, 130 },
    { 80, 800, 150, 700, 480, 270, 600, 50, 800, 550, -40, 400, 540, 270, 25, 0, 80 },
    { 200, 1000, 300, 800, 500, 400, 800, 400, 800, 550, -100, 600, 540, 750, 125, 400, 200 },
};

// Used to map action params to model groups
u8 sActionModelGroups[] = {
    PLAYER_MODELGROUP_DEFAULT,          // PLAYER_IA_NONE
    PLAYER_MODELGROUP_SWORD,            // PLAYER_IA_SWORD_CS
    PLAYER_MODELGROUP_10,               // PLAYER_IA_FISHING_POLE
    PLAYER_MODELGROUP_SWORD_AND_SHIELD, // PLAYER_IA_SWORD_MASTER
    PLAYER_MODELGROUP_SWORD_AND_SHIELD, // PLAYER_IA_SWORD_KOKIRI
    PLAYER_MODELGROUP_BGS,              // PLAYER_IA_SWORD_BIGGORON
    PLAYER_MODELGROUP_10,               // PLAYER_IA_DEKU_STICK
    PLAYER_MODELGROUP_HAMMER,           // PLAYER_IA_HAMMER
    PLAYER_MODELGROUP_BOW_SLINGSHOT,    // PLAYER_IA_BOW
    PLAYER_MODELGROUP_BOW_SLINGSHOT,    // PLAYER_IA_BOW_FIRE
    PLAYER_MODELGROUP_BOW_SLINGSHOT,    // PLAYER_IA_BOW_ICE
    PLAYER_MODELGROUP_BOW_SLINGSHOT,    // PLAYER_IA_BOW_LIGHT
    PLAYER_MODELGROUP_BOW_SLINGSHOT,    // PLAYER_IA_BOW_0C
    PLAYER_MODELGROUP_BOW_SLINGSHOT,    // PLAYER_IA_BOW_0D
    PLAYER_MODELGROUP_BOW_SLINGSHOT,    // PLAYER_IA_BOW_0E
    PLAYER_MODELGROUP_BOW_SLINGSHOT,    // PLAYER_IA_SLINGSHOT
    PLAYER_MODELGROUP_HOOKSHOT,         // PLAYER_IA_HOOKSHOT
    PLAYER_MODELGROUP_HOOKSHOT,         // PLAYER_IA_LONGSHOT
    PLAYER_MODELGROUP_EXPLOSIVES,       // PLAYER_IA_BOMB
    PLAYER_MODELGROUP_EXPLOSIVES,       // PLAYER_IA_BOMBCHU
    PLAYER_MODELGROUP_BOOMERANG,        // PLAYER_IA_BOOMERANG
    PLAYER_MODELGROUP_DEFAULT,          // PLAYER_IA_MAGIC_SPELL_15
    PLAYER_MODELGROUP_DEFAULT,          // PLAYER_IA_MAGIC_SPELL_16
    PLAYER_MODELGROUP_BOW_SLINGSHOT,    // PLAYER_IA_MAGIC_SPELL_17
    PLAYER_MODELGROUP_DEFAULT,          // PLAYER_IA_FARORES_WIND
    PLAYER_MODELGROUP_DEFAULT,          // PLAYER_IA_NAYRUS_LOVE
    PLAYER_MODELGROUP_DEFAULT,          // PLAYER_IA_DINS_FIRE
    PLAYER_MODELGROUP_DEFAULT,          // PLAYER_IA_DEKU_NUT
    PLAYER_MODELGROUP_OCARINA,          // PLAYER_IA_OCARINA_FAIRY
    PLAYER_MODELGROUP_OOT,              // PLAYER_IA_OCARINA_OF_TIME
    PLAYER_MODELGROUP_BOTTLE,           // PLAYER_IA_BOTTLE
    PLAYER_MODELGROUP_BOTTLE,           // PLAYER_IA_BOTTLE_FISH
    PLAYER_MODELGROUP_BOTTLE,           // PLAYER_IA_BOTTLE_FIRE
    PLAYER_MODELGROUP_BOTTLE,           // PLAYER_IA_BOTTLE_BUG
    PLAYER_MODELGROUP_BOTTLE,           // PLAYER_IA_BOTTLE_POE
    PLAYER_MODELGROUP_BOTTLE,           // PLAYER_IA_BOTTLE_BIG_POE
    PLAYER_MODELGROUP_BOTTLE,           // PLAYER_IA_BOTTLE_RUTOS_LETTER
    PLAYER_MODELGROUP_BOTTLE,           // PLAYER_IA_BOTTLE_POTION_RED
    PLAYER_MODELGROUP_BOTTLE,           // PLAYER_IA_BOTTLE_POTION_BLUE
    PLAYER_MODELGROUP_BOTTLE,           // PLAYER_IA_BOTTLE_POTION_GREEN
    PLAYER_MODELGROUP_BOTTLE,           // PLAYER_IA_BOTTLE_MILK_FULL
    PLAYER_MODELGROUP_BOTTLE,           // PLAYER_IA_BOTTLE_MILK_HALF
    PLAYER_MODELGROUP_BOTTLE,           // PLAYER_IA_BOTTLE_FAIRY
    PLAYER_MODELGROUP_DEFAULT,          // PLAYER_IA_ZELDAS_LETTER
    PLAYER_MODELGROUP_DEFAULT,          // PLAYER_IA_WEIRD_EGG
    PLAYER_MODELGROUP_DEFAULT,          // PLAYER_IA_CHICKEN
    PLAYER_MODELGROUP_DEFAULT,          // PLAYER_IA_MAGIC_BEAN
    PLAYER_MODELGROUP_DEFAULT,          // PLAYER_IA_POCKET_EGG
    PLAYER_MODELGROUP_DEFAULT,          // PLAYER_IA_POCKET_CUCCO
    PLAYER_MODELGROUP_DEFAULT,          // PLAYER_IA_COJIRO
    PLAYER_MODELGROUP_DEFAULT,          // PLAYER_IA_ODD_MUSHROOM
    PLAYER_MODELGROUP_DEFAULT,          // PLAYER_IA_ODD_POTION
    PLAYER_MODELGROUP_DEFAULT,          // PLAYER_IA_POACHERS_SAW
    PLAYER_MODELGROUP_DEFAULT,          // PLAYER_IA_BROKEN_GORONS_SWORD
    PLAYER_MODELGROUP_DEFAULT,          // PLAYER_IA_PRESCRIPTION
    PLAYER_MODELGROUP_DEFAULT,          // PLAYER_IA_FROG
    PLAYER_MODELGROUP_DEFAULT,          // PLAYER_IA_EYEDROPS
    PLAYER_MODELGROUP_DEFAULT,          // PLAYER_IA_CLAIM_CHECK
    PLAYER_MODELGROUP_DEFAULT,          // PLAYER_IA_MASK_KEATON
    PLAYER_MODELGROUP_DEFAULT,          // PLAYER_IA_MASK_SKULL
    PLAYER_MODELGROUP_DEFAULT,          // PLAYER_IA_MASK_SPOOKY
    PLAYER_MODELGROUP_DEFAULT,          // PLAYER_IA_MASK_BUNNY_HOOD
    PLAYER_MODELGROUP_DEFAULT,          // PLAYER_IA_MASK_GORON
    PLAYER_MODELGROUP_DEFAULT,          // PLAYER_IA_MASK_ZORA
    PLAYER_MODELGROUP_DEFAULT,          // PLAYER_IA_MASK_GERUDO
    PLAYER_MODELGROUP_DEFAULT,          // PLAYER_IA_MASK_TRUTH
    PLAYER_MODELGROUP_DEFAULT,          // PLAYER_IA_LENS_OF_TRUTH
};

TextTriggerEntry sTextTriggers[] = {
    { 1, 0x3040 },
    { 2, 0x401D },
    { 0, 0x0000 },
    { 2, 0x401D },
};

// Used to map model groups to model types for [animation, left hand, right hand, sheath, waist]
u8 gPlayerModelTypes[PLAYER_MODELGROUP_MAX][PLAYER_MODELGROUPENTRY_MAX] = {
    /* PLAYER_MODELGROUP_0 */
    { PLAYER_ANIMTYPE_2, PLAYER_MODELTYPE_LH_OPEN, PLAYER_MODELTYPE_RH_SHIELD, PLAYER_MODELTYPE_SHEATH_16,
      PLAYER_MODELTYPE_WAIST },
    /* PLAYER_MODELGROUP_CHILD_HYLIAN_SHIELD */
    { PLAYER_ANIMTYPE_1, PLAYER_MODELTYPE_LH_SWORD, PLAYER_MODELTYPE_RH_CLOSED, PLAYER_MODELTYPE_SHEATH_19,
      PLAYER_MODELTYPE_WAIST },
    /* PLAYER_MODELGROUP_SWORD_AND_SHIELD */
    { PLAYER_ANIMTYPE_1, PLAYER_MODELTYPE_LH_SWORD, PLAYER_MODELTYPE_RH_SHIELD, PLAYER_MODELTYPE_SHEATH_17,
      PLAYER_MODELTYPE_WAIST },
    /* PLAYER_MODELGROUP_DEFAULT */
    { PLAYER_ANIMTYPE_0, PLAYER_MODELTYPE_LH_OPEN, PLAYER_MODELTYPE_RH_OPEN, PLAYER_MODELTYPE_SHEATH_18,
      PLAYER_MODELTYPE_WAIST },
    /* PLAYER_MODELGROUP_4 */
    { PLAYER_ANIMTYPE_0, PLAYER_MODELTYPE_LH_OPEN, PLAYER_MODELTYPE_RH_OPEN, PLAYER_MODELTYPE_SHEATH_18,
      PLAYER_MODELTYPE_WAIST },
    /* PLAYER_MODELGROUP_BGS */
    { PLAYER_ANIMTYPE_3, PLAYER_MODELTYPE_LH_BGS, PLAYER_MODELTYPE_RH_CLOSED, PLAYER_MODELTYPE_SHEATH_19,
      PLAYER_MODELTYPE_WAIST },
    /* PLAYER_MODELGROUP_BOW_SLINGSHOT */
    { PLAYER_ANIMTYPE_4, PLAYER_MODELTYPE_LH_CLOSED, PLAYER_MODELTYPE_RH_BOW_SLINGSHOT, PLAYER_MODELTYPE_SHEATH_18,
      PLAYER_MODELTYPE_WAIST },
    /* PLAYER_MODELGROUP_EXPLOSIVES */
    { PLAYER_ANIMTYPE_5, PLAYER_MODELTYPE_LH_OPEN, PLAYER_MODELTYPE_RH_OPEN, PLAYER_MODELTYPE_SHEATH_18,
      PLAYER_MODELTYPE_WAIST },
    /* PLAYER_MODELGROUP_BOOMERANG */
    { PLAYER_ANIMTYPE_0, PLAYER_MODELTYPE_LH_BOOMERANG, PLAYER_MODELTYPE_RH_OPEN, PLAYER_MODELTYPE_SHEATH_18,
      PLAYER_MODELTYPE_WAIST },
    /* PLAYER_MODELGROUP_HOOKSHOT */
    { PLAYER_ANIMTYPE_4, PLAYER_MODELTYPE_LH_OPEN, PLAYER_MODELTYPE_RH_HOOKSHOT, PLAYER_MODELTYPE_SHEATH_18,
      PLAYER_MODELTYPE_WAIST },
    /* PLAYER_MODELGROUP_10 */
    { PLAYER_ANIMTYPE_3, PLAYER_MODELTYPE_LH_CLOSED, PLAYER_MODELTYPE_RH_CLOSED, PLAYER_MODELTYPE_SHEATH_18,
      PLAYER_MODELTYPE_WAIST },
    /* PLAYER_MODELGROUP_HAMMER */
    { PLAYER_ANIMTYPE_3, PLAYER_MODELTYPE_LH_HAMMER, PLAYER_MODELTYPE_RH_CLOSED, PLAYER_MODELTYPE_SHEATH_18,
      PLAYER_MODELTYPE_WAIST },
    /* PLAYER_MODELGROUP_OCARINA */
    { PLAYER_ANIMTYPE_0, PLAYER_MODELTYPE_LH_OPEN, PLAYER_MODELTYPE_RH_OCARINA, PLAYER_MODELTYPE_SHEATH_18,
      PLAYER_MODELTYPE_WAIST },
    /* PLAYER_MODELGROUP_OOT */
    { PLAYER_ANIMTYPE_0, PLAYER_MODELTYPE_LH_OPEN, PLAYER_MODELTYPE_RH_OOT, PLAYER_MODELTYPE_SHEATH_18,
      PLAYER_MODELTYPE_WAIST },
    /* PLAYER_MODELGROUP_BOTTLE */
    { PLAYER_ANIMTYPE_0, PLAYER_MODELTYPE_LH_BOTTLE, PLAYER_MODELTYPE_RH_OPEN, PLAYER_MODELTYPE_SHEATH_18,
      PLAYER_MODELTYPE_WAIST },
    /* PLAYER_MODELGROUP_SWORD */
    { PLAYER_ANIMTYPE_0, PLAYER_MODELTYPE_LH_SWORD, PLAYER_MODELTYPE_RH_OPEN, PLAYER_MODELTYPE_SHEATH_19,
      PLAYER_MODELTYPE_WAIST },
};

Gfx* sPlayerRightHandShieldDLs[PLAYER_SHIELD_MAX * 4] = {
    // PLAYER_SHIELD_NONE
    gLinkAdultRightHandClosedNearDL,
    gLinkChildRightHandClosedNearDL,
    gLinkAdultRightHandClosedFarDL,
    gLinkChildRightHandClosedFarDL,
    // PLAYER_SHIELD_DEKU
    gLinkAdultRightHandClosedNearDL,
    gLinkChildRightFistAndDekuShieldNearDL,
    gLinkAdultRightHandClosedFarDL,
    gLinkChildRightFistAndDekuShieldFarDL,
    // PLAYER_SHIELD_HYLIAN
    gLinkAdultRightHandHoldingHylianShieldNearDL,
    gLinkChildRightHandClosedNearDL,
    gLinkAdultRightHandHoldingHylianShieldFarDL,
    gLinkChildRightHandClosedFarDL,
    // PLAYER_SHIELD_MIRROR
    gLinkAdultRightHandHoldingMirrorShieldNearDL,
    gLinkChildRightHandClosedNearDL,
    gLinkAdultRightHandHoldingMirrorShieldFarDL,
    gLinkChildRightHandClosedFarDL,
};

Gfx* sSheathWithSwordDLs[(PLAYER_SHIELD_MAX + 2) * 4] = {
    // PLAYER_SHIELD_NONE
    gLinkAdultMasterSwordAndSheathNearDL,
    gLinkChildSwordAndSheathNearDL,
    gLinkAdultMasterSwordAndSheathFarDL,
    gLinkChildSwordAndSheathFarDL,
    // PLAYER_SHIELD_DEKU
    gLinkAdultMasterSwordAndSheathNearDL,
    gLinkChildDekuShieldSwordAndSheathNearDL,
    gLinkAdultMasterSwordAndSheathFarDL,
    gLinkChildDekuShieldSwordAndSheathFarDL,
    // PLAYER_SHIELD_HYLIAN
    gLinkAdultHylianShieldSwordAndSheathNearDL,
    gLinkChildHylianShieldSwordAndSheathNearDL,
    gLinkAdultHylianShieldSwordAndSheathFarDL,
    gLinkChildHylianShieldSwordAndSheathFarDL,
    // PLAYER_SHIELD_MIRROR
    gLinkAdultMirrorShieldSwordAndSheathNearDL,
    gLinkChildSwordAndSheathNearDL,
    gLinkAdultMirrorShieldSwordAndSheathFarDL,
    gLinkChildSwordAndSheathFarDL,
    // PLAYER_SHIELD_NONE (child, no sword)
    NULL,
    NULL,
    NULL,
    NULL,
    // PLAYER_SHIELD_DEKU (child, no sword)
    NULL,
    gLinkChildDekuShieldWithMatrixDL,
    NULL,
    gLinkChildDekuShieldWithMatrixDL,
};

Gfx* sSheathWithoutSwordDLs[(PLAYER_SHIELD_MAX + 2) * 4] = {
    // PLAYER_SHIELD_NONE
    gLinkAdultSheathNearDL,
    gLinkChildSheathNearDL,
    gLinkAdultSheathFarDL,
    gLinkChildSheathFarDL,
    // PLAYER_SHIELD_DEKU
    gLinkAdultSheathNearDL,
    gLinkChildDekuShieldAndSheathNearDL,
    gLinkAdultSheathFarDL,
    gLinkChildDekuShieldAndSheathFarDL,
    // PLAYER_SHIELD_HYLIAN
    gLinkAdultHylianShieldAndSheathNearDL,
    gLinkChildHylianShieldAndSheathNearDL,
    gLinkAdultHylianShieldAndSheathFarDL,
    gLinkChildHylianShieldAndSheathFarDL,
    // PLAYER_SHIELD_MIRROR
    gLinkAdultMirrorShieldAndSheathNearDL,
    gLinkChildSheathNearDL,
    gLinkAdultMirrorShieldAndSheathFarDL,
    gLinkChildSheathFarDL,
    // PLAYER_SHIELD_NONE (child, no sword)
    NULL,
    NULL,
    NULL,
    NULL,
    // PLAYER_SHIELD_DEKU (child, no sword)
    gLinkAdultSheathNearDL,
    gLinkChildDekuShieldWithMatrixDL,
    gLinkAdultSheathNearDL,
    gLinkChildDekuShieldWithMatrixDL,
};

Gfx* gPlayerLeftHandBgsDLs[] = {
    // Biggoron Sword
    gLinkAdultLeftHandHoldingBgsNearDL,
    gLinkChildLeftHandHoldingMasterSwordDL,
    gLinkAdultLeftHandHoldingBgsFarDL,
    gLinkChildLeftHandHoldingMasterSwordDL,
    // Broken Giant's Knife
    gLinkAdultHandHoldingBrokenGiantsKnifeDL,
    gLinkChildLeftHandHoldingMasterSwordDL,
    gLinkAdultHandHoldingBrokenGiantsKnifeFarDL,
    gLinkChildLeftHandHoldingMasterSwordDL,
};

Gfx* gPlayerLeftHandOpenDLs[] = {
    gLinkAdultLeftHandNearDL,
    gLinkChildLeftHandNearDL,
    gLinkAdultLeftHandFarDL,
    gLinkChildLeftHandFarDL,
};

Gfx* gPlayerLeftHandClosedDLs[] = {
    gLinkAdultLeftHandClosedNearDL,
    gLinkChildLeftFistNearDL,
    gLinkAdultLeftHandClosedFarDL,
    gLinkChildLeftFistFarDL,
};

Gfx* sPlayerLeftHandSwordDLs2[] = {
    gLinkAdultLeftHandHoldingMasterSwordNearDL,
    gLinkChildLeftFistAndKokiriSwordNearDL,
    gLinkAdultLeftHandHoldingMasterSwordFarDL,
    gLinkChildLeftFistAndKokiriSwordFarDL,
};

Gfx* sPlayerLeftHandSwordDLs[] = {
    gLinkAdultLeftHandHoldingMasterSwordNearDL,
    gLinkChildLeftFistAndKokiriSwordNearDL,
    gLinkAdultLeftHandHoldingMasterSwordFarDL,
    gLinkChildLeftFistAndKokiriSwordFarDL,
};

Gfx* sPlayerRightHandOpenDLs[] = {
    gLinkAdultRightHandNearDL,
    gLinkChildRightHandNearDL,
    gLinkAdultRightHandFarDL,
    gLinkChildRightHandFarDL,
};

Gfx* sPlayerRightHandClosedDLs[] = {
    gLinkAdultRightHandClosedNearDL,
    gLinkChildRightHandClosedNearDL,
    gLinkAdultRightHandClosedFarDL,
    gLinkChildRightHandClosedFarDL,
};

Gfx* sPlayerRightHandBowSlingshotDLs[] = {
    gLinkAdultRightHandHoldingBowNearDL,
    gLinkChildRightHandHoldingSlingshotNearDL,
    gLinkAdultRightHandHoldingBowFarDL,
    gLinkChildRightHandHoldingSlingshotFarDL,
};

Gfx* sSwordAndSheathDLs[] = {
    gLinkAdultMasterSwordAndSheathNearDL,
    gLinkChildSwordAndSheathNearDL,
    gLinkAdultMasterSwordAndSheathFarDL,
    gLinkChildSwordAndSheathFarDL,
};

Gfx* sSheathDLs[] = {
    gLinkAdultSheathNearDL,
    gLinkChildSheathNearDL,
    gLinkAdultSheathFarDL,
    gLinkChildSheathFarDL,
};

Gfx* sPlayerWaistDLs[] = {
    gLinkAdultWaistNearDL,
    gLinkChildWaistNearDL,
    gLinkAdultWaistFarDL,
    gLinkChildWaistFarDL,
};

Gfx* sPlayerRightHandBowSlingshotDLs2[] = {
    gLinkAdultRightHandHoldingBowNearDL,
    gLinkChildRightHandHoldingSlingshotNearDL,
    gLinkAdultRightHandHoldingBowFarDL,
    gLinkChildRightHandHoldingSlingshotFarDL,
};

Gfx* sPlayerRightHandOcarinaDLs[] = {
    gLinkAdultRightHandHoldingOotNearDL,
    gLinkChildRightHandHoldingFairyOcarinaNearDL,
    gLinkAdultRightHandHoldingOotFarDL,
    gLinkChildRightHandHoldingFairyOcarinaFarDL,
};

Gfx* sPlayerRightHandOotDLs[] = {
    gLinkAdultRightHandHoldingOotNearDL,
    gLinkChildRightHandAndOotNearDL,
    gLinkAdultRightHandHoldingOotFarDL,
    gLinkChildRightHandHoldingOOTFarDL,
};

Gfx* sPlayerRightHandHookshotDLs[] = {
    gLinkAdultRightHandHoldingHookshotNearDL,
    gLinkChildRightHandNearDL,
    gLinkAdultRightHandHoldingHookshotNearDL, // The 'far' display list exists but is not used
    gLinkChildRightHandFarDL,
};

Gfx* sPlayerLeftHandHammerDLs[] = {
    gLinkAdultLeftHandHoldingHammerNearDL,
    gLinkChildLeftHandNearDL,
    gLinkAdultLeftHandHoldingHammerFarDL,
    gLinkChildLeftHandFarDL,
};

Gfx* gPlayerLeftHandBoomerangDLs[] = {
    gLinkAdultLeftHandNearDL,
    gLinkChildLeftFistAndBoomerangNearDL,
    gLinkAdultLeftHandFarDL,
    gLinkChildLeftFistAndBoomerangFarDL,
};

Gfx* sPlayerLeftHandBottleDLs[] = {
    gLinkAdultLeftHandOutNearDL,
    gLinkChildLeftHandUpNearDL,
    gLinkAdultLeftHandOutNearDL,
    gLinkChildLeftHandUpNearDL,
};

Gfx* sFirstPersonLeftForearmDLs[] = {
    gLinkAdultRightArmOutNearDL,
    NULL,
};

Gfx* sFirstPersonLeftHandDLs[] = {
    gLinkAdultRightHandOutNearDL,
    NULL,
};

Gfx* sFirstPersonRightShoulderDLs[] = {
    gLinkAdultRightShoulderNearDL,
    gLinkChildRightShoulderNearDL,
};

Gfx* sFirstPersonForearmDLs[] = {
    gLinkAdultLeftArmOutNearDL,
    NULL,
};

Gfx* sFirstPersonRightHandHoldingWeaponDLs[] = {
    gLinkAdultRightHandHoldingBowFirstPersonDL,
    gLinkChildRightArmStretchedSlingshotDL,
};

// Indexed by model types (left hand, right hand, sheath or waist)
Gfx** sPlayerDListGroups[PLAYER_MODELTYPE_MAX] = {
    gPlayerLeftHandOpenDLs,           // PLAYER_MODELTYPE_LH_OPEN
    gPlayerLeftHandClosedDLs,         // PLAYER_MODELTYPE_LH_CLOSED
    sPlayerLeftHandSwordDLs,          // PLAYER_MODELTYPE_LH_SWORD
    sPlayerLeftHandSwordDLs2,         // PLAYER_MODELTYPE_LH_SWORD_2
    gPlayerLeftHandBgsDLs,            // PLAYER_MODELTYPE_LH_BGS
    sPlayerLeftHandHammerDLs,         // PLAYER_MODELTYPE_LH_HAMMER
    gPlayerLeftHandBoomerangDLs,      // PLAYER_MODELTYPE_LH_BOOMERANG
    sPlayerLeftHandBottleDLs,         // PLAYER_MODELTYPE_LH_BOTTLE
    sPlayerRightHandOpenDLs,          // PLAYER_MODELTYPE_RH_OPEN
    sPlayerRightHandClosedDLs,        // PLAYER_MODELTYPE_RH_CLOSED
    sPlayerRightHandShieldDLs,        // PLAYER_MODELTYPE_RH_SHIELD
    sPlayerRightHandBowSlingshotDLs,  // PLAYER_MODELTYPE_RH_BOW_SLINGSHOT
    sPlayerRightHandBowSlingshotDLs2, // PLAYER_MODELTYPE_RH_BOW_SLINGSHOT_2
    sPlayerRightHandOcarinaDLs,       // PLAYER_MODELTYPE_RH_OCARINA
    sPlayerRightHandOotDLs,           // PLAYER_MODELTYPE_RH_OOT
    sPlayerRightHandHookshotDLs,      // PLAYER_MODELTYPE_RH_HOOKSHOT
    sSwordAndSheathDLs,               // PLAYER_MODELTYPE_SHEATH_16
    sSheathDLs,                       // PLAYER_MODELTYPE_SHEATH_17
    sSheathWithSwordDLs,              // PLAYER_MODELTYPE_SHEATH_18
    sSheathWithoutSwordDLs,           // PLAYER_MODELTYPE_SHEATH_19
    sPlayerWaistDLs,                  // PLAYER_MODELTYPE_WAIST
};

Gfx gCullBackDList[] = {
    gsSPSetGeometryMode(G_CULL_BACK),
    gsSPEndDisplayList(),
};

Gfx gCullFrontDList[] = {
    gsSPSetGeometryMode(G_CULL_FRONT),
    gsSPEndDisplayList(),
};

Vec3f* D_80160000;
s32 sDListsLodOffset;
Vec3f sGetItemRefPos;
s32 sLeftHandType;
s32 sRightHandType;

void Player_SetBootData(PlayState* play, Player* this) {
    s32 currentBoots;
    s16* bootRegs;

    REG(27) = 2000;
    REG(48) = 370;

    currentBoots = this->currentBoots;
    if (currentBoots == PLAYER_BOOTS_KOKIRI) {
        if (!LINK_IS_ADULT) {
            currentBoots = PLAYER_BOOTS_KOKIRI_CHILD;
        }
    } else if (currentBoots == PLAYER_BOOTS_IRON) {
        if (this->stateFlags1 & PLAYER_STATE1_IN_WATER) {
            currentBoots = PLAYER_BOOTS_IRON_UNDERWATER;
        }
        REG(27) = 500;
        REG(48) = 100;
    }

    bootRegs = sBootData[currentBoots];
#ifdef COMBO_BUILD
    {
        // ComboShip (forms audit 2026-11-13): a transformed Link moves on MM's own row for its form (mm z_player_lib.c
        // D_801BFE14 via D_801BFF90 / func_80123140), not on OOT's Kokiri boots — Goron gravity -140, Deku REG(38)
        // 1000, Fierce Deity run limit 1000, the sinking Goron's row in water. MM's REG(39) has no OOT reader and is
        // dropped; the rest is the same register in both games.
        static s16 sComboFormRegs[][17] = {
            /* FD */ { 200, 666, 200, 700, 366, 200, 600, 175, 800, 1000, -100, 600, 590, 800, 125, 300, 65 },
            /* Goron */ { 200, 1000, 300, 700, 550, 270, 700, 200, 800, 600, -140, 600, 590, 750, 125, 200, 130 },
            /* Goron in water (PLAYER_BOOTS_7) */
            { 80, 800, 150, 700, 480, 270, 600, 50, 800, 300, -40, 400, 540, 270, 25, 0, 80 },
            /* Zora (land = underwater row) */
            { 200, 1000, 300, 700, 550, 270, 700, 300, 800, 600, -100, 600, 590, 750, 125, 200, 130 },
            /* Deku */ { 200, 1000, 300, 700, 550, 270, 600, 1000, 800, 600, -100, 600, 590, 750, 125, 200, 130 },
        };
        s32 row = -1;

        if ((Combo_OotForm() != COMBO_FORM_HUMAN) && (this->actor.id == ACTOR_PLAYER)) {
            // MM func_80123140 has no iron-boots override (OOT's REG(27) 500 / REG(48) 100 above): every form starts
            // from 2000 / 370, so the sunk Zora (OOT iron boots standing in for PLAYER_BOOTS_ZORA_UNDERWATER) turns
            // like MM's, not like iron-booted Link.
            REG(27) = 2000;
            REG(48) = 370;
        }
        switch (Combo_OotForm()) {
            case COMBO_FORM_FIERCE_DEITY:
                row = 0;
                REG(27) = 1200;
                break;
            case COMBO_FORM_GORON:
                row = (this->stateFlags1 & PLAYER_STATE1_IN_WATER) ? 2 : 1;
                REG(48) = 200;
                break;
            case COMBO_FORM_ZORA:
                // MM: PLAYER_BOOTS_ZORA_UNDERWATER in water steps one row on (currentBoots++), onto the row labelled
                // PLAYER_BOOTS_GORON (gravity -140, REG(38) 200) -- the same values as the port's Goron row.
                row =
                    ((this->currentBoots == PLAYER_BOOTS_IRON) && (this->stateFlags1 & PLAYER_STATE1_IN_WATER)) ? 1 : 3;
                break;
            case COMBO_FORM_DEKU:
                row = 4;
                break;
        }
        if ((row >= 0) && (this->actor.id == ACTOR_PLAYER)) {
            bootRegs = sComboFormRegs[row];
        }
    }
#endif
    REG(19) = bootRegs[0];
    REG(30) = bootRegs[1];
    REG(32) = bootRegs[2];
    REG(34) = bootRegs[3];
    REG(35) = bootRegs[4];
    REG(36) = bootRegs[5];
    REG(37) = bootRegs[6];
    REG(38) = bootRegs[7];
    REG(43) = bootRegs[8];
    REG(45) = bootRegs[9];
    REG(68) = bootRegs[10];
    REG(69) = bootRegs[11];
    IREG(66) = bootRegs[12];
    IREG(67) = bootRegs[13];
    IREG(68) = bootRegs[14];
    IREG(69) = bootRegs[15];
    MREG(95) = bootRegs[16];

    if (play->roomCtx.curRoom.behaviorType1 == ROOM_BEHAVIOR_TYPE1_2) {
        REG(45) = 500;
    }
}

// SoH: Custom method used to determine if we're using a custom model for link
uint8_t Player_IsCustomLinkModel() {
    return (LINK_IS_ADULT && ResourceGetIsCustomByName(gLinkAdultSkel)) ||
           (LINK_IS_CHILD && ResourceGetIsCustomByName(gLinkChildSkel));
}

s32 Player_InBlockingCsMode(PlayState* play, Player* this) {
    return (this->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_CUTSCENE)) || (this->csAction != 0) ||
           (play->transitionTrigger == TRANS_TRIGGER_START) || (this->stateFlags1 & PLAYER_STATE1_LOADING) ||
           (this->stateFlags3 & PLAYER_STATE3_FLYING_WITH_HOOKSHOT) ||
           ((gSaveContext.magicState != MAGIC_STATE_IDLE) && (Player_ActionToMagicSpell(this, this->itemAction) >= 0));
}

s32 Player_InCsMode(PlayState* play) {
    Player* this = GET_PLAYER(play);

    return Player_InBlockingCsMode(play, this) || (this->unk_6AD == 4);
}

/**
 * Checks if Player is currently locked onto a hostile actor.
 * `PLAYER_STATE1_HOSTILE_LOCK_ON` controls Player's "battle" response to hostile actors.
 *
 * Note that within Player, `Player_UpdateHostileLockOn` exists, which updates the flag and also returns the check.
 * Player can use this function instead if the flag should be checked, but not updated.
 */
s32 Player_CheckHostileLockOn(Player* this) {
    return (this->stateFlags1 & PLAYER_STATE1_HOSTILE_LOCK_ON);
}

s32 Player_IsChildWithHylianShield(Player* this) {
#ifdef COMBO_BUILD
    if (Combo_OotForm() != COMBO_FORM_HUMAN) {
        return false; // a form guards its own way (MM Player_ActionHandler_11), never crouched behind a shield
    }
#endif
    return gSaveContext.linkAge != 0 && (this->currentShield == PLAYER_SHIELD_HYLIAN);
}

#ifdef COMBO_BUILD
// ComboShip: MM's Great Fairy's Sword in Link's hand in Hyrule. It rides OOT's Biggoron item action (two-handed
// swing, reach, giant-sword damage), but it is MM's sword: MM's model, no durability, the B sword stays sheathed.
s32 gComboGfsInHand = false;
s16 gComboBlastMaskTimer = 0; // ComboShip: MM's blastMaskTimer for the Blast Mask worn in Hyrule (z_player.c ticks it)
#include "ComboGfsGrip.h"     // adult grip for MM's GFS blade, shared with MM's adult-in-Termina draw
static Vec3f sComboGfsAdultGrip = { COMBO_GFS_ADULT_GRIP_X, COMBO_GFS_ADULT_GRIP_Y, COMBO_GFS_ADULT_GRIP_Z };
#define kComboGfsAdultScale COMBO_GFS_ADULT_SCALE
s32 Combo_GfsInHand(Player* this) {
    return gComboGfsInHand && (this->heldItemAction == PLAYER_IA_SWORD_BIGGORON);
}

// ComboShip (owner 2026-11-14: "make sure the double helix sword works in OOT"): Fierce Deity's B is always MM's
// Double Helix Sword (MM gives the form ITEM_SWORD_DEITY on B whatever the human owns). It rides OOT's two-handed
// sword action -- MM's Deity sword is two-handed too, damage 4 / 8 (mm z_player.c D_8085D09C[SWORD_TWO_HANDED]),
// exactly OOT's Biggoron's Sword -- and like MM's it never breaks. His MM hand model carries the blade.
s32 Combo_FdSword(Player* this) {
    return (Combo_OotForm() == COMBO_FORM_FIERCE_DEITY) && (this->heldItemAction == PLAYER_IA_SWORD_BIGGORON);
}

// A two-handed sword with no durability: MM's Great Fairy's Sword or Fierce Deity's Double Helix.
s32 Combo_UnbreakableTwoHander(Player* this) {
    return Combo_GfsInHand(this) || Combo_FdSword(this);
}
#endif

s32 Player_ActionToModelGroup(Player* this, s32 itemAction) {
    s32 modelGroup = sActionModelGroups[itemAction];
#ifdef COMBO_BUILD
    if ((itemAction == PLAYER_IA_SWORD_BIGGORON) && gComboGfsInHand) {
        // MM's PLAYER_MODELGROUP_TWO_HAND_SWORD: closed left hand (MM's blade drawn in it), B sword kept sheathed,
        // two-handed animations (anim type 3) — OOT's model group 10 is exactly that.
        return PLAYER_MODELGROUP_10;
    }
#endif

    if ((modelGroup == PLAYER_MODELGROUP_SWORD_AND_SHIELD) && Player_IsChildWithHylianShield(this)) {
        // child, using kokiri sword with hylian shield equipped
        return PLAYER_MODELGROUP_CHILD_HYLIAN_SHIELD;
    } else {
        return modelGroup;
    }
}

void Player_SetModelsForHoldingShield(Player* this) {
    if ((this->stateFlags1 & PLAYER_STATE1_SHIELDING) &&
        ((this->itemAction < 0) || (this->itemAction == this->heldItemAction))) {
        if ((CVarGetInteger(CVAR_CHEAT("ShieldTwoHanded"), 0) && (this->heldItemAction != PLAYER_IA_DEKU_STICK) ||
             !Player_HoldsTwoHandedWeapon(this)) &&
            !Player_IsChildWithHylianShield(this)) {
            this->rightHandType = PLAYER_MODELTYPE_RH_SHIELD;
            if (LINK_IS_CHILD && (CVarGetInteger(CVAR_ENHANCEMENT("EquipmentAlwaysVisible"), 0)) &&
                (this->currentShield == PLAYER_SHIELD_MIRROR)) {
                this->rightHandDLists = &sPlayerDListGroups[PLAYER_MODELTYPE_RH_SHIELD][0];
            } else if (LINK_IS_ADULT && (CVarGetInteger(CVAR_ENHANCEMENT("EquipmentAlwaysVisible"), 0)) &&
                       (this->currentShield == PLAYER_SHIELD_DEKU)) {
                this->rightHandDLists = &sPlayerDListGroups[PLAYER_MODELTYPE_RH_SHIELD][1];
            } else {
                this->rightHandDLists = &sPlayerDListGroups[PLAYER_MODELTYPE_RH_SHIELD][gSaveContext.linkAge];
            }
            if (this->sheathType == PLAYER_MODELTYPE_SHEATH_18) {
                this->sheathType = PLAYER_MODELTYPE_SHEATH_16;
            } else if (this->sheathType == PLAYER_MODELTYPE_SHEATH_19) {
                this->sheathType = PLAYER_MODELTYPE_SHEATH_17;
            }
            this->sheathDLists = &sPlayerDListGroups[this->sheathType][gSaveContext.linkAge];
            if ((CVarGetInteger(CVAR_ENHANCEMENT("EquipmentAlwaysVisible"), 0)) && LINK_IS_CHILD &&
                gSaveContext.equips.buttonItems[0] != ITEM_SWORD_KOKIRI) {
                this->sheathDLists = &sPlayerDListGroups[this->sheathType][0];
            } else if ((CVarGetInteger(CVAR_ENHANCEMENT("EquipmentAlwaysVisible"), 0)) && LINK_IS_ADULT &&
                       gSaveContext.equips.buttonItems[0] == ITEM_SWORD_KOKIRI) {
                this->sheathDLists = &sPlayerDListGroups[this->sheathType][1];
            }
            this->modelAnimType = PLAYER_ANIMTYPE_2;
            this->itemAction = -1;
        }
    }
}

void Player_SetModels(Player* this, s32 modelGroup) {
    // Left hand
    this->leftHandType = gPlayerModelTypes[modelGroup][PLAYER_MODELGROUPENTRY_LEFT_HAND];
    this->leftHandDLists = &sPlayerDListGroups[this->leftHandType][gSaveContext.linkAge];

    if (CVarGetInteger(CVAR_ENHANCEMENT("EquipmentAlwaysVisible"), 0)) {
        if (LINK_IS_CHILD &&
            (this->leftHandType == PLAYER_MODELTYPE_LH_HAMMER ||
             ((this->leftHandType == PLAYER_MODELTYPE_LH_SWORD || this->leftHandType == PLAYER_MODELTYPE_LH_BGS) &&
              (gSaveContext.equips.buttonItems[0] != ITEM_SWORD_KOKIRI)))) {
            this->leftHandDLists = &sPlayerDListGroups[this->leftHandType][0];
        }

        if (LINK_IS_ADULT && (this->leftHandType == PLAYER_MODELTYPE_LH_BOOMERANG ||
                              (this->leftHandType == PLAYER_MODELTYPE_LH_SWORD &&
                               gSaveContext.equips.buttonItems[0] == ITEM_SWORD_KOKIRI))) {
            this->leftHandDLists = &sPlayerDListGroups[this->leftHandType][1];
        }
    }

    // Right hand
    this->rightHandType = gPlayerModelTypes[modelGroup][PLAYER_MODELGROUPENTRY_RIGHT_HAND];
    this->rightHandDLists = &sPlayerDListGroups[this->rightHandType][gSaveContext.linkAge];

    this->rightHandType = gPlayerModelTypes[modelGroup][PLAYER_MODELGROUPENTRY_RIGHT_HAND];
    this->rightHandDLists = &sPlayerDListGroups[this->rightHandType][gSaveContext.linkAge];

    if (CVarGetInteger(CVAR_ENHANCEMENT("EquipmentAlwaysVisible"), 0)) {
        if (LINK_IS_CHILD &&
            (this->rightHandType == PLAYER_MODELTYPE_RH_HOOKSHOT ||
             (this->rightHandType == PLAYER_MODELTYPE_RH_SHIELD && this->currentShield == PLAYER_SHIELD_MIRROR))) {
            this->rightHandDLists = &sPlayerDListGroups[this->rightHandType][0];
        }
        if (LINK_IS_ADULT &&
            (this->rightHandType == PLAYER_MODELTYPE_RH_SHIELD && this->currentShield == PLAYER_SHIELD_DEKU)) {
            this->rightHandDLists = &sPlayerDListGroups[this->rightHandType][1];
        }
    }
    if ((CVarGetInteger(CVAR_ENHANCEMENT("BowSlingshotAmmoFix"), 0) ||
         CVarGetInteger(CVAR_ENHANCEMENT("EquipmentAlwaysVisible"), 0)) &&
        this->rightHandType == 11) { // If holding Bow/Slingshot
        this->rightHandDLists = &sPlayerDListGroups[this->rightHandType][Player_HoldsSlingshot(this)];
    }

    // Sheath
    this->sheathType = gPlayerModelTypes[modelGroup][PLAYER_MODELGROUPENTRY_SHEATH];
    this->sheathDLists = &sPlayerDListGroups[this->sheathType][gSaveContext.linkAge];

    if (CVarGetInteger(CVAR_ENHANCEMENT("EquipmentAlwaysVisible"), 0)) {
        if (LINK_IS_CHILD && (this->currentShield == PLAYER_SHIELD_HYLIAN &&
                                  ((gSaveContext.equips.buttonItems[0] == ITEM_SWORD_MASTER) ||
                                   (gSaveContext.equips.buttonItems[0] == ITEM_SWORD_BGS)) ||
                              (this->currentShield == PLAYER_SHIELD_MIRROR) &&
                                  (gSaveContext.equips.buttonItems[0] != ITEM_SWORD_KOKIRI))) {
            this->sheathDLists = &sPlayerDListGroups[this->sheathType][0];
        } else if (LINK_IS_CHILD && this->currentShield == PLAYER_SHIELD_MIRROR &&
                   gSaveContext.equips.buttonItems[0] == ITEM_SWORD_KOKIRI &&
                   this->sheathType == PLAYER_MODELTYPE_SHEATH_18) {
            this->sheathDLists = &sPlayerDListGroups[this->sheathType][0];
        } else if (LINK_IS_ADULT && (this->currentShield == PLAYER_SHIELD_DEKU &&
                                     gSaveContext.equips.buttonItems[0] != ITEM_SWORD_MASTER) ||
                   (gSaveContext.equips.buttonItems[0] == ITEM_SWORD_MASTER &&
                    this->sheathType == PLAYER_MODELTYPE_SHEATH_18 && this->currentShield == PLAYER_SHIELD_DEKU)) {
            this->sheathDLists = &sPlayerDListGroups[this->sheathType][1];
        } else if (LINK_IS_CHILD && this->sheathType == PLAYER_MODELTYPE_SHEATH_17 &&
                   ((gSaveContext.equips.buttonItems[0] == ITEM_SWORD_MASTER) ||
                    (gSaveContext.equips.buttonItems[0] == ITEM_SWORD_BGS))) {
            this->sheathDLists = &sPlayerDListGroups[this->sheathType][0];
        }
    }

    // Waist
    this->waistDLists = &sPlayerDListGroups[gPlayerModelTypes[modelGroup][4]][gSaveContext.linkAge];

    Player_SetModelsForHoldingShield(this);
    GameInteractor_ExecuteOnPlayerSetModels(this, modelGroup);
}

void Player_SetModelGroup(Player* this, s32 modelGroup) {
    this->modelGroup = modelGroup;

    if (modelGroup == PLAYER_MODELGROUP_CHILD_HYLIAN_SHIELD) {
        this->modelAnimType = PLAYER_ANIMTYPE_0;
    } else {
        this->modelAnimType = gPlayerModelTypes[modelGroup][PLAYER_MODELGROUPENTRY_ANIM];
    }

    if ((this->modelAnimType < PLAYER_ANIMTYPE_3) && (this->currentShield == PLAYER_SHIELD_NONE)) {
        this->modelAnimType = PLAYER_ANIMTYPE_0;
    }

    Player_SetModels(this, modelGroup);
}

void func_8008EC70(Player* this) {
    this->itemAction = this->heldItemAction;
    Player_SetModelGroup(this, Player_ActionToModelGroup(this, this->heldItemAction));
    this->unk_6AD = 0;
}

void Player_SetEquipmentData(PlayState* play, Player* this) {
    if (this->csAction != 0x56) {
        this->currentShield = SHIELD_EQUIP_TO_PLAYER(CUR_EQUIP_VALUE(EQUIP_TYPE_SHIELD));
        this->currentTunic = TUNIC_EQUIP_TO_PLAYER(CUR_EQUIP_VALUE(EQUIP_TYPE_TUNIC));
        this->currentBoots = BOOTS_EQUIP_TO_PLAYER(CUR_EQUIP_VALUE(EQUIP_TYPE_BOOTS));
#ifdef COMBO_BUILD
        // ComboShip (owner 2026-11-14: "boots and tunics don't affect transformations"): equipping boots or a tunic
        // while transformed changes the save, not the transformed Link -- he keeps no boots and no tunic (MM's forms
        // have neither). Human Link gets them back (Combo_FormBoots in z_player.c restores from the save).
        if (Combo_OotForm() != COMBO_FORM_HUMAN) {
            this->currentBoots = PLAYER_BOOTS_KOKIRI;
            if (Combo_OotForm() != COMBO_FORM_FIERCE_DEITY) { // owner: Fierce Deity is human -- he keeps the tunic
                this->currentTunic = PLAYER_TUNIC_KOKIRI;
            }
        }
#endif
        this->currentSwordItemId = B_BTN_ITEM;
        Player_SetModelGroup(this, Player_ActionToModelGroup(this, this->heldItemAction));
        Player_SetBootData(play, this);
    }
}

void Player_UpdateBottleHeld(PlayState* play, Player* this, s32 item, s32 itemAction) {
#ifdef COMBO_BUILD
    // ComboShip (MM items in Hyrule): an MM bottle on this button lives in MM's save; drinking / pouring / catching
    // changes MM's own slot (the button keeps its foreign id, which now names the new content).
    if ((this->heldItemButton >= 0) && Combo_PauseIsForeign(gSaveContext.equips.buttonItems[this->heldItemButton]) &&
        Combo_PauseSetForeignBottle(gSaveContext.equips.buttonItems[this->heldItemButton], item)) {
        gSaveContext.buttonStatus[(this->heldItemButton >= 4) ? (this->heldItemButton + 1) : this->heldItemButton] =
            BTN_ENABLED;
    } else
#endif
        Inventory_UpdateBottleItem(play, item, this->heldItemButton);

    if (item != ITEM_BOTTLE) {
        this->heldItemId = item;
        this->heldItemAction = itemAction;
    }

    if (GameInteractor_Should(VB_PLAYER_UPDATE_BOTTLE_HELD, true, this)) {
        this->itemAction = itemAction;
    }
}

void Player_ReleaseLockOn(Player* this) {
    this->focusActor = NULL;
    this->stateFlags2 &= ~PLAYER_STATE2_LOCK_ON_WITH_SWITCH;
}

/**
 * This function aims to clear Z-Target related state when it isn't in use.
 * It also handles setting a specific free fall related state that is interntwined with Z-Targeting.
 * TODO: Learn more about this and give a name to PLAYER_STATE1_19
 */
void Player_ClearZTargeting(Player* this) {
    if ((this->actor.bgCheckFlags & BGCHECKFLAG_GROUND) ||
        (this->stateFlags1 & (PLAYER_STATE1_CLIMBING_LADDER | PLAYER_STATE1_ON_HORSE | PLAYER_STATE1_IN_WATER)) ||
        (!(this->stateFlags1 & (PLAYER_STATE1_JUMPING | PLAYER_STATE1_FREEFALL)) &&
         ((this->actor.world.pos.y - this->actor.floorHeight) < 100.0f))) {
        this->stateFlags1 &=
            ~(PLAYER_STATE1_Z_TARGETING | PLAYER_STATE1_FRIENDLY_ACTOR_FOCUS | PLAYER_STATE1_PARALLEL |
              PLAYER_STATE1_JUMPING | PLAYER_STATE1_FREEFALL | PLAYER_STATE1_LOCK_ON_FORCED_TO_RELEASE);
    } else if (!(this->stateFlags1 &
                 (PLAYER_STATE1_JUMPING | PLAYER_STATE1_FREEFALL | PLAYER_STATE1_CLIMBING_LADDER))) {
        this->stateFlags1 |= PLAYER_STATE1_FREEFALL;
    }

    Player_ReleaseLockOn(this);
}

/**
 * Sets the "auto lock-on actor" to lock onto an actor without Player's input.
 * This function will first release any existing lock-on or (try to) release parallel.
 *
 * When using Switch Targeting, it is not possible to carry an auto lock-on actor into a normal
 * lock-on when the auto lock-on is finished.
 * This is because the `PLAYER_STATE2_LOCK_ON_WITH_SWITCH` flag is never set with an auto lock-on.
 * With Hold Targeting it is possible to keep the auto lock-on going by keeping the Z button held down.
 *
 * The auto lock-on is considered "friendly" even if the actor is actually hostile. If the auto lock-on is hostile,
 * Player's battle response will not occur (if he is actionable) and the camera behaves differently.
 * When transitioning from auto lock-on to normal lock-on (with Hold Targeting) there will be a noticeable change
 * when it switches from "friendly" mode to "hostile" mode.
 */
void Player_SetAutoLockOnActor(PlayState* play, Actor* actor) {
    Player* this = GET_PLAYER(play);

    Player_ClearZTargeting(this);
    this->focusActor = actor;
    this->autoLockOnActor = actor;
    this->stateFlags1 |= PLAYER_STATE1_FRIENDLY_ACTOR_FOCUS;
    Camera_SetParam(Play_GetCamera(play, CAM_ID_MAIN), 8, actor);
    Camera_RequestMode(Play_GetCamera(play, CAM_ID_MAIN), 2);
}

s32 func_8008EF30(PlayState* play) {
    Player* this = GET_PLAYER(play);

    return (this->stateFlags1 & PLAYER_STATE1_ON_HORSE);
}

s32 func_8008EF44(PlayState* play, s32 ammo) {
    play->shootingGalleryStatus = ammo + 1;
    return 1;
}

s32 Player_IsBurningStickInRange(PlayState* play, Vec3f* pos, f32 xzRange, f32 yRange) {
    Player* this = GET_PLAYER(play);
    Vec3f diff;
    s32 pad;

    if ((this->heldItemAction == PLAYER_IA_DEKU_STICK) && (this->unk_860 != 0)) {
        Math_Vec3f_Diff(&this->meleeWeaponInfo[0].tip, pos, &diff);
        return ((SQ(diff.x) + SQ(diff.z)) <= SQ(xzRange)) && (0.0f <= diff.y) && (diff.y <= yRange);
    } else {
        return false;
    }
}

s32 Player_GetStrength(void) {
    s32 strengthUpgrade = CUR_UPG_VALUE(UPG_STRENGTH);

#ifdef COMBO_BUILD
    // ComboShip (form moves as items): Goron Link lifts what the Golden Gauntlets lift (bomb flowers, silver rocks,
    // the giant blocks), as MM's Goron hauls powder kegs and boulders.
    if ((Combo_OotForm() == COMBO_FORM_GORON) && CVarGetInteger("gCombo.Forms.ItemAnalogs", 1)) {
        return PLAYER_STR_GOLD_G;
    }
#endif
    if (CVarGetInteger(CVAR_ENHANCEMENT("ToggleStrength"), 0) &&
        CVarGetInteger(CVAR_ENHANCEMENT("StrengthDisabled"), 0)) {
        return PLAYER_STR_NONE;
    }

    if (GameInteractor_Should(VB_PLAYER_MEETS_AGE_REQ, LINK_IS_ADULT, LINK_AGE_ADULT)) {
        return strengthUpgrade;
    } else if (strengthUpgrade != 0) {
        return PLAYER_STR_BRACELET;
    } else {
        return PLAYER_STR_NONE;
    }
}

u8 Player_GetMask(PlayState* play) {
    Player* this = GET_PLAYER(play);

    return this->currentMask;
}

Player* Player_UnsetMask(PlayState* play) {
    Player* this = GET_PLAYER(play);

    this->currentMask = PLAYER_MASK_NONE;

    return this;
}

s32 Player_HasMirrorShieldEquipped(PlayState* play) {
    Player* this = GET_PLAYER(play);

    return (this->currentShield == PLAYER_SHIELD_MIRROR);
}

s32 Player_HasMirrorShieldSetToDraw(PlayState* play) {
    Player* this = GET_PLAYER(play);

    return (this->rightHandType == PLAYER_MODELTYPE_RH_SHIELD) && (this->currentShield == PLAYER_SHIELD_MIRROR);
}

s32 Player_ActionToMagicSpell(Player* this, s32 itemAction) {
    s32 magicSpell = itemAction - PLAYER_IA_MAGIC_SPELL_15;

    if ((magicSpell >= 0) && (magicSpell < 6)) {
        return magicSpell;
    } else {
        return -1;
    }
}

s32 Player_HoldsHookshot(Player* this) {
    return (this->heldItemAction == PLAYER_IA_HOOKSHOT) || (this->heldItemAction == PLAYER_IA_LONGSHOT);
}

// SoH
s32 Player_HoldsBow(Player* this) {
    switch (this->heldItemAction) {
        case PLAYER_IA_BOW:
        case PLAYER_IA_BOW_FIRE:
        case PLAYER_IA_BOW_ICE:
        case PLAYER_IA_BOW_LIGHT:
            return true;
        default:
            return false;
    }
}

// SoH
s32 Player_HoldsSlingshot(Player* this) {
    return this->heldItemAction == PLAYER_IA_SLINGSHOT;
}

s32 func_8008F128(Player* this) {
    return Player_HoldsHookshot(this) && (this->heldActor == NULL);
}

s32 Player_ActionToMeleeWeapon(s32 itemAction) {
    s32 sword = itemAction - PLAYER_IA_FISHING_POLE;

    if ((sword > 0) && (sword < 6)) {
        return sword;
    } else {
        return 0;
    }
}

s32 Player_GetMeleeWeaponHeld(Player* this) {
    return Player_ActionToMeleeWeapon(this->heldItemAction);
}

s32 Player_HoldsTwoHandedWeapon(Player* this) {
    if ((this->heldItemAction >= PLAYER_IA_SWORD_BIGGORON) && (this->heldItemAction <= PLAYER_IA_HAMMER)) {
        return true;
    } else {
        return false;
    }
}

s32 Player_HoldsBrokenKnife(Player* this) {
#ifdef COMBO_BUILD
    if (Combo_UnbreakableTwoHander(this)) {
        return false; // MM's Great Fairy's Sword / Fierce Deity's Double Helix never break
    }
#endif
    return (this->heldItemAction == PLAYER_IA_SWORD_BIGGORON) && (gSaveContext.swordHealth <= 0.0f);
}

s32 Player_ActionToBottle(Player* this, s32 itemAction) {
    s32 bottle = itemAction - PLAYER_IA_BOTTLE;

    if ((bottle >= 0) && (bottle < 13)) {
        return bottle;
    } else {
        return -1;
    }
}

s32 Player_GetBottleHeld(Player* this) {
    return Player_ActionToBottle(this, this->heldItemAction);
}

s32 Player_ActionToExplosive(Player* this, s32 itemAction) {
    s32 explosive = itemAction - PLAYER_IA_BOMB;

    if ((explosive >= 0) && (explosive < 2)) {
        return explosive;
    } else {
        return -1;
    }
}

s32 Player_GetExplosiveHeld(Player* this) {
    return Player_ActionToExplosive(this, this->heldItemAction);
}

s32 func_8008F2BC(Player* this, s32 itemAction) {
    s32 sword = 0;

    if (itemAction != PLAYER_IA_SWORD_CS) {
        sword = itemAction - PLAYER_IA_SWORD_MASTER;
        if ((sword < 0) || (sword >= 3)) {
            goto return_neg;
        }
    }

    return sword;

return_neg:
    return -1;
}

s32 Player_GetEnvironmentalHazard(PlayState* play) {
    Player* this = GET_PLAYER(play);
    TextTriggerEntry* triggerEntry;
    s32 envHazard;

    if (play->roomCtx.curRoom.behaviorType2 == ROOM_BEHAVIOR_TYPE2_3) { // Room is hot
        envHazard = PLAYER_ENV_HAZARD_HOTROOM - 1;
    } else if (((this->underwaterTimer > 80) &&
                ((this->currentBoots == PLAYER_BOOTS_IRON) || (this->underwaterTimer >= 300)))
#ifdef COMBO_BUILD
               // The sunk Zora: his breath timer never runs (MM: no breath for the Zora), so OOT's "deep" test never
               // passed and the floor rule (C buttons: hookshot / longshot usable) never applied to him.
               || ((Combo_OotForm() == COMBO_FORM_ZORA) && (this->currentBoots == PLAYER_BOOTS_IRON) &&
                   (this->stateFlags1 & PLAYER_STATE1_IN_WATER))
#endif
    ) { // Deep underwater
        envHazard = ((this->currentBoots == PLAYER_BOOTS_IRON) && (this->actor.bgCheckFlags & BGCHECKFLAG_GROUND))
                        ? (PLAYER_ENV_HAZARD_UNDERWATER_FLOOR - 1)
                        : (PLAYER_ENV_HAZARD_UNDERWATER_FREE - 1);
    } else if (this->stateFlags1 & PLAYER_STATE1_IN_WATER) { // Swimming
        envHazard = PLAYER_ENV_HAZARD_SWIMMING - 1;
    } else {
        return PLAYER_ENV_HAZARD_NONE;
    }

    // Trigger general textboxes under certain conditions, like "It's so hot in here!"
    if (!Player_InCsMode(play)) {
        triggerEntry = &sTextTriggers[envHazard];

        if ((triggerEntry->flag != 0) && !(gSaveContext.textTriggerFlags & triggerEntry->flag) &&
            (((envHazard == (PLAYER_ENV_HAZARD_HOTROOM - 1)) &&
              (this->currentTunic != PLAYER_TUNIC_GORON && CVarGetInteger(CVAR_CHEAT("SuperTunic"), 0) == 0 &&
               !COMBO_HEAT_PROOF() && CVarGetInteger(CVAR_ENHANCEMENT("DisableTunicWarningText"), 0) == 0)) ||
             (((envHazard == (PLAYER_ENV_HAZARD_UNDERWATER_FLOOR - 1)) ||
               (envHazard == (PLAYER_ENV_HAZARD_UNDERWATER_FREE - 1))) &&
              (this->currentBoots == PLAYER_BOOTS_IRON) &&
              (this->currentTunic != PLAYER_TUNIC_ZORA && CVarGetInteger(CVAR_CHEAT("SuperTunic"), 0) == 0 &&
               !COMBO_WATER_PROOF() && CVarGetInteger(CVAR_ENHANCEMENT("DisableTunicWarningText"), 0) == 0)))) {
            Message_StartTextbox(play, triggerEntry->textId, NULL);
            gSaveContext.textTriggerFlags |= triggerEntry->flag;
        }
    }

    return envHazard + 1;
}

u8 sEyeMouthIndexes[][2] = {
    { 0, 0 }, { 1, 0 }, { 2, 0 }, { 0, 0 }, { 1, 0 }, { 2, 0 }, { 4, 0 }, { 5, 1 },
    { 7, 2 }, { 0, 2 }, { 3, 0 }, { 4, 0 }, { 2, 2 }, { 1, 1 }, { 0, 2 }, { 0, 0 },
};

/**
 * Link's eye and mouth textures are placed at the exact same place in adult and child Link's respective object files.
 * This allows the array to only contain the symbols for one file and have it apply to both. This is a problem for
 * shiftability, and changes will need to be made in the code to account for this in a modding scenario. The symbols
 * from adult Link's object are used here.
 */

#if defined(MODDING) || defined(_MSC_VER) || defined(__GNUC__)
// TODO: Formatting
void* sEyeTextures[2][8] = {
    { gLinkAdultEyesOpenTex, gLinkAdultEyesHalfTex, gLinkAdultEyesClosedfTex, gLinkAdultEyesRollLeftTex,
      gLinkAdultEyesRollRightTex, gLinkAdultEyesShockTex, gLinkAdultEyesUnk1Tex, gLinkAdultEyesUnk2Tex },
    { gLinkChildEyesOpenTex, gLinkChildEyesHalfTex, gLinkChildEyesClosedfTex, gLinkChildEyesRollLeftTex,
      gLinkChildEyesRollRightTex, gLinkChildEyesShockTex, gLinkChildEyesUnk1Tex, gLinkChildEyesUnk2Tex },
};

#else
void* sEyeTextures[] = {
    gLinkAdultEyesOpenTex,      gLinkAdultEyesHalfTex,  gLinkAdultEyesClosedfTex, gLinkAdultEyesRollLeftTex,
    gLinkAdultEyesRollRightTex, gLinkAdultEyesShockTex, gLinkAdultEyesUnk1Tex,    gLinkAdultEyesUnk2Tex,
};
#endif

#if defined(MODDING) || defined(_MSC_VER) || defined(__GNUC__)
void* sMouthTextures[2][4] = {
    {
        gLinkAdultMouth1Tex,
        gLinkAdultMouth2Tex,
        gLinkAdultMouth3Tex,
        gLinkAdultMouth4Tex,
    },
    {
        gLinkChildMouth1Tex,
        gLinkChildMouth2Tex,
        gLinkChildMouth3Tex,
        gLinkChildMouth4Tex,
    },
};
#else
void* sMouthTextures[] = {
    gLinkAdultMouth1Tex,
    gLinkAdultMouth2Tex,
    gLinkAdultMouth3Tex,
    gLinkAdultMouth4Tex,
};
#endif

Color_RGB8 sTunicColors[] = {
    { 30, 105, 27 },
    { 100, 20, 0 },
    { 0, 60, 100 },
};

Color_RGB8 sGauntletColors[] = {
    { 255, 255, 255 },
    { 254, 207, 15 },
    // #region SOH [RBA] values matching OOB reads on N64
    { 0, 0, 6 },
    { 2, 89, 24 },
    { 6, 2, 90 },
    { 96, 6, 2 },
};

Gfx* sBootDListGroups[][2] = {
    { gLinkAdultLeftIronBootDL, gLinkAdultRightIronBootDL },   // PLAYER_BOOTS_IRON
    { gLinkAdultLeftHoverBootDL, gLinkAdultRightHoverBootDL }, // PLAYER_BOOTS_HOVER
};

void Player_DrawImpl(PlayState* play, void** skeleton, Vec3s* jointTable, s32 dListCount, s32 lod, s32 tunic, s32 boots,
                     s32 face, OverrideLimbDrawOpa overrideLimbDraw, PostLimbDrawOpa postLimbDraw, void* data) {
    Color_RGB8* color;
    s32 eyeIndex = (jointTable[22].x & 0xF) - 1;
    s32 mouthIndex = (jointTable[22].x >> 4) - 1;

    OPEN_DISPS(play->state.gfxCtx);

    if (eyeIndex < 0) {
        eyeIndex = sEyeMouthIndexes[face][0];
    }

    if (eyeIndex > 7)
        eyeIndex = 7;

#if defined(MODDING) || defined(_MSC_VER) || defined(__GNUC__)
    gSPSegment(POLY_OPA_DISP++, 0x08, SEGMENTED_TO_VIRTUAL(sEyeTextures[gSaveContext.linkAge][eyeIndex]));
#else
    gSPSegment(POLY_OPA_DISP++, 0x08, SEGMENTED_TO_VIRTUAL(sEyeTextures[eyeIndex]));
#endif
    if (mouthIndex < 0) {
        mouthIndex = sEyeMouthIndexes[face][1];
    }

    if (mouthIndex > 3)
        mouthIndex = 3;

#if defined(MODDING) || defined(_MSC_VER) || defined(__GNUC__)
    gSPSegment(POLY_OPA_DISP++, 0x09, SEGMENTED_TO_VIRTUAL(sMouthTextures[gSaveContext.linkAge][mouthIndex]));
#else
    gSPSegment(POLY_OPA_DISP++, 0x09, SEGMENTED_TO_VIRTUAL(sMouthTextures[eyeIndex]));
#endif

    Color_RGB8 sTemp;
    color = &sTunicColors[tunic];
    if (tunic == PLAYER_TUNIC_KOKIRI && CVarGetInteger(CVAR_COSMETIC("Link.KokiriTunic.Changed"), 0)) {
        sTemp = CVarGetColor24(CVAR_COSMETIC("Link.KokiriTunic.Value"), sTunicColors[PLAYER_TUNIC_KOKIRI]);
        color = &sTemp;
    } else if (tunic == PLAYER_TUNIC_GORON && CVarGetInteger(CVAR_COSMETIC("Link.GoronTunic.Changed"), 0)) {
        sTemp = CVarGetColor24(CVAR_COSMETIC("Link.GoronTunic.Value"), sTunicColors[PLAYER_TUNIC_GORON]);
        color = &sTemp;
    } else if (tunic == PLAYER_TUNIC_ZORA && CVarGetInteger(CVAR_COSMETIC("Link.ZoraTunic.Changed"), 0)) {
        sTemp = CVarGetColor24(CVAR_COSMETIC("Link.ZoraTunic.Value"), sTunicColors[PLAYER_TUNIC_ZORA]);
        color = &sTemp;
    }

    if (GameInteractor_Should(VB_APPLY_TUNIC_COLOR, true, data, color)) {
        gDPSetEnvColor(POLY_OPA_DISP++, color->r, color->g, color->b, 0);
    }
#ifdef COMBO_BUILD
    sComboTunicEnv = *color; // ComboShip: restored after an MM mask that sets its own env colour (Blast Mask)
#endif

    // If we have a custom link model, always use the most detailed LOD
    if (Player_IsCustomLinkModel()) {
        lod = 0;
    }

    sDListsLodOffset = lod * 2;

#ifdef COMBO_BUILD
    // ComboShip (owner ask 2026-11-12): a Majora's Mask form in Hyrule draws the form's MM body (ComboForms.cpp).
    // Not the pause screen's Link: he previews the human's equipment, and his data is a u8[2] of sword and shield, not
    // an actor -- read as one, "id == ACTOR_PLAYER" held whenever neither was equipped, the form's override then
    // treated the array as a Player and pausing as Zora crashed (owner 2026-11-14, Gerudo Valley / the hideout).
    extern s32 Player_OverrideLimbDrawPause(PlayState * play, s32 limbIndex, Gfx * *dList, Vec3f * pos, Vec3s * rot,
                                            void* arg);
    const s32 comboForm = (overrideLimbDraw != Player_OverrideLimbDrawPause) && Combo_OotFormDrawActive(data);
    if (comboForm) {
        POLY_OPA_DISP = (Gfx*)Combo_OotFormDrawPrep(POLY_OPA_DISP, eyeIndex, mouthIndex);
        skeleton = Combo_OotFormLimbs(&dListCount);
        overrideLimbDraw = (OverrideLimbDrawOpa)Combo_OotFormWrapOverride((void*)overrideLimbDraw);
        lod = 0;
    }
#else
    const s32 comboForm = 0;
#endif
#ifdef COMBO_BUILD
    extern s32 gComboGoronBall;
    extern s16 Combo_ZoraBarrierStrength(void);
    extern void Combo_GoronCurledBodyParts(Player * player);
    if (comboForm && (Combo_OotForm() == COMBO_FORM_ZORA) && (Combo_ZoraBarrierStrength() != 0)) {
        // MM Player_Draw: the barrier shell, stood up and lifted to Link's middle, before the body
        OPEN_DISPS(play->state.gfxCtx);
        gSPClearGeometryMode(POLY_XLU_DISP++, G_CULL_BOTH);
        CLOSE_DISPS(play->state.gfxCtx);
        Matrix_Push();
        Matrix_RotateX(-M_PI / 2.0f, MTXMODE_APPLY);
        Matrix_Translate(0.0f, 0.0f, -1800.0f, MTXMODE_APPLY);
        Combo_ZoraBarrierDraw(play, Combo_ZoraBarrierStrength());
        Matrix_Pop();
    }
    if (comboForm && gComboGoronBall && (Combo_OotForm() == COMBO_FORM_GORON)) {
        Combo_GoronCurledBodyParts((Player*)data);                // MM Player_Draw: func_80846460, then the ball
        Combo_OotFormDrawBall(play, ((Actor*)data)->shape.rot.y); // curled up and rolling (only ever as a Goron)
    } else if (comboForm && (Combo_OotForm() == COMBO_FORM_GORON) &&
               (((Player*)data)->stateFlags1 & PLAYER_STATE1_SHIELDING) && Combo_GoronGuardDraw(play)) {
        // MM: the Goron's guard is its own skeleton (gLinkGoronShieldingSkel), drawn instead of the body
    } else
#endif
        SkelAnime_DrawFlexLod(play, skeleton, jointTable, dListCount, overrideLimbDraw, postLimbDraw, data, lod);

    if (!comboForm && !GameInteractor_InvisibleLinkActive() &&
        ((CVarGetInteger(CVAR_ENHANCEMENT("FirstPersonGauntlets"), 0) && LINK_IS_ADULT) ||
         (overrideLimbDraw != Player_OverrideLimbDrawGameplayFirstPerson)) &&
        (overrideLimbDraw != Player_OverrideLimbDrawGameplayCrawling) &&
        (gSaveContext.gameMode != GAMEMODE_END_CREDITS)) {
        if (LINK_IS_ADULT) {
            s32 strengthUpgrade = CUR_UPG_VALUE(UPG_STRENGTH);

            if (strengthUpgrade >= 2) { // silver or gold gauntlets
                gDPPipeSync(POLY_OPA_DISP++);

                color = &sGauntletColors[strengthUpgrade - 2];
                if (strengthUpgrade == PLAYER_STR_SILVER_G &&
                    CVarGetInteger(CVAR_COSMETIC("Gloves.SilverGauntlets.Changed"), 0)) {
                    sTemp = CVarGetColor24(CVAR_COSMETIC("Gloves.SilverGauntlets.Value"), *color);
                    color = &sTemp;
                } else if (strengthUpgrade == PLAYER_STR_GOLD_G &&
                           CVarGetInteger(CVAR_COSMETIC("Gloves.GoldenGauntlets.Changed"), 0)) {
                    sTemp = CVarGetColor24(CVAR_COSMETIC("Gloves.GoldenGauntlets.Value"), *color);
                    color = &sTemp;
                }
                gDPSetEnvColor(POLY_OPA_DISP++, color->r, color->g, color->b, 0);

                gSPDisplayList(POLY_OPA_DISP++, gLinkAdultLeftGauntletPlate1DL);
                gSPDisplayList(POLY_OPA_DISP++, gLinkAdultRightGauntletPlate1DL);
                gSPDisplayList(POLY_OPA_DISP++, (sLeftHandType == PLAYER_MODELTYPE_LH_OPEN)
                                                    ? gLinkAdultLeftGauntletPlate2DL
                                                    : gLinkAdultLeftGauntletPlate3DL);
                gSPDisplayList(POLY_OPA_DISP++, (sRightHandType == PLAYER_MODELTYPE_RH_OPEN)
                                                    ? gLinkAdultRightGauntletPlate2DL
                                                    : gLinkAdultRightGauntletPlate3DL);
            }

            if (boots != 0) {
                Gfx** bootDLists = sBootDListGroups[boots - 1];

                gSPDisplayList(POLY_OPA_DISP++, bootDLists[0]);
                gSPDisplayList(POLY_OPA_DISP++, bootDLists[1]);
            }
        } else {
            if (Player_GetStrength() > PLAYER_STR_NONE) {
                gSPDisplayList(POLY_OPA_DISP++, gLinkChildGoronBraceletDL);
            }
        }
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

Vec3f sZeroVec = { 0.0f, 0.0f, 0.0f };

Vec3f D_80126038[] = {
    { 1304.0f, 0.0f, 0.0f },
    { 695.0f, 0.0f, 0.0f },
};

f32 D_80126050[] = { 1265.0f, 826.0f };
f32 D_80126058[] = { SQ(13.04f), SQ(6.95f) };
f32 D_80126060[] = { 10.019104f, -19.925102f };
f32 D_80126068[] = { 5.0f, 3.0f };

Vec3f D_80126070 = { 0.0f, -300.0f, 0.0f };

void func_8008F87C(PlayState* play, Player* this, SkelAnime* skelAnime, Vec3f* pos, Vec3s* rot, s32 thighLimbIndex,
                   s32 shinLimbIndex, s32 footLimbIndex) {
    Vec3f spA4;
    Vec3f sp98;
    Vec3f footprintPos;
    CollisionPoly* sp88;
    s32 sp84;
    f32 sp80;
    f32 sp7C;
    f32 sp78;
    f32 sp74;
    f32 sp70;
    f32 sp6C;
    f32 sp68;
    f32 sp64;
    f32 sp60;
    f32 sp5C;
    f32 sp58;
    f32 sp54;
    f32 sp50;
    s16 temp1;
    s16 temp2;
    s32 temp3;

    if ((this->actor.scale.y >= 0.0f) && !(this->stateFlags1 & PLAYER_STATE1_DEAD) &&
        (Player_ActionToMagicSpell(this, this->itemAction) < 0)) {
        s32 pad;

        Vec3f* shinPos = &D_80126038[gSaveContext.linkAge];
        f32 footX = D_80126050[gSaveContext.linkAge];

        sp7C = D_80126058[gSaveContext.linkAge];
        sp78 = D_80126060[gSaveContext.linkAge];
        sp74 = D_80126068[gSaveContext.linkAge] - this->unk_6C4;
#ifdef COMBO_BUILD
        {
            // ComboShip: MM Player_AdjustSingleLeg measures each form's own leg (D_801C08C0 shin, D_801C08FC foot,
            // D_801C0910 / D_801C0924 lengths, D_801C0938 sole). Measuring the Deku's short legs with child Link's
            // made the IK see his feet under the floor and lift them (owner 2026-11-15: "his feet float").
            // Indexed by ComboForm (human = OOT's own above).
            static Vec3f sFormShin[COMBO_FORM_MAX] = {
                { 0.0f, 0.0f, 0.0f },   { 1156.0f, 0.0f, 0.0f }, { 1406.0f, 0.0f, 0.0f },
                { 408.0f, 0.0f, 0.0f }, { 1304.0f, 0.0f, 0.0f },
            };
            static const f32 sFormFoot[COMBO_FORM_MAX] = { 0.0f, 1056.0f, 1506.0f, 359.0f, 1265.0f };
            static const f32 sFormLenA[COMBO_FORM_MAX] = { 0.0f, 133.63359f, 197.68358f, 16.646399f, 170.0416f };
            static const f32 sFormLenB[COMBO_FORM_MAX] = { 0.0f, 22.120003f, -29.12001f, 3.7582989f, 10.019104f };
            static const f32 sFormSole[COMBO_FORM_MAX] = { 0.0f, 4.0f, 1.0f, 1.0f, 5.0f };
            const int form = Combo_OotFormDrawActive(this) ? Combo_OotForm() : COMBO_FORM_HUMAN;

            if ((form > COMBO_FORM_HUMAN) && (form < COMBO_FORM_MAX)) {
                shinPos = &sFormShin[form];
                footX = sFormFoot[form];
                sp7C = sFormLenA[form];
                sp78 = sFormLenB[form];
                sp74 = sFormSole[form] - this->unk_6C4;
            }
        }
#endif

        Matrix_Push();
        Matrix_TranslateRotateZYX(pos, rot);
        Matrix_MultVec3f(&sZeroVec, &spA4);
        Matrix_TranslateRotateZYX(shinPos, &skelAnime->jointTable[shinLimbIndex]);
        Matrix_Translate(footX, 0.0f, 0.0f, MTXMODE_APPLY);
        Matrix_MultVec3f(&sZeroVec, &sp98);
        Matrix_MultVec3f(&D_80126070, &footprintPos);
        Matrix_Pop();

        footprintPos.y += 15.0f;

        sp80 = BgCheck_EntityRaycastFloor4(&play->colCtx, &sp88, &sp84, &this->actor, &footprintPos) + sp74;

        if (sp98.y < sp80) {
            sp70 = sp98.x - spA4.x;
            sp6C = sp98.y - spA4.y;
            sp68 = sp98.z - spA4.z;

            sp64 = sqrtf(SQ(sp70) + SQ(sp6C) + SQ(sp68));
            sp60 = (SQ(sp64) + sp78) / (2.0f * sp64);

            sp58 = sp7C - SQ(sp60);
            sp58 = (sp7C < SQ(sp60)) ? 0.0f : sqrtf(sp58);

            sp54 = Math_FAtan2F(sp58, sp60);

            sp6C = sp80 - spA4.y;

            sp64 = sqrtf(SQ(sp70) + SQ(sp6C) + SQ(sp68));
            sp60 = (SQ(sp64) + sp78) / (2.0f * sp64);
            sp5C = sp64 - sp60;

            sp58 = sp7C - SQ(sp60);
            sp58 = (sp7C < SQ(sp60)) ? 0.0f : sqrtf(sp58);

            sp50 = Math_FAtan2F(sp58, sp60);

            temp1 = (M_PI - (Math_FAtan2F(sp5C, sp58) + ((M_PI / 2) - sp50))) * (0x8000 / M_PI);
            temp1 = temp1 - skelAnime->jointTable[shinLimbIndex].z;

            if ((s16)(ABS(skelAnime->jointTable[shinLimbIndex].x) + ABS(skelAnime->jointTable[shinLimbIndex].y)) < 0) {
                temp1 += 0x8000;
            }

            temp2 = (sp50 - sp54) * (0x8000 / M_PI);
            rot->z -= temp2;

            skelAnime->jointTable[thighLimbIndex].z = skelAnime->jointTable[thighLimbIndex].z - temp2;
            skelAnime->jointTable[shinLimbIndex].z = skelAnime->jointTable[shinLimbIndex].z + temp1;
            skelAnime->jointTable[footLimbIndex].z = skelAnime->jointTable[footLimbIndex].z + temp2 - temp1;

            temp3 = SurfaceType_GetFloorType(&play->colCtx, sp88, sp84);

            if ((temp3 >= 2) && (temp3 < 4) && !SurfaceType_IsWallDamage(&play->colCtx, sp88, sp84)) {
                footprintPos.y = sp80;
                EffectSsGFire_Spawn(play, &footprintPos);
            }
        }
    }
}

s32 Player_OverrideLimbDrawGameplayCommon(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot,
                                          void* thisx) {
    Player* this = (Player*)thisx;

    if (CVarGetInteger(CVAR_ENHANCEMENT("EquipmentAlwaysVisible"), 0) &&
        CVarGetInteger(CVAR_ENHANCEMENT("ScaleAdultEquipmentAsChild"), 0) && LINK_IS_CHILD) {
        if (limbIndex == PLAYER_LIMB_L_HAND) {
            if ((gSaveContext.equips.buttonItems[0] != ITEM_SWORD_KOKIRI &&
                 sLeftHandType == PLAYER_MODELTYPE_LH_SWORD) ||
                (sLeftHandType == PLAYER_MODELTYPE_LH_BGS) || (sLeftHandType == PLAYER_MODELTYPE_LH_HAMMER)) {
                Matrix_Scale(0.8, 0.8, 0.8, MTXMODE_APPLY);
            }
        }
        if (limbIndex == PLAYER_LIMB_R_HAND) {
            if ((this->currentShield == PLAYER_SHIELD_MIRROR && sRightHandType == PLAYER_MODELTYPE_RH_SHIELD) ||
                sRightHandType == PLAYER_MODELTYPE_RH_HOOKSHOT ||
                (sRightHandType == PLAYER_MODELTYPE_RH_BOW_SLINGSHOT && Player_HoldsBow(this))) {
                Matrix_Scale(0.8, 0.8, 0.8, MTXMODE_APPLY);
            }
        }
        if (limbIndex == PLAYER_LIMB_SHEATH) {
            if ((this->currentShield == PLAYER_SHIELD_MIRROR ||
                 (this->currentShield == PLAYER_SHIELD_HYLIAN &&
                  (gSaveContext.equips.buttonItems[0] == ITEM_SWORD_MASTER ||
                   gSaveContext.equips.buttonItems[0] == ITEM_SWORD_BGS))) &&
                ((this->sheathType == PLAYER_MODELTYPE_SHEATH_16) || (this->sheathType == PLAYER_MODELTYPE_SHEATH_17) ||
                 (this->sheathType == PLAYER_MODELTYPE_SHEATH_18) ||
                 (this->sheathType == PLAYER_MODELTYPE_SHEATH_19))) {
                Matrix_Translate(218, -100, 62, MTXMODE_APPLY);
                Matrix_Scale(0.8, 0.8, 0.8, MTXMODE_APPLY);
            }
            if ((this->currentShield == PLAYER_SHIELD_DEKU && gSaveContext.equips.buttonItems[0] != ITEM_SWORD_KOKIRI &&
                 (this->sheathType == PLAYER_MODELTYPE_SHEATH_16 || this->sheathType == PLAYER_MODELTYPE_SHEATH_17))) {
                Matrix_Translate(218, -100, 62, MTXMODE_APPLY);
                Matrix_Scale(0.8, 0.8, 0.8, MTXMODE_APPLY);
            }
        }
    }

    if (limbIndex == PLAYER_LIMB_ROOT) {
        sLeftHandType = this->leftHandType;
        sRightHandType = this->rightHandType;
        D_80160000 = &this->meleeWeaponInfo[2].base;

#ifdef COMBO_BUILD
        // ComboShip: a Majora's Mask form scales the animation's root translation by its own body's factor, as MM does
        // (ageProperties->unk_08: Deku 0.3, Goron 0.74, Zora / Fierce Deity 1.0). OOT hardcodes child = 0.64 here.
        if (Combo_OotForm() != COMBO_FORM_HUMAN) {
            const f32 rootScale = this->ageProperties->unk_08;
            if (!(this->skelAnime.movementFlags & 4) || (this->skelAnime.movementFlags & 1)) {
                pos->x *= rootScale;
                pos->z *= rootScale;
            }
            if (!(this->skelAnime.movementFlags & 4) || (this->skelAnime.movementFlags & 2)) {
                pos->y *= rootScale;
            }
        } else
#endif
            if (!LINK_IS_ADULT) {
            if (!(this->skelAnime.movementFlags & 4) || (this->skelAnime.movementFlags & 1)) {
                pos->x *= 0.64f;
                pos->z *= 0.64f;
            }

            if (!(this->skelAnime.movementFlags & 4) || (this->skelAnime.movementFlags & 2)) {
                pos->y *= 0.64f;
            }
        }

        pos->y -= this->unk_6C4;

#ifdef COMBO_BUILD
        extern s32 Combo_ZoraSwimRootTransform(s16 * pitch, s16 * roll);
        s16 comboPitch, comboRoll;
        if (Combo_ZoraSwimRootTransform(&comboPitch, &comboRoll)) {
            // MM Player_OverrideLimbDrawGameplayCommon, Zora + PLAYER_STATE3_8000: the fast swim tilts the body by
            // its pitch about a point 200 above the root and barrel-rolls it
            Matrix_Translate(pos->x, ((Math_CosS(comboPitch) - 1.0f) * 200.0f) + pos->y, pos->z, MTXMODE_APPLY);
            Matrix_RotateX(comboPitch * (M_PI / 0x8000), MTXMODE_APPLY);
            Matrix_RotateZ(comboRoll * (M_PI / 0x8000), MTXMODE_APPLY);
            Matrix_RotateZYX(rot->x, rot->y, rot->z, MTXMODE_APPLY);
            pos->x = pos->y = pos->z = 0.0f;
            rot->x = rot->y = rot->z = 0;
        } else
#endif
            if (this->unk_6C2 != 0) {
            Matrix_Translate(pos->x, ((Math_CosS(this->unk_6C2) - 1.0f) * 200.0f) + pos->y, pos->z, MTXMODE_APPLY);
            Matrix_RotateX(this->unk_6C2 * (M_PI / 0x8000), MTXMODE_APPLY);
            Matrix_RotateZYX(rot->x, rot->y, rot->z, MTXMODE_APPLY);
            pos->x = pos->y = pos->z = 0.0f;
            rot->x = rot->y = rot->z = 0;
        }
    } else {
        if (*dList != NULL) {
            D_80160000++;
        }

        if (limbIndex == PLAYER_LIMB_HEAD) {
            if (CVarGetInteger(CVAR_COSMETIC("Link.HeadScale.Changed"), 0)) {
                f32 scale = CVarGetFloat(CVAR_COSMETIC("Link.HeadScale.Value"), 1.0f);
                if (scale != 1.0f) {
                    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
                    if (scale > 1.2f) {
                        Matrix_Translate(-((LINK_IS_ADULT ? 320.0f : 200.0f) * scale), 0.0f, 0.0f, MTXMODE_APPLY);
                    } else if (scale < 1.0f) {
                        Matrix_Translate((LINK_IS_ADULT ? 3600.0f : 2900.0f) * ABS(scale - 1.0f), 0.0f, 0.0f,
                                         MTXMODE_APPLY);
                    }
                }
            }
            rot->x += this->headLimbRot.z;
            rot->y -= this->headLimbRot.y;
            rot->z += this->headLimbRot.x;
#ifdef COMBO_BUILD
            {
                // ComboShip: Deku Link's head puffs and tilts with the bubble he's blowing (MM z_player_lib.c head
                // limb, PLAYER_FORM_DEKU): translate/rotate, save the matrix (restored after the head's post-limb, as
                // MM's func_80125340 / func_8012536C), squash by the bubble's size, roll by it.
                Vec3f squash;
                s16 rollZ;
                s16 upperX;
                if ((Combo_OotForm() == COMBO_FORM_DEKU) && (this->stateFlags1 & PLAYER_STATE1_READY_TO_FIRE) &&
                    EnArrow_ComboBubbleHeadPose(this->heldActor, &squash, &rollZ, &upperX)) {
                    Matrix_TranslateRotateZYX(pos, rot);
                    Matrix_Push();
                    sComboDekuHeadPushed = true;
                    pos->x = pos->y = pos->z = 0.0f;
                    rot->x = rot->y = 0;
                    Matrix_Scale(squash.x, squash.y, squash.z, MTXMODE_APPLY);
                    rot->z = rollZ;
                }
            }
#endif
        } else if (limbIndex == PLAYER_LIMB_L_HAND) {
            if (CVarGetInteger(CVAR_COSMETIC("Link.SwordScale.Changed"), 0)) {
                f32 scale = CVarGetFloat(CVAR_COSMETIC("Link.SwordScale.Value"), 1.0f);
                if (scale != 1.0f) {
                    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
                    Matrix_Translate(-((LINK_IS_ADULT ? 320.0f : 200.0f) * (scale - 1.0f)), 0.0f, 0.0f, MTXMODE_APPLY);
                }
            }
        } else if (limbIndex == PLAYER_LIMB_UPPER) {
            if (this->upperLimbYawSecondary != 0) {
                Matrix_RotateZ(0x44C * (M_PI / 0x8000), MTXMODE_APPLY);
                Matrix_RotateY(this->upperLimbYawSecondary * (M_PI / 0x8000), MTXMODE_APPLY);
            }
            if (this->upperLimbRot.y != 0) {
                Matrix_RotateY(this->upperLimbRot.y * (M_PI / 0x8000), MTXMODE_APPLY);
            }
            {
                s16 upperRotX = this->upperLimbRot.x;
#ifdef COMBO_BUILD
                // ComboShip: Deku Link leans back as the bubble grows (MM: rotX += bubble size * -470).
                Vec3f squash;
                s16 rollZ;
                s16 upperX;
                if ((Combo_OotForm() == COMBO_FORM_DEKU) && (this->stateFlags1 & PLAYER_STATE1_READY_TO_FIRE) &&
                    EnArrow_ComboBubbleHeadPose(this->heldActor, &squash, &rollZ, &upperX)) {
                    upperRotX += upperX;
                }
#endif
                if (upperRotX != 0) {
                    Matrix_RotateX(upperRotX * (M_PI / 0x8000), MTXMODE_APPLY);
                }
            }
            if (this->upperLimbRot.z != 0) {
                Matrix_RotateZ(this->upperLimbRot.z * (M_PI / 0x8000), MTXMODE_APPLY);
            }
        } else if (limbIndex == PLAYER_LIMB_L_THIGH) {
            func_8008F87C(play, this, &this->skelAnime, pos, rot, PLAYER_LIMB_L_THIGH, PLAYER_LIMB_L_SHIN,
                          PLAYER_LIMB_L_FOOT);
        } else if (limbIndex == PLAYER_LIMB_R_THIGH) {
            func_8008F87C(play, this, &this->skelAnime, pos, rot, PLAYER_LIMB_R_THIGH, PLAYER_LIMB_R_SHIN,
                          PLAYER_LIMB_R_FOOT);
            return false;
        } else {
            return false;
        }
    }

    return false;
}

s32 Player_OverrideLimbDrawGameplayDefault(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot,
                                           void* thisx) {
    Player* this = (Player*)thisx;

    if (!Player_OverrideLimbDrawGameplayCommon(play, limbIndex, dList, pos, rot, thisx)) {
        if (limbIndex == PLAYER_LIMB_L_HAND) {
            Gfx** dLists = this->leftHandDLists;

            if ((sLeftHandType == PLAYER_MODELTYPE_LH_BGS) && (gSaveContext.swordHealth <= 0.0f)) {
                dLists += 4;
            } else if ((sLeftHandType == PLAYER_MODELTYPE_LH_BOOMERANG) &&
                       (this->stateFlags1 & PLAYER_STATE1_BOOMERANG_THROWN)) {
                dLists = &gPlayerLeftHandOpenDLs[gSaveContext.linkAge];
                sLeftHandType = PLAYER_MODELTYPE_LH_OPEN;
            } else if ((this->leftHandType == PLAYER_MODELTYPE_LH_OPEN) && (this->actor.speedXZ > 2.0f) &&
                       !(this->stateFlags1 & PLAYER_STATE1_IN_WATER)) {
                dLists = &gPlayerLeftHandClosedDLs[gSaveContext.linkAge];
                sLeftHandType = PLAYER_MODELTYPE_LH_CLOSED;
            }

            *dList = ResourceMgr_LoadGfxByName(dLists[sDListsLodOffset]);
        } else if (limbIndex == PLAYER_LIMB_R_HAND) {
            Gfx** dLists = this->rightHandDLists;

            if (sRightHandType == PLAYER_MODELTYPE_RH_SHIELD) {
                dLists += this->currentShield * 4;
            } else if ((this->rightHandType == PLAYER_MODELTYPE_RH_OPEN) && (this->actor.speedXZ > 2.0f) &&
                       !(this->stateFlags1 & PLAYER_STATE1_IN_WATER)) {
                dLists = &sPlayerRightHandClosedDLs[gSaveContext.linkAge];
                sRightHandType = PLAYER_MODELTYPE_RH_CLOSED;
            }

            *dList = ResourceMgr_LoadGfxByName(dLists[sDListsLodOffset]);
        } else if (limbIndex == PLAYER_LIMB_SHEATH) {
            Gfx** dLists = this->sheathDLists;

            if ((this->sheathType == PLAYER_MODELTYPE_SHEATH_18) || (this->sheathType == PLAYER_MODELTYPE_SHEATH_19)) {
                dLists += this->currentShield * 4;
                if (!LINK_IS_ADULT && (this->currentShield < PLAYER_SHIELD_HYLIAN) &&
                    (gSaveContext.equips.buttonItems[0] != ITEM_SWORD_KOKIRI)) {
                    dLists += PLAYER_SHIELD_MAX * 4;
                }
            } else if (!CVarGetInteger(CVAR_ENHANCEMENT("EquipmentAlwaysVisible"), 0) ||
                       (CVarGetInteger(CVAR_ENHANCEMENT("EquipmentAlwaysVisible"), 0) &&
                        ((gSaveContext.equips.buttonItems[0] != ITEM_SWORD_MASTER &&
                          gSaveContext.equips.buttonItems[0] != ITEM_SWORD_BGS) &&
                         this->currentShield == PLAYER_SHIELD_DEKU))) {
                if (!LINK_IS_ADULT &&
                    ((this->sheathType == PLAYER_MODELTYPE_SHEATH_16) ||
                     (this->sheathType == PLAYER_MODELTYPE_SHEATH_17)) &&
                    (gSaveContext.equips.buttonItems[0] != ITEM_SWORD_KOKIRI)) {
                    dLists = &sSheathWithSwordDLs[PLAYER_SHIELD_MAX * 4];
                }
            }

            if (dLists[sDListsLodOffset] != NULL) {
                *dList = ResourceMgr_LoadGfxByName(dLists[sDListsLodOffset]);
            } else {
                *dList = NULL;
            }

        } else if (limbIndex == PLAYER_LIMB_WAIST) {

            if (!Player_IsCustomLinkModel()) {
                *dList = ResourceMgr_LoadGfxByName(
                    this->waistDLists[sDListsLodOffset]); // NOTE: This needs to be disabled when using custom
                                                          // characters - they're not going to have LODs anyways...
            }
        }
    }

    GameInteractor_Should(VB_PLAYER_OVERRIDE_LIMB_DRAW, true, limbIndex, dList, thisx, play);

    if (GameInteractor_InvisibleLinkActive()) {
        this->actor.shape.shadowDraw = NULL;
        *dList = NULL;
    }

    return false;
}

s32 Player_OverrideLimbDrawGameplayFirstPerson(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot,
                                               void* thisx) {
    Player* this = (Player*)thisx;

    if (!Player_OverrideLimbDrawGameplayCommon(play, limbIndex, dList, pos, rot, thisx)) {
        if (this->unk_6AD != 2) {
            *dList = NULL;
        } else if (limbIndex == PLAYER_LIMB_L_FOREARM) {
            *dList = sFirstPersonLeftForearmDLs[gSaveContext.linkAge];
        } else if (limbIndex == PLAYER_LIMB_L_HAND) {
            s32 handOutDlIndex = gSaveContext.linkAge;
            if ((CVarGetInteger(CVAR_ENHANCEMENT("BowSlingshotAmmoFix"), 0) ||
                 CVarGetInteger(CVAR_ENHANCEMENT("EquipmentAlwaysVisible"), 0)) &&
                LINK_IS_ADULT && Player_HoldsSlingshot(this)) {
                handOutDlIndex = 1;
            }
            *dList = sFirstPersonLeftHandDLs[handOutDlIndex];
        } else if (limbIndex == PLAYER_LIMB_R_SHOULDER) {
            *dList = sFirstPersonRightShoulderDLs[gSaveContext.linkAge];
        } else if (limbIndex == PLAYER_LIMB_R_FOREARM) {
            *dList = sFirstPersonForearmDLs[gSaveContext.linkAge];
        } else if (limbIndex == PLAYER_LIMB_R_HAND) {
            s32 firstPersonWeaponIndex = gSaveContext.linkAge;
            if (CVarGetInteger(CVAR_ENHANCEMENT("BowSlingshotAmmoFix"), 0) ||
                CVarGetInteger(CVAR_ENHANCEMENT("EquipmentAlwaysVisible"), 0)) {
                if (Player_HoldsBow(this)) {
                    firstPersonWeaponIndex = 0;
                } else if (Player_HoldsSlingshot(this)) {
                    firstPersonWeaponIndex = 1;
                }
            }
            *dList = Player_HoldsHookshot(this) ? gLinkAdultRightHandHoldingHookshotFarDL
                                                : sFirstPersonRightHandHoldingWeaponDLs[firstPersonWeaponIndex];
        } else {
            *dList = NULL;
        }
    }

    GameInteractor_Should(VB_PLAYER_OVERRIDE_LIMB_DRAW, true, limbIndex, dList, thisx, play);

    return false;
}

s32 Player_OverrideLimbDrawGameplayCrawling(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot,
                                            void* thisx) {
    if (!Player_OverrideLimbDrawGameplayCommon(play, limbIndex, dList, pos, rot, thisx)) {
        *dList = NULL;
    }

    return false;
}

u8 func_80090480(PlayState* play, ColliderQuad* collider, WeaponInfo* weaponInfo, Vec3f* newTip, Vec3f* newBase) {
    if (weaponInfo->active == 0) {
        if (collider != NULL) {
            Collider_ResetQuadAT(play, &collider->base);
        }
        Math_Vec3f_Copy(&weaponInfo->tip, newTip);
        Math_Vec3f_Copy(&weaponInfo->base, newBase);
        weaponInfo->active = 1;
        return 1;
    } else if ((weaponInfo->tip.x == newTip->x) && (weaponInfo->tip.y == newTip->y) &&
               (weaponInfo->tip.z == newTip->z) && (weaponInfo->base.x == newBase->x) &&
               (weaponInfo->base.y == newBase->y) && (weaponInfo->base.z == newBase->z)) {
        if (collider != NULL) {
            Collider_ResetQuadAT(play, &collider->base);
        }
        return 0;
    } else {
        if (collider != NULL) {
            Collider_SetQuadVertices(collider, newBase, newTip, &weaponInfo->base, &weaponInfo->tip);
            CollisionCheck_SetAT(play, &play->colChkCtx, &collider->base);
        }
        Math_Vec3f_Copy(&weaponInfo->base, newBase);
        Math_Vec3f_Copy(&weaponInfo->tip, newTip);
        weaponInfo->active = 1;
        return 1;
    }
}

void Player_UpdateShieldCollider(PlayState* play, Player* this, ColliderQuad* collider, Vec3f* quadSrc) {
    static u8 shieldColTypes[PLAYER_SHIELD_MAX] = {
        COLTYPE_METAL,
        COLTYPE_WOOD,
        COLTYPE_METAL,
        COLTYPE_METAL,
    };

    if (this->stateFlags1 & PLAYER_STATE1_SHIELDING) {
        Vec3f quadDest[4];

        this->shieldQuad.base.colType = shieldColTypes[this->currentShield];

        Matrix_MultVec3f(&quadSrc[0], &quadDest[0]);
        Matrix_MultVec3f(&quadSrc[1], &quadDest[1]);
        Matrix_MultVec3f(&quadSrc[2], &quadDest[2]);
        Matrix_MultVec3f(&quadSrc[3], &quadDest[3]);
        Collider_SetQuadVertices(collider, &quadDest[0], &quadDest[1], &quadDest[2], &quadDest[3]);

        CollisionCheck_SetAC(play, &play->colChkCtx, &collider->base);
        CollisionCheck_SetAT(play, &play->colChkCtx, &collider->base);
    }
}

Vec3f D_80126080 = { 5000.0f, 400.0f, 0.0f };
Vec3f D_8012608C = { 5000.0f, -400.0f, 1000.0f };
Vec3f D_80126098 = { 5000.0f, 1400.0f, -1000.0f };

Vec3f D_801260A4[3] = {
    { 0.0f, 400.0f, 0.0f },
    { 0.0f, 1400.0f, -1000.0f },
    { 0.0f, -400.0f, 1000.0f },
};

void func_800906D4(PlayState* play, Player* this, Vec3f* newTipPos) {
    Vec3f newBasePos[3];

    Matrix_MultVec3f(&D_801260A4[0], &newBasePos[0]);
    Matrix_MultVec3f(&D_801260A4[1], &newBasePos[1]);
    Matrix_MultVec3f(&D_801260A4[2], &newBasePos[2]);

    if (func_80090480(play, NULL, &this->meleeWeaponInfo[0], &newTipPos[0], &newBasePos[0]) &&
        !(this->stateFlags1 & PLAYER_STATE1_SHIELDING) &&
        !CVarGetInteger(CVAR_ENHANCEMENT("DisableLinkSwordTrail"), 0)) {
        EffectBlure_AddVertex(Effect_GetByIndex(this->meleeWeaponEffectIndex), &this->meleeWeaponInfo[0].tip,
                              &this->meleeWeaponInfo[0].base);
    }

    if ((this->meleeWeaponState > 0) &&
        ((this->meleeWeaponAnimation < 0x18) || (this->stateFlags2 & PLAYER_STATE2_SPIN_ATTACKING))) {
        func_80090480(play, &this->meleeWeaponQuads[0], &this->meleeWeaponInfo[1], &newTipPos[1], &newBasePos[1]);
        func_80090480(play, &this->meleeWeaponQuads[1], &this->meleeWeaponInfo[2], &newTipPos[2], &newBasePos[2]);
    }
}

void Player_DrawGetItemIceTrap(PlayState* play, Player* this, Vec3f* refPos, s32 drawIdPlusOne, f32 height) {
    OPEN_DISPS(play->state.gfxCtx);

    if (CVarGetInteger(CVAR_GENERAL("LetItSnow"), 0)) {
        Gfx_SetupDL_25Opa(play->state.gfxCtx);

        Matrix_Scale(0.2f, 0.2f, 0.2f, MTXMODE_APPLY);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);

        gDPSetGrayscaleColor(POLY_OPA_DISP++, 75, 75, 75, 255);
        gSPGrayscale(POLY_OPA_DISP++, true);

        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gSilverRockDL);

        gSPGrayscale(POLY_OPA_DISP++, false);
    } else {
        if (iceTrapScale < 0.01) {
            iceTrapScale += 0.001f;
        } else if (iceTrapScale < 0.8f) {
            iceTrapScale += 0.2f;
        }

        // Draw the ice only after a bit so it doesn't spoil the fact that it's a trap
        if (iceTrapScale >= 0.01) {
            gSPSegment(POLY_XLU_DISP++, 0x08,
                       Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, 0, (0 - play->gameplayFrames) % 128, 32, 32, 1, 0,
                                          (play->gameplayFrames * -2) % 128, 32, 32, 0, -1, 0, -2));

            Matrix_Translate(0.0f, -40.0f, 0.0f, MTXMODE_APPLY);
            Matrix_Scale(iceTrapScale, iceTrapScale, iceTrapScale, MTXMODE_APPLY);
            gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gDPSetEnvColor(POLY_XLU_DISP++, 0, 50, 100, 255);
            gSPDisplayList(POLY_XLU_DISP++, gEffIceFragment3DL);

            // Reset matrix for the fake item model because we're animating the size of the ice block around it before
            // this.
            Matrix_Translate(refPos->x + (3.3f * Math_SinS(this->actor.shape.rot.y)), refPos->y + height,
                             refPos->z + ((3.3f + (IREG(90) / 10.0f)) * Math_CosS(this->actor.shape.rot.y)),
                             MTXMODE_NEW);
            Matrix_RotateZYX(0, play->gameplayFrames * 1000, 0, MTXMODE_APPLY);
            Matrix_Scale(0.2f, 0.2f, 0.2f, MTXMODE_APPLY);
        }

        // Draw fake item model.
        if (this->getItemEntry.drawFunc != NULL) {
            this->getItemEntry.drawFunc(play, &this->getItemEntry);
        } else {
            GetItem_Draw(play, drawIdPlusOne - 1);
        }
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

void Player_DrawGetItemImpl(PlayState* play, Player* this, Vec3f* refPos, s32 drawIdPlusOne) {
    f32 height = (this->exchangeItemId != EXCH_ITEM_NONE) ? 6.0f : 14.0f;

    OPEN_DISPS(play->state.gfxCtx);

    gSegments[6] = VIRTUAL_TO_PHYSICAL(this->giObjectSegment);

    gSPSegment(POLY_OPA_DISP++, 0x06, this->giObjectSegment);
    gSPSegment(POLY_XLU_DISP++, 0x06, this->giObjectSegment);

    Matrix_Translate(refPos->x + (3.3f * Math_SinS(this->actor.shape.rot.y)), refPos->y + height,
                     refPos->z + ((3.3f + (IREG(90) / 10.0f)) * Math_CosS(this->actor.shape.rot.y)), MTXMODE_NEW);
    Matrix_RotateZYX(0, play->gameplayFrames * 1000, 0, MTXMODE_APPLY);
    Matrix_Scale(0.2f, 0.2f, 0.2f, MTXMODE_APPLY);

    if (this->getItemEntry.modIndex == MOD_RANDOMIZER && this->getItemEntry.getItemId == RG_ICE_TRAP) {
        Player_DrawGetItemIceTrap(play, this, refPos, drawIdPlusOne, height);
    } else if (this->getItemEntry.modIndex == MOD_RANDOMIZER &&
               (this->getItemEntry.getItemId == RG_TRIFORCE_PIECE || this->getItemEntry.getItemId == RG_TRIFORCE)) {
        Randomizer_DrawTriforcePieceGI(play, this->getItemEntry);
    } else if (this->getItemEntry.drawFunc != NULL) {
        this->getItemEntry.drawFunc(play, &this->getItemEntry);
    } else {
        GetItem_Draw(play, drawIdPlusOne - 1);
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

void Player_DrawGetItem(PlayState* play, Player* this) {
    // if (!this->giObjectLoading || !osRecvMesg(&this->giObjectLoadQueue, NULL, OS_MESG_NOBLOCK)) // OTRTODO: Do
    // something about osRecvMesg here...
    {
        this->giObjectLoading = false;
        Player_DrawGetItemImpl(play, this, &sGetItemRefPos, ABS(this->unk_862));
    }
}

void func_80090A28(Player* this, Vec3f* vecs) {
    D_8012608C.x = D_80126080.x;

    if (this->unk_845 >= 3) {
        this->unk_845 += 1;
        D_8012608C.x *= 1.0f + ((9 - this->unk_845) * 0.1f);
    }

    D_8012608C.x += 1200.0f;
    D_80126098.x = D_8012608C.x;

    Matrix_MultVec3f(&D_80126080, &vecs[0]);
    Matrix_MultVec3f(&D_8012608C, &vecs[1]);
    Matrix_MultVec3f(&D_80126098, &vecs[2]);
}

void Player_DrawHookshotReticle(PlayState* play, Player* this, f32 hookshotRange) {
    static Vec3f D_801260C8 = { -500.0f, -100.0f, 0.0f };
    CollisionPoly* colPoly;
    s32 bgId;
    Vec3f hookshotStart;
    Vec3f hookshotEnd;
    Vec3f firstHit;
    Vec3f sp68;
    f32 sp64;

    D_801260C8.z = 0.0f;
    Matrix_MultVec3f(&D_801260C8, &hookshotStart);
    D_801260C8.z = hookshotRange;
    Matrix_MultVec3f(&D_801260C8, &hookshotEnd);

    if (BgCheck_AnyLineTest3(&play->colCtx, &hookshotStart, &hookshotEnd, &firstHit, &colPoly, 1, 1, 1, 1, &bgId)) {
        OPEN_DISPS(play->state.gfxCtx);

        OVERLAY_DISP = Gfx_SetupDL(OVERLAY_DISP, 0x07);

        SkinMatrix_Vec3fMtxFMultXYZW(&play->viewProjectionMtxF, &firstHit, &sp68, &sp64);

        const f32 sp60 = (sp64 < 200.0f) ? 0.08f : (sp64 / 200.0f) * 0.08f;

        Matrix_Translate(firstHit.x, firstHit.y, firstHit.z, MTXMODE_NEW);
        Matrix_Scale(sp60, sp60, sp60, MTXMODE_APPLY);

        gSPMatrix(OVERLAY_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        if (GameInteractor_Should(VB_TARGETABLE_HOOKSHOT_RETICLE, true, colPoly, bgId)) {
            gSPSegment(OVERLAY_DISP++, 0x06, play->objectCtx.status[this->actor.objBankIndex].segment);
            gSPDisplayList(OVERLAY_DISP++, gLinkAdultHookshotReticleDL);
        }

        CLOSE_DISPS(play->state.gfxCtx);
    }
}

// Coordinates of the player focus position, in the head limb's own model space.
Vec3f sPlayerFocusOffsetFromHead = { 1100.0f, -700.0f, 0.0f };

f32 sMeleeWeaponLengths[] = {
    0.0f, 4000.0f, 3000.0f, 5500.0f, 0.0f, 2500.0f,
};

f32 sSwordTypes[] = {
    TRAIL_TYPE_REST,           TRAIL_TYPE_MASTER_SWORD, TRAIL_TYPE_KOKIRI_SWORD,
    TRAIL_TYPE_BIGGORON_SWORD, TRAIL_TYPE_REST,         TRAIL_TYPE_HAMMER,
};

Gfx* sBottleDLists[] = { gLinkAdultBottleDL, gLinkChildBottleDL };

Color_RGB8 sBottleColors[] = {
    { 255, 255, 255 }, { 80, 80, 255 },   { 255, 100, 255 }, { 0, 0, 255 }, { 255, 0, 255 },
    { 255, 0, 255 },   { 200, 200, 100 }, { 255, 0, 0 },     { 0, 0, 255 }, { 0, 255, 0 },
    { 255, 255, 255 }, { 255, 255, 255 }, { 80, 80, 255 },
};

Vec3f sLeftHandArrowVec3 = { 398.0f, 1419.0f, 244.0f };

BowStringData sBowStringData[] = {
    { gLinkAdultBowStringDL, { 0.0f, -360.4f, 0.0f } },        // bow
    { gLinkChildSlingshotStringDL, { 606.0f, 236.0f, 0.0f } }, // slingshot
};

Vec3f sRightHandLimbModelShieldQuadVertices[] = {
    { -4500.0f, -3000.0f, -600.0f },
    { 1500.0f, -3000.0f, -600.0f },
    { -4500.0f, 3000.0f, -600.0f },
    { 1500.0f, 3000.0f, -600.0f },
};

Vec3f D_80126184 = { 100.0f, 1500.0f, 0.0f };
Vec3f D_80126190 = { 100.0f, 1640.0f, 0.0f };

Vec3f sSheathLimbModelShieldQuadVertices[] = {
    { -3000.0f, -3000.0f, -900.0f },
    { 3000.0f, -3000.0f, -900.0f },
    { -3000.0f, 3000.0f, -900.0f },
    { 3000.0f, 3000.0f, -900.0f },
};

Vec3f sSheathLimbModelShieldOnBackPos = { 630.0f, 100.0f, -30.0f };
Vec3s sSheathLimbModelShieldOnBackZyxRot = { 0, 0, 0x7FFF };

Vec3f sLeftRightFootLimbModelFootPos[] = {
    { 200.0f, 300.0f, 0.0f },
    { 200.0f, 200.0f, 0.0f },
};

#ifdef COMBO_BUILD
extern s32 gComboGoronPunchLimb; // z_player.c: the Goron fist (or butt) whose matrix carries the hit quads

// ComboShip: the Zora's forearm fins, MM func_80126BD0 (the default pose; the swim/instrument poses come with those
// moves). MM draws them as their own models on the forearms: tucked (0.4, 0.6, 0.7) normally, full size while held
// ready to throw, gone while that fin is in flight. The Zora's left jab / right combo jab hit from these fins (MM
// D_801C0A00 over D_801C09DC, under the fin's scale), not from the fists.
s32 gComboZoraFinBlure[2] = { TOTAL_EFFECT_COUNT, TOTAL_EFFECT_COUNT }; // z_player.c Player_Init / Player_Destroy
s16 Combo_ZoraRollVel(void);                                            // z_player.c: MM unk_B8A

// MM func_80124618: a keyframed scale (frame, percent xyz) at curFrame.
typedef struct {
    s16 frame;
    Vec3s pct;
} ComboScaleKey;
static void Combo_KeyframeScale(ComboScaleKey* keys, f32 curFrame, Vec3f* out) {
    s32 currentFrame = curFrame;
    f32 start;
    f32 progress;
    s16 next;

    do {
        next = keys[1].frame;
        keys++;
    } while (next < currentFrame);

    start = keys[-1].frame;
    progress = (curFrame - start) / (next - start);
    out->x = (keys[-1].pct.x + (keys->pct.x - keys[-1].pct.x) * progress) * 0.01f;
    out->y = (keys[-1].pct.y + (keys->pct.y - keys[-1].pct.y) * progress) * 0.01f;
    out->z = (keys[-1].pct.z + (keys->pct.z - keys[-1].pct.z) * progress) * 0.01f;
}

static s32 Combo_AnimIs(Player* this, const char* mmAnim) {
    const char* anim = (const char*)this->skelAnime.animation;
    return (anim != NULL) && (strcmp(anim, mmAnim) == 0);
}

// MM func_80126BD0's second half: the Zora's swim fins (their own models, sized by the fast swim's poses) and the
// blur trail each one draws while in water (MM func_80126440 with no collider -> EffectBlure_AddVertex).
static void Combo_ZoraSwimFin(PlayState* play, Player* this, s32 side) {
    static const char* const sSwimFinDL[2] = { "objects/object_link_zora/object_link_zora_DL_010868",
                                               "objects/object_link_zora/object_link_zora_DL_010978" };
    static Vec3f sTip[2] = { { 5400.0f, 1700.0f, 1800.0f }, { 5400.0f, 1700.0f, -1800.0f } }; // MM D_801C0AC4
    static Vec3f sBase[2] = { { 5250.0f, 570.0f, 2400.0f }, { 5250.0f, 570.0f, -2400.0f } };  // MM D_801C0ADC
    static ComboScaleKey sRollIn[] = { { 0, { 0, 0, 0 } }, { 17, { 50, 50, 50 } } };          // MM D_801C05C8
    static ComboScaleKey sToWait[] = { { 0, { 100, 100, 100 } },
                                       { 5, { 100, 100, 100 } },
                                       { 9, { 0, 0, 0 } } }; // D_801C05F0
    static WeaponInfo sInfo[2];
    Vec3f scale;
    Vec3f tip;
    Vec3f base;
    s32 add;

    if (Combo_AnimIs(this, "__OTR__@mm:objects/gameplay_keep/gPlayerAnim_pz_waterroll")) {
        Combo_KeyframeScale(sRollIn, this->skelAnime.curFrame, &scale);
        sToWait[0].pct.x = sToWait[0].pct.y = sToWait[0].pct.z = 50; // MM writes its own table
    } else if (Combo_AnimIs(this, "__OTR__@mm:objects/gameplay_keep/gPlayerAnim_pz_swimtowait")) {
        Combo_KeyframeScale(sToWait, this->skelAnime.curFrame, &scale);
    } else if (Combo_AnimIs(this, "__OTR__@mm:objects/gameplay_keep/gPlayerAnim_pz_fishswim")) {
        scale.x = (ABS(Combo_ZoraRollVel()) * 0.00003f) + 0.5f;
        scale.y = scale.z = scale.x;
        sToWait[0].pct.x = sToWait[0].pct.y = sToWait[0].pct.z = (s16)(scale.x * 100.0f);
    } else {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx);
    Matrix_Push();
    Matrix_Scale(scale.x, scale.y, scale.z, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)Combo_MmDL(sSwimFinDL[side]));
    Matrix_MultVec3f(&sTip[side], &tip);
    Matrix_MultVec3f(&sBase[side], &base);
    // MM func_80126440(play, NULL, ...): the first frame (or any move) of the fin's edge counts
    add = !sInfo[side].active || (sInfo[side].tip.x != tip.x) || (sInfo[side].tip.y != tip.y) ||
          (sInfo[side].tip.z != tip.z) || (sInfo[side].base.x != base.x) || (sInfo[side].base.y != base.y) ||
          (sInfo[side].base.z != base.z);
    sInfo[side].tip = tip;
    sInfo[side].base = base;
    sInfo[side].active = true;
    if (add && (this->stateFlags1 & PLAYER_STATE1_IN_WATER) && (gComboZoraFinBlure[side] != TOTAL_EFFECT_COUNT)) {
        EffectBlure_AddVertex(Effect_GetByIndex(gComboZoraFinBlure[side]), &tip, &base);
    }
    Matrix_Pop();
    CLOSE_DISPS(play->state.gfxCtx);
}

// MM Player_PostLimbDrawGameplay, PLAYER_LIMB_TORSO: the Goron's drums and the Deku's pipes while it plays (the
// instrument grows in over the start animation by MM's keyframes, then holds). Owner 2026-11-14: "get the instruments
// visible ... for all transformations in OOT".
static void Combo_FormInstrumentDraw(PlayState* play, Player* this, s32 limbIndex) {
    static ComboScaleKey sGoronStart[] = { { 0, { 0, 0, 0 } },       { 6, { 0, 0, 0 } },      { 7, { 60, 60, 50 } },
                                           { 8, { 120, 130, 100 } }, { 9, { 100, 120, 80 } }, { 11, { 100, 100, 100 } },
                                           { 13, { 100, 100, 100 } } }; // D_801C0428
    static ComboScaleKey sDekuBase[] = { { 0, { 0, 0, 0 } },
                                         { 5, { 0, 0, 0 } },
                                         { 7, { 100, 100, 100 } },
                                         { 9, { 110, 110, 110 } },
                                         { 11, { 100, 100, 100 } } }; // D_801C0340
    static ComboScaleKey sDekuPipes[] = { { 0, { 0, 0, 0 } },       { 4, { 0, 0, 0 } },       { 6, { 120, 150, 60 } },
                                          { 8, { 130, 80, 160 } },  { 9, { 100, 100, 100 } }, { 10, { 90, 100, 90 } },
                                          { 11, { 100, 100, 100 } } }; // D_801C0368
    static const char* const sDrums[] = {
        "objects/object_link_goron/object_link_goron_DL_010590",
        "objects/object_link_goron/object_link_goron_DL_010368",
        "objects/object_link_goron/object_link_goron_DL_010140",
        "objects/object_link_goron/object_link_goron_DL_00FF18",
        "objects/object_link_goron/object_link_goron_DL_00FCF0",
    };
    static const char* const sPipes[] = {
        "objects/object_link_nuts/object_link_nuts_DL_007A28", "objects/object_link_nuts/object_link_nuts_DL_0077D0",
        "objects/object_link_nuts/object_link_nuts_DL_007548", "objects/object_link_nuts/object_link_nuts_DL_007900",
        "objects/object_link_nuts/object_link_nuts_DL_0076A0",
    };
    static Vec3f sDrumScale = { 1.0f, 1.0f, 1.0f }; // MM unk_AF0[1]
    static f32 sPipeBase = 1.0f;                    // MM arr_AF0[0]
    static Vec3f sPipeScale = { 1.0f, 1.0f, 1.0f }; // MM arr_AF0[1..5] (one value each in MM, equal here)
    s32 i;

    // MM draws the drums in torso space and the pipes in HEAD space (Player_PostLimbDrawGameplay, PLAYER_LIMB_HEAD).
    // Note by note (z_player.c Combo_FormInstrumentUpdate, MM func_8085255C): each drum squashes on its own timer
    // (D_801C0510) while the Goron sits on pg_gakkiwait, all pulse together while a song plays back; each pipe pops
    // on its own note.
    extern f32 gComboDrumTimer[5];
    extern f32 gComboPipeScale[5];
    extern Vec3f gComboDrumAll;
    static ComboScaleKey sDrumHit[] = { { 0, { 100, 100, 100 } },
                                        { 4, { 100, 100, 100 } },
                                        { 5, { 90, 110, 100 } },
                                        { 6, { 110, 105, 100 } },
                                        { 8, { 100, 100, 100 } } }; // D_801C0510
    if ((Combo_OotForm() == COMBO_FORM_GORON) && (limbIndex == PLAYER_LIMB_TORSO)) {
        const s32 start = Combo_AnimIs(this, "__OTR__@mm:objects/gameplay_keep/gPlayerAnim_pg_gakkistart");
        const s32 wait = Combo_AnimIs(this, "__OTR__@mm:objects/gameplay_keep/gPlayerAnim_pg_gakkiwait");
        const s32 play_ = Combo_AnimIs(this, "__OTR__@mm:objects/gameplay_keep/gPlayerAnim_pg_gakkiplay");
        Vec3f drum[5];
        if (!start && !wait && !play_) {
            return;
        }
        if (wait) {
            for (i = 0; i < 5; i++) {
                Combo_KeyframeScale(sDrumHit, gComboDrumTimer[i], &drum[i]);
            }
        } else {
            if (start) {
                Combo_KeyframeScale(sGoronStart, this->skelAnime.curFrame, &sDrumScale);
            } else {
                sDrumScale = gComboDrumAll; // a song played back: all together (MM unk_AF0[1] <- D_801C0490)
            }
            for (i = 0; i < 5; i++) {
                drum[i] = sDrumScale;
            }
        }
        OPEN_DISPS(play->state.gfxCtx);
        Matrix_Push();
        Matrix_Scale(sDrumScale.x, sDrumScale.y, sDrumScale.z, MTXMODE_APPLY);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)Combo_MmDL("objects/object_link_goron/object_link_goron_DL_00FC18"));
        Matrix_Pop();
        for (i = 0; i < ARRAY_COUNT(sDrums); i++) {
            Matrix_Push();
            Matrix_Scale(drum[i].x, drum[i].y, drum[i].z, MTXMODE_APPLY);
            gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_OPA_DISP++, (Gfx*)Combo_MmDL(sDrums[i]));
            Matrix_Pop();
        }
        CLOSE_DISPS(play->state.gfxCtx);
    } else if ((Combo_OotForm() == COMBO_FORM_DEKU) && (limbIndex == PLAYER_LIMB_HEAD)) {
        const s32 start = Combo_AnimIs(this, "__OTR__@mm:objects/gameplay_keep/gPlayerAnim_pn_gakkistart");
        if (!start && !Combo_AnimIs(this, "__OTR__@mm:objects/gameplay_keep/gPlayerAnim_pn_gakkiplay")) {
            return;
        }
        if (start) {
            Vec3f base;

            Combo_KeyframeScale(sDekuBase, this->skelAnime.curFrame, &base);
            sPipeBase = base.x;
            Combo_KeyframeScale(sDekuPipes, this->skelAnime.curFrame, &sPipeScale);
            for (i = 0; i < 5; i++) {
                gComboPipeScale[i] = sPipeScale.x; // MM: arr_AF0[1..5] all take the start key's x
            }
        }
        OPEN_DISPS(play->state.gfxCtx);
        Matrix_Push();
        Matrix_Scale(sPipeBase, sPipeBase, sPipeBase, MTXMODE_APPLY);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)Combo_MmDL("objects/object_link_nuts/object_link_nuts_DL_007390"));
        Matrix_Pop();
        for (i = 0; i < ARRAY_COUNT(sPipes); i++) {
            const f32 k = gComboPipeScale[i];
            Matrix_Push();
            Matrix_Scale(k, k, k, MTXMODE_APPLY);
            gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_OPA_DISP++, (Gfx*)Combo_MmDL(sPipes[i]));
            Matrix_Pop();
        }
        CLOSE_DISPS(play->state.gfxCtx);
    }
}

// MM func_8012669C for the forms: entry [0] is the swing's trail line (MM's white melee blur, which the Goron doesn't
// draw) and the line the wall tests follow; [1] and [2] are the hit quads, armed only once the swing is live
// (meleeWeaponState >= 1, not during the windup). Matrix = the limb (or the scaled fin) the move hits from.
static void Combo_FormMeleeLimb(PlayState* play, Player* this, const Vec3f* tips, const Vec3f* bases) {
    Vec3f tip;
    Vec3f base;
    s32 q;

    Matrix_MultVec3f(&tips[0], &tip);
    Matrix_MultVec3f(&bases[0], &base);
    if (func_80090480(play, NULL, &this->meleeWeaponInfo[0], &tip, &base) && (Combo_OotForm() != COMBO_FORM_GORON) &&
        !CVarGetInteger(CVAR_ENHANCEMENT("DisableLinkSwordTrail"), 0)) {
        EffectBlureShip_ChangeType(Effect_GetByIndex(this->meleeWeaponEffectIndex), TRAIL_TYPE_SWORDS);
        EffectBlure_AddVertex(Effect_GetByIndex(this->meleeWeaponEffectIndex), &this->meleeWeaponInfo[0].tip,
                              &this->meleeWeaponInfo[0].base);
    }
    if (this->meleeWeaponState >= 1) {
        for (q = 1; q < 3; q++) {
            Matrix_MultVec3f(&tips[q], &tip);
            Matrix_MultVec3f(&bases[q], &base);
            func_80090480(play, &this->meleeWeaponQuads[q - 1], &this->meleeWeaponInfo[q], &tip, &base);
        }
    }
}

static void Combo_ZoraArmFin(PlayState* play, Player* this, s32 side) {
    static const char* const sFinDL[2] = { "objects/object_link_zora/object_link_zora_DL_00CC38",
                                           "objects/object_link_zora/object_link_zora_DL_00CDA0" };
    static const Vec3f sFinTip[3] = { { -2500.0f, 1400.0f, 1100.0f },
                                      { -2900.0f, 1000.0f, 1500.0f },
                                      { -2100.0f, 1800.0f, 700.0f } }; // MM D_801C0A00
    static const Vec3f sFinBase[3] = { { 900.0f, 300.0f, 100.0f },
                                       { 1300.0f, 700.0f, -300.0f },
                                       { 500.0f, -100.0f, 500.0f } }; // MM D_801C09DC
    Vec3f scale;

    if ((Combo_OotForm() != COMBO_FORM_ZORA) || (this->rightHandType == PLAYER_MODELTYPE_RH_HOOKSHOT)) {
        return;
    }
    if (this->stateFlags1 & PLAYER_STATE1_BOOMERANG_THROWN) {
        Actor* fin = this->boomerangActor;
        if (fin == NULL) {
            return;
        }
        if (((fin->params - COMBO_ZORA_FIN_LEFT) == side) ||
            ((fin->child != NULL) && ((fin->child->params - COMBO_ZORA_FIN_LEFT) == side))) {
            return; // this arm's fin is out
        }
    }
    if (Combo_AnimIs(this, "__OTR__@mm:objects/gameplay_keep/gPlayerAnim_pz_fishswim") ||
        Combo_AnimIs(this, "__OTR__@mm:objects/gameplay_keep/gPlayerAnim_pz_gakkiplay")) {
        Combo_ZoraSwimFin(play, this, side); // MM: the resting fins are not drawn in the fast swim or on the guitar
        return;
    }
    // MM func_80126BD0: the arm fins grow, shrink and pop by the animation's own keyframes.
    if (Combo_AnimIs(this, "__OTR__@mm:objects/gameplay_keep/gPlayerAnim_pz_gakkistart")) {
        static ComboScaleKey sKeys[] = { { 0, { 100, 100, 100 } },
                                         { 9, { 100, 100, 100 } },
                                         { 10, { 150, 150, 150 } },
                                         { 12, { 0, 0, 0 } },
                                         { 14, { 0, 0, 0 } } }; // D_801C0580
        Combo_KeyframeScale(sKeys, this->skelAnime.curFrame, &scale);
    } else if (Combo_AnimIs(this, "__OTR__@mm:objects/gameplay_keep/gPlayerAnim_pz_waterroll")) {
        static ComboScaleKey sKeys[] = {
            { 0, { 100, 100, 100 } }, { 6, { 100, 100, 100 } }, { 7, { 0, 0, 0 } }, { 17, { 0, 0, 0 } }
        }; // D_801C05A8
        Combo_KeyframeScale(sKeys, this->skelAnime.curFrame, &scale);
    } else if (Combo_AnimIs(this, "__OTR__@mm:objects/gameplay_keep/gPlayerAnim_pz_swimtowait")) {
        static ComboScaleKey sKeys[] = { { 0, { 0, 0, 0 } },
                                         { 5, { 0, 0, 0 } },
                                         { 9, { 100, 100, 100 } } }; // D_801C05D8
        Combo_KeyframeScale(sKeys, this->skelAnime.curFrame, &scale);
    } else if (Combo_AnimIs(this, "__OTR__@mm:objects/gameplay_keep/gPlayerAnim_pz_bladeon")) {
        static ComboScaleKey sKeys[] = { { 0, { 40, 60, 70 } },    { 3, { 40, 60, 70 } },    { 4, { 75, 90, 85 } },
                                         { 5, { 110, 120, 100 } }, { 7, { 100, 100, 100 } }, { 8, { 100, 100, 100 } } };
        Combo_KeyframeScale(sKeys, this->skelAnime.curFrame, &scale); // D_801C07F0
    } else if (Combo_AnimIs(this, "__OTR__@mm:objects/gameplay_keep/gPlayerAnim_pz_jumpAT")) {
        static ComboScaleKey sKeys[] = { { 0, { 100, 100, 100 } }, { 7, { 100, 100, 100 } }, { 12, { 40, 60, 70 } } };
        Combo_KeyframeScale(sKeys, this->skelAnime.curFrame, &scale); // D_801C0820
    } else if (Combo_AnimIs(this, "__OTR__@mm:objects/gameplay_keep/gPlayerAnim_pz_jumpATend")) {
        static ComboScaleKey sKeys[] = { { 0, { 40, 60, 70 } }, { 7, { 40, 60, 70 } }, { 12, { 100, 100, 100 } } };
        Combo_KeyframeScale(sKeys, this->skelAnime.curFrame, &scale); // D_801C0838
    } else if (this->heldItemAction == PLAYER_IA_BOOMERANG) {
        scale.x = scale.y = scale.z = 1.0f;
    } else {
        scale.x = 0.4f;
        scale.y = 0.6f;
        scale.z = 0.7f;
    }

    OPEN_DISPS(play->state.gfxCtx);
    Matrix_Push();
    Matrix_Scale(scale.x, scale.y, scale.z, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)Combo_MmDL(sFinDL[side]));
    if ((this->meleeWeaponState != 0) &&
        (gComboGoronPunchLimb == ((side == 0) ? PLAYER_LIMB_L_FOREARM : PLAYER_LIMB_R_FOREARM))) {
        Combo_FormMeleeLimb(play, this, sFinTip, sFinBase);
    }
    Matrix_Pop();
    CLOSE_DISPS(play->state.gfxCtx);
    Combo_ZoraSwimFin(play, this, side);
}
#endif
#ifdef COMBO_BUILD
// ComboShip (merged pause menus): MM's Bunny Hood worn in Hyrule. Its display list reads the two ears' matrices from
// segment 0x0B, which MM fills every frame from a little spring (mm z_player_lib.c Player_UpdateBunnyEars /
// Player_DrawBunnyHood); without them the ears are drawn with whatever 0x0B holds (a spike to the sky). Ported 1:1;
// the spring steps once per game frame.
#define COMBO_MM_ITEM_MASK_BUNNY 0x39
#define COMBO_MM_ITEM_MASK_BLAST 0x47
static void Combo_DrawMmBunnyEars(PlayState* play, Player* player) {
    static Vec3s sRot;
    static Vec3s sAngVel;
    static u32 sLastFrame = 0xFFFFFFFF;
    Mtx* mtx = Graph_Alloc(play->state.gfxCtx, 2 * sizeof(Mtx));
    Vec3s earRot;

    if (sLastFrame != play->gameplayFrames) {
        Vec3s force;
        s16 angle = player->actor.world.rot.y - player->actor.shape.rot.y;

        sLastFrame = play->gameplayFrames;
        sAngVel.x -= sAngVel.x >> 3;
        sAngVel.y -= sAngVel.y >> 3;
        sAngVel.x += -sRot.x >> 2;
        sAngVel.y += -sRot.y >> 2;
        force.x =
            (s32)(player->actor.speedXZ * -200.0f * Math_CosS(angle) * (Rand_CenteredFloat(2.0f) + 10.0f)) & 0xFFFF;
        force.y =
            (s32)(player->actor.speedXZ * 100.0f * Math_SinS(angle) * (Rand_CenteredFloat(2.0f) + 10.0f)) & 0xFFFF;
        sAngVel.x += force.x >> 2;
        sAngVel.y += force.y >> 2;
        sAngVel.x = CLAMP(sAngVel.x, -0x1770, 0x1770);
        sAngVel.y = CLAMP(sAngVel.y, -0x1770, 0x1770);
        sRot.x += sAngVel.x;
        sRot.y += sAngVel.y;
        sRot.z = (sRot.x < 0) ? (sRot.x >> 1) : 0;
    }

    OPEN_DISPS(play->state.gfxCtx);
    gSPSegment(POLY_OPA_DISP++, 0x0B, mtx);
    Matrix_Push();
    earRot.x = sRot.y + 0x3E2;
    earRot.y = sRot.z + 0xDBE;
    earRot.z = sRot.x - 0x348A;
    Matrix_SetTranslateRotateYXZ(97.0f, -1203.0f, -240.0f, &earRot);
    MATRIX_TOMTX(mtx++);
    earRot.x = sRot.y - 0x3E2;
    earRot.y = -sRot.z - 0xDBE;
    earRot.z = sRot.x - 0x348A;
    Matrix_SetTranslateRotateYXZ(97.0f, -1203.0f, 240.0f, &earRot);
    MATRIX_TOMTX(mtx);
    Matrix_Pop();
    CLOSE_DISPS(play->state.gfxCtx);
}

// ComboShip (merged pause menus): MM's Great Fairy's Mask worn in Hyrule. Its three hair strands read six matrices
// from segment 0x0B, which MM computes every frame from a small hair simulation (mm z_player_lib.c
// Player_DrawGreatFairysMask / func_80127DA4 / func_80128388 / func_80127B64). Ported 1:1, including MM's reaction to
// a stray fairy nearby (ACTORCTX_FLAG_3 -> gComboStrayFairyNear, set by MM stray fairies at randomizer checks in Hyrule
// and by containers holding one: z_en_item00_combo_strayfairy.c / ComboStrayFairy.cpp): the strands float instead of
// hanging and shed sparkles (Player_DrawStrayFairyParticles). MM's Math_Atan2S_XY is OOT's Math_Atan2S.
#define COMBO_MM_ITEM_MASK_GREAT_FAIRY 0x40
typedef struct {
    Vec3f pos;
    Vec3f vel;
    s16 yaw;
    s16 pitch;
} ComboHairNode;
typedef struct {
    f32 length;
    s16 restYaw;
    s16 restPitch;
    Vec3f unused;
    f32 unused2;
    s16 maxYaw;
    s16 maxPitch;
} ComboHairLink;
static ComboHairNode sComboHair[3][3];
static ComboHairLink sComboHairLinks[3] = {
    { 0.0f, 0x0000, 0x8000, { 0.0f, 0.0f, 0.0f }, 0.0f, 0x0000, 0x0000 },
    { 16.8f, 0x0000, 0x0000, { 0.0f, 0.0f, 0.0f }, 20.0f, 0x1388, 0x1388 },
    { 30.0f, 0x0000, 0x0000, { 0.0f, 0.0f, 0.0f }, 20.0f, 0x1F40, 0x2EE0 },
};

extern s32 gComboStrayFairyNear; // z_en_item00_combo_strayfairy.c: MM's ACTORCTX_FLAG_3

// MM Player_DrawStrayFairyParticles (D_801C0BE0 / D_801C0BEC / D_801C0BF8 / D_801C0BFC)
static void Combo_MmDrawStrayFairyParticles(PlayState* play, Vec3f* arg1) {
    static Vec3f sVel = { 0.0f, 0.3f, 0.0f };
    static Vec3f sAccel = { 0.0f, -0.025f, 0.0f };
    static Color_RGBA8 sPrim = { 250, 100, 100, 0 };
    static Color_RGBA8 sEnv = { 0, 0, 100, 0 };
    Vec3f pos;
    f32 sign;

    sVel.y = Rand_ZeroFloat(0.07f) + -0.1f;
    sAccel.y = Rand_ZeroFloat(0.1f) + 0.04f;
    if (Rand_ZeroOne() < 0.5f) {
        sign = -1.0f;
    } else {
        sign = 1.0f;
    }
    sVel.x = (Rand_ZeroFloat(0.2f) + 0.1f) * sign;
    sAccel.x = 0.1f * sign;
    if (Rand_ZeroOne() < 0.5f) {
        sign = -1.0f;
    } else {
        sign = 1.0f;
    }
    sVel.z = (Rand_ZeroFloat(0.2f) + 0.1f) * sign;
    sAccel.z = 0.1f * sign;
    pos.x = arg1->x;
    pos.y = Rand_ZeroFloat(15.0f) + arg1->y;
    pos.z = arg1->z;
    EffectSsKiraKira_SpawnDispersed(play, &pos, &sVel, &sAccel, &sPrim, &sEnv, -50, 11);
}

static void Combo_MmHairStep(PlayState* play, ComboHairNode* node, ComboHairLink* link, s32 count, Vec3f* root,
                             Vec3f* dir, u32* frameBits) {
    ComboHairNode* next = &node[1];
    Vec3f diff;
    Vec3f offset;
    f32 f22, f28, f24, f20, f0;
    f32 gravity = -1.0f;
    s16 s0, s2;
    s32 i;

    Math_Vec3f_Copy(&node->pos, root);
    Math_Vec3f_Diff(dir, root, &diff);
    node->yaw = Math_Atan2S(diff.z, diff.x);
    node->pitch = Math_Atan2S(sqrtf(SQ(diff.x) + SQ(diff.z)), diff.y);
    link++;
    for (i = 1; i < count; i++) {
        if (gComboStrayFairyNear) { // MM: play->actorCtx.flags & ACTORCTX_FLAG_3
            if (*frameBits & 0x20) {
                gravity = -0.2f;
            } else {
                gravity = 0.2f;
            }
            *frameBits += 0x16;
            if (!(*frameBits & 1)) {
                Combo_MmDrawStrayFairyParticles(play, &next->pos);
            }
        }
        Math_Vec3f_Sum(&next->pos, &next->vel, &next->pos);
        f0 = Math_Vec3f_DistXYZAndStoreDiff(&node->pos, &next->pos, &diff);
        f28 = f0 - link->length;
        if (f0 == 0.0f) {
            diff.x = 0.0f;
            diff.y = link->length;
            diff.z = 0.0f;
        }
        f20 = sqrtf(SQ(diff.x) + SQ(diff.z));
        if (f20 > 4.0f) {
            next->yaw = Math_Atan2S(diff.z, diff.x);
            s2 = next->yaw - node->yaw;
            if (ABS(s2) > 0x4000) {
                next->yaw = (s16)(next->yaw + 0x8000);
                f20 = -f20;
            }
        }
        next->pitch = Math_Atan2S(f20, diff.y);
        s2 = next->yaw - node->yaw;
        s2 = CLAMP(s2, -link->maxYaw, link->maxYaw);
        next->yaw = node->yaw + s2;
        s0 = next->pitch - node->pitch;
        s0 = CLAMP(s0, -link->maxPitch, link->maxPitch);
        next->pitch = node->pitch + s0;

        f20 = Math_CosS(next->pitch) * link->length;
        offset.x = Math_SinS(next->yaw) * f20;
        offset.z = Math_CosS(next->yaw) * f20;
        offset.y = Math_SinS(next->pitch) * link->length;
        Math_Vec3f_Sum(&node->pos, &offset, &next->pos);
        next->vel.x *= 0.9f;
        next->vel.z *= 0.9f;
        f22 = Math_CosS(s0) * f28;
        f24 = Math_SinS(s0) * f28;
        next->vel.y += gravity;
        if (gComboStrayFairyNear) {
            next->vel.y = CLAMP(next->vel.y, -0.8f, 0.8f);
        } else {
            f20 = Math_SinS(node->pitch);
            next->vel.y += (((f22 * Math_CosS(node->pitch)) + (f24 * f20)) * 0.2f);
            next->vel.y = CLAMP(next->vel.y, -2.0f, 4.0f);
        }
        f20 = (f24 * Math_CosS(node->pitch)) - (Math_SinS(node->pitch) * f22);
        f22 = Math_CosS(s2) * f20;
        f24 = Math_SinS(s2) * f20;
        f20 = Math_SinS(node->yaw);
        next->vel.x += (((f24 * Math_CosS(node->yaw)) - (f22 * f20)) * 0.1f);
        next->vel.x = CLAMP(next->vel.x, -4.0f, 4.0f);
        f20 = Math_SinS(node->yaw);
        next->vel.z += (((f22 * Math_CosS(node->yaw)) + (f24 * f20)) * -0.1f);
        next->vel.z = CLAMP(next->vel.z, -4.0f, 4.0f);
        node++;
        next++;
        link++;
    }
}

static void Combo_DrawMmGreatFairyHair(PlayState* play, Player* player) {
    static const Vec3f kTip = { 0.0f, 20.0f, 0.0f };
    static const Vec3f kRoots[3] = { { 174.0f, -1269.0f, -1.0f },
                                     { 401.0f, -729.0f, -701.0f },
                                     { 401.0f, -729.0f, 699.0f } };
    static const Vec3f kDirs[3] = { { 74.0f, -1269.0f, -1.0f },
                                    { 301.0f, -729.0f, -701.0f },
                                    { 301.0f, -729.0f, 699.0f } };
    static s32 sWorn = false;
    Mtx* mtx = Graph_Alloc(play->state.gfxCtx, 6 * sizeof(Mtx));
    Vec3f root, dir;
    u32 frameBits = play->gameplayFrames; // MM sp6C
    s32 i, j;

    if (!sWorn) { // MM func_80127B64, when the mask's object loads: every node at the head, at rest
        for (i = 0; i < 3; i++) {
            for (j = 0; j < 3; j++) {
                Math_Vec3f_Copy(&sComboHair[i][j].pos, &player->bodyPartsPos[PLAYER_BODYPART_HEAD]);
                sComboHair[i][j].vel.x = sComboHair[i][j].vel.y = sComboHair[i][j].vel.z = 0.0f;
                sComboHair[i][j].yaw = sComboHair[i][j].pitch = 0;
            }
        }
        sWorn = true;
    }

    OPEN_DISPS(play->state.gfxCtx);
    gSPSegment(POLY_OPA_DISP++, 0x0B, mtx);
    Matrix_MultVec3f((Vec3f*)&kTip, &sComboHairLinks[1].unused);
    { // Math_Vec3f_Lerp(head, waist, 0.2f) (MM writes it into the table; nothing reads it back)
        Vec3f* h = &player->bodyPartsPos[PLAYER_BODYPART_HEAD];
        Vec3f* w = &player->bodyPartsPos[PLAYER_BODYPART_WAIST];
        sComboHairLinks[2].unused.x = h->x + (w->x - h->x) * 0.2f;
        sComboHairLinks[2].unused.y = h->y + (w->y - h->y) * 0.2f;
        sComboHairLinks[2].unused.z = h->z + (w->z - h->z) * 0.2f;
    }
    for (i = 0; i < 3; i++) {
        Matrix_MultVec3f((Vec3f*)&kRoots[i], &root);
        Matrix_MultVec3f((Vec3f*)&kDirs[i], &dir);
        Combo_MmHairStep(play, sComboHair[i], sComboHairLinks, 3, &root, &dir, &frameBits);
        frameBits += 11;
        Matrix_Push();
        Matrix_Translate(kRoots[i].x, kRoots[i].y, kRoots[i].z, MTXMODE_APPLY);
        for (j = 1; j < 3; j++) { // MM func_80128388: node j against link j - 1
            Vec3f t = { sComboHairLinks[j - 1].length * 100.0f, 0.0f, 0.0f };
            Vec3s r;
            r.x = 0;
            r.z = sComboHairLinks[j - 1].restPitch + (s16)(sComboHair[i][j].pitch - sComboHair[i][j - 1].pitch);
            r.y = sComboHairLinks[j - 1].restYaw + (s16)(sComboHair[i][j].yaw - sComboHair[i][j - 1].yaw);
            Matrix_TranslateRotateZYX(&t, &r);
            MATRIX_TOMTX(mtx++);
        }
        Matrix_Pop();
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// ComboShip (merged pause menus): MM's Circus Leader's Mask worn in Hyrule cries: two tears (MM gEffBubbleDL with
// gEffBubble1Tex) grow under the eyes and, when full, drop off as bubbles. Ported 1:1 from mm z_player_lib.c:
// the tear growth from func_801229FC (D_801F59C8 += Rand_S16Offset(4, 23) + speedXZ * 50; MM's player->speedXZ is
// OOT's linearVelocity), stepped once per game frame here, and Player_DrawCircusLeadersMask (D_801C0B90 positions in
// head-limb space, D_801F59B0 / D_801F59C8 state). OOT's Effect_Ss_Dt_Bubble lacks MM's colour profile 3, so the
// bubble is spawned with that profile's colours (prim 255,255,255,255 / env 150,150,150,0) as a custom colour.
static void Combo_DrawMmCircusLeaderTears(PlayState* play, Player* player) {
    static Vec3f sTearOffsets[2] = {
        // MM D_801C0B90
        { 950.0f, -800.0f, 300.0f },
        { 950.0f, -800.0f, -300.0f },
    };
    static Vec3f sTearPos[2];  // MM D_801F59B0
    static s32 sTearGrowth[2]; // MM D_801F59C8
    static u32 sLastFrame = 0xFFFFFFFF;
    static Vec3f sBubbleVelocity = { 0.0f, 0.0f, 0.0f };
    static Vec3f sBubbleAccel = { 0.0f, 0.0f, 0.0f };
    static Color_RGBA8 sBubblePrim = { 255, 255, 255, 255 }; // MM Effect_Ss_Dt_Bubble sPrimColors[3]
    static Color_RGBA8 sBubbleEnv = { 150, 150, 150, 0 };    // MM Effect_Ss_Dt_Bubble sEnvColors[3]
    const void* tearDL = Combo_MmDL("objects/gameplay_keep/gEffBubbleDL");
    s32 i;

    if (sLastFrame != play->gameplayFrames) { // MM func_801229FC (Player_Update), once per game frame
        sLastFrame = play->gameplayFrames;
        for (i = 0; i < 2; i++) {
            sTearGrowth[i] += Rand_S16Offset(4, 23) + (s32)(fabsf(player->linearVelocity) * 50.0f);
        }
    }

    OPEN_DISPS(play->state.gfxCtx);
    for (i = 0; i < 2; i++) {
        f32 scaleY = (sTearGrowth[i] / 400.0f) * 0.1f;

        Matrix_MultVec3f(&sTearOffsets[i], &sTearPos[i]);
        sTearPos[i].y += -10.0f * scaleY;

        if (sTearGrowth[i] < 400) {
            f32 scaleXZ = CLAMP_MAX(scaleY, 0.05f);

            Matrix_Push();
            Matrix_Translate(sTearPos[i].x, sTearPos[i].y, sTearPos[i].z, MTXMODE_NEW);
            Matrix_Scale(scaleXZ, scaleY, scaleXZ, MTXMODE_APPLY);
            gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_PUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            // Plain path: resolved by MM's ResourceManager while the routed MM gEffBubbleDL below loads it.
            gSPSegment(POLY_XLU_DISP++, 0x08, (uintptr_t) "__OTR__objects/gameplay_keep/gEffBubble1Tex");
            gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, 255, 255, 255, 255);
            gDPSetEnvColor(POLY_XLU_DISP++, 150, 150, 150, 0);
            if (tearDL != NULL) {
                gSPDisplayList(POLY_XLU_DISP++, tearDL);
            }
            gSPPopMatrix(POLY_XLU_DISP++, G_MTX_MODELVIEW);
            Matrix_Pop();
        } else {
            f32 speedXZ = sqrtf(SQ(player->actor.velocity.x) + SQ(player->actor.velocity.z));
            s16 yaw = speedXZ * 2000.0f;
            f32 spread;

            sBubbleVelocity.y = speedXZ * 0.4f;
            sBubbleAccel.y = -0.3f;
            if (yaw > 0x3E80) {
                yaw = 0x3E80;
            }
            yaw = player->actor.focus.rot.y + ((i != 0) ? yaw : -yaw);
            spread = speedXZ * 0.2f;
            spread = CLAMP_MAX(spread, 4.0f);
            sBubbleVelocity.x = -Math_SinS(yaw) * spread;
            sBubbleVelocity.z = -Math_CosS(yaw) * spread;
            EffectSsDtBubble_SpawnCustomColor(play, &sTearPos[i], &sBubbleVelocity, &sBubbleAccel, &sBubblePrim,
                                              &sBubbleEnv, 20, 20, 0);
            sTearGrowth[i] -= 400;
        }
    }
    CLOSE_DISPS(play->state.gfxCtx);
}
#endif

void Player_PostLimbDrawGameplay(PlayState* play, s32 limbIndex, Gfx** dList, Vec3s* rot, void* thisx) {
#ifdef COMBO_BUILD
    {
        // MM's transformation models: the mask in hand, the mask on the face, the head glow (z_player.c)
        extern void Combo_TransformDrawLimb(PlayState * play, Player * this, s32 limbIndex);
        if ((thisx != NULL) && (((Actor*)thisx)->id == ACTOR_PLAYER)) {
            Combo_TransformDrawLimb(play, (Player*)thisx, limbIndex);
        }
    }
    if ((Combo_OotForm() == COMBO_FORM_GORON) && (thisx != NULL) && (((Actor*)thisx)->id == ACTOR_PLAYER) &&
        ((limbIndex == PLAYER_LIMB_L_HAND) || (limbIndex == PLAYER_LIMB_R_HAND))) {
        // MM Player_PostLimbDrawGameplay -> func_80127488: the red flash on the punching fist by the punch's frame
        // (D_801C0778 for pg_punchA's left fist, D_801C07AC for pg_punchB's right), leaving the fist afterimage.
        static const u8 sPunchA[] = { 0, 0, 0, 0, 0, 100, 200, 255, 255, 255, 200, 100 };
        static const u8 sPunchB[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 200, 255, 255, 255, 200, 100, 0 };
        const char* anim = (const char*)((Player*)thisx)->skelAnime.animation;
        const s32 frame = (s32)((Player*)thisx)->skelAnime.curFrame;
        const s32 left = limbIndex == PLAYER_LIMB_L_HAND;
        if ((anim != NULL) && (strcmp(anim, left ? "__OTR__@mm:objects/gameplay_keep/gPlayerAnim_pg_punchA"
                                                 : "__OTR__@mm:objects/gameplay_keep/gPlayerAnim_pg_punchB") == 0)) {
            const s32 count = left ? ARRAY_COUNT(sPunchA) : ARRAY_COUNT(sPunchB);
            Combo_GoronPunchFist(play, (frame >= 0) && (frame < count) ? (left ? sPunchA : sPunchB)[frame] : 0);
        }
    }
    if ((gComboGoronPunchLimb != 0) && (limbIndex == gComboGoronPunchLimb) && (thisx != NULL) &&
        (((Actor*)thisx)->id == ACTOR_PLAYER) && (limbIndex != PLAYER_LIMB_L_FOREARM) &&
        (limbIndex != PLAYER_LIMB_R_FOREARM)) {
        // ComboShip: a form's punch/kick hits from the limb itself, with MM's own hit shapes (mm z_player_lib.c:
        // Goron fists D_801C09B8 over D_801C0970, Goron butt D_801C0A90 over D_801C0A6C, Zora kick D_801C0A48 over
        // D_801C0A24, Zora fists = a bare-hand sword length of 0 + 1200 over D_801C0970). Quad 0 and 1 are MM's
        // entries [1] and [2].
        // MM's [0] entries are the trail / wall-test line (Combo_FormMeleeLimb).
        static const Vec3f sFistBase[3] = { { 0.0f, 400.0f, 0.0f },
                                            { 0.0f, 1400.0f, -1000.0f },
                                            { 0.0f, -400.0f, 1000.0f } }; // MM D_801C0970
        static const Vec3f sGoronFistTip[3] = { { 0.0f, 750.0f, 750.0f },
                                                { 1500.0f, 1500.0f, 1500.0f },
                                                { -2500.0f, -2000.0f, -3000.0f } }; // MM D_801C09B8
        static const Vec3f sGoronButtTip[3] = { { -400.0f, 1800.0f, 0.0f },
                                                { 5000.0f, 8000.0f, 4000.0f },
                                                { 5000.0f, -500.0f, -4000.0f } }; // MM D_801C0A90
        static const Vec3f sGoronButtBase[3] = { { -400.0f, 800.0f, 0.0f },
                                                 { -5000.0f, -500.0f, -4000.0f },
                                                 { -5000.0f, 8000.0f, 4000.0f } }; // MM D_801C0A6C
        static const Vec3f sZoraKickTip[3] = { { 2000.0f, 0.0f, 0.0f },
                                               { 2800.0f, -800.0f, -800.0f },
                                               { 2800.0f, 800.0f, 800.0f } }; // MM D_801C0A48
        static const Vec3f sZoraKickBase[3] = { { 0.0f, 0.0f, 0.0f },
                                                { -800.0f, 800.0f, 800.0f },
                                                { -800.0f, -800.0f, -800.0f } }; // MM D_801C0A24
        Player* punchingPlayer = (Player*)thisx;
        if (limbIndex == PLAYER_LIMB_WAIST) {
            Combo_FormMeleeLimb(play, punchingPlayer, sGoronButtTip, sGoronButtBase);
        } else if (limbIndex == PLAYER_LIMB_R_SHIN) {
            Combo_FormMeleeLimb(play, punchingPlayer, sZoraKickTip, sZoraKickBase);
        } else {
            Combo_FormMeleeLimb(play, punchingPlayer, sGoronFistTip, sFistBase);
        }
        if (Combo_DebugEnv("COMBO_DEBUG_FORM_B")) {
            const Vec3f* p = &punchingPlayer->actor.world.pos;
            Combo_DebugLog("[ComboShip] hit limb %d trail tip (%.0f %.0f %.0f) rel link, state %d AT %d\n", limbIndex,
                           punchingPlayer->meleeWeaponInfo[0].tip.x - p->x,
                           punchingPlayer->meleeWeaponInfo[0].tip.y - p->y,
                           punchingPlayer->meleeWeaponInfo[0].tip.z - p->z, punchingPlayer->meleeWeaponState,
                           (punchingPlayer->meleeWeaponQuads[0].base.atFlags & AT_ON) != 0);
        }
    }
#endif
    Player* this = (Player*)thisx;

    if (*dList != NULL) {
        Matrix_MultVec3f(&sZeroVec, D_80160000);
    }

    if (limbIndex == PLAYER_LIMB_L_HAND) {
#ifdef COMBO_BUILD
        {
            // MM func_80128640: the limb drew the Zora's guitar (ComboForms.cpp); the hand holding it is its own model
            // (owner 2026-11-14: "zora guitar makes the hand holding the guitar dissapear").
            extern int Combo_ZoraGuitarOut(void* player);
            if ((this->actor.id == ACTOR_PLAYER) && Combo_ZoraGuitarOut(this)) {
                OPEN_DISPS(play->state.gfxCtx);
                gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx),
                          G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
                gSPDisplayList(POLY_OPA_DISP++,
                               (Gfx*)Combo_MmDL("objects/object_link_zora/object_link_zora_DL_00E088"));
                CLOSE_DISPS(play->state.gfxCtx);
            }
        }
#endif
        MtxF sp14C;
        Actor* hookedActor;

        Math_Vec3f_Copy(&this->leftHandPos, D_80160000);
#ifdef COMBO_BUILD
        if (Combo_GfsInHand(this) && !Combo_BremenMarching() && (this->actor.scale.y >= 0.0f) && (*dList != NULL)) {
            // MM's Great Fairy's Sword blade in the closed fist, as MM draws its split hand + sword
            // (CosmeticEditor sHeldItemSplits: gLinkHumanGreatFairysSwordDL in the hand's own space). OOT's child hand
            // shares MM's human hand space; the adult hand is larger (grip offset/scale from screenshots).
            OPEN_DISPS(play->state.gfxCtx);
            Matrix_Push();
            if (LINK_IS_ADULT) {
                Matrix_Translate(sComboGfsAdultGrip.x, sComboGfsAdultGrip.y, sComboGfsAdultGrip.z, MTXMODE_APPLY);
                Matrix_Scale(kComboGfsAdultScale, kComboGfsAdultScale, kComboGfsAdultScale, MTXMODE_APPLY);
            }
            gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_OPA_DISP++, (Gfx*)Combo_MmDL("objects/object_link_child/gLinkHumanGreatFairysSwordDL"));
            // MM's blade DL sets its own environment colour, which the limbs drawn after the hand (torso, sheath)
            // then inherited: the tunic came out black (owner 2026-11-14). Put the tunic colour back, as after the
            // Blast Mask.
            gDPPipeSync(POLY_OPA_DISP++);
            gDPSetEnvColor(POLY_OPA_DISP++, sComboTunicEnv.r, sComboTunicEnv.g, sComboTunicEnv.b, 0);
            Matrix_Pop();
            CLOSE_DISPS(play->state.gfxCtx);
        }
#endif

        if (this->itemAction == PLAYER_IA_DEKU_STICK) {
            Vec3f sp124[3];

            OPEN_DISPS(play->state.gfxCtx);

            if (this->actor.scale.y >= 0.0f) {
                D_80126080.x = this->unk_85C * 5000.0f;
                func_80090A28(this, sp124);
                if (this->meleeWeaponState != 0) {
                    EffectBlureShip_ChangeType(Effect_GetByIndex(this->meleeWeaponEffectIndex), TRAIL_TYPE_STICK);
                    func_800906D4(play, this, sp124);
                } else {
                    Math_Vec3f_Copy(&this->meleeWeaponInfo[0].tip, &sp124[0]);
                }
            }

            Matrix_Translate(-428.26f, 267.2f, -33.82f, MTXMODE_APPLY);
            Matrix_RotateZYX(-0x8000, 0, 0x4000, MTXMODE_APPLY);
            Matrix_Scale(1.0f, this->unk_85C, 1.0f, MTXMODE_APPLY);

            gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_OPA_DISP++, gLinkChildLinkDekuStickDL);

            CLOSE_DISPS(play->state.gfxCtx);
        } else if ((this->actor.scale.y >= 0.0f) &&
                   (this->meleeWeaponState != 0)
#ifdef COMBO_BUILD
                   // ComboShip: Goron/Zora/Deku attacks carry no sword; this left-hand sword path would overwrite
                   // the form's own hit quads (and draw a sword trail). Fierce Deity keeps OOT's sword.
                   && ((Combo_OotForm() == COMBO_FORM_HUMAN) || (Combo_OotForm() == COMBO_FORM_FIERCE_DEITY))
#endif
        ) {
            Vec3f spE4[3];

            if (Player_HoldsBrokenKnife(this)) {
                D_80126080.x = 1500.0f;
            } else {
                D_80126080.x = sMeleeWeaponLengths[Player_GetMeleeWeaponHeld(this)];
                EffectBlureShip_ChangeType(Effect_GetByIndex(this->meleeWeaponEffectIndex),
                                           sSwordTypes[Player_GetMeleeWeaponHeld(this)]);
            }

            func_80090A28(this, spE4);
            func_800906D4(play, this, spE4);
        } else if ((*dList != NULL) && (this->leftHandType == PLAYER_MODELTYPE_LH_BOTTLE)) {
            Color_RGB8* bottleColor = &sBottleColors[Player_ActionToBottle(this, this->itemAction)];
#ifdef COMBO_BUILD
            // ComboShip: an MM bottle shows MM's content colour (MM sPlayerBottleColors), e.g. a Zora egg or gold dust.
            Color_RGB8 comboMmColor;
            if ((this->heldItemButton >= 0) &&
                Combo_PauseMmBottleColor(gSaveContext.equips.buttonItems[this->heldItemButton], &comboMmColor.r)) {
                bottleColor = &comboMmColor;
            }
#endif

            OPEN_DISPS(play->state.gfxCtx);

            gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            if (GameInteractor_Should(VB_PLAYER_DRAW_BOTTLE, true, this, play)) {
                gDPSetEnvColor(POLY_XLU_DISP++, bottleColor->r, bottleColor->g, bottleColor->b, 0);
                gSPDisplayList(POLY_XLU_DISP++, sBottleDLists[gSaveContext.linkAge]);
            }

            CLOSE_DISPS(play->state.gfxCtx);
        }

        if (this->actor.scale.y >= 0.0f) {
            if (!Player_HoldsHookshot(this) && ((hookedActor = this->heldActor) != NULL)) {
                if ((this->stateFlags1 & PLAYER_STATE1_READY_TO_FIRE)
#ifdef COMBO_BUILD
                    // ComboShip: Deku Link's bubble sits at his mouth (head limb below), not in his hand.
                    && (Combo_OotForm() != COMBO_FORM_DEKU)
#endif
                ) {
                    Matrix_MultVec3f(&sLeftHandArrowVec3, &hookedActor->world.pos);
                    Matrix_RotateZYX(0x69E8, -0x5708, 0x458E, MTXMODE_APPLY);
                    Matrix_Get(&sp14C);
                    Matrix_MtxFToYXZRotS(&sp14C, &hookedActor->world.rot, 0);
                    hookedActor->shape.rot = hookedActor->world.rot;
                } else if (this->stateFlags1 & PLAYER_STATE1_CARRYING_ACTOR) {
                    Vec3s spB8;

                    Matrix_Get(&sp14C);
                    Matrix_MtxFToYXZRotS(&sp14C, &spB8, 0);

                    if (hookedActor->flags & ACTOR_FLAG_CARRY_X_ROT_INFLUENCE) {
                        hookedActor->world.rot.x = hookedActor->shape.rot.x = spB8.x - this->unk_3BC.x;
                    } else {
                        hookedActor->world.rot.y = hookedActor->shape.rot.y = this->actor.shape.rot.y + this->unk_3BC.y;
                    }
                }
            } else {
                Matrix_Get(&this->mf_9E0);
                Matrix_MtxFToYXZRotS(&this->mf_9E0, &this->unk_3BC, 0);
            }
        }
    } else if (limbIndex == PLAYER_LIMB_R_HAND) {
        Actor* heldActor = this->heldActor;

        if (this->rightHandType == PLAYER_MODELTYPE_RH_FF) {
            Matrix_Get(&this->shieldMf);
        } else if (((this->rightHandType == PLAYER_MODELTYPE_RH_BOW_SLINGSHOT) ||
                    (this->rightHandType == PLAYER_MODELTYPE_RH_BOW_SLINGSHOT_2))
#ifdef COMBO_BUILD
                   // ComboShip: a form holds no bow or slingshot (the Deku blows bubbles), so no string.
                   && ((Combo_OotForm() == COMBO_FORM_HUMAN) || (Combo_OotForm() == COMBO_FORM_FIERCE_DEITY))
#endif
        ) {
            s32 stringModelToUse = gSaveContext.linkAge;
            if (CVarGetInteger(CVAR_ENHANCEMENT("BowSlingshotAmmoFix"), 0) ||
                CVarGetInteger(CVAR_ENHANCEMENT("EquipmentAlwaysVisible"), 0)) {
                stringModelToUse = Player_HoldsSlingshot(this);
            }
            BowStringData* stringData = &sBowStringData[stringModelToUse];

            OPEN_DISPS(play->state.gfxCtx);

            Matrix_Push();
            Matrix_Translate(stringData->pos.x, stringData->pos.y, stringData->pos.z, MTXMODE_APPLY);

            if ((this->stateFlags1 & PLAYER_STATE1_READY_TO_FIRE) && (this->unk_860 >= 0) && (this->unk_834 <= 10)) {
                Vec3f sp90;
                f32 distXYZ;

                Matrix_MultVec3f(&sZeroVec, &sp90);
                distXYZ = Math_Vec3f_DistXYZ(D_80160000, &sp90);

                this->unk_858 = distXYZ - 3.0f;
                if (distXYZ < 3.0f) {
                    this->unk_858 = 0.0f;
                } else {
                    this->unk_858 *= 1.6f;
                    if (this->unk_858 > 1.0f) {
                        this->unk_858 = 1.0f;
                    }
                }

                this->unk_85C = -0.5f;
            }

            Matrix_Scale(1.0f, this->unk_858, 1.0f, MTXMODE_APPLY);

            if (!LINK_IS_ADULT) {
                Matrix_RotateZ(this->unk_858 * -0.2f, MTXMODE_APPLY);
            }

            gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPDisplayList(POLY_XLU_DISP++, stringData->dList);

            Matrix_Pop();

            CLOSE_DISPS(play->state.gfxCtx);
        } else if ((this->actor.scale.y >= 0.0f) && (this->rightHandType == PLAYER_MODELTYPE_RH_SHIELD)) {
            Matrix_Get(&this->shieldMf);
            Player_UpdateShieldCollider(play, this, &this->shieldQuad, sRightHandLimbModelShieldQuadVertices);
        }

        if (this->actor.scale.y >= 0.0f) {
            if (GameInteractor_Should(VB_DRAW_ADDITIONAL_RETICLES,
                                      (this->heldItemAction == PLAYER_IA_HOOKSHOT) ||
                                          (this->heldItemAction == PLAYER_IA_LONGSHOT),
                                      this)) {
                Matrix_MultVec3f(&D_80126184, &this->unk_3C8);

                if (heldActor != NULL) {
                    MtxF sp44;
                    s32 pad;

                    Matrix_MultVec3f(&D_80126190, &heldActor->world.pos);
                    Matrix_RotateZYX(0, -0x4000, -0x4000, MTXMODE_APPLY);
                    Matrix_Get(&sp44);
                    Matrix_MtxFToYXZRotS(&sp44, &heldActor->world.rot, 0);
                    heldActor->shape.rot = heldActor->world.rot;

                    if (func_8002DD78(this) != 0) {
                        Matrix_Translate(500.0f, 300.0f, 0.0f, MTXMODE_APPLY);
                        Player_DrawHookshotReticle(
                            play, this,
                            ((this->heldItemAction == PLAYER_IA_HOOKSHOT) ? 38600.0f : 77600.0f) *
                                CVarGetFloat(CVAR_CHEAT("HookshotReachMultiplier"), 1.0f));
                    }
                }
            }

            if ((this->unk_862 != 0) || ((func_8002DD6C(this) == 0) && (heldActor != NULL))) {
                if (!(this->stateFlags1 & PLAYER_STATE1_GETTING_ITEM) && (this->unk_862 != 0) &&
                    (this->exchangeItemId != EXCH_ITEM_NONE)) {
                    Math_Vec3f_Copy(&sGetItemRefPos, &this->leftHandPos);
                } else {
                    sGetItemRefPos.x = (this->bodyPartsPos[15].x + this->leftHandPos.x) * 0.5f;
                    sGetItemRefPos.y = (this->bodyPartsPos[15].y + this->leftHandPos.y) * 0.5f;
                    sGetItemRefPos.z = (this->bodyPartsPos[15].z + this->leftHandPos.z) * 0.5f;
                }

                if (this->unk_862 == 0) {
                    Math_Vec3f_Copy(&heldActor->world.pos, &sGetItemRefPos);
                }
            }
        }
    } else if (this->actor.scale.y >= 0.0f) {
#ifdef COMBO_BUILD
        if ((limbIndex == PLAYER_LIMB_L_FOREARM) || (limbIndex == PLAYER_LIMB_R_FOREARM)) {
            Combo_ZoraArmFin(play, this, (limbIndex == PLAYER_LIMB_L_FOREARM) ? 0 : 1);
        }
        if ((limbIndex == PLAYER_LIMB_TORSO) || (limbIndex == PLAYER_LIMB_HEAD)) {
            Combo_FormInstrumentDraw(play, this, limbIndex);
        }
#endif
#ifdef COMBO_BUILD
        if ((limbIndex == PLAYER_LIMB_HAT) && (Combo_OotForm() == COMBO_FORM_DEKU)) {
            // ComboShip: MM's white trail off the tip of Deku Link's hat while he spins (MM
            // Player_PostLimbDrawGameplay, PLAYER_LIMB_HAT with PLAYER_STATE3_100000: hat-space x 3000 -> 2300 into the
            // melee weapon trail).
            extern s32 Combo_DekuSpinCameraYaw(s16 * outYaw);
            static s32 sComboHatTrail = false;
            s16 spinYaw;
            if (Combo_DekuSpinCameraYaw(&spinYaw)) {
                static Vec3f sHatTip = { 3000.0f, 0.0f, 0.0f };
                static Vec3f sHatBase = { 2300.0f, 0.0f, 0.0f };
                Vec3f tip;
                Vec3f base;
                Matrix_MultVec3f(&sHatTip, &tip);
                Matrix_MultVec3f(&sHatBase, &base);
                if (!sComboHatTrail) {
                    EffectBlureShip_ChangeType(Effect_GetByIndex(this->meleeWeaponEffectIndex), TRAIL_TYPE_SWORDS);
                }
                sComboHatTrail = true;
                if (func_80090480(play, NULL, &this->meleeWeaponInfo[0], &tip, &base)) {
                    EffectBlure_AddVertex(Effect_GetByIndex(this->meleeWeaponEffectIndex),
                                          &this->meleeWeaponInfo[0].tip, &this->meleeWeaponInfo[0].base);
                }
            } else if (sComboHatTrail) {
                sComboHatTrail = false;
                this->meleeWeaponInfo[0].active = false; // spin over: the next trail starts fresh
            }
        }
#endif
        if (limbIndex == PLAYER_LIMB_SHEATH) {
            if ((this->rightHandType != PLAYER_MODELTYPE_RH_SHIELD) &&
                (this->rightHandType != PLAYER_MODELTYPE_RH_FF)) {
                if (Player_IsChildWithHylianShield(this)) {
                    Player_UpdateShieldCollider(play, this, &this->shieldQuad, sSheathLimbModelShieldQuadVertices);
                }

                Matrix_TranslateRotateZYX(&sSheathLimbModelShieldOnBackPos, &sSheathLimbModelShieldOnBackZyxRot);
                Matrix_Get(&this->shieldMf);
            }
        } else if (limbIndex == PLAYER_LIMB_HEAD) {
            Matrix_MultVec3f(&sPlayerFocusOffsetFromHead, &this->actor.focus.pos);
#ifdef COMBO_BUILD
            // ComboShip: the bubble Deku Link is blowing hangs off his snout (MM D_801C0EA0, head limb space).
            if ((Combo_OotForm() == COMBO_FORM_DEKU) && (this->stateFlags1 & PLAYER_STATE1_READY_TO_FIRE) &&
                (this->heldActor != NULL) && (this->heldActor->id == ACTOR_EN_ARROW) &&
                (this->heldActor->params == ARROW_COMBO_DEKU_BUBBLE)) {
                static Vec3f sDekuMouth = { 1300.0f, -400.0f, 0.0f };
                Actor* bubble = this->heldActor;
                MtxF mouthMf;

                Matrix_Push();
                Matrix_MultVec3f(&sDekuMouth, &bubble->world.pos);
                Matrix_RotateZYX(0, 0x4000, 0, MTXMODE_APPLY);
                Matrix_Get(&mouthMf);
                Matrix_MtxFToYXZRotS(&mouthMf, &bubble->world.rot, 0);
                bubble->shape.rot = bubble->world.rot;
                Matrix_Pop();
            }
            if (sComboDekuHeadPushed) {
                Matrix_Pop();
                sComboDekuHeadPushed = false;
            }
            // ComboShip (merged pause menus): an MM mask worn in Hyrule is MM's own model on the head limb, drawn as
            // MM's z_player_lib.c draws it (head limb space, D_801C0B20), routed to MM's ResourceManager.
            if ((Combo_OotForm() == COMBO_FORM_HUMAN) && (this->actor.id == ACTOR_PLAYER)) {
                const void* mmMask = Combo_PauseWornMmMaskDL();
                if (mmMask != NULL) {
                    if (Combo_PauseWornMmMaskItem() == COMBO_MM_ITEM_MASK_BUNNY) {
                        Combo_DrawMmBunnyEars(play, this); // the ears' two matrices (segment 0x0B), as MM sets them
                    } else if (Combo_PauseWornMmMaskItem() == COMBO_MM_ITEM_MASK_GREAT_FAIRY) {
                        Combo_DrawMmGreatFairyHair(play, this); // the hair's six matrices (segment 0x0B)
                    } else if (Combo_PauseWornMmMaskItem() == COMBO_MM_ITEM_MASK_CIRCUS_LEADER) {
                        Combo_DrawMmCircusLeaderTears(play, this); // MM Player_DrawCircusLeadersMask (XLU tears)
                    }
                    OPEN_DISPS(play->state.gfxCtx);
                    if (Combo_PauseWornMmMaskItem() == COMBO_MM_ITEM_MASK_COUPLE) {
                        // MM Player_DrawCouplesMask: AnimatedMat_DrawOpa(object_mask_meoto_Matanimheader_001CD8). MM's
                        // gSegments[0xA] = maskObjectSegment is N64 object addressing; resources are by path here.
                        Combo_MmAnimatedMat(play, "objects/object_mask_meoto/object_mask_meoto_Matanimheader_001CD8",
                                            1.0f, 1); // AnimatedMat_DrawOpa
                    }
                    if (Combo_PauseWornMmMaskItem() == COMBO_MM_ITEM_MASK_BLAST) {
                        // MM's Blast Mask list calls segment 0x09 (Player_DrawBlastMask). Not exploding: D_801C0BC0.
                        // After a blast (blastMaskTimer running): MM's scorched overlay (Matanimheader_0011F8 scroll +
                        // DL_000440) fading out over the last 10 frames, and the mask drawn see-through (D_801C0BD0).
                        static Gfx sComboBlastIdle[] = {
                            gsDPSetEnvColor(0, 0, 0, 255),
                            gsSPEndDisplayList(),
                        };
                        static Gfx sComboBlastXlu[] = {
                            gsDPSetRenderMode(AA_EN | Z_CMP | Z_UPD | IM_RD | CLR_ON_CVG | CVG_DST_WRAP | ZMODE_XLU |
                                                  FORCE_BL | G_RM_FOG_SHADE_A,
                                              AA_EN | Z_CMP | Z_UPD | IM_RD | CLR_ON_CVG | CVG_DST_WRAP | ZMODE_XLU |
                                                  FORCE_BL | GBL_c2(G_BL_CLR_IN, G_BL_A_IN, G_BL_CLR_MEM, G_BL_1MA)),
                            gsSPEndDisplayList(),
                        };
                        if (gComboBlastMaskTimer != 0) {
                            const s32 alpha =
                                (gComboBlastMaskTimer <= 10) ? (s32)((gComboBlastMaskTimer / 10.0f) * 255) : 255;
                            const void* scorch =
                                Combo_MmDL("objects/object_mask_bakuretu/object_mask_bakuretu_DL_000440");
                            Combo_MmAnimatedMat(
                                play, "objects/object_mask_bakuretu/object_mask_bakuretu_Matanimheader_0011F8", 1.0f,
                                1); // AnimatedMat_DrawOpa
                            gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx),
                                      G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
                            gDPSetEnvColor(POLY_OPA_DISP++, 0, 0, 0, (u8)alpha);
                            if (scorch != NULL) {
                                gSPDisplayList(POLY_OPA_DISP++, scorch);
                            }
                            gSPSegment(POLY_OPA_DISP++, 0x09, sComboBlastXlu);
                            gDPSetEnvColor(POLY_OPA_DISP++, 0, 0, 0, (u8)(255 - alpha));
                        } else {
                            gSPSegment(POLY_OPA_DISP++, 0x09, sComboBlastIdle);
                        }
                    }
                    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx),
                              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
                    gSPDisplayList(POLY_OPA_DISP++, mmMask);
                    if (Combo_PauseWornMmMaskItem() == COMBO_MM_ITEM_MASK_BLAST) {
                        // The mask leaves its env colour (black) set; OOT draws the rest of Link's limbs with the
                        // tunic colour in env, so put it back.
                        gDPSetEnvColor(POLY_OPA_DISP++, sComboTunicEnv.r, sComboTunicEnv.g, sComboTunicEnv.b, 0);
                    }
                    CLOSE_DISPS(play->state.gfxCtx);
                }
            }
#endif
        } else {
            Vec3f* vec = &sLeftRightFootLimbModelFootPos[(gSaveContext.linkAge)];

            Actor_SetFeetPos(&this->actor, limbIndex, PLAYER_LIMB_L_FOOT, vec, PLAYER_LIMB_R_FOOT, vec);
        }
    }
}

u32 func_80091738(PlayState* play, u8* segment, SkelAnime* skelAnime) {
    s16 linkObjectId = gLinkObjectIds[gSaveContext.linkAge];
    size_t size;
    void* ptr;

    size = gObjectTable[OBJECT_GAMEPLAY_KEEP].vromEnd - gObjectTable[OBJECT_GAMEPLAY_KEEP].vromStart;
    ptr = segment + 0x3800;
    DmaMgr_SendRequest1(ptr, gObjectTable[OBJECT_GAMEPLAY_KEEP].vromStart, size, __FILE__, __LINE__);

    size = gObjectTable[linkObjectId].vromEnd - gObjectTable[linkObjectId].vromStart;
    ptr = segment + 0x8800;
    DmaMgr_SendRequest1(ptr, gObjectTable[linkObjectId].vromStart, size, __FILE__, __LINE__);

    ptr = (void*)ALIGN16((intptr_t)ptr + size);

    gSegments[4] = VIRTUAL_TO_PHYSICAL(segment + 0x3800);
    gSegments[6] = VIRTUAL_TO_PHYSICAL(segment + 0x8800);

    SkelAnime_InitLink(play, skelAnime, gPlayerSkelHeaders[gSaveContext.linkAge], &gPlayerAnim_link_normal_wait, 9, ptr,
                       ptr, PLAYER_LIMB_MAX);

    return size + 0x8800 + 0x90;
}

u8 sPauseModelGroupBySword[] = {
    PLAYER_MODELGROUP_SWORD_AND_SHIELD, // PLAYER_SWORD_KOKIRI
    PLAYER_MODELGROUP_SWORD_AND_SHIELD, // PLAYER_SWORD_MASTER
    PLAYER_MODELGROUP_BGS,              // PLAYER_SWORD_BIGGORON
};

s32 Player_OverrideLimbDrawPause(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot, void* arg) {
    u8* playerSwordAndShield = arg;
    // SOH: Ensure positive value from playerSwordAndShield[] to avoid OOB array access.
    //      This can occur in the case where playerSwordAndShield[0] is PLAYER_SWORD_NONE
    u8 modelGroup =
        sPauseModelGroupBySword[playerSwordAndShield[0] > 0 ? playerSwordAndShield[0] - PLAYER_SWORD_KOKIRI : 0];
    s32 type;
    s32 dListOffset = 0;
    Gfx** dLists;
    size_t ptrSize = sizeof(uint32_t);

    if ((modelGroup == PLAYER_MODELGROUP_SWORD_AND_SHIELD) && !LINK_IS_ADULT &&
        (playerSwordAndShield[1] == PLAYER_SHIELD_HYLIAN)) {
        modelGroup = PLAYER_MODELGROUP_CHILD_HYLIAN_SHIELD;
    }

    if (limbIndex == PLAYER_LIMB_L_HAND) {
        type = gPlayerModelTypes[modelGroup][PLAYER_MODELGROUPENTRY_LEFT_HAND];
        sLeftHandType = type;

        // SOH: Handle unexpected swordless case. Previously OOB array access is avoided, but we want the
        //      hand model-type to be set to open (otherwise it is set to holding sword model-type)
        if (playerSwordAndShield[0] == PLAYER_SWORD_NONE) {
            type = PLAYER_MODELTYPE_LH_OPEN;
        }

        if ((type == PLAYER_MODELTYPE_LH_BGS) && (gSaveContext.swordHealth <= 0.0f)) {
            dListOffset = 4;
        }
    } else if (limbIndex == PLAYER_LIMB_R_HAND) {
        type = gPlayerModelTypes[modelGroup][PLAYER_MODELGROUPENTRY_RIGHT_HAND];
        sRightHandType = type;
        if (type == PLAYER_MODELTYPE_RH_SHIELD) {
            dListOffset = playerSwordAndShield[1] * ptrSize;
        }
    } else if (limbIndex == PLAYER_LIMB_SHEATH) {
        type = gPlayerModelTypes[modelGroup][PLAYER_MODELGROUPENTRY_SHEATH];
        if ((type == PLAYER_MODELTYPE_SHEATH_18) || (type == PLAYER_MODELTYPE_SHEATH_19)) {
            dListOffset = playerSwordAndShield[1] * ptrSize;
        }
    } else if (limbIndex == PLAYER_LIMB_WAIST) {
        type = gPlayerModelTypes[modelGroup][PLAYER_MODELGROUPENTRY_WAIST];

        if (Player_IsCustomLinkModel()) {
            return 0;
        }
    } else {
        return 0;
    }

    dLists = &sPlayerDListGroups[type][gSaveContext.linkAge];
    *dList = dLists[dListOffset];

    GameInteractor_Should(VB_PLAYER_OVERRIDE_LIMB_DRAW_PAUSE, true, limbIndex, dList, GET_PLAYER(play), play);

    return 0;
}

#include <overlays/actors/ovl_Demo_Effect/z_demo_effect.h>
void DemoEffect_DrawTriforceSpot(Actor* thisx, PlayState* play);

void Pause_DrawTriforceSpot(PlayState* play, s32 showLightColumn) {
    static DemoEffect triforce;
    static s16 rotation = 0;

    triforce.triforceSpot.crystalLightOpacity = 244;
    triforce.triforceSpot.triforceSpotOpacity = 249;
    triforce.triforceSpot.lightColumnOpacity = showLightColumn ? 244 : 0;
    triforce.triforceSpot.rotation = rotation;

    DemoEffect_DrawTriforceSpot(&triforce, play);

    rotation += 0x03E8;
}

void Player_DrawPauseImpl(PlayState* play, void* gameplayKeep, void* linkObject, SkelAnime* skelAnime, Vec3f* pos,
                          Vec3s* rot, f32 scale, s32 sword, s32 tunic, s32 shield, s32 boots, s32 width, s32 height,
                          Vec3f* eye, Vec3f* at, f32 fovy, void* colorFrameBuffer, void* depthFrameBuffer) {
    // Note: the viewport x and y values are overwritten below, before usage
    static Vp viewport = { (PAUSE_EQUIP_PLAYER_WIDTH / 2) << 2, (PAUSE_EQUIP_PLAYER_HEIGHT / 2) << 2, G_MAXZ / 2, 0,
                           (PAUSE_EQUIP_PLAYER_WIDTH / 2) << 2, (PAUSE_EQUIP_PLAYER_HEIGHT / 2) << 2, G_MAXZ / 2, 0 };
    static Lights1 lights1 = gdSPDefLights1(80, 80, 80, 255, 255, 255, 84, 84, -84);
    static Vec3f lightDir = { 89.8f, 0.0f, 89.8f };
    u8 playerSwordAndShield[2];
    Gfx* opaRef;
    Gfx* xluRef;
    u16 perspNorm;
    Mtx* perspMtx = Graph_Alloc(play->state.gfxCtx, sizeof(Mtx));
    Mtx* lookAtMtx = Graph_Alloc(play->state.gfxCtx, sizeof(Mtx));

    u8 mirrorWorldActive = CVarGetInteger(CVAR_ENHANCEMENT("MirroredWorld"), 0);

    OPEN_DISPS(play->state.gfxCtx);

    opaRef = POLY_OPA_DISP;
    POLY_OPA_DISP++;

    xluRef = POLY_XLU_DISP;
    POLY_XLU_DISP++;

    gSPDisplayList(WORK_DISP++, POLY_OPA_DISP);
    gSPDisplayList(WORK_DISP++, POLY_XLU_DISP);

    if (mirrorWorldActive) {
        gSPSetExtraGeometryMode(POLY_OPA_DISP++, G_EX_INVERT_CULLING);
        gSPSetExtraGeometryMode(POLY_XLU_DISP++, G_EX_INVERT_CULLING);
    }

    gSPSegment(POLY_OPA_DISP++, 0x00, NULL);

    gDPPipeSync(POLY_OPA_DISP++);

    gSPLoadGeometryMode(POLY_OPA_DISP++, 0);
    gSPTexture(POLY_OPA_DISP++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE);
    gDPSetOtherMode(POLY_OPA_DISP++,
                    G_AD_DISABLE | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_BILERP | G_TT_NONE | G_TL_TILE |
                        G_TD_CLAMP | G_TP_PERSP | G_CYC_FILL | G_PM_NPRIMITIVE,
                    G_AC_NONE | G_ZS_PIXEL | G_RM_NOOP | G_RM_NOOP2);

    // Also matches if some of the previous graphics commands are moved inside this block too. Possible macro?
    if (1) {
        s32 pad[2];

        gSPLoadGeometryMode(POLY_OPA_DISP++, G_ZBUFFER | G_SHADE | G_CULL_BACK | G_LIGHTING | G_SHADING_SMOOTH);
    }

    gDPSetScissor(POLY_OPA_DISP++, G_SC_NON_INTERLACE, 0, 0, width, height);
    gSPClipRatio(POLY_OPA_DISP++, FRUSTRATIO_1);

    gDPSetColorImage(POLY_OPA_DISP++, G_IM_FMT_RGBA, G_IM_SIZ_16b, width, depthFrameBuffer);
    gDPSetCycleType(POLY_OPA_DISP++, G_CYC_FILL);
    gDPSetRenderMode(POLY_OPA_DISP++, G_RM_NOOP, G_RM_NOOP2);
    gDPSetFillColor(POLY_OPA_DISP++, (GPACK_ZDZ(G_MAXFBZ, 0) << 16) | GPACK_ZDZ(G_MAXFBZ, 0));
    gDPFillRectangle(POLY_OPA_DISP++, 0, 0, width - 1, height - 1);

    gDPPipeSync(POLY_OPA_DISP++);

    gDPSetColorImage(POLY_OPA_DISP++, G_IM_FMT_RGBA, G_IM_SIZ_16b, width, colorFrameBuffer);
    gDPSetCycleType(POLY_OPA_DISP++, G_CYC_FILL);
    gDPSetRenderMode(POLY_OPA_DISP++, G_RM_NOOP, G_RM_NOOP2);
    gDPSetFillColor(POLY_OPA_DISP++, (GPACK_RGBA5551(0, 0, 0, 1) << 16) | GPACK_RGBA5551(0, 0, 0, 1));
    gDPFillRectangle(POLY_OPA_DISP++, 0, 0, width - 1, height - 1);

    gDPPipeSync(POLY_OPA_DISP++);

    gDPSetDepthImage(POLY_OPA_DISP++, depthFrameBuffer);

    viewport.vp.vscale[0] = viewport.vp.vtrans[0] = width * ((1 << 2) / 2);
    viewport.vp.vscale[1] = viewport.vp.vtrans[1] = height * ((1 << 2) / 2);
    gSPViewport(POLY_OPA_DISP++, &viewport);

    guPerspective(perspMtx, &perspNorm, fovy, (f32)width / (f32)height, 10.0f, 4000.0f, 1.0f);

    gSPPerspNormalize(POLY_OPA_DISP++, perspNorm);
    gSPMatrix(POLY_OPA_DISP++, perspMtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);

    guLookAt(lookAtMtx, eye->x, eye->y, eye->z, at->x, at->y, at->z, 0.0f, 1.0f, 0.0f);

    gSPMatrix(POLY_OPA_DISP++, lookAtMtx, G_MTX_NOPUSH | G_MTX_MUL | G_MTX_PROJECTION);

    playerSwordAndShield[0] = sword;
    playerSwordAndShield[1] = shield;

    Matrix_SetTranslateRotateYXZ(
        pos->x - ((CVarGetInteger(CVAR_ENHANCEMENT("PauseMenuAnimatedLink"), 0) && LINK_AGE_IN_YEARS == YEARS_ADULT)
                      ? 25
                      : 0),
        pos->y - (CVarGetInteger(CVAR_GENERAL("PauseMenuAnimatedLinkTriforce"), 0) ? 16 : 0), pos->z, rot);
    Matrix_Scale(scale * (mirrorWorldActive ? -1 : 1), scale, scale, MTXMODE_APPLY);

    gSPSegment(POLY_OPA_DISP++, 0x04, gameplayKeep);
    gSPSegment(POLY_OPA_DISP++, 0x06, linkObject);

    gSPSetLights1(POLY_OPA_DISP++, lights1);

    func_80093C80(play);

    POLY_OPA_DISP = Gfx_SetFog2(POLY_OPA_DISP++, 0, 0, 0, 0, 997, 1000);

    func_8002EABC(pos, &play->view.eye, &lightDir, play->state.gfxCtx);

    gSPSegment(POLY_OPA_DISP++, 0x0C, gCullBackDList);

    Player_DrawImpl(play, skelAnime->skeleton, skelAnime->jointTable, skelAnime->dListCount, 0, tunic, boots, 0,
                    Player_OverrideLimbDrawPause, NULL, &playerSwordAndShield);

    if (CVarGetInteger(CVAR_GENERAL("PauseMenuAnimatedLinkTriforce"), 0)) {
        Matrix_SetTranslateRotateYXZ(pos->x - (LINK_AGE_IN_YEARS == YEARS_ADULT ? 25 : 0),
                                     pos->y + 280 + (LINK_AGE_IN_YEARS == YEARS_ADULT ? 48 : 0), pos->z, rot);
        Matrix_Scale(scale * (mirrorWorldActive ? -1 : 1), scale * 1, scale * 1, MTXMODE_APPLY);

        Pause_DrawTriforceSpot(play, 1);
    }

    if (mirrorWorldActive) {
        gSPClearExtraGeometryMode(POLY_OPA_DISP++, G_EX_INVERT_CULLING);
        gSPClearExtraGeometryMode(POLY_XLU_DISP++, G_EX_INVERT_CULLING);
    }

    gSPEndDisplayList(POLY_OPA_DISP++);
    gSPEndDisplayList(POLY_XLU_DISP++);

    gSPBranchList(opaRef, POLY_OPA_DISP);
    gSPBranchList(xluRef, POLY_XLU_DISP);

    CLOSE_DISPS(play->state.gfxCtx);
}

void Player_DrawPause(PlayState* play, u8* segment, SkelAnime* skelAnime, Vec3f* pos, Vec3s* rot, f32 scale, s32 sword,
                      s32 tunic, s32 shield, s32 boots) {
    Input* p1Input = &play->state.input[0];
    Vec3f eye = { 0.0f, 0.0f, -400.0f };
    Vec3f at = { 0.0f, 0.0f, 0.0f };
    Vec3s* destTable;
    Vec3s* srcTable;
    s32 i;
    bool canswitchrnd = false;

    gSegments[4] = VIRTUAL_TO_PHYSICAL(segment + 0x3800);
    gSegments[6] = VIRTUAL_TO_PHYSICAL(segment + 0x8800);

    uintptr_t* PauseMenuAnimSet[4] = { // IDLE                       // Two Handed                       // No shield //
                                       // Kid Hylian Shield
                                       gPlayerAnim_link_normal_wait, gPlayerAnim_link_fighter_wait_long,
                                       gPlayerAnim_link_normal_wait_free, gPlayerAnim_link_normal_wait_free
    };

    if (CVarGetInteger(CVAR_ENHANCEMENT("PauseMenuAnimatedLink"), 0) ||
        CVarGetInteger(CVAR_GENERAL("PauseMenuAnimatedLinkTriforce"), 0)) {
        uintptr_t anim = 0; // Initialise anim

        s16 EquipedStance;
        if (CUR_EQUIP_VALUE(EQUIP_TYPE_SWORD) >= EQUIP_VALUE_SWORD_BIGGORON) {
            EquipedStance = 1;
        } else if (CUR_EQUIP_VALUE(EQUIP_TYPE_SHIELD) == EQUIP_VALUE_SWORD_NONE) {
            EquipedStance = 2;
        } else if (CUR_EQUIP_VALUE(EQUIP_TYPE_SHIELD) == EQUIP_VALUE_SWORD_MASTER && LINK_AGE_IN_YEARS == YEARS_CHILD) {
            EquipedStance = 3;
        } else {
            // Link is idle so revert to 0
            EquipedStance = 0;
        }

        if (!CVarGetInteger(CVAR_GENERAL("PauseMenuAnimatedLinkTriforce"), 0)) {
            anim = PauseMenuAnimSet[EquipedStance];
        } else {
            anim = gPlayerAnim_link_magic_kaze2;
            sword = 0;
            shield = 0;
        }

        if (skelAnime->animation != anim) {
            LinkAnimation_Change(play, skelAnime, anim, 1.0f, 0.0f, Animation_GetLastFrame(anim), ANIMMODE_LOOP, -6.0f);
        }

        LinkAnimation_Update(play, skelAnime);

        if (!LINK_IS_ADULT) {
            // Link is placed too far up by default when animating
            at.y += 60;
        }
    } else {

        if (!LINK_IS_ADULT) {
            if (shield == PLAYER_SHIELD_DEKU) {
                srcTable = gLinkPauseChildDekuShieldJointTable;
            } else {
                srcTable = gLinkPauseChildJointTable;
            }
        } else {
            if (sword == PLAYER_SWORD_BIGGORON) {
                srcTable = gLinkPauseAdultBgsJointTable;
            } else if (shield != PLAYER_SHIELD_NONE) {
                srcTable = gLinkPauseAdultShieldJointTable;
            } else {
                srcTable = gLinkPauseAdultJointTable;
            }
        }

        srcTable = ResourceMgr_LoadArrayByNameAsVec3s(srcTable);
        Vec3s* ogSrcTable = srcTable;
        destTable = skelAnime->jointTable;
        for (i = 0; i < skelAnime->limbCount; i++) {
            *destTable++ = *srcTable++;
        }
        free(ogSrcTable);
    }

    Player_DrawPauseImpl(play, segment + 0x3800, segment + 0x8800, skelAnime, pos, rot, scale, sword, tunic, shield,
                         boots, PAUSE_EQUIP_PLAYER_WIDTH, PAUSE_EQUIP_PLAYER_HEIGHT, &eye, &at, 60.0f,
                         play->state.gfxCtx->curFrameBuffer,
                         play->state.gfxCtx->curFrameBuffer + (PAUSE_EQUIP_PLAYER_WIDTH * PAUSE_EQUIP_PLAYER_HEIGHT));
}
