/// Billboard vertex shader for GPU particle rendering.
///
/// Expands each particle into a camera-facing quad using only SV_VertexID
/// (no vertex buffer required). Each particle produces 6 vertices forming
/// 2 triangles. The shader reads particle attributes (position, size, color,
/// rotation) from StructuredBuffers written by the simulation compute pass.
///
/// # Billboard Geometry
/// ```
/// TL(2,5)──────TR(4)        UV: (0,0)───(1,0)
///   │  \         │                │  \      │
///   │    \  T1   │                │    \    │
///   │ T0   \     │                │      \  │
///   │         \  │                │        \│
/// BL(0)──────BR(1,3)             (0,1)───(1,1)
/// ```
/// Triangle 0: BL(0) - BR(1) - TL(2)
/// Triangle 1: BR(3) - TR(4) - TL(5)
///
/// # Vertex Index Mapping
/// Six vertices per particle, two triangles per quad:
///   vertex_id % 6 -> quad corner: 0=BL, 1=BR, 2=TL, 3=BR, 4=TR, 5=TL
///
/// # Billboard Method
/// Camera-facing billboard: right/up vectors extracted from the inverse view
/// matrix (columns 0 and 1). For row-major storage, these are rows 0 and 1.
/// The billboard quad is offset from the particle world position by
/// `(right * u + up * v) * halfSize`, where u,v are [-1,+1] corner offsets.
///
/// # Root Signature
/// - b0: VFXBillboardConstants (unified bindless indices for VS buffers + PS textures)
/// - b1: PerFrameCB (viewProj, cameraPos, invView for billboard vectors)
/// - b2: VFXBillboardCB (unified VS emitter params + PS rendering params)
/// - s0: not used in VS
///
/// # Outputs
/// In addition to clip-space position, UV, and color, this shader outputs:
/// - prevPositionCS for TAA motion vector computation
///
/// @ref Niagara GPU sprite rendering — billboard expansion from SV_VertexID
/// @ref Karis 2014 "High Quality Temporal Supersampling" — per-object motion vectors
/// @ref Latta 2004 "Inside Geometry Instancing" — camera-position billboard (mode 4)
/// @ref Valve 2006 "Rendering in Left 4 Dead" — axis-aligned billboard (mode 2)
/// @constants VERTICES_PER_PARTICLE=6
/// @deviations NONE

#include "../include/depth_common.hlsli"
#include "flipbook_atlas.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Number of vertices per billboard quad (2 triangles = 6 vertices).
static const uint VERTICES_PER_PARTICLE = 6;

/// Corner UV offsets for each of the 6 vertices in the billboard quad.
/// Maps vertex_in_quad index [0..5] to UV coordinates in [0,1] range.
/// Order: BL, BR, TL, BR, TR, TL (two triangles, CCW winding).
static const float2 kCornerUVs[6] = {
    float2(0.0f, 1.0f),  // 0: BL
    float2(1.0f, 1.0f),  // 1: BR
    float2(0.0f, 0.0f),  // 2: TL
    float2(1.0f, 1.0f),  // 3: BR (shared with T0)
    float2(1.0f, 0.0f),  // 4: TR
    float2(0.0f, 0.0f),  // 5: TL (shared with T0)
};

/// Corner position offsets in [-1, +1] range, derived from UVs.
/// Used to expand the billboard from center outward.
static const float2 kCornerOffsets[6] = {
    float2(-1.0f, -1.0f), // 0: BL
    float2( 1.0f, -1.0f), // 1: BR
    float2(-1.0f,  1.0f), // 2: TL
    float2( 1.0f, -1.0f), // 3: BR
    float2( 1.0f,  1.0f), // 4: TR
    float2(-1.0f,  1.0f), // 5: TL
};

// ─────────────────────────────────────────────────────────────────────────────
// Output Structure
// ─────────────────────────────────────────────────────────────────────────────

/// Interpolants passed to the billboard pixel shader.
struct VSOutput {
    float4 positionCS     : SV_Position; ///< Clip-space position for rasterization.
    float2 texcoord       : TEXCOORD0;   ///< UV for procedural circle generation.
    float4 color          : COLOR0;      ///< Particle color (RGBA), interpolated per-vertex.
    float3 positionWS     : TEXCOORD1;   ///< World-space position for soft particle depth fade.
    float  age            : TEXCOORD2;   ///< Particle age in seconds.
    float  lifetime       : TEXCOORD3;   ///< Particle total lifetime in seconds.
    uint   particleIdx    : TEXCOORD4;   ///< Particle index (reserved for future use).
    float4 prevPositionCS : TEXCOORD5;   ///< Previous frame clip-space position for TAA motion vectors.
    float  speedFactor    : TEXCOORD6;   ///< Normalized speed [0,1] = saturate(speed / speedRangeMax). Used by PS for color-by-speed.
    float4 dynamicParams  : TEXCOORD7;   ///< Per-particle dynamic material parameters (Task 4.14.0). X=normalizedAge, YZW=user.
    // ── Niagara-style flipbook UVs (pre-computed per vertex, interpolated) ──
    /// @ref Niagara NiagaraSpriteVertexFactory.ush — VS-computed atlas UVs
    float2 flipbookUV0    : TEXCOORD8;   ///< Atlas UV for current frame at this vertex.
    float2 flipbookUV1    : TEXCOORD9;   ///< Atlas UV for next frame at this vertex (for blending).
    float  flipbookLerp   : TEXCOORD10;  ///< Inter-frame blend factor [0,1].
};

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants (b0) — Bindless Descriptor Indices
// ─────────────────────────────────────────────────────────────────────────────

