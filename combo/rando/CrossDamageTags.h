// combo/rando/CrossDamageTags.h — ComboShip: the MM damage type an MM attack carries in Hyrule.
//
// OOT damage tables index ONE OOT damage bit, so a form attack that carries, say, DMG_SLASH_KOKIRI makes every OOT
// enemy react as to a Kokiri slash. MM decides each enemy's reaction to each attack separately (DamageTable rows
// DMG_DEKU_SPIN, DMG_GORON_PUNCH, ...). So each MM attack also carries its MM damage type in the collider's toucher
// (ColliderTouch.comboMmDmg = MM DMG_* bit index + 1, comboMmAtDamage = MM's AT damage for that attack), and OOT's
// collision check looks up the hit enemy's MM reaction in combo/rando/CrossDamageMap.h. The OOT damage bit stays on
// the collider: it is what OOT's puzzle actors read (hammer switches, boomerang targets, eye switches, rocks, grass).
// Link's own OOT weapons carry no tag and behave exactly as in vanilla OOT.
#pragma once

// MM damage types (mm/include/z64collision_check.h DMG_*), as tags: bit index + 1. 0 = untagged (an OOT attack).
#define CROSS_MM_TAG_NONE 0
#define CROSS_MM_TAG_EXPLOSIVES (0x03 + 1)     // MM bombs / Blast Mask (EnBom DMG_EXPLOSIVES, 2)
#define CROSS_MM_TAG_ZORA_BOOMERANG (0x04 + 1) // Zora fins (EnBoom 0x10, 2)
#define CROSS_MM_TAG_GORON_PUNCH (0x08 + 1)    // Goron punches (D_8085D09C[NONE]: 2 / 2)
#define CROSS_MM_TAG_GORON_POUND (0x0A + 1)    // Goron ground pound (Player_SetCylinderForAttack 4, r60)
#define CROSS_MM_TAG_GORON_SPIKES (0x0E + 1)   // spiked Goron roll (1, r25)
#define CROSS_MM_TAG_DEKU_SPIN (0x0F + 1)      // Deku spin (1, r30)
#define CROSS_MM_TAG_DEKU_BUBBLE (0x10 + 1)    // Deku bubble (EnArrow ARROW_TYPE_DEKU_BUBBLE, 2)
#define CROSS_MM_TAG_ZORA_BARRIER (0x13 + 1)   // Zora barrier (shieldCylinder 0x80000, 2)
#define CROSS_MM_TAG_ZORA_PUNCH (0x17 + 1)     // Zora jabs 1 / jump kick 2 (D_8085D09C[ZORA_BOOMERANG])
#define CROSS_MM_TAG_NORMAL_ROLL (0x1A + 1)    // plain Goron roll (1, r25)
#define CROSS_MM_TAG_POWDER_KEG (0x1F + 1)     // Powder Keg (EnBom 0x80000008 -> MM's table row 31, 4)
#define CROSS_MM_TAG_SWORD_BEAM (0x19 + 1)     // Fierce Deity beam (EnMThunder DMG_SWORD_BEAM, 3)

// Tag a collider element's toucher (a ColliderInfo*'s .toucher, any collider shape).
#define CROSS_MM_TAG_TOUCHER(toucherPtr, tag, atDamage)    \
    do {                                                   \
        (toucherPtr)->comboMmDmg = (tag);                  \
        (toucherPtr)->comboMmAtDamage = (atDamage);        \
    } while (0)
