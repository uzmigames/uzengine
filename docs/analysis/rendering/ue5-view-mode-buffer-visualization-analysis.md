# UE5 View Mode & Buffer Visualization System — Complete Architecture Analysis

**Status:** Complete analysis of UE5 5.6 view mode system for UzEngine adaptation
**Source:** W:\UE_5.6_Source
**Analyzed:** 2026-03-14

## Executive Summary

UE5's view mode system is a **two-layer architecture**:

1. **Upper Layer (EViewModeIndex)**: High-level view mode selection (Lit, Unlit, Wireframe, Buffer Visualization, etc.) — controls renderer behavior via show flags.
2. **Lower Layer (EDebugViewShaderMode)**: Shader-level debug visualization modes (Shader Complexity, LOD Coloration, Material accuracy, etc.) — compiles special shader permutations at runtime.
3. **Composition System**: A post-process override system that composites multiple buffer visualization materials into a grid/overview.

**Key insight**: UE5 does NOT switch shaders at runtime for view modes. Instead, it:
- Uses **show flags** to enable/disable rendering features (lights, materials, post-processing)
- Uses **shader permutations** (pre-compiled debug variants) for complexity/accuracy visualizations
- Uses **post-process override materials** for G-Buffer channel visualization
- Uses a **grid compositor** to blend multiple visualizations into a single overview

---

## 1. View Modes (EViewModeIndex) — 37 Modes Total

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Classes\Engine\EngineBaseTypes.h`
**Lines**: ~120 enum values

### Complete Enum List

```cpp
enum EViewModeIndex : int
{
    // Geometric/Lighting Modes
    VMI_BrushWireframe = 0,                    // Wireframe of BSP brushes
    VMI_Wireframe = 1,                         // CSG wireframe (legacy)
    VMI_Unlit = 2,                             // No lighting, textured
    VMI_Lit = 3,                               // Normal lit (default)
    VMI_Lit_DetailLighting = 4,                // Lit with detail normal accentuation
    VMI_LightingOnly = 5,                      // Grayscale lighting (no materials)
    VMI_LightComplexity = 6,                   // Color-coded by light count
    VMI_ShaderComplexity = 8,                  // Color-coded by pixel shader instruction count
    VMI_LightmapDensity = 9,                   // Light map texel density heatmap
    VMI_LitLightmapDensity = 10,               // Lit + lightmap density overlay

    // Reflection & Deferred
    VMI_ReflectionOverride = 11,               // Reflection probe visualization
    VMI_VisualizeBuffer = 12,                  // G-Buffer channel visualization (multi-choice)

    // Lighting Diagnostics
    VMI_StationaryLightOverlap = 14,           // Stationary light overlap heatmap
    VMI_CollisionPawn = 15,                    // Player collision geometry
    VMI_CollisionVisibility = 16,              // Visibility collision geometry

    // Level-Of-Detail (LOD)
    VMI_LODColoration = 18,                    // LOD index color-coded
    VMI_QuadOverdraw = 19,                     // Quad overdraw heatmap
    VMI_HLODColoration = 23,                   // Hierarchical LOD coloration
    VMI_GroupLODColoration = 24,               // Group LOD coloration

    // Texture Streaming Accuracy
    VMI_PrimitiveDistanceAccuracy = 20,        // Primitive distance for texture streaming
    VMI_MeshUVDensityAccuracy = 21,            // UV density accuracy heatmap
    VMI_MaterialTextureScaleAccuracy = 25,     // Material texture scale accuracy
    VMI_RequiredTextureResolution = 26,        // Required vs. actual texture resolution
    VMI_ShaderComplexityWithQuadOverdraw = 22, // Shader complexity + quad overdraw combined

    // Ray Tracing
    VMI_PathTracing = 27,                      // Full path tracing pipeline
    VMI_RayTracingDebug = 28,                  // Ray tracing debug visualization

