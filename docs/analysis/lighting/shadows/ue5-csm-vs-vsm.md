<!-- doc-meta
title: CSM vs VSM for Interior Lighting — Technical Comparison
created: 2026-04-06
status: reference
audience: rendering engineers
keywords: shadow cascades, virtual shadow maps, clipmaps, interior shadows
-->

# Cascaded Shadow Maps vs Virtual Shadow Maps for Interior Lighting

## Problem Statement

**Cascaded Shadow Maps (CSM)** are optimized for **outdoor scenes**:
- Sun is distant, parallel light
- Camera is in open space
- Shadow quality degrades with distance from camera

**Interior scenes** have different requirements:
- Sun blocked by walls
- Camera surrounded by geometry
- Need **high-res shadows NEAR camera** (not far away)

**Virtual Shadow Maps (VSM)** address this through camera-relative clipmaps.

---

## Cascaded Shadow Maps (CSM) — Current UzEngine

### Architecture

```
┌─ DirectionalLight (Sun)
│  ├─ Renders shadow depth from light's perspective
│  └─ Creates depth pyramid
│
├─ Camera View
│  └─ Extracts view frustum
│
├─ Cascade 0 (Nearest, High Res)
│  ├─ Coverage: Camera +/- 500cm
│  ├─ Resolution: 2048x2048
│  └─ Per-pixel shadow detail: High
│
├─ Cascade 1 (Medium, Medium Res)
│  ├─ Coverage: Camera +/- 2000cm
│  ├─ Resolution: 2048x2048 (same hardware, covers larger area)
│  └─ Per-pixel shadow detail: Medium
│
└─ Cascade 2 (Far, Low Res)
   ├─ Coverage: Camera +/- 10000cm
   ├─ Resolution: 2048x2048
   └─ Per-pixel shadow detail: Very Low
```

### Properties (UE5 DirectionalLightComponent)

**From DirectionalLightComponent.h, lines 60-99**:

```cpp
// How many distance buckets to subdivide view frustum
UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category=CascadedShadowMaps, 
    meta=(UIMin = "0", UIMax = "4"))
int32 DynamicShadowCascades = 4;

// Distance each cascade covers (from camera)
UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category=CascadedShadowMaps, 
    meta=(UIMin = "0", UIMax = "20000"))
float DynamicShadowDistanceMovableLight = 5000.0f;

// How cascades are distributed (1 = proportional to resolution, 4 = cluster near camera)
UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category=CascadedShadowMaps, 
    meta=(UIMin = "1", UIMax = "4"))
float CascadeDistributionExponent = 2.0f;

// Fade between cascades (0 = hard transition, 0.3 = 30% overlap)
UPROPERTY(EditAnywhere, BlueprintReadOnly, interp, Category=CascadedShadowMaps, 
    meta=(UIMin = "0", UIMax = "0.3"))
float CascadeTransitionFraction = 0.2f;
```

### How It Fails Indoors

**Example: Sponza Atrium**

```
Configuration:
  DynamicShadowDistanceMovableLight = 5000cm
  DynamicShadowCascades = 3
  CascadeDistributionExponent = 2.0

Result:
  Cascade 0: 0-500cm (high res)
  Cascade 1: 500-1500cm (medium res)
  Cascade 2: 1500-5000cm (low res)
  Beyond 5000cm: No shadows (interior far wall invisible)
```

**Interior Atrium Walls** (example):
- **Ground to ceiling**: ~1000cm
- **Furthest wall**: ~800cm away horizontally
- **Viewport**: From center looking at wall

**CSM Result**:
- Walls fall into **Cascade 1 or 2** (medium/low res)
- Sun doesn't reach interior (in shadow = value 0)
- **All interior surfaces are uniformly dark**

**Why**: CSM bounds are computed from sun-visible geometry. Interior surfaces are **occluded from sun**, so they all sample as "in shadow". CSM can't distinguish between "in shadow" and "unreachable by sun".

### CSM Strengths

✓ Simple to understand  
✓ Predictable performance (fixed resolution overhead)  
✓ Works well outdoors (optimal resolution distribution)  
✓ Standard, well-documented  

### CSM Weaknesses

