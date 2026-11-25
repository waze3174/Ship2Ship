
#ifndef SAVE_MANAGER_H
#define SAVE_MANAGER_H

#ifdef __cplusplus
#include <string>
#include <filesystem>
#include <nlohmann/json.hpp>
#ifdef COMBO_BUILD
#include "ComboSaveTiming.h"    // ComboShip: launcher IO stats for the opt-in save-timing log
#include "rando/CrossForeign.h" // ComboShip: merged-save IO callback typedefs
// ComboShip: register the launcher's .combosav IO callbacks (routes file{N}.json IO into the container).
void SaveManager_SetComboSaveIO(ComboRando::FnComboReadSave r, ComboRando::FnComboWriteSave w);
void SaveManager_SetContainerIoStatsGetter(ComboSaveTiming::FnGetContainerIoStats get);
void SaveManager_SetComboLiveWriteRead(ComboRando::FnComboReadSaveForLiveWrite read);
// ComboShip: one write for the live saves that one pickup or one shared-items pass asks for. Between Begin and the
// matching End, SaveManager_SaveCurrentForCombo only marks a save as owed, and the outermost End writes it with the
// state as it is then. Batches nest. Game thread only.
void SaveManager_ComboSaveBatchBegin();
void SaveManager_ComboSaveBatchEnd();
class ComboSaveBatch {
  public:
    ComboSaveBatch() {
        SaveManager_ComboSaveBatchBegin();
    }
    ~ComboSaveBatch() {
        SaveManager_ComboSaveBatchEnd();
    }
    ComboSaveBatch(const ComboSaveBatch&) = delete;
    ComboSaveBatch& operator=(const ComboSaveBatch&) = delete;
};
// The live saves asked for and written since the previous call, for the opt-in frame-hitch log line.
struct ComboFrameSaves {
    int asked = 0;
    int written = 0;
    double ms = 0.0;
};
ComboFrameSaves SaveManager_ComboTakeFrameSaves();
#endif
std::string SaveManager_GetFileName(int fileNum, bool isBackup = false);
bool SaveManager_HandleFileDropped(char* filePath);
bool BinarySaveConverter_HandleFileDropped(char* filePath);
int SaveManager_GetOpenFileSlot();
void SaveManager_WriteSaveFile(const std::filesystem::path& fileName, nlohmann::json j);
void SaveManager_PersistSariaHintsAvailable();
// ComboShip: cross-game save activation/persistence entry points
void SaveManager_InitNewSaveForSlot(int mmFileNum, const unsigned char* ootName8 = nullptr);
// A new vanilla Majora's Mask save (SAVETYPE_VANILLA, opening not yet played), written to the slot.
void SaveManager_InitVanillaNewSaveForSlot(int mmFileNum, const unsigned char* ootName8);
// 0 = loaded; negative = nothing usable was loaded (codes at the definition). Never creates or persists:
// a failure is logged and leaves the fail-closed sentinel behind, and play still proceeds.
int SaveManager_LoadSaveFile(int mmFileNum);
void SaveManager_SaveCurrentForCombo();
// Stamps the "nothing usable loaded" sentinel a failed load leaves behind (fileNum 0xFF, VANILLA).
void SaveManager_MarkNoSaveLoaded();
#else
void SaveManager_SysFlashrom_WriteData(u8* addr, u32 pageNum, u32 pageCount);
s32 SaveManager_SysFlashrom_ReadData(void* addr, u32 pageNum, u32 pageCount);
#endif

#endif // SAVE_MANAGER_H