    // Advanced Systems Visualization
    VMI_VisualizeNanite = 29,                  // Nanite geometry visualization
    VMI_VisualizeVirtualTexture = 30,          // Virtual texture visualization
    VMI_VisualizeLumen = 31,                   // Lumen GI debug views
    VMI_VisualizeVirtualShadowMap = 32,        // Virtual shadow map visualization
    VMI_VisualizeGPUSkinCache = 33,            // GPU skin cache visualization
    VMI_VisualizeSubstrate = 34,               // Substrate material system visualization
    VMI_VisualizeGroom = 35,                   // Groom (hair) visualization
    VMI_LWCComplexity = 36,                    // Large World Coordinates function usage
    VMI_Lit_Wireframe = 37,                    // Lit + wireframe overlay
};
```

### View Mode Behavior Matrix

| Mode | Render | Lights | Materials | PostProcess | Uses Debug Shader |
|------|--------|--------|-----------|-------------|-------------------|
| VMI_Lit | Yes | Yes | Yes | Yes | No |
| VMI_Unlit | Yes | No | Yes | No | No |
| VMI_Wireframe | Yes | No | No | No | No |
| VMI_LightComplexity | Yes | No | Yes | No | Yes |
| VMI_ShaderComplexity | Yes | No | Yes | No | Yes |
| VMI_VisualizeBuffer | Yes | No | Yes | No | No (uses post-process mat) |
| VMI_LightingOnly | Yes | Yes | No (flat white) | No | No |
| VMI_Lit_DetailLighting | Yes | Yes | Yes | Yes | No |
| VMI_QuadOverdraw | Yes | No | Yes | No | Yes |
| VMI_LODColoration | Yes | No | Yes | No | Yes |
| VMI_VisualizeNanite | Yes | No | Yes | No | Yes |

**Applied via**: `ApplyViewMode()` in `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\ShowFlags.cpp` — sets show flags like `VisualizeBuffer`, `VisualizeNanite`, etc.

---

## 2. Debug View Shader Modes (EDebugViewShaderMode) — 13 Modes

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\DebugViewModeHelpers.h`
**Lines**: 22–38

```cpp
enum EDebugViewShaderMode
{
    DVSM_None = 0,                              // No debug visualization

    // Complexity Modes (requires special shader compilation)
    DVSM_ShaderComplexity = 1,                  // Pixel shader instruction count
    DVSM_ShaderComplexityContainedQuadOverhead = 2,  // Shader complexity + quad overhead scaling
    DVSM_ShaderComplexityBleedingQuadOverhead = 3,   // Shader complexity + quad overhead bleeding
    DVSM_QuadComplexity = 4,                   // Quad overdraw only

    // Texture Streaming Accuracy
    DVSM_PrimitiveDistanceAccuracy = 5,        // Distance-based mip selection accuracy
    DVSM_MeshUVDensityAccuracy = 6,            // UV texel density accuracy
    DVSM_MaterialTextureScaleAccuracy = 7,     // Material texture scale accuracy
    DVSM_OutputMaterialTextureScales = 8,      // Output texture scales for analysis
    DVSM_RequiredTextureResolution = 9,        // Required vs. streamed resolution

    // Miscellaneous
    DVSM_LODColoration = 10,                   // LOD index coloration
    DVSM_VisualizeGPUSkinCache = 11,           // GPU skin cache visualization
    DVSM_LWCComplexity = 12,                   // Large World Coordinates function usage

    DVSM_MAX = 13
};
```

**Behavior**: These modes trigger **special shader permutations** that are compiled offline (ODSC) or on-demand. They inject additional code into the pixel shader to output debug colors instead of the final lit color.

---

## 3. Buffer Visualization — G-Buffer Channel Display (25 Channels)

**Files**:
- `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\BufferVisualizationData.h`
- `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\BufferVisualizationData.cpp`
- `W:\UE_5.6_Source\Engine\Config\BaseEngine.ini` (lines for `[Engine.BufferVisualizationMaterials]`)

### Architecture

Buffer visualization is **NOT** a shader mode. It's a **post-process material override** system:

1. **Configuration-Driven**: List of materials defined in `BaseEngine.ini`
2. **Material-Based**: Each channel (BaseColor, Metallic, etc.) is a separate post-process material
3. **Runtime Selection**: Console command `r.BufferVisualizationTarget` picks which material to apply
4. **Overview Mode**: Multiple materials composited into a grid (see Section 4)

