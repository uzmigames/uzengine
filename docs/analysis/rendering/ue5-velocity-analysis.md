<!-- doc-meta
id: ue5-velocity-rendering-analysis
title: UE5 Velocity Rendering Analysis — Depth-Based Reprojection vs Vertex Velocity
keywords: velocity, reprojection, temporal AA, motion blur, depth, world-space velocity
created: 2026-03-18
updated: 2026-03-18
-->

# UE5 Velocity Rendering Analysis

## Overview

This analysis compares UzEngine's current velocity shader against UE5's implementation to verify correctness and identify architectural patterns.

**Key Finding**: UE5 uses **TWO complementary velocity approaches**:
1. **Vertex-based velocity** (primary): Shader outputs pre-computed world-space velocity at vertex stage
2. **Depth-based velocity** (fallback/static objects): Camera-only motion computed via depth reprojection at post-process stage

UzEngine currently implements depth-based velocity only. This analysis validates whether that choice is correct and identifies potential gaps.

---

## UE5 Implementation Details

### Source Files
- `W:\UE_5.6_Source\Engine\Shaders\Private\VelocityShader.usf` — Primary velocity pass vertex/pixel shaders
- `W:\UE_5.6_Source\Engine\Shaders\Private\VelocityCommon.ush` — Velocity computation utilities
- `W:\UE_5.6_Source\Engine\Shaders\Private\TemporalAA.usf` — Temporal AA reprojection (lines 2087–2122)
- `W:\UE_5.6_Source\Engine\Shaders\Private\DBufferNormalReprojection.ush` — Decal reprojection with depth fallback
- `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\VelocityRendering.cpp` — C++ infrastructure
- `W:\UE_5.6_Source\Engine\Shaders\Private\Common.ush` — Encoding/decoding functions

---

## Approach 1: Vertex-Based Velocity (Primary Path)

### Implementation
**VelocityShader.usf:45–97** (vertex stage):
```hlsl
void MainVertexShader(...)
{
    // Current frame
    float4 TranslatedWorldPosition = VertexFactoryGetWorldPosition(Input, VFIntermediates);
    TranslatedWorldPosition.xyz += GetMaterialWorldPositionOffset(VertexParameters);
    float4 Position = mul(float4(TranslatedWorldPosition.xyz, 1), ResolvedView.TranslatedWorldToClip);

    // Previous frame
    float4 PrevTranslatedWorldPosition = VertexFactoryGetPreviousWorldPosition(Input, VFIntermediates);
    PrevTranslatedWorldPosition.xyz += GetMaterialPreviousWorldPositionOffset(VertexParameters);
    float4 PrevScreenPosObj = mul(float4(PrevTranslatedWorldPosition.xyz, 1),
                                   ResolvedView.PrevTranslatedWorldToClip);

    Output.Interps.PackedVelocityC = INVARIANT(PrevScreenPosObj);
}
```

**VelocityShader.usf:108–185** (pixel stage):
```hlsl
void MainPixelShader(...)
{
    #if !TRANSLUCENCY_VELOCITY_FROM_DEPTH
        // Get current frame screen position
        float4 ScreenPosition = SvPositionToResolvedScreenPosition(SvPosition);

        // Compute velocity
        float3 Velocity = Calculate3DVelocity(ScreenPosition, Input.PackedVelocityC);

        // Encode to texture (format PF_A16B16G16R16)
        OutColor = EncodeVelocityToTexture(Velocity, bHasPixelAnimation);
    #else
        // For translucency, output zero (use depth reprojection instead)
        OutColor = 0;
    #endif
}
```

**VelocityCommon.ush:9–22**:
```hlsl
float3 Calculate3DVelocity(float4 PackedVelocityA, float4 PackedVelocityC)
{
    // Remove temporal AA jitter from both frames
    float2 ScreenPos = PackedVelocityA.xy / PackedVelocityA.w - ResolvedView.TemporalAAJitter.xy;
    float2 PrevScreenPos = PackedVelocityC.xy / PackedVelocityC.w - ResolvedView.TemporalAAJitter.zw;

    // Include depth delta for motion blur (contains camera motion in Z)
    float DeviceZ = PackedVelocityA.z / PackedVelocityA.w;
    float PrevDeviceZ = PackedVelocityC.z / PackedVelocityC.w;

    // Output: 3D velocity (current_screen - previous_screen, current_depth - previous_depth)
    float3 Velocity = float3(ScreenPos - PrevScreenPos, DeviceZ - PrevDeviceZ);

    return Velocity;
}
```

