// combo/rando/ComboSeedFileType.h
// Ship2Ship: the "fileType" marker of a consolidated combo seed. Header-only, shared by the launcher, the
// comborando validator, the combo menu and soh's file-drop handler. Writers stamp kSeedFileType; readers
// accept IsSeedFileType, which also takes the marker ComboShip builds wrote and the Archipelago seed's own marker.
#pragma once

#include <string>

namespace ComboRando {

inline constexpr const char* kSeedFileType = "Ship2ShipRandomizer";
inline constexpr const char* kLegacySeedFileType = "ComboShipRandomizer";
// An Archipelago slot's seed (ComboApSeed.h). A build that predates it knows no such marker and refuses the file.
inline constexpr const char* kApSeedFileType = "Ship2ShipArchipelago";

inline bool IsApSeedFileType(const std::string& fileType) {
    return fileType == kApSeedFileType;
}

inline bool IsSeedFileType(const std::string& fileType) {
    return fileType == kSeedFileType || fileType == kLegacySeedFileType || IsApSeedFileType(fileType);
}

} // namespace ComboRando
