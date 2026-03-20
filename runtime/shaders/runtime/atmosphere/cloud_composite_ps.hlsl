/// @ref Schneider & Vos 2015 "Real-Time Volumetric Cloudscapes of Horizon Zero Dawn" — cloud compositing
/// @ref Hillaire 2020 "A Scalable and Production Ready Sky and Atmosphere Rendering Technique" — premultiplied transmittance compositing
/// @constants NONE (no numeric constants — compositing is a direct blend operation)
/// @deviations NONE
///
/// Cloud composite pixel shader — upscales half-res cloud output and composites
/// it over the full-res HDR scene color via hardware alpha blending.
///
/// The cloud raymarch pass outputs RGBA16F at half resolution where:
///   rgb = premultiplied cloud color (luminance * transmittance already baked in)
///   a   = transmittance (1.0 = fully transparent / no cloud, 0.0 = fully opaque)
///
/// Compositing equation:
///   final = sceneColor * transmittance + cloudColor
///
/// This is achieved with hardware blend state:
///   SrcBlend  = One
///   DestBlend = SrcAlpha
///   BlendOp   = Add
///   Output    = float4(cloudColor.rgb, transmittance)
///
/// The blend unit computes:
///   final.rgb = output.rgb * 1 + scene.rgb * output.a
///             = cloudColor + sceneColor * transmittance
/// which is the correct over-compositing for premultiplied color + transmittance.
///
/// # Root Signature
/// - b0: 4x uint32 root constants (bindless descriptor indices)
/// - b1: PerFrameCB (camera data — kept for consistency, not actively used)
/// - s0: static linear clamp sampler
///
/// # Blend State
/// SrcBlend=One, DestBlend=SrcAlpha, BlendOp=Add
/// SrcBlendAlpha=Zero, DestBlendAlpha=SrcAlpha, BlendOpAlpha=Add
///   (preserves scene alpha: finalAlpha = 0 + sceneAlpha * transmittance)

// ─────────────────────────────────────────────────────────────────────────────
// Input Structure
// ─────────────────────────────────────────────────────────────────────────────

/// Interpolants from the fullscreen triangle vertex shader.
struct FSInput {
    float4 positionCS : SV_Position;
    float2 texcoord   : TEXCOORD0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants (bindless descriptor indices)
// ─────────────────────────────────────────────────────────────────────────────

#ifdef UZ_BINDLESS
/// Root constants at b0 — descriptor heap indices for input textures.
struct CloudCompositeIndices {
    uint cloudSrvIndex;   // SRV of half-res cloud output RGBA16F
    uint depthSrvIndex;   // SRV of fullscreen depth (reserved for depth-aware upscale)
    uint _pad0;
    uint _pad1;
};
ConstantBuffer<CloudCompositeIndices> compositeIndices : register(b0);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// PerFrameCB at b1 — kept for root signature consistency
// ─────────────────────────────────────────────────────────────────────────────

/// Per-frame constant buffer. Layout MUST match the C++ PerFrameCB struct and
/// all other shaders that reference b1. Not actively read by this shader, but
/// the slot must be declared so the root signature layout is consistent across
/// all fullscreen atmosphere passes.
cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;     // 64 bytes (offset 0)
    float4   cameraPos;              // 16 bytes (offset 64)
    float4   timeParams;             // 16 bytes (offset 80)
    float4   lightDir;               // 16 bytes (offset 96)
    float4   lightColor;             // 16 bytes (offset 112)
    float4   ambientColor;           // 16 bytes (offset 128)
    row_major float4x4 invViewProj;  // 64 bytes (offset 144)
    float    nearPlane;              // 4 bytes  (offset 208)
    float    farPlane;               // 4 bytes  (offset 212)
    float    _framePad[10];          // 40 bytes (offset 216)
};

// ─────────────────────────────────────────────────────────────────────────────
// Texture Declarations (SM5 fallback)
// ─────────────────────────────────────────────────────────────────────────────

#ifndef UZ_BINDLESS
/// Half-res cloud output: rgb = premultiplied color, a = transmittance.
Texture2D<float4> cloudTexture : register(t0);

/// Fullscreen depth buffer (reserved for future depth-aware bilateral upscale).
Texture2D<float>  sceneDepth   : register(t1);
#endif

/// Bilinear clamp sampler for upscaling the half-res cloud texture.
SamplerState linearSampler : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// Vertex Shader — Fullscreen Triangle (no vertex buffer)
// ─────────────────────────────────────────────────────────────────────────────

/// Generates a fullscreen triangle from SV_VertexID without any vertex buffer.
/// Three vertices (0, 1, 2) produce a triangle that covers the entire screen.
///
/// Vertex 0: pos=(-1, +1), uv=(0, 0)  — top-left
/// Vertex 1: pos=(+3, +1), uv=(2, 0)  — far right (clipped)
/// Vertex 2: pos=(-1, -3), uv=(0, 2)  — far bottom (clipped)
///
/// The GPU clips the oversized triangle to the viewport, covering all pixels
/// with a single draw call (3 vertices, 1 triangle, no index buffer).
// ─────────────────────────────────────────────────────────────────────────────
// Pixel Shader — Cloud Composite
// ─────────────────────────────────────────────────────────────────────────────
//
// NOTE: This shader uses the shared fullscreen_vs.hlsl vertex shader.
// The VS is NOT defined here — it is compiled separately and paired via
// PipelineLibrary::GetCloudComposite().

/// @ref Schneider & Vos 2015 "Real-Time Volumetric Cloudscapes of Horizon Zero Dawn" — cloud compositing
/// @deviations NONE
///
/// Samples the half-res cloud texture at the current pixel's UV coordinate
/// using bilinear filtering (hardware upscale), then outputs the premultiplied
/// cloud color with transmittance in the alpha channel.
///
/// The hardware blend state (SrcBlend=One, DestBlend=SrcAlpha, BlendOp=Add)
/// computes the correct over-compositing:
///
///   final.rgb = cloudColor.rgb * 1 + sceneColor.rgb * transmittance
///             = cloudColor + sceneColor * transmittance
///
/// This matches the volumetric rendering compositing equation for
/// premultiplied color (where cloudColor already encodes luminance * (1 - transmittance)).
///
/// @param input  Fullscreen triangle interpolants (position + UV).
/// @return float4(premultiplied_cloud_color, transmittance) for blend state compositing.
float4 main(FSInput input) : SV_Target
{
    float2 uv = input.texcoord;

    // ── Sample half-res cloud texture with bilinear filtering ──────────────
    // SampleLevel with mip 0 ensures we always read the base mip of the
    // half-res target. The GPU's bilinear filter performs the 2x upscale.
#ifdef UZ_BINDLESS
    Texture2D<float4> cloudTex = ResourceDescriptorHeap[compositeIndices.cloudSrvIndex];
    float4 cloud = cloudTex.SampleLevel(linearSampler, uv, 0.0f);
#else
    float4 cloud = cloudTexture.SampleLevel(linearSampler, uv, 0.0f);
#endif

    // cloud.rgb = premultiplied cloud color (luminance * transmittance baked in by raymarch)
    // cloud.a   = transmittance (1.0 = fully transparent, 0.0 = fully opaque)

    // ── Output for hardware compositing ────────────────────────────────────
    // With blend state SrcBlend=One, DestBlend=SrcAlpha:
    //   final.rgb = cloud.rgb * 1 + scene.rgb * cloud.a
    //             = cloudColor + sceneColor * transmittance
    return float4(cloud.rgb, cloud.a);
}
