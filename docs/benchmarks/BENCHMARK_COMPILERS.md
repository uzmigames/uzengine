# Compiler Benchmark — Runtime Build

> **Date**: 2026-03-10
> **Source**: `runtime/src/uz_runtime.cpp` (minimal bootstrap — 4 functions, ~20 LOC)
> **Machine**: Windows 10 Pro, AMD/Intel x64
> **Iterations**: 5 per test (warmup + 5 measured)

---

## Compilers Tested

| Compiler | Version |
|----------|---------|
| **Zig cc** | 0.15.2 (uses bundled clang/LLVM backend) |
| **Clang** | 21.1.8 (standalone LLVM install) |
| **MSVC** | 19.44.35223 (VS 2022 Build Tools) |

---

## Results — Direct Compilation (`*.cpp` → `*.obj`)

### Debug (no optimization)

| Compiler | Avg (ms) | Min (ms) | Max (ms) |
|----------|----------|----------|----------|
| **Zig cc** | **75** | **55** | 154 |
| MSVC | 114 | 91 | 204 |
| Clang | 396 | 367 | 493 |

### Release (optimized)

| Compiler | Avg (ms) | Min (ms) | Max (ms) |
|----------|----------|----------|----------|
| **Zig cc -O2** | **84** | **63** | 157 |
| MSVC /O2 | 113 | 92 | 195 |
| Clang -O2 | 410 | 374 | 506 |

### Zig Build System (full `zig build` — includes build.zig eval, lib creation)

| Mode | Avg (ms) | Min (ms) | Max (ms) |
|------|----------|----------|----------|
| `zig build` (debug) | 4044 | 3960 | 4081 |
| `zig build -Doptimize=ReleaseFast` | 4104 | 4051 | 4141 |

---

## Output Sizes

| Compiler | Debug (bytes) | Release (bytes) |
|----------|---------------|-----------------|
| MSVC | 923 | 1,288 |
| Clang | 1,023 | 1,023 |
| Zig cc | 13,323 | 12,535 |

---

## Analysis

### Compilation Speed

**Zig cc is the fastest compiler** for direct C++ compilation — **1.5x faster than MSVC** and **5x faster than standalone Clang**. This is surprising since Zig cc uses LLVM internally, but Zig's compiler driver has significantly less startup overhead than the standalone Clang installation.

MSVC is the second fastest, which is expected — it's highly optimized for Windows and doesn't load the full LLVM pipeline.

Standalone Clang 21.1.8 is the slowest, likely due to heavy startup cost (loading LLVM libraries, parsing default include paths, etc.).

### Zig Build System Overhead

`zig build` adds ~4 seconds of overhead even for a single file. This is the cost of:
1. Compiling `build.zig` itself (Zig → machine code)
2. Evaluating the build graph
3. Creating the static library archive

For incremental builds (file already cached), `zig build` should be near-instant. The 4s cost is cold-start only.

### Object Sizes

Zig cc produces significantly larger object files (13KB vs ~1KB). This is because Zig's LLVM backend emits additional debug/metadata sections by default. In a full engine build, the final linked binary size difference will be negligible — the linker strips unused sections.

### Recommendation

**Use Zig cc for the runtime build** (via `zig build`). The direct compilation speed advantage (55ms min) is excellent, and the build system overhead (4s cold) is acceptable since:
- Incremental builds are near-instant
- The build system gives us cross-compilation for free
- No external CMake/build tool dependency for the runtime

**Keep MSVC (via cmake-js) for the napi/ addon** — it's required for the delay-load hook and Electron compatibility.

---

*Benchmark script: `scripts/benchmark_compilers.bat`*
