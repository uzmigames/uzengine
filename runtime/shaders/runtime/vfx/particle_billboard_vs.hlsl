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
/// @constants VERTICES_PER_PARTICLE=6
/// @deviations NONE

#include "../include/depth_common.hlsli"

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
    uint _vfxPad0;
    uint _vfxPad1;
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
    float    _psPad0;
    float    _pad1;
    float    _pad2;
    float    _pad3;
    float    _pad4;
    float    _pad5;
    float    _pad6;
    float    _pad7;
    float    _pad8;
    // ── Size over lifetime (analytical piecewise-linear curve, 16 bytes) ──
    float    sizeOverLifeStart;    ///< Size multiplier at birth (t=0).
    float    sizeOverLifePeak;     ///< Size multiplier at peak time.
    float    sizeOverLifeEnd;      ///< Size multiplier at death (t=1).
    float    sizeOverLifePeakTime; ///< Normalized age [0,1] where peak occurs.
    // ── Color over lifetime (linear interpolation, 32 bytes) ──
    float4   colorLifeStart;       ///< RGBA color at birth.
    float4   colorLifeEnd;         ///< RGBA color at death.
    // ── Billboard alignment and stretching (16 bytes) ──
    uint     alignmentMode;        ///< 0=CameraFacing, 1=VelocityAligned.
    float    stretchFactor;        ///< Velocity stretch multiplier. 0=no stretch.
    uint     blendModeHint;        ///< 0=AlphaBlend, 1=Additive, 2=PremulAlpha (read by PS, declared for layout).
    uint     colorRampLutSrvIndex; ///< Bindless SRV for color ramp 256x1 LUT. 0xFFFFFFFF = legacy lerp (read by PS).
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
#else
    float3 particlePos   = particlePositions[particleID].xyz;
    float  particleSize  = particleSizes[particleID];
    float4 particleColor = particleColors[particleID];
    float  particleRot   = particleRotations[particleID];
    float3 particleVel   = particleVelocities[particleID].xyz;
    float  particleAge   = particleAges[particleID];
    float  particleLife  = particleLifespans[particleID];
#endif

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

    // ── Extract camera right/up from inverse view matrix ──
    // For row-major storage, row 0 = right vector, row 1 = up vector.
    // These vectors are unit-length in world space, forming the billboard plane.
    float3 cameraRight = invView[0].xyz;
    float3 cameraUp    = invView[1].xyz;

    // ── Compute billboard corner offset ──
    float2 cornerOffset = kCornerOffsets[vertexInQuad];

    // Apply per-particle rotation around the billboard's facing axis
    cornerOffset = RotateOffset(cornerOffset, particleRot);

    // Scale by half-size (offset is [-1,+1], so multiply by half to get world units)
    float halfSize = finalSize * 0.5f;

    // ── Billboard orientation and stretching ──
    // Determine right/up axes and per-axis half-extents based on alignment mode.
    //
    // @ref Niagara GPU sprite rendering — velocity-aligned billboard orientation
    // Camera-facing: right/up from inverse view matrix (standard billboard).
    // Velocity-aligned: tangent along velocity, bitangent perpendicular in the
    // plane formed by velocity and camera-to-particle direction. Falls back to
    // camera-facing when velocity is near zero to avoid degenerate normals.
    float3 billboardRight;
    float3 billboardUp;
    float  halfW = halfSize; // horizontal half-extent (perpendicular to velocity)
    float  halfH = halfSize; // vertical half-extent (along velocity / up)

    float  speed = length(particleVel);

    if (alignmentMode == 1 && speed > 0.01f) {
        // Velocity-aligned: tangent along velocity direction
        float3 velDir = particleVel / speed; // normalize without recomputing length
        float3 camToParticle = normalize(cameraPos.xyz - particlePos);

        // Compute side vector perpendicular to both velocity and camera direction.
        // If velocity is nearly parallel to camera direction, fall back to camera up.
        float3 side = cross(velDir, camToParticle);
        float sideLen = length(side);
        if (sideLen > 0.001f) {
            side /= sideLen;
        } else {
            side = cameraRight; // fallback for degenerate case
        }

        billboardRight = side;
        billboardUp    = velDir;

        // Stretch along velocity direction (up axis) proportional to speed.
        // stretchFactor controls how aggressively particles elongate.
        // 0.1 scaling keeps the multiplier reasonable at typical game velocities.
        // Clamped to 10x particle size to prevent extremely thin triangles.
        if (stretchFactor > 0.0f) {
            halfH = halfSize + halfSize * speed * stretchFactor * 0.1f;
            halfH = min(halfH, halfSize * 10.0f);
        }
    } else {
        // Camera-facing (default behavior)
        billboardRight = cameraRight;
        billboardUp    = cameraUp;

        // In camera-facing mode, stretch projects along camera up axis when moving.
        // This gives a subtle motion-blur feel without changing billboard orientation.
        if (stretchFactor > 0.0f && speed > 0.01f) {
            halfH = halfSize + halfSize * speed * stretchFactor * 0.1f;
            halfH = min(halfH, halfSize * 10.0f);
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

    return output;
}
