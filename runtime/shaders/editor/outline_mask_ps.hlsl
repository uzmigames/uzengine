/// Outline mask pixel shader — writes 1.0 for selected entity pixels.
///
/// Used to create a binary mask that the outline detection pass reads.
/// The vertex shader (mesh_vs.hlsl) transforms the mesh normally; this PS
/// simply outputs a solid value to the R8Unorm mask render target.
/// Depth test against the GBuffer depth ensures only visible fragments are marked.

struct PSInput {
    float4 position : SV_Position;
};

float4 main(PSInput input) : SV_Target {
    return float4(1.0, 0.0, 0.0, 1.0);
}
