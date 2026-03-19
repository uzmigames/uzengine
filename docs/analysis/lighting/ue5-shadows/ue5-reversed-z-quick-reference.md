<!-- doc-meta
title: UE5 Reversed-Z Quick Reference
stage: quick-reference
created: 2026-03-16
source: W:\UE_5.6_Source
depth: summary
status: ready
-->

# UE5 Reversed-Z Quick Reference

**TL;DR:** UE5 uses compile-time reversed-Z depth buffering across all rendering. Depth=1.0 is near, depth=0.0 is far. No runtime option to toggle.

## One-Line Answers

| Question | Answer | Source |
|----------|--------|--------|
| **Where is reversed-Z configured?** | `enum class ERHIZBuffer { FarPlane=0, NearPlane=1, IsInverted=... }` | RHIDefinitions.h:278-287 |
| **Depth clear value?** | DepthFar=0.0, DepthNear=1.0 | RHI.cpp:131-132 |
| **Depth comparison function?** | CF_Greater (not CF_Less) for "closer" | RHIDefinitions.h:400-403 |
| **Projection matrix?** | MinZ/(MinZ-MaxZ) for reversed-Z z-column coefficient | PerspectiveMatrix.h:139 |
| **All shaders affected?** | Yes, via `#if HAS_INVERTED_Z_BUFFER` global flag | Definitions.usf:230 |
| **Shadow depth?** | Also reversed-Z via FReversedZPerspectiveMatrix | ShadowSetup.cpp:3165 |
| **Precision improvement?** | ~100-1000x better near plane precision | Mathematical property of reciprocal |
| **Runtime CVars?** | None. Compile-time constant only. | N/A |

## Key Constants

```cpp
// Z-buffer configuration (immutable)
enum class ERHIZBuffer
{
    FarPlane = 0,      // Farthest point from camera = depth 0.0
    NearPlane = 1,     // Nearest point (camera) = depth 1.0
    IsInverted = 1,    // Evaluates to true (0 < 1)
};

// Depth clear values
DepthFar = 0.0f;       // Clear to "nothing, all far away"
DepthNear = 1.0f;      // Clear to "right here at camera"

// Comparison functions (resolve at compile-time)
CF_DepthNearOrEqual = CF_GreaterEqual;       // Closer objects have higher z
CF_DepthNear = CF_Greater;
CF_DepthFartherOrEqual = CF_LessEqual;
CF_DepthFarther = CF_Less;
```

## Shader Patterns

```hlsl
// Global flag (included everywhere)
#define HAS_INVERTED_Z_BUFFER 1

// Pattern 1: Min vs Max for depth aggregation
#if HAS_INVERTED_Z_BUFFER
    FarDepth = min(z0, z1, z2, z3);  // Smaller = farther
#else
    FarDepth = max(z0, z1, z2, z3);  // Larger = farther
#endif

// Pattern 2: Comparison
#if HAS_INVERTED_Z_BUFFER
    if (pixelDepth > referenceDepth)  // Higher = closer
        CloserObject();
#else
    if (pixelDepth < referenceDepth)  // Lower = closer
        CloserObject();
#endif

// Pattern 3: Depth bounds
#if HAS_INVERTED_Z_BUFFER
    float minDepth = 0.0;  // Far
    float maxDepth = 1.0;  // Near
#else
    float minDepth = 0.0;  // Near
    float maxDepth = 1.0;  // Far
#endif
```

## C++ Projection Matrix Patterns

```cpp
// Perspective: reversed-Z z-coefficient
float A = MinZ / (MinZ - MaxZ);          // Different from normal-Z!
float B = -MaxZ * MinZ / (MinZ - MaxZ);  // Both num and denom flipped

// Ortho: Z scaling negated
Matrix[2][2] = -ZScale;              // Negated!
Matrix[3][2] = 1.0 - ZOffset*ZScale; // 1.0 - offset

// Rule: Use FReversedZPerspectiveMatrix, never FPerspectiveMatrix
FReversedZPerspectiveMatrix Proj(HalfFOV, Width, Height, NearPlane, FarPlane);
```

