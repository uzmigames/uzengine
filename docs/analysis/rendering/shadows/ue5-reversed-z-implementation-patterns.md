<!-- doc-meta
title: UE5 Reversed-Z Implementation Patterns for UzEngine
stage: implementation-guide
created: 2026-03-16
source: W:\UE_5.6_Source
focus: code-patterns, math-formulas, shader-patterns
depth: implementation-ready
status: ready-to-code
-->

# UE5 Reversed-Z Implementation Patterns

This document extracts exact patterns from UE5 that UzEngine should replicate for reversed-Z depth buffer support.

## 1. C++ Header Patterns

### 1.1 RHI Definitions (W:\UE_5.6_Source\Engine\Source\Runtime\RHI\Public\RHIDefinitions.h)

**Pattern: Z-Buffer Enum**

```cpp
enum class ERHIZBuffer
{
    FarPlane = 0,
    NearPlane = 1,

    // Compile-time constant: 1 if FarPlane < NearPlane (true for DX12)
    IsInverted = (int32)((int32)ERHIZBuffer::FarPlane < (int32)ERHIZBuffer::NearPlane),
};
```

**Adaptation for UzEngine:**
```cpp
// uz/rhi/rhi_definitions.h
enum class URHIZBuffer
{
    FarPlane = 0,
    NearPlane = 1,

    IsInverted = (int32)((int32)URHIZBuffer::FarPlane < (int32)URHIZBuffer::NearPlane),
};

// Static assertion pattern UE5 uses:
// static_assert(URHIZBuffer::IsInvertedZBuffer(), "Your assumption requires reversed-Z");
```

### 1.2 Comparison Function Enum (RHIDefinitions.h:385-404)

**Pattern: Automatic Depth Comparison Mapping**

```cpp
enum ECompareFunction
{
    CF_Less,
    CF_LessEqual,
    CF_Greater,
    CF_GreaterEqual,
    CF_Equal,
    CF_NotEqual,
    CF_Never,
    CF_Always,

    // Utility enumerations that resolve based on Z-buffer inversion
    CF_DepthNearOrEqual    = (((int32)ERHIZBuffer::IsInverted != 0) ? CF_GreaterEqual : CF_LessEqual),
    CF_DepthNear           = (((int32)ERHIZBuffer::IsInverted != 0) ? CF_Greater : CF_Less),
    CF_DepthFartherOrEqual = (((int32)ERHIZBuffer::IsInverted != 0) ? CF_LessEqual : CF_GreaterEqual),
    CF_DepthFarther        = (((int32)ERHIZBuffer::IsInverted != 0) ? CF_Less : CF_Greater),
};
```

**Why this pattern works:**
- Uses ternary operator at compile-time
- Compiler optimizes to single constant
- Code using `CF_DepthNearOrEqual` doesn't change between reversed-Z and normal-Z
- Self-documenting: "depth near or equal" is clearer than "greater equal"

### 1.3 Clear Value Binding Pattern (RHIResources.h:248-372)

**Pattern: Depth Clear Values**

In header:
```cpp
struct FClearValueBinding
{
    struct DSValue
    {
        float Depth;
        uint32 Stencil;
    };

    explicit FClearValueBinding(float DepthClearValue, uint32 StencilClearValue = 0)
        : ColorBinding(EClearBinding::EDepthStencilBound)
    {
        Value.DSValue.Depth = DepthClearValue;
        Value.DSValue.Stencil = StencilClearValue;
    }

    // Static instances (defined in RHI.cpp)
    static RHI_API const FClearValueBinding DepthNear;
    static RHI_API const FClearValueBinding DepthFar;
};
```

In implementation (RHI.cpp:131-132):
```cpp
const FClearValueBinding FClearValueBinding::DepthNear((float)ERHIZBuffer::NearPlane, 0);
const FClearValueBinding FClearValueBinding::DepthFar((float)ERHIZBuffer::FarPlane, 0);
```

**Adaptation for UzEngine:**
```cpp
// uz/rhi/rhi_resources.h
struct URHIClearValue
{
    union
    {
        float DepthValue;
        struct { float Depth; uint32 Stencil; } DepthStencil;
    } Value;

    static const URHIClearValue DepthNear;  // = 1.0f
    static const URHIClearValue DepthFar;   // = 0.0f
};

// uz/rhi/rhi_resources.cpp
const URHIClearValue URHIClearValue::DepthNear  = { .Value.DepthValue = (float)URHIZBuffer::NearPlane };
const URHIClearValue URHIClearValue::DepthFar   = { .Value.DepthValue = (float)URHIZBuffer::FarPlane };
```

---

## 2. Matrix Patterns

### 2.1 Reversed-Z Perspective Matrix (PerspectiveMatrix.h:135-141)

