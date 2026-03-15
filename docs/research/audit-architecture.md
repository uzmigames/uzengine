# Architecture Audit: UzEngine 0.2 Runtime

**Scope**: `e:\UzEngine\0.2\runtime\engine\` and `runtime\demos\`
**Date**: 2026-03-13
**Auditor**: Claude Code (Sonnet 4.6)

---

## Executive Summary

The UzEngine 0.2 runtime has a well-structured layered architecture with a strong RHI abstraction. The RHI interface (`rhi.h`) is clean and backend-agnostic, and the render graph is a sound design. The most significant issues are concentrated in three areas: the pass descriptor structs assume a single-mesh world, the `math_defs.h` header pollutes every translation unit with `using namespace DirectX`, and the demos bypass the engine's math layer by calling DirectXMath directly. None of these are fatal, but they will create friction when adding Vulkan/Metal or scaling to a multi-mesh scene.

---

## Finding Index

| ID | Severity | Category | Title |
|----|----------|----------|-------|
| A-01 | **High** | Module Boundaries | `math_defs.h` leaks `using namespace DirectX` globally |
| A-02 | **High** | Scalability | Pass descriptors hard-code a single-mesh assumption |
| A-03 | **High** | Coupling | `constant_buffers.h` encodes a D3D12-specific root signature layout |
| A-04 | **Medium** | SOLID / SRP | `CBRingAllocator` lives inside `constant_buffers.h` — misplaced responsibility |
| A-05 | **Medium** | Dependency Flow | `shadow_pass.h` uses non-prefixed includes (`rhi_types.h`, `render_graph_types.h`) |
| A-06 | **Medium** | SOLID / DIP | Demos call `DirectXMath` directly, bypassing the engine math layer |
| A-07 | **Medium** | Coupling | `LightingPassDesc` carries both bindless and SM5 fields — fat descriptor |
| A-08 | **Medium** | Abstraction Gap | No `IRenderPass` interface — passes are free functions, not a composable system |
| A-09 | **Low** | SOLID / ISP | `IRHICommandList` mixes graphics, compute, and copy operations in one interface |
| A-10 | **Low** | Scalability | Root signature slot assignments are magic numbers scattered across pass descriptors |
| A-11 | **Low** | Documentation | `IRHIDevice` comment block for `GetBackend()` is embedded inside `IsBindless()` doc |
| A-12 | **Low** | Thread Safety | `DX12Device` header states thread safety is NOT guaranteed, contradicting `rhi.h` promise |

---

## Detailed Findings

---

### A-01 — `math_defs.h` leaks `using namespace DirectX` globally

**Severity**: High

**Context**: `runtime/engine/math/math_defs.h` includes `<DirectXMath.h>` and then places `using namespace DirectX;` at file scope (line 11), outside any namespace guard.

**Problem**: Every translation unit that includes any math header (transitively, the entire engine) silently imports the `DirectX` namespace. This makes `XMVector3Normalize`, `XMFLOAT4X4`, `XMMATRIX`, and all other DirectXMath symbols available without qualification in every file — including files that should have no knowledge of the backend.

Concretely: `renderer/camera.h` includes `engine_math.h`, which includes `math_defs.h`. Any file that includes `camera.h` — including Vulkan or Metal backend files — now has the entire DirectXMath namespace imported. When adding a Vulkan backend, any local `float4` or `matrix` type from the Vulkan SDK that collides with a DirectXMath name will cause silent shadowing or ambiguous lookup errors.

**Evidence**: `camera.cpp` line 15 has a redundant `using namespace DirectX;` that would be unnecessary if the header-level import were removed.

**Recommended Fix**: Remove `using namespace DirectX;` from `math_defs.h`. Replace all bare `XM*` usages in math implementation files with the qualified `DirectX::XM*`. The math public API (Vec3, Mat4, Quat, etc.) should never expose `DirectX::` types directly — use `memcpy` bridging or `toXM()` helper methods confined to `.cpp` files.

**Trade-off**: Requires touching all math `.cpp` files to qualify DirectXMath calls. Estimated effort: 2–3 hours.

**Consequences if unaddressed**: When Vulkan or Metal backends are added, name collisions with platform SDKs become likely. The pollution also makes code review harder because it is not obvious which symbols come from which namespace.

---

### A-02 — Pass descriptors hard-code a single-mesh assumption

**Severity**: High

**Context**: `GBufferPassDesc`, `PBRPassDesc`, and `ShadowPassDesc` all contain:

```cpp
RHIBufferHandle   vertexBuffer;   // single global vertex buffer
RHIBufferHandle   indexBuffer;    // single global index buffer
uint32_t          indexCount;     // "Total indices per mesh (single-mesh path)"
```

The comment in `pbr_pass.h` line 63 explicitly reads `"single-mesh path"`.

**Problem**: This design assumes a single packed vertex/index buffer shared by all draw commands in the pass. Once the engine needs to render multiple meshes with different vertex buffers (e.g., after proper asset loading), every pass descriptor and its implementation must be refactored. The `DrawCommand` already stores `meshId` for future-proofing, but the pass machinery cannot act on it — it binds one VB/IB globally and draws every command against it.

This is an SRP violation: the pass descriptor is responsible for both the rendering configuration and the mesh data policy. The mesh policy should live in the `DrawCommand` or in a `MeshTable` lookup, not in the per-frame pass descriptor.

**Recommended Fix**: Move `vertexBuffer`, `indexBuffer`, `indexFormat`, and `indexCount` out of pass descriptor structs. Add them to `DrawCommand` alongside `meshId`, or introduce a `MeshTable` that passes look up by `meshId`. The pass implementation iterates the draw list and binds the appropriate buffers per command.

**Trade-off**: This requires refactoring three pass implementations and their callers (demos). It must be done before real asset loading is integrated; delaying makes the refactor progressively more expensive.

**Consequences if unaddressed**: The engine cannot render scenes with more than one unique mesh through the render graph passes without significant rewrites.

---

### A-03 — `constant_buffers.h` encodes a D3D12-specific root signature layout

**Severity**: High

**Context**: `constant_buffers.h` documents and hard-codes root parameter slot assignments throughout:

```cpp
/// - `b0`: Root constants (descriptor index)
/// - `b1`: Per-frame CB
/// - `b2`: Per-object CB
/// - `b3`: Lighting CB
/// - `b4`: Shadow data CB
```

These register assignments are replicated verbatim as magic integers in every pass implementation (`SetRootCBV(1, ...)`, `SetRootCBV(2, ...)`, `SetDescriptorIndex(0, ...)`). The register layout is a D3D12 root signature concept — Vulkan uses push constants and descriptor sets; Metal uses argument buffers. The current design couples the renderer layer's data types to a single API's binding model.

**Problem**: When adding a Vulkan or Metal backend, the root parameter indices (0, 1, 2, 3, 4) lose meaning. Vulkan's equivalent is a `(set, binding)` pair; Metal uses `[[buffer(N)]]`. The pass implementations will need backend-conditional logic or the RHI interface must abstract the binding model more completely.

**Recommended Fix**: Introduce a `RootLayout` enum or named constants in a renderer-level file (`renderer/root_layout.h`) that maps semantic slots to backend-specific binding points. The pass functions reference `RootLayout::kPerFrameCB` rather than literal `1`. Each backend's command list implementation translates these named slots to native binding calls. This follows the DIP principle — the renderer depends on the abstraction, not on D3D12 register numbers.

**Trade-off**: Additional indirection. For DX12-only builds this adds no value, but it is the gate that unlocks safe multi-backend pass implementations.

---

### A-04 — `CBRingAllocator` lives inside `constant_buffers.h` — misplaced responsibility

**Severity**: Medium

**Context**: `constant_buffers.h` defines three distinct concerns in one file: CB data layout structs (`PerFrameCB`, `PerObjectCB`, `LightingCB`, `ShadowDataCB`), the `CBRingAllocator` class, and the `CBAllocation` struct. The file is 249 lines.

**Problem**: `CBRingAllocator` is an upload-heap management utility. It depends on `IRHIDevice` and `RHIBufferHandle`. The CB layout structs are pure data. These are separate concerns with different change reasons: the layout structs change when the shader constant layout changes; `CBRingAllocator` changes when the memory management strategy changes.

Per CLAUDE.md rule: one file = one concern. Files approaching 1000 lines should be split proactively. This file is at 249 lines now but will grow as more CB types are added. The current mix means any file that needs `CBRingAllocator` must also pull in all the CB data structs, and vice versa.

**Recommended Fix**: Split into:
- `renderer/constant_buffer_types.h` — data-only: `PerFrameCB`, `PerObjectCB`, `LightingCB`, `ShadowDataCB`, `CBAllocation`
- `renderer/cb_ring_allocator.h/.cpp` — `CBRingAllocator` class

**Trade-off**: More include paths to update, but cleaner dependency edges.

---

### A-05 — `shadow_pass.h` uses non-prefixed includes

**Severity**: Medium

**Context**: `shadow_pass.h` includes:

```cpp
#include "render_graph_types.h"
#include "rhi_types.h"
```

All other pass headers in the same directory use the full path:

```cpp
#include "engine/renderer/render_graph/render_graph_types.h"
#include "engine/rhi/rhi_types.h"
```

**Problem**: This inconsistency indicates the build system has search paths that make both forms resolve, but the short form is fragile — it works only because the compiler's `-I` includes the parent directories of these headers. If include paths change, or if `shadow_pass.h` is included from a translation unit with different search paths, it will fail to compile. It also makes the dependency graph opaque to tools that walk `#include` directives literally.

