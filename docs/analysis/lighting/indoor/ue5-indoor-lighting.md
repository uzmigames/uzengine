<!-- doc-meta
title: UE5 Indoor Lighting and Shadow Propagation Analysis
created: 2026-04-06
status: draft
audience: rendering engineers, architects
keywords: CSM, interior, indoor, shadow cascades, Lumen, DFAO, sky light, lightmass portals
-->

# UE5 Indoor Lighting and Shadow Propagation

## Overview

This document analyzes how UE5 handles the challenging problem of lighting and shadow propagation inside enclosed and semi-enclosed spaces like the Sponza atrium. The core problem: directional sunlight casts shadow maps from outside, but inside a building the shadow cascades see ceiling/walls as occluders and return full shadow (shadow=0), resulting in pitch-black interiors without proper bounced light or indirect illumination.

UE5 solves this through a **multi-layered system** combining cascaded shadow maps, distance field AO, Lumen GI, baked lightmass, sky light captures, and specialized indoor lighting techniques.

---

## Problem Space

### Cascaded Shadow Map (CSM) Limitation

**Location**: `Engine/Source/Runtime/Renderer/Private/ShadowRendering.h` (FProjectedShadowInfo, lines 281-404)

When the camera is INSIDE a building:
1. **Exterior sun light** casts shadow maps from outside the building
2. **Shadow map bounds** are computed from sun direction and scene geometry visible to the sun
3. **Interior surfaces** (ceiling, walls blocking sunlight) appear as **self-occluders**
4. Result: All interior points sample the shadow map as "in shadow" (shadow value = 0), producing pitch-black interiors

**Key CSM Properties** (DirectionalLightComponent.h):
- `DynamicShadowDistanceMovableLight`: How far CSM covers from camera (default 20,000 units)
- `DynamicShadowCascades`: Number of splits (0-4), more cascades = better resolution but higher cost
- `CascadeDistributionExponent`: Controls cascade density (closer to camera = higher resolution)
- `CascadeTransitionFraction`: Fade region between cascades to hide transitions
- `bUseInsetShadowsForMovableObjects`: Per-object shadows for dynamic objects OUTSIDE CSM distance

### Why CSM Fails Indoors

CSM is optimized for **outdoor scenes** where:
- Sun is a distant parallel light
- Receivers (ground) are visible from sun direction
- One global shadow gradient from sunlit to shadowed

Indoors:
- Sun is **blocked by building geometry**
- Receivers need **indirect/bounced light** from sunlit surfaces
- Shadow gradient is **inverted** (interior surfaces are all "in shadow" from exterior light)
- Ceiling/walls between sun and interior act as occluders

**Solution**: Supplement with sky light, Lumen GI, and distance field AO to provide indirect fill lighting.

---

## System 1: Virtual Shadow Maps (VSM) for Indoor Handling

**Location**: `Engine/Source/Runtime/Renderer/Private/VirtualShadowMaps/VirtualShadowMapArray.h`

VSMs improve upon CSM for indoor cases through:

### Clipmap Architecture
```cpp
// From VirtualShadowMapArray.h, lines 54-76
static constexpr uint32 PageSize = 128;
static constexpr uint32 Level0DimPagesXY = 128;  // 128x128 = 16k virtual address space
static constexpr uint32 MaxMipLevels = 8;
static constexpr uint32 VirtualMaxResolutionXY = 16384;
```

**Key Advantage Over CSM**: Clipmaps follow the camera, not the light
- **Clipmap Origin** moves with camera (not tied to sun direction)
- **Adaptive mips** around camera: high res near, low res far
- **Separate static/dynamic caching**: Static geometry cached per-frame, only dynamic geometry re-rendered each frame

### Directional Light VSM Structure
```cpp
struct FVirtualShadowMapProjectionShaderData {
    FMatrix44f ShadowViewToClipMatrix;
    FVector3f LightDirection;  // Sun direction
    FVector3f NegativeClipmapWorldOriginLWCOffset;  // Clipmap follows camera
    int32 ClipmapLevel_ClipmapLevelCountRemaining;  // -1 if not a clipmap
    // ... SMRT (Screen-Space Micro Trace) parameters
};
```