**Formula breakdown:**

**Normal-Z (OpenGL):**
```
z_ndc = (MaxZ * z_view + (-MinZ * MaxZ)) / (z_view * (MaxZ - MinZ))
      = MaxZ / (MaxZ - MinZ) + (-MinZ * MaxZ) / (z_view * (MaxZ - MinZ))
```

**Reversed-Z (DX12):**
```
z_ndc = (MinZ * z_view + (-MaxZ * MinZ)) / (z_view * (MinZ - MaxZ))
      = MinZ / (MinZ - MaxZ) + (-MaxZ * MinZ) / (z_view * (MinZ - MaxZ))
```

**In matrix form (4x4 perspective projection):**

Normal-Z:
```
[1/tan(fov)     0           0          0]
[0         w/tan(fov)/h     0          0]
[0              0      MaxZ/(MaxZ-MinZ)   1]
[0              0     -MinZ*MaxZ/(MaxZ-MinZ)  0]
```

Reversed-Z:
```
[1/tan(fov)     0           0          0]
[0         w/tan(fov)/h     0          0]
[0              0      MinZ/(MinZ-MaxZ)   1]
[0              0     -MaxZ*MinZ/(MinZ-MaxZ)  0]
```

**UzEngine pattern:**
```cpp
// uz/math/matrix.h

template<typename T>
struct FReversedZPerspectiveMatrix
{
    // Constructor: (HalfFOV, Width, Height, MinZ, MaxZ)
    FReversedZPerspectiveMatrix(T HalfFOV, T Width, T Height, T MinZ, T MaxZ)
    {
        T XScale = 1.0f / tan(HalfFOV);
        T YScale = Width * XScale / Height;

        T RangeZ = MinZ - MaxZ;  // Negative!
        T A = MinZ / RangeZ;     // This is negative
        T B = -MaxZ * MinZ / RangeZ;  // This is positive

        // Set matrix elements (row-major or column-major depending on convention)
        M[0][0] = XScale; M[0][1] = 0;     M[0][2] = 0; M[0][3] = 0;
        M[1][0] = 0;      M[1][1] = YScale; M[1][2] = 0; M[1][3] = 0;
        M[2][0] = 0;      M[2][1] = 0;     M[2][2] = A; M[2][3] = 1;
        M[3][0] = 0;      M[3][1] = 0;     M[3][2] = B; M[3][3] = 0;
    }
};
```

### 2.2 Reversed-Z Orthographic Matrix (OrthoMatrix.h:65-82)

**Normal-Z:**
```
[2/Width    0         0       -1]
[0      2/Height     0       -1]
[0          0      ZScale  ZOffset*ZScale]
[0          0        0        1]
```

**Reversed-Z (note the negations and 1.0 offset):**
```
[2/Width    0         0       -1]
[0      2/Height     0       -1]
[0          0     -ZScale  1.0-ZOffset*ZScale]
[0          0        0        1]
```

**UzEngine pattern:**
```cpp
template<typename T>
struct FReversedZOrthoMatrix
{
    FReversedZOrthoMatrix(T Width, T Height, T ZScale, T ZOffset)
    {
        M[0][0] = (Width != 0.0f) ? (2.0f / Width) : 1.0f;
        M[1][1] = (Height != 0.0f) ? (2.0f / Height) : 1.0f;
        M[2][2] = -ZScale;                    // NEGATED
        M[3][3] = 1.0f - ZOffset * ZScale;    // 1.0 - offset
    }
};
```

---

## 3. Shader Patterns

### 3.1 Global Definition (Definitions.usf:230-231)

**Pattern: Feature flag for entire shader system**

```hlsl
#ifndef HAS_INVERTED_Z_BUFFER
#define HAS_INVERTED_Z_BUFFER    1
#endif
```

**Usage:** Include once in your master definitions file, consumed by all shaders.

**UzEngine adaptation (shaders/common/definitions.hlsl):**
```hlsl
// Replicate UE5's depth buffer convention
#ifndef HAS_INVERTED_Z_BUFFER
#define HAS_INVERTED_Z_BUFFER    1
#endif

// Helper macros based on Z-buffer inversion
#if HAS_INVERTED_Z_BUFFER
    #define DEPTH_FAR       0.0
    #define DEPTH_NEAR      1.0
    #define IsFarther(a, b) ((a) < (b))
    #define IsCloser(a, b)  ((a) > (b))
#else
    #define DEPTH_FAR       1.0
    #define DEPTH_NEAR      0.0
    #define IsFarther(a, b) ((a) > (b))
    #define IsCloser(a, b)  ((a) < (b))
#endif
```

### 3.2 Min vs Max Pattern (DownsampleDepthPixelShader.usf:84-88)

**Pattern: Conditional depth aggregation**