### Available Channels (25 Total)

**File**: `W:\UE_5.6_Source\Engine\Config\BaseEngine.ini` (lines ~3100–3125)

```ini
[Engine.BufferVisualizationMaterials]
BaseColor=(Material="/Engine/BufferVisualization/BaseColor.BaseColor", Name=LOCTEXT("BaseColorMat", "Base Color"))
CustomDepth=(Material="/Engine/BufferVisualization/CustomDepth.CustomDepth", Name=LOCTEXT("BaseCustomDepthMat", "Custom Depth"))
CustomStencil=(Material="/Engine/BufferVisualization/CustomStencil.CustomStencil", Name=LOCTEXT("BaseCustomStencilMat", "Custom Stencil"))
FinalImage=(Material="/Engine/BufferVisualization/FinalImage.FinalImage", Name=LOCTEXT("BaseFinalImageMat", "Final Image"), ApplyAutoExposure=true)
ShadingModel=(Material="/Engine/BufferVisualization/LightingModel.LightingModel", Name=LOCTEXT("BaseShadingModelMat", "Shading Model"))
MaterialAO=(Material="/Engine/BufferVisualization/MaterialAO.MaterialAO", Name=LOCTEXT("BaseMaterialAOMat", "Material Ambient Occlusion"))
Metallic=(Material="/Engine/BufferVisualization/Metallic.Metallic", Name=LOCTEXT("BaseMetallicMat", "Metallic"))
Opacity=(Material="/Engine/BufferVisualization/Opacity.Opacity", Name=LOCTEXT("BaseOpacityMat", "Opacity"))
Roughness=(Material="/Engine/BufferVisualization/Roughness.Roughness", Name=LOCTEXT("BaseRoughnessMat", "Roughness"))
Anisotropy=(Material="/Engine/BufferVisualization/Anisotropy.Anisotropy", Name=LOCTEXT("BaseAnisotropyMat", "Anisotropy"))
SceneColor=(Material="/Engine/BufferVisualization/SceneColor.SceneColor", Name=LOCTEXT("BaseSceneColorMat", "Scene Color"), ApplyAutoExposure=true)
SceneDepth=(Material="/Engine/BufferVisualization/SceneDepth.SceneDepth", Name=LOCTEXT("BaseSceneDepthMat", "Scene Depth"))
SeparateTranslucencyRGB=(Material="/Engine/BufferVisualization/SeparateTranslucencyRGB.SeparateTranslucencyRGB", Name=LOCTEXT("BaseSeparateTranslucencyRGBMat", "Separate Translucency RGB"), ApplyAutoExposure=true)
SeparateTranslucencyA=(Material="/Engine/BufferVisualization/SeparateTranslucencyA.SeparateTranslucencyA", Name=LOCTEXT("BaseSeparateTranslucencyAMat", "Separate Translucency Alpha"))
Specular=(Material="/Engine/BufferVisualization/Specular.Specular", Name=LOCTEXT("BaseSpecularMat", "Specular"))
SubsurfaceColor=(Material="/Engine/BufferVisualization/SubsurfaceColor.SubsurfaceColor", Name=LOCTEXT("BaseSubsurfaceColorMat", "Subsurface Color"))
WorldNormal=(Material="/Engine/BufferVisualization/WorldNormal.WorldNormal", Name=LOCTEXT("BaseWorldNormalMat", "World Normal"))
WorldTangent=(Material="/Engine/BufferVisualization/WorldTangent.WorldTangent", Name=LOCTEXT("BaseWorldTangentMat", "World Tangent"))
AmbientOcclusion=(Material="/Engine/BufferVisualization/AmbientOcclusion.AmbientOcclusion", Name=LOCTEXT("BaseAmbientOcclusionMat", "Ambient Occlusion"))
CustomDepthWorldUnits=(Material="/Engine/BufferVisualization/CustomDepthWorldUnits.CustomDepthWorldUnits", Name=LOCTEXT("BaseCustomDepthWorldUnitsMat", "Custom Depth World Units"))
SceneDepthWorldUnits=(Material="/Engine/BufferVisualization/SceneDepthWorldUnits.SceneDepthWorldUnits", Name=LOCTEXT("BaseSceneDepthWorldUnitsMat", "Scene Depth World Units"))
Velocity=(Material="/Engine/BufferVisualization/Velocity.Velocity", Name=LOCTEXT("Velocity", "Velocity"))
PreTonemapHDRColor=(Material="/Engine/BufferVisualization/PreTonemapHDRColor.PreTonemapHDRColor", Name=LOCTEXT("PreTonemapHDRColor", "Pre Tonemap HDR Color"), ApplyAutoExposure=true)
PostTonemapHDRColor=(Material="/Engine/BufferVisualization/PostTonemapHDRColor.PostTonemapHDRColor", Name=LOCTEXT("PostTonemapHDRColor", "Post Tonemap HDR Color"), ApplyAutoExposure=true)
```