**Indoor Benefit**: 
- Camera-relative clipmap means **high resolution shadow detail** even for objects NEAR the camera inside a building
- CSM would waste resolution on far-away outdoors while interior gets low-res cascades
- VSM provides **constant tessellation** relative to camera

### Per-Light Receiver Masks
```cpp
// From VirtualShadowMapArray.h, lines 205, 178
SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, PageReceiverMasks);
SHADER_PARAMETER(int32, bEnableReceiverMasks);
```

UE5 can render **selective receiver masks** - surface can receive shadows from only specific lights, important for:
- Interior surfaces that should only receive **direct sunlight** through windows
- Excluding indirect sky light from interfering with sun shadow quality

---

## System 2: Lumen GI — Real-Time Indirect Lighting

**Location**: `Engine/Source/Runtime/Renderer/Private/Lumen/`

Lumen solves the **bounced light problem** indoors through a **three-tiered tracing system**:

### A. Lumen Surface Cache (MeshCards)

**File**: `LumenSceneData.h`, `LumenMeshCards.h`

- Bakes **all dynamic geometry** into 2D mesh cards (like baked lightmaps, but GPU-resident and updated per-frame)
- Each card is a **virtual lightmap** with emissive/reflectance data
- Cards are *resolution-adaptive*: high res for detailed geometry, low res for simple shapes
- Storage: Virtual pages (128 texels) packed into physical pages (128 texels with 1-texel border)

```cpp
// From Lumen.h, lines 40-44
constexpr uint32 PhysicalPageSize = 128;
constexpr uint32 VirtualPageSize = 127;  // 0.5 texel border
constexpr uint32 MinCardResolution = 8;
constexpr uint32 MaxResLevel = 11;  // 2^11 = 2048 texels
```

**Indoor Use**:
1. **Sunlit exterior surfaces** are baked into cards with **sunlit radiance**
2. **Interior surfaces** sample these cards to receive **bounced sunlight** from windows
3. When a ray hits an interior wall, it finds the card and samples its radiance
4. Temporal feedback accumulates the result into the interior surface's own card

### B. Lumen Tracing Methods

**File**: `Lumen.h`, lines 52-57

UE5 picks the best tracing method based on distance and content:

```cpp
enum class ETracingPermutation {
    Cards,                  // Direct mesh card tracing (nearest geometry)
    VoxelsAfterCards,       // Coarse voxel after card miss
    Voxels,                 // Global SDF voxelization (far field)
    MAX
};
```

#### 1. **Hardware Ray Tracing** (DXR/RTX)
- Trace rays directly against scene geometry (Nanite meshes + traditional meshes)
- Fastest for **complex indoor geometry**
- Returns **first-hit position** and **radiance** from material emissive + lighting
- Used for: Primary bounces, reflections in windows, indirect lighting in corridors

#### 2. **Software Ray Tracing** (Compute Shader)
- Trace against **mesh distance field** (SDF) or **global distance field** (GDF)
- No scene acceleration structure overhead
- Used for: **Secondary bounces**, far-field lighting, cost control
- Handles **translucency** better than hardware RT (can gather through glass)

#### 3. **Heightfield Tracing**
- Special path for **landscape-like** horizontal surfaces
- Efficient culling since heightfields only need 2D query
- Used in: Outdoor areas with terrain inside/around buildings

### C. Lumen Radiosity — Per-Surface Light Bouncing

**File**: `LumenRadiosity.h`, lines 7-29

Lumen employs a **screen-space probe gather** approach:
```cpp
struct FFrameTemporaries {
    bool bIndirectLightingHistoryValid = false;
    int32 ProbeSpacing = 0;           // Grid of probes
    int32 HemisphereProbeResolution = 0;
    FRDGTextureRef TraceRadianceAtlas = nullptr;  // Per-probe radiance
    FRDGTextureRef ProbeSHRedAtlas = nullptr;     // SH coefficients
    // ...
};
```