```hlsl
float FarDepth;

#if HAS_INVERTED_Z_BUFFER
    // In reversed-Z, farther objects have LOWER z values (closer to 0)
    FarDepth = min(min(DeviceZ0, DeviceZ1), min(DeviceZ2, DeviceZ3));
#else
    // In normal-Z, farther objects have HIGHER z values (closer to 1)
    FarDepth = max(max(DeviceZ0, DeviceZ1), max(DeviceZ2, DeviceZ3));
#endif
```

**Apply this pattern to:**
- Hi-Z mipmap generation
- Depth pyramid downsampling
- Occlusion testing (conservative queries need farthest depth)
- Depth compression/encoding

### 3.3 Comparison Pattern (Generic depth comparison)

**Pattern: Semantic depth comparison in shaders**

```hlsl
// Instead of hard-coding > or <, use semantic comparison
bool IsPixelCloserThanReference(float pixelDepth, float referenceDepth)
{
#if HAS_INVERTED_Z_BUFFER
    return pixelDepth > referenceDepth;  // Higher z = closer
#else
    return pixelDepth < referenceDepth;  // Lower z = closer
#endif
}

// Usage in SSAO, SSR, shadow sampling, etc.
if (IsPixelCloserThanReference(sampleDepth, centerDepth))
{
    // Apply ambient occlusion, reflection, shadow, etc.
}
```

---

## 4. Renderer State Patterns

### 4.1 Depth Stencil State Setup (ShadowSetup.cpp:2625-2639)

**Pattern: Using semantic comparison functions**

```cpp
// In render state initialization
DrawRenderState.SetDepthStencilState(
    TStaticDepthStencilState<
        false,                  // bEnableStencilTest = false
        CF_DepthNearOrEqual,    // Use semantic function, not raw CF_GreaterEqual
        SO_Keep,
        SO_Keep,
        SO_Keep,
        CF_Always,
        0xFF,
        0xFF
    >::GetRHI()
);
```

**Why:** `CF_DepthNearOrEqual` is self-documenting and automatically resolves to the correct comparison function (CF_GreaterEqual in reversed-Z, CF_LessEqual in normal-Z).

**UzEngine adaptation:**
```cpp
// uz/rhi/rhi_state.h
enum class URHIDepthStencilState
{
    // Standard opaque rendering (write depth, test near-or-equal)
    Standard = CF_DepthNearOrEqual,

    // Sky dome (write depth, always pass)
    Sky = CF_Always,

    // Translucent (read depth, test near, no write)
    Translucent = CF_DepthNear,

    // Reverse-facing (for double-sided translucents)
    ReverseFacing = CF_DepthFarther,
};
```

### 4.2 Depth Clear Pattern (DepthRendering.cpp:603)

**Pattern: Creating depth texture with correct clear value**

```cpp
FRDGTextureDesc DepthDesc = FRDGTextureDesc::Create2D(
    Extent,
    PF_DepthStencil,
    FClearValueBinding::DepthFar,  // <- Always use DepthFar, which is 0.0 in reversed-Z
    TexCreate_DepthStencilTargetable | TexCreate_ShaderResource
);
```

**Never hardcode 0.0 or 1.0:**
```cpp
// ❌ WRONG — hardcoded value, breaks if we switch Z-buffer mode
FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Extent, PF_DepthStencil,
    FClearValueBinding(0.0f), ...);

// ✅ CORRECT — uses semantic constant
FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Extent, PF_DepthStencil,
    FClearValueBinding::DepthFar, ...);
```

---

## 5. Validation & Testing Patterns

### 5.1 Static Assertion Pattern (PostProcessAmbientOcclusion.cpp:908)

**Pattern: Verify assumptions at compile-time**

```cpp
static_assert(bool(ERHIZBuffer::IsInverted), "Inverted depth buffer is assumed when setting depth bounds test for AO.");
```

**Purpose:** Catches architecture changes (e.g., if someone tries to add normal-Z support).

**UzEngine pattern:**
```cpp
// uz/renderer/depth_buffer.cpp
static_assert(bool(URHIZBuffer::IsInverted), "UzEngine requires reversed-Z depth buffer");

// uz/math/matrix.cpp
static_assert(
    ((int32)URHIZBuffer::NearPlane > (int32)URHIZBuffer::FarPlane),
    "Reversed-Z requires NearPlane > FarPlane"
);
```

### 5.2 Visual Validation Checklist

