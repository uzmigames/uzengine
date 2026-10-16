// Additive explicit-layer path. Legacy/neutral single-layer scenes keep the
// original march and noise evaluation, including its sample locations.
#ifndef UZ_CLOUD_LAYERS
#define UZ_CLOUD_LAYERS

float LayerMip(uint layer, float3 direction, float stepLength, float distance) {
    float3 texels = 128.0f * float3(g_noiseXYScale, g_noiseXYScale,
        g_noiseZScale / max(g_layerGeometry[layer].y, EXT_EPS));
    float3 footprint = abs(direction * texels) * stepLength;
    return log2(max(1.0f, max(max(footprint.x, max(footprint.y, footprint.z)),
        distance * max(texels.x, texels.z) / float(g_halfHeight))));
}

float LayerDensity(float3 pos, uint layer, float mip) {
    float4 geometry = g_layerGeometry[layer];
    float h = (length(pos) - (PLANET_RADIUS_KM + geometry.x)) / geometry.y;
    if (h <= 0.0f || h >= 1.0f) return 0.0f;
    float gradient = HeightGradient(length(pos), PLANET_RADIUS_KM + geometry.x, geometry.y);
    float4 profile = g_layerProfile[layer];
    gradient *= max(0.0f, profile.x * exp(clamp(profile.y * h, -80.0f, 80.0f))
        + profile.z * h + profile.w);
    if (gradient <= 0.0f) return 0.0f;
    float3 wind = float3(g_windVelocityX, g_windVelocityY, g_windVelocityZ) * g_time;
    float3 noisePos = float3((pos.xy + wind.xy) * g_noiseXYScale,
                            h * g_noiseZScale + wind.z * g_windZScale);
    float4 controls = g_layerShape[layer];
    float coverage = g_coverage;
#ifdef UZ_BINDLESS
    if (g_weatherSrv != 0xFFFFFFFF) {
        Texture2D<float4> weather = ResourceDescriptorHeap[g_weatherSrv];
        float4 channels = weather.SampleLevel(linearSampler, (pos.xy + wind.xy) * g_weatherUVScale, 0);
        coverage *= pow(max(channels[min(uint(geometry.w), 3u)], 0.0f), controls.z);
    }
#endif
    float shape;
#ifdef UZ_BINDLESS
    if (g_proceduralNoise == 0 && g_shapeNoiseSrv != 0xFFFFFFFF) {
        Texture3D<float> texture = ResourceDescriptorHeap[g_shapeNoiseSrv];
        shape = texture.SampleLevel(linearSampler, noisePos, mip);
    } else
#endif
        shape = FBM(noisePos, g_shapeOctaves);
    shape = shape * controls.x + controls.w;
    float density = g_proceduralNoise != 0 || g_shapeNoiseSrv == 0xFFFFFFFF
        ? saturate(shape + coverage - 0.5f)
        : saturate((shape - (1.0f - coverage)) / max(coverage, EXT_EPS));
    density *= density;
#ifdef UZ_BINDLESS
    if (g_detailNoiseSrv != 0xFFFFFFFF && density > 0.0f && controls.y > 0.0f) {
        Texture3D<float> detail = ResourceDescriptorHeap[g_detailNoiseSrv];
        float3 detailPos = noisePos * (g_fbmLacunarity * g_detailFrequencyScale)
            + float3(wind.xy * g_detailWindScale, wind.z * g_detailWindZScale);
        float detailMip = max(0.0f, mip + log2(g_fbmLacunarity * g_detailMipScale));
        float erosion = detail.SampleLevel(linearSampler, detailPos * g_detailUVScale, detailMip);
        density = saturate(density - erosion * g_detailErosion * controls.y
            * (1.0f - saturate(density * 2.0f)));
    }
#endif
    return density * gradient * g_density * geometry.z;
}

float LayerLightDepth(float3 pos, float3 sunDir, uint layer) {
    float4 geometry = g_layerGeometry[layer];
    float2 outer = RaySphereIntersect(pos, sunDir, 0.0f,
        PLANET_RADIUS_KM + geometry.x + geometry.y);
    float distance = min(max(outer.y, 0.0f), geometry.y * g_lightMarchHeightScale);
    uint steps = clamp(g_lightSteps, 1u, 8u);
    float stepLength = distance / float(steps);
    float mip = LayerMip(layer, sunDir, stepLength, 0.0f);
    float depth = 0.0f;
    for (uint i = 0; i < steps; ++i)
        depth += LayerDensity(pos + sunDir * ((float(i) + 0.5f) * stepLength), layer, mip) * stepLength;
    return depth * g_extinction * DENSITY_SCALE;
}