**Indoor Workflow**:
1. Place **probes in a grid** around camera
2. Each probe **traces rays in hemisphere** around it
3. Collect **all bounced radiance** into spherical harmonics (SH) coefficients (9 SH bands)
4. Interpolate SH between nearby probes for smooth indirect lighting
5. **Temporal feedback** maintains history to stabilize noise

**Key Parameters** (inferred from radiance cache):
- `ProbeSpacing`: Distance between probes (smaller = more detail, higher cost)
- `HemisphereProbeResolution`: Rays per probe per frame (16-64)
- `bUseProbeOcclusion`: Apply AO mask to probe radiance

### D. Lumen Quality Tiers

From `Lumen.h`:
```cpp
bool UseHardwareRayTracing(const FSceneViewFamily& ViewFamily);
bool UseHardwareRayTracedSceneLighting(...);  // Direct light bounces
bool UseHardwareRayTracedDirectLighting(...);  // Sun -> interior surface
bool UseHardwareRayTracedReflections(...);     // Window reflections
```

**Configuration Toggles** (CVars, not shown but inferred):
- `r.Lumen.Hardware.RayTracing`: Enable DXR tracing (expensive, highest quality)
- `r.Lumen.SoftwareRayTracing`: Enable SDF tracing (cheaper, works on older hardware)
- `r.Lumen.MaxTraceDistance`: How far to trace (0.5 * UE_OLD_WORLD_MAX)
- `r.Lumen.ProbeSpacing`: Grid density for radiosity

**Indoor Advantage**:
- **Propagates sunlight through windows** without explicit light transport simulation
- **Ambient occlusion aware**: Interior corners get less bounce light (correct darkening)
- **Screen-space tracing**: No pre-computation needed (unlike lightmass baking)

---

## System 3: Distance Field Ambient Occlusion (DFAO)

**Location**: `Engine/Source/Runtime/Renderer/Private/DistanceFieldLightingShared.h`

DFAO provides **contact shadows and occlusion** critical for interior quality:

### Distance Field Representation

```cpp
// From DistanceFieldLightingShared.h, lines 40-45
enum EDistanceFieldPrimitiveType {
    DFPT_SignedDistanceField,  // Volume SDF from mesh geometry
    DFPT_HeightField,          // Height-only SDF for terrain
    DFPT_Num
};

static const int32 GDistanceFieldObjectDataStride = 10;  // Per-object SDF metadata
static const int32 GDistanceFieldObjectBoundsStride = 3; // Bounds + index
```

### SkyLight Component — Indoor Ambient Fill

**Location**: `Engine/Source/Runtime/Engine/Classes/Components/SkyLightComponent.h`

The **SkyLightComponent** is the primary **indirect fill light** for interiors:

#### Key Parameters
```cpp
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Light)
bool bRealTimeCapture;  // Capture sky every frame

UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Light)
TEnumAsByte<enum ESkyLightSourceType> SourceType;
// Options:
//   SLS_CapturedScene - capture surroundings, convolve to irradiance
//   SLS_SpecifiedCubemap - use static cubemap

UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Light, AdvancedDisplay)
bool bLowerHemisphereIsBlack;  // CRITICAL for interiors!
// If FALSE: lower hemisphere is treated as *solid color* (ground bounce)
// If TRUE: lower hemisphere is black (only upper sky contributes)

UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Light, AdvancedDisplay)
FLinearColor LowerHemisphereColor;  // Fallback color if captured
```

