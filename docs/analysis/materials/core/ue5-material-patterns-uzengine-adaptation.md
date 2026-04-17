<!-- doc-meta -->
<!-- date: 2026-03-17 -->
<!-- author: unreal-architect -->
<!-- type: analysis -->
<!-- tags: ue5-patterns, uzengine-adaptation, material-system-design -->
<!-- end-doc-meta -->

# UE5 Material Patterns — UzEngine Adaptation Guide

**Analysis Date**: 2026-03-17
**Purpose**: Concrete recommendations for evolving UzEngine's material system from fixed struct to UE5-inspired flexible design
**Audience**: UzEngine architecture leads, material system implementers

---

## Executive Summary

UzEngine's current material system is a **single fixed `PBRMaterial` struct** uploaded to GPU. This design is **fundamentally incompatible** with UE5's approach and will become a blocker as the engine grows.

### Critical Limitations of Current Approach

| Issue | Impact | Severity |
|-------|--------|----------|
| **No polymorphism** | Can't add cloth, hair, skin shading models | CRITICAL |
| **Fixed 48 bytes** | Can't add new parameters without binary break | CRITICAL |
| **No inheritance** | Can't share base material + override variants | HIGH |
| **No texture transforms** | Can't tiling/scale per-texture, only per-material | HIGH |
| **No sparse storage** | 1000 instances = 1000 full copies (waste) | MEDIUM |
| **No thread-safe snapshots** | Potential race if game thread modifies during render | HIGH |

### Recommended Path Forward

**Phase 1 (2 weeks)**: Add virtual base class + sparse overrides system
**Phase 2 (2 weeks)**: Implement render-time proxy snapshot + parameter metadata
**Phase 3 (1 week)**: Add material inheritance (parent → override chain)
**Phase 4 (optional, future)**: Add cloth/hair shading models

---

## Part 1: Phase 1 — Virtual Material Interface

### Current State

```cpp
// runtime/engine/renderer/material/material.h (CURRENT)
struct PBRMaterial {
    Vec4 baseColor{1.0F, 1.0F, 1.0F, 1.0F};
    float metallic = 0.0F;
    float roughness = 0.5F;
    float ao = 1.0F;
    float _pad0 = 0.0F;
    Vec4 emissive{0.0F, 0.0F, 0.0F, 0.0F};
};

// runtime/engine/renderer/material/material_buffer.h (CURRENT)
class MaterialBuffer {
    // Stores fixed-size PBRMaterial structs in GPU StructuredBuffer
    RHIBufferHandle m_buffer;
    std::vector<PBRMaterial> m_materials;
    // ...
};
```

### Recommended Phase 1 Design

**Step 1.1: Create virtual base class**

```cpp
// NEW: runtime/engine/renderer/material/material_interface.h

#pragma once

#include "math/vector.h"
#include <cstdint>
#include <string>

namespace uz {

class MaterialRenderProxy;

/// Abstract base for all materials (PBR, cloth, skin, etc.)
/// Enables future shading model extensions without breaking render code.
class IMaterialInterface {
public:
    virtual ~IMaterialInterface() = default;

    /// Unique ID for sorting/caching
    virtual uint32_t GetMaterialId() const = 0;

    /// Material type enum (for optional RTTI-free type checking)
    virtual uint32_t GetMaterialType() const = 0;

    /// Get thread-safe parameter snapshot (render-time)
    /// Called on game thread during scene capture
    virtual MaterialRenderProxy* GetRenderProxy() = 0;

    /// Non-copyable (or explicit copy with clone pattern)
    IMaterialInterface(const IMaterialInterface&) = delete;
    IMaterialInterface& operator=(const IMaterialInterface&) = delete;

protected:
    IMaterialInterface() = default;
    IMaterialInterface(IMaterialInterface&&) = default;
    IMaterialInterface& operator=(IMaterialInterface&&) = default;
};

} // namespace uz
```

**Step 1.2: Implement concrete PBR material**

