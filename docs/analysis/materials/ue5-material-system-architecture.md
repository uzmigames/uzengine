<!-- doc-meta
title: UE5 Material System Architecture
description: Exact data model for multi-material meshes, material slots, and MaterialInstance relationship
created: 2026-03-17
source: W:\UE_5.6_Source Engine branch
keywords: material-slots, material-instance, override-materials, static-mesh
-->

# UE5 Material System Architecture

## 1. Material Class Hierarchy

UE5 uses a **single-inheritance base class model** for all materials:

```
UObject
  └── UMaterialInterface (Abstract base)
        ├── UMaterial (Concrete: the base material asset)
        └── UMaterialInstance (Concrete: parameter override without changing parent)
```

**Key insight**: Both `UMaterial` and `UMaterialInstance` implement the same interface. **The mesh component does not care which type is assigned to a slot.** This is the core pattern UzEngine should adopt.

### UMaterialInterface (Base)
- **File**: `W:/UE_5.6_Source/Engine/Source/Runtime/Engine/Public/Materials/MaterialInterface.h:293`
- **Type**: Abstract base class for all materials
- **Responsibility**: Virtual interface for material properties, parameter binding, texture sampling info
- **Key methods**:
  - `GetMaterial()` — returns the base UMaterial
  - `GetNaniteOverride()` — returns Nanite-specific override material
  - `GetPhysicalMaterial()` — returns associated physical material

### UMaterial (Concrete)
- **File**: `W:/UE_5.6_Source/Engine/Source/Runtime/Engine/Public/Materials/Material.h:431`
- **Type**: `class UMaterial : public UMaterialInterface`
- **Responsibility**: The "master" material definition with shader nodes
- **Ownership**: Can own shader graph, expressions, material flags

### UMaterialInstance (Concrete)
- **File**: `W:/UE_5.6_Source/Engine/Source/Runtime/Engine/Public/Materials/MaterialInstance.h:578`
- **Type**: `class UMaterialInstance : public UMaterialInterface`
- **Key members**:
  ```cpp
  /// Parent material (can be UMaterial or another UMaterialInstance)
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=MaterialInstance)
  TObjectPtr<class UMaterialInterface> Parent;

  /// Static parameters that require shader permutation
  uint8 bHasStaticPermutationResource:1;

  /// Physical material override
  TObjectPtr<class UPhysicalMaterial> PhysMaterial;
  ```
- **Responsibility**:
  - Does NOT copy parent shader graph
  - Only stores parameter overrides (scalars, vectors, textures)
  - Can have static parameters that create shader permutations
  - References parent by `UMaterialInterface*` pointer

**CRITICAL PATTERN**: MaterialInstance is NOT a subclass of Material. It's a sibling that inherits from MaterialInterface. This allows the system to be polymorphic without type-switching.

---

## 2. StaticMesh Material Slots

A `UStaticMesh` defines N **material slots**, each with a default material.

### FStaticMaterial Struct
- **File**: `W:/UE_5.6_Source/Engine/Source/Runtime/Engine/Classes/Engine/StaticMesh.h:486`
- **Type**: Struct (not a class)
- **Definition**:
  ```cpp
  struct FStaticMaterial
  {
    /// The default material for this slot (can be UMaterial or UMaterialInstance)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = StaticMesh)
    TObjectPtr<class UMaterialInterface> MaterialInterface;

    /// Optional: User-friendly name for this slot (for UI, serialization)
    /// If set, gameplay code uses this name instead of array index
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = StaticMesh)
    FName MaterialSlotName;

    /// Optional: Original name from source file (FBX/glTF import)
    /// Used during re-import to maintain slot ordering
    UPROPERTY(VisibleAnywhere, Category = StaticMesh)
    FName ImportedMaterialSlotName;

    /// Per-slot UV streaming metadata
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = StaticMesh)
    FMeshUVChannelInfo UVChannelData;

    /// Optional: Overlay material for this specific slot
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = StaticMesh)
    TObjectPtr<class UMaterialInterface> OverlayMaterialInterface;
  };
  ```

