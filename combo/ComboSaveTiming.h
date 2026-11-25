#pragma once

// combo/ComboSaveTiming.h — ComboShip: opt-in timing of the combo save path, so a log can show what the hitch after a
// pickup is made of. Off by default; turn it on with `set gCombo.Debug.SaveTiming 1` in the console. A save write as
// slow as kSlowWriteMs, and any failed container write, is logged even with it off. Each game DLL logs the lines itself
// (the launcher has no log file); the launcher only measures its share, see ContainerIoStats and
// ContainerWriterReport.

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <string>

namespace ComboSaveTiming {

inline constexpr const char* kCVar = "gCombo.Debug.SaveTiming";

// Kill switch for the save writer thread (combo/ComboContainerWriter.h): 1 writes each container on the thread that
// saved, as before 0.1.3. The game in play hands its value to the launcher every frame.
inline constexpr const char* kSyncWritesCVar = "gCombo.Debug.SyncSaveWrites";

// A per-frame call is only worth a log line when it did real work; an idle one takes microseconds.
inline constexpr double kSlowCallMs = 1.0;

// A game frame normally ends 50 ms after the previous one (both games update at 20 Hz). Gaps of 2 s or more are
// switches between the games or a parked game, not hitches, unless the frame's saves took most of it.
inline constexpr double kFrameHitchMs = 100.0;
inline constexpr double kFrameHitchMaxMs = 2000.0;

// A save write normally takes 50-130 ms on the Steam Deck's SD card; one this slow is a freeze the player sees (or,
// on the save writer thread, a change that long at risk of a crash).
inline constexpr double kSlowWriteMs = 250.0;

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
    // The file itself, when it was written on the thread that saved (syncWrite); otherwise the save writer thread
    // wrote it later and ContainerWriterReport has these.
    bool syncWrite = false;
    double openMs = 0.0;      // making sure the save folder exists and creating the temp file
    double fileWriteMs = 0.0; // writing and closing the temp file
    double renameMs = 0.0;    // moving the temp file over the container
    // When the save writer thread took the file: snapshots waiting or being written right after the hand-over, and
    // whether this one replaced an older snapshot of the slot that was still waiting.
    int queuedWrites = 0;
    bool replacedUnwritten = false;
    size_t sectionBytes = 0; // the game's section as JSON text, handed in or back
    size_t containerBytes = 0;
    // The kernel's whole-system dirty and under-writeback page cache, sampled after a slow write (Linux only). Lots
    // dirty means writes were being throttled; lots under writeback with little dirty means the disk itself was slow.
    bool kernelSampled = false;
    unsigned long dirtyKb = 0;
    unsigned long writebackKb = 0;
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

inline void SampleKernelWriteback(ContainerIoStats& s) {
#ifdef __linux__
    std::FILE* f = std::fopen("/proc/meminfo", "r");
    if (f == nullptr) {
        return;
    }
    char line[128];
    while (std::fgets(line, sizeof(line), f) != nullptr) {
        unsigned long kb = 0;
        if (std::sscanf(line, "Dirty: %lu kB", &kb) == 1) {
            s.dirtyKb = kb;
        } else if (std::sscanf(line, "Writeback: %lu kB", &kb) == 1) {
            s.writebackKb = kb;
        }
    }
    std::fclose(f);
    s.kernelSampled = true;
#else
    (void)s;
#endif
}

inline std::string DescribeKernel(const ContainerIoStats& s) {
    if (!s.kernelSampled) {
        return "";
    }
    char buf[80];
    std::snprintf(buf, sizeof(buf), "; kernel dirty %lu KB, writeback %lu KB", s.dirtyKb, s.writebackKb);
    return buf;
}

inline std::string DescribeFile(const ContainerIoStats& s) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "open %.1f, file write %.1f, rename %.1f", s.openMs, s.fileWriteMs, s.renameMs);
    return buf;
}

