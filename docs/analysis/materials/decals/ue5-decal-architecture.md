<!-- doc-meta
type: architecture-analysis
scope: deferred-decal-system
references: DeferredDecal.usf, DecalRenderingShared.cpp, DBufferDecalShared.ush
last-updated: 2026-03-31
status: complete
-->

# UE5 Deferred Decal (DBuffer) Architecture

## Executive Summary

UE5 implements deferred decals using a **DBuffer (deferred buffer) system** that decouples decal rendering from GBuffer layout. The core pattern:

1. **Vertex Stage**: Render a unit cube (OBB) using a frustum-to-clip transform; depth comes from scene depth lookup
2. **Pixel Stage**: Reproject screen position → decal local space via a precomputed matrix, clip if outside [-1,1]³
3. **Output**: Encode decal properties (normal, albedo, roughness/metallic) into 3 DBuffer RTs before lighting pass applies them
4. **Timing**: Decals render AFTER GBuffer but BEFORE lighting, allowing multiple decal passes without depth-write conflicts

This analysis extracts the EXACT matrix math, UV mapping, and PSO configuration from UE5 5.6 source.

---

## 1. Vertex Shader: The Frustum-to-Clip Transform

### Source Code
```cpp
// File: W:\UE_5.6_Source\Engine\Shaders\Private\DeferredDecal.usf:70-85
void MainVS(
    in FStereoVSInput StereoInput,
    in float4 InPosition : ATTRIBUTE0,
    out float4 OutPosition : SV_POSITION,
    out FStereoVSOutput StereoOutput
)
{
    StereoSetupVS(StereoInput, StereoOutput);
    
#if MOBILE_MULTI_VIEW
    float4x4 FrustumToClip = mul(FrustumComponentToClip, ResolvedView.MobileMultiviewDecalTransform);
#else
    float4x4 FrustumToClip = FrustumComponentToClip;
#endif
    OutPosition = mul(InPosition, FrustumToClip);
}
```

### Pattern: Unit Cube to Screen Space

- **Input Geometry**: A unit cube (OBB) with vertices at `±(1,1,1)` in decal space
- **Transform**: `OutPosition = InPosition * FrustumComponentToClip`
  - This is **column-major matrix × row vector** (transpose of typical row-major math)
  - `FrustumComponentToClip` is a 4×4 that directly converts decal OBB corners to clip space
- **No VB/IB needed**: Uses implicit/generated vertices (indexed by SV_VertexID)
- **Depth from GBuffer**: The pixel shader later overwrites `SvPosition.z` with scene depth

### Matrix Construction (C++)

From `DecalRenderingShared.cpp:199-252`:

```cpp
// Setup SvPositionToDecal = SvPositionToDecalBase * InvViewProjection * WorldToDecal
// Note: InvViewProjection = inverse(ViewProjection)

float4x4 SvPositionToDecalBase(
    FPlane(Mx,  0,   0, 0),   // Mx = 2.0 * InvViewSize.X
    FPlane(0,   My,  0, 0),   // My = -2.0 * InvViewSize.Y
    FPlane(0,   0,   1, 0),   // Z passthrough
    FPlane(Ax,  Ay,  0, 1)    // Ax, Ay = view rect min adjusted
);

// Final matrix: transforms from screen-space back to decal local space
FMatrix44f SvPositionToDecal = FMatrix44f(
    SvPositionToDecalBase * View.ViewMatrices.GetInvViewProjectionMatrix() * WorldToDecalMatrix
);
```

**Key insight**: The vertex shader doesn't use this matrix directly. It uses `FrustumComponentToClip`, which is computed separately (see PSO setup). The `SvPositionToDecal` matrix is used in the pixel shader.

---

## 2. Pixel Shader: Screen Space → Decal Local Space

