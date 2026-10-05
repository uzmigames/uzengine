/// UzEngine local-light shadow depth — linear distance written to SV_Depth.
///
/// Spot and point-light shadow tiles store `distance(fragment, light) / radius` instead of the
/// projection's NDC depth. The perspective projections those lights use come from `Camera`
/// (reversed-Z, like the main view), while the shadow PSO, the atlas clear and the comparison
/// sampler all follow the CSM's standard-Z convention; before 2026-10-05 the local atlas was cleared
/// to 0.0 and drawn with `Less`, so no caster ever passed the test and every spot pixel read as
/// fully shadowed (block A.1). A linear value is independent of the projection convention, keeps
/// its precision across the whole range (NDC depth with a 0.1 cm near plane spends it all next to
/// the light) and lets the lighting shader bias in centimetres.
///
/// Paired with `shadow_depth_vs.hlsl` compiled with UZ_SHADOW_LINEAR=1, which forwards the world
/// position. The atlas is cleared to 1.0 (= radius, nothing closer), tested with `Less`, and sampled
/// with the LESS comparison sampler: a receiver is lit when its own distance/radius is below the
/// stored value.

cbuffer ShadowCB : register(b4) {
    row_major float4x4 shadowViewProj;
    float3 shadowLightPosition;   ///< Light position (world, cm).
    float  shadowLightRadius;     ///< Light range (cm); distances are stored as a fraction of it.
};

struct PSInput {
    float4 position : SV_Position;
    float3 worldPos : TEXCOORD0;
};

float main(PSInput input) : SV_Depth {
    const float dist = length(input.worldPos - shadowLightPosition);
    return saturate(dist / max(shadowLightRadius, 1e-3));
}
