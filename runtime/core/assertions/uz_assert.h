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
#ifdef UZ_SHIPPING
#define UZ_CHECK(expr) ((void)0)
#define UZ_CHECKF(expr, ...) ((void)0)
#else
#define UZ_CHECK(expr)                                                                             \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            ::uz::detail::FatalAssert(#expr, __FILE__, __LINE__, nullptr);                         \
        }                                                                                          \
    } while (0)

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
// Evaluates to the condition result so you can use: if (!UZ_ENSURE(ptr)) return;
#ifdef UZ_SHIPPING
#define UZ_ENSURE(expr) (!!(expr))
#else
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
#define UZ_VERIFY(expr)                                                                            \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            ::uz::detail::FatalAssert(#expr, __FILE__, __LINE__, nullptr);                         \
        }                                                                                          \
    } while (0)
