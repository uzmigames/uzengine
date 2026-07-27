/// D3D12MA compilation wrapper with GUID compatibility.
///
/// D3D12MA uses IID_PPV_ARGS which requires __uuidof<T> template
/// specializations. On Clang/MinGW, these come from __CRT_UUID_DECL.
/// This wrapper includes our GUID declarations before D3D12MA.

#include "dx12_guids.h"

#include <cstdio>

/// Route D3D12MA's unfreed-allocation report to stderr. The library's
/// "not empty at block destruction" assert aborts the process, and the
/// engine log may not have flushed by then — stderr is the only channel
/// guaranteed to survive. Each leaked allocation prints its debug name,
/// which is the fastest way to identify the leaking subsystem.
#define D3D12MA_DEBUG_LOG(format, ...) do { \
    std::fwprintf(stderr, format, __VA_ARGS__); \
    std::fwprintf(stderr, L"\n"); \
    std::fflush(stderr); \
} while (false)

#include "D3D12MemAlloc.cpp"
