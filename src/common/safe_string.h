#pragma once
// ---------------------------------------------------------------------------
// safe_string.h -- Safe C string utilities (MSVC-warning-free)
//
// Drop-in replacements for strncpy / fopen that use the _s variants on MSVC
// and the standard POSIX versions elsewhere.
// ---------------------------------------------------------------------------

#include <cstdio>
#include <cstring>

namespace uz {

/// Safe strncpy: copies at most (dst_size - 1) chars and always null-terminates.
inline void str_copy(char* dst, size_t dst_size, const char* src) {
    if (!dst || dst_size == 0) return;
#ifdef _MSC_VER
    strncpy_s(dst, dst_size, src, _TRUNCATE);
#else
    std::strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
#endif
}

/// Safe fopen: returns nullptr on failure.
inline FILE* file_open(const char* path, const char* mode) {
#ifdef _MSC_VER
    FILE* f = nullptr;
    if (fopen_s(&f, path, mode) != 0) return nullptr;
    return f;
#else
    return std::fopen(path, mode);
#endif
}

} // namespace uz
