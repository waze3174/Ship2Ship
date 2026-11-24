#pragma once

// combo/ComboSaveTiming.h — ComboShip: opt-in timing of the combo save path, so a log can show what the hitch after a
// pickup is made of. Off by default; turn it on with `set gCombo.Debug.SaveTiming 1` in the console. Each game DLL
// logs the lines itself (the launcher has no log file).

#include <chrono>

namespace ComboSaveTiming {

inline constexpr const char* kCVar = "gCombo.Debug.SaveTiming";

// A per-frame call is only worth a log line when it did real work; an idle one takes microseconds.
inline constexpr double kSlowCallMs = 1.0;

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

} // namespace ComboSaveTiming