```cpp
// NEW: runtime/engine/renderer/material/pbr_material.h

#pragma once

#include "material_interface.h"
#include "math/vector.h"
#include <memory>

namespace uz {

/// Concrete PBR material (Cook-Torrance BRDF)
/// Can be subclassed for variants (wet, rusty, etc.) via inheritance
class PBRMaterial : public IMaterialInterface {
public:
    PBRMaterial();
    ~PBRMaterial() override = default;

    // ─── Parameter Access ─────────────────────────────────────
    void SetScalar(const std::string& name, float value);
    void SetVector(const std::string& name, const Vec3& value);
    void SetVector(const std::string& name, const Vec4& value);

    bool GetScalar(const std::string& name, float& outValue) const;
    bool GetVector(const std::string& name, Vec3& outValue) const;
    bool GetVector(const std::string& name, Vec4& outValue) const;

    // ─── Interface Implementation ──────────────────────────────
    uint32_t GetMaterialId() const override;
    uint32_t GetMaterialType() const override { return 0; }  // PBR = 0
    MaterialRenderProxy* GetRenderProxy() override;

    // ─── Direct Access (for editor/debugging) ──────────────────
    // These are for cases where parameters are known at compile-time
    const Vec4& GetBaseColor() const { return m_baseColor; }
    float GetMetallic() const { return m_metallic; }
    float GetRoughness() const { return m_roughness; }
    float GetAO() const { return m_ao; }

    void SetBaseColor(const Vec4& color) { m_baseColor = color; }
    void SetMetallic(float m) { m_metallic = m; }
    void SetRoughness(float r) { m_roughness = r; }
    void SetAO(float ao) { m_ao = ao; }

private:
    uint32_t m_id = 0;  // Set by material manager

    // GPU struct (for fast path)
    Vec4 m_baseColor{1.0F, 1.0F, 1.0F, 1.0F};
    float m_metallic = 0.0F;
    float m_roughness = 0.5F;
    float m_ao = 1.0F;
    float m_pad0 = 0.0F;
    Vec4 m_emissive{0.0F, 0.0F, 0.0F, 0.0F};

    // Parameter registry (for sparse access)
    std::unordered_map<std::string, float> m_scalarParams;
    std::unordered_map<std::string, Vec3> m_vectorParams;

    friend class MaterialBuffer;
};

} // namespace uz
```

**Step 1.3: Create material render proxy**

```cpp
// NEW: runtime/engine/renderer/material/material_render_proxy.h

#pragma once

#include "material_interface.h"
#include "math/vector.h"
#include <unordered_map>
#include <memory>

namespace uz {

/// Immutable snapshot of material parameters for render thread
/// Captured once per frame on game thread, used during rendering
class MaterialRenderProxy {
public:
    MaterialRenderProxy(IMaterialInterface* source);
    ~MaterialRenderProxy() = default;

    // ─── Parameter Query (Render Thread) ───────────────────────
    // These return copies, so render thread never calls virtual functions
    bool GetScalar(const std::string& name, float& outValue) const;
    bool GetVector(const std::string& name, Vec3& outValue) const;
    bool GetVector(const std::string& name, Vec4& outValue) const;

    // ─── Metadata ──────────────────────────────────────────────
    uint32_t GetMaterialId() const { return m_materialId; }
    uint32_t GetMaterialType() const { return m_materialType; }

    // ─── GPU Binding ───────────────────────────────────────────
    /// Returns offset of parameter in GPU constant buffer
    /// Returns UINT32_MAX if parameter not found
    uint32_t GetParameterOffset(const std::string& name) const;

private:
    uint32_t m_materialId;
    uint32_t m_materialType;

    // Captured scalar values (immutable after construction)
    std::unordered_map<std::string, float> m_scalarValues;
    std::unordered_map<std::string, Vec3> m_vectorValues;
    std::unordered_map<std::string, Vec4> m_vectorValuesRGBA;

    // Parameter name → GPU constant buffer offset
    std::unordered_map<std::string, uint32_t> m_parameterOffsets;
};

} // namespace uz
```

**Step 1.4: Refactor MaterialBuffer to use interface**

```cpp
// MODIFIED: runtime/engine/renderer/material/material_buffer.h

#pragma once

#include "material_interface.h"
#include "material_render_proxy.h"
#include "rhi_types.h"

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <memory>

namespace uz {

class IRHIDevice;

/// Manages materials (can be PBR, cloth, skin, etc.)
class MaterialBuffer {
public:
    MaterialBuffer() = default;
    ~MaterialBuffer();

    MaterialBuffer(const MaterialBuffer&) = delete;
    MaterialBuffer& operator=(const MaterialBuffer&) = delete;

    MaterialBuffer(MaterialBuffer&& other) noexcept;
    MaterialBuffer& operator=(MaterialBuffer&& other) noexcept;

    /// Initialize with capacity
    bool Init(IRHIDevice* device, uint32_t maxMaterials = 256);

    /// Release GPU resources
    void Shutdown();

    /// Add material, return its ID
    uint32_t AddMaterial(std::unique_ptr<IMaterialInterface> material);

    /// Get material by ID
    IMaterialInterface* GetMaterial(uint32_t id);
    const IMaterialInterface* GetMaterial(uint32_t id) const;

    /// Upload all dirty materials to GPU
    void Upload();

    /// Query counts
    uint32_t Count() const { return m_count; }
    uint32_t Capacity() const { return m_capacity; }

private:
    IRHIDevice* m_device = nullptr;

    // CPU-side: polymorphic material collection
    std::unordered_map<uint32_t, std::unique_ptr<IMaterialInterface>> m_materials;
    uint32_t m_nextId = 1;
    uint32_t m_count = 0;
    uint32_t m_capacity = 0;

    // GPU-side: Fallback structured buffer (for fast materials)
    // For complex materials, render passes handle GPU binding themselves
    RHIBufferHandle m_buffer;
    uint8_t* m_mappedPtr = nullptr;

    bool m_dirty = false;
};

} // namespace uz
```