**Key Properties**:
- **Input**: Two clip-space positions (current and previous frame)
- **Processing**: Simple subtraction in NDC space (after perspective divide)
- **Output**: Screen-space velocity (pixels per frame) + depth delta
- **Scope**: Only objects with vertex data (dynamic meshes, skeletal, etc.)
- **Cost**: Computed during vertex/pixel stage; one pass per frame

---

## Approach 2: Depth-Based Velocity (Fallback for Static/Post-Process)

### Implementation in TemporalAA
**TemporalAA.usf:2087–2122**:
```hlsl
float4 ThisClip = float4( PosN.xy, PosN.z, 1 );                    // Current NDC + depth
float4 PrevClip = mul( ThisClip, View.ClipToPrevClip );            // Reproject to previous frame

float2 PrevScreen = PrevClip.xy / PrevClip.w;                      // Divide by previous w
float2 BackN = PosN.xy - PrevScreen;                               // Compute velocity

// Preference: use explicit velocity if available (dynamic objects)
#if AA_DYNAMIC
{
    ENCODED_VELOCITY_TYPE EncodedVelocity = SampleVelocityTexture(InputParams.NearestBufferUV + VelocityOffset);
    bool DynamicN = EncodedVelocity.x > 0.0;                       // Check encoding marker
    if(DynamicN)
    {
        BackN = DecodeVelocityFromTexture(EncodedVelocity).xy;     // Override with vertex velocity
    }
}
#endif

// Convert to pixel units for motion blur
float2 BackTemp = BackN * OutputViewportSize.xy;
float Velocity = sqrt(dot(BackTemp, BackTemp));

// Compute history UV
float2 HistoryScreenPosition = InputParams.ScreenPos - BackN;
```

### Implementation in Decal Reprojection
**DBufferNormalReprojection.ush:16–32**:
```hlsl
float2 DeferredDecalPostProcessVelocityLookup(float Depth, float2 UV, float4 EncodedVelocity)
{
    float2 Velocity;
    if( EncodedVelocity.x > 0.0 )
    {
        // Use explicit vertex velocity if available
        Velocity = DecodeVelocityFromTexture(EncodedVelocity).xy;
    }
    else
    {
        // Fallback: depth-based reprojection for static geometry
        float4 ThisClip = float4( UV, Depth, 1 );
        float4 PrevClip = mul( ThisClip, View.ClipToPrevClip );
        float2 PrevScreen = PrevClip.xy / PrevClip.w;
        Velocity = UV - PrevScreen;
    }

    return Velocity;
}
```

**Key Properties**:
- **Input**: Current depth + NDC coordinates + View.ClipToPrevClip matrix
- **Processing**: Matrix multiply + perspective divide
- **Output**: Screen-space velocity (pixels per frame) — no depth delta
- **Scope**: All pixels (covers static geometry + anything without explicit velocity)
- **Cost**: Post-process only; does not affect rasterization pipeline
- **Encoding Logic**: `EncodedVelocity.x > 0.0` — zero velocity uses encoding as marker for "no explicit data"

---

## Velocity Encoding Format

**VelocityRendering.cpp:142**:
```cpp
OutEnvironment.SetRenderTargetOutputFormat(0, PF_A16B16G16R16);  // 16-bit per channel
```

