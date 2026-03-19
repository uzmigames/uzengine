<!-- doc-meta
title: UE5 Far Shadow Implementation Guide for UzEngine
description: Step-by-step implementation guide adapting UE5's far shadow cascades to UzEngine's C++ architecture
created: 2026-03-16
status: implementation-guide
tags: [ue5-adaptation, shadows, implementation, rendering]
-->

# UE5 Far Shadow Cascades — Implementation Guide for UzEngine

## Architecture Overview

UE5's Far Shadow Cascades extend CSM beyond the main dynamic shadow distance by:
1. Adding a second tier of cascades covering `[main_distance, far_distance]`
2. Using **explicit opt-in** via `cast_far_shadow` primitive flag (not automatic)
3. Applying **LOD bias** to reduce geometry complexity at distance
4. Using **same atlas** as main cascades (sequential indexing)

## Implementation Steps

### Step 1: Add Component Properties

**File**: `src/engine/renderer/components/directional_light_component.h`

```cpp
struct DirectionalLightComponentDesc {
    // Existing
    float dynamic_shadow_distance_movable = 40000.0f;
    float dynamic_shadow_distance_stationary = 40000.0f;
    int32 dynamic_shadow_cascades = 4;

    // NEW: Far shadows
    float far_shadow_distance = 300000.0f;      // World units, 3km default
    int32 far_shadow_cascade_count = 0;         // Disabled by default, max 10
};
```

**Validation** (post-construction):
```cpp
void ValidateFarShadowSettings() {
    far_shadow_cascade_count = uz_clamp(far_shadow_cascade_count, 0, 10);

    float far_cascade_size = far_shadow_distance - dynamic_shadow_distance_stationary;
    if (far_shadow_cascade_count > 0 && far_cascade_size <= 100.0f) {
        uz_log(LogShadow, Warning, "Far cascade range too small: %f cm (min 100)", far_cascade_size);
        far_shadow_cascade_count = 0;
    }
}
```

### Step 2: Add Cascade Settings Structure

**File**: `src/engine/renderer/core/shadow_types.h`

```cpp
struct CascadeSettings {
    int32 shadow_split_index;    // 0 = first cascade
    float split_near;
    float split_far;
    bool is_far_shadow_cascade;  // NEW: indicates this is a far cascade
    // ... other existing fields
};
```

### Step 3: Implement Cascade Distance Computation

**File**: `src/engine/renderer/shadows/cascade_setup.cpp`

```cpp
/// Compute split distance for a single cascade.
///
/// For main cascades (split_index < num_main_cascades):
///   Uses range [0, main_distance] distributed by exponent
///
/// For far cascades (split_index >= num_main_cascades):
///   Uses range [main_distance, far_distance] distributed by exponent
///
/// # Parameters
/// - split_index: Absolute cascade index (0 .. total_cascades-1)
/// - num_main_cascades: Number of main CSM cascades
/// - num_far_cascades: Number of far CSM cascades
/// - main_distance: End of main CSM range
/// - far_distance: End of far CSM range
/// - exponent: Distribution exponent (3.0 = UE5 default)
///
/// # Returns
/// - Distance from camera to cascade far plane
float ComputeCascadeSplitDistance(
    int32 split_index,
    int32 num_main_cascades,
    int32 num_far_cascades,
    float main_distance,
    float far_distance,
    float exponent)
{
    uz_assert(split_index >= 0 && split_index < num_main_cascades + num_far_cascades);

    if (split_index < num_main_cascades) {
        // Main cascade formula
        float scale = ComputeAccumulatedScale(exponent, split_index, num_main_cascades);
        return 0.0f + scale * (main_distance - 0.0f);  // Shadow near = 0
    } else {
        // Far cascade formula: scale within [main_distance, far_distance] range
        int32 far_index = split_index - num_main_cascades;
        float scale = ComputeAccumulatedScale(exponent, far_index, num_far_cascades);
        return main_distance + scale * (far_distance - main_distance);
    }
}

/// Helper: Compute accumulated distribution scale [0..1]
/// Used by both main and far cascades
float ComputeAccumulatedScale(float exponent, int32 index, int32 total_count) {
    uz_assert(total_count > 0);
    float normalized = (float)(index + 1) / (float)total_count;
    return FMath::Pow(normalized, exponent);
}
```