### Phase 1 Implementation Checklist

- [ ] Create `material_interface.h` with `IMaterialInterface` base class
- [ ] Create `pbr_material.h` with concrete `PBRMaterial` implementation
- [ ] Create `material_render_proxy.h` with snapshot class
- [ ] Refactor `material_buffer.h` to store polymorphic materials
- [ ] Update scene renderer to call `GetRenderProxy()` per material
- [ ] Update render passes to use proxy instead of direct struct access
- [ ] Verify no visual regression (should be identical to current)
- [ ] Update documentation with new design

**Estimated Effort**: 5-7 days
**Risk**: LOW (new classes, old path still works during transition)

---

## Part 2: Phase 2 — Sparse Parameter Overrides & Inheritance

### Design

Add parent material concept + sparse override storage:

```cpp
// NEW: runtime/engine/renderer/material/material_instance.h

#pragma once

#include "material_interface.h"
#include <memory>
#include <unordered_map>
#include <string>

namespace uz {

/// Material instance with parent inheritance + sparse parameter overrides
/// Example: BasePBR (parent) → WoodMaterial (overrides BaseColor) → OakWood (overrides to more reddish)
class MaterialInstance : public IMaterialInterface {
public:
    explicit MaterialInstance(IMaterialInterface* parent);
    ~MaterialInstance() override = default;

    // ─── Parameter Override (Sparse) ───────────────────────────
    void SetScalar(const std::string& name, float value) override;
    void SetVector(const std::string& name, const Vec3& value) override;
    void SetVector(const std::string& name, const Vec4& value) override;

    // ─── Parameter Lookup (Inheritance Chain) ──────────────────
    // Walks up parent chain to find parameter
    bool GetScalar(const std::string& name, float& outValue) const override;
    bool GetVector(const std::string& name, Vec3& outValue) const override;
    bool GetVector(const std::string& name, Vec4& outValue) const override;

    // ─── Parent Access ────────────────────────────────────────
    IMaterialInterface* GetParent() { return m_parent; }
    const IMaterialInterface* GetParent() const { return m_parent; }

    // ─── Interface Implementation ──────────────────────────────
    uint32_t GetMaterialId() const override;
    uint32_t GetMaterialType() const override;
    MaterialRenderProxy* GetRenderProxy() override;

private:
    IMaterialInterface* m_parent;  // Parent material (can be another instance)

    // Only store OVERRIDDEN parameters (sparse)
    std::unordered_map<std::string, float> m_scalarOverrides;
    std::unordered_map<std::string, Vec3> m_vectorOverridesRGB;
    std::unordered_map<std::string, Vec4> m_vectorOverridesRGBA;
};

} // namespace uz
```

### Usage Example

```cpp
// Create base material
auto basePBR = std::make_unique<PBRMaterial>();
basePBR->SetRoughness(0.5f);
basePBR->SetMetallic(0.0f);

// Create instance (wood variant)
auto woodMaterial = std::make_unique<MaterialInstance>(basePBR.get());
woodMaterial->SetVector("BaseColor", Vec4(0.8f, 0.6f, 0.3f, 1.0f));
woodMaterial->SetScalar("Roughness", 0.6f);  // Override only roughness

// Create another instance (oak wood variant) — inherits from wood
auto oakWood = std::make_unique<MaterialInstance>(woodMaterial.get());
oakWood->SetVector("BaseColor", Vec4(0.9f, 0.5f, 0.2f, 1.0f));  // More reddish

// Memory savings:
// - basePBR: ~64 bytes (full PBRMaterial struct)
// - woodMaterial: ~96 bytes (2 overrides)
// - oakWood: ~96 bytes (1 override)
// Total: ~256 bytes for 3 variants
//
// With fixed copies:
// - 3 × 48 bytes = 144 bytes (fixed struct size, no inheritance benefit)
```

### Phase 2 Implementation Checklist