float4 MarchCloudLayers(float3 origin, float3 direction, uint2 pixel, out float frontDepth) {
    float2 intervals[4];
    float first = 1e30f, last = 0.0f, support = 0.0f;
    uint count = min(g_layerCount, 4u);
    frontDepth = 0.0f;
    for (uint layer = 0; layer < count; ++layer) {
        float inner = PLANET_RADIUS_KM + g_layerGeometry[layer].x;
        float nearT, farT;
        bool hit = GetRayNearFar(origin, direction, inner,
            inner + g_layerGeometry[layer].y, nearT, farT);
        intervals[layer] = hit ? float2(nearT, farT) : float2(1e30f, -1.0f);
        if (hit) {
            first = min(first, nearT);
            last = max(last, farT);
            support += farT - nearT;
        }
    }
    last = min(last, first + g_tracingMaxDistanceKm);
#ifdef UZ_BINDLESS
    if (g_depthSrvIdx != 0xFFFFFFFF) {
        Texture2D<float> depth = ResourceDescriptorHeap[g_depthSrvIdx];
        uint width, height;
        depth.GetDimensions(width, height);
#ifdef UZ_CLOUD_TEMPORAL_UPSCALE
        uint2 depthPixel = min(pixel * 4 + CloudBayerOffset(g_frameIndex), uint2(width - 1, height - 1));
#else
        uint2 depthPixel = min(pixel * 2, uint2(width - 1, height - 1));
#endif
        float z = depth.Load(int3(depthPixel, 0));
        if (z > 0.0f) {
            float2 uv = (float2(depthPixel) + 0.5f) / float2(width, height);
            float4 world = mul(float4(uv.x * 2 - 1, 1 - uv.y * 2, z, 1), g_invViewProj);
            last = min(last, length(world.xyz / world.w * 1e-5f - g_cameraPos));
        }
    }
#endif
    if (last <= first) return float4(0, 0, 0, 1);
    float minStep = max(g_minStepSize, 0.001f);
    float maxStep = max(minStep, g_maxStepSize);
    float stepT = clamp(max(first * (g_perspectiveStepScale - 1.0f),
        min(support, last - first) / 256.0f), minStep, maxStep);
    float jitter = 0.0f;
#ifdef UZ_BINDLESS
    if (g_stbnSrvIndex != 0xFFFFFFFF) {
        Texture3D<float> stbn = ResourceDescriptorHeap[g_stbnSrvIndex];
        jitter = stbn.Load(int4(pixel % 128, g_frameIndex % 64, 0)) * 2.0f * g_jitterScale;
    }
#endif
    float t = first + stepT * jitter;
    float transmittance = 1.0f, weightedDepth = 0.0f;
    float3 luminance = 0.0f;
    float3 sunDir = normalize(g_sunDir);
    float cosTheta = dot(direction, sunDir);
    // One shared iteration budget, not 256 iterations per layer.
    for (uint iteration = 0; iteration < 256 && t < last; ++iteration) {
        if (transmittance < g_stopTracingTransmittanceThreshold) break;
        float next = last, intervalEnd = last;
        bool inside = false;
        for (uint layer = 0; layer < count; ++layer) {
            if (t >= intervals[layer].x && t < intervals[layer].y) {
                inside = true;
                intervalEnd = min(intervalEnd, intervals[layer].y);
            } else if (intervals[layer].x > t) next = min(next, intervals[layer].x);
        }
        if (!inside) {
            // Jump directly to the next occupied shell; never march the gap.
            t = next;
            stepT = clamp(max(t * (g_perspectiveStepScale - 1.0f), minStep), minStep, maxStep);
            continue;
        }
        float stepLength = min(stepT, min(intervalEnd, next) - t);
        float3 pos = origin + direction * t;
        float density = 0.0f, height = 0.0f, lightDepth = 0.0f, mip = 0.0f;
        for (uint layer = 0; layer < count; ++layer) {
            if (t < intervals[layer].x || t >= intervals[layer].y) continue;
            float layerMip = LayerMip(layer, direction, stepLength, t);
            mip = max(mip, layerMip);
            float d = LayerDensity(pos, layer, layerMip);
            if (d <= 0.0f) continue;
            density += d;
            height += d * saturate((length(pos) - PLANET_RADIUS_KM - g_layerGeometry[layer].x)
                / g_layerGeometry[layer].y);
#ifndef UZ_CLOUD_BSM
            lightDepth += d * LayerLightDepth(pos, sunDir, layer);
#endif
        }
        float sampleT = t;
        t += stepLength;
        stepT = clamp(stepT * g_perspectiveStepScale, minStep, maxStep);
        if (density <= 0.0f) {
            stepT = min(maxStep, stepT * (2.0f + mip));
            continue;
        }
#ifdef UZ_CLOUD_BSM
        lightDepth = CloudShadowOpticalDepth(pos - float3(0, 0, PLANET_RADIUS_KM), sunDir,
            g_cloudShadowBufferSrv, g_cloudShadowCascadeCount);
#else
        lightDepth /= density;
#endif
        float extinction = density * g_extinction * DENSITY_SCALE;
        float stepTransmittance = exp(-extinction * stepLength);
        float3 radiance = EvaluateMultiScattering(lightDepth, cosTheta);
        float3 skyIrradiance = g_ambientColor * (4.0f * PI);
        radiance += skyIrradiance * (1.0f / (4.0f * PI))
            * lerp(g_skyBottomVisibility, 1.0f, height / density) * (g_ambientMultiplier / g_skyBottomVisibility);
        radiance *= 1.0f - g_powderScale * exp(-extinction * g_powderExponent);
        float3 scatter = extinction * g_scatteringAlbedo * radiance;
        luminance += transmittance * (scatter - scatter * stepTransmittance) / max(extinction, EXT_EPS);
        weightedDepth += transmittance * (1.0f - stepTransmittance) * sampleT;
        transmittance *= stepTransmittance;
    }
    frontDepth = weightedDepth / max(1.0f - transmittance, EXT_EPS);
    float3 skyColor = dot(g_ambientColor, g_ambientColor) < 0.001f
        ? float3(0.4f, 0.55f, 0.8f) : g_ambientColor;
    luminance = lerp(luminance, skyColor * (1.0f - transmittance), 1.0f - exp(-first * g_aerialPerspectiveDensity));
    return float4(luminance, transmittance);
}
#endif