### Step 4: Add Primitive Flags

**File**: `src/engine/renderer/components/mesh_component.h`

```cpp
struct MeshComponentDesc {
    bool cast_shadow = true;      // Shadow casting enabled
    bool cast_far_shadow = true;  // NEW: can appear in far cascades
};
```

**Reason**: Allows artists to exclude tiny objects from far cascades to reduce shadow rendering cost.

### Step 5: Implement Cascade Creation

**File**: `src/engine/renderer/shadows/shadow_setup.cpp`

```cpp
/// Create projected shadow initializers for a directional light.
/// Returns both main and far cascade initializers.
void CreateDirectionalShadowCascades(
    const DirectionalLightDesc& light,
    const ViewInfo& view,
    TArray<ProjectedShadowInitializer>& out_initializers)
{
    uz_assert(light.cast_shadow);

    // Compute total cascade count
    int32 num_main_cascades = light.dynamic_shadow_cascades;
    int32 num_far_cascades = 0;

    // Check far cascade activation condition
    float far_cascade_size = light.far_shadow_distance - light.dynamic_shadow_distance_stationary;
    if (light.far_shadow_cascade_count > 0 && far_cascade_size > 100.0f) {
        num_far_cascades = uz_min(light.far_shadow_cascade_count, g_max_far_shadow_cascades);
    }

    int32 total_cascades = num_main_cascades + num_far_cascades;

    // Generate per-cascade initializers
    for (int32 i = 0; i < total_cascades; i++) {
        ProjectedShadowInitializer init = {};

        // Compute split distances
        float split_near = (i == 0) ? 0.0f : ComputeCascadeSplitDistance(
            i - 1, num_main_cascades, num_far_cascades,
            light.dynamic_shadow_distance_stationary,
            light.far_shadow_distance,
            light.cascade_distribution_exponent);

        float split_far = ComputeCascadeSplitDistance(
            i, num_main_cascades, num_far_cascades,
            light.dynamic_shadow_distance_stationary,
            light.far_shadow_distance,
            light.cascade_distribution_exponent);

        // Build frustum, compute bounds, etc...
        BuildCascadeInitializer(view, split_near, split_far, init);

        // Set cascade info
        init.cascade_settings.shadow_split_index = i;
        init.cascade_settings.split_near = split_near;
        init.cascade_settings.split_far = split_far;
        init.cascade_settings.is_far_shadow_cascade = (i >= num_main_cascades);

        out_initializers.Push(init);
    }
}
```

### Step 6: Implement Primitive Filtering

**File**: `src/engine/renderer/shadows/shadow_subject_culling.cpp`

```cpp
/// Determine if a primitive should cast a shadow in a cascade.
bool ShouldPrimitiveCastShadowInCascade(
    const MeshComponent& mesh,
    const CascadeSettings& cascade,
    bool is_whole_scene)
{
    uz_assert(is_whole_scene);  // Whole-scene directional shadow

    // Check shadow casting
    if (!mesh.cast_shadow) {
        return false;
    }

    // Far cascade filtering: explicit opt-in
    if (cascade.is_far_shadow_cascade) {
        return mesh.cast_far_shadow;
    }

    // Main cascade: all shadow-casting objects
    return true;
}
```

### Step 7: Implement LOD Bias Application

**File**: `src/engine/renderer/shadows/lod_selection.cpp`

