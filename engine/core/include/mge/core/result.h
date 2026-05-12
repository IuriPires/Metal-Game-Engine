#pragma once

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>
#include <variant>

namespace mge::core {

// Tagged sum: Ok(T) | Err(E). Engine-wide return type for fallible APIs that
// do not throw exceptions. Modeled after std::expected (C++23) but kept
// minimal so we can ship under C++20.
//
// Use:
//   Result<Pipeline, RhiError> create_pipeline(...);
//   if (auto r = create_pipeline(...); r.is_ok()) { use(r.value()); }
//   else { log_error("{}", r.error()); }
//
// T and E must be distinct types.

namespace detail {
template <class T>
struct OkTag {
    T value;
};
template <class E>
struct ErrTag {
    E value;
};
}  // namespace detail

template <class T>
[[nodiscard]] constexpr detail::OkTag<std::decay_t<T>> Ok(T&& v) {
    return {std::forward<T>(v)};
}

template <class E>
[[nodiscard]] constexpr detail::ErrTag<std::decay_t<E>> Err(E&& e) {
    return {std::forward<E>(e)};
}

template <class T, class E>
class Result {
    static_assert(!std::is_same_v<T, E>, "Result<T, E> requires T and E to differ");

public:
    using value_type = T;
    using error_type = E;

    constexpr Result(detail::OkTag<T> ok) : storage_(std::in_place_index<0>, std::move(ok.value)) {}

    constexpr Result(detail::ErrTag<E> err)
        : storage_(std::in_place_index<1>, std::move(err.value)) {}

    [[nodiscard]] constexpr bool is_ok() const noexcept { return storage_.index() == 0; }
    [[nodiscard]] constexpr bool is_err() const noexcept { return storage_.index() == 1; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return is_ok(); }

    [[nodiscard]] constexpr const T& value() const& { return std::get<0>(storage_); }
    [[nodiscard]] constexpr T&        value() & { return std::get<0>(storage_); }
    [[nodiscard]] constexpr T&&       value() && { return std::move(std::get<0>(storage_)); }

    [[nodiscard]] constexpr const E& error() const& { return std::get<1>(storage_); }
    [[nodiscard]] constexpr E&        error() & { return std::get<1>(storage_); }
    [[nodiscard]] constexpr E&&       error() && { return std::move(std::get<1>(storage_)); }

    [[nodiscard]] constexpr T value_or(T fallback) const& {
        return is_ok() ? value() : std::move(fallback);
    }

private:
    std::variant<T, E> storage_;
};

}  // namespace mge::core