#ifdef UZ_BINDLESS
/// Unified root constants for bindless resource access (shared by VS and PS).
/// Contains SRV descriptor heap indices for particle attribute buffers (VS)
/// and texture resources (PS). Both stages read from the same b0 memory,
/// so the layout MUST be identical in VS and PS.
///
/// Uses plain cbuffer syntax instead of ConstantBuffer<T> to ensure correct
/// DXIL code generation with DXC for root constants. See particle_billboard_ps.hlsl
/// for the detailed rationale.
cbuffer VFXBillboardConstants : register(b0) {
    // VS indices (bytes 0-31)
    uint positionBufferIdx;    ///< SRV index: StructuredBuffer<float4> (xyz=pos, w=unused).
    uint sizeBufferIdx;        ///< SRV index: StructuredBuffer<float> (uniform scale).
    uint colorBufferIdx;       ///< SRV index: StructuredBuffer<float4> (rgba).
    uint rotationBufferIdx;    ///< SRV index: StructuredBuffer<float> (radians, Z-axis rotation).
    uint sortedIndicesSrvIndex;///< SRV index: StructuredBuffer<uint> (sorted draw order).
    uint velocityBufferIdx;    ///< SRV index: StructuredBuffer<float4> (xyz=velocity, w=unused).
    uint agesBufferIdx;        ///< SRV index: StructuredBuffer<float> (particle age in seconds).
    uint lifespansBufferIdx;   ///< SRV index: StructuredBuffer<float> (particle total lifetime).
    // PS indices (bytes 32-47)
    uint spriteTextureIdx;     ///< SRV index: Texture2D<float4> sprite/atlas (PS only).
    uint depthBufferIdx;       ///< SRV index: Texture2D<float> scene depth reversed-Z (PS only).
    uint dynamicParamsBufferIdx; ///< SRV index: StructuredBuffer<float4> dynamic material params (Task 4.14.0).
    /// @ref Niagara SubImageIndex module — per-particle SoA flipbook index.
    uint subImageIndexBufferIdx; ///< SRV index: StructuredBuffer<float> subImageIndices.
};
#else
/// SM5 fallback: explicit SRV bindings for particle attribute buffers.
StructuredBuffer<float4> particlePositions  : register(t0);
StructuredBuffer<float>  particleSizes      : register(t1);
StructuredBuffer<float4> particleColors     : register(t2);
StructuredBuffer<float>  particleRotations  : register(t3);
StructuredBuffer<float4> particleVelocities : register(t4);
StructuredBuffer<float>  particleAges       : register(t5);
StructuredBuffer<float>  particleLifespans  : register(t6);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Per-Frame Constant Buffer (b1) — Camera and Timing
// ─────────────────────────────────────────────────────────────────────────────

/// Per-frame constant buffer. Layout MUST match the C++ PerFrameCB struct and
/// all other shaders that reference b1. We only read viewProj and invView here,
/// but all fields must be declared for correct memory layout.
cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;        ///< View * Projection matrix.
    float4   cameraPos;                 ///< xyz = camera world position, w = unused.
    float4   timeParams;                ///< x = totalTime, y = deltaTime, z = frameCount, w = unused.
    float4   lightDir;                  ///< xyz = direction toward light, w = unused.
    float4   lightColor;                ///< xyz = color * intensity, w = unused.
    float4   ambientColor;              ///< xyz = ambient radiance, w = unused.
    row_major float4x4 invViewProj;     ///< Inverse view-projection (used elsewhere, declared for layout).
    float    nearPlane;                 ///< Camera near plane distance.
    float    farPlane;                  ///< Camera far plane distance.
    uint     _perFramePad0;
    uint     _perFramePad1;
    uint     _perFramePad2;
    uint     _perFramePad3;
    float    _perFramePad4;
    float    _perFramePad5;
    float    _perFramePad6;
    float    _perFramePad7;
    float    exposure;                  ///< Auto-exposure multiplier.
};

// ─────────────────────────────────────────────────────────────────────────────
// Unified Billboard Constant Buffer (b2) — VS Emitter + PS Rendering Params
// ─────────────────────────────────────────────────────────────────────────────

