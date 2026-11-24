// soh/soh/Enhancements/randomizer/ComboMmItems.cpp — ComboShip: Majora's Mask items used in Hyrule (see
// ComboMmItems.h). Registers MM's ObjAqua port (soh/src/overlays/actors/ovl_Obj_Aqua) as a Ship actor.
//
// Why a new actor and not combo params on an existing OOT actor (as the Deku Bubble rides EnArrow and the Zora fins
// ride EnBoom): those are MM actors whose OOT twin exists (MM's bubble IS an EnArrow, MM's fins ARE an EnBoom). MM's
// ObjAqua has no OOT twin; hosting it on an unrelated OOT actor would grow that actor's struct, bend its
// init/update/draw and make every OOT reaction to that actor (its id, its category) see water. SoH's ActorDB hands out
// ids past ACTOR_ID_EXTRA_MAX at runtime (the way Ivan's En_Partner is added), so the port is its own actor, with MM's
// category, flags and object.
#ifdef COMBO_BUILD

#include <libultraship/libultraship.h> // C++ headers first: z64.h pulls <memory> under the extern "C" below
#include "soh/ActorDB.h"
#include "ComboMmItems.h"

extern "C" {
#include "z64.h"
#include "functions.h"
#include "src/overlays/actors/ovl_Obj_Aqua/z_obj_aqua.h"
}

extern "C" short Combo_MmAquaActorId(void) {
    static int sId = -1;

    if (sId >= 0) {
        return (short)sId;
    }
    if (ActorDB::Instance == nullptr) {
        return -1;
    }
    sId = ActorDB::Instance->RetrieveId("Obj_Aqua");
    if (sId < 0) {
        // MM's Obj_Aqua_Profile: ACTORCAT_ITEMACTION, ACTOR_FLAG_UPDATE_CULLING_DISABLED, GAMEPLAY_KEEP.
        ActorDBInit init = {
            "Obj_Aqua",
            "Bottle Water (MM)",
            -1, // next free id
            ACTORCAT_ITEMACTION,
            ACTOR_FLAG_UPDATE_CULLING_DISABLED,
            OBJECT_GAMEPLAY_KEEP,
            sizeof(ObjAqua),
            (ActorFunc)ObjAqua_Init,
            (ActorFunc)ObjAqua_Destroy,
            (ActorFunc)ObjAqua_Update,
            (ActorFunc)ObjAqua_Draw,
            nullptr,
        };
        sId = ActorDB::Instance->AddEntry(init).entry.id;
    }
    return (short)sId;
}

#endif // COMBO_BUILD
