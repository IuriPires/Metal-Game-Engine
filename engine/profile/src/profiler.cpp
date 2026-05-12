#include "mge/profile/profiler.h"

#include <algorithm>
#include <limits>

namespace mge::profile {

Profiler& Profiler::get() {
    static Profiler instance;
    return instance;
}

Profiler::Zone& Profiler::zone_for(std::string_view name) {
    // Linear scan is fine — Phase 1 has < 20 zones in practice.
    for (auto& z : zones_) {
        if (z.name == name) return z;
    }
    zones_.push_back(Zone{name, {}, 0, 0, core::now(), 0, 0.0});
    return zones_.back();
}

void Profiler::begin_zone(std::string_view name) {
    std::lock_guard<std::mutex> lock(mu_);
    Zone& z = zone_for(name);
    if (z.depth == 0) {
        z.last_begin = core::now();
    }
    ++z.depth;
}

void Profiler::end_zone(std::string_view name) {
    std::lock_guard<std::mutex> lock(mu_);
    Zone& z = zone_for(name);
    if (z.depth <= 0) return;  // unbalanced end; ignore
    --z.depth;
    if (z.depth != 0) return;  // still nested

    const double ms = core::milliseconds(core::now() - z.last_begin);
    z.last_ms      = ms;
    z.samples[z.head] = ms;
    z.head            = (z.head + 1) % window_size;
    if (z.count < window_size) ++z.count;
}

std::vector<ZoneStats> Profiler::snapshot() const {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<ZoneStats> out;
    out.reserve(zones_.size());
    for (const auto& z : zones_) {
        ZoneStats s;
        s.name    = z.name;
        s.last_ms = z.last_ms;
        s.samples = z.count;
        if (z.count == 0) {
            out.push_back(s);
            continue;
        }
        double sum = 0.0;
        double mn  = std::numeric_limits<double>::infinity();
        double mx  = 0.0;
        for (std::size_t i = 0; i < z.count; ++i) {
            const double v = z.samples[i];
            sum += v;
            if (v < mn) mn = v;
            if (v > mx) mx = v;
        }
        s.avg_ms = sum / static_cast<double>(z.count);
        s.min_ms = mn;
        s.max_ms = mx;
        out.push_back(s);
    }
    return out;
}

void Profiler::reset() {
    std::lock_guard<std::mutex> lock(mu_);
    zones_.clear();
}

}  // namespace mge::profile