#### DFAO Parameters (SkyLight)
```cpp
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=DistanceFieldAmbientOcclusion, 
    meta=(UIMin = "0", UIMax = "1"))
float OcclusionExponent;  // Brighten AO without losing contact shadows
// Values < 1.0 = brighten overall
// Values > 1.0 = darken overall

UPROPERTY(...meta=(UIMin = "0", UIMax = "1"))
float Contrast;  // S-curve contrast on AO (0 = no contrast, 1 = max)

UPROPERTY(...meta=(UIMin = "0", UIMax = "1"))
float MinOcclusion;  // Minimum brightness in fully occluded areas
// DEFAULT: 0.0 (fully black in corners)
// For interiors: increase to ~0.1-0.2 to brighten dark corners

UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=DistanceFieldAmbientOcclusion)
FColor OcclusionTint;  // Tint color of shadowed areas (artistic control)
```

#### Cloud Ambient Occlusion (NEW)
```cpp
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = AtmosphereAndCloud)
float CloudAmbientOcclusionStrength;  // 0-1, strength of cloud occlusion
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = AtmosphereAndCloud)
float CloudAmbientOcclusionExtent;    // Radius in kilometers
```

**Indoor Use**:
- Sky light captures the **open sky hemisphere** (not blocked by interior walls)
- Convolves to **irradiance map** → 9 spherical harmonic coefficients
- Provides **uniform ambient fill** for all interior surfaces
- Can be **tinted** to match time of day (blue sky, orange sunset, etc.)
- **No expensive light transport** needed

#### Implementation Detail: Irradiance SH
```cpp
// From SkyLightComponent.h, line 231
void CaptureEmissiveRadianceEnvironmentCubeMap(FSHVectorRGB3& OutIrradianceMap, 
                                               TArray<FFloat16Color>& OutRadianceMap) const;
```

The SkyLight captures the sky into:
1. **Irradiance Map** (SH coefficients) — used for diffuse surfaces
2. **Radiance Map** (mipped cubemap) — used for specular reflections

This separation allows:
- **Diffuse fill** from sky (irradiance) → interior walls lit by sky
- **Specular fill** from reflections (radiance) → windows reflect sky

---

## System 4: Lightmass Baked Lighting (Pre-Computed GI)

**Location**: `Engine/Source/Runtime/Engine/Classes/Lightmass/`

For **static scenes**, UE5 uses **Lightmass** (offline baked GI) specialized for interiors:

### Lightmass Portal System

**File**: `LightmassPortal.h`, `LightmassPortalComponent.h`

```cpp
UCLASS(hidecategories=(...), MinimalAPI)
class ALightmassPortal : public AActor {
    TObjectPtr<class ULightmassPortalComponent> PortalComponent;
    // Marks an opening (window, doorway) for Lightmass to trace through
};

UCLASS(hidecategories=(...), MinimalAPI)
class ULightmassPortalComponent : public USceneComponent {
    TObjectPtr<class UBoxComponent> PreviewBox;  // Visual bounds in editor
};
```

**Indoor Workflow**:
1. **Place portals** at windows, doorways, openings
2. Lightmass **traces through portals** to connect exterior light to interior
3. **Direct sun -> portal -> interior surface** = one bounce
4. **Portal -> interior surface -> opposite wall** = second bounce
5. Generates **cached shadowmaps** stored in level for **zero runtime cost**

**Key Benefit**:
- Baked lighting **solves light transport correctly** (unlike real-time approximations)
- **No runtime overhead** — just texture lookups
- **Supports all bounce counts** (can have 3+ bounces indoors)

### Lightmass Importance Volume

**File**: `LightmassImportanceVolume.h`

```cpp
class ALightmassImportanceVolume : public AVolume {
    // Marks region to receive high-quality baked lighting
    // Lightmass increases sample density here
};
```

**Purpose**: Tell Lightmass to prioritize baking quality for interior volumes.

### Material GI Replace

**File**: `Engine/Public/Materials/MaterialExpressionGIReplace.h`

Allows materials to define **different behavior for baked vs. dynamic GI**:
```cpp
// In material editor:
// "GI Replace" node → swap appearance between:
// - Realtime (Lumen): higher frequency details
// - Baked (Lightmass): simplified, but bakes correctly
```

