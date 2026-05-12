#include "mge/core/result.h"

#include <doctest/doctest.h>

#include <string>

using mge::core::Err;
using mge::core::Ok;
using mge::core::Result;

namespace {

Result<int, std::string> parse(const std::string& s) {
    if (s.empty()) {
        return Err<std::string>("empty");
    }
    return Ok(static_cast<int>(s.size()));
}

}  // namespace

TEST_CASE("Result ok carries value") {
    auto r = parse("hello");
    REQUIRE(r.is_ok());
    REQUIRE_FALSE(r.is_err());
    CHECK(static_cast<bool>(r));
    CHECK(r.value() == 5);
}

TEST_CASE("Result err carries error") {
    auto r = parse("");
    REQUIRE(r.is_err());
    REQUIRE_FALSE(r.is_ok());
    CHECK_FALSE(static_cast<bool>(r));
    CHECK(r.error() == "empty");
}

TEST_CASE("Result value_or returns fallback on err") {
    auto r = parse("");
    CHECK(r.value_or(-1) == -1);
}

TEST_CASE("Result rvalue value moves out") {
    auto                  r = parse("abc");
    Result<int, std::string> r2 = std::move(r);
    CHECK(r2.value() == 3);
}
