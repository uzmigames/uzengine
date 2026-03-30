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
/// - b0: VFXBillboardIndices (bindless descriptor indices for particle buffers)
/// - b1: PerFrameCB (viewProj, cameraPos, invView for billboard vectors)
/// - b2: VFXEmitterCB (per-emitter parameters, prevViewProj for motion vectors)
/// - s0: not used in VS
///
/// # Outputs
/// In addition to clip-space position, UV, and color, this shader outputs:
/// - age/lifetime/particleIdx for flipbook animation in the pixel shader
/// - prevPositionCS for TAA motion vector computation
///
/// @ref Niagara GPU sprite rendering — billboard expansion from SV_VertexID
/// @ref Karis 2014 "High Quality Temporal Supersampling" — per-object motion vectors
/// @constants VERTICES_PER_PARTICLE=6
/// @deviations NONE

#include "../runtime/include/depth_common.hlsli"

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
    float2 texcoord       : TEXCOORD0;   ///< UV for sprite texture sampling.
    float4 color          : COLOR0;      ///< Particle color (RGBA), interpolated per-vertex.
    float3 positionWS     : TEXCOORD1;   ///< World-space position for soft particle depth fade.
    float  age            : TEXCOORD2;   ///< Particle age in seconds (for flipbook animation in PS).
    float  lifetime       : TEXCOORD3;   ///< Particle total lifetime in seconds.
    uint   particleIdx    : TEXCOORD4;   ///< Particle index for flipbook deterministic random frame.
    float4 prevPositionCS : TEXCOORD5;   ///< Previous frame clip-space position for TAA motion vectors.
};

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants (b0) — Bindless Descriptor Indices
// ─────────────────────────────────────────────────────────────────────────────

#ifdef UZ_BINDLESS
/// Root constants for bindless resource access.
/// Contains SRV descriptor heap indices for particle attribute buffers.
/// These are written by the CPU each frame and point into the global
/// descriptor heap where the simulation compute pass left particle data.
struct VFXBillboardIndices {
    uint positionBufferIdx;   ///< SRV index: StructuredBuffer<float4> (xyz=pos, w=unused).
    uint sizeBufferIdx;       ///< SRV index: StructuredBuffer<float> (uniform scale).
    uint colorBufferIdx;      ///< SRV index: StructuredBuffer<float4> (rgba).
    uint rotationBufferIdx;   ///< SRV index: StructuredBuffer<float> (radians, Z-axis rotation).
    uint sortedIndicesSrvIndex; ///< SRV index: StructuredBuffer<uint> (sorted draw order).
    uint velocityBufferIdx;   ///< SRV index: StructuredBuffer<float4> (xyz=velocity, w=unused).
    uint agesBufferIdx;       ///< SRV index: StructuredBuffer<float> (particle age in seconds).
    uint lifespansBufferIdx;  ///< SRV index: StructuredBuffer<float> (particle total lifetime).
};
ConstantBuffer<VFXBillboardIndices> vfxIndices : register(b0);
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
// Per-Emitter Constant Buffer (b2) — VFX Emitter Parameters
// ─────────────────────────────────────────────────────────────────────────────

/// Per-emitter parameters uploaded each frame via the CB ring allocator.
/// Contains the inverse view matrix (for billboard orientation) and emitter
/// transform, plus optional size/color overrides.
cbuffer VFXEmitterCB : register(b2) {
    row_major float4x4 invView;        ///< Inverse view matrix — rows 0,1 give camera right/up.
    float3   emitterPosition;          ///< World-space emitter origin (for local-space particles).
    float    globalSizeMultiplier;     ///< Multiplied with per-particle size (default 1.0).
    float4   globalColorTint;          ///< Multiplied with per-particle color (default white).
    row_major float4x4 prevViewProj;   ///< Previous frame's view * projection for TAA motion vectors.
                                       ///< Set to identity when no previous frame is available,
                                       ///< producing zero motion vectors on the first frame.
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

    // ── Resolve sorted index for back-to-front transparency ordering ──
    // The simulation/sort pass produces a sortedIndices buffer that maps
    // draw-order particle ID to the actual attribute buffer index.
    // Reading through this indirection ensures back-to-front rendering.
    uint sortedIdx = particleID;
#ifdef UZ_BINDLESS
    if (vfxIndices.sortedIndicesSrvIndex > 0) {
        StructuredBuffer<uint> sortedIndices = ResourceDescriptorHeap[vfxIndices.sortedIndicesSrvIndex];
        sortedIdx = sortedIndices[particleID];
    }
#endif

    // ── Read particle attributes from structured buffers ──
#ifdef UZ_BINDLESS
    StructuredBuffer<float4> positions  = ResourceDescriptorHeap[vfxIndices.positionBufferIdx];
    StructuredBuffer<float>  sizes      = ResourceDescriptorHeap[vfxIndices.sizeBufferIdx];
    StructuredBuffer<float4> colors     = ResourceDescriptorHeap[vfxIndices.colorBufferIdx];
    StructuredBuffer<float>  rotations  = ResourceDescriptorHeap[vfxIndices.rotationBufferIdx];
    StructuredBuffer<float4> velocities = ResourceDescriptorHeap[vfxIndices.velocityBufferIdx];
    StructuredBuffer<float>  ages       = ResourceDescriptorHeap[vfxIndices.agesBufferIdx];
    StructuredBuffer<float>  lifespans  = ResourceDescriptorHeap[vfxIndices.lifespansBufferIdx];

    float3 particlePos   = positions[sortedIdx].xyz;
    float  particleSize  = sizes[sortedIdx];
    float4 particleColor = colors[sortedIdx];
    float  particleRot   = rotations[sortedIdx];
    float3 particleVel   = velocities[sortedIdx].xyz;
    float  particleAge   = ages[sortedIdx];
    float  particleLife  = lifespans[sortedIdx];
#else
    float3 particlePos   = particlePositions[sortedIdx].xyz;
    float  particleSize  = particleSizes[sortedIdx];
    float4 particleColor = particleColors[sortedIdx];
    float  particleRot   = particleRotations[sortedIdx];
    float3 particleVel   = particleVelocities[sortedIdx].xyz;
    float  particleAge   = particleAges[sortedIdx];
    float  particleLife  = particleLifespans[sortedIdx];
#endif

    // ── Apply global emitter overrides ──
    float  finalSize  = particleSize * globalSizeMultiplier;
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

    // ── Compute world-space vertex position ──
    // Billboard: offset particle center by camera-aligned right/up vectors
    float3 worldPos = particlePos
                    + cameraRight * (cornerOffset.x * halfSize)
                    + cameraUp    * (cornerOffset.y * halfSize);

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
                        + cameraRight * (cornerOffset.x * halfSize)
                        + cameraUp    * (cornerOffset.y * halfSize);
    output.prevPositionCS = mul(float4(prevWorldPos, 1.0f), prevViewProj);

    // ── Pass UV and color to pixel shader ──
    output.texcoord = kCornerUVs[vertexInQuad];
    output.color    = finalColor;

    // ── Pass flipbook attributes to pixel shader ──
    output.age         = particleAge;
    output.lifetime    = particleLife;
    output.particleIdx = particleID;

    return output;
}