✗ **Wasted resolution outdoors** (Cascade 2 covers 1500-5000cm, but interior is only 500cm tall)  
✗ **All-or-nothing for occluded surfaces** (interior = fully shadowed, no gradations)  
✗ **Requires supplementary systems** (sky light, Lumen) to light indoors  
✗ **Banding artifacts at cascade transitions** (needs fade regions, adds cost)  

---

## Virtual Shadow Maps (VSM) — Superior for Mixed Scenes

### Architecture (Camera-Relative Clipmap)

```
┌─ Camera Position
│  └─ CLIPMAP ORIGIN (moves with camera)
│
├─ Clipmap Level 0 (Nearest, Highest Res)
│  ├─ Virtual extent: Camera ±256cm
│  ├─ Physical resolution: 2048×2048 (virtual paging)
│  └─ Per-texel coverage: ~0.25cm
│
├─ Clipmap Level 1
│  ├─ Virtual extent: Camera ±1024cm
│  ├─ Per-texel coverage: ~1cm
│  └─ Scrolls with camera (tiles update only at edges)
│
├─ Clipmap Level 2
│  ├─ Virtual extent: Camera ±4096cm
│  └─ Per-texel coverage: ~4cm
│
└─ Clipmap Level 3 (Furthest, Coarsest Res)
   ├─ Virtual extent: Camera ±16384cm
   └─ Per-texel coverage: ~16cm
```

### Key Innovation: Virtual Paging

**From VirtualShadowMapArray.h, lines 69-85**:

```cpp
static constexpr uint32 PageSize = 128;           // Texels per page
static constexpr uint32 Level0DimPagesXY = 128;   // 128×128 pages = 16k virtual
static constexpr uint32 MaxMipLevels = 8;
static constexpr uint32 VirtualMaxResolutionXY = 16384;
static constexpr uint32 PhysicalPageAddressBits = 16;  // Max 2^16 pages physical
static constexpr uint32 MaxPhysicalTextureDim = 1U << PhysicalPageAddressBits;  // 65536 texels
```

**Implication**:
- Virtual address space: 16384×16384 texels (huge!)
- Physical memory: Only 65536×65536 texels across all clipmaps
- **Paging system**: Only render pages that are visible/accessed

### How VSM Solves Interior Lighting

**Same Sponza Example**:

```
Configuration:
  Clipmap origin = camera position (at atrium center)
  Level 0: Camera ±256cm (ultra-high res)
  Level 1: Camera ±1024cm
  ...

Result:
  All atrium geometry: Levels 0-2 (high to medium res)
  Beyond atrium: Levels 3+ (coarse res, if reached)
```

**Interior Rendering**:
- **Sun blocks interior** → surfaces in shadow = value 0 ✓ (correct)
- **But shadow detail is HIGH** → exact contact shadows visible ✓
- **Adaptive resolution** → nearby geometry gets ultra-high res ✓

**Outdoor View Transition**:
- Walk outside → clipmap origin follows
- Outdoor geometry moves into higher-res levels automatically ✓
- **No cascade transitions** → seamless coverage ✓

### VSM Strengths

✓ **Camera-relative**: Adaptive resolution everywhere (no wasted res in far cascades)  
✓ **Seamless**: No hard cascade transitions (single continuous clipmap space)  
✓ **Efficient**: Only visible pages rendered (virtual paging)  
✓ **Works indoors + outdoors**: Same algorithm for both  
✓ **Hardware acceleration**: Nanite + VSM = perfect pair  

### VSM Weaknesses

✗ More complex (paging system, virtual addressing)  
✗ Requires GPU support (compute shaders, indirect dispatch)  
✗ Memory slightly higher (page tables, allocation tracking)  
✗ Longer development cycle (research-grade complexity)  

---

## Comparative Table

| Aspect | CSM | VSM |
|--------|-----|-----|
| **Resolution Distribution** | Exponential (far cascades waste pixels) | Linear (adaptive per level) |
| **Outdoor Shadows** | Excellent (optimized for this) | Excellent (same quality, more detail) |
| **Indoor Shadows** | Poor (all-or-nothing, needs sky light fill) | Excellent (high-res despite occlusion) |
| **Transition Artifacts** | Banding (between cascades) | None (continuous space) |
| **Clipmap Scrolling** | N/A (light-relative, static) | Yes (camera-relative, dynamic) |
| **Per-Object Shadows** | Yes (inset shadows, fallback) | Yes (single-page VSM) |
| **Page Caching** | N/A | Yes (separate static/dynamic) |
| **Development Complexity** | Low (well-established) | High (research, advanced) |
| **Hardware Requirement** | DX11+ | DX12+ compute shaders |

