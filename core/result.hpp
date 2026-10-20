#pragma once

#include <cassert>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace syspulse {

// Tag struct for explicit error initialization
template <typename E>
struct Err {
    E error;
    explicit Err(E e) : error(std::move(e)) {}
};

template <typename E>
Err<std::decay_t<E>> make_err(E&& e) {
    return Err<std::decay_t<E>>(std::forward<E>(e));
}

// Result<T, E> implementation using std::variant
template <typename T, typename E>
class [[nodiscard]] Result {
public:
    using value_type = T;
    using error_type = E;

    // Success constructors
    template <typename U = T, typename = std::enable_if_t<std::is_constructible_v<T, U>>>
    Result(U&& val) : data_(std::in_place_index<0>, std::forward<U>(val)) {}

    // Explicit error constructor
    template <typename F = E, typename = std::enable_if_t<std::is_constructible_v<E, F>>>
    Result(Err<F> err) : data_(std::in_place_index<1>, std::forward<F>(err.error)) {}

    // Converting constructor from another Result with convertible types
    template <typename U, typename F,
              typename = std::enable_if_t<std::is_constructible_v<T, U> && std::is_constructible_v<E, F>>>
    Result(const Result<U, F>& other) {
        if (other.is_ok()) {
            data_.template emplace<0>(other.value());
        } else {
            data_.template emplace<1>(other.error());
        }
    }

    template <typename U, typename F,
              typename = std::enable_if_t<std::is_constructible_v<T, U> && std::is_constructible_v<E, F>>>
    Result(Result<U, F>&& other) {
        if (other.is_ok()) {
            data_.template emplace<0>(std::move(other).value());
        } else {
            data_.template emplace<1>(std::move(other).error());
        }
    }

    [[nodiscard]] bool is_ok() const noexcept {
        return data_.index() == 0;
    }

    [[nodiscard]] bool is_err() const noexcept {
        return data_.index() == 1;
    }

    explicit operator bool() const noexcept {
        return is_ok();
    }

    [[nodiscard]] const T& value() const& {
        if (is_err()) {
            throw std::runtime_error("Attempted to access value() of an Err Result");
        }
        return std::get<0>(data_);
    }

    [[nodiscard]] T& value() & {
        if (is_err()) {
            throw std::runtime_error("Attempted to access value() of an Err Result");
        }
        return std::get<0>(data_);
    }

    [[nodiscard]] T&& value() && {
        if (is_err()) {
            throw std::runtime_error("Attempted to access value() of an Err Result");
        }
        return std::get<0>(std::move(data_));
    }

    [[nodiscard]] const E& error() const& {
        if (is_ok()) {
            throw std::runtime_error("Attempted to access error() of an Ok Result");
        }
        return std::get<1>(data_);
    }

    [[nodiscard]] E& error() & {
        if (is_ok()) {
            throw std::runtime_error("Attempted to access error() of an Ok Result");
        }
        return std::get<1>(data_);
    }

    [[nodiscard]] E&& error() && {
        if (is_ok()) {
            throw std::runtime_error("Attempted to access error() of an Ok Result");
        }
        return std::get<1>(std::move(data_));
    }

    [[nodiscard]] T unwrap() {
        return std::move(value());
    }

    [[nodiscard]] T unwrap_or(T default_value) const {
        if (is_ok()) {
            return std::get<0>(data_);
        }
        return default_value;
    }

    template <typename F>
    auto map(F&& f) const -> Result<decltype(f(std::declval<T>())), E> {
        using NewT = decltype(f(std::declval<T>()));
        if (is_ok()) {
            return Result<NewT, E>(f(std::get<0>(data_)));
        }
        return Result<NewT, E>(Err<E>(std::get<1>(data_)));
    }

private:
    std::variant<T, E> data_;
};

// Result<void, E> partial specialization
template <typename E>
class [[nodiscard]] Result<void, E> {
public:
    using value_type = void;
    using error_type = E;

    // Success constructors
    Result() : data_(std::monostate{}) {}

    // Explicit error constructor
    template <typename F = E, typename = std::enable_if_t<std::is_constructible_v<E, F>>>
    Result(Err<F> err) : data_(std::forward<F>(err.error)) {}

    // Converting constructor from another Result<void, F>
    template <typename F, typename = std::enable_if_t<std::is_constructible_v<E, F>>>
    Result(const Result<void, F>& other) {
        if (other.is_ok()) {
            data_ = std::monostate{};
        } else {
            data_ = other.error();
        }
    }

    template <typename F, typename = std::enable_if_t<std::is_constructible_v<E, F>>>
    Result(Result<void, F>&& other) {
        if (other.is_ok()) {
            data_ = std::monostate{};
        } else {
            data_ = std::move(other).error();
        }
    }

    [[nodiscard]] bool is_ok() const noexcept {
        return data_.index() == 0;
    }

    [[nodiscard]] bool is_err() const noexcept {
        return data_.index() == 1;
    }

    explicit operator bool() const noexcept {
        return is_ok();
    }

    void value() const {
        if (is_err()) {
            throw std::runtime_error("Attempted to access value() of an Err Result");
        }
    }

    [[nodiscard]] const E& error() const& {
        if (is_ok()) {
            throw std::runtime_error("Attempted to access error() of an Ok Result");
        }
        return std::get<1>(data_);
    }

    [[nodiscard]] E& error() & {
        if (is_ok()) {
            throw std::runtime_error("Attempted to access error() of an Ok Result");
        }
        return std::get<1>(data_);
    }

    [[nodiscard]] E&& error() && {
        if (is_ok()) {
            throw std::runtime_error("Attempted to access error() of an Ok Result");
        }
        return std::get<1>(std::move(data_));
    }

    void unwrap() const {
        value();
    }

private:
    std::variant<std::monostate, E> data_;
};

// Convenient factory functions
template <typename T>
Result<std::decay_t<T>, std::string> Ok(T&& val) {
    return Result<std::decay_t<T>, std::string>(std::forward<T>(val));
}

inline Result<void, std::string> Ok() {
    return Result<void, std::string>();
}

template <typename T = void, typename E>
Result<T, std::decay_t<E>> Error(E&& err) {
    return Result<T, std::decay_t<E>>(Err<std::decay_t<E>>(std::forward<E>(err)));
}

} // namespace syspulse