inline std::string DescribeWrite(const ContainerIoStats& s) {
    char file[128];
    if (s.syncWrite) {
        std::snprintf(file, sizeof(file), "%s", DescribeFile(s).c_str());
    } else {
        std::snprintf(file, sizeof(file), "handed to the save writer with %d queued%s", s.queuedWrites,
                      s.replacedUnwritten ? ", replacing an unwritten snapshot" : "");
    }
    char buf[384];
    std::snprintf(buf, sizeof(buf),
                  "launcher lock wait %.1f, parse %.1f, container dump %.1f, %s; section %zu KB, container %zu KB%s",
                  s.lockWaitMs, s.parseMs, s.containerDumpMs, file, s.sectionBytes / 1024, s.containerBytes / 1024,
                  DescribeKernel(s).c_str());
    return buf;
}

// What the launcher's save writer thread did since the game in play last asked (once a frame). Plain data: it crosses
// the exe/DLL boundary.
struct ContainerWriterReport {
    int written = 0;   // container files the thread finished, including failed ones
    int failed = 0;    // container writes (or erases) that failed, on the thread or by the caller (lastError: the last)
    int replaced = 0;  // snapshots dropped unwritten: a newer snapshot or an erase of the same slot came first
    int maxQueued = 0; // most snapshots waiting or being written right after one hand-over
    // The finished write whose oldest change waited longest to be on disk, from its hand-over (or the hand-over of the
    // oldest snapshot it replaced) to its rename. That wait is how long a crash could have lost the change.
    double maxHandoverToDiskMs = 0.0;
    int slowestSlot = -1;
    double slowestWaitMs = 0.0;   // of that wait, the time before the thread started on it
    ContainerIoStats slowestFile; // its open, file write, rename, size and kernel sample
    char lastError[192] = "";
    // Fallbacks around the container files (ComboContainerWriter.h), on the thread or by the caller: writes whose
    // rename over the container had to be retried, those of them that then removed the container first, and
    // containers recovered from the temp file such a write left (lastNotice: what the last of these was).
    int renameRetried = 0;
    int removedFirst = 0;
    int recovered = 0;
    char lastNotice[192] = "";
};

// The launcher's per-frame entry, pushed into each game: takes the game's kSyncWritesCVar and fills the report.
typedef void (*FnContainerWriterFrame)(int syncWrites, ContainerWriterReport* out);

enum class ReportLevel { kNone, kInfo, kWarn, kError };

inline ReportLevel WriterReportLevel(const ContainerWriterReport& r, bool timingOn) {
    if (r.failed > 0) {
        return ReportLevel::kError;
    }
    if ((r.maxHandoverToDiskMs >= kSlowWriteMs) || (r.renameRetried > 0) || (r.recovered > 0)) {
        return ReportLevel::kWarn;
    }
    if (timingOn && ((r.written > 0) || (r.replaced > 0))) {
        return ReportLevel::kInfo;
    }
    return ReportLevel::kNone;
}

inline std::string DescribeWriterReport(const ContainerWriterReport& r) {
    char slowest[256] = "";
    if (r.slowestSlot >= 0) {
        std::snprintf(slowest, sizeof(slowest),
                      "; slowest: slot %d on disk %.1f ms after hand-over (waited %.1f, %s; container %zu KB%s)",
                      r.slowestSlot, r.maxHandoverToDiskMs, r.slowestWaitMs, DescribeFile(r.slowestFile).c_str(),
                      r.slowestFile.containerBytes / 1024, DescribeKernel(r.slowestFile).c_str());
    }
    char failed[256] = "";
    if (r.failed > 0) {
        std::snprintf(failed, sizeof(failed), "; %d FAILED, last: %s", r.failed, r.lastError);
    }
    char fallbacks[320] = "";
    if ((r.renameRetried > 0) || (r.recovered > 0)) {
        std::snprintf(fallbacks, sizeof(fallbacks),
                      "; %d rename(s) retried (%d removing the container first), %d recovered from the temp file, "
                      "last: %s",
                      r.renameRetried, r.removedFirst, r.recovered, r.lastNotice);
    }
    char buf[960];
    std::snprintf(buf, sizeof(buf), "save writer: %d written, %d replaced unwritten, at most %d queued%s%s%s",
                  r.written, r.replaced, r.maxQueued, slowest, failed, fallbacks);
    return buf;
}

} // namespace ComboSaveTiming
