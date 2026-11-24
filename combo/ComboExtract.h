// combo/ComboExtract.h
// ComboShip: ABI for the combo-owned ROM-extraction screen.
//
// ComboShip needs BOTH an OoT ROM and an MM ROM, and optionally an OoT Master Quest ROM. The launcher
// (ComboShip.exe) resolves the per-DLL extraction primitives from soh.dll / 2ship.dll, packs them into a
// ComboExtractCallbacks, and hands that to comboui.dll's ComboUI_RunExtraction(), which renders the screen +
// progress bars and drives the libultraship frame loop (the only place a window/ImGui context exists this
// early).
//
// Pure C ABI — no STL across the DLL boundary. Bool-ish values are int (0/1) so the resolved
// function-pointer types match the exports exactly.
#pragma once

extern "C" {

// Validate a ROM path (no dialog, no extraction). Returns nonzero if it's a recognized ROM.
typedef int (*ComboFnValidateRom)(const char* romPath);
// Begin extraction of romPath on a background task. Non-blocking. Returns nonzero if accepted.
typedef int (*ComboFnStartExtraction)(const char* romPath);
// Poll progress. *done is set when the task finishes; *success is valid once *done. count/total drive
// the progress bar (total==0 => "Starting Up"). Any pointer may be null.
typedef void (*ComboFnGetProgress)(unsigned long long* count, unsigned long long* total, int* done, int* success);

// Which OoT archive a ROM extracts to, judged from its header only. The two OoT slots use it to keep a
// Master Quest ROM out of the vanilla slot and the other way round. Returns a ComboRomQuest value.
enum ComboRomQuest {
    COMBO_ROM_QUEST_UNKNOWN = 0, // not a recognized OoT ROM
    COMBO_ROM_QUEST_VANILLA = 1, // extracts to oot.o2r
    COMBO_ROM_QUEST_MASTER = 2,  // extracts to oot-mq.o2r
};
typedef int (*ComboFnRomQuest)(const char* romPath);

typedef struct ComboExtractCallbacks {
    ComboFnValidateRom sohValidate;
    ComboFnStartExtraction sohStart;
    ComboFnGetProgress sohProgress;
    int sohNeeded; // 1 if oot.o2r (the vanilla OoT archive) is missing and must be extracted
    // Header-only version check for the folder auto-scan (no full-ROM read/CRC); may be null.
    ComboFnValidateRom sohClassify;
    // The optional OoT Master Quest slot reuses the OoT validate/classify/start/progress functions.
    int sohMqNeeded;          // 1 if oot-mq.o2r is missing; the slot is then offered, but never required
    ComboFnRomQuest sohQuest; // keeps the vanilla and Master Quest slots apart; required

    ComboFnValidateRom mmValidate;
    ComboFnStartExtraction mmStart;
    ComboFnGetProgress mmProgress;
    int mmNeeded; // 1 if the MM ROM archive is missing and must be extracted
    ComboFnValidateRom mmClassify;
} ComboExtractCallbacks;

// Implemented in comboui.dll (ComboExtractScreen.cpp). Blocks until every needed ROM is extracted
// (returns 1) or the player quits / an extraction fails / the window closes (returns 0). The launcher
// resolves this via GetProcAddress through the ComboFnRunExtraction typedef below.
typedef int (*ComboFnRunExtraction)(const ComboExtractCallbacks* cb);

} // extern "C"
