#pragma once
// ComboShip co-op: the C ABI between the launcher and comboui for the co-op UI (room-seed status, the
// Co-op Sync debug window, the different-seed modal). Plain C function pointers only — nothing with a
// C++ type crosses the module boundary (see combo/ComboExport.h).

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ComboCoopHooks {
    // JSON snapshots; the returned pointer stays valid until the next call of the same function.
    const char* (*seedStatusJson)(void); // SeedShare::StatusJson
    const char* (*statsJson)(void);      // per-type traffic + SessionGuard drops
    void (*shareSeed)(void);             // host: re-offer the room seed now
    void (*useRoomSeed)(void);           // joiner: apply the downloaded room seed (file select only)
    void (*idleTick)(void);              // called by comboui every frame; runs even while disconnected
} ComboCoopHooks;

#ifdef __cplusplus
}
#endif
