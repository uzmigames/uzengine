#pragma once

/// UzEngine Assertion System
/// Pattern: UE5's check/ensure/verify with crash context.

#include <cstdio>
#include <cstdlib>
#include <cstdint>

namespace uz::detail {

/// Report assertion failure. Returns true if execution should continue (ensure), false if fatal.
[[nodiscard]] bool AssertionFailed(const char* expr, const char* file, int line, const char* msg);

/// Report fatal assertion. Never returns.
[[noreturn]] void FatalAssert(const char* expr, const char* file, int line, const char* msg);

} // namespace uz::detail

// ── UZ_CHECK — fatal in debug, stripped in shipping ─────────────────────────
#ifdef UZ_SHIPPING
    #define UZ_CHECK(expr) ((void)0)
    #define UZ_CHECKF(expr, ...) ((void)0)
#else
    #define UZ_CHECK(expr) \
        do { \
            if (!(expr)) { \
                ::uz::detail::FatalAssert(#expr, __FILE__, __LINE__, nullptr); \
            } \
        } while (0)

    #define UZ_CHECKF(expr, fmt, ...) \
        do { \
            if (!(expr)) { \
                char _uz_buf[1024]; \
                std::snprintf(_uz_buf, sizeof(_uz_buf), fmt, ##__VA_ARGS__); \
                ::uz::detail::FatalAssert(#expr, __FILE__, __LINE__, _uz_buf); \
            } \
        } while (0)
#endif

// ── UZ_ENSURE — non-fatal, logs + continues ─────────────────────────────────
// Evaluates to the condition result so you can use: if (!UZ_ENSURE(ptr)) return;
#ifdef UZ_SHIPPING
    #define UZ_ENSURE(expr) (!!(expr))
#else
    #define UZ_ENSURE(expr) \
        ([&]() -> bool { \
            bool _uz_ok = !!(expr); \
            if (!_uz_ok) { \
                ::uz::detail::AssertionFailed(#expr, __FILE__, __LINE__, nullptr); \
            } \
            return _uz_ok; \
        }())
#endif

// ── UZ_VERIFY — never stripped (side effects must execute) ──────────────────
#define UZ_VERIFY(expr) \
    do { \
        if (!(expr)) { \
            ::uz::detail::FatalAssert(#expr, __FILE__, __LINE__, nullptr); \
        } \
    } while (0)
