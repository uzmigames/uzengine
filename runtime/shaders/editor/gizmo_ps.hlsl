/// Gizmo pixel shader — N-dot-L diffuse shading with alpha.
///
/// Uses a simple directional light from upper-right-forward to give 3D depth
/// perception to cylindrical stems, cones, and cubes. Ambient + diffuse model
/// ensures geometry is never fully black (ambient = 0.45, diffuse weight = 0.55).
/// Alpha channel dims non-active axes when one axis is being dragged.

struct PSInput {
    float4 position : SV_Position;
    float3 normal   : NORMAL;
    float3 color    : COLOR;
    float  alpha    : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target {
    // Simple directional light from upper-right-forward for depth cues.
    float3 lightDir = normalize(float3(0.4, 0.3, 0.7));
    float NdotL = saturate(dot(normalize(input.normal), lightDir));

    // Ambient + diffuse: ensures geometry is never fully black.
    float3 lit = input.color * (0.45 + 0.55 * NdotL);

    return float4(lit, input.alpha);
}
