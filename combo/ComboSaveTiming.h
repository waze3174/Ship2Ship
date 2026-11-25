#pragma once

// combo/ComboSaveTiming.h — ComboShip: opt-in timing of the combo save path, so a log can show what the hitch after a
// pickup is made of. Off by default; turn it on with `set gCombo.Debug.SaveTiming 1` in the console. Each game DLL
// logs the lines itself (the launcher has no log file); the launcher only measures its share, see ContainerIoStats.

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <string>

namespace ComboSaveTiming {

inline constexpr const char* kCVar = "gCombo.Debug.SaveTiming";

// A per-frame call is only worth a log line when it did real work; an idle one takes microseconds.
inline constexpr double kSlowCallMs = 1.0;

// A game frame normally ends 50 ms after the previous one (both games update at 20 Hz). Gaps of 2 s or more are
// switches between the games or a parked game, not hitches.
inline constexpr double kFrameHitchMs = 100.0;
inline constexpr double kFrameHitchMaxMs = 2000.0;

class Stopwatch {
  public:
    // Milliseconds since construction or since the previous Lap().
    double Lap() {
        const auto now = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(now - mLast).count();
        mLast = now;
        return ms;
    }

    double TotalMs() const {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - mStart).count();
    }

  private:
    std::chrono::steady_clock::time_point mStart = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point mLast = mStart;
};

// What the launcher's last container read or write on the calling thread spent. The launcher keeps one per thread and
// hands each game a getter, so a game's timing line can show the launcher's share. Plain data: it crosses the exe/DLL
// boundary.
struct ContainerIoStats {
    double lockWaitMs = 0.0;      // waiting for the container mutex (the other game's save can hold it)
    double parseMs = 0.0;         // parsing what the game handed in (its section, bottle shelf, shared health)
    double sectionDumpMs = 0.0;   // serializing the section handed back to a reading game
    double containerDumpMs = 0.0; // serializing the whole container for the file
    double fileWriteMs = 0.0;     // opening and writing the temp file
    double renameMs = 0.0;        // moving the temp file over the container
    size_t sectionBytes = 0;      // the game's section as JSON text, handed in or back
    size_t containerBytes = 0;
};

typedef const ContainerIoStats* (*FnGetContainerIoStats)(void);

inline ContainerIoStats Fetch(FnGetContainerIoStats get) {
    const ContainerIoStats* stats = (get != nullptr) ? get() : nullptr;
    return (stats != nullptr) ? *stats : ContainerIoStats{};
}

inline std::string DescribeRead(const ContainerIoStats& s) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "launcher lock wait %.1f, section dump %.1f, section %zu KB", s.lockWaitMs,
                  s.sectionDumpMs, s.sectionBytes / 1024);
    return buf;
}

inline std::string DescribeWrite(const ContainerIoStats& s) {
    char buf[224];
    std::snprintf(buf, sizeof(buf),
                  "launcher lock wait %.1f, parse %.1f, container dump %.1f, file write %.1f, rename %.1f; section "
                  "%zu KB, container %zu KB",
                  s.lockWaitMs, s.parseMs, s.containerDumpMs, s.fileWriteMs, s.renameMs, s.sectionBytes / 1024,
                  s.containerBytes / 1024);
    return buf;
}

} // namespace ComboSaveTiming
