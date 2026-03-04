# RHI Abstraction Layer — Architecture Directives

> **Status**: MANDATORY — applies to all code from Phase 1 onwards.
> **Enforced by**: Code review, CI lint, AGENTS.md rule §RHI-BOUNDARY.

---

## 1. The Problem This Solves

UzEngine must run on Windows (DX12), Linux (Vulkan), and macOS (Metal). Any code that
directly uses `ID3D12Device`, `VkDevice`, `MTLDevice`, or any backend-specific type
**above** the RHI layer creates a multi-thousand line refactor every time a new backend
is added. Evidence of this problem in the current codebase:

```cpp
// ❌ VIOLATION — src/renderer/renderer.h (legacy, must be migrated)
Microsoft::WRL::ComPtr<ID3D12Device>       device_;       // DX12 leak above RHI
Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_; // DX12 leak above RHI
D3D12_VIEWPORT viewport_{};                               // DX12 type in business logic
DirectX::XMFLOAT4X4 worldViewProj_{};                    // DX12 math in renderer
```

The solution is already defined in `src/engine/renderer/rhi/rhi.h`. This document
defines the rules for enforcing that boundary everywhere.

---

## 2. The RHI Boundary — The Golden Rule

```
┌──────────────────────────────────────────────────────────────┐
│   ABOVE rhi.h (engine systems, render graph, components)     │
│   Only allowed types: RhiBuffer, RhiTexture, RhiPipeline,   │
│   RhiCmdList, RhiFence, RhiFormat, RhiResourceState, etc.   │
├──────────────────────────────────────────────────────────────┤  ← THE WALL
│   rhi.h  (the only include that crosses the boundary)        │
├──────────────────────────────────────────────────────────────┤
│   BELOW rhi.h (backend implementations only)                  │
│   rhi_dx12.c  → uses d3d12.h, dxgi.h, DirectXMath.h         │
│   rhi_vulkan.c → uses vulkan.h                               │
│   rhi_metal.c  → uses Metal.h                                │
└──────────────────────────────────────────────────────────────┘
```

### RULE RHI-1 (ABSOLUTE): No backend types above rhi.h

**MUST NOT** include `<d3d12.h>`, `<vulkan/vulkan.h>`, or `<Metal/Metal.h>` in any file
outside of `src/engine/renderer/rhi/rhi_dx12.c`, `rhi_vulkan.c`, `rhi_metal.c`.

```cpp
// ❌ WRONG — anywhere above the RHI layer
#include <d3d12.h>
#include <vulkan/vulkan.h>
ID3D12Device* device = ...;       // FORBIDDEN
VkDevice vk_device = ...;         // FORBIDDEN
Microsoft::WRL::ComPtr<...> ...;  // FORBIDDEN above rhi.h

// ✅ CORRECT — anywhere above the RHI layer
#include "engine/renderer/rhi/rhi.h"
RhiBuffer vb = rhi()->buffer_create(&desc);   // CORRECT
RhiTexture rt = rhi()->texture_create(&desc);  // CORRECT
```

### RULE RHI-2 (ABSOLUTE): No DirectXMath / GLM / simd above rhi.h

Math types used for GPU data MUST be plain `float[16]` or `uz::Mat4` from `engine/core/math.h`.
Never store `DirectX::XMFLOAT4X4`, `glm::mat4`, or `vmath::*` in objects that cross
the render boundary.

```cpp
// ❌ WRONG
DirectX::XMFLOAT4X4 worldViewProj_;   // backend-specific math in renderer struct

// ✅ CORRECT
#include "engine/core/math.h"
uz::Mat4 worldViewProj_;              // portable math type
```

### RULE RHI-3: Command recording goes through RhiCmdList

All GPU commands MUST be issued via `rhi()->cmd_*()` functions using a `RhiCmdList` handle.

```cpp
// ❌ WRONG
commandList_->DrawIndexedInstanced(36, 1, 0, 0, 0);   // raw DX12

// ✅ CORRECT
rhi()->cmd_draw_indexed(cmd, 36, 1, 0, 0, 0);         // RHI abstracted
```

### RULE RHI-4: Resource handles are uint64_t, never raw pointers

No raw `ID3D12Resource*`, `VkBuffer`, or `MTLBuffer` may be stored outside the RHI
backend files.

```cpp
// ❌ WRONG
Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;

// ✅ CORRECT
RhiBuffer vertex_buffer_ = RHI_NULL;
```

---

## 3. Resource Lifetime Rules

### RULE RHI-5: RHI resources must be explicitly destroyed

Every `rhi()->buffer_create()` or `rhi()->texture_create()` MUST have a corresponding
`rhi()->buffer_destroy()` / `rhi()->texture_destroy()` in the shutdown path.

```cpp
// ✅ CORRECT — in component initialization
void MyPass::init() {
    RhiBufferDesc desc = { .size = sizeof(float) * 16, .usage = RHI_BUFFER_USAGE_CONSTANT };
    const_buf_ = rhi()->buffer_create(&desc);
}

// ✅ CORRECT — in component shutdown
void MyPass::shutdown() {
    if (const_buf_ != RHI_NULL) {
        rhi()->buffer_destroy(const_buf_);
        const_buf_ = RHI_NULL;
    }
}
```

