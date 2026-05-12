#include "mge/core/time.h"

#include <algorithm>
#include <limits>

namespace mge::core {

void FrameStats::push(double frame_seconds) noexcept {
    last_ = frame_seconds;
    samples_[head_] = frame_seconds;
    head_ = (head_ + 1) % window_size;
    if (count_ < window_size) {
        ++count_;
    }
}

double FrameStats::avg_seconds() const noexcept {
    if (count_ == 0) {
        return 0.0;
    }
    double sum = 0.0;
    for (std::size_t i = 0; i < count_; ++i) {
        sum += samples_[i];
    }
    return sum / static_cast<double>(count_);
}

double FrameStats::min_seconds() const noexcept {
    if (count_ == 0) {
        return 0.0;
    }
    double m = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < count_; ++i) {
        m = std::min(m, samples_[i]);
    }
    return m;
}

double FrameStats::max_seconds() const noexcept {
    double m = 0.0;
    for (std::size_t i = 0; i < count_; ++i) {
        m = std::max(m, samples_[i]);
    }
    return m;
}

}  // namespace mge::core