**Recommended Fix**: Change `shadow_pass.h` to use the same full-path include style used by its sibling headers.

**Trade-off**: Trivial. Zero risk.

---

### A-06 — Demos call DirectXMath directly, bypassing the engine math layer

**Severity**: Medium

**Context**: `deferred_test_demo.cpp`, `pbr_test_demo.cpp`, `mesh_test_demo.cpp`, `camera_test_demo.cpp`, and `demos/common/fps_camera_controller.cpp` all contain `using namespace DirectX;` and use `XMMATRIX`, `XMMatrixInverse`, `XMMatrixTranspose`, `XMVectorGetX`, etc. directly.

**Problem**: Demos are the specification-by-example for how the engine API should be used. If demos call DirectXMath directly, new engineers will copy that pattern into game code and engine systems. The engine already provides `Mat4`, `Vec3`, `Quat` with SIMD acceleration via DirectXMath — demos should demonstrate the engine math API, not bypass it.

Additionally, `deferred_test_demo.cpp` contains a local `ComputeNormalMatrix()` function that calls `XMMatrixInverse` and `XMMatrixTranspose`. This is exactly the kind of utility that should live in the engine's math layer (`Mat4::NormalMatrix()` or `engine_math.h`).

**Recommended Fix**:
1. Add `Mat4::NormalMatrix()` to the engine math API.
2. Remove `using namespace DirectX;` from all demo files.
3. Replace DirectXMath calls with engine API calls throughout demos.