### RULE RHI-6: Never hold RhiCmdList across frames

`RhiCmdList` handles are **frame-local**. Do NOT store them as member variables.
Obtain a command list at the beginning of each frame, submit at the end.

```cpp
// ❌ WRONG — storing cmd list across frames
class Renderer {
    RhiCmdList cmd_;  // FORBIDDEN as member
};

// ✅ CORRECT — frame-local usage
void Renderer::render_frame() {
    RhiCmdList cmd = rhi()->cmd_begin();
    // ... record commands ...
    rhi()->cmd_end(cmd);
    rhi()->cmd_submit(cmd);
    // cmd is now invalid — do not use
}
```

---

## 4. The Legacy Renderer Migration

`src/renderer/renderer.h` and `src/renderer/renderer.cpp` are **LEGACY** code that
directly violate all RHI rules above. This code MUST be migrated to use `rhi.h`.

### Migration Priority (Phase 1)

| Legacy File | Status | Migration Target |
|-------------|--------|------------------|
| `src/renderer/renderer.h` | 🔴 VIOLATION | Rewrite using rhi.h |
| `src/renderer/renderer.cpp` | 🔴 VIOLATION | Rewrite using rhi.h |
| `src/renderer/components/editor_component.h` | 🔴 VIOLATION | Rewrite using rhi.h |
| `src/renderer/components/camera_component.h` | 🔴 VIOLATION | Rewrite using rhi.h |
| `src/renderer/components/light_component.h` | 🔴 VIOLATION | Rewrite using rhi.h |
| `src/renderer/gizmos/*.h` | 🔴 VIOLATION | Rewrite using rhi.h |

### RULE RHI-7: All new rendering code MUST use rhi.h

From the moment of this directive (Phase 1), **no new file** may be created in the
`src/renderer/` directory. All new rendering code goes into `src/engine/renderer/`.

The legacy `src/renderer/` directory exists **only** until its migration is complete.
After migration, the entire `src/renderer/` directory will be deleted.

---

## 5. Module Dependency for RHI

```
Allowed to include rhi.h:
  ✅ src/engine/renderer/render_graph.h
  ✅ src/engine/renderer/shader_manager.h
  ✅ src/engine/renderer/*.cpp
  ✅ src/engine/editor/*.cpp (for screenshot etc.)
  ✅ render passes (future: src/engine/renderer/passes/*.cpp)

FORBIDDEN to include rhi.h:
  ❌ src/engine/scene/*.h (scene is backend-agnostic)
  ❌ src/engine/core/*.h (core has zero GPU dependencies)
  ❌ game.dll / scripting layer
  ❌ Any ECS component header
```

The scene system MUST NOT depend on RHI. Scene nodes hold logical data (position, mesh
path, material path). The render graph reads scene data and translates it to RHI calls.

---

## 6. The Render Backend vtable (`rhi_backend_t`)

`rhi.h` defines the complete GPU surface as a C vtable:

```c
const rhi_backend_t *rhi(void);   // global accessor
void rhi_set_backend(const rhi_backend_t *backend);  // set at engine init
```

### RULE RHI-8: One backend is active per process lifetime

Call `rhi_set_backend()` exactly ONCE at engine startup, before any render code runs.
Never swap backends at runtime.

```c
// ✅ CORRECT — engine init
static rhi_backend_t s_dx12_backend = rhi_dx12_make_backend();
rhi_set_backend(&s_dx12_backend);
```

### RULE RHI-9: No static DX12/Vulkan globals outside backend files

Global variables of type `ID3D12Device*`, `VkInstance`, `VkDevice`, etc., MUST only
exist inside `rhi_dx12.c`, `rhi_vulkan.c`, `rhi_metal.c`.

---

## 7. Comparison with Unreal Engine 5

UE5 uses a very similar pattern with `FDynamicRHI`:
- Virtual function table (class with pure virtual functions) — we use a C function pointer struct
- `FRHIResource` base with ref counting — we use `uint64_t` handles (simpler, no ref counting needed for our scale)
- `FRHICommandList` for command recording — maps directly to our `RhiCmdList`
- No backend types in `Engine/Source/Runtime/Engine/` — enforced by module build system

Key difference: UE5 uses C++ virtual dispatch; UzEngine uses a C vtable for simpler cross-language FFI (game.dll, plugins). The principle is identical: **backend types never escape the backend module**.

---

## 8. Checklist for New Rendering Code

Before submitting any rendering code, verify:

```
□ No #include <d3d12.h> outside rhi_dx12.c
□ No #include <vulkan/vulkan.h> outside rhi_vulkan.c
□ No Microsoft::WRL::ComPtr<*> outside rhi_dx12.c
□ No DirectX::XM* types outside rhi_dx12.c
□ All GPU resources stored as RhiBuffer / RhiTexture / RhiPipeline
□ All GPU commands issued via rhi()->cmd_*()
□ Resources explicitly destroyed in shutdown path
□ No RhiCmdList stored as member variable
□ New files go into src/engine/renderer/, NOT src/renderer/
```