```
Depth clear test:
  ☐ Render empty scene (just clear depth)
  ☐ Read back depth texture
  ☐ Verify all pixels = 0.0f (DepthFar)
  ☐ Visually: should appear black in depth visualization

Depth write test:
  ☐ Render single quad at z=0.5 (halfway between near=1, far=0)
  ☐ Read back depth texture
  ☐ Verify pixels = ~0.5f (within float32 precision)
  ☐ Render at different depths, verify gradient

Depth comparison test:
  ☐ Render two overlapping quads
  ☐ Front quad (closer): z = 0.9
  ☐ Back quad (farther): z = 0.1
  ☐ Verify front quad occludes back quad
  ☐ Swap order, verify front still wins

Shadow comparison test:
  ☐ Render shadow from light source
  ☐ Verify objects in shadow (farther than light) = darker
  ☐ Objects closer than shadow depth = lit
  ☐ Check for artifacts at shadow boundaries

Hi-Z test:
  ☐ Create depth pyramid (4x downsampled, etc.)
  ☐ Verify each level uses min() of source depths
  ☐ Visually: each level should show farthest objects
```

---

## 6. Integration Checklist for UzEngine

### 6.1 RHI Layer
```
☐ Define URHIZBuffer enum with FarPlane=0, NearPlane=1, IsInverted
☐ Create URHIClearValue with DepthNear=1.0, DepthFar=0.0
☐ Define ECompareFunction with CF_DepthNearOrEqual, CF_DepthNear, CF_DepthFartherOrEqual, CF_DepthFarther
☐ All enums use compile-time ternary based on IsInverted
```

### 6.2 Math Library
```
☐ Implement FReversedZPerspectiveMatrix (3 constructors: full, fov, fov-with-infinite-far)
☐ Implement FReversedZOrthoMatrix (2 constructors: dimensions, LRBT)
☐ Add static_assert(URHIZBuffer::IsInverted) to verify assumptions
☐ Update all scene/shadow projection uses to call FReversedZ* not FPerspective*
```

### 6.3 Shaders
```
☐ Add #define HAS_INVERTED_Z_BUFFER 1 to master definitions.hlsl
☐ Audit all depth comparisons (find and fix hardcoded < and >)
☐ Update depth downsampling: min() for reversed-Z
☐ Update Hi-Z pyramid generation: min() for reversed-Z
☐ Update fog/occlusion shaders: IsCloser/IsFarther macros
```

### 6.4 Render Passes
```
☐ Scene depth: clear with DepthFar (0.0), test CF_DepthNearOrEqual
☐ Shadow depth: use FReversedZPerspectiveMatrix, test CF_DepthNearOrEqual
☐ Post-process: depth reads use correct comparison semantics
☐ Custom depth: clear DepthFar, compare with CF_DepthNearOrEqual
```

### 6.5 Testing & Validation
```
☐ Unit test: Matrix depth mapping (near→1, far→0)
☐ Unit test: Comparison function resolution (CF_DepthNearOrEqual → CF_GreaterEqual)
☐ Visual test: Depth clear (should be black/far)
☐ Visual test: Depth write gradient (near=bright, far=dark)
☐ Visual test: Occlusion (front occludes back)
☐ Visual test: Shadow comparison (near lit, far in shadow)
☐ Performance test: Verify no precision-related Z-fighting artifacts
```

---

## 7. Files to Create in UzEngine

```
engine/rhi/
  ├── rhi_z_buffer.h         (URHIZBuffer enum, ERHIZBuffer equivalent)
  ├── rhi_clear_value.h      (URHIClearValue with DepthNear, DepthFar)
  ├── rhi_depth_state.h      (Depth comparison function mappings)

engine/math/
  ├── matrix_reversed_z.h    (FReversedZPerspectiveMatrix, FReversedZOrthoMatrix)
  └── matrix_reversed_z.cpp  (Implementations)

engine/renderer/
  ├── depth_buffer.h         (Depth pipeline interface)
  ├── depth_buffer.cpp       (Static assertions, initialization)

shaders/common/
  ├── definitions.hlsl       (HAS_INVERTED_Z_BUFFER flag)
  ├── depth_utils.hlsl       (IsCloser, IsFarther, DEPTH_NEAR, DEPTH_FAR macros)

tests/
  ├── test_matrix_reversed_z.cpp    (Matrix depth mapping tests)
  ├── test_depth_comparison.cpp     (Comparison function resolution)
  ├── test_depth_visual.cpp         (Visual validation tests)
```

---

## Conclusion

UE5's reversed-Z implementation follows consistent patterns:
1. **Enum-based** configuration (ERHIZBuffer::IsInverted)
2. **Compile-time resolution** (ternary operators in enums)
3. **Semantic naming** (CF_DepthNearOrEqual instead of CF_GreaterEqual)
4. **Shader conditionals** (#if HAS_INVERTED_Z_BUFFER)
5. **Static assertions** to catch architecture violations
6. **Consistent application** across all rendering paths

UzEngine should replicate this structure exactly, adapting namespace/naming conventions to match existing codebase.