### How It Works

1. **Initialization** (`FBufferVisualizationData::Initialize()`):
   - Reads INI section `[Engine.BufferVisualizationMaterials]`
   - Loads all material assets into a `TMultiMap<FName, Record>`
   - Each record stores: name, display name, material pointer, ApplyAutoExposure flag

2. **Runtime Selection**:
   - Console: `r.BufferVisualizationTarget BaseColor` (selects which material)
   - API: `FBufferVisualizationData::GetMaterial(FName)` returns the material to apply

3. **Rendering**:
   - Applied as a post-process material in the post-processing pipeline
   - File: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\PostProcess\PostProcessing.cpp`
   - Pass: `EPass::VisualizeGBufferOverview` (if overview mode) or direct application (single channel)

4. **Optional Auto-Exposure**:
   - Per-material flag `ApplyAutoExposure=true` enables HDR tonemapping for that channel
   - Used for color-space visualizations (Scene Color, HDR Color, etc.)

---

## 4. Buffer Visualization Overview / Composite Grid Mode

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\PostProcess\PostProcessing.cpp`
**Key Functions**:
- `IsVisualizeGBufferOverviewEnabled()` — checks if overview mode is active
- `FVisualizeGBufferOverviewInputs` — input structure
- `EPass::VisualizeGBufferOverview` — post-processing pass

### Console Commands

```
r.BufferVisualizationOverviewTargets=BaseColor,Specular,SubsurfaceColor,WorldNormal,SeparateTranslucencyRGB,,,WorldTangent,SeparateTranslucencyA,,,Opacity,SceneDepth,Roughness,Metallic,ShadingModel,,SceneDepthWorldUnits,SceneColor,PreTonemapHDRColor,PostTonemapHDRColor

r.BufferVisualizationDumpFrames 0|1    # Dump visualizations with screenshots
r.BufferVisualizationDumpFramesAsHDR 0|1  # Force HDR format for dumps
```

### Grid Layout Logic

From `PostProcessing.cpp` (lines ~1860–1900):

```cpp
bool bVisualizeGBufferOverview = IsVisualizeGBufferOverviewEnabled(View);

if (PassSequence.IsEnabled(EPass::VisualizeGBufferOverview))
{
    FVisualizeGBufferOverviewInputs PassInputs;
    PassInputs.OverrideOutput = (output override if last pass);
    PassInputs.SceneColor = SceneColor;
    PassInputs.SceneColorBeforeTonemap = SceneColorBeforeTonemap;
    PassInputs.SceneColorAfterTonemap = SceneColorAfterTonemap;

    // Get the list of materials to composite from r.BufferVisualizationOverviewTargets
    auto& OverviewMaterials = GetBufferVisualizationData().GetOverviewMaterials();

    // Composite each material into a grid tile
    // Implementation: Each material is rendered to a sub-viewport within the screen
}
```

**Composite Strategy**:
- Parse `r.BufferVisualizationOverviewTargets` comma-separated list
- Load each material (skip empty slots with `,, `)
- Render each to a grid cell (typically 4x5 or 5x4 grid)
- Add text labels below each cell showing the channel name
- Scale grid cells to fit within viewport

**Key insight**: The overview is NOT a separate pass per material. Instead:
1. The post-process material override system iterates over the material list
2. Each material is applied to a sub-rectangle of the output
3. A final composite pass blends them together