**Common.ush EncodeVelocityToTexture** (velocity encoding function):
```hlsl
ENCODED_VELOCITY_TYPE EncodeVelocityToTexture(float3 V, bool bHasPixelAnimation)
{
    #if VELOCITY_ENCODE_GAMMA
        V.xy = sign(V.xy) * sqrt(abs(V.xy)) * (2.0 / sqrt(2.0));  // Optional gamma encoding
    #endif

    // 0.5 range to allow -2..2 velocity instead of -1..1 for high-speed objects
    float4 EncodedV;
    EncodedV.xy = V.xy * (0.499f * 0.5f) + 32767.0f / 65535.0f;

    #if VELOCITY_ENCODE_DEPTH
        // Encode depth delta in Z and W using bit packing
        uint Vz = asuint(V.z);
        EncodedV.z = saturate(float((Vz >> 16) & 0xFFFF) * rcp(65535.0f) + (0.1 / 65535.0f));
        EncodedV.w = saturate(float(((Vz >>  0) & 0xFFFE) | uint(bHasPixelAnimation)) * rcp(65535.0f) + (0.1 / 65535.0f));
    #else
        EncodedV.zw = 0.0;
    #endif

    return EncodedV;
}
```

**Key Details**:
- **Format**: 4x 16-bit float (A16B16G16R16)
- **X, Y**: Screen-space velocity XY (pixels per frame), scaled to -2..2 range
- **Z, W**: Optional depth delta (via bit packing) + pixel animation flag
- **Marker**: `x == 0.0` means "no explicit velocity, use depth fallback"
- **Decoding Scale**: `1.0 / (0.499f * 0.5f)` ≈ 4.01 to recover original velocity

---

## NDC and Screen-Space Coordinate Transforms

### Current Frame: SvPosition to NDC
**Common.ush**:
```hlsl
float4 SvPositionToResolvedScreenPosition(float4 SvPosition)
{
    float2 PixelPos = SvPosition.xy - ResolvedView.ViewRectMin.xy;

    // NDC: convert pixel coords [0, width] → NDC [-1, 1]
    // NOTE: Y is flipped (multiply by -2, not 2)
    float3 NDCPos = float3((PixelPos * ResolvedView.ViewSizeAndInvSize.zw - 0.5f) * float2(2, -2), SvPosition.z);

    return float4(NDCPos.xyz, 1) * SvPosition.w;
}
```

**Formula breakdown**:
```
PixelPos = SvPosition.xy - ViewRectMin       // Relative to viewport
PixelPosFrac = PixelPos * ViewSizeAndInvSize.zw  // = PixelPos / ViewSize, in [0, 1]
NDC.x = (PixelPosFrac.x - 0.5) * 2 = PixelPosFrac.x * 2 - 1  // In [-1, 1]
NDC.y = (PixelPosFrac.y - 0.5) * -2 = 1 - PixelPosFrac.y * 2  // In [-1, 1], inverted
NDC.z = SvPosition.z                         // Device depth (typically in [0, 1] with inverted Z)
```

### Screen Position (homogeneous clip space)
In the velocity shader, `ScreenPosition` is not NDC but **homogeneous clip space**:
```hlsl
float4 ScreenPosition = SvPositionToResolvedScreenPosition(SvPosition);
// ScreenPosition = float4(NDC.x * w, NDC.y * w, NDC.z * w, w)
```

The divide-by-w happens in `Calculate3DVelocity`:
```hlsl
float2 ScreenPos = PackedVelocityA.xy / PackedVelocityA.w;  // Perspective divide
```

---

## UzEngine's Current Implementation vs UE5

### UzEngine (velocity_ps.hlsl — Depth-Based Only)
```hlsl
// Load depth
float depth = depth_texture.Load(pixel_coord);

// Guard against sky pixels
if (depth == 0.0 || depth == 1.0)
    return float4(0, 0, 0, 0);

// Reconstruct NDC
float2 ndc = uv * 2.0 - 1.0;
ndc.y = -ndc.y;
float4 clipPos = float4(ndc, depth, 1.0);

// Reproject using view matrix
float4 worldPos = mul(clipPos, inv_view_proj);
worldPos.xyz /= worldPos.w;
float4 prevClip = mul(worldPos, prev_view_proj);
float2 prevNdc = prevClip.xy / prevClip.w;

// Convert NDC to screen pixels
float2 currScreen = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5) * viewport_size;
float2 prevScreen = float2(prevNdc.x * 0.5 + 0.5, -prevNdc.y * 0.5 + 0.5) * viewport_size;

// Velocity in pixels
float2 velocity = currScreen - prevScreen;
```

### Comparison