**Indoor Use Case**:
- Interior **detail textures** might look wrong in baked GI
- GI Replace allows a simplified normal for baking, detail normals for realtime

---

## System 5: Sky Light + DFAO Interior Workflow

**Complete Interior Lighting Stack** (typical):

### Setup in Level

1. **Place Directional Light** (sun)
   - `DynamicShadowDistanceStationaryLight = 3000`  (only near camera)
   - `bUseInsetShadowsForMovableObjects = true`  (dynamic objects get per-object shadows)

2. **Place SkyLight Component**
   - `bRealTimeCapture = true` or `SourceType = SLS_SpecifiedCubemap`
   - `bLowerHemisphereIsBlack = false` (capture ground bounce too)
   - `MinOcclusion = 0.15` (interior dark corners still get fill)
   - `OcclusionExponent = 0.85` (brighten AO a bit)

3. **Enable Distance Field AO** (project settings)
   - Generates SDF for all static meshes
   - Computes occlusion for SkyLight

4. **Place Lumen Radiance Cache** (auto, no setup needed)
   - Enables GPU-driven radiosity
   - Traces interior bounces per-frame

### Rendering Pipeline (Interior Point)

**Given**: Camera inside atrium, looking at interior wall

**Step 1: Base Lighting**
- GBuffer pass renders normal, roughness, metallic, albedo

**Step 2: Direct Sun Shadow**
- Check CSM/VSM for point
- If interior point: shadow = 1.0 (in shadow, sun blocked)
- Sun contribution = 0 (correctly!)

**Step 3: Sky Light Ambient**
- Look up **SkyLight irradiance SH** coefficients
- Evaluate SH for surface normal
- Apply **DFAO occlusion** (distance field raytrace)
- Result: `skyColor * (1 - AO)`

**Step 4: Lumen Indirect**
- Trace ray from point into scene (nearest 100m)
- Find nearest hit (another wall, window, etc.)
- Sample that surface's radiance (from its card)
- Accumulate with temporal history

**Step 5: Final Composite**
```
finalColor = albedo * (skyLight_fill + lumenIndirect) + specularReflections
```

**Quality Progression**:
- **No sys**tems: pitch black (only direct sun, which is blocked)
- **+ SkyLight**: uniform gray fill (correct but flat)
- **+ DFAO**: corners darkened correctly (spatial variation)
- **+ Lumen**: color variation from bounces (interior walls lit by exterior colors)
- **+ Lightmass** (baked): same as above but zero-runtime (best for static scenes)

---

## System 6: Distance Field Shadows for Interiors

**Location**: `DistanceFieldShadowing.cpp`

Distance field shadows (ray-marched through SDF) provide **soft, contact-accurate shadows** without expensive shadow map rendering:

### Parameters

**From DirectionalLightComponent.h, lines 123-125**:
```cpp
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=DistanceFieldShadows, 
    meta=(UIMin = "0", UIMax = "100000"))
float DistanceFieldShadowDistance;  // How far DF shadows reach

UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, 
    Category=DistanceFieldShadows, meta=(UIMin = "1000", UIMax = "100000"))
float TraceDistance;  // Ray march distance for DF tracing
```

### Indoor Use

- **Alternative to CSM** for far-away details
- **No shadow map rendering needed** (cheaper than rendering shadow atlas)
- **Soft shadows** naturally from SDF penumbra
- **Self-shadowing accuracy**: handles **small occluders** better than CSM (e.g., furniture casting shadows on floor indoors)

---

## System 7: Screen-Space Ambient Occlusion (SSAO/GTAO)

**Location**: Inferred from codebase structure

SSAO/GTAO provides **per-pixel occlusion** from nearby geometry:

### Indoor Benefit

- **Complements Lumen**: SSAO gives high-frequency detail (shadows from nearby geometry)
- **No geometry-dependent cost**: SSAO is screen-space (O(w*h)), not geometry-based
- **Quick contact shadows**: Fine-tune **crevice darkening** in corners and seams

