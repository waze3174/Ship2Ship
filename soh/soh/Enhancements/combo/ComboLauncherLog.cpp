// ComboShip: the launcher's way into the game's log. Combo-owned file (soh GLOB_RECURSE). The launcher has no logger of
// its own: it hands its lines here, and they land in logs/Ship2Ship.log beside the game's. See
// combo/ComboLauncherLog.h.
#ifdef COMBO_BUILD

#include <spdlog/spdlog.h>

#include "ComboExport.h"
#include <ship/Context.h>

namespace {

// The values of ComboLauncherLog::Level.
constexpr int kLevelWarn = 1;
constexpr int kLevelError = 2;

spdlog::level::level_enum SpdlogLevel(int level) {
    if (level >= kLevelError)
        return spdlog::level::err;
    return (level == kLevelWarn) ? spdlog::level::warn : spdlog::level::info;
}

} // namespace

// 1 when the line was logged, 0 while the game's logger does not exist yet (the launcher then keeps the line).
extern "C" COMBO_EXPORT int SOH_LauncherLog(int level, const char* message) {
    const auto* context = Ship::Context::GetRawInstance();
    if (context == nullptr || message == nullptr)
        return 0;
    const auto logger = context->GetLogger();
    if (logger == nullptr)
        return 0;
    logger->log(SpdlogLevel(level), "{}", message);
    if (level >= kLevelError)
        logger->flush(); // an error may be the last thing the process writes
    return 1;
}

#endif // COMBO_BUILD