**Trade-off**: Demo refactor cost (estimated 4–6 hours). The win is that demos become correct usage examples.

---

### A-07 — `LightingPassDesc` carries both bindless and SM5 fields — fat descriptor

**Severity**: Medium

**Context**: `LightingPassDesc` (lighting_pass.h, 68 lines) contains both sets of fields:

```cpp
// SM6.6 bindless fields
uint32_t albedoSrvIndex;
uint32_t normalSrvIndex;
uint32_t metalRoughSrvIndex;
uint32_t depthSrvIndex;

// SM5 fallback fields
RHITextureHandle albedoTexture;
RHITextureHandle normalTexture;
RHITextureHandle metalRoughTexture;
RHITextureHandle depthTextureForSrv;

bool bindless; // selects which half to use
```

The pattern is identical in `GBufferPassDesc`, `PBRPassDesc`, and `ShadowPassDesc`. Every pass descriptor is carrying dead weight: at runtime exactly one branch is active and the other set of fields is unused.

**Problem**: This violates ISP (callers must always provide both sets), adds cognitive load (which fields are actually used?), and makes the descriptor struct fragile if additional binding models are added (e.g., a Vulkan descriptor-set model is neither bindless-SRV nor SetFragmentTexture).

**Recommended Fix**: Use a tagged union or a `RenderBindingDesc` variant:

