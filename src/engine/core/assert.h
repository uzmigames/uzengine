#pragma once

// UzEngine assert/verify macros.
//
// In Debug builds: evaluates condition, logs file/line/function to stderr, aborts.
// In Release builds (UZ_SHIPPING): ASSERT/ASSERT_MSG are no-ops; VERIFY keeps
// the expression evaluation (side-effects preserved); ENSURE logs but does not abort.

#include <cstdio>
#include <cstdlib>

namespace uz::detail {

[[noreturn]] inline void assert_fail(const char* expr, const char* file,
                                     int line, const char* func) {
    std::fprintf(stderr, "ASSERTION FAILED: %s\n  at %s:%d in %s\n",
                 expr, file, line, func);
    std::abort();
}

[[noreturn]] inline void assert_fail_msg(const char* expr, const char* file,
                                         int line, const char* func,
                                         const char* msg) {
    std::fprintf(stderr, "ASSERTION FAILED: %s\n  Message: %s\n  at %s:%d in %s\n",
                 expr, msg, file, line, func);
    std::abort();
}

} // namespace uz::detail

#if defined(UZ_SHIPPING)
    // Shipping builds: ASSERT/ASSERT_MSG are complete no-ops.
    // VERIFY keeps expression evaluation for side-effects.
    // ENSURE keeps expression evaluation and logs on failure (no abort).
    #define ASSERT(expr)            ((void)0)
    #define ASSERT_MSG(expr, msg)   ((void)0)
    #define VERIFY(expr)            (expr)
    #define ENSURE(expr)            (expr)
#else
    // Debug/development builds: all macros are active.
    // Wrapped in do-while(0) for safe use in if/else without braces.
    #define ASSERT(expr) \
        do { if (!(expr)) { ::uz::detail::assert_fail(#expr, __FILE__, __LINE__, __func__); } } while(0)

    #define ASSERT_MSG(expr, msg) \
        do { if (!(expr)) { ::uz::detail::assert_fail_msg(#expr, __FILE__, __LINE__, __func__, msg); } } while(0)

    #define VERIFY(expr) ASSERT(expr)

    #define ENSURE(expr) \
        do { if (!(expr)) { std::fprintf(stderr, "ENSURE failed: %s at %s:%d\n", #expr, __FILE__, __LINE__); } } while(0)
#endif