/// Unified per-emitter constant buffer (shared by VS and PS).
/// Both stages read from the same b2 memory, so the layout MUST be
/// identical in VS and PS. VS reads the first 160 bytes (emitter params),
/// PS reads the remaining 48 bytes (rendering params).
cbuffer VFXBillboardCB : register(b2) {
    // ── VS data (emitter parameters, 160 bytes) ─────────────────────────
    row_major float4x4 invView;        ///< Inverse view matrix — rows 0,1 give camera right/up.
    float3   emitterPosition;          ///< World-space emitter origin (for local-space particles).
    float    globalSizeMultiplier;     ///< Multiplied with per-particle size (default 1.0).
    float4   globalColorTint;          ///< Multiplied with per-particle color (default white).
    row_major float4x4 prevViewProj;   ///< Previous frame's view * projection for TAA motion vectors.
    // ── PS data (rendering parameters, 48 bytes) ────────────────────────
    float    softFadeDistance;         ///< World-space distance for soft particle depth fade.
    float    alphaErosion;            ///< Subtractive alpha erosion [0, 1].
    float    emissiveIntensity;       ///< Multiplier for additive emissive contribution.
    uint     litMode;                ///< 0=Unlit, 1=Lit. Not used by VS, declared for layout.
    // ── Camera offset & pivot offset (Task 4.7.0) ─────────────────────────
    /// @ref Latta 2004 "Inside Geometry Instancing" — camera offset for depth layering
    float    cameraOffset;             ///< Push/pull along camera forward (world units). 0=disabled.
    float    pivotOffsetX;             ///< Sprite-local X pivot shift [-1,1]. 0=center.
    float    pivotOffsetY;             ///< Sprite-local Y pivot shift [-1,1]. 0=center.
    // Repurposed from _camPivotPad — same 4 bytes, same 16-byte row alignment.
    float    screenSpacePixelSize;     ///< Target diameter in pixels for screen-space mode.
    // ── Size by speed (Task 4.8.0) ──────────────────────────────────────────
    uint     sizeBySpeedEnabled;       ///< 0=off, 1=on. Maps particle speed to size multiplier.
    float    sizeBySpeedMin;           ///< Size multiplier at zero speed.
    float    sizeBySpeedMax;           ///< Size multiplier at speedRangeMax.
    float    speedRangeMax;            ///< Speed that maps to t=1 for size-by-speed.
    // ── Size over lifetime (analytical piecewise-linear curve, 16 bytes) ──
    float    sizeOverLifeStart;    ///< Size multiplier at birth (t=0).
    float    sizeOverLifePeak;     ///< Size multiplier at peak time.
    float    sizeOverLifeEnd;      ///< Size multiplier at death (t=1).
    float    sizeOverLifePeakTime; ///< Normalized age [0,1] where peak occurs.
    // ── Color over lifetime (linear interpolation, 32 bytes) ──
    float4   colorLifeStart;       ///< RGBA color at birth.
    float4   colorLifeEnd;         ///< RGBA color at death.
    // ── Billboard facing mode + custom direction (16 bytes) ──────────────
    /// @ref Latta 2004 "Inside Geometry Instancing" — camera-position billboard
    /// @ref Valve 2006 "Rendering in Left 4 Dead" — axis-aligned billboard
    uint     facingMode;           ///< 0=FaceCamera, 1=VelocityAligned, 2=FaceCameraPlane, 3=CustomVector, 4=FaceCameraPosition, 5=PlaneAxis (plane normal from customFacingXYZ), 6=WorldZAligned.
    float    customFacingX;        ///< Custom facing direction X (facingMode 3 only).
    float    customFacingY;        ///< Custom facing direction Y (facingMode 3 only).
    float    customFacingZ;        ///< Custom facing direction Z (facingMode 3 only). Default: world Z-up.
    // ── Billboard stretching + blend (16 bytes) ──────────────────────────
    float    stretchFactor;        ///< Velocity stretch multiplier. 0=no stretch.
    uint     blendModeHint;        ///< 0=AlphaBlend, 1=Additive, 2=PremulAlpha (read by PS, declared for layout).
    uint     colorRampLutSrvIndex; ///< Bindless SRV for color ramp 256x1 LUT. 0xFFFFFFFF = legacy lerp (read by PS).
    // Repurposed from _alignPad — same 4 bytes, 16-byte boundary preserved.
    // ── Screen-space size (Feature: constant pixel coverage) ─────────────────
    /// When non-zero, overrides finalSize so the particle covers exactly
    /// screenSpacePixelSize pixels in diameter at its clip-space depth.
    ///
    /// @ref Valve 2006 "Rendering in Left 4 Dead" — constant-screen-size sprites
    uint     screenSpaceSizeEnabled; ///< 0=world-space (default), 1=screen-space constant pixel size.
    // ── Color by speed (VS→PS, 16 bytes) ──────────────────────────────────
    /// When non-zero, VS passes speed-normalized factor to PS for color lerp.
    uint     colorBySpeedEnabled;   ///< 0=off, 1=on. PS uses speed instead of age for color lerp.
    uint     flipbookRows;          ///< Sprite atlas rows (1=no flipbook). VS computes per-frame UVs.
    uint     flipbookCols;          ///< Sprite atlas cols (1=no flipbook).
    uint     flipbookTotalFrames;   ///< Total valid frames in atlas.
    // ── Flipbook FPS + channel mask (16 bytes) ──────────────────────────────
    float    _cbsFlipbookFPS;       ///< Flipbook playback rate. 0=lifetime-driven.
    uint     _cbsSpriteChannelMask; ///< PS reads spriteChannelMask here.
    float    _cbsVfxTotalTime;      ///< Global total time — matches PS vfxTotalTime.
    uint     _cbsPad4;              ///< 16-byte alignment padding.
};

// ─────────────────────────────────────────────────────────────────────────────
// Helper Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Applies 2D rotation around the billboard's facing axis (Z in billboard space).
///
/// Rotates the corner offset by the particle's rotation angle so individual
/// particles can spin without changing the camera-facing billboard orientation.
///
/// # Parameters
/// - offset: the [-1,+1] corner offset to rotate.
/// - rotationRad: rotation angle in radians (positive = counter-clockwise).
///
/// # Returns
/// Rotated 2D offset.
float2 RotateOffset(float2 offset, float rotationRad) {
    float sinR, cosR;
    sincos(rotationRad, sinR, cosR);
    return float2(
        offset.x * cosR - offset.y * sinR,
        offset.x * sinR + offset.y * cosR
    );
}