---

## 5. How Modes Are Passed to Shaders

### Show Flags Path (Most Modes)

```
EViewModeIndex (e.g., VMI_Lit)
  → ApplyViewMode() in ShowFlags.cpp
  → FEngineShowFlags::VisuzlizeNanite = true, etc.
  → BasePassRendering.cpp checks View.Family->EngineShowFlags
  → Selects different render path (but same shaders)
```

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\ShowFlags.cpp`
**Function**: `void ApplyViewMode(EViewModeIndex ViewModeIndex, bool bPerspective, FEngineShowFlags& EngineShowFlags)`

Example mapping:
```cpp
switch(ViewModeIndex)
{
    case VMI_Wireframe:
        // NO shader change, just set show flags to disable materials/lighting
        break;
    case VMI_ShaderComplexity:
        // Uses EDebugViewShaderMode::DVSM_ShaderComplexity
        // Compiles special shader variant
        break;
    case VMI_VisualizeBuffer:
        // Uses post-process material override
        break;
}
```

### Debug View Shader Mode Path

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\SceneView.h`

```cpp
class FSceneView
{
    EViewModeIndex ViewMode;
    EDebugViewShaderMode DebugViewShaderMode;  // Set by ChooseDebugViewShaderMode()

    EDebugViewShaderMode ChooseDebugViewShaderMode() const;
};
```

**Compilation**: Debug shaders are compiled with permutation defines:
- `MATERIAL_COMPILE_FOR_SHADER_COMPLEXITY=1`
- `MATERIAL_COMPILE_FOR_QUAD_OVERDRAW=1`
- etc.

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\DebugViewModeRendering.cpp` (lines ~200–250):

```cpp
bool ShouldCompileDebugViewModeShaders(const FMeshMaterialShaderPermutationParameters& Parameters)
{
    // Only compile debug shaders if WITH_DEBUG_VIEW_MODES is enabled
    // and the view requests a debug mode
    EShaderPermutationFlags Flags = IsDebugViewShaderModeODSCOnly()
        ? EShaderPermutationFlags::IsODSCOnly
        : EShaderPermutationFlags::HasEditorOnlyData | EShaderPermutationFlags::IsODSCOnly;
    return Parameters.Flags & Flags;
}
```

---

## 6. Key Data Structures

### FDebugViewModeShaderElementData

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\DebugViewModeRendering.h` (lines 50–96)

```cpp
class FDebugViewModeShaderElementData : public FMeshMaterialShaderElementData
{
public:
    FDebugViewModeShaderElementData(
        const FMaterialRenderProxy& InMaterialRenderProxy,
        const FMaterial& InMaterial,
        EDebugViewShaderMode InDebugViewMode,     // Which debug mode
        const FVector& InViewOrigin,
        int32 InVisualizeLODIndex,
        const FColor& InSkinCacheDebugColor,
        int32 InViewModeParam,                    // Parameter for the mode
        const FName& InViewModeParamName);

    EDebugViewShaderMode DebugViewMode;
    int32 VisualizeLODIndex;
    int32 NumVSInstructions, NumPSInstructions, NumCSInstructions;
    uint32 LWCComplexityVS, LWCComplexityPS, LWCComplexityCS;
    int32 ViewModeParam;
    FName ViewModeParamName;
};
```

This data is passed to mesh processors which then bind it to debug view shaders.

### FBufferVisualizationData::Record

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\BufferVisualizationData.h` (lines 58–67)

```cpp
struct Record
{
    FString Name;                              // Key from INI (e.g., "BaseColor")
    FText DisplayName;                         // Localized display name
    UMaterialInterface* Material;              // Post-process material to apply
    bool bApplyAutoExposure;                   // Enable tonemapping for this channel
};
```

Stored in `TMultiMap<FName, Record> MaterialMap` for fast lookup.

---

## 7. Rendering Pipeline Integration

### View Mode Application Flow

```
1. Editor/Game selects view mode (e.g., VMI_ShaderComplexity)
   ↓
2. ApplyViewMode() sets show flags (e.g., EngineShowFlags.VisualizeNanite)
   ↓
