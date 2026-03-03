#pragma once

// Result<T, E> — value-semantic error type for explicit error handling.
//
// Inspired by Rust's Result<T, E>. Forces the caller to inspect the result
// before accessing the value. No exceptions, no null returns, no silent errors.
//
// Storage uses a tagged union — only one of value_ or error_ is live at any
// time. The destructor calls the correct member's destructor based on ok_.
//
// The TRY macro propagates errors up the call stack. It has two implementations:
//   - GCC/Clang: uses GCC statement expressions ({...})
//   - MSVC: uses an immediately-invoked lambda

#include <cassert>
#include <new>
#include <string>
#include <type_traits>
#include <utility>

namespace uz {

// ============================================================================
// Result<T, E>
// ============================================================================

template<typename T, typename E = std::string>
class Result {
    bool ok_;

    // Tagged union — only one member is live at any time.
    union {
        T value_;
        E error_;
    };

    // Private default constructor — callers must use Ok() or Err().
    Result() noexcept : ok_(false) {}

public:
    // -- Factory methods ----------------------------------------------------

    /// Construct a success result.
    [[nodiscard]] static Result Ok(T v) {
        Result r;
        r.ok_ = true;
        new (&r.value_) T(std::move(v));
        return r;
    }

    /// Construct an error result.
    [[nodiscard]] static Result Err(E e) {
        Result r;
        r.ok_ = false;
        new (&r.error_) E(std::move(e));
        return r;
    }

    // -- Copy / Move / Destroy ----------------------------------------------

    Result(const Result& other) : ok_(other.ok_) {
        if (ok_) {
            new (&value_) T(other.value_);
        } else {
            new (&error_) E(other.error_);
        }
    }

    Result(Result&& other) noexcept(
        std::is_nothrow_move_constructible_v<T> &&
        std::is_nothrow_move_constructible_v<E>
    ) : ok_(other.ok_) {
        if (ok_) {
            new (&value_) T(std::move(other.value_));
        } else {
            new (&error_) E(std::move(other.error_));
        }
    }

    Result& operator=(const Result& other) {
        if (this == &other) return *this;
        destroy();
        ok_ = other.ok_;
        if (ok_) {
            new (&value_) T(other.value_);
        } else {
            new (&error_) E(other.error_);
        }
        return *this;
    }

    Result& operator=(Result&& other) noexcept(
        std::is_nothrow_move_constructible_v<T> &&
        std::is_nothrow_move_constructible_v<E>
    ) {
        if (this == &other) return *this;
        destroy();
        ok_ = other.ok_;
        if (ok_) {
            new (&value_) T(std::move(other.value_));
        } else {
            new (&error_) E(std::move(other.error_));
        }
        return *this;
    }

    ~Result() {
        destroy();
    }

    // -- Observers ----------------------------------------------------------

    /// True if the result holds a value.
    [[nodiscard]] bool is_ok()  const noexcept { return ok_; }

    /// True if the result holds an error.
    [[nodiscard]] bool is_err() const noexcept { return !ok_; }

    /// Access the value. UB if is_err(); asserts in debug.
    [[nodiscard]] const T& value() const& {
        assert(ok_ && "Result::value() called on error result");
        return value_;
    }

    [[nodiscard]] T& value() & {
        assert(ok_ && "Result::value() called on error result");
        return value_;
    }

    /// Move the value out. The result should not be used after this call.
    [[nodiscard]] T&& take() && {
        assert(ok_ && "Result::take() called on error result");
        return std::move(value_);
    }

    /// Non-rvalue take() for use in macros where the result is an lvalue.
    [[nodiscard]] T take() & {
        assert(ok_ && "Result::take() called on error result");
        return std::move(value_);
    }

    /// Access the error. UB if is_ok(); asserts in debug.
    [[nodiscard]] const E& error() const& {
        assert(!ok_ && "Result::error() called on ok result");
        return error_;
    }

    [[nodiscard]] E& error() & {
        assert(!ok_ && "Result::error() called on ok result");
        return error_;
    }

    /// Access the value or return a default if error.
    [[nodiscard]] T value_or(T default_value) const& {
        if (ok_) return value_;
        return default_value;
    }

private:
    void destroy() {
        if (ok_) {
            value_.~T();
        } else {
            error_.~E();
        }
    }
};

} // namespace uz

// ============================================================================
// Convenience macros — outside namespace uz so they work everywhere
// ============================================================================

// UZ_TRY_ASSIGN(var, expr)
//
// The portable, statement-level TRY macro. Works on all compilers (MSVC, GCC,
// Clang). Evaluates `expr` (which must return a uz::Result<T,E>). If the result
// is an error, early-returns that error from the enclosing function. Otherwise,
// declares `var` with the unwrapped value.
//
// Usage:
//   uz::Result<int> foo() {
//       UZ_TRY_ASSIGN(val, bar());   // bar() returns Result<int>
//       return uz::Result<int>::Ok(val + 1);
//   }
//
// The enclosing function's return type must be compatible with the error type
// of the expression.

#define UZ_TRY_ASSIGN(var, expr)                                                \
    auto _uz_try_tmp_##var = (expr);                                            \
    if (_uz_try_tmp_##var.is_err())                                             \
        return std::remove_cvref_t<decltype(_uz_try_tmp_##var)>::Err(           \
            _uz_try_tmp_##var.error());                                         \
    auto var = _uz_try_tmp_##var.take()

// UZ_TRY(expr) — expression-level TRY (GCC/Clang only)
//
// On GCC/Clang, this uses statement expressions ({...}) to let UZ_TRY be used
// inline as an expression. On MSVC, statement expressions are not supported, so
// UZ_TRY falls back to UZ_TRY_ASSIGN with a generated variable name. In that
// case, the unwrapped value is stored in a variable named _uz_try_val_<LINE>.
//
// Prefer UZ_TRY_ASSIGN for portable code.

// Helper to generate unique variable names.
#define UZ_CONCAT_IMPL_(a, b) a##b
#define UZ_CONCAT_(a, b) UZ_CONCAT_IMPL_(a, b)

#if defined(__GNUC__) || defined(__clang__)
    #define UZ_TRY(expr)                                                        \
        ({                                                                      \
            auto _uz_result = (expr);                                           \
            if (_uz_result.is_err())                                            \
                return std::remove_cvref_t<decltype(_uz_result)>::Err(          \
                    _uz_result.error());                                        \
            _uz_result.take();                                                  \
        })
#else
    // MSVC fallback — expands to a statement, not an expression. The result is
    // stored in a generated variable. This means UZ_TRY cannot be used as a
    // sub-expression on MSVC (e.g. `foo(UZ_TRY(bar()))` won't work).
    // Use UZ_TRY_ASSIGN instead.
    #define UZ_TRY(expr)                                                        \
        UZ_TRY_ASSIGN(UZ_CONCAT_(_uz_try_val_, __LINE__), expr)
#endif