/// Builds billboard right/up axes for mode 0: FaceCamera.
/// Standard spherical billboard — fully tracks camera orientation in all axes.
/// Extracts right/up directly from the inverse view matrix rows.
///
/// @ref Niagara GPU sprite rendering — inverse view row extraction
void BasisFaceCamera(out float3 right, out float3 up)
{
    // For row-major storage: invView row 0 = camera right, row 1 = camera up.
    right = invView[0].xyz;
    up    = invView[1].xyz;
}

/// Builds billboard right/up axes for mode 1: VelocityAligned.
/// Long axis (up) follows the particle velocity direction. Produces stretched
/// trail-like sprites aligned with motion direction.
///
/// Falls back to camera-facing when velocity is near zero to avoid degenerate normals.
///
/// @ref Niagara GPU sprite rendering — velocity-aligned billboard orientation
///
/// # Parameters
/// - velDir: normalized particle velocity direction.
/// - camRight: camera right vector (fallback for degenerate cases).
/// - side: out — billboard right (perpendicular to velocity in screen plane).
/// - up: out — billboard up (along velocity direction).
void BasisVelocityAligned(float3 velDir, float3 camRight, out float3 side, out float3 up)
{
    float3 camToParticleHint = normalize(cameraPos.xyz - float3(0, 0, 0)); // generic hint
    float3 s = cross(velDir, cameraPos.xyz);
    float sideLen = length(s);
    side = (sideLen > 0.001f) ? (s / sideLen) : camRight;
    up   = velDir;
}

/// Builds billboard right/up axes for mode 2: FaceCameraPlane.
/// Y axis is locked to world up (Z in Z-up convention). The billboard rotates
/// only around the vertical axis to face the camera — never tilts sideways.
/// Ideal for upright sprites, ground decals, or signs that should remain vertical.
///
/// @ref Valve 2006 "Rendering in Left 4 Dead" — axis-aligned billboard (Y-locked)
///
/// # Parameters
/// - particlePos: world-space particle center.
/// - right: out — billboard right (in world XY plane).
/// - up: out — billboard up (world Z, locked).
void BasisFaceCameraPlane(float3 particlePos, out float3 right, out float3 up)
{
    // Compute the horizontal vector from particle to camera, zeroing the Z component.
    // This projects the look direction onto the XY plane (horizontal facing only).
    float3 toCam = cameraPos.xyz - particlePos;
    toCam.z = 0.0f; // zero out Z component (Z-up convention)
    float horizLen = length(toCam);
    // If particle is directly below/above camera, fall back to camera forward projected.
    float3 fwd = (horizLen > 0.001f) ? (toCam / horizLen) : float3(1.0f, 0.0f, 0.0f);
    // Right: perpendicular to fwd in horizontal plane, using world Z-up as the cross axis.
    right = normalize(cross(float3(0.0f, 0.0f, 1.0f), fwd));
    // Up: always world Z-up — billboard never tilts.
    up = float3(0.0f, 0.0f, 1.0f);
}

/// Builds billboard right/up axes for mode 3: CustomVector.
/// Long axis (up) is aligned to a fixed world-space direction regardless of camera.
/// Useful for sparks that fly upward, rain streaks falling down, or any effect
/// where the billboard should have a fixed world-space orientation.
///
/// @ref GDC 2019 "Niagara: Building the Future of FX in Unreal Engine" — fixed-axis sprites
///
/// # Parameters
/// - customDir: normalized world-space direction for the billboard long axis.
/// - right: out — billboard right (perpendicular to customDir and camera direction).
/// - up: out — billboard up (along customDir).
void BasisCustomVector(float3 customDir, out float3 right, out float3 up)
{
    // Use camera forward to compute the right vector perpendicular to the custom axis.
    // camera forward = -invView[2].xyz (row 2 of inverse view = camera forward in world space)
    float3 camFwd = -invView[2].xyz;
    float3 r = cross(customDir, camFwd);
    float rLen = length(r);
    // If customDir is parallel to camera forward, fall back to camera right to avoid NaN.
    right = (rLen > 0.001f) ? (r / rLen) : invView[0].xyz;
    up    = customDir; // long axis locked to custom direction
}

/// Builds billboard right/up axes for mode 4: FaceCameraPosition.
/// The billboard faces the actual camera POSITION rather than the camera view direction.
/// This produces correct parallax at wide FOV angles — edge particles rotate toward the
/// camera eye point rather than the screen center. Matches UE5 "Camera Facing" behavior.
///
/// @ref Latta 2004 "Inside Geometry Instancing" — camera-position facing billboard
///
/// # Parameters
/// - particlePos: world-space particle center.
/// - right: out — billboard right vector.
/// - up: out — billboard up vector.
void BasisFaceCameraPosition(float3 particlePos, out float3 right, out float3 up)
{
    // Vector from particle to camera eye — this is the billboard's facing direction.
    float3 toCamera = normalize(cameraPos.xyz - particlePos);
    // World up (Z-up convention). If toCamera is nearly parallel to world up,
    // fall back to world X to avoid degenerate cross product.
    float3 worldUp = (abs(toCamera.z) < 0.99f) ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    right = normalize(cross(worldUp, toCamera));
    up    = cross(toCamera, right); // re-orthogonalize: guaranteed perpendicular and unit length
}

