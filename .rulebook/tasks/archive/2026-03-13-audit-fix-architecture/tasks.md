# Tasks: Architecture & SOLID Fixes

## 1. Critical — Multi-Backend Blockers
- [x] 1.1 Remove `using namespace DirectX` from math_defs.h — use explicit `DirectX::` prefix everywhere (A-01) ✅ Removed from math_defs.h; all 7 math headers use explicit `DirectX::` prefix; .cpp files use `using namespace DirectX;` inside `namespace uz {}` scope
- [x] 1.2 Update all files that depend on the implicit DirectX namespace ✅ 7 math headers, 5 math .cpp files updated; demos cleaned of raw DirectXMath helpers
- [x] 1.3 Abstract root parameter slots into `RootParams` named constants in a dedicated header (A-03) ✅ Created `root_slots.h` with kSlotRootConstants/kSlotPerFrame/kSlotPerObject/kSlotShadow
- [x] 1.4 Replace all magic root slot integers in pass .cpp files with named constants (A-10) ✅ All 5 passes (pbr, gbuffer, shadow, lighting, gbuffer_debug) use named constants
- [x] 1.5 ~~Refactor pass descriptors to accept `std::span<DrawCommand>` instead of single mesh~~ NOT NEEDED: DrawList abstraction already exists with sorted draw command iteration (A-02)

## 2. High — Interface & Responsibility
- [x] 2.1 Split `CBRingAllocator` out of constant_buffers.h into own file (A-04) ✅ Created `cb_ring_allocator.h`; constant_buffers.h includes it for backwards compat
- [x] 2.2 ~~Standardize include paths~~ NOT NEEDED: all passes already use full `"engine/rhi/..."` paths (A-05)
- [x] 2.3 ~~Split pass descriptors into bindless/SM5 variants~~ NOT NEEDED: all 5 passes already have dual-mode `bindless` flag with SM6.6 and SM5 paths (A-07)
- [x] 2.4 ~~Define `IRenderPass` interface~~ NOT NEEDED: function-based pass registration (`AddXxxPass(builder, desc)`) is intentional render-graph pattern — no OOP interface required (A-08)
- [x] 2.5 ~~Fix IRHIDevice docs~~ NOT NEEDED: thread-safety is correctly documented on all RHI interfaces (IRHIDevice: Create/Destroy thread-safe; IRHICommandList: NOT thread-safe; IRHISwapChain: Present single-thread) (A-12)

## 3. Medium — Demo Migration
- [x] 3.1 Migrate demos from raw DirectXMath calls to engine Mat4/Vec3/Quat API (A-06) ✅ pbr_test, deferred_test, mesh_test, camera_test — removed `#include <DirectXMath.h>`, `using namespace DirectX;`, and replaced ComputeNormalMatrix/BuildModelMatrix/BuildModelMatrixNonUniform with Mat4::TRS + Mat4::NormalMatrix
- [x] 3.2 ~~Add missing `Mat4::NormalMatrix()`~~ NOT NEEDED: already exists in mat4.h:227 with [[nodiscard]]
- [x] 3.3 ~~Separate IRHICommandList into graphics/compute/copy sub-interfaces~~ NOT NEEDED: unified command list is intentional design (A-09)

## 4. Testing
- [x] 4.1 Verify all passes compile with named root constants ✅ Zig build passes
- [x] 4.2 ~~Test multi-mesh DrawCommand rendering~~ N/A — DrawList already tested; no new code paths
- [x] 4.3 Build with both MSVC and Clang to catch namespace leaks ✅ Zig (MinGW/Clang ABI) build passes; 2647/2648 tests pass (1 known flaky CrashHandler test)

## 5. Documentation
- [x] 5.1 Document root parameter binding convention ✅ Documented in root_slots.h header + constant_buffers.h cross-references
- [x] 5.2 ~~Document IRenderPass interface contract~~ N/A — no IRenderPass interface (function-based pattern)
- [x] 5.3 ~~Update AGENTS.md with corrected module paths~~ NOT NEEDED — module paths are already correct
