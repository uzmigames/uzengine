/// Trail/ribbon vertex shader — transforms ribbon vertices to clip space
/// and applies UV scrolling for animated trail textures.
///
/// The ribbon geometry is generated entirely on the CPU: the emitter system
/// walks the chain of anchor points and produces a quad strip, expanding each
/// edge perpendicular to the camera look direction. This VS merely projects
/// the final world-space positions to clip space and animates the U coordinate.
///
/// # Bindless Vertex Fetch
/// Instead of using the Input Assembler (which requires IA support in the root
/// signature), this shader reads trail vertices from a StructuredBuffer via
/// SV_VertexID. The draw call uses Draw(indexCount) where each "vertex" maps
/// to an entry in the index buffer SRV, which then indexes into the vertex
/// buffer SRV. This matches the engine's SM6.6 bindless root signature.
///
/// # UV Scroll
/// The U coordinate (along the trail length) is offset by `time * uvScrollRate`
/// each frame. A positive rate scrolls texture detail from head toward tail,
/// giving the illusion of motion along the ribbon. The V coordinate (width)
/// is passed through unchanged.
///
/// # Pipeline State Requirements
/// - No Input Assembler: vertices fetched from StructuredBuffer via SV_VertexID.
/// - Rasterizer: CullMode = NONE (ribbon is single-sided but may fold).
/// - DepthStencil: DepthWrite = OFF (trails composite on top, no depth write).
/// - Blend: Premultiplied alpha (SrcOne / InvSrcAlpha).
///
/// # Register Layout (Shared Root Signature)
/// - b0: TrailRootConstants (32 bytes root constants — uvScrollRate, SRV indices)
/// - b1: PerFrameCB (root CBV — viewProj, timeParams.x = totalTime)

#include "trail_common.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffers
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants at b0 — per-draw-call trail parameters.
/// Both VS and PS share this layout. The VS reads uvScrollRate and the bindless
/// SRV indices for the vertex and index buffers.
/// Layout MUST match TrailRootConstants in trail_common.hlsli exactly.
cbuffer TrailRootCB : register(b0) {
    float uvScrollRate;          ///< U-axis scroll speed (UV-units/second).
    float softDepthRange;        ///< Soft-fade radius in metres. 0 = disabled.
    float alphaClipThreshold;    ///< Alpha discard threshold [0, 1].
    uint  trailTexSrvIndex;      ///< Bindless SRV index for trail texture (0xFFFFFFFF = none).
    uint  depthBufferSrvIndex;   ///< Bindless SRV index for scene depth buffer.
    uint  vertexBufferSrvIndex;  ///< Bindless SRV index for trail StructuredBuffer<TrailVertexData>.
    uint  indexBufferSrvIndex;   ///< Bindless SRV index for trail StructuredBuffer<uint>.
    uint  _cbPad0;
};

/// Per-frame constant buffer at b1 — standard engine layout.
/// The VS reads viewProj (for world->clip transform) and timeParams.x
/// (for UV scroll animation). All fields must be declared for correct
/// memory layout even though only a subset is consumed.
cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;    ///< View * Projection matrix.
    float4   cameraPos;             ///< xyz = camera world position, w = unused.
    float4   timeParams;            ///< x = totalTime, y = deltaTime, z = frameCount, w = unused.
    float4   lightDir;              ///< xyz = direction toward light, w = unused.
    float4   lightColor;            ///< xyz = color * intensity, w = unused.
    float4   ambientColor;          ///< xyz = ambient radiance, w = unused.
    row_major float4x4 invViewProj; ///< Inverse view-projection (declared for layout).
    float    nearPlane;             ///< Camera near plane distance.
    float    farPlane;              ///< Camera far plane distance.
    uint     _perFramePad0;
    uint     _perFramePad1;
    uint     _perFramePad2;
    uint     _perFramePad3;
    float    _perFramePad4;
    float    _perFramePad5;
};

// ─────────────────────────────────────────────────────────────────────────────
// Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Transforms a ribbon vertex from world space to clip space and animates UVs.
///
/// Uses SV_VertexID for bindless vertex fetch: reads an index from the index
/// buffer SRV, then fetches vertex data from the vertex buffer SRV. This
/// avoids the Input Assembler entirely, matching the engine's bindless root
/// signature.
///
/// # Parameters
/// - vertexID: sequential vertex index from Draw(indexCount, 1, 0, 0).
///             Maps 1:1 to entries in the index buffer.
///
/// # Returns
/// Clip-space position, scrolled UVs, interpolated color, and linear depth
/// for soft-particle depth comparison in the pixel shader.
TrailVSOutput main(uint vertexID : SV_VertexID) {
    TrailVSOutput output;

    // ── Step 1: Bindless vertex fetch ────────────────────────────────────
    // Read the actual vertex index from the index buffer SRV, then use it
    // to fetch vertex data from the vertex buffer SRV. This replaces the
    // traditional IA + SetVertexBuffer + SetIndexBuffer + DrawIndexed path.
    StructuredBuffer<uint>            indexBuffer  = ResourceDescriptorHeap[indexBufferSrvIndex];
    StructuredBuffer<TrailVertexData> vertexBuffer = ResourceDescriptorHeap[vertexBufferSrvIndex];

    uint actualIndex = indexBuffer[vertexID];
    TrailVertexData vtx = vertexBuffer[actualIndex];

    // ── Step 2: World -> Clip ────────────────────────────────────────────
    // The ribbon CPU builder already places vertices in world space.
    // Multiply as a row vector (HLSL convention: vector * matrix).
    output.positionCS = mul(float4(vtx.position, 1.0f), viewProj);

    // ── Step 3: UV scroll along trail length ─────────────────────────────
    // Offset V by accumulated time to animate the texture downstream.
    // U (ribbon width axis) is passed through unmodified.
    // timeParams.x = totalTime from PerFrameCB.
    output.uv = float2(
        vtx.u,                                          // U: width, no scroll
        vtx.v + timeParams.x * uvScrollRate             // V: scroll along trail
    );

    // ── Step 4: Pass-through color ───────────────────────────────────────
    // CPU encodes lifetime fade and emitter tint in the vertex color.
    // The PS multiplies this against the texture sample, so no processing here.
    output.color = vtx.color;

    // ── Step 5: Linear depth for soft-particle comparison ───────────────
    // positionCS.w is the (positive) view-space Z for perspective-correct
    // interpolation. We store it as a scalar interpolant so the PS can compare
    // it against the linearized scene depth without needing the inverse matrices.
    // Note: clip-space W equals view-space Z in a standard projection matrix,
    // which is positive in front of the camera (right-handed view space).
    output.linearDepth = output.positionCS.w;

    return output;
}
