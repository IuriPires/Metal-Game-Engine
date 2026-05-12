#pragma once

#include "mge/core/time.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <string_view>
#include <vector>

namespace mge::profile {

// Per-zone rolling-window statistics. The window is fixed-size; older samples
// fall off. Used to compute avg/min/max for the overlay each frame.
struct ZoneStats {
    std::string_view name;
    double           last_ms = 0.0;
    double           avg_ms  = 0.0;
    double           min_ms  = 0.0;
    double           max_ms  = 0.0;
    std::size_t      samples = 0;
};

// Thread-safe CPU profiler. Phase 1 v1 is intentionally simple: one
// rolling window per unique zone name. No hierarchical zones, no GPU,
// no flame graphs.
class Profiler {
public:
    static constexpr std::size_t window_size = 120;

    [[nodiscard]] static Profiler& get();

    // Open/close a zone. Names are taken by string_view; the caller must
    // keep the storage alive for the lifetime of the program (literal
    // string OK).
    void begin_zone(std::string_view name);
    void end_zone(std::string_view name);

    // Snapshot of all zones' rolling stats for the overlay / logging.
    [[nodiscard]] std::vector<ZoneStats> snapshot() const;

    // Reset all zone data (useful between scenes / replays).
    void reset();

private:
    struct Zone {
        std::string_view              name;
        std::array<double, window_size> samples{};
        std::size_t                   head        = 0;
        std::size_t                   count       = 0;
        core::TimePoint               last_begin;
        int                           depth       = 0;
        double                        last_ms     = 0.0;
    };

    Zone& zone_for(std::string_view name);

    mutable std::mutex mu_;
    std::vector<Zone>  zones_;
};

// RAII scope timer. Usage:
//   {
//       MGE_PROFILE_ZONE("fill_instances");
//       ...
//   }
class ScopeTimer {
public:
    explicit ScopeTimer(std::string_view name) noexcept : name_(name) {
        Profiler::get().begin_zone(name_);
    }
    ~ScopeTimer() { Profiler::get().end_zone(name_); }

    ScopeTimer(const ScopeTimer&)            = delete;
    ScopeTimer& operator=(const ScopeTimer&) = delete;

private:
    std::string_view name_;
};

}  // namespace mge::profile

#define MGE_PROFILE_TOKEN_PASTE_INNER(a, b) a##b
#define MGE_PROFILE_TOKEN_PASTE(a, b)       MGE_PROFILE_TOKEN_PASTE_INNER(a, b)

#define MGE_PROFILE_ZONE(name)                                                 \
    ::mge::profile::ScopeTimer MGE_PROFILE_TOKEN_PASTE(_mge_zone_, __LINE__)(name)