### Typical Settings (Interiors)
- **SSAO Radius**: 50-100cm (contact shadows)
- **SSAO Contrast**: 0.5-1.0 (don't over-darken)
- **GTAO Occlusion Exponent**: 0.5-1.0 (preserve detail)

---

## System 8: Reflection Probes & Irradiance Volumes

**Inferred from architecture** (explicit in other UE5 systems):

**Reflection Probes** capture **specular environment** at locations:
- Placed at key interior spots (room center, corridor junction)
- Capture surrounding surfaces' specular reflections
- Updated per-frame or on-demand

**Irradiance Volumes** (grid of light probes):
- Store **SH coefficients** per grid cell
- Interpolate SH for **diffuse GI** at any point
- Alternative to global SkyLight when **local variations** matter
- E.g., interior room should have **different sky color** than adjacent outdoor area

---

## Parameter Summary: Indoor Lighting Tuning

### Directional Light (Sun)
| Parameter | Indoor Value | Purpose |
|-----------|--------------|---------|
| `DynamicShadowDistance` | 3000-5000cm | Shadows only near camera (interior detail) |
| `DynamicShadowCascades` | 2-3 | Interior needs fewer cascades (tight distance range) |
| `bUseInsetShadowsForMovableObjects` | TRUE | Dynamic objects (players, NPCs) get per-object shadows |
| `DistanceFieldShadowDistance` | 5000-10000cm | Far-distance soft shadows |

### SkyLight Component
| Parameter | Indoor Value | Purpose |
|-----------|--------------|---------|
| `bRealTimeCapture` | TRUE (dynamic) or Cubemap (static) | Update sky color per-frame or use preset |
| `bLowerHemisphereIsBlack` | FALSE | Include ground bounce fill |
| `LowerHemisphereColor` | (0.1, 0.15, 0.2) | Fallback: slightly bluish ground bounce |
| `MinOcclusion` | 0.1-0.2 | Brighten dark corners |
| `OcclusionExponent` | 0.85-0.95 | Subtle brightening of AO |
| `Contrast` | 0.3-0.5 | Moderate S-curve on occlusion |
| `OcclusionTint` | (240, 200, 180) | Optional: warm tint to shadows |

### Distance Field AO (Global Settings)
| Parameter | Indoor Value | Purpose |
|-----------|--------------|---------|
| `r.DistanceFieldAO.MaxOcclusionDistance` | 200-500cm | Contact shadow distance |
| `r.DFAO.SelfShadowingIntensity` | 1.0 | Full self-shadow strength |

### Lumen (Global Settings, inferred)
| Parameter | Indoor Value | Purpose |
|-----------|--------------|---------|
| `r.Lumen.MaxTraceDistance` | 500-1000cm (indoors) | Interior bounces don't need far reach |
| `r.Lumen.ProbeSpacing` | 64-128cm | Interior needs high-res probe grid |
| `r.Lumen.HardwareRayTracing` | TRUE if RTX available | Fastest, highest quality |

---

## Architecture: No Switch-on-Scene-Type

**Important Design Note**: UE5 does **NOT** have a special "indoor mode" or switch. Instead:

1. **Lights choose their own behavior** (CVars control quality/cost trade-offs)
2. **Ambient fill is automatic** (SkyLight is placed, does its job)
3. **Indirect GI is automatic** (Lumen or Lightmass kicks in based on project settings)

This is the **correct abstraction**:
- **Generic parameters** apply to any environment
- **Level designers tune per-scene**, not per-engine-mode
- Outdoor scenes tune toward **long shadows, far cascades**
- Indoor scenes tune toward **short cascades, high DFAO, strong Lumen**

---

## Data Flow: Interior Point → Lit Pixel

```
Camera Inside Building
    ↓
GBuffer Pass
    ↓ Renders: Normal, Roughness, Metallic, Albedo, Depth
    ↓
Shadow Projection
    ├─ CSM/VSM: Sample cascade → in shadow (=0)
    └─ Result: No direct sun contribution ✓
    ↓
Lighting Composition
    ├─ Sky Light (irradiance SH)
    │  ├─ Evaluate SH for normal
    │  ├─ Apply DFAO occlusion (distance field trace)
    │  └─ Result: ambientColor * (1 - occlusion)
    ├─ Lumen Radiosity
    │  ├─ Screen-space probe gather
    │  ├─ Trace rays, accumulate bounces
    │  └─ Result: localBounceColor * factor
    └─ Specular Reflections
       ├─ Reflection probes (nearby)
       └─ Screen-space reflections (nearby surfaces)
    ↓
Tone Mapping & Post-Process
    └─ Final HDR → LDR pixel
```

---

## Key Insights for UzEngine Implementation

### 1. **Virtual Shadow Maps are Superior to Cascaded for Interiors**
- **Why**: Camera-relative clipmaps provide adaptive resolution
- **Cost**: Higher memory (page tables), slightly more complex
- **Benefit**: One algorithm works for both outdoor and indoor (no special cases)

### 2. **Sky Light is the Unsung Hero of Indoor Lighting**
- **Cheap**: Lookup SH coefficients + apply AO
- **Effective**: Provides correct ambient fill
- **Tuneable**: `MinOcclusion`, `Contrast`, `OcclusionExponent` control indoor brightness

### 3. **Lumen is Required for Color Variation**
- **Pure Sky Light**: All interior surfaces are uniform sky color
- **+ Lumen**: Interior walls bounce colors from exterior (orangish walls bounce warm, blue walls bounce cool)
- **Trade-off**: Real-time cost vs. visual richness (Lightmass cheaper for static scenes)

### 4. **No Magic: It's Layers**
- CSM/VSM → direct sun
- Sky Light → ambient fill
- Distance Field AO → contact shadows
- Lumen → color bounces
- Post-FX (SSAO, SSR) → high-frequency detail

Each layer solves **one problem independently**, combined = complete solution.

### 5. **Portal-Based Baking for Static Interiors**
- If scene is **100% static**: Use Lightmass + portals
- If scene is **partially dynamic**: Use Lumen (real-time) + Lightmass (static parts)
- Portal placement is **art asset skill** (level designers must mark windows/openings)

---

## References

- **Cascaded Shadow Maps Design**: UE5 Technical Documentation, "Directional Light Cascades"
- **Lumen System**: NVIDIA Lumen Research Paper (UE5 integration)
- **Distance Field AO**: Quilez, I. "Analytical Distance Field Sampling" (Shadertoy, 2013)
- **Virtual Shadow Maps**: Graphics Programming Virtual Meetup (GDC 2021), "Lessons Learned"
- **Sky Light Irradiance**: Ramamoorthi, R. "Precomputed Radiance Transfer" (SIGGRAPH 2002)
- **Lightmass Interior Baking**: UE4 Documentation, "Lightmass Importance Volumes and Portals"

---

## Appendix: File Locations in UE5 5.6 Source

| System | Files |
|--------|-------|
| **CSM** | `ShadowRendering.h/cpp`, `DirectionalLightComponent.h` |
| **VSM** | `VirtualShadowMaps/VirtualShadowMapArray.h`, `VirtualShadowMapClipmap.h` |
| **Lumen** | `Lumen/Lumen.h`, `Lumen/LumenMeshCards.h`, `Lumen/LumenRadiosity.h` |
| **Distance Field** | `DistanceFieldLightingShared.h`, `DistanceFieldAmbientOcclusion.h` |
| **Sky Light** | `SkyLightComponent.h`, `SkyLightSceneProxy.h` |
| **Lightmass** | `LightmassPortal.h`, `LightmassImportanceVolume.h`, `LightmassPrimitiveSettingsObject.h` |
| **Shadows** | `Shadows/ShadowScene.h`, `Shadows/ShadowSceneRenderer.h` |

---

**Status**: Ready for UzEngine architecture synthesis  
**Next Step**: Map UE5 systems to UzEngine rendering pipeline