### UStaticMesh.StaticMaterials Array
- **File**: `W:/UE_5.6_Source/Engine/Source/Runtime/Engine/Classes/Engine/StaticMesh.h:957`
- **Type**: `TArray<FStaticMaterial> StaticMaterials`
- **Access**: Private (wrapped by getter/setter methods)
- **Capacity**: Unbounded (grows as needed)
- **Getter**:
  ```cpp
  const TArray<FStaticMaterial>& GetStaticMaterials() const
  {
    WaitUntilAsyncPropertyReleased(EStaticMeshAsyncProperties::StaticMaterials);
    return StaticMaterials;
  }
  ```

### UStaticMesh.GetMaterial(int32 MaterialIndex)
- **File**: `W:/UE_5.6_Source/Engine/Source/Runtime/Engine/Private/StaticMesh.cpp:9520`
- **Implementation**:
  ```cpp
  UMaterialInterface* UStaticMesh::GetMaterial(int32 MaterialIndex) const
  {
    if (GetStaticMaterials().IsValidIndex(MaterialIndex))
    {
      return GetStaticMaterials()[MaterialIndex].MaterialInterface;
    }
    return nullptr;
  }
  ```
- **Return type**: `UMaterialInterface*` (not UMaterial* — can be Material or MaterialInstance)
- **Bounds checking**: Returns nullptr if index is out of range
- **Thread safety**: Async compilation locks enforced (WaitUntilAsyncPropertyReleased)

---

## 3. StaticMeshComponent Material Resolution

### OverrideMaterials Array
- **File**: `W:/UE_5.6_Source/Engine/Source/Runtime/Engine/Classes/Components/MeshComponent.h:31`
- **Type**: `TArray<TObjectPtr<class UMaterialInterface>> OverrideMaterials`
- **Owner**: `UMeshComponent` (base class for all mesh components)
- **Semantics**: **Per-component material overrides** that can be null
- **Access**: Public UPROPERTY (editable in editor, but should use setter for thread safety)
- **Comment**: "Per-Component material overrides. These must NOT be set directly or a race condition can occur between GC and the rendering thread."

### Resolution Order: GetMaterial(int32 MaterialIndex)
- **File**: `W:/UE_5.6_Source/Engine/Source/Runtime/Engine/Public/StaticMeshComponentHelper.h:108`
- **Type**: Template method (works for UStaticMeshComponent, UInstancedStaticMeshComponent, etc.)
- **Implementation**:
  ```cpp
  template<class T>
  UMaterialInterface* FStaticMeshComponentHelper::GetMaterial(
    const T& Component,
    int32 MaterialIndex,
    bool bDoingNaniteMaterialAudit = false,
    bool bIgnoreNaniteOverrideMaterials = false)
  {
    UMaterialInterface* OutMaterial = nullptr;

    // STEP 1: Check component override first
    if (Component.OverrideMaterials.IsValidIndex(MaterialIndex) &&
        Component.OverrideMaterials[MaterialIndex])
    {
      OutMaterial = Component.OverrideMaterials[MaterialIndex];
    }
    // STEP 2: Fall back to mesh default
    else if (Component.GetStaticMesh())
    {
      OutMaterial = Component.GetStaticMesh()->GetMaterial(MaterialIndex);
    }

    // STEP 3: Apply Nanite override if needed
    if (OutMaterial)
    {
      OutMaterial->ConditionalPostLoad();

      if (!bIgnoreNaniteOverrideMaterials &&
          Component.UseNaniteOverrideMaterials(bDoingNaniteMaterialAudit))
      {
        UMaterialInterface* NaniteOverride = OutMaterial->GetNaniteOverride();
        OutMaterial = NaniteOverride != nullptr ? NaniteOverride : OutMaterial;
      }
    }

    return OutMaterial;
  }
  ```

