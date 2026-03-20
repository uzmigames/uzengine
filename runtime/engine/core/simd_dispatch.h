/// UzEngine SIMD Dispatch — Compile-Time and Runtime SIMD Capability Detection.
///
/// Provides compile-time macros for platform intrinsic includes and runtime
/// query functions for optional SIMD instruction sets. The runtime result is
/// cached on first call so CPUID is invoked at most once per process.
///
/// # Platform Support
/// - **x86_64**: SSE2 is the architecture baseline (always available).
///               AVX2 is detected at runtime via the existing CpuInfo system.
/// - **ARM64**:  NEON is the architecture baseline (always available).
///
/// # Usage
/// ```cpp
/// #include "engine/core/simd_dispatch.h"
///
/// if (uz::SimdHasAVX2()) {
///     // fast AVX2 path
/// } else {
///     // SSE2 fallback (guaranteed on x86_64)
/// }
/// ```
///
/// # Thread Safety
/// All query functions are safe to call from any thread. The cached flag is
/// initialized via a thread-safe static local (C++11 "magic statics").

#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Compile-Time Platform Detection
// ─────────────────────────────────────────────────────────────────────────────

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    /// x86/x64 platform — SSE2 is always available, AVX2 may be available.
    #define UZ_SIMD_SSE2  1
    #define UZ_SIMD_AVX2  1  // Header available; runtime check needed before use.
    #define UZ_SIMD_NEON  0
#elif defined(__aarch64__) || defined(_M_ARM64)
    /// ARM64 platform — NEON is always available.
    #define UZ_SIMD_SSE2  0
    #define UZ_SIMD_AVX2  0
    #define UZ_SIMD_NEON  1
#else
    /// Unknown platform — scalar fallback only.
    #define UZ_SIMD_SSE2  0
    #define UZ_SIMD_AVX2  0
    #define UZ_SIMD_NEON  0
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Platform Intrinsic Headers
// ─────────────────────────────────────────────────────────────────────────────

#if UZ_SIMD_SSE2
    #include <emmintrin.h>   // SSE2
#endif

#if UZ_SIMD_AVX2
    #include <immintrin.h>   // AVX, AVX2, FMA (superset of emmintrin.h)
#endif

#if UZ_SIMD_NEON
    #include <arm_neon.h>
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Runtime SIMD Detection
// ─────────────────────────────────────────────────────────────────────────────

#if UZ_SIMD_AVX2
    // Full struct definition needed because SimdHasAVX2() accesses
    // CpuInfo::hasAVX2 inline. The platform-specific system_info.h is
    // resolved via the build system's include directories.
    #include "system_info.h"
#endif

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Query Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Returns true if SSE2 is available.
///
/// On x86_64, SSE2 is part of the architecture baseline and is unconditionally
/// available. On ARM64 and other platforms, returns false.
[[nodiscard]] inline bool SimdHasSSE2() noexcept {
#if UZ_SIMD_SSE2
    return true;
#else
    return false;
#endif
}

/// Returns true if AVX2 is available at runtime.
///
/// The result is cached on first call using a thread-safe static local.
/// CPUID is invoked at most once per process lifetime. On non-x86 platforms,
/// returns false at compile time (zero overhead).
[[nodiscard]] inline bool SimdHasAVX2() noexcept {
#if UZ_SIMD_AVX2
    // C++11 "magic static" — thread-safe initialization, cached forever.
    static const bool s_hasAvx2 = GetCpuInfo().hasAVX2;
    return s_hasAvx2;
#else
    return false;
#endif
}

/// Returns true if NEON is available.
///
/// On ARM64, NEON is part of the architecture baseline and is unconditionally
/// available. On x86 and other platforms, returns false.
[[nodiscard]] inline bool SimdHasNEON() noexcept {
#if UZ_SIMD_NEON
    return true;
#else
    return false;
#endif
}

} // namespace uz
