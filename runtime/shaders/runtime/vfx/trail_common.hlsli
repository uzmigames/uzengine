/// Trail/ribbon shared definitions — vertex layout, VS output, root constants,
/// and common constants.
///
/// Included by both trail_vs.hlsl and trail_ps.hlsl to guarantee that the
/// interpolated struct layout is identical on both sides of the rasterizer.
///
/// # Trail Geometry Model
/// A trail is a camera-facing ribbon built from a chain of world-space anchor
/// points. The CPU generates a quad strip between consecutive anchors, expanding
/// each edge perpendicular to the view direction. Each quad vertex carries a
/// pre-computed UV (U = time-normalized position along the trail [0,1],
/// V = 0 = left edge / 1 = right edge) and a per-vertex color that encodes
/// age fade and lifetime tint.
///
/// # Coordinate Conventions
/// - World space: Y-up, right-handed (same as the rest of UzEngine).
/// - Clip space: reversed-Z (near=1, far=0) — never assume depth direction.
/// - UVs: U scrolls along the trail length (top-level), V across the width.
///
/// # Bindless Vertex Fetch
/// The trail VS uses SV_VertexID with bindless StructuredBuffer reads instead
/// of the Input Assembler. This matches the engine's SM6.6 bindless root
/// signature which does not declare IA support. The VS reads an index from
/// StructuredBuffer<uint> (index buffer SRV) and then fetches the vertex from
/// StructuredBuffer<TrailVertexData> (vertex buffer SRV).
///
/// # Register Conventions (Shared Root Signature)
/// - b0: TrailRootConstants (32 bytes, root constants — shared by VS and PS)
/// - b1: PerFrameCB         (root CBV — viewProj, timeParams, nearPlane, farPlane)
/// - t0: trailTexture       (PS color/alpha map, SM5 fallback)
/// - t1: sceneDepth         (PS soft-particle depth fade, SM5 fallback)
/// - s0: linearSampler      (bilinear + wrap, static sampler from root signature)

#ifndef TRAIL_COMMON_HLSLI
#define TRAIL_COMMON_HLSLI

// ─────────────────────────────────────────────────────────────────────────────
// Shared Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Minimum alpha below which a fragment is considered transparent.
/// Matches the default alphaClipThreshold in cb_TrailPixelConstants so
/// shaders compiled without the PS cbuffer still have a sensible fallback.
static const float kTrailAlphaClipMin = 1.0f / 255.0f;

/// Discard threshold for soft-particle depth fade comparison.
/// Avoids a division by zero when softDepthRange is set to exactly 0.
static const float kSoftDepthEpsilon = 1e-5f;

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants (b0) — 32 bytes, shared by VS and PS
// ─────────────────────────────────────────────────────────────────────────────

/// Per-draw-call root constants for trail rendering (8 x uint32 = 32 bytes).
///
/// Packed into root parameter 0 (b0) via SetRootConstants. Both vertex and
/// pixel shaders share this memory, so the layout is unified.
///
/// uvScrollRate:          UV-space units per second applied to the U axis in the VS.
///                        Positive scrolls head-to-tail. 0 disables scrolling.
///
/// softDepthRange:        World-space fade radius in metres for soft-particle
///                        depth fade in the PS. 0 disables the feature (skips
///                        depth texture read).
///
/// alphaClipThreshold:    Fragments below this alpha are discarded. Typical: 1/255.
///
/// trailTexSrvIndex:      Bindless SRV heap index for the trail color/alpha texture.
///                        0xFFFFFFFF = no texture (vertex color only).
///
/// depthBufferSrvIndex:   Bindless SRV heap index for the reversed-Z scene depth.
///                        Only sampled when softDepthRange > 0.
///
/// vertexBufferSrvIndex:  Bindless SRV heap index for StructuredBuffer<TrailVertexData>.
///                        Used by the VS for bindless vertex fetch via SV_VertexID.
///
/// indexBufferSrvIndex:   Bindless SRV heap index for StructuredBuffer<uint> (index buffer).
///                        The VS uses SV_VertexID to index into this buffer, then
///                        fetches the actual vertex from the vertex buffer.
struct TrailRootConstants {
    float uvScrollRate;          ///< U-axis scroll speed (UV-units/second).
    float softDepthRange;        ///< Soft-fade radius in metres. 0 = disabled.
    float alphaClipThreshold;    ///< Alpha discard threshold [0, 1].
    uint  trailTexSrvIndex;      ///< Bindless SRV index for trail texture (0xFFFFFFFF = none).
    uint  depthBufferSrvIndex;   ///< Bindless SRV index for scene depth buffer.
    uint  vertexBufferSrvIndex;  ///< Bindless SRV index for trail StructuredBuffer<TrailVertexData>.
    uint  indexBufferSrvIndex;   ///< Bindless SRV index for trail StructuredBuffer<uint>.
    uint  _pad0;                 ///< Alignment pad — unused.
};

