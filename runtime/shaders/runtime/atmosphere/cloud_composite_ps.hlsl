/// Cloud composite pixel shader — blends half-res cloud output over HDR scene.
///
/// @ref Schneider & Vos 2015 "Real-Time Volumetric Cloudscapes of Horizon Zero Dawn"
///
/// # Blend Mode (set on pipeline)
/// SrcBlend = One, DstBlend = SrcAlpha, BlendOp = Add
/// final.rgb = cloud.rgb * 1 + scene.rgb * cloud.a
/// where cloud.a = transmittance (1 = transparent, 0 = opaque)
///
/// # Root Constants (b0)
/// - [0]: cloud output SRV index
/// - [1]: depth SRV index (for geometry masking)
///
/// # Depth Check
/// Geometry pixels (reversed-Z depth > 0.0001) are masked to prevent clouds
/// from appearing in front of meshes. This check is done here (not in the
/// compute shader) because the compute runs on a separate command list where
/// the depth texture may not be in a readable state.

#include "../common/constants.hlsli"
#include "../common/bindless.hlsli"

struct FSInput {
    float4 positionCS : SV_Position;
    float2 texcoord   : TEXCOORD0;
};

#ifdef UZ_BINDLESS
cbuffer CompositeIndices : register(b0) {
    uint cloudSrvIdx;
    uint depthSrvIdx;
    uint _pad0;
    uint _pad1;
};
#else
Texture2D<float4> cloudTexture : register(t0);
Texture2D<float>  sceneDepth   : register(t1);
#endif

SamplerState linearSampler : register(s0);

float4 main(FSInput input) : SV_Target {
    float2 uv = input.texcoord;

    // ── Depth check — skip geometry pixels ──
    // Reversed-Z: geometry depth > 0.0001, sky depth near 0.0.
    // For geometry pixels, output (0,0,0,1) so the blend becomes:
    //   final = 0 + scene * 1 = scene (no change)
#ifdef UZ_BINDLESS
    Texture2D<float> depthTex = ResourceDescriptorHeap[depthSrvIdx];
    float deviceZ = depthTex.SampleLevel(linearSampler, uv, 0).r;
#else
    float deviceZ = sceneDepth.SampleLevel(linearSampler, uv, 0).r;
#endif

    if (deviceZ > 0.0001f) {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    // ── Sample cloud output ──
#ifdef UZ_BINDLESS
    Texture2D<float4> cloudTex = ResourceDescriptorHeap[cloudSrvIdx];
    float4 cloud = cloudTex.SampleLevel(linearSampler, uv, 0);
#else
    float4 cloud = cloudTexture.SampleLevel(linearSampler, uv, 0);
#endif

    // cloud.rgb = premultiplied cloud color
    // cloud.a   = transmittance (1 = transparent, 0 = opaque)
    return cloud;
}