---

## Interior-Specific Features: VSM vs CSM

### Feature: Contact Shadows in Tight Spaces

**CSM**:
- Cascade 0 covers 500cm
- In a 300cm-wide corridor, resolution = 2048px / 500cm = 4.1 px/cm
- Contact shadows visible but coarse

**VSM**:
- Level 0 covers 256cm
- In a 300cm-wide corridor, resolution = 2048px / 256cm = 8 px/cm
- Contact shadows are **2x more precise**

### Feature: Smooth Exterior-to-Interior Transitions

**CSM**:
- Outdoor area: Cascade 2 (low res)
- Walk into doorway: Jump to Cascade 0
- **Visible pop** in shadow quality at threshold

**VSM**:
- Outdoor area: Level 3 (coarse)
- Walk toward doorway: Levels 1, 2 (automatic refinement)
- Smooth, **no visible transitions**

### Feature: Dynamic Objects Indoors

**CSM**:
- Moving object between cascades = shadow quality pop
- Requires inset shadows as fallback (extra rendering)

**VSM**:
- Moving object stays in high-res region automatically
- Single-page shadow maps for small objects
- **No pops, no extra costs**

---

## Implementation Recommendation for UzEngine

### Near Term (Maintain CSM)

**Keep CSM** if:
- Most scenes are **outdoor** or **open spaces**
- Performance budget is tight
- Team unfamiliar with advanced techniques

**Mitigation for interiors**:
- Lower `DynamicShadowDistance` for interior levels (3000cm vs 10000cm outdoor)
- Use `bUseInsetShadowsForMovableObjects = true`
- **Mandatory**: Full SkyLight + DFAO system (provides interior fill)
- **Fallback**: Pre-bake interiors with Lightmass

### Long Term (Migrate to VSM)

**Adopt VSM** when:
- Building open-world with interior locations
- Targeting next-gen platforms (PS5, RTX hardware)
- Want seamless indoor-outdoor rendering
- Can afford 6-8 weeks architecture work

**Plan**:
1. Implement VSM as **alternative** shadow technique (dual render path)
2. Migrate outdoor scenes to VSM first (lower risk, immediate benefit)
3. Add VSM-specific features (static/dynamic page caching)
4. Retire CSM in phases

---

## Hybrid Approach: CSM + Supplementary Systems

**Most pragmatic for mixed scenes** (current UzEngine recommendation):

```
┌─ Directional Light
│  └─ CSM (2-3 cascades, 5000cm distance)
│
├─ SkyLight
│  ├─ Captures sky irradiance
│  └─ Provides ambient fill (interior default)
│
├─ Distance Field AO
│  ├─ Computes contact shadows
│  └─ Darkens corners (interior detail)
│
└─ Lumen GI
   ├─ Real-time bounces
   └─ Color variation (interior walls lit by sky color)
```

**Interior Rendering Pipeline**:
```
Pixel color = 
  Direct Sun (CSM) → 0 if interior
  + Sky Light Irradiance (SH) * (1 - DFAO)
  + Lumen Bounces
  = Correctly lit interior (no special case)
```

**Advantages**:
- No engine rewrite (keep CSM)
- Incremental improvements (add each system independently)
- Proven approach (UE5 uses this exact stack)
- Scales from indie to AAA

---

## References

- **CSM Design**: ShadowRendering.h (UE5 source)
- **VSM Architecture**: VirtualShadowMapArray.h (UE5 source)
- **Clipmap Theory**: Losasso & Hoppe 2004, "Geometry Clipmaps"
- **Virtual Paging**: Nanite whitepaper (Epic Games)
- **Interior Lighting**: This analysis series

---

**Conclusion**: For UzEngine's current trajectory, **improve CSM with SkyLight + Lumen + DFAO** (achievable in 2-3 months). **Plan VSM migration** for Phase 2 when open-world system is ready.

**File**: `E:/UzEngine/docs/analysis/rendering/ue5-csm-vsm-indoor-comparison.md`  
**Status**: Ready for implementation planning