```cpp
/// Select LOD for rendering a mesh in shadow pass.
///
/// For far cascades, applies GFarShadowStaticMeshLODBias to use coarser LOD.
int32 SelectShadowLOD(
    const MeshComponent& mesh,
    const CascadeSettings& cascade,
    float lod_distance_factor) // = LOD distance scale for this cascade type
{
    int32 primary_lod = mesh.compute_shadow_lod(lod_distance_factor);

    // Apply LOD bias for far cascades (only static meshes)
    if (cascade.is_far_shadow_cascade && !mesh.IsGPULODRange()) {
        int32 biased_lod = primary_lod + g_far_shadow_static_mesh_lod_bias;

        // Clamp to valid LOD range
        if (biased_lod < mesh.num_lods) {
            primary_lod = biased_lod;
        }
    }

    return primary_lod;
}
```

### Step 8: Add Console Variables

**File**: `src/engine/renderer/shadows/shadow_depth_rendering.cpp`

```cpp
// Far shadow cascades
int32 g_max_far_shadow_cascades = 10;
int32 g_far_shadow_static_mesh_lod_bias = 0;
float g_far_shadow_lod_distance_factor = 1.0f;
float g_far_shadow_distance_override = 0.0f;

// Main cascades (existing)
float g_shadow_lod_distance_factor = 1.0f;
float g_shadow_lod_distance_factor_cascade_scale = 0.0f;

// Console bindings
void RegisterShadowCVars() {
    // ... existing main cascade cvars ...

    // NEW: far shadow cascades
    uz_cvar_register(
        "r.Shadow.MaxNumFarShadowCascades",
        g_max_far_shadow_cascades,
        "Hard limit on active far shadow cascades per light",
        ECVF_Scalability);

    uz_cvar_register(
        "r.Shadow.FarShadowStaticMeshLODBias",
        g_far_shadow_static_mesh_lod_bias,
        "LOD offset for static meshes in far cascades (0=same as main)",
        ECVF_Scalability);

    uz_cvar_register(
        "r.Shadow.FarShadow.LODDistanceFactor",
        g_far_shadow_lod_distance_factor,
        "Scale for LOD selection distance in far cascades",
        ECVF_RenderThreadSafe);

    uz_cvar_register(
        "r.Shadow.FarShadowDistanceOverride",
        g_far_shadow_distance_override,
        "Override far shadow distance for all lights (0=disabled)",
        ECVF_RenderThreadSafe);
}

/// Get LOD distance factor for a cascade
float GetCascadeLODDistanceFactor(const CascadeSettings& cascade) {
    if (cascade.is_far_shadow_cascade) {
        return g_far_shadow_lod_distance_factor;
    }
    return g_shadow_lod_distance_factor
         + (float)cascade.shadow_split_index * g_shadow_lod_distance_factor_cascade_scale;
}
```

### Step 9: Add Draw Flags

**File**: `src/engine/renderer/core/draw_flags.h`

```cpp
enum class DrawDynamicFlags : uint32 {
    // ... existing flags ...
    FarShadowCascade = 1 << 15,  // Rendering far shadow cascade
};

// Usage in ShadowSetup (shadow_setup.cpp)
if (cascade.is_far_shadow_cascade) {
    shadow_view->draw_flags |= DrawDynamicFlags::FarShadowCascade;
}
```

### Step 10: Update Shadow Rendering Loop

**File**: `src/engine/renderer/shadows/shadow_rendering.cpp`

```cpp
void RenderShadowCascades(
    const DirectionalLightDesc& light,
    const ViewInfo& view,
    RenderCommandList& cmd_list)
{
    uz_assert(light.cast_shadow);

    // Create cascade initializers (includes both main and far)
    TArray<ProjectedShadowInitializer> cascades;
    CreateDirectionalShadowCascades(light, view, cascades);

    // Render each cascade
    for (const auto& init : cascades) {
        const CascadeSettings& cascade = init.cascade_settings;

        uz_profile_scope("RenderShadowCascade", cascade.shadow_split_index);

        // Set up shadow pass state
        SetupShadowPass(init, cmd_list);

        // Mark as far cascade if needed
        if (cascade.is_far_shadow_cascade) {
            cmd_list.MarkDrawFlag(DrawDynamicFlags::FarShadowCascade);
        }

        // Render subjects
        for (const auto& mesh : scene.meshes) {
            if (!ShouldPrimitiveCastShadowInCascade(mesh, cascade, true)) {
                continue;
            }

            int32 lod = SelectShadowLOD(
                mesh,
                cascade,
                GetCascadeLODDistanceFactor(cascade));

            RenderMeshShadow(mesh, lod, cmd_list);
        }
    }
}
```

