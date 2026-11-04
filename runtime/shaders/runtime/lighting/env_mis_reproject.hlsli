/// Environment MIS history reprojection (r.EnvMIS.Reproject), shared by the accumulation permutation of
/// env_mis_temporal_cs.hlsl (UZ_ENVMIS_REPROJECT=1) and by env_mis_resolve_cs.hlsl (r.EnvMIS.Denoise).
///
/// The including shader declares, in its root constants, `g_width`, `g_height` and the previous frame's camera
/// position `g_prevCameraPosX/Y/Z` (cm), which the view-distance test below reads.
///
/// The screen-locked accumulation (the default permutation of env_mis_temporal_cs.hlsl) blends every pixel with its
/// own previous value, so under camera motion a covered pixel inherits whatever was on that screen pixel before: the
/// background's decayed estimate on the leading rim of a moving metal (a dark crescent that restarts every frame),
/// or another metal's estimate (the chrome sphere's mirror image ghosting inside the dark one), and the per-frame
/// restarts leave the 8-spp noise unfiltered along the silhouette. Here the history is fetched where the surface
/// point was last frame (the velocity buffer, TAA's reprojection), the four bilinear taps are kept only when they
/// were accumulated on this surface (the geometry history: the shading normal, the view distance the previous
/// camera had to that point, and a sample count of 0 for pixels that had no estimate), and the blend weight follows
/// the count (1/2, 1/3, ... down to g_temporalAlpha) so a restarted pixel converges in a few frames instead of
/// twenty. The accumulated estimate converges to the same value as before: a static camera fetches its own texel
/// exactly (velocity 0) at the same alpha floor.
/// @ref Wright et al. 2022 "Lumen", final gather depth and normal history rejection (ssgi_temporal_accum_cs.hlsl)
/// Geometry history (RGBA16F): xy the octahedral shading normal, z the view distance in metres (fp16 keeps
/// ~0.1 % at room scale), w the accumulated sample count (0 = no estimate: sky, a dielectric, a rough surface).
#ifndef UZ_ENV_MIS_REPROJECT_HLSLI
#define UZ_ENV_MIS_REPROJECT_HLSLI

static const float kEnvMisMaxCount       = 64.0;   ///< the count saturates here; the alpha floor is reached long before
static const float kEnvMisNormalCos      = 0.9;    ///< the SSGI gather's receiver test (ssgi_surface.hlsli SurfaceCompatible)
static const float kEnvMisDistanceTol    = 0.05;   ///< relative tolerance between the stored and the expected view distance
static const float kEnvMisDistanceFloorM = 0.02;   ///< 2 cm floor: fp16 rounding and the pixel footprint at grazing angles

/// The bilinear mix of the history taps that still describe this surface point; false when none does (off-screen,
/// a disocclusion, a surface that carried no estimate): the caller restarts the accumulation from the current
/// estimate.
bool ReprojectEnvMisHistory(int2 px, float3 worldPos, float3 N, Texture2D<float4> historyTex,
                            Texture2D<float4> geometryTex, Texture2D<float2> velocityTex,
                            out float3 historyColor, out float historyCount) {
    historyColor = float3(0, 0, 0);
    historyCount = 0.0;
    float2 prevPixel = float2(px) - velocityTex.Load(int3(px, 0));
    if (any(prevPixel < -0.5) || any(prevPixel > float2(g_width, g_height) - 0.5)) return false;
    int2 base = int2(floor(prevPixel));
    float2 f = prevPixel - float2(base);
    float3 prevCameraPos = float3(g_prevCameraPosX, g_prevCameraPosY, g_prevCameraPosZ);
    float expectedM = length(prevCameraPos - worldPos) * 0.01;
    float3 colorSum = float3(0, 0, 0);
    float countSum = 0.0, weightSum = 0.0;
    [unroll] for (int y = 0; y < 2; ++y) {
        [unroll] for (int x = 0; x < 2; ++x) {
            int2 hp = base + int2(x, y);
            if (any(hp < 0) || any(hp >= int2(g_width, g_height))) continue;
            float4 g = geometryTex.Load(int3(hp, 0));
            if (g.w <= 0.0) continue;
            if (dot(N, OctDecode(g.xy)) < kEnvMisNormalCos) continue;
            if (abs(g.z - expectedM) > kEnvMisDistanceTol * expectedM + kEnvMisDistanceFloorM) continue;
            float w = (x == 0 ? 1.0 - f.x : f.x) * (y == 0 ? 1.0 - f.y : f.y);
            colorSum += historyTex.Load(int3(hp, 0)).rgb * w;
            countSum += g.w * w;
            weightSum += w;
        }
    }
    if (weightSum <= 1e-4) return false;
    historyColor = colorSum / weightSum;
    historyCount = countSum / weightSum;
    return true;
}

#endif // UZ_ENV_MIS_REPROJECT_HLSLI
