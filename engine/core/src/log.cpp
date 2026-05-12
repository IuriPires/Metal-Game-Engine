#include "mge/core/log.h"

#include <cstdio>
#include <fmt/color.h>
#include <fmt/format.h>
#include <unistd.h>

namespace mge::core {

std::string_view to_string(LogLevel l) noexcept {
    switch (l) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
        case LogLevel::Off:   return "OFF";
    }
    return "?";
}

namespace {

class ConsoleSink final : public LogSink {
public:
    ConsoleSink() : color_(::isatty(fileno(stderr)) != 0) {}

    void write(const LogRecord& r) override {
        std::lock_guard<std::mutex> lock(mu_);
        FILE*                       stream =
            (r.level >= LogLevel::Warn) ? stderr : stdout;
        if (color_) {
            const auto       lvl = to_string(r.level);
            fmt::text_style  style;
            switch (r.level) {
                case LogLevel::Trace: style = fmt::fg(fmt::color::gray); break;
                case LogLevel::Debug: style = fmt::fg(fmt::color::cyan); break;
                case LogLevel::Info:  style = fmt::fg(fmt::color::white); break;
                case LogLevel::Warn:  style = fmt::fg(fmt::color::orange); break;
                case LogLevel::Error: style = fmt::fg(fmt::color::red); break;
                case LogLevel::Fatal:
                    style = fmt::bg(fmt::color::red) | fmt::fg(fmt::color::white);
                    break;
                default: break;
            }
            fmt::print(stream, style, "[{:5}]", lvl);
            fmt::print(stream, " [{}] {}\n", r.category, r.message);
        } else {
            fmt::print(stream, "[{:5}] [{}] {}\n", to_string(r.level), r.category, r.message);
        }
        std::fflush(stream);
    }

private:
    std::mutex mu_;
    bool       color_;
};

}  // namespace

std::unique_ptr<LogSink> make_console_sink() {
    return std::make_unique<ConsoleSink>();
}

CapturingSink::CapturingSink(std::size_t capacity) : capacity_(capacity) {
    records_.reserve(capacity);
}

void CapturingSink::write(const LogRecord& rec) {
    std::lock_guard<std::mutex> lock(mu_);
    if (records_.size() >= capacity_) {
        records_.erase(records_.begin());
    }
    records_.emplace_back(fmt::format("[{}] [{}] {}", to_string(rec.level), rec.category,
                                      rec.message));
}

std::vector<std::string>& CapturingSink::records() {
    return records_;
}

std::size_t CapturingSink::size() const {
    std::lock_guard<std::mutex> lock(mu_);
    return records_.size();
}

void CapturingSink::clear() {
    std::lock_guard<std::mutex> lock(mu_);
    records_.clear();
}

Logger& Logger::get() {
    static Logger instance;
    return instance;
}

void Logger::add_sink(std::shared_ptr<LogSink> sink) {
    std::lock_guard<std::mutex> lock(mu_);
    sinks_.push_back(std::move(sink));
}

void Logger::remove_all_sinks() {
    std::lock_guard<std::mutex> lock(mu_);
    sinks_.clear();
}

void Logger::set_global_min_level(LogLevel l) noexcept {
    std::lock_guard<std::mutex> lock(mu_);
    global_min_ = l;
}

LogLevel Logger::global_min_level() const noexcept {
    std::lock_guard<std::mutex> lock(mu_);
    return global_min_;
}

bool Logger::should_log(const LogCategory& cat, LogLevel l) const noexcept {
    std::lock_guard<std::mutex> lock(mu_);
    if (l == LogLevel::Off) {
        return false;
    }
    if (static_cast<std::uint8_t>(l) < static_cast<std::uint8_t>(global_min_)) {
        return false;
    }
    if (static_cast<std::uint8_t>(l) < static_cast<std::uint8_t>(cat.min_level)) {
        return false;
    }
    return !sinks_.empty();
}

void Logger::dispatch(LogLevel level, const LogCategory& cat, const char* file, int line,
                      const char* function, std::string_view msg) {
    LogRecord r{level, cat.name, msg, file, line, function};
    std::vector<std::shared_ptr<LogSink>> snapshot;
    {
        std::lock_guard<std::mutex> lock(mu_);
        snapshot = sinks_;
    }
    for (auto& s : snapshot) {
        s->write(r);
    }
}

}  // namespace mge::core
