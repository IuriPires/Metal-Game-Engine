#include "mge/core/log.h"

#include <doctest/doctest.h>

#include <memory>

using mge::core::CapturingSink;
using mge::core::Logger;
using mge::core::LogLevel;
using mge::core::LogCategory;

namespace {

constexpr LogCategory test_cat{"test"};

struct LoggerScope {
    LoggerScope() {
        Logger::get().remove_all_sinks();
        Logger::get().set_global_min_level(LogLevel::Trace);
    }
    ~LoggerScope() {
        Logger::get().remove_all_sinks();
        Logger::get().set_global_min_level(LogLevel::Trace);
    }
};

}  // namespace

TEST_CASE("Logger dispatches to a capturing sink") {
    LoggerScope                    scope;
    auto                           sink = std::make_shared<CapturingSink>();
    Logger::get().add_sink(sink);

    MGE_LOG_INFO(test_cat, "hello {}", 42);
    MGE_LOG_WARN(test_cat, "warn {}", "world");

    CHECK(sink->size() == 2);
    CHECK(sink->records()[0].find("hello 42") != std::string::npos);
    CHECK(sink->records()[1].find("warn world") != std::string::npos);
}

TEST_CASE("Logger filters by global min level") {
    LoggerScope scope;
    auto        sink = std::make_shared<CapturingSink>();
    Logger::get().add_sink(sink);
    Logger::get().set_global_min_level(LogLevel::Warn);

    MGE_LOG_INFO(test_cat, "should be skipped");
    MGE_LOG_ERROR(test_cat, "should land");

    CHECK(sink->size() == 1);
    CHECK(sink->records()[0].find("should land") != std::string::npos);
}

TEST_CASE("Logger filters by category min level") {
    LoggerScope                    scope;
    auto                           sink = std::make_shared<CapturingSink>();
    Logger::get().add_sink(sink);

    constexpr LogCategory chatty{"chatty", LogLevel::Error};
    MGE_LOG_INFO(chatty, "skipped");
    MGE_LOG_ERROR(chatty, "kept");

    CHECK(sink->size() == 1);
}

TEST_CASE("Logger with no sinks short-circuits should_log") {
    LoggerScope scope;
    CHECK_FALSE(Logger::get().should_log(test_cat, LogLevel::Info));
}
