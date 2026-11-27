// combo/gui/ComboApMenuApi.h
//
// The plain-C table through which Settings > Online > Archipelago (comboui.dll) reaches the launcher's client. The
// launcher fills it and hands it over with ComboUI_SetArchipelagoApi; strings are fixed buffers so nothing crosses the
// module boundary as a C++ object.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COMBO_AP_TEXT_MAX 256

// The connection's phase, as ComboArchipelago::Phase numbers it (ComboShip.cpp checks they agree).
enum {
    COMBO_AP_PHASE_OFF = 0,
    COMBO_AP_PHASE_CONNECTING = 1,
    COMBO_AP_PHASE_CONNECTED = 2,
    COMBO_AP_PHASE_REFUSED = 3,
    COMBO_AP_PHASE_WRONG_ROOM = 4,
};

// What the panel shows.
typedef struct ComboApMenuState {
    int32_t fileLoaded;   // the loaded file is an Archipelago file
    int32_t tlsAvailable; // wss:// is built in
    int32_t phase;
    int32_t receivedIndex;
    int32_t itemsWaiting;
    int32_t checksKnown;
    int32_t checksUnmapped;
    char status[COMBO_AP_TEXT_MAX];   // one line
    char server[COMBO_AP_TEXT_MAX];   // the file's saved server
    char slot[COMBO_AP_TEXT_MAX];     // the slot name to connect as
    char fileSlot[COMBO_AP_TEXT_MAX]; // the slot the file was generated for
} ComboApMenuState;

typedef struct ComboApMenuApi {
    void (*getState)(ComboApMenuState* out);
    // Saves the server and slot in the loaded file and connects. The password is used for this connection only.
    void (*connect)(const char* server, const char* slot, const char* password);
    // Disconnects and stops reconnecting when the file loads next time.
    void (*disconnect)(void);
} ComboApMenuApi;

#ifdef __cplusplus
}
#endif