```cpp
struct BindlessBinding { uint32_t srvIndex; };
struct SM5TextureBinding { RHITextureHandle handle; };
using GBufferBinding = std::variant<BindlessBinding, SM5TextureBinding>;
```

Or, since the binding model is determined at device creation and doesn't change per-frame, make it a pass constructor parameter rather than a per-frame descriptor field. The pass can branch once at creation time rather than once per frame.

**Trade-off**: Variant syntax adds complexity. An alternative is two concrete pass types: `AddGBufferPassBindless()` and `AddGBufferPassSM5()`, selected once at startup. This is simpler and avoids the runtime branch entirely.

---

### A-08 — No `IRenderPass` interface — passes are free functions, not a composable system

**Severity**: Medium

**Context**: All render passes (`AddGBufferPass`, `AddLightingPass`, `AddShadowPass`, `AddPBRPass`) are standalone free functions that take a descriptor struct and a `RgBuilder&`. There is no common interface or registry.

**Problem**: There is no mechanism to:
- Enumerate active passes at startup or debug time
- Hot-swap a pass implementation (e.g., swap deferred lighting for ray-traced lighting)
- Attach profiling or validation uniformly across all passes
- Build a pass pipeline from config (e.g., enable/disable AO pass, SSAO pass)

This is a missing Open/Closed abstraction. Adding a new pass is additive (good), but removing, replacing, or conditionally enabling passes requires callers to have `if` branches for each pass type.

**Recommended Fix**: Define an `IRenderPass` interface:

```cpp
class IRenderPass {
public:
    virtual ~IRenderPass() = default;
    virtual void AddToGraph(rg::RgBuilder& builder, const RenderFrameContext& ctx) = 0;
    virtual const char* GetName() const = 0;
};
```

Concrete passes implement this interface. The renderer holds a `std::vector<std::unique_ptr<IRenderPass>>` that is populated at startup (or by config) and iterated each frame. This enables pass enable/disable, ordering changes, and future plugin-style pass extension.

**Trade-off**: More boilerplate per pass. For the current 4-pass setup this may feel like over-engineering. The return is high when the pass count grows to 10+ (SSAO, TAA, bloom, tone-mapping, etc.).

---

### A-09 — `IRHICommandList` mixes graphics, compute, and copy in one interface

**Severity**: Low

**Context**: `IRHICommandList` (rhi.h) declares methods for all three queue types in one class: `DrawIndexed`, `Draw` (graphics), `Dispatch` (compute), `CopyBuffer`, `CopyTexture`, `CopyBufferToTexture`, `CopyTextureToBuffer` (copy).

**Problem**: A `Copy`-queue command list on DX12 or Vulkan cannot legally call `DrawIndexed` or `Dispatch`. The interface does not communicate which operations are valid for a given list type — a caller must remember the queue type passed to `CreateCommandList()`. There is no compile-time prevention of calling `Dispatch` on a copy list.

**Recommended Fix**: Split into focused interfaces:

```cpp
class IRHICommandList        { /* Begin, End, Execute, barriers, etc. */ };
class IRHIGraphicsCommandList : public IRHICommandList { /* Draw*, SetPipeline, viewport, etc. */ };
class IRHIComputeCommandList  : public IRHICommandList { /* Dispatch */ };
class IRHICopyCommandList     : public IRHICommandList { /* Copy* */ };
```