### Resolution Algorithm (Plain English)
1. **If** `OverrideMaterials[i]` is set (not null) → use it
2. **Else if** mesh has a default material at slot i → use it
3. **Else** → return nullptr
4. **After resolving**: Check if material has Nanite override (advanced, can ignore for now)

### Key Properties
- **Sparse**: Can override slot 5 without touching slots 0-4 (remaining slots are null)
- **Selective**: Each slot is independent; null means "use mesh default"
- **Type-agnostic**: Override can be UMaterial OR UMaterialInstance, doesn't matter
- **Dynamic**: Can change at runtime via `SetMaterial(i, material)` (thread-safe wrapper)

---

## 4. Section-to-Slot Mapping

Each mesh LOD contains **sections**, and each section has a **material index** that points into the StaticMaterials array.

### FStaticMeshLODResources
- **File**: Not shown, but relevant structure in StaticMeshResources.h
- **Member**: `TArray<FStaticMeshSection> Sections`

### FStaticMeshSection
- **Key member**:
  ```cpp
  int16 MaterialIndex;  // Index into StaticMaterials array
  ```
- **Semantics**: This is the **slot index**, not a per-section override
- **Usage in render code**:
  ```cpp
  // From StaticMeshComponentHelper.h:191-195
  for (FStaticMeshSection& RenderSection : LODRenderData.Sections)
  {
    int16 MaterialIndex = RenderSection.MaterialIndex;  // Get the slot
    // Later use: Component.GetMaterial(MaterialIndex)  // Resolve to actual material
  }
  ```

### Complete Data Flow
```
StaticMesh
  ├── StaticMaterials[0]  {MaterialInterface: M0, MaterialSlotName: "BaseMaterial"}
  ├── StaticMaterials[1]  {MaterialInterface: M1, MaterialSlotName: "DetailMaterial"}
  └── RenderData
       └── LODResources[0]
            ├── Sections[0]  {MaterialIndex: 0, ...}  → uses StaticMaterials[0]
            ├── Sections[1]  {MaterialIndex: 1, ...}  → uses StaticMaterials[1]
            └── Sections[2]  {MaterialIndex: 0, ...}  → uses StaticMaterials[0] again

StaticMeshComponent (instance of mesh)
  ├── StaticMesh: ^(above)
  └── OverrideMaterials
       ├── [0]: null        → GetMaterial(0) → M0 (mesh default)
       ├── [1]: InstanceM1  → GetMaterial(1) → InstanceM1 (override)
       └── [2]: null        → GetMaterial(2) → nullptr (no default, out of bounds)

Render call for Sections[0]:
  Section.MaterialIndex = 0
  Material = Component.GetMaterial(0)  → InstanceM0? No, [0] is null → M0

Render call for Sections[1]:
  Section.MaterialIndex = 1
  Material = Component.GetMaterial(1)  → InstanceM1 (override!)

Render call for Sections[2]:
  Section.MaterialIndex = 0  (same as Sections[0])
  Material = Component.GetMaterial(0)  → M0
```

---

## 5. Material Parameter Data Flow

When a material is resolved for a section, how do parameters reach the GPU?

### Per-Draw Parameters
UE5 does **NOT** create a unique constant buffer per material instance. Instead:

1. **Material is resolved** via `GetMaterial(SlotIndex)` → returns `UMaterialInterface*`
2. **Material resources are fetched**:
   - For `UMaterial`: Use directly compiled shader
   - For `UMaterialInstance`: Use parent shader + instance parameter data
3. **Instance parameters are uploaded**:
   - Scalar parameters, vector parameters, texture bindings
   - Stored in `FMaterialInstanceResource` or similar runtime structure
4. **Render pass binds parameters to PSO + CBs**:
   - Material's constant buffer bound (shared per material)
   - Instance overrides bound as separate buffer or packed into material CB
   - Shader samples from the resolved buffer