### Source Code
```hlsl
// File: DeferredDecal.usf:125-185
void FPixelShaderInOut_MainPS(inout FPixelShaderIn In, inout FPixelShaderOut Out)
{
    float2 ScreenUV = SvPositionToBufferUV(In.SvPosition);

    // Step 1: Replace SvPosition.z with depth from scene depth buffer
    In.SvPosition.z = LookupDeviceZ(ScreenUV);
    In.SvPosition.w = ConvertFromDeviceZ(In.SvPosition.z);

    // Step 2: Transform screen position to decal local space
    //   Input:  float4(SvPosition.xyz, 1)
    //   Output: float3(OSPosition) after perspective divide
    float4 DecalVectorHom = mul(float4(In.SvPosition.xyz, 1), SvPositionToDecalForSlice);
    float3 OSPosition = DecalVectorHom.xyz / DecalVectorHom.w;

    // Step 3: Clip geometry outside [-1,1]³
    clip(OSPosition.xyz + 1.0f);  // Cull if any component < -1
    clip(1.0f - OSPosition.xyz);  // Cull if any component > 1

    // Step 4: Compute texture UVs
    float3 DecalVector = OSPosition * 0.5f + 0.5f;  // [-1,1] → [0,1]
    float3 SwizzlePos = DecalVector.zyx;             // Permute for correct orientation

    // Step 5: UV mapping
    float2 DecalUVs = SwizzlePos.xy;  // XY of decal space → material UVs
    // DecalVector.z = distance along projection axis (used for fadeout)
    const float DecalFading = saturate(4 - 4 * abs(SwizzlePos.z * 2 - 1)) * DecalParams.x;
}
```

### Matrix Operation: SvPositionToDecal

This is the critical transform. From `DecalRenderingShared.cpp:238-239`:

```cpp
FMatrix44f SvPositionToDecal = FMatrix44f(
    SvPositionToDecalBase * View.ViewMatrices.GetInvViewProjectionMatrix() * WorldToDecalMatrix
);
```

**Decomposition**:

1. **SvPositionToDecalBase**: Maps screen pixel coordinates to normalized device space
   ```
   [2/W      0      0    -1]
   [  0   -2/H     0     1]     (where W,H = viewport size)
   [  0      0     1     0]
   [  0      0     0     1]
   ```
   Note: Y is negated because screen Y increases downward, NDC Y increases upward

2. **InvViewProjection**: Transforms NDC [-1,1]³ to world space
   - Computed as `inverse(ViewMatrix * ProjectionMatrix)`
   - Includes camera position translation

3. **WorldToDecal**: Transforms world space to decal local space
   - `WorldToDecal = inverse(ComponentTrans.ToMatrixWithScale())`
   - `ComponentTrans` is the decal's transform including scale/rotation

### UV Mapping Logic

```hlsl
float3 DecalVector = OSPosition * 0.5f + 0.5f;  // [-1,1] → [0,1]
float3 SwizzlePos = DecalVector.zyx;             // Axis reorder

float2 DecalUVs = SwizzlePos.xy;  // Final UVs for material sampling
```

**Why the swizzle `.zyx`?**
- Decal OBB has X = left/right, Y = up/down, Z = projection axis (away from decal)
- Material textures expect UV in XY plane, so we swap axes
- `SwizzlePos.z` (original X) becomes the fade distance from center

---

## 3. Render Target Setup: DBuffer (3 RTs + optional mask)

### Output Targets

From `PostProcessDeferredDecals.cpp:150-163` and `DBufferDecalShared.ush:393-400`:

**DBuffer RT Layout** (when `DECAL_RENDERTARGETMODE_DBUFFER`):

| Target | Format | Contents | Blend Mode |
|--------|--------|----------|-----------|
| **DBufferA** | RGBA8 (UNORM) | `(BaseColor.rgb, ColorOpacity)` | Blend |
| **DBufferB** | RGBA8 (UNORM) | `(Normal×0.5+0.5, NormalOpacity)` | Blend |
| **DBufferC** | RGBA8 (UNORM) | `(Metallic, Specular, Roughness, RoughnessOpacity)` | Blend |
| **DBufferMask** | R8 (UNORM) | `(1.0)` | Write |

### Encoding (Pixel Shader Output)