`IRHIDevice::CreateCommandList()` returns the appropriate subtype based on queue type.

**Trade-off**: Splitting requires updating all call sites. The ISP win is that callers of copy lists cannot accidentally issue draw calls — the error is caught at compile time. Deferrable until a second queue type is actively used.

---

### A-10 — Root signature slot assignments are magic numbers

**Severity**: Low

**Context**: Pass implementations use bare integer literals for root parameter indices:

```cpp
cmd->SetRootCBV(1, alloc.gpuAddr);            // PerFrame at b1
cmd->SetRootCBV(2, perObjectAlloc.gpuAddr);   // PerObject at b2
cmd->SetDescriptorIndex(0, srvIndex);          // Descriptor index at slot 0
cmd->SetRootCBV(3, lightingAlloc.gpuAddr);    // Lighting at b3
cmd->SetRootCBV(3, shadowAlloc.gpuAddr);      // Shadow at b3 (different slot!)
```

The comment in `constant_buffers.h` documents the layout, but the integers `0, 1, 2, 3, 4` appear in pass `.cpp` files without named constants.

**Problem**: If the root signature layout is changed (e.g., to add a new binding or reorder for Vulkan compatibility), every pass implementation must be updated manually. The current approach relies on the developer remembering the layout documented in a separate file.

**Recommended Fix**: Define a `RootParams` namespace in `renderer/root_layout.h`:

```cpp
namespace RootParams {
    constexpr uint32_t kDescriptorIndex = 0;
    constexpr uint32_t kPerFrameCB      = 1;
    constexpr uint32_t kPerObjectCB     = 2;
    constexpr uint32_t kLightingCB      = 3;
    constexpr uint32_t kShadowDataCB    = 4;
}
```

All pass implementations use these constants. Changing the layout is a one-file change.

**Trade-off**: Trivial. Pure benefit.

---

### A-11 — `IRHIDevice` has a documentation block collision

**Severity**: Low

**Context**: In `rhi.h`, the `///` comment block for `GetBackend()` begins mid-way through the `IsBindless()` comment:

```cpp
/// Returns true if the device supports SM6.6+ bindless resource access.
///
/// [IsBindless docs...]
///
/// Returns the RHI backend type of this device (DX12, DX11, Vulkan, etc.).  ← this belongs to GetBackend()
///
/// # Returns
/// The `RHIBackend` enum value corresponding to this device's backend.
virtual RHIBackend GetBackend() const = 0;

/// Defined out-of-line in rhi_config.h (needs full ShaderModel enum).
bool IsBindless() const;
```

The method declaration order and documentation are inconsistent: `GetBackend()` is documented before `IsBindless()` is declared, but `IsBindless()` has the `GetBackend()` description embedded in its comment.

**Recommended Fix**: Separate the doc blocks. `GetBackend()` should have its own `///` comment immediately before its declaration. `IsBindless()` should have its own standalone comment.

**Trade-off**: Trivial cosmetic fix.

---

### A-12 — `DX12Device` thread-safety contract contradicts `IRHIDevice` promise

**Severity**: Low

**Context**: `rhi.h` line 29 states:

> `IRHIDevice` methods (Create/Destroy) are thread-safe (internally synchronized).

`dx12_device.h` line 13 states:

> Resource creation/destruction methods are NOT internally synchronized in the current implementation. Callers must serialize access from multiple threads.

**Problem**: The base interface documents a thread-safety guarantee that the concrete implementation explicitly does not honor. Any caller relying on `IRHIDevice`'s documented guarantee will have a data race when using the DX12 backend. This is an LSP violation — the subtype weakens the postcondition.

**Recommended Fix**: Either:
1. Add a mutex to `DX12Device`'s `Create*/Destroy*` methods to honor the interface contract.
2. Revise `IRHIDevice`'s documentation to remove the thread-safety guarantee (demoting it to "implementation-defined"), and document that callers must synchronize.

Option 1 is safer for multi-threaded asset loading. Option 2 is appropriate only if all backends will remain single-threaded.

