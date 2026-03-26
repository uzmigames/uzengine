#pragma once

/// UzEngine Assertion System
///
/// Provides three assertion types inspired by Unreal Engine 5's philosophy:
/// - **UZ_CHECK**: Verify invariants (fatal in debug, stripped in shipping)
/// - **UZ_ENSURE**: Handle recoverable conditions (logs + continues)
/// - **UZ_VERIFY**: Must execute side effects (never stripped)
///
/// # Shipping Builds
/// UZ_CHECK and UZ_ENSURE are compiled out in UZ_SHIPPING builds.
/// UZ_VERIFY is always executed (because it may have side effects).
///
/// # Example
/// ```cpp
/// UZ_CHECK(device != nullptr);  // Fatal if null in debug
/// if (!UZ_ENSURE(buffer.size() > 0)) return;  // Log, continue
/// UZ_VERIFY(file.close());  // Always runs, checks result
/// ```

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace uz::detail {

/// Reports a recoverable assertion failure (used by UZ_ENSURE).
///
/// # Arguments
/// * `expr` - The failed expression as a string (from macro)
/// * `file` - Source file name (__FILE__)
/// * `line` - Source line number (__LINE__)
/// * `msg` - Optional formatted message or nullptr
///
/// # Returns
/// Always returns false (the condition was false).
/// Caller uses this to continue execution after logging.
[[nodiscard]] bool AssertionFailed(const char* expr, const char* file, int line, const char* msg);

/// Reports a fatal assertion failure (used by UZ_CHECK and UZ_VERIFY).
///
/// # Arguments
/// * `expr` - The failed expression as a string (from macro)
/// * `file` - Source file name (__FILE__)
/// * `line` - Source line number (__LINE__)
/// * `msg` - Optional formatted message or nullptr
///
/// # Behavior
/// - Logs full context (expression, file, line, custom message)
/// - Logs a stack trace (if available)
/// - Calls std::abort() — NEVER RETURNS
[[noreturn]] void FatalAssert(const char* expr, const char* file, int line, const char* msg);

} // namespace uz::detail

// ── UZ_CHECK — fatal in debug, stripped in shipping ─────────────────────────

/// Verifies an invariant condition. Fatal in debug builds, stripped in shipping.
///
/// # Arguments
/// * `expr` - Boolean expression to verify
///
/// # Behavior
/// - **Debug/Development**: Evaluates `expr`; if false, prints diagnostics + stack trace, aborts.
/// - **Shipping (UZ_SHIPPING)**: Entire macro is `((void)0)` — zero cost.
///
/// # Panics
/// Calls `std::abort()` via `FatalAssert` when `expr` is false (non-shipping builds).
///
/// # Examples
/// ```cpp
/// UZ_CHECK(device != nullptr);
/// UZ_CHECK(index < array.size());
/// ```

/// Verifies an invariant condition with a formatted message. Fatal in debug, stripped in shipping.
///
/// # Arguments
/// * `expr` - Boolean expression to verify
/// * `fmt` - printf-style format string for the failure message
/// * `...` - Format arguments
///
/// # Panics
/// Calls `std::abort()` via `FatalAssert` when `expr` is false (non-shipping builds).
///
/// # Examples
/// ```cpp
/// UZ_CHECKF(size > 0, "Buffer size must be positive, got %d", size);
/// ```
#ifdef UZ_SHIPPING
/// Fatal assertion — no-op in shipping builds.
#define UZ_CHECK(expr) ((void)0)
/// Fatal assertion with format message — no-op in shipping builds.
#define UZ_CHECKF(expr, ...) ((void)0)
#else
/// Fatal assertion — aborts with diagnostic if expr is false.
#define UZ_CHECK(expr)                                                                             \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            ::uz::detail::FatalAssert(#expr, __FILE__, __LINE__, nullptr);                         \
        }                                                                                          \
    } while (0)

/// Fatal assertion with format message — aborts with diagnostic if expr is false.
#define UZ_CHECKF(expr, fmt, ...)                                                                  \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            char _uz_buf[1024];                                                                    \
            std::snprintf(_uz_buf, sizeof(_uz_buf), fmt, ##__VA_ARGS__);                           \
            ::uz::detail::FatalAssert(#expr, __FILE__, __LINE__, _uz_buf);                         \
        }                                                                                          \
    } while (0)
#endif

// ── UZ_ENSURE — non-fatal, logs + continues ─────────────────────────────────

/// Checks a recoverable condition. Logs failure but continues execution.
///
/// Evaluates to the boolean result of `expr`, so it can be used in conditionals.
/// In shipping builds, evaluates `expr` without logging (for the boolean result).
///
/// # Arguments
/// * `expr` - Boolean expression to check
///
/// # Returns
/// `true` if `expr` is truthy, `false` otherwise.
///
/// # Behavior
/// - **Debug/Development**: If false, logs expression + file + line + stack trace to stderr.
/// - **Shipping (UZ_SHIPPING)**: Evaluates to `!!(expr)` with no logging.
///
/// # Examples
/// ```cpp
/// if (!UZ_ENSURE(texture != nullptr)) return;
/// if (!UZ_ENSURE(file.is_open())) { UseDefault(); }
/// ```
#ifdef UZ_SHIPPING
/// Recoverable assertion — evaluates to bool, no logging in shipping.
#define UZ_ENSURE(expr) (!!(expr))
#else
/// Recoverable assertion — logs failure and returns bool result.
#define UZ_ENSURE(expr)                                                                            \
    ([&]() -> bool {                                                                               \
        bool _uz_ok = !!(expr);                                                                    \
        if (!_uz_ok) {                                                                             \
            ::uz::detail::AssertionFailed(#expr, __FILE__, __LINE__, nullptr);                     \
        }                                                                                          \
        return _uz_ok;                                                                             \
    }())
#endif

// ── UZ_VERIFY — never stripped (side effects must execute) ──────────────────

/// Verifies a condition that has side effects. NEVER stripped, even in shipping.
///
/// Use when the expression must always execute (e.g., `file.close()`, `mutex.unlock()`),
/// and the return value indicates success.
///
/// # Arguments
/// * `expr` - Boolean expression with side effects that must always execute
///
/// # Panics
/// Calls `std::abort()` via `FatalAssert` when `expr` is false (ALL build configs).
///
/// # Examples
/// ```cpp
/// UZ_VERIFY(CloseHandle(handle));
/// UZ_VERIFY(allocator.Free(ptr));
/// ```
#define UZ_VERIFY(expr)                                                                            \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            ::uz::detail::FatalAssert(#expr, __FILE__, __LINE__, nullptr);                         \
        }                                                                                          \
    } while (0)