```hlsl
// File: DBufferDecalShared.ush:387-400
void EncodeDBufferData(FGBufferData GBufferData, float3 MultiOpacity,
    out float4 DBufferA, out float4 DBufferB, out float4 DBufferC)
{
    // Pre-multiply with opacity for blend state
    DBufferA = float4(GBufferData.BaseColor, MultiOpacity.x);
    
    // Normal: map [-1,1] → [0,1] for UNORM storage
    DBufferB = float4(GBufferData.WorldNormal * 0.5f + 128.0f/255.0f, MultiOpacity.y);
    
    // Roughness/Metallic/Specular packed into one RT
    DBufferC = float4(GBufferData.Metallic, GBufferData.Specular, 
                      GBufferData.Roughness, MultiOpacity.z);
}
```

### Decoding (During Lighting Pass)

```hlsl
// File: DBufferDecalShared.ush:403-425
FDBufferData DecodeDBufferData(float4 DBufferA, float4 DBufferB, float4 DBufferC)
{
    FDBufferData ret;
    ret.PreMulColor = DBufferA.rgb;
    ret.ColorOpacity = DBufferA.a;
    
    // Reverse the normal encoding: [0,1] → [-1,1]
    ret.PreMulWorldNormal = DBufferB.rgb * 2 - (256.0 / 255.0);
    ret.NormalOpacity = DBufferB.a;
    
    ret.PreMulMetallic = DBufferC.r;
    ret.PreMulSpecular = DBufferC.g;
    ret.PreMulRoughness = DBufferC.b;
    ret.RoughnessOpacity = DBufferC.a;
    
    return ret;
}
```

---

## 4. PSO (Pipeline State Object) Configuration

### Depth & Stencil

From `PostProcessDeferredDecals.cpp:193-194`:

```cpp
SetupDepthStencilInfo(PF_DepthStencil, Config.DepthCreateFlags, 
    ERenderTargetLoadAction::ELoad,        // Load existing depth
    ERenderTargetLoadAction::ELoad,        // Load existing stencil
    FExclusiveDepthStencil::DepthRead_StencilWrite,  // Read depth, write stencil
    RenderTargetsInfo);
```

**Key settings**:
- **Depth Test**: `DEPTH_TEST_GREATER_EQUAL` (reversed-Z, default in UE5)
  - Decals only affect pixels in front of the decal plane
- **Depth Write**: **DISABLED** — decals don't modify the depth buffer
- **Stencil Write**: **ENABLED** — used for optimization (large screen-space decals can stencil-cull)

### Face Culling

- **Rasterizer State**: Render **back-faces only** (CCW winding)
  - Decal OBB back-faces face outward; we render from inside the box
  - Front-face culling is enabled by default

### Blend Mode

From `DecalRenderingShared.cpp:45-46` and decal material definitions:

```cpp
BlendDesc = DecalRendering::ComputeDecalBlendDesc(ShaderPlatform, MaterialResource);
```

**Standard blend modes** (computed per decal material):
- **DBuffer standard**: `SrcBlend=SrcAlpha, DstBlend=InvSrcAlpha` (alpha-blend)
  - Pre-multiplied alpha in DBuffer RTs
  - Coverage is in the alpha channel of each RT

### Render Pass Timing

From `PostProcessDeferredDecals.cpp:98-108`:

```cpp
// DBuffer Passes (run BEFORE base/lighting passes)
if (bDBufferEnabled)
{
    LaunchRelevantDecalTask(EDecalRenderStage::BeforeBasePass);
    LaunchRelevantDecalTask(EDecalRenderStage::Emissive);
}
// GBuffer Passes (alternative, run AFTER base pass)
else if (bGBufferEnabled)
{
    LaunchRelevantDecalTask(EDecalRenderStage::BeforeLighting);
}
```

**Pipeline order**:
1. **GBuffer Fill** (base pass)
2. **DBuffer Decals** (this pass) — writes to DBuffer RTs
3. **Lighting Pass** (applies DBuffer modulation)