3. FSceneView::ChooseDebugViewShaderMode() returns EDebugViewShaderMode
   ↓
4. BasePassRendering::AddBasePass() checks View.DebugViewShaderMode
   ↓
5a. If debug mode: Use FDebugViewModeMeshProcessor → render with debug shader
5b. If post-process mode: Normal rendering → Post-process applies material
   ↓
6. Output buffer visualization result or normal scene
```

### Post-Processing Integration

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\PostProcess\PostProcessing.cpp`

```cpp
enum class EPass : uint32
{
    // ... many other passes ...
    VisualizeGBufferOverview,           // Grid composite of buffer visualizations
    VisualizeLumenSceneOverview,        // Lumen overview grid
    // ... more ...
};

if (PassSequence.IsEnabled(EPass::VisualizeGBufferOverview))
{
    // Apply buffer visualization post-process
    FVisualizeGBufferOverviewInputs PassInputs;
    PassInputs.SceneColor = SceneColor;
    // ... populate inputs ...
    SceneColor = AddVisualizeGBufferOverviewPass(GraphBuilder, View, PassInputs);
}
```

---

## 8. Adaptation Strategy for UzEngine

### What UE5 Does Well (Adopt)

1. **Configuration-Driven Buffer Viz**: Define visualization channels in INI/config. No hardcoding.
2. **Material-Based Approach**: Use post-process materials to visualize buffers (don't bake logic into shaders).
3. **Show Flags for Mode Control**: Use bit flags (not enums) to enable/disable features per-mode.
4. **Deferred Compilation**: Don't compile debug shaders upfront; use ODSC or on-demand.
5. **Grid Overview**: Composite multiple visualizations into one viewport for rapid inspection.

### UzEngine Implementation Outline

#### 1. Define ViewModeIndex Enum (C)

```c
typedef enum {
    UZ_VIEW_MODE_LIT = 0,
    UZ_VIEW_MODE_UNLIT,
    UZ_VIEW_MODE_WIREFRAME,
    UZ_VIEW_MODE_LIGHTING_ONLY,
    UZ_VIEW_MODE_BUFFER_VISUALIZATION,  // G-Buffer channels
    UZ_VIEW_MODE_SHADER_COMPLEXITY,
    UZ_VIEW_MODE_LOD_COLORATION,
    UZ_VIEW_MODE_LIGHT_COMPLEXITY,
    // ... more modes ...
    UZ_VIEW_MODE_MAX
} uz_view_mode_t;
```

#### 2. Define Show Flags Bitset

```c
typedef struct {
    uint64 bRenderGeometry : 1;
    uint64 bRenderLighting : 1;
    uint64 bRenderMaterials : 1;
    uint64 bRenderPostProcess : 1;
    uint64 bVisualizeBuffer : 1;
    uint64 bVisualizeNanite : 1;
    uint64 bVisualizeLOD : 1;
    uint64 bVisualizeComplexity : 1;
    // ... more flags ...
} uz_show_flags_t;
```

#### 3. Buffer Visualization Configuration (TOML)

```toml
[visualization.buffers]
channels = [
    { id = "base_color", material = "viz_base_color.uzmat", label = "Base Color" },
    { id = "metallic", material = "viz_metallic.uzmat", label = "Metallic" },
    { id = "roughness", material = "viz_roughness.uzmat", label = "Roughness" },
    { id = "world_normal", material = "viz_normal.uzmat", label = "World Normal" },
    { id = "scene_depth", material = "viz_depth.uzmat", label = "Scene Depth" },
]

overview_order = ["base_color", "metallic", "roughness", "world_normal", "scene_depth", "", "", ""]
# Empty strings = gaps in grid
```

#### 4. View Mode Apply Function

```c
void uz_apply_view_mode(uz_view_mode_t mode, uz_show_flags_t* out_flags) {
    // Clear existing flags
    memset(out_flags, 0, sizeof(uz_show_flags_t));

    switch (mode) {
        case UZ_VIEW_MODE_LIT:
            out_flags->bRenderGeometry = 1;
            out_flags->bRenderLighting = 1;
            out_flags->bRenderMaterials = 1;
            out_flags->bRenderPostProcess = 1;
            break;
        case UZ_VIEW_MODE_UNLIT:
            out_flags->bRenderGeometry = 1;
            out_flags->bRenderMaterials = 1;
            out_flags->bRenderPostProcess = 0;
            break;
        case UZ_VIEW_MODE_BUFFER_VISUALIZATION:
            out_flags->bVisualizeBuffer = 1;
            out_flags->bRenderPostProcess = 0;
            break;
        case UZ_VIEW_MODE_SHADER_COMPLEXITY:
            out_flags->bVisualizeComplexity = 1;
            out_flags->bRenderPostProcess = 0;
            break;
        // ... more modes ...
    }
}
```

#### 5. Buffer Visualization Manager

```c
typedef struct {
    uz_material_handle_t material;
    const char* label;
    bool apply_auto_exposure;
} uz_buffer_viz_channel_t;

typedef struct {
    uz_hashmap_t channels;  // FName → uz_buffer_viz_channel_t
    uz_array_t overview_order;  // Array of FName in grid order
    uint32_t grid_cols, grid_rows;  // 4x5, 5x4, etc.
} uz_buffer_viz_manager_t;

uz_buffer_viz_manager_t* uz_buffer_viz_create();
uz_material_handle_t uz_buffer_viz_get_material(uz_buffer_viz_manager_t*, const char* channel_id);
void uz_buffer_viz_render_overview(uz_render_context_t*, uz_buffer_viz_manager_t*);
```

#### 6. Post-Process Integration

```cpp
// In render_scene.cpp
if (show_flags.bVisualizeBuffer) {
    // Get current visualization material from config
    uz::MaterialHandle mat = buf_viz_mgr->GetMaterial(current_channel);

    // Apply as post-process
    render_graph->AddPass<FApplyPostProcessMaterialPass>(
        mat,
        scene_color_hdr,
        out_target
    );
} else if (show_flags.bVisualizBufferOverview) {
    // Render all materials in grid layout
    render_buffer_viz_overview(render_graph, buf_viz_mgr, scene_color_hdr, out_target);
}
```

---

## 9. Key Files & Line References

| File | Lines | Purpose |
|------|-------|---------|
| `EngineBaseTypes.h` | ~100–180 | EViewModeIndex enum (37 modes) |
| `DebugViewModeHelpers.h` | 22–38 | EDebugViewShaderMode enum (13 modes) |
| `BufferVisualizationData.h` | 7–98 | Buffer visualization manager |
| `BufferVisualizationData.cpp` | 29–215 | INI loading, material registry |
| `BaseEngine.ini` | ~3100–3125 | Buffer visualization channel definitions |
| `DebugViewModeRendering.h` | 42–213 | Debug view mode shaders & processors |
| `DebugViewModeRendering.cpp` | 1–500+ | Debug shader compilation |
| `PostProcessing.cpp` | 420–1950 | Post-processing pipeline, overview pass |
| `PostProcessBufferInspector.h/.cpp` | - | Per-pixel buffer inspection (separate system) |
| `ShowFlags.cpp` | - | ApplyViewMode() show flag configuration |

---

## 10. Summary: Why This Matters

UE5's view mode system demonstrates:

1. **No runtime shader switching** — Instead, uses pre-compiled permutations (debug shaders) + show flags to control rendering behavior.
2. **Material-based visualization** — Buffer channels are post-process materials, not shader code. This is highly modular and extensible.
3. **Configuration over code** — Define visualization channels in INI, not C++. Makes it easy to add new debug channels without recompiling.
4. **Composite rendering** — Multiple visualizations are layered (show flags control which features render, then post-process overlays results).
5. **Grid composition** — The overview mode iterates a list of materials and renders each to a screen tile. Simple but effective.

**For UzEngine**: Adopt the configuration-driven approach, but simplify:
- Use TOML/JSON instead of INI (simpler to parse)
- Implement view mode enum with ~15 essential modes (not 37)
- Use post-process materials for G-Buffer visualization
- Defer debug shader compilation (compile on-demand, cache results)
- Implement grid overview with CSS-like layout (not hardcoded grid math)