- [ ] Create `material_instance.h` with inheritance support
- [ ] Implement sparse parameter override maps
- [ ] Implement recursive `GetScalar/GetVector` with parent chain walk
- [ ] Create render proxy that captures inherited parameters
- [ ] Update material manager to support instance creation
- [ ] Add editor UI for creating instances + overrides
- [ ] Test inheritance chain up to 3 levels deep
- [ ] Benchmark memory usage (1000 instances with overrides)

**Estimated Effort**: 5-7 days
**Risk**: MEDIUM (changes parameter lookup path, requires testing)

---

## Part 3: Phase 3 — Render-Time Proxy Snapshot & Thread Safety

### Problem

Currently, if game thread modifies parameters while render thread is reading them, there's a potential race condition. Solution: **Capture immutable proxy at render time**.

### Design

```cpp
// MODIFIED: runtime/engine/renderer/material/material_render_proxy.h

class MaterialRenderProxy {
public:
    // Created on game thread during scene capture
    explicit MaterialRenderProxy(IMaterialInterface* source);
    ~MaterialRenderProxy() = default;

    // Deleted copy (must move if needed)
    MaterialRenderProxy(const MaterialRenderProxy&) = delete;
    MaterialRenderProxy& operator=(const MaterialRenderProxy&) = delete;

    // OK to move
    MaterialRenderProxy(MaterialRenderProxy&&) = default;
    MaterialRenderProxy& operator=(MaterialRenderProxy&&) = default;

    // ─── Render Thread Access (Immutable) ──────────────────────
    bool GetScalar(const std::string& name, float& outValue) const;
    bool GetVector(const std::string& name, Vec3& outValue) const;
    bool GetVector(const std::string& name, Vec4& outValue) const;

    // Return GPU offset for direct CB upload
    uint32_t GetParameterOffset(const std::string& name) const;

    uint32_t GetMaterialId() const { return m_materialId; }
    uint32_t GetMaterialType() const { return m_materialType; }

    // ─── GPU Constant Buffer Upload ────────────────────────────
    /// Returns size of constant buffer this material needs
    uint32_t GetConstantBufferSize() const { return m_cbSize; }

    /// Fill a constant buffer with captured parameters
    /// Caller allocates cbMemory of at least GetConstantBufferSize() bytes
    void FillConstantBuffer(void* cbMemory) const;

private:
    uint32_t m_materialId;
    uint32_t m_materialType;
    uint32_t m_cbSize;

    // Immutable snapshots (captured at proxy creation time)
    std::unordered_map<std::string, float> m_scalarValues;
    std::unordered_map<std::string, Vec3> m_vectorValues;
    std::unordered_map<std::string, Vec4> m_vectorValuesRGBA;

    // Parameter name → CB offset (for fast upload)
    std::unordered_map<std::string, uint32_t> m_parameterOffsets;
};
```

### Phase 3 Implementation Checklist

- [ ] Implement render proxy snapshot capture
- [ ] Add parameter offset metadata to proxy
- [ ] Implement `FillConstantBuffer()` method
- [ ] Update scene renderer to call `GetRenderProxy()` for each material
- [ ] Pass immutable proxy to render command
- [ ] Update render passes to use proxy instead of material interface
- [ ] Verify thread-safe (render thread only reads proxy, never modifies)
- [ ] Add profiling to measure proxy capture time

**Estimated Effort**: 3-5 days
**Risk**: MEDIUM (changes render loop, requires frame profiling)

---

## Part 4: Future Extensions (Post-Phase 3)

### Cloth Material Example

```cpp
// NEW: runtime/engine/renderer/material/cloth_material.h

class ClothMaterial : public IMaterialInterface {
public:
    ClothMaterial();

    // Cloth-specific parameters
    void SetFuzzStrength(float f) { m_fuzzStrength = f; }
    void SetAnisotropyDirection(const Vec3& dir) { m_anisotropyDir = dir; }
    void SetFuzzColor(const Vec3& color) { m_fuzzColor = color; }

    uint32_t GetMaterialType() const override { return 1; }  // Cloth = 1

    MaterialRenderProxy* GetRenderProxy() override;

private:
    float m_fuzzStrength = 0.3f;
    Vec3 m_anisotropyDir = Vec3(0, 0, 1);
    Vec3 m_fuzzColor = Vec3(0.5f, 0.5f, 0.5f);
};
```

Then render pass switches on material type:

```cpp
// In deferred lighting pass:
void RenderClothPixel(uint32_t materialType, ...) {
    if (materialType == 0) {  // PBR
        // Cook-Torrance
    } else if (materialType == 1) {  // Cloth
        // Anisotropic cloth BRDF
    }
}
```

