#pragma once

#include <chrono>
#include <cstdint>

namespace mge::core {

// Strong types over std::chrono for engine clocks. Monotonic on Apple via
// std::chrono::steady_clock (which wraps mach_absolute_time).
using Clock     = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Duration  = Clock::duration;

[[nodiscard]] inline TimePoint now() noexcept {
    return Clock::now();
}

[[nodiscard]] inline double seconds(Duration d) noexcept {
    return std::chrono::duration<double>(d).count();
}

[[nodiscard]] inline double milliseconds(Duration d) noexcept {
    return std::chrono::duration<double, std::milli>(d).count();
}

[[nodiscard]] inline double microseconds(Duration d) noexcept {
    return std::chrono::duration<double, std::micro>(d).count();
}

// Rolling frame stats over a fixed-size window. Used by the headed demo
// and (later) by the on-screen profiler overlay. No allocations.
class FrameStats {
public:
    static constexpr std::size_t window_size = 120;

    void push(double frame_seconds) noexcept;

    [[nodiscard]] double avg_seconds() const noexcept;
    [[nodiscard]] double min_seconds() const noexcept;
    [[nodiscard]] double max_seconds() const noexcept;
    [[nodiscard]] double last_seconds() const noexcept { return last_; }
    [[nodiscard]] std::size_t count() const noexcept { return count_; }

private:
    double samples_[window_size] = {};
    std::size_t head_  = 0;
    std::size_t count_ = 0;
    double last_       = 0.0;
};

}  // namespace mge::core
