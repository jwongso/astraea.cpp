#pragma once
//
// Per-request timing instrumentation, compile-time switchable.
//
// Enable by building with -DASTRAEA_ENABLE_TIMING (CMake option
// ASTRAEA_ENABLE_TIMING=ON sets this on the nz_tenancy target).
//
// When disabled every method is a zero-overhead inline no-op - the compiler
// eliminates all timing call sites entirely with -O1 or higher.
//
// Usage:
//   TimingCollector t;
//   auto t0 = t.now();
//   // ... work ...
//   t.record("step_name", t0);        // records ms since t0
//   t.record_ms("step_name", 42.5);   // records a pre-computed value
//   double total = t.elapsed_ms();    // ms since construction
//   auto& steps  = t.steps();         // const ref to all recorded steps
//
#include <chrono>
#include <string>
#include <string_view>
#include <vector>

namespace astraea {

/// @brief A single named timing measurement captured by TimingCollector.
struct TimingStep {
    std::string name; ///< Human-readable step name matching a Python core/timing.py aggregate slot.
    double      ms = 0.0; ///< Duration of this step in milliseconds.
};

#ifdef ASTRAEA_ENABLE_TIMING

/// @brief Per-request timing collector that records named steps since a start time.
///
/// Enabled at compile time with -DASTRAEA_ENABLE_TIMING. When disabled, the
/// stub class below replaces every method with an inline no-op so the compiler
/// eliminates all call sites at -O1 or higher with zero overhead.
class TimingCollector {
public:
    using Clock = std::chrono::steady_clock;
    using TP    = Clock::time_point;

    TimingCollector() noexcept : _start(Clock::now()) {}

    /// @brief Return the current time point for use with record().
    TP now() const noexcept { return Clock::now(); }

    /// @brief Record a step as the elapsed time since `since`.
    void record(std::string name, TP since) {
        using namespace std::chrono;
        const double ms = duration_cast<microseconds>(Clock::now() - since).count() / 1000.0;
        _steps.push_back({std::move(name), ms});
    }

    /// @brief Record a step from a pre-computed millisecond value.
    void record_ms(std::string name, double ms) {
        _steps.push_back({std::move(name), ms});
    }

    /// @brief Total milliseconds elapsed since construction.
    double elapsed_ms() const noexcept {
        using namespace std::chrono;
        return duration_cast<microseconds>(Clock::now() - _start).count() / 1000.0;
    }

    /// @brief Read-only access to all recorded steps in insertion order.
    const std::vector<TimingStep>& steps() const noexcept { return _steps; }

    /// @brief Sum all recorded steps whose name matches any entry in `names`.
    double agg(std::initializer_list<std::string_view> names) const noexcept {
        double total = 0.0;
        for (const auto& s : _steps)
            for (auto n : names)
                if (s.name == n) { total += s.ms; break; }
        return total;
    }

private:
    TP                      _start;
    std::vector<TimingStep> _steps;
};

#else // !ASTRAEA_ENABLE_TIMING

// Zero-overhead stub. All methods are empty inline functions; the compiler
// elides every call site. TimingStep is still defined so TimingEvent
// (the SSE serialisation struct) compiles with an empty detail vector.
class TimingCollector {
public:
    using Clock = std::chrono::steady_clock;
    using TP    = Clock::time_point;

    TP   now()                                              const noexcept { return {}; }
    void record(std::string, TP)                                  noexcept {}
    void record_ms(std::string, double)                           noexcept {}
    double elapsed_ms()                                     const noexcept { return 0.0; }
    const std::vector<TimingStep>& steps()                  const noexcept { return _empty; }
    double agg(std::initializer_list<std::string_view>)     const noexcept { return 0.0; }
private:
    inline static const std::vector<TimingStep> _empty{};
};

#endif // ASTRAEA_ENABLE_TIMING

} // namespace astraea