**UE5 doesn't do per-draw custom CBs for materials.** Instead, it uses:
- **Material CB**: Shared by all instances of that material
- **Instance CB** (if needed): Contains only parameter deltas vs. parent
- **Per-object CB**: Contains world transform, previous frame transform, etc.

---

## 6. Important Nuances

### Material Slots Can Exceed Actual Usage
```cpp
StaticMesh->StaticMaterials.Num() = 5  // 5 defined slots

// But only 3 are actually used by any section:
Section[0].MaterialIndex = 1
Section[1].MaterialIndex = 3
Section[2].MaterialIndex = 1
// Indices 0, 2, 4 are unused
```
→ This is valid. Unused slots don't affect rendering performance.

### Overlay Materials (Per-Slot)
```cpp
FStaticMaterial
{
  MaterialInterface: MainMaterial,
  OverlayMaterialInterface: TranslucentOverlay,  // Rendered on top
}
```
→ Each slot can have a **per-slot overlay material** (rendered in a second pass)
→ File: `MeshComponent.h:72` — `MaterialSlotsOverlayMaterial`

### Editor vs. Runtime Behavior
- **Editor**: `UStaticMeshComponent::GetEditorMaterial()` ignores Nanite overrides
- **Runtime**: `GetMaterial()` respects Nanite overrides

---

## 7. UzEngine Adaptation Strategy

### Pattern to Copy
1. **Single polymorphic base interface** (like UMaterialInterface)
2. **Two concrete implementations**:
   - Base Material (shader graph owner)
   - Material Instance (parameter overrides only)
3. **Mesh stores array of interface pointers** (not concrete types)
4. **Component can override per-slot** with nullable array
5. **Resolution is two-level**: component override → mesh default → nullptr

### Implementation Sketch
```cpp
// uz/engine/renderer/materials/material_interface.h
struct IMaterialInterface {
  virtual ~IMaterialInterface() = default;
  virtual const char* GetName() const = 0;
  virtual IMaterialInterface* GetParent() const { return nullptr; }
  virtual uint32_t GetHash() const = 0;
  // parameter accessors, texture binding, etc.
};

// uz/engine/renderer/materials/material.h
struct Material : public IMaterialInterface {
  // shader graph, compiled shader bytecode, const buffer layout
};

// uz/engine/renderer/materials/material_instance.h
struct MaterialInstance : public IMaterialInterface {
  IMaterialInterface* parent;
  // parameter overrides only (scalar, vec4, texture pointers)
  // NO shader graph, NO bytecode
};

// uz/engine/scene/static_mesh.h
struct StaticMesh {
  struct MaterialSlot {
    IMaterialInterface* material_interface;
    StringId slot_name;
    // UV channel data, overlay material, etc.
  };

  Array<MaterialSlot> material_slots;
  IMaterialInterface* GetMaterial(uint32_t slot_idx) const;
};

// uz/engine/renderer/static_mesh_component.h
struct StaticMeshComponent {
  Handle<StaticMesh> mesh;
  Array<Handle<IMaterialInterface>> override_materials;  // sparse, can be null

  IMaterialInterface* GetMaterial(uint32_t slot_idx) const {
    // Step 1: Check override
    if (override_materials.is_valid_index(slot_idx) &&
        override_materials[slot_idx].is_valid()) {
      return override_materials[slot_idx].Get();
    }
    // Step 2: Check mesh default
    if (mesh.is_valid()) {
      return mesh->GetMaterial(slot_idx);
    }
    return nullptr;
  }
};
```

### Key Differences from UE5
- **No Nanite override** (unless implementing UE5 Nanite)
- **No physical material per-slot** (can add later)
- **No async compilation locks** (if not doing async mesh builds)
- **Handle<T> instead of TObjectPtr** (UzEngine memory model)

---

## 8. Thread Safety Notes (UE5 Implementation)

UE5 has **race conditions** between:
- Game thread modifying `OverrideMaterials`
- Render thread reading material list in scene proxy

