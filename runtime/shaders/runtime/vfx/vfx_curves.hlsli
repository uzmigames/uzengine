/// GPU curve sampling utilities for VFX particle systems.
///
/// Curves are baked to 1D textures (256 texels) on the CPU and sampled
/// via linear-filtered texture fetch on the GPU. This avoids per-particle
/// keyframe search on the GPU.

#ifndef VFX_CURVES_HLSLI
#define VFX_CURVES_HLSLI

/// Samples a scalar curve at normalized time t in [0, 1].
///
/// The texture should be R32_FLOAT format, baked from a VFXCurve with
/// CurveType::Float via VFXCurve::BakeToTexture().
///
/// @param tex   1D texture containing the baked float curve.
/// @param samp  Linear-filtered clamp sampler.
/// @param t     Normalized lifetime [0, 1].
/// @return      Interpolated scalar value.
float SampleCurveFloat(Texture1D<float> tex, SamplerState samp, float t)
{
    return tex.SampleLevel(samp, saturate(t), 0);
}

/// Samples a float4 curve at normalized time t in [0, 1].
///
/// The texture should be RGBA32_FLOAT format, baked from a VFXCurve with
/// CurveType::Vec4 via VFXCurve::BakeToTexture().
///
/// @param tex   1D texture containing the baked vec4 curve.
/// @param samp  Linear-filtered clamp sampler.
/// @param t     Normalized lifetime [0, 1].
/// @return      Interpolated float4 value (e.g., RGBA color over lifetime).
float4 SampleCurveVec4(Texture1D<float4> tex, SamplerState samp, float t)
{
    return tex.SampleLevel(samp, saturate(t), 0);
}

/// Samples a float3 curve (stored as float4 with w=0).
///
/// Convenience wrapper that discards the w component. Use for Vec3 curves
/// (e.g., velocity, offset) stored in RGBA32_FLOAT textures.
///
/// @param tex   1D texture containing the baked vec4 curve (w=0 for vec3).
/// @param samp  Linear-filtered clamp sampler.
/// @param t     Normalized lifetime [0, 1].
/// @return      Interpolated float3 value (xyz components only).
float3 SampleCurveVec3(Texture1D<float4> tex, SamplerState samp, float t)
{
    return tex.SampleLevel(samp, saturate(t), 0).xyz;
}

#endif // VFX_CURVES_HLSLI