## Testing Strategy

### Test 1: Cascade Activation

```cpp
TEST(FarShadowCascades, ActivationCondition) {
    DirectionalLightDesc light;
    light.dynamic_shadow_distance_stationary = 10000.0f;
    light.far_shadow_distance = 50000.0f;
    light.far_shadow_cascade_count = 2;

    // Should activate: size = 40000 cm > 100 cm
    EXPECT_TRUE(light.ShouldUseFarShadowCascades());

    // Disable if too small
    light.far_shadow_distance = 10050.0f;
    EXPECT_FALSE(light.ShouldUseFarShadowCascades());  // size = 50 cm < 100 cm
}
```

### Test 2: Distance Computation

```cpp
TEST(FarShadowCascades, DistanceFormula) {
    // 4 main + 2 far cascades, exponent=3.0
    int num_main = 4, num_far = 2;
    float main_dist = 10000.0f, far_dist = 50000.0f;
    float exp = 3.0f;

    // Cascade 0 (main)
    EXPECT_NEAR(ComputeCascadeSplitDistance(0, 4, 2, 10000, 50000, 3.0f), 1562.5f, 1.0f);

    // Cascade 4 (far, index 0)
    EXPECT_NEAR(ComputeCascadeSplitDistance(4, 4, 2, 10000, 50000, 3.0f), 18000.0f, 1.0f);
}
```

### Test 3: Primitive Filtering

```cpp
TEST(FarShadowCascades, PrimitiveFiltering) {
    MeshComponent mesh;
    mesh.cast_shadow = true;
    mesh.cast_far_shadow = false;

    CascadeSettings cascade;
    cascade.is_far_shadow_cascade = true;

    // Should reject: mesh doesn't cast far shadows
    EXPECT_FALSE(ShouldPrimitiveCastShadowInCascade(mesh, cascade, true));

    cascade.is_far_shadow_cascade = false;
    // Should include: main cascade includes all casting meshes
    EXPECT_TRUE(ShouldPrimitiveCastShadowInCascade(mesh, cascade, true));
}
```

## Integration Checklist

- [ ] Add `FarShadowDistance` and `FarShadowCascadeCount` properties to light component
- [ ] Add `cast_far_shadow` flag to mesh components
- [ ] Implement `ComputeCascadeSplitDistance()` for far cascades
- [ ] Implement primitive filtering in `ShouldPrimitiveCastShadowInCascade()`
- [ ] Add LOD bias in `SelectShadowLOD()`
- [ ] Add `is_far_shadow_cascade` to `CascadeSettings`
- [ ] Register 4 console variables (MaxNum, LODBias, LODDistanceFactor, DistanceOverride)
- [ ] Add `FarShadowCascade` draw flag
- [ ] Update `CreateDirectionalShadowCascades()` to generate both tiers
- [ ] Update shadow rendering loop to respect `is_far_shadow_cascade`
- [ ] Write tests for activation, distance formula, and filtering
- [ ] Profile memory/performance impact
- [ ] Document in rendering architecture docs

## Performance Expectations

| Configuration | Shadow Render Cost | Notes |
|---------------|-------------------|-------|
| 4 main only | 100% baseline | Current state |
| 4 main + 2 far (no LOD bias) | +45-60% | Full geometry complexity |
| 4 main + 2 far (LOD+1 bias) | +20-30% | Recommended setup |
| 4 main + 2 far (LOD+2 bias) | +10-15% | Aggressive optimization |

LOD bias is the primary performance lever — far more impactful than cascade count.