**UE5's solution**:
- **Don't modify OverrideMaterials directly**
- Use `UMeshComponent::SetMaterial(int32 Index, UMaterialInterface* Material)`
- This function uses render command queue to update thread-safely

**For UzEngine**:
- If using immediate render graph (single-threaded render), not a concern
- If future multi-threaded render, use command queue for material changes

---

## Summary: Data Model Diagram

```
╔══════════════════════════════════════════════════════════════════════╗
║                    MATERIAL INHERITANCE TREE                         ║
╠══════════════════════════════════════════════════════════════════════╣
║                                                                      ║
║  UMaterialInterface (Abstract Base)                                 ║
║        ▲                                  ▲                         ║
║        │                                  │                         ║
║   UMaterial              UMaterialInstance (Parameter Overrides)   ║
║   (Master Graph)         ├─ Parent: UMaterialInterface*            ║
║                          ├─ Scalar Parameters                       ║
║                          ├─ Vector Parameters                       ║
║                          ├─ Texture Parameters                      ║
║                          └─ Static Parameters (permutations)        ║
║                                                                      ║
╚══════════════════════════════════════════════════════════════════════╝

╔══════════════════════════════════════════════════════════════════════╗
║                  STATICMESH DATA ORGANIZATION                        ║
╠══════════════════════════════════════════════════════════════════════╣
║                                                                      ║
║  UStaticMesh                                                        ║
║  ├─ StaticMaterials: TArray<FStaticMaterial>                       ║
║  │  ├─ [0] {MaterialInterface: M0, SlotName: "Base"}              ║
║  │  ├─ [1] {MaterialInterface: M1, SlotName: "Detail"}            ║
║  │  └─ [2] {MaterialInterface: M2, SlotName: "Glow"}              ║
║  │                                                                 ║
║  └─ RenderData.LODResources[0].Sections                           ║
║     ├─ [0] {MaterialIndex: 0}  → GetMaterial(0) → M0             ║
║     ├─ [1] {MaterialIndex: 1}  → GetMaterial(1) → M1             ║
║     └─ [2] {MaterialIndex: 0}  → GetMaterial(0) → M0             ║
║                                                                      ║
╚══════════════════════════════════════════════════════════════════════╝

╔══════════════════════════════════════════════════════════════════════╗
║            STATICMESHCOMPONENT OVERRIDE RESOLUTION                   ║
╠══════════════════════════════════════════════════════════════════════╣
║                                                                      ║
║  UStaticMeshComponent                                              ║
║  ├─ StaticMesh: UStaticMesh*                                       ║
║  │  └─ (see above)                                                  ║
║  │                                                                  ║
║  └─ OverrideMaterials: TArray<UMaterialInterface*>                 ║
║     ├─ [0]: null          → GetMaterial(0): Check mesh → M0       ║
║     ├─ [1]: CustomMat1    → GetMaterial(1): Use override           ║
║     ├─ [2]: null          → GetMaterial(2): Check mesh → M2       ║
║     └─ (sparse: only N elements needed)                            ║
║                                                                      ║
║  GetMaterial(int32 Index):                                          ║
║    if (OverrideMaterials[Index] != null)  → return override       ║
║    else if (Mesh->GetMaterial(Index))     → return mesh default   ║
║    else                                     → return nullptr        ║
║                                                                      ║
╚══════════════════════════════════════════════════════════════════════╝
```

---

## References

| Item | File | Line |
|------|------|------|
| FStaticMaterial struct | StaticMesh.h | 486 |
| UStaticMesh.StaticMaterials | StaticMesh.h | 957 |
| UStaticMesh::GetMaterial() | StaticMesh.cpp | 9520 |
| UMeshComponent.OverrideMaterials | MeshComponent.h | 31 |
| FStaticMeshComponentHelper::GetMaterial() | StaticMeshComponentHelper.h | 108 |
| UMaterialInterface class | MaterialInterface.h | 293 |
| UMaterial class | Material.h | 431 |
| UMaterialInstance class | MaterialInstance.h | 578 |