This allows unlimited decals without depth-buffer conflicts.

---

## 5. Projection Matrix Math (Complete Derivation)

### Input to Output Chain

```
Screen Pixel (SvPosition)
    ↓ [SvPositionToDecalBase]
NDC Space
    ↓ [InvViewProjection]
World Space
    ↓ [WorldToDecal]
Decal Local Space [-1,1]³
    ↓ [Clip & Fade]
Final Fragment (or discarded)
```

### Matrix Composition

**SvPositionToDecal** is a single 4×4 computed as:

```
SvPositionToDecal = SvPositionToDecalBase × InvViewProjection × WorldToDecal

Where:
  SvPositionToDecalBase = Screen-to-NDC matrix (handles viewport scaling)
  InvViewProjection = NDC-to-World (inverted camera matrix)
  WorldToDecal = World-to-Local (inverted decal transform)
```

### Component Details

**SvPositionToDecalBase** (from `DecalRenderingShared.cpp:230-235`):
```
Mx = 2.0 / ViewportWidth
My = -2.0 / ViewportHeight  (negative because Y flips)
Ax = -1.0 - 2.0 * ViewRectMin.X / ViewportWidth
Ay = 1.0 + 2.0 * ViewRectMin.Y / ViewportHeight

Matrix = [Mx  0   0  0]
         [0   My  0  0]
         [0   0   1  0]
         [Ax  Ay  0  1]
```

**InvViewProjection**:
```
P = Projection (camera frustum)
V = View (camera transform)
InvViewProjection = inverse(V × P)
```

**WorldToDecal**:
```
DecalToWorld = ComponentTrans.ToMatrixWithScale()
WorldToDecal = inverse(DecalToWorld)
```

---

## 6. Coordinate System & Conventions

### Decal OBB Orientation

- **X-axis**: Left-right (perpendicular to projection plane)
- **Y-axis**: Up-down (perpendicular to projection plane)
- **Z-axis**: Projection direction (away from surface, +Z = away from decal)

Stored in: `VisibleDecal.ComponentTrans` (FTransform with scale for OBB size)

### Depth Convention

- **Reversed-Z**: depth ≈ 0 for far plane, ≈ 1 for near plane
  - `ConvertFromDeviceZ()` converts stored depth to linear camera space
  - Comparison: `DEPTH_GREATER_EQUAL` (reversed logic)

### Matrix Convention in UE5

- **Column-major storage**: matrices stored as `[col0, col1, col2, col3]`
- **Row-vector multiplication**: `vec4 × matrix` (transposes math, but simpler in HLSL)

---

## 7. Anti-Patterns (What NOT to Do)

### ❌ Anti-Pattern 1: Using a VB/IB Instead of Implicit Geometry
**Wrong**: Building a vertex buffer with 24 vertices (cube) for each decal
**Right**: Use a unit cube generated implicitly (6 quad faces with SV_VertexID)

### ❌ Anti-Pattern 2: Writing to Depth Buffer
**Wrong**: `DepthWrite=True` in PSO
**Right**: `DepthRead_StencilWrite` — read depth, don't modify it

### ❌ Anti-Pattern 3: Rendering Decals AFTER Lighting
**Wrong**: Trying to sample lit GBuffer and composite decals on top
**Right**: Write to DBuffer BEFORE lighting, let lighting apply decal modulation

### ❌ Anti-Pattern 4: Front-Face Culling
**Wrong**: Culling front faces of the decal OBB (default state)
**Right**: Cull back faces — render from inside the box looking outward

### ❌ Anti-Pattern 5: Hardcoding Matrix Order
**Wrong**: Assuming row-major or column-major without checking the engine's convention
**Right**: Use the engine's `GetInvViewProjectionMatrix()` and compose matrices in the documented order

### ❌ Anti-Pattern 6: Not Clipping Outside the OBB
**Wrong**: Rendering decal UVs for pixels outside the [-1,1]³ volume
**Right**: Use `clip()` before material evaluation to discard out-of-bounds pixels