| Aspect | UE5 Vertex Path | UE5 Depth Path | UzEngine Current |
|--------|-----------------|----------------|------------------|
| **When Used** | Dynamic objects with vs/ps output | Static objects, post-process fallback | All pixels (post-process only) |
| **Data Source** | Vertex shader (prev frame transform) | Depth buffer + view matrices | Depth buffer + view matrices |
| **Encoding** | RGB16F + A16F (depth delta) | RGB16F only (depth not encoded) | Computed per-pixel at read |
| **Y-Flip** | Handled via INVARIANT output | In NDC computation | In NDC reconstruction |
| **Velocity Range** | -2..2 (via 0.499*0.5 scale) | Implicit in matrix math | Implicit in matrix math |
| **Temporal AA Jitter Removal** | Yes (TemporalAAJitter.xy/zw) | Via inverted matrix (implicit) | Not visible in current code |
| **Marker for "No Velocity"** | EncodedV.x == 0.0 → use depth | N/A | N/A (always have velocity) |

---

## Critical Differences & Gaps in UzEngine

### 1. **Missing Temporal AA Jitter Correction**
**UE5 Does**: Removes jitter before velocity computation
```hlsl
float2 ScreenPos = PackedVelocityA.xy / PackedVelocityA.w - ResolvedView.TemporalAAJitter.xy;
float2 PrevScreenPos = PackedVelocityC.xy / PackedVelocityC.w - ResolvedView.TemporalAAJitter.zw;
```

**Impact**: Without this, TAA will reproject jittered pixels incorrectly, causing ghosting or jitter artifacts.

**UzEngine Status**: Need to verify if `ResolvedView.TemporalAAJitter` is available in constant buffer.

### 2. **No Explicit Velocity Pass**
**UE5 Does**: Separate velocity pass that outputs full dynamic object velocity during depth/base pass.

**UzEngine Status**: Depth-only approach means all velocity is camera-motion only. Moving objects will have incorrect velocity (will appear static in motion blur). This is a **functional gap** for motion blur quality.

### 3. **Depth Delta (Z Component)**
**UE5 Encodes**: `V.z = current_depth - prev_depth` via bit-packing in encoding

**Use Case**: Motion blur along Z axis (forward/backward motion). TAA reads this to guide history lookup blurring.

**UzEngine Status**: Not computing or using depth delta. Motion blur will not account for Z motion.

### 4. **Velocity Encoding Scale**
**UE5 Uses**: `V.xy * (0.499f * 0.5f) + 32767.0f / 65535.0f`
- Allows velocity range of -2..2 pixels instead of -1..1
- Supports high-speed motion (fast objects moving > 1 pixel/frame)

**UzEngine Status**: If using raw float16, this is implicitly supported. If using a different format, may truncate at ±1 pixel.

### 5. **Decoding Marker for Fallback**
**UE5 Uses**: `EncodedVelocity.x > 0.0` to detect "has explicit vertex velocity"
- If false, TAA falls back to depth-based reprojection

