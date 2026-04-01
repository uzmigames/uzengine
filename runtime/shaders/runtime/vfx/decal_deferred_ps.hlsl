/// UzEngine Deferred Decal PS — placeholder for the VfxDecal path.
/// The primary DBuffer decal path uses gbuffer/decal_ps.hlsl.
///
/// @ref Valient 2014 "Deferred Rendering in Killzone: Shadow Fall"

float4 main(float4 positionCS : SV_Position) : SV_Target {
    // The VfxDecal path (frame_decals.cpp + DecalManager) is secondary.
    // For now, output a visible debug color so we can tell the path is active.
    return float4(1.0, 0.0, 1.0, 0.5); // Magenta debug output
}
