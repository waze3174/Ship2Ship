// combo/discord/DiscordIpcPaths.h
//
// ComboShip: where Discord's IPC endpoint can be, by platform. Pure and header-only, so the lists are tested.
// See docs/deviations/discord.md.
#pragma once

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace ComboDiscord {

inline constexpr int kIpcSlots = 10; // discord-ipc-0 to discord-ipc-9: one per running Discord client
inline constexpr const char* kPipePrefix = "\\\\.\\pipe\\discord-ipc-";
inline constexpr const char* kSocketName = "discord-ipc-";

inline std::string WindowsPipeName(int slot) {
    return kPipePrefix + std::to_string(slot);
}

// Directories a Unix socket may live in, in the order to try. The Flatpak and Snap builds of Discord keep theirs in a
// subfolder of the runtime directory.
inline std::vector<std::string> UnixSocketPaths(const std::function<const char*(const char*)>& getenvFn) {
    static const char* const kBaseVars[] = { "XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP" };
    static const char* const kFallbackBase = "/tmp";
    static const char* const kSubfolders[] = { "", "app/com.discordapp.Discord/", "snap.discord/" };

    std::vector<std::string> bases;
    for (const char* var : kBaseVars) {
        const char* value = getenvFn(var);
        if (value != nullptr && value[0] != '\0')
            bases.push_back(value);
    }
    bases.push_back(kFallbackBase);

    std::vector<std::string> paths;
    for (std::string base : bases) {
        while (base.size() > 1 && base.back() == '/')
            base.pop_back();
        for (const char* subfolder : kSubfolders) {
            for (int slot = 0; slot < kIpcSlots; ++slot) {
                const std::string path = base + "/" + subfolder + kSocketName + std::to_string(slot);
                if (std::find(paths.begin(), paths.end(), path) == paths.end())
                    paths.push_back(path);
            }
        }
    }
    return paths;
}

} // namespace ComboDiscord