**UzEngine Status**: Always uses depth-based velocity, so no need for this marker (but doesn't hurt to follow convention).

---

## Correctness of UzEngine's Math

### NDC Reconstruction ✓
```hlsl
float2 ndc = uv * 2.0 - 1.0;  // [0,1] → [-1,1]
ndc.y = -ndc.y;                // Flip Y (DX11/DX12 convention)
```
**Status**: CORRECT. Matches UE5's NDC space (inverted Y).

### Perspective Divide ✓
```hlsl
float4 worldPos = mul(clipPos, inv_view_proj);
worldPos.xyz /= worldPos.w;
float4 prevClip = mul(worldPos, prev_view_proj);
float2 prevNdc = prevClip.xy / prevClip.w;
```
**Status**: CORRECT. This is mathematically equivalent to UE5's `View.ClipToPrevClip` matrix multiply.

### Screen Pixel Conversion ✗
```hlsl
float2 currScreen = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5) * viewport_size;
```
**Issue**: This converts NDC back to [0,1] UV range, then scales by viewport size.

**Problem**: If viewport is offset (ViewRectMin != 0), this will be wrong.

**UE5 Does**:
```hlsl
// ScreenPos is already homogeneous clip space (NDC * w), not divided yet
float2 ScreenPos = PackedVelocityA.xy / PackedVelocityA.w;  // Back to NDC
// Then work in NDC space [-1,1], convert only at final output
```

**Fix**: Should use `View.ViewRectMin` if needed, or work entirely in NDC space and multiply velocity by OutputViewportSize at the end.

---

## Motion Blur Quality Impact

UE5's depth-based reprojection **alone** produces acceptable motion blur because:
1. **Static geometry** dominates most scenes — velocity purely from camera motion
2. **Dynamic objects** output explicit velocity during rendering
3. **Fallback chain**: Try explicit velocity first → depth reprojection second

UzEngine's depth-only approach will:
- ✓ Correctly blur static geometry
- ✓ Correctly blur camera pans/rotations
- ✗ **Fail to blur moving objects** (skeletal characters, vehicles, animated props)
- ✗ **Produce ghosting in TAA** if jitter correction is missing
- ✗ **Not blur along Z** (forward motion will not blur)

---

## Recommendations

### Short-term (Validate Current)
1. **Verify temporal AA jitter constant** is available and being subtracted
2. **Test motion blur on dynamic objects** — if visibly wrong, need vertex velocity pass
3. **Verify viewport offset handling** — if ViewRectMin != 0 anywhere, fix screen pixel conversion
4. **Check depth delta usage** — if motion blur looks wrong along Z, implement depth delta encoding

### Medium-term (UE5 Parity)
1. Implement separate **velocity pass** that outputs dynamic object velocities
   - Add `PrevTranslatedWorldToClip` matrix to constant buffer
   - Implement `VelocityVS/VelocityPS` shaders (copy from UE5, adapt to UzEngine RHI)
   - Render during depth pass or as post-geometry pass before motion blur

2. Implement **velocity encoding** following UE5 format:
   - Store in float4 (A16B16G16R16)
   - Encode XY velocity with 0.499*0.5 scale
   - Optionally encode depth delta in Z,W
   - Use x==0 marker to signal "use depth fallback"

3. Implement **decoding** in TAA/motion blur:
   ```hlsl
   if (velocity.x > 0.0) {
       // Use explicit velocity
       actualVelocity = DecodeVelocity(velocity);
   } else {
       // Use depth-based reprojection
       actualVelocity = ComputeDepthVelocity(depth, uv);
   }
   ```

### Long-term (Full Feature Parity)
- Implement **per-material velocity offset** (GetMaterialWorldPositionOffset must be accounted for in both frames)
- Add **TRANSLUCENCY_VELOCITY_FROM_DEPTH** permutation for translucent objects
- Implement **pixel animation flag** for vertex-animated meshes (bit 0 of velocity.w encoding)

---

## Conclusions

### Is Depth-Based Only Acceptable?
**For static scenes**: Yes.
**For animated scenes**: No — moving objects will not blur correctly.

### Does UzEngine's Math Match UE5?
**Mostly yes**, with one caveat:
- NDC reconstruction: ✓ Correct
- Reprojection logic: ✓ Correct (equivalent to ClipToPrevClip)
- Screen pixel conversion: ⚠ Correct only if viewport rect is [0,0]

### Next Steps
1. **Urgent**: Add temporal AA jitter correction if not already present
2. **Important**: Test motion blur on skeletal meshes — if wrong, implement vertex velocity pass
3. **Nice-to-have**: Implement full encoding/decoding scheme for cross-pass data sharing

---

## References

- **UE5 Velocity Shader**: `W:\UE_5.6_Source\Engine\Shaders\Private\VelocityShader.usf:45–185`
- **Velocity Encoding**: `W:\UE_5.6_Source\Engine\Shaders\Private\Common.ush:EncodeVelocityToTexture`
- **Temporal AA Reprojection**: `W:\UE_5.6_Source\Engine\Shaders\Private\TemporalAA.usf:2087–2122`
- **Decal Fallback**: `W:\UE_5.6_Source\Engine\Shaders\Private\DBufferNormalReprojection.ush:16–32`
- **C++ Setup**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\VelocityRendering.cpp`