---

## Part 5: Comparison Table — Before & After

| Aspect | Current | Phase 1 | Phase 2 | Phase 3 |
|--------|---------|---------|---------|---------|
| **Base class** | None (struct) | Virtual interface | Inheritance chain | Proxy snapshot |
| **Extensibility** | 0 (fixed struct) | ★★★★★ (via subclass) | ★★★★★ (via instance) | ★★★★★ |
| **Memory (1000 instances)** | 48 KB | 48 KB | 48-344 KB (sparse) | 48-344 KB |
| **Parameter count** | Fixed 6 | Flexible | Flexible | Flexible |
| **Thread safety** | Implicit | Implicit | Explicit snapshot | Explicit snapshot |
| **Material variants** | Copy full struct | Subclass | Sparse overrides | Sparse + snapshot |
| **Nesting depth** | N/A | N/A | Up to 5 | Up to 5 |

---

## Part 6: Migration Path (Avoiding Break)

### Step 1: Keep Old Path Working During Transition

```cpp
// Keep PBRMaterial struct AND new interface running in parallel
// Render passes accept both:

void RenderMesh(const DrawCommand& cmd) {
    // New path (uses interface)
    MaterialRenderProxy* proxy = cmd.material->GetRenderProxy();

    // Old path still works for backwards compat
    if (auto* pbrMat = dynamic_cast<PBRMaterial*>(cmd.material)) {
        // Fast path for simple PBR
        gpu_upload_pbr_struct(pbrMat->GetGPUStruct());
    } else {
        // Flexible path for subclasses
        FillConstantBufferFromProxy(proxy);
    }
}
```

### Step 2: Deprecate Old Path Over 2 Releases

- **Phase 1** (now): Both paths work, prefer new interface
- **Phase 2** (1 release): Log deprecation warning for old path
- **Phase 3** (2 releases): Remove old struct fast-path

---

## Part 7: Validation Strategy

### Unit Tests

```cpp
// NEW: runtime/tests/engine/renderer/test_material_system.cpp

TEST(MaterialSystem, InstanceInheritanceLookup) {
    auto baseMat = std::make_unique<PBRMaterial>();
    baseMat->SetRoughness(0.5f);

    auto instance = std::make_unique<MaterialInstance>(baseMat.get());
    instance->SetRoughness(0.7f);

    float inherited;
    EXPECT_TRUE(baseMat->GetScalar("Roughness", inherited));
    EXPECT_EQ(inherited, 0.5f);

    EXPECT_TRUE(instance->GetScalar("Roughness", inherited));
    EXPECT_EQ(inherited, 0.7f);  // Override wins
}

TEST(MaterialSystem, RenderProxySnapshot) {
    auto mat = std::make_unique<PBRMaterial>();
    mat->SetRoughness(0.5f);

    auto proxy1 = mat->GetRenderProxy();
    float v1;
    EXPECT_TRUE(proxy1->GetScalar("Roughness", v1));
    EXPECT_EQ(v1, 0.5f);

    // Game thread modifies parameter
    mat->SetRoughness(0.8f);

    // Proxy snapshot still has old value (immutable)
    EXPECT_TRUE(proxy1->GetScalar("Roughness", v1));
    EXPECT_EQ(v1, 0.5f);

    // New proxy has new value
    auto proxy2 = mat->GetRenderProxy();
    float v2;
    EXPECT_TRUE(proxy2->GetScalar("Roughness", v2));
    EXPECT_EQ(v2, 0.8f);
}
```

### Visual Tests

```bash
# Render scene with new material system
# Verify pixel-perfect match with old system
```

### Profiling

```
Frame time: < 0.1ms overhead for GetRenderProxy() call
Memory: < 400 bytes per material instance (sparse)
```

---

## Conclusion

UE5's material system is **production-proven** and **highly extensible**. By adapting its core patterns (virtual interface, sparse overrides, render-time snapshots), UzEngine can:

1. Support unlimited parameters (not fixed 48 bytes)
2. Support multiple shading models (cloth, hair, skin)
3. Support material inheritance (reduce duplication)
4. Support thread-safe parameter updates
5. Maintain backward compatibility during transition

**Critical Path**: Phase 1 → Phase 2 → Phase 3 = 12-16 days
**Ship-ready**: Phase 3 complete, old path deprecated

---

## References

- UE5 Source: `MaterialInterface.h`, `MaterialInstance.h`, `MaterialRenderProxy.h`
- UzEngine Current: `runtime/engine/renderer/material/*.h`
- Analysis Docs: `ue5-material-parameter-system-extraction.md`, `ue5-material-struct-layouts-concrete.md`