// ─────────────────────────────────────────────────────────────────────────────
// Vertex Data — matches TrailVertex on the CPU side (36 bytes)
// ─────────────────────────────────────────────────────────────────────────────

/// Per-vertex data layout in the StructuredBuffer, generated by the CPU ribbon
/// builder. The VS reads this via bindless SRV (no Input Assembler).
///
/// The memory layout MUST match the C++ TrailVertex struct exactly:
///   float3 position (12 bytes) + float u (4 bytes) + float v (4 bytes) +
///   float4 color (16 bytes) = 36 bytes total.
///
/// position: World-space vertex position of the expanded ribbon quad edge.
///           The CPU widens each anchor point perpendicular to the camera look
///           direction by half the trail width.
///
/// u, v:     Texture coordinates.
///             u: normalized width across the ribbon [0=left, 1=right].
///             v: normalized position along the trail [0=head, 1=tail].
///
/// color:    RGBA per-vertex color. The CPU encodes lifetime fade in the alpha
///           channel: alpha reaches 0 when the ribbon segment dies. The RGB
///           tint is set by the emitter's color-over-lifetime curve.
struct TrailVertexData {
    float3 position; ///< World-space ribbon edge position.
    float  u;        ///< Texture U: ribbon width [0=left, 1=right].
    float  v;        ///< Texture V: arc-length along trail [0=head, 1=tail].
    float4 color;    ///< RGBA per-vertex tint and lifetime alpha.
};

// ─────────────────────────────────────────────────────────────────────────────
// Vertex Shader Output / Pixel Shader Input
// ─────────────────────────────────────────────────────────────────────────────

/// Interpolants passed from the trail vertex shader to the pixel shader.
///
/// positionCS: Clip-space position for rasterization (SV_Position semantic).
///             The rasterizer converts this to screen-space for the PS.
///
/// uv:         Scrolled texture coordinates. The VS applies the UV scroll
///             offset so the PS samples at the animated coordinates without
///             needing the time constant again.
///
/// color:      Per-vertex RGBA tint with lifetime alpha, linearly interpolated
///             across the ribbon. The PS multiplies this against the texture
///             sample before alpha testing.
///
/// positionVS: View-space position, used by the PS for soft-particle depth
///             comparison. The W component carries the linear view-space depth
///             (positive = in front of camera). Stored as float4 to leverage
///             the free W interpolant.
struct TrailVSOutput {
    float4 positionCS : SV_Position; ///< Clip-space position for rasterization.
    float2 uv         : TEXCOORD0;   ///< Scrolled (trailU, widthV) texture coords.
    float4 color      : COLOR0;      ///< RGBA per-vertex tint and lifetime alpha.
    float  linearDepth : TEXCOORD1;  ///< View-space linear depth (positive, metres).
};

#endif // TRAIL_COMMON_HLSLI
