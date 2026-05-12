#pragma once

#include <cstdint>
#include <fmt/format.h>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace mge::core {

enum class LogLevel : std::uint8_t {
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
    Off,
};

[[nodiscard]] std::string_view to_string(LogLevel l) noexcept;

// A LogCategory identifies a subsystem. Define one per static-storage scope:
//   namespace mge::renderer { inline constexpr LogCategory log_cat{"renderer"}; }
struct LogCategory {
    std::string_view name;
    LogLevel         min_level = LogLevel::Trace;
};

struct LogRecord {
    LogLevel         level;
    std::string_view category;
    std::string_view message;  // borrowed; valid only inside dispatch
    const char*      file;
    int              line;
    const char*      function;
};

// Sinks consume records. Implementations must be thread-safe across calls.
class LogSink {
public:
    virtual ~LogSink()                       = default;
    virtual void write(const LogRecord& rec) = 0;
};

// Built-in sink: stdout/stderr with ANSI color when isatty.
[[nodiscard]] std::unique_ptr<LogSink> make_console_sink();

// In-memory ring sink for tests. Captures the last N messages.
class CapturingSink final : public LogSink {
public:
    explicit CapturingSink(std::size_t capacity = 64);
    void                                   write(const LogRecord& rec) override;
    [[nodiscard]] std::vector<std::string>& records();
    [[nodiscard]] std::size_t              size() const;
    void                                    clear();

private:
    mutable std::mutex       mu_;
    std::vector<std::string> records_;
    std::size_t              capacity_;
};

// Global logger - simple sink list behind a mutex. Phase 1 priority: correct
// and easy to extend; performance work happens when profiling tells us to.
class Logger {
public:
    static Logger& get();

    void add_sink(std::shared_ptr<LogSink> sink);
    void remove_all_sinks();
    void set_global_min_level(LogLevel l) noexcept;
    [[nodiscard]] LogLevel global_min_level() const noexcept;

    bool should_log(const LogCategory& cat, LogLevel l) const noexcept;

    void dispatch(LogLevel level, const LogCategory& cat, const char* file, int line,
                  const char* function, std::string_view msg);

private:
    mutable std::mutex                    mu_;
    std::vector<std::shared_ptr<LogSink>> sinks_;
    LogLevel                              global_min_ = LogLevel::Trace;
};

// Format helper used by the macros. Keeps templated formatting out of the
// header consumers when possible.
template <class... Args>
void log_format(LogLevel level, const LogCategory& cat, const char* file, int line,
                const char* function, fmt::format_string<Args...> fs, Args&&... args) {
    auto& logger = Logger::get();
    if (!logger.should_log(cat, level)) {
        return;
    }
    std::string msg = fmt::format(fs, std::forward<Args>(args)...);
    logger.dispatch(level, cat, file, line, function, msg);
}

}  // namespace mge::core

// ---------------- Macros ----------------

#define MGE_LOG(level, cat, ...)                                                              \
    ::mge::core::log_format((level), (cat), __FILE__, __LINE__, __func__, __VA_ARGS__)

#define MGE_LOG_TRACE(cat, ...) MGE_LOG(::mge::core::LogLevel::Trace, cat, __VA_ARGS__)
#define MGE_LOG_DEBUG(cat, ...) MGE_LOG(::mge::core::LogLevel::Debug, cat, __VA_ARGS__)
#define MGE_LOG_INFO(cat, ...)  MGE_LOG(::mge::core::LogLevel::Info,  cat, __VA_ARGS__)
#define MGE_LOG_WARN(cat, ...)  MGE_LOG(::mge::core::LogLevel::Warn,  cat, __VA_ARGS__)
#define MGE_LOG_ERROR(cat, ...) MGE_LOG(::mge::core::LogLevel::Error, cat, __VA_ARGS__)
#define MGE_LOG_FATAL(cat, ...) MGE_LOG(::mge::core::LogLevel::Fatal, cat, __VA_ARGS__)
