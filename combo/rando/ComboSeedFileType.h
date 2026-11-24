// combo/rando/ComboSeedFileType.h
// Ship2Ship: the "fileType" marker of a consolidated combo seed. Header-only, shared by the launcher, the
// comborando validator, the combo menu and soh's file-drop handler. Writers stamp kSeedFileType; readers
// accept IsSeedFileType, which also takes the marker ComboShip builds wrote.
#pragma once

#include <string>

namespace ComboRando {

inline constexpr const char* kSeedFileType = "Ship2ShipRandomizer";
inline constexpr const char* kLegacySeedFileType = "ComboShipRandomizer";

inline bool IsSeedFileType(const std::string& fileType) {
    return fileType == kSeedFileType || fileType == kLegacySeedFileType;
}

} // namespace ComboRando