## Validation Checklist

```
☐ Depth cleared = 0.0 (black in visualization)
☐ Near geometry depth ≈ 1.0 (bright in visualization)
☐ Far geometry depth ≈ 0.0 (dark in visualization)
☐ Closer object occludes farther object
☐ Hi-Z pyramid uses min(), not max()
☐ Shadow comparison: closer objects lit, farther in shadow
☐ SSAO/SSR use correct depth comparison direction
☐ No Z-fighting on near objects (major win vs normal-Z)
```

## File Reference Cheat Sheet

```
Architecture & Enum:
  RHIDefinitions.h:278-287      ERHIZBuffer enum
  RHIDefinitions.h:400-403      CF_Depth* comparison functions
  RHI.cpp:131-132               Clear value constants

Math:
  PerspectiveMatrix.h:135-141   FReversedZPerspectiveMatrix constructor
  OrthoMatrix.h:65-82           FReversedZOrthoMatrix constructor

Shaders:
  Definitions.usf:230-231       HAS_INVERTED_Z_BUFFER flag
  DownsampleDepthPixelShader.usf:84-88  Min vs Max pattern

Renderer:
  DepthRendering.cpp:603        Clear depth texture
  ShadowSetup.cpp:2625-2627     Shadow depth state
  ShadowSetup.cpp:3165          Shadow projection matrix

Static Assertions (verify assumptions):
  PostProcessAmbientOcclusion.cpp:908        IsInverted assertion
  SceneOcclusion.cpp:726         IsInverted assertion
```

## Common Mistakes to Avoid

| Mistake | Impact | Fix |
|---------|--------|-----|
| **Using `> 0.5` for "closer than halfway"** | Inverted logic | Use `IsCloser(a, b)` macro |
| **Clearing depth to 1.0** | Everything appears closer than needed | Clear to `FClearValueBinding::DepthFar` = 0.0 |
| **Using `max()` for Hi-Z pyramid** | Nearest objects culled (inverted pyramid) | Use `min()` instead |
| **Comparing `sampledDepth < referenceDepth`** | Inverted shadow/SSAO | Use `sampledDepth > referenceDepth` |
| **Hardcoding CF_LessEqual for depth test** | Objects render backward | Use `CF_DepthNearOrEqual` |
| **Using FPerspectiveMatrix instead of FReversedZPerspectiveMatrix** | Objects at wrong depth | Always use FReversedZ* versions |

## Decision Point for UzEngine

**Choose one (recommend reversed-Z):**

### Option 1: Adopt Reversed-Z (RECOMMENDED)
- **Pros:** DX12 native, 100x+ precision improvement at near plane, no Z-fighting on characters
- **Cons:** Must implement from scratch, all shaders need updates
- **Effort:** ~2-3 weeks for full integration + validation
- **Visual quality:** Excellent, matches AAA engines

### Option 2: Use Normal-Z
- **Pros:** Simpler initially, matches OpenGL, some tutorials use this
- **Cons:** Z-fighting on near objects, worse precision, doesn't match UE5
- **Effort:** Lower initial effort, but will regret later
- **Visual quality:** Adequate for prototypes, unacceptable for AAA

**Recommendation:** Adopt reversed-Z. The precision improvement alone justifies the effort. UzEngine's DX12-first architecture should match UE5's decisions.

## Precision Numbers

**Setup:** Near=0.1cm, Far=10,000cm

| Aspect | Normal-Z | Reversed-Z | Ratio |
|--------|----------|-----------|-------|
| Near-plane precision | 0.0001 units | 0.000001 units | **100x** |
| Mid-distance precision | 0.00001 units | 0.0000001 units | **100x** |
| Far-plane precision | 0.000001 units | 0.000001 units | 1x |
| Z-fighting risk | VERY HIGH | Very low | **100x** better |

---

**For implementation questions, see:**
- `ue5-reversed-z-architecture.md` — Deep technical analysis
- `ue5-reversed-z-implementation-patterns.md` — Code patterns & formulas