**Trade-off**: Option 1 adds lock overhead. Option 2 shifts burden to callers.

---

## Scalability Assessment: Adding Vulkan/Metal

The following items will require work when adding a second backend:

| Item | Effort | Blocking |
|------|--------|---------|
| Remove `using namespace DirectX` from `math_defs.h` (A-01) | Medium | Yes — headers will include DX headers in Vulkan/Metal code |
| Abstract root signature slot layout (A-03, A-10) | Medium | Yes — Vulkan uses `(set, binding)`, not slot integers |
| Resolve thread-safety contract (A-12) | Low | No, but needed for correct multi-threaded resource creation |
| Fat pass descriptors (A-07) | Low | No, but SM5 fallback fields become irrelevant overhead |
| `IRHICommandList` interface split (A-09) | Low | No, but enforcing queue-type correctness helps |

The RHI abstraction itself (`rhi.h`, `rhi_types.h`) is sound and backend-agnostic. The generational handle system, the barrier model, and the swap chain interface are all portable. The render graph (`render_graph_builder.h`) operates entirely through `IRHIDevice` and `IRHICommandList` and has no DX12-specific knowledge. These are architectural strengths.

---

## Dependency Flow Assessment

The intended layer order is: `core → rhi → renderer → demos`

| Check | Result |
|-------|--------|
| DX12 types confined to `rhi/dx12/` | PASS — `d3d12.h` only included in `dx12_device.h` |
| `rhi.h` depends only on `rhi_types.h` | PASS |
| `renderer/` depends only on `rhi/` abstractions | PASS — no DX12 types in renderer headers |
| Demos depend only on `renderer/` and `rhi/` interfaces | PARTIAL — demos import `DirectXMath` directly (A-06) |
| `math_defs.h` does not expose DirectXMath symbols | FAIL — `using namespace DirectX;` at file scope (A-01) |
| Passes do not include backend-specific headers | PASS |
| `shadow_pass.h` include paths are consistent | FAIL — bare includes instead of full paths (A-05) |

---

## Prioritized Remediation Roadmap

### Immediate (block nothing, but fix before Vulkan work begins)

1. **A-01**: Remove `using namespace DirectX;` from `math_defs.h`. This is the highest-risk item for multi-backend work.
2. **A-10**: Introduce `RootParams` named constants. Zero risk, small payoff now, larger payoff later.
3. **A-05**: Fix `shadow_pass.h` include paths to use full paths. Trivial.
4. **A-11**: Fix `IRHIDevice` documentation collision. Cosmetic but confusing.

### Short-term (before multi-mesh asset loading)

5. **A-02**: Refactor pass descriptors to remove single-mesh fields. Necessary before real asset loading.
6. **A-04**: Split `constant_buffers.h` into data types and allocator. Prevents the file from becoming a dumping ground.
7. **A-06**: Add `Mat4::NormalMatrix()` and remove DirectXMath calls from demos.

### Medium-term (before Vulkan backend)

8. **A-03**: Abstract root signature binding model into `RootLayout` indirection.
9. **A-07**: Eliminate fat bindless+SM5 dual-field descriptors.
10. **A-12**: Resolve `IRHIDevice` vs `DX12Device` thread-safety contract.

### Long-term (before pass count grows beyond 8)

11. **A-08**: Define `IRenderPass` interface and pass registry.
12. **A-09**: Split `IRHICommandList` into queue-typed subinterfaces.

---

## Conclusion

The architecture is structurally sound at the RHI and render graph layers. The abstraction boundaries are well-drawn and DX12 types do not leak above `rhi/dx12/`. The most urgent issue is the global DirectXMath namespace pollution in `math_defs.h`, which will cause compilation failures or silent name collisions the moment a non-Windows SDK is included anywhere in the build. The second most urgent issue is the single-mesh assumption baked into all pass descriptors, which is a design debt that compounds with every new mesh type added. Both issues are addressable without architectural restructuring — they require targeted, bounded refactors.
