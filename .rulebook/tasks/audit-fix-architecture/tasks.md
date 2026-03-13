# Tasks: Architecture & SOLID Fixes

## 1. Critical — Multi-Backend Blockers
- [ ] 1.1 Remove `using namespace DirectX` from math_defs.h — use explicit `DirectX::` prefix everywhere (A-01)
- [ ] 1.2 Update all files that depend on the implicit DirectX namespace
- [ ] 1.3 Abstract root parameter slots into `RootParams` named constants in a dedicated header (A-03)
- [ ] 1.4 Replace all magic root slot integers in pass .cpp files with named constants (A-10)
- [ ] 1.5 Refactor pass descriptors to accept `std::span<DrawCommand>` instead of single mesh (A-02)

## 2. High — Interface & Responsibility
- [ ] 2.1 Split `CBRingAllocator` out of constant_buffers.h into own file (A-04)
- [ ] 2.2 Standardize include paths: all passes use full `"engine/rhi/..."` paths (A-05)
- [ ] 2.3 Split pass descriptors into bindless/SM5 variants or use template policy (A-07)
- [ ] 2.4 Define `IRenderPass` interface with `Setup()`, `Execute()`, `GetName()` (A-08)
- [ ] 2.5 Fix IRHIDevice docs: either make DX12Device thread-safe or remove thread-safety promise from interface (A-12)

## 3. Medium — Demo Migration
- [ ] 3.1 Migrate demos from raw DirectXMath calls to engine Mat4/Vec3/Quat API (A-06)
- [ ] 3.2 Add missing `Mat4::NormalMatrix()` if not already present
- [ ] 3.3 Separate IRHICommandList into graphics/compute/copy sub-interfaces or add queue-type validation (A-09)

## 4. Testing
- [ ] 4.1 Verify all passes compile with named root constants
- [ ] 4.2 Test multi-mesh DrawCommand rendering
- [ ] 4.3 Build with both MSVC and Clang to catch namespace leaks

## 5. Documentation
- [ ] 5.1 Document root parameter binding convention
- [ ] 5.2 Document IRenderPass interface contract
- [ ] 5.3 Update AGENTS.md with corrected module paths
