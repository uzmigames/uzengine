# Milestone 1.1 — Math & Containers

## Why

Game engines require high-performance math primitives and memory-efficient data structures:
- **Math**: Physics, transforms, graphics all depend on vectors/matrices/quaternions with SIMD acceleration
- **Containers**: ECS, scene graphs, physics bodies need allocation-friendly structures without std::vector's reallocation overhead
- **Allocators**: Frame-based allocation patterns reduce fragmentation and improve cache locality
- **Random**: Procedural generation and physics simulation need deterministic, fast RNG

A unified math library with zero-copy containers enables both game code and engine systems to share the same primitives, ensuring consistency.

## What Changes

### Math Foundation
**`core/math/uz_math.h`** — SIMD-accelerated via DirectXMath:
- `Vec2`, `Vec3`, `Vec4`: Zero-initialized, component getters, arithmetic operators, SIMD dot/cross/normalize
- `Mat4`: Identity-default, column-major (DirectX convention), point/direction transforms, decomposition
- `Quat`: Slerp/lerp interpolation, axis-angle construction, vector rotation, matrix conversion
- `Transform`: Position/rotation/scale composition, hierarchy support, decomposition
- `LinearColor`, `sRGBColor`: Gamma-correct conversion with proper piecewise threshold functions

**`core/math/uz_geometry.h`** — Frustum culling and collision:
- `Plane`: Half-space representation, signed distance calculations
- `AABB`: Axis-aligned bounding box with containment, intersection, expansion tests
- `Frustum`: 6-plane view frustum extraction from view-projection matrix, conservative culling

### Containers (Zero-Copy, Allocation-Friendly)
**`core/containers/uz_containers.h`** (umbrella header):
- `RingBuffer`: Lock-free SPSC circular queue (acquire/release semantics, cache-line aligned)
- `FreeList`: Intrusive free list for element-stable allocation
- `HandleMap`: Generational handle → T mapping with use-after-free prevention
- `FrameArray`: Frame-scoped temporary allocations

### Memory Allocators (Named, Budget-Tracked)
**`core/memory/uz_allocators.h`**:
- `LinearAllocator`: Bump pointer with reset (frame/streaming scenarios)
- `PoolAllocator`: Fixed-size block pool with intrusive free chain
- `FrameAllocator`: Linear allocator that auto-resets per frame

### Random Number Generation
**`core/random/uz_random.h`** — PCG (Permuted Congruential Generator):
- 64-bit state, 32-bit output
- Statistically excellent (passes BigCrush test suite)
- Seeded and stream variants for deterministic parallel generation
- Utility methods: `Float()`, `Range()`, `Int()`, `UInt()`, `Bool()`, `UnitSphere()`, `UnitDisk()`

## Testing & Documentation

### Unit Tests (160 total, 27 suites)
- **Math**: 88 tests covering vectors, matrices, quaternions, transforms, color conversions
- **Geometry**: 26 tests for AABB, Frustum, plane operations, frustum culling
- **Containers**: 6 tests for ring buffer, free list, handle map
- **Allocators**: 11 tests for linear, pool, frame allocation patterns
- **Random**: 17 tests for distribution, seeding, streaming
- **Core Systems**: Existing logging, assertions, memory, config, platform, input, profiling

### Documentation
All public types/functions use **Rust-style documentation** (`///` comments):
- Type invariants and thread safety guarantees
- Usage examples for each API
- Performance characteristics (SIMD codegen, O(1) operations, etc.)

## Success Metrics
- ✅ **160 unit tests passing** (all coverage)
- ✅ **Zero TODOs** (no deferred decisions)
- ✅ **100% Rust-style documentation** (all public items)
- ✅ **SIMD codegen** verified (release build disassembly shows XMVector* instructions)
- ✅ **No compiler warnings** (MSVC, Clang)
- ✅ **All files ≤1500 lines** (single responsibility)

## Dependencies
- ✅ **1.0** (Logging, Assertions, Platform Window, Config) — DONE

## Impact
Provides foundation for:
- **1.2 RHI**: Graphics math transforms
- **1.3 Renderer**: Culling, transforms, color management
- **1.4 Scene**: Transform hierarchy, entity handles
- **1.5 Gameplay**: Physics vectors, animation interpolation, procedural generation