/// Builds billboard right/up axes for mode 5: PlaneAxis.
///
/// The billboard rotates within the plane whose normal is `planeNormal`
/// (author-supplied via customFacingXYZ). The long axis (`up` output)
/// stays as the most camera-facing direction within that plane; the short
/// axis (`right`) is perpendicular to both plane normal and up.
///
/// Typical use: `planeNormal = (0,0,1)` to constrain billboards to the
/// XY plane (ground-locked sprites), or `(0,1,0)` for a wall decal.
///
/// @ref ENiagaraBillboardType::PlaneAxis
///
/// # Parameters
/// - particlePos:  world-space particle center (for parallax-correct up axis).
/// - planeNormal:  unit-length plane normal (rotation axis).
/// - right:        out — billboard right vector (perpendicular to planeNormal).
/// - up:           out — billboard up vector (lies in the plane, faces camera).
void BasisPlaneAxis(float3 particlePos, float3 planeNormal,
                    out float3 right, out float3 up)
{
    // Vector from particle to camera; we project it onto the plane to pick
    // the "most camera-facing" in-plane orientation. The plane projection is
    // toCamera - (toCamera · n) n.
    float3 toCamera = cameraPos.xyz - particlePos;
    float  dotN     = dot(toCamera, planeNormal);
    float3 inPlane  = toCamera - dotN * planeNormal;

    float inPlaneLen = length(inPlane);
    if (inPlaneLen < 0.001f) {
        // Degenerate: camera is on the plane normal. Pick an arbitrary in-plane
        // axis by taking the world-X cross; fall back to world-Y if collinear.
        float3 alt = (abs(planeNormal.x) < 0.99f) ? float3(1.0f, 0.0f, 0.0f)
                                                    : float3(0.0f, 1.0f, 0.0f);
        right = normalize(cross(planeNormal, alt));
    } else {
        // Right axis = planeNormal × inPlane. Because inPlane is in the plane,
        // this cross is also in the plane and perpendicular to inPlane.
        right = normalize(cross(planeNormal, inPlane / inPlaneLen));
    }
    up = cross(right, planeNormal); // in-plane, perpendicular to right
}

/// Builds billboard right/up axes for mode 6: WorldZAligned.
///
/// Long axis is locked to world up (Z axis). The short axis lies in the
/// horizontal plane, perpendicular to the camera-to-particle direction
/// projected onto horizontal. Equivalent to `PlaneAxis(planeNormal=(0,0,1))`
/// but cheaper — no dynamic planeNormal branch.
///
/// Common use: vertical rain streaks, vertical laser beams, upright flares.
///
/// @ref ENiagaraBillboardType::WorldUpAligned
///
/// # Parameters
/// - particlePos:  world-space particle center (for horizontal facing).
/// - right:        out — billboard right vector (horizontal, perpendicular to camera).
/// - up:           out — billboard up vector (always world Z).
void BasisWorldZAligned(float3 particlePos, out float3 right, out float3 up)
{
    const float3 worldUp = float3(0.0f, 0.0f, 1.0f);

    // Horizontal direction from particle to camera (drop Z).
    float2 toCam2D = cameraPos.xy - particlePos.xy;
    float  len2D   = length(toCam2D);

    if (len2D < 0.001f) {
        // Camera directly overhead — pick world-X as a stable fallback.
        right = float3(1.0f, 0.0f, 0.0f);
    } else {
        // Right axis: perpendicular to the in-plane toCamera, still in the
        // horizontal plane. Rotating (x,y) by 90° gives (-y,x).
        float2 horiz = toCam2D / len2D;
        right = float3(-horiz.y, horiz.x, 0.0f);
    }
    up = worldUp;
}