---

## 8. UzEngine Adaptation Strategy

### Phase 1: Core Projection
1. Implement `SvPositionToDecalMatrix` computation in C++ (same math as UE5)
2. Pass as a shader parameter (float4x4)
3. In pixel shader:
   ```hlsl
   float4 DecalVectorHom = mul(float4(SvPosition.xyz, 1), SvPositionToDecal);
   float3 OSPosition = DecalVectorHom.xyz / DecalVectorHom.w;
   clip(OSPosition.xyz + 1.0f);
   clip(1.0f - OSPosition.xyz);
   ```

### Phase 2: DBuffer Encoding
1. Create 3 DBuffer RTs (see format table above)
2. Encode pixel shader outputs using the `EncodeDBufferData` pattern
3. Use alpha-blend with pre-multiplied alpha

### Phase 3: Lighting Integration
1. During lighting pass, read DBuffer RTs
2. Decode using `DecodeDBufferData`
3. Apply modulation to base color, normal, roughness/metallic

### Phase 4: Optional: Stencil Optimization
1. For large screen-space decals, pre-render stencil mask
2. Use stencil test to skip pixels outside the projected bounds
3. Reduces overdraw on complex scenes

---

## References

- **Shader**: `W:\UE_5.6_Source\Engine\Shaders\Private\DeferredDecal.usf` (lines 70-185)
- **C++ Setup**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\DecalRenderingShared.cpp` (lines 199-252)
- **DBuffer Encoding**: `W:\UE_5.6_Source\Engine\Shaders\Private\DBufferDecalShared.ush` (lines 387-425)
- **Render Graph**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\CompositionLighting\PostProcessDeferredDecals.cpp` (lines 127-195)

---

## Data Flow: Decal Component → GPU

```
UDecalComponent (game thread)
    ↓
FDeferredDecalProxy (scene thread snapshot)
    ├─ ComponentTrans: FTransform (position, rotation, scale)
    ├─ DecalMaterial: UMaterialInterface*
    └─ DecalColor: FLinearColor
    ↓
FVisibleDecal (render thread)
    ├─ ComponentTrans → WorldToDecal matrix
    ├─ Material → pixel shader dispatch
    └─ DecalParams (fade alpha, lifetime)
    ↓
Vertex Shader
    ├─ FrustumComponentToClip: OutPosition
    └─ (depth comes from pixel shader lookup)
    ↓
Pixel Shader
    ├─ SvPositionToDecal: OSPosition in [-1,1]³
    ├─ Clip if outside bounds
    ├─ Sample decal material
    └─ Encode to DBuffer RTs
    ↓
DBuffer RTs (3 FP32 targets)
    ↓
Lighting Pass
    ├─ Read DBuffer
    ├─ Decode & blend with geometry data
    └─ Output final lit color
```

---

## Summary Table

| Aspect | UE5 Implementation | Key Insight |
|--------|-------------------|------------|
| **Geometry** | Unit cube ([-1,1]³) rendered via frustum projection | No VB/IB needed |
| **Depth** | Read from scene depth buffer; don't write | Enables stacking |
| **Projection** | `SvPositionToDecal = Base × InvViewProj × WorldToDecal` | Single 4×4 matrix |
| **Clipping** | `clip(OSPosition ± 1.0)` after projection | Cheap early discard |
| **UVs** | `DecalVector.zyx` → `SwizzlePos.xy` | Axis reorder for correct orientation |
| **Fade** | `saturate(4 - 4×abs(z×2-1))` along projection axis | Smooth edge blend |
| **Output** | DBuffer: 3×RGB8A8 (color, normal, rough/metal/spec) | Deferred application before lighting |
| **Culling** | Back-face cull (render from inside box) | Depth test reads geometry depth |
| **PSO Timing** | Decals → DBuffer (before lighting), OR GBuffer (before lighting) | No depth-write conflicts |
| **Blend Mode** | `SrcAlpha, InvSrcAlpha` (pre-multiplied) | Alpha per-RT in DBuffer |

