#pragma once

// UZ_CHECK / UZ_ENSURE -- Structured assertion macros (UE5-style).
//
// UZ_CHECK(cond)            -- Fatal. Writes crash report + aborts.
// UZ_CHECK_MSG(cond, ...)   -- Fatal with formatted message.
// UZ_ENSURE(cond)           -- Non-fatal. Writes report + logs, returns false.
// UZ_ENSURE_MSG(cond, ...)  -- Non-fatal with formatted message.
//
// Usage:
//   UZ_CHECK(ptr != nullptr);
//   UZ_CHECK_MSG(idx < count, "Index %u out of bounds (max %u)", idx, count);
//
//   if (!UZ_ENSURE(texture.is_valid())) return;
//   if (!UZ_ENSURE_MSG(size > 0, "Invalid size %d", size)) return default_val;

#include "engine/core/crash_reporter.h"
#include "engine/core/diag.h"

#include <cstdio>
#include <cstdlib>

namespace uz::detail {

// Format an assertion message and write a crash report.
// Returns false so UZ_ENSURE can use it in an expression.
inline bool assertion_failed(const char* expr, const char* file, int line,
                              bool fatal, const char* msg = nullptr) {
    char detail[1024] = {};
    if (msg) {
        std::snprintf(detail, sizeof(detail),
                      "Assertion failed: %s\n  at %s:%d\n  %s",
                      expr, file, line, msg);
    } else {
        std::snprintf(detail, sizeof(detail),
                      "Assertion failed: %s\n  at %s:%d",
                      expr, file, line);
    }

    // Log to DIAG.
    DIAG(detail);

    // Write structured crash report.
    CrashReporter::write_report(fatal ? "UZ_CHECK" : "UZ_ENSURE", detail);

    if (fatal) {
        std::abort();
    }

    return false; // For UZ_ENSURE: the condition was false.
}

// Variadic message formatter.
template <typename... Args>
inline bool assertion_failed_fmt(const char* expr, const char* file, int line,
                                  bool fatal, const char* fmt, Args... args) {
    char msg[512] = {};
    std::snprintf(msg, sizeof(msg), fmt, args...);
    return assertion_failed(expr, file, line, fatal, msg);
}

} // namespace uz::detail

// ---------------------------------------------------------------------------
// Public macros
// ---------------------------------------------------------------------------

// Fatal assertion -- writes report + aborts.
#define UZ_CHECK(cond) \
    do { \
        if (!(cond)) { \
            ::uz::detail::assertion_failed(#cond, __FILE__, __LINE__, true); \
        } \
    } while (0)

#define UZ_CHECK_MSG(cond, fmt, ...) \
    do { \
        if (!(cond)) { \
            ::uz::detail::assertion_failed_fmt(#cond, __FILE__, __LINE__, true, fmt, ##__VA_ARGS__); \
        } \
    } while (0)

// Non-fatal assertion -- writes report + logs, evaluates to bool.
// Usage: if (!UZ_ENSURE(x)) return;
#define UZ_ENSURE(cond) \
    ((cond) ? true : ::uz::detail::assertion_failed(#cond, __FILE__, __LINE__, false))

#define UZ_ENSURE_MSG(cond, fmt, ...) \
    ((cond) ? true : ::uz::detail::assertion_failed_fmt(#cond, __FILE__, __LINE__, false, fmt, ##__VA_ARGS__))