// ─────────────────────────────────────────────────────────────────────────────
// Vertex Shader Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Expands GPU particles into camera-facing billboard quads.
///
/// Uses SV_VertexID to determine which particle and which corner of the quad
/// this vertex represents. No vertex/index buffer is bound — called via
/// DrawIndirect with vertexCount = aliveParticles * 6.
///
/// # Parameters
/// - vertexID: sequential vertex index from DrawIndirect(vertexCount, 1, 0, 0).
///
/// # Returns
/// Clip-space position, UV, and particle color for the pixel shader.
VSOutput main(uint vertexID : SV_VertexID) {
    VSOutput output;

    // Determine which particle this vertex belongs to and which corner
    uint particleID   = vertexID / VERTICES_PER_PARTICLE;
    uint vertexInQuad = vertexID % VERTICES_PER_PARTICLE;

    // ── Read particle attributes from structured buffers ──
#ifdef UZ_BINDLESS
    StructuredBuffer<float4> positions  = ResourceDescriptorHeap[positionBufferIdx];
    StructuredBuffer<float>  sizes      = ResourceDescriptorHeap[sizeBufferIdx];
    StructuredBuffer<float4> colors     = ResourceDescriptorHeap[colorBufferIdx];
    StructuredBuffer<float>  rotations  = ResourceDescriptorHeap[rotationBufferIdx];
    StructuredBuffer<float4> velocities = ResourceDescriptorHeap[velocityBufferIdx];
    StructuredBuffer<float>  ages       = ResourceDescriptorHeap[agesBufferIdx];
    StructuredBuffer<float>  lifespans  = ResourceDescriptorHeap[lifespansBufferIdx];

    float3 particlePos   = positions[particleID].xyz;
    float  particleSize  = sizes[particleID];
    float4 particleColor = colors[particleID];
    float  particleRot   = rotations[particleID];
    float3 particleVel   = velocities[particleID].xyz;
    float  particleAge   = ages[particleID];
    float  particleLife  = lifespans[particleID];

    // Dynamic material parameters (Task 4.14.0)
    // Guard: descriptor index 0 or UINT32_MAX means no dynamic params buffer.
    float4 particleDynParams = float4(0, 0, 0, 0);
    if (dynamicParamsBufferIdx != 0 && dynamicParamsBufferIdx != 0xFFFFFFFF) {
        StructuredBuffer<float4> dynParams = ResourceDescriptorHeap[dynamicParamsBufferIdx];
        particleDynParams = dynParams[particleID];
    }
#else
    float3 particlePos   = particlePositions[particleID].xyz;
    float  particleSize  = particleSizes[particleID];
    float4 particleColor = particleColors[particleID];
    float  particleRot   = particleRotations[particleID];
    float3 particleVel   = particleVelocities[particleID].xyz;
    float  particleAge   = particleAges[particleID];
    float  particleLife  = particleLifespans[particleID];
    float4 particleDynParams = float4(0, 0, 0, 0); // SM5: no dynamic params buffer
#endif

    // ── Cull dead/uninitialized particles ──
    // ── Cull dead/uninitialized particles ──
    if (particleLife <= 0.0f || particleAge >= particleLife) {
        output.positionCS     = float4(0, 0, 0, 1);
        output.texcoord       = float2(0, 0);
        output.color          = float4(0, 0, 0, 0);
        output.positionWS     = float3(0, 0, 0);
        output.age            = 0;
        output.lifetime       = 0;
        output.particleIdx    = particleID;
        output.prevPositionCS = float4(0, 0, 0, 1);
        output.speedFactor    = 0;
        output.dynamicParams  = float4(0, 0, 0, 0);
        output.flipbookUV0    = float2(0, 0);
        output.flipbookUV1    = float2(0, 0);
        output.flipbookLerp   = 0;
        return output;
    }

    // ── Evaluate size over lifetime (piecewise-linear: start→peak→end) ──
    // normalizedAge in [0,1] maps the particle's age to its full lifespan.
    // Before peakTime: lerp from start to peak size.
    // After peakTime: lerp from peak to end size.
    float normalizedAge = saturate(particleAge / max(particleLife, 0.0001f));
    float sizeLifeScale;
    if (normalizedAge < sizeOverLifePeakTime) {
        // Rising phase: birth → peak
        float t = (sizeOverLifePeakTime > 0.0001f)
                ? (normalizedAge / sizeOverLifePeakTime)
                : 1.0f;
        sizeLifeScale = lerp(sizeOverLifeStart, sizeOverLifePeak, t);
    } else {
        // Falling phase: peak → death
        float remaining = 1.0f - sizeOverLifePeakTime;
        float t = (remaining > 0.0001f)
                ? ((normalizedAge - sizeOverLifePeakTime) / remaining)
                : 1.0f;
        sizeLifeScale = lerp(sizeOverLifePeak, sizeOverLifeEnd, t);
    }

    // ── Apply global emitter overrides ──
    float  finalSize  = particleSize * globalSizeMultiplier * sizeLifeScale;
    float4 finalColor = particleColor * globalColorTint;

    // ── Size by speed (Task 4.8.0) ──
    // Maps particle speed to a size multiplier via linear remap [0, speedRangeMax] → [sizeBySpeedMin, sizeBySpeedMax].
    // Applied after size-over-life so both effects compose multiplicatively.
    if (sizeBySpeedEnabled != 0u) {
        float speedForSize = length(particleVel);
        float speedT = saturate(speedForSize / max(speedRangeMax, 0.001f));
        float speedSizeMul = lerp(sizeBySpeedMin, sizeBySpeedMax, speedT);
        finalSize *= speedSizeMul;
    }

    // ── Screen-space size (Feature: constant pixel coverage) ──
    // When screenSpaceSizeEnabled is non-zero, compute the world-space diameter
    // that produces exactly screenSpacePixelSize pixels on screen at this particle's
    // clip-space depth.  The formula derives from the perspective projection:
    //   NDC size = worldSize * P11 / clipW   (P11 = proj[1][1] for vertical FOV)
    //   pixelSize = NDC size * (screenH / 2)
    // Solving for worldSize:
    //   worldSize = (screenSpacePixelSize * clipW) / (P11 * screenH / 2)
    //             = (screenSpacePixelSize * clipW * 2) / (P11 * screenH)
    // P11 from a standard projection matrix = 1/tan(fovY/2). We extract it from
    // the viewProj matrix: viewProj[1][1] (row-major, column index 1 of row 1).
    // clipW = distance along the view-Z axis = dot(particlePos - cameraPos, camFwd)
    //       which equals the w component of the clip-space point.
    //
    // @ref Valve 2006 "Rendering in Left 4 Dead" — constant-screen-size sprites
    // @ref Lengyel 2019 "Foundations of Game Engine Development Vol.2" Ch.5 — projection math
    if (screenSpaceSizeEnabled != 0u) {
        // Compute clip-space w for this particle position.  This equals the view-space Z,
        // i.e. the eye-space depth.  It drives the perspective divide and therefore the
        // mapping between world size and pixel size.
        float4 clipPos = mul(float4(particlePos, 1.0f), viewProj);
        float  clipW   = max(clipPos.w, 0.0001f); // guard against clip plane / behind-camera

        // Extract the vertical scale from the projection matrix (P[1][1] in row-major).
        // For a standard symmetric perspective: P11 = 1 / tan(fovY/2).
        // We use viewProj[1][1] because the view part is orthogonal and doesn't affect
        // the (1,1) element for standard cameras.
        float projY = viewProj[1][1]; // yscale = cot(fovY/2)
        float safeProjectY = max(abs(projY), 0.0001f);

        // screenSpacePixelSize is the desired particle diameter in pixels.
        // screenHeight is approximated as 720 pixels — a reasonable constant that scales
        // the pixel-to-world ratio for typical render targets.  The exact value does NOT
        // need to match the actual resolution: it only affects the absolute pixel count
        // at any given distance, not distance-independence itself.  Callers can tune
        // screenSpacePixelSize proportionally if rendering at a different height.
        // @constants kScreenHeight=720.0 (internal constant, proportional scaling)
        float screenHeight = 720.0f;

        // Solve for world diameter: worldSize = (pixelSize * clipW * 2) / (projY * screenH)
        float worldDiameter = (screenSpacePixelSize * clipW * 2.0f) / (safeProjectY * screenHeight);
        finalSize = worldDiameter;
    }

    // ── Camera offset (Task 4.7.0) ──
    // Push/pull the particle along the camera-to-particle direction before billboard
    // expansion. Positive values push away from camera (depth layering), negative
    // values pull toward camera (prevent z-fighting with geometry).
    /// @ref Latta 2004 "Inside Geometry Instancing" — camera offset for depth layering
    if (cameraOffset != 0.0f) {
        float3 viewDir = normalize(cameraPos.xyz - particlePos);
        particlePos += viewDir * cameraOffset;
    }

    // ── Extract camera right/up from inverse view matrix ──
    // For row-major storage, row 0 = right vector, row 1 = up vector.
    // These vectors are unit-length in world space, forming the billboard plane.
    float3 cameraRight = invView[0].xyz;
    float3 cameraUp    = invView[1].xyz;

    // ── Compute billboard corner offset ──
    float2 cornerOffset = kCornerOffsets[vertexInQuad];

    // ── Pivot offset (Task 4.7.0) ──
    // Shift the billboard expansion origin in sprite-local space before rotation.
    // pivotOffsetX/Y in [-1,1]: (0,-1)=bottom anchor, (0,1)=top anchor.
    // The corner offsets are in [-1,+1], so we add the pivot shift directly.
    /// @ref Unity VFX Graph 2021 "Pivot Offset" — sprite pivot in local space
    cornerOffset += float2(pivotOffsetX, pivotOffsetY);

    // Apply per-particle rotation around the billboard's facing axis
    cornerOffset = RotateOffset(cornerOffset, particleRot);

    // Scale by half-size (offset is [-1,+1], so multiply by half to get world units)
    float halfSize = finalSize * 0.5f;

    // ── Billboard orientation and stretching ──
    // Selects right/up axes based on facingMode. Each mode calls a dedicated
    // helper function that returns a consistent (right, up) pair in world space.
    // stretchFactor elongates the vertical (up) axis proportional to speed for
    // both velocity-aligned and camera-facing modes.
    //
    // @ref Latta 2004 "Inside Geometry Instancing" — camera-position billboard
    // @ref Valve 2006 "Rendering in Left 4 Dead" — axis-aligned billboard
    // @ref Niagara GPU sprite rendering — velocity-aligned billboard orientation
    float3 billboardRight;
    float3 billboardUp;
    float  halfW = halfSize; // horizontal half-extent
    float  halfH = halfSize; // vertical half-extent (elongated by stretchFactor)

    float  speed = length(particleVel);

    switch (facingMode)
    {
        case 1: // VelocityAligned — long axis follows particle velocity
        {
            if (speed > 0.01f) {
                float3 velDir = particleVel / speed; // already normalized via stored length
                BasisVelocityAligned(velDir, cameraRight, billboardRight, billboardUp);
                // Stretch along velocity direction (up axis) proportional to speed.
                // 0.1f scaling keeps the multiplier reasonable at typical game velocities.
                // Clamped to 10x to prevent degenerate thin triangles.
                if (stretchFactor > 0.0f) {
                    halfH = halfSize + halfSize * speed * stretchFactor * 0.1f;
                    halfH = min(halfH, halfSize * 10.0f);
                }
            } else {
                // Degenerate case: velocity too small — fall back to camera-facing.
                BasisFaceCamera(billboardRight, billboardUp);
            }
            break;
        }
        case 2: // FaceCameraPlane — billboard rotates around world Z, never tilts
        {
            BasisFaceCameraPlane(particlePos, billboardRight, billboardUp);
            break;
        }
        case 3: // CustomVector — long axis locked to a fixed world-space direction
        {
            // Normalize on GPU as a safety net; CPU should upload a unit vector.
            float3 customDir = float3(customFacingX, customFacingY, customFacingZ);
            float  dirLen    = length(customDir);
            customDir = (dirLen > 0.001f) ? (customDir / dirLen) : float3(0.0f, 0.0f, 1.0f);
            BasisCustomVector(customDir, billboardRight, billboardUp);
            break;
        }
        case 4: // FaceCameraPosition — faces camera EYE position, not view direction
        {
            BasisFaceCameraPosition(particlePos, billboardRight, billboardUp);
            break;
        }
        case 5: // PlaneAxis — rotate within the plane whose normal is customFacingXYZ
        {
            float3 planeNormal = float3(customFacingX, customFacingY, customFacingZ);
            float  pnLen       = length(planeNormal);
            // Default to world-Z when the plane normal is unset / zero.
            planeNormal = (pnLen > 0.001f) ? (planeNormal / pnLen)
                                            : float3(0.0f, 0.0f, 1.0f);
            BasisPlaneAxis(particlePos, planeNormal, billboardRight, billboardUp);
            break;
        }
        case 6: // WorldZAligned — long axis locked to world up
        {
            BasisWorldZAligned(particlePos, billboardRight, billboardUp);
            break;
        }
        default: // 0: FaceCamera — standard full-spherical camera-facing billboard
        {
            BasisFaceCamera(billboardRight, billboardUp);
            // In camera-facing mode, stretch projects along camera up axis when moving.
            if (stretchFactor > 0.0f && speed > 0.01f) {
                halfH = halfSize + halfSize * speed * stretchFactor * 0.1f;
                halfH = min(halfH, halfSize * 10.0f);
            }
            break;
        }
    }

    // ── Compute world-space vertex position ──
    // Billboard: offset particle center by orientation-aligned axes with per-axis extents
    float3 worldPos = particlePos
                    + billboardRight * (cornerOffset.x * halfW)
                    + billboardUp    * (cornerOffset.y * halfH);

    output.positionWS = worldPos;

    // ── Transform to clip space ──
    output.positionCS = mul(float4(worldPos, 1.0f), viewProj);

    // ── Compute previous frame clip-space position for TAA motion vectors ──
    // Estimate previous world position by subtracting velocity * deltaTime from
    // the particle center, then applying the same billboard corner expansion.
    // This gives per-particle motion vectors that account for particle movement
    // but assume the billboard expansion stays the same (camera-relative).
    //
    // @ref Karis 2014 "High Quality Temporal Supersampling" — per-object motion vectors
    float dt = timeParams.y; // deltaTime from PerFrameCB
    float3 prevParticlePos = particlePos - particleVel * dt;
    float3 prevWorldPos = prevParticlePos
                        + billboardRight * (cornerOffset.x * halfW)
                        + billboardUp    * (cornerOffset.y * halfH);
    output.prevPositionCS = mul(float4(prevWorldPos, 1.0f), prevViewProj);

    // ── Pass UV and color to pixel shader ──
    output.texcoord = kCornerUVs[vertexInQuad];
    output.color    = finalColor;

    // ── Pass particle lifetime attributes to pixel shader ──
    output.age         = particleAge;
    output.lifetime    = particleLife;
    output.particleIdx = particleID;

    // ── Color by speed: pass normalized speed factor to PS ──
    // speed is already computed above for billboard stretching/size-by-speed.
    // Always write the interpolant; PS checks colorBySpeedEnabled to use it.
    output.speedFactor = saturate(speed / max(speedRangeMax, 0.001f));

    // ── Dynamic material parameters (Task 4.14.0) ──
    // Pass through per-particle float4 to PS for shader-driven effects.
    // Override .x with authoritative normalizedAge (computed from VS-visible
    // particleAge/particleLife). PS color-over-life reads this field.
    output.dynamicParams = particleDynParams;
    output.dynamicParams.x = saturate(particleAge / max(particleLife, 0.0001f));

    // ── Niagara-style atlas UV pre-computation (per-vertex) ──────────────
    // Reads the per-particle subImageIndex written by the update CS each
    // frame and computes the atlas UVs for the current frame, the next
    // frame, and the blend factor between them. The PS simply samples
    // at these UVs and lerps — no flipbook math runs in the PS.
    //
    /// @ref Niagara NiagaraSpriteVertexFactory.ush — SubUV VS pre-computation
    /// @ref Nguyen 2007 "GPU Gems 3" ch.23 — SubUV sprite sheet animation
    if (flipbookTotalFrames > 1u) {
        // Read subImageIndex from SoA channel (written each frame by update CS).
        float subImgIdx = 0.0f;
#ifdef UZ_BINDLESS
        if (subImageIndexBufferIdx != 0xFFFFFFFFu) {
            StructuredBuffer<float> subImages = ResourceDescriptorHeap[subImageIndexBufferIdx];
            subImgIdx = subImages[particleID];
        }
#endif
        // Shared atlas helper — see `flipbook_atlas.hlsli`. Keeps the math
        // identical between this path and the debug quad overlay.
        uint  frameA;
        uint  frameB;
        float lerpT;
        ComputeFlipbookFrames(subImgIdx, flipbookTotalFrames, frameA, frameB, lerpT);

        float2 cornerUV    = kCornerUVs[vertexInQuad];
        output.flipbookUV0 = AtlasUV(frameA, flipbookRows, flipbookCols, cornerUV);
        output.flipbookUV1 = AtlasUV(frameB, flipbookRows, flipbookCols, cornerUV);
        output.flipbookLerp = lerpT;
    } else {
        // No flipbook — pass straight corner UVs.
        output.flipbookUV0  = kCornerUVs[vertexInQuad];
        output.flipbookUV1  = kCornerUVs[vertexInQuad];
        output.flipbookLerp = 0.0f;
    }

    return output;
}
