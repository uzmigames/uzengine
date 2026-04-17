# UzEngine · Shader Sketches for the New Default Pipeline

Concrete HLSL sketches for the passes in [aaa-default-pipeline.md](./aaa-default-pipeline.md).
Not final code — they are **architectural templates** showing data
flow and structure. Production code goes through the cpp-core-expert
and shader-engineer agents.

---

## 1 · Indirect Integration pass

New pass that unifies SSGI + DDGI + IBL into one `IndirectDiffuse` buffer.

```hlsl
// runtime/shaders/runtime/lighting/indirect_integration_cs.hlsl

Texture2D<float4>  GBufferNormal    : register(t0);
Texture2D<float>   GBufferDepth     : register(t1);
Texture2D<float4>  GBufferAlbedo    : register(t2);
Texture2D<float2>  GBufferMaterial  : register(t3);  // R: roughness, G: metallic

Texture2D<float4>  SSGIIrradiance   : register(t4);  // demodulated
Texture2D<float>   SSGIValidMask    : register(t5);

Texture2DArray<float4> DDGIIrradiance : register(t6);
Texture2DArray<float2> DDGIVisibility : register(t7);

TextureCube<float3> IBLPrefiltered  : register(t8);
TextureCube<float3> IBLIrradiance   : register(t9);

Texture2D<float>   GTAOMask         : register(t10);

RWTexture2D<float4> IndirectDiffuse : register(u0);  // demodulated
RWTexture2D<float4> IndirectSpec    : register(u1);

struct Constants
{
    float4x4 InvViewProj;
    float3   CamPos;
    float    IndirectLightingIntensity;
    float3   DDGIOrigin;
    float    DDGIProbeSpacing;
    int3     DDGIProbeCount;
    float    SkyIntensity;
};
ConstantBuffer<Constants> C : register(b0);

[numthreads(8, 8, 1)]
void main(uint2 tid : SV_DispatchThreadID)
{
    float2 uv = (tid + 0.5) / ScreenSize;

    // ─ Decode G-Buffer ──────────────────────────────────────────────
    float  depth   = GBufferDepth[tid];
    if (depth >= 1.0) { IndirectDiffuse[tid] = 0; IndirectSpec[tid] = 0; return; }

    float3 N       = OctDecode(GBufferNormal[tid].rg);
    float3 albedo  = GBufferAlbedo[tid].rgb;
    float  rough   = GBufferMaterial[tid].r;
    float  metal   = GBufferMaterial[tid].g;
    float3 pos     = ReconstructWorldPos(uv, depth, C.InvViewProj);
    float3 V       = normalize(C.CamPos - pos);
    float  ao      = GTAOMask[tid];
    float  NdotV   = saturate(dot(N, V));

    // ─ Sample sources ───────────────────────────────────────────────
    float3 ssgi       = SSGIIrradiance[tid].rgb;    // demodulated
    float  ssgiValid  = SSGIValidMask[tid];         // 1 = in-screen valid

    float3 ddgi       = SampleDDGI(pos, N, DDGIIrradiance, DDGIVisibility,
                                   C.DDGIOrigin, C.DDGIProbeSpacing, C.DDGIProbeCount);
    float  ddgiValid  = length(ddgi) > 0.001 ? 1 : 0;

    float3 ibl        = IBLIrradiance.SampleLevel(LinearSampler, WorldToCubemap(N), 0) * C.SkyIntensity;

    // ─ Cascade fallback: prefer SSGI > DDGI > IBL ────────────────────
    float3 diffuse;
    if (ssgiValid > 0.5) {
        // SSGI is trustworthy here
        diffuse = ssgi;
        // Smooth feather to DDGI near screen edges
        float screenEdgeFeather = SmoothEdge(uv, 0.03);
        diffuse = lerp(ddgi, ssgi, screenEdgeFeather);
    } else if (ddgiValid > 0.5) {
        diffuse = ddgi;
    } else {
        diffuse = ibl;
    }

    // ─ Apply AO uniformly to all indirect ───────────────────────────
    diffuse *= pow(ao, 2.2);
    diffuse *= C.IndirectLightingIntensity;

    // ─ Specular (reflections) ───────────────────────────────────────
    // Computed similarly — SSR + radiance cache + prefiltered IBL
    float3 R = reflect(-V, N);
    float3 ssr       = SSRColor[tid].rgb;
    float  ssrValid  = SSRValidMask[tid];
    float3 iblSpec   = IBLPrefiltered.SampleLevel(LinearSampler, WorldToCubemap(R), rough * 6.0);

    float3 spec;
    if (ssrValid > 0.5) {
        spec = ssr;
    } else {
        spec = iblSpec;  // TODO: add radiance cache lookup when available
    }

    // Roughness gate — high-roughness pixels ignore SSR entirely
    if (rough > 0.6) spec = iblSpec;

    // ─ Output (both still demodulated — re-modulated in deferred lighting)
    IndirectDiffuse[tid] = float4(diffuse, 1);
    IndirectSpec[tid]    = float4(spec, 1);
}
```

---

## 2 · DDGI bilinear+Chebyshev sample

```hlsl
// runtime/shaders/runtime/gi/ddgi_sample.hlsli

float3 SampleDDGI(
    float3 pos,         // world-space shading position
    float3 N,           // world-space surface normal
    Texture2DArray<float4> irradiance,
    Texture2DArray<float2> visibility,
    float3 origin,
    float  probeSpacing,
    int3   probeCount)
{
    // Compute local probe-grid coordinates
    float3 localPos  = (pos - origin) / probeSpacing;
    int3   baseIdx   = floor(localPos);
    float3 frac      = localPos - baseIdx;

    // Sample 8 corners with Chebyshev visibility weighting
    float3 sumIrradiance = 0;
    float  sumWeight     = 0;

    [unroll] for (int i = 0; i < 8; ++i) {
        int3 cornerIdx = baseIdx + int3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        cornerIdx = clamp(cornerIdx, 0, probeCount - 1);

        // Trilinear position weight
        float3 cornerFrac = float3(
            (i & 1) ? frac.x : (1 - frac.x),
            ((i >> 1) & 1) ? frac.y : (1 - frac.y),
            ((i >> 2) & 1) ? frac.z : (1 - frac.z));
        float trilinear = cornerFrac.x * cornerFrac.y * cornerFrac.z;
        if (trilinear < 0.001) continue;

        // Probe world position
        float3 probePos = origin + cornerIdx * probeSpacing;
        float3 toPixel  = pos - probePos;
        float  distToPixel = length(toPixel);
        float3 dirToPixel  = toPixel / (distToPixel + 1e-5);

        // Normal weight (probe from behind? skip)
        float backfaceWeight = max(0, dot(N, -dirToPixel)) * 0.5 + 0.5;

        // Chebyshev visibility test
        float2 probeVis = SampleOctahedral(visibility, cornerIdx, dirToPixel);
        float  probeDistance = probeVis.x;
        float  probeVariance = max(probeVis.y - probeVis.x * probeVis.x, 0);

        float chebyshev = 1;
        if (distToPixel > probeDistance) {
            // Chebyshev inequality upper bound
            chebyshev = probeVariance / (probeVariance + (distToPixel - probeDistance) * (distToPixel - probeDistance));
            chebyshev = max(0, chebyshev * chebyshev * chebyshev);  // cubic falloff
        }

        float weight = trilinear * backfaceWeight * chebyshev;

        // Irradiance sample in N direction
        float3 irr = SampleOctahedral(irradiance, cornerIdx, N);

        sumIrradiance += weight * irr;
        sumWeight     += weight;
    }

    return (sumWeight > 0) ? (sumIrradiance / sumWeight) : 0;
}
```

---

## 3 · ReSTIR DI initial candidate pass

```hlsl
// runtime/shaders/runtime/lighting/restir_di_initial_cs.hlsl

struct Reservoir
{
    uint   lightIdx;
    float  targetPdf;  // W for the final estimator
    float  weightSum;
    float  M;          // effective sample count (clamped to 20)
};

Texture2D<float4>   GBufferNormal   : register(t0);
Texture2D<float>    GBufferDepth    : register(t1);
Texture2D<float2>   GBufferMaterial : register(t2);
StructuredBuffer<Light> Lights      : register(t3);

RWStructuredBuffer<Reservoir> Reservoirs : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 tid : SV_DispatchThreadID)
{
    float2 uv = (tid + 0.5) / ScreenSize;

    float depth = GBufferDepth[tid];
    if (depth >= 1.0) return;

    float3 N = OctDecode(GBufferNormal[tid].rg);
    float3 pos = ReconstructWorldPos(uv, depth, ...);

    Reservoir r = (Reservoir)0;
    r.lightIdx = 0xFFFFFFFFu;

    // 32 candidates from global light pool, uniformly sampled
    uint seed = WangHash(tid.x * 1000u + tid.y * 5u + FrameIdx);
    for (uint i = 0; i < 32; ++i) {
        uint  lightIdx = (Rand(seed) * LightCount);
        Light L = Lights[lightIdx];

        // Target PDF: unshadowed contribution
        float3 toLight = L.pos - pos;
        float  dist2   = dot(toLight, toLight);
        float3 Ld      = normalize(toLight);
        float  cosTheta = saturate(dot(N, Ld));
        float3 contrib = L.color * L.intensity * cosTheta / (dist2 + 1);
        float  targetPdf = Luminance(contrib);

        // Streaming WRS update
        r.weightSum += targetPdf * LightCount;  // uniform pdf is 1/LightCount
        r.M += 1;
        if (Rand(seed) < (targetPdf * LightCount) / r.weightSum) {
            r.lightIdx  = lightIdx;
            r.targetPdf = targetPdf;
        }
    }

    Reservoirs[tid.y * ScreenSize.x + tid.x] = r;
}
```

---

## 4 · LTC rectangle area light (integrated into deferred)

```hlsl
// runtime/shaders/runtime/lighting/ltc_rect.hlsli

Texture2D<float4> LTC_Matrix    : register(t20);   // 32x32 RGBA16F
Texture2D<float2> LTC_Magnitude : register(t21);   // 32x32 RG16F

float IntegrateEdge(float3 v1, float3 v2)
{
    float cosTheta = dot(v1, v2);
    float theta = acos(clamp(cosTheta, -0.9999, 0.9999));
    float3 crossTerm = cross(v1, v2);
    return crossTerm.z * theta / max(sin(theta), 1e-5);
}

float3 LTC_EvaluateRectangle(
    float3 N, float3 V, float3 pos,
    float  roughness,
    float3 lightP0, float3 lightP1, float3 lightP2, float3 lightP3,
    float3 lightColor, float3 specColor)
{
    float3x3 Minv = LTC_LookupMatrix(roughness, saturate(dot(N, V)));
    float2 magFresnel = LTC_LookupMag(roughness, saturate(dot(N, V)));

    // Build basis aligned to N and V
    float3 T = normalize(V - N * dot(V, N));
    float3 B = cross(N, T);
    float3x3 basis = float3x3(T, B, N);

    // Transform rect vertices into LTC space
    float3 L0 = mul(Minv, mul(basis, lightP0 - pos));
    float3 L1 = mul(Minv, mul(basis, lightP1 - pos));
    float3 L2 = mul(Minv, mul(basis, lightP2 - pos));
    float3 L3 = mul(Minv, mul(basis, lightP3 - pos));

    // Normalize to unit sphere
    L0 = normalize(L0);
    L1 = normalize(L1);
    L2 = normalize(L2);
    L3 = normalize(L3);

    // Lambert integration
    float integral = IntegrateEdge(L0, L1)
                   + IntegrateEdge(L1, L2)
                   + IntegrateEdge(L2, L3)
                   + IntegrateEdge(L3, L0);
    integral = max(0, integral);

    // Specular: magnitude × Fresnel-modulated spec color
    float3 specular = integral * magFresnel.x * specColor
                    + integral * magFresnel.y * float3(1, 1, 1);

    // Diffuse: identity matrix — integrate Lambert over the polygon
    integral = /* same formula with Minv = identity */;
    float3 diffuse = integral * (1 - metallic);

    return lightColor * (specular + diffuse * albedo);
}
```

---

## 5 · Separable SSS (horizontal blur)

```hlsl
// runtime/shaders/runtime/lighting/sss_horizontal_cs.hlsl

Texture2D<float4>   SSSInput   : register(t0);  // diffuse radiance for SSS pixels
Texture2D<float>    Depth      : register(t1);
Texture2D<float4>   Normal     : register(t2);
Texture2D<uint>     MaterialID : register(t3);

RWTexture2D<float4> SSSOutput : register(u0);

// Jimenez 2015 human skin kernel (11 taps, RGB-colored)
static const float4 KERNEL[11] = {
    float4(0.560f, 0.560f, 0.560f, 0.000f),  // center
    float4(0.203f, 0.156f, 0.074f, 1.000f),  // and neighbors
    float4(0.203f, 0.156f, 0.074f,-1.000f),
    float4(0.065f, 0.054f, 0.033f, 3.000f),
    float4(0.065f, 0.054f, 0.033f,-3.000f),
    float4(0.024f, 0.021f, 0.013f, 5.000f),
    float4(0.024f, 0.021f, 0.013f,-5.000f),
    float4(0.011f, 0.011f, 0.009f, 7.000f),
    float4(0.011f, 0.011f, 0.009f,-7.000f),
    float4(0.006f, 0.008f, 0.011f, 9.000f),
    float4(0.006f, 0.008f, 0.011f,-9.000f)
};

[numthreads(8, 8, 1)]
void main(uint2 tid : SV_DispatchThreadID)
{
    if (MaterialID[tid] != MATERIAL_SKIN) {
        SSSOutput[tid] = SSSInput[tid];  // pass-through for non-SSS
        return;
    }

    float  centerDepth  = Depth[tid];
    float3 centerNormal = OctDecode(Normal[tid].rg);

    float4 color = SSSInput[tid];
    float3 sum   = color.rgb * KERNEL[0].rgb;
    float3 sumW  = KERNEL[0].rgb;

    for (int i = 1; i < 11; ++i) {
        int2 offset = int2(KERNEL[i].w, 0);
        int2 sampleTid = tid + offset;

        // Bilateral stop: reject cross-edge samples
        float  d = Depth[sampleTid];
        float3 n = OctDecode(Normal[sampleTid].rg);
        float  depthWeight  = exp(-50.0 * abs(d - centerDepth) / centerDepth);
        float  normalWeight = pow(saturate(dot(n, centerNormal)), 8.0);

        float3 w = KERNEL[i].rgb * depthWeight * normalWeight;
        float4 s = SSSInput[sampleTid];

        sum  += s.rgb * w;
        sumW += w;
    }

    SSSOutput[tid] = float4(sum / max(sumW, 1e-4), color.a);
}
```

Vertical pass identical but with `offset = int2(0, KERNEL[i].w)`.

---

## 6 · Contact shadows improvements

Our current contact_shadows.hlsli likely has basic sampling. Improvements:

```hlsl
float ContactShadow(float3 rayOrigin, float3 rayDir, Texture2D<float> depth)
{
    float  rayLen = 0.5;     // world-space max length
    int    steps  = 16;
    float  stepLen = rayLen / steps;

    float3 rayStep = rayDir * stepLen;
    float3 curPos  = rayOrigin + rayStep;

    // Blue-noise temporal jitter (decorrelate across frames)
    curPos += rayStep * BlueNoise(tid, FrameIdx) * 0.5;

    float  visibility = 1.0;

    for (int i = 0; i < steps; ++i) {
        float4 screenPos  = mul(ViewProj, float4(curPos, 1));
        float2 screenUV   = (screenPos.xy / screenPos.w) * 0.5 + 0.5;
        float  screenDepth = screenPos.z / screenPos.w;  // reversed-Z

        if (any(screenUV < 0 || screenUV > 1)) break;

        float sampledDepth = depth.SampleLevel(LinearSampler, screenUV, 0);

        // Reversed-Z: ray is occluded if sampledDepth > screenDepth + bias
        const float BIAS = 5e-4;  // reversed-Z bias
        if (sampledDepth > screenDepth + BIAS &&
            sampledDepth < screenDepth + BIAS * 10)  // don't shadow through thin walls
        {
            // Fade based on how close the sample is to the ray (thickness)
            float thickness = sampledDepth - screenDepth;
            visibility *= saturate(1 - thickness * 2000);
            if (visibility < 0.1) break;
        }

        curPos += rayStep;
    }

    return visibility;
}
```

Key additions vs our current version:
- Blue-noise temporal jitter.
- Thickness-fade to avoid shadowing through distant geometry.
- Early-exit when visibility drops below threshold.

---

## 7 · References

- Indirect integration pattern: Lumen's final-gather (see [../lumen/03-screen-probe-gather.md](../lumen/03-screen-probe-gather.md)).
- DDGI sampling: Majercik et al. 2019.
- ReSTIR candidate pass: Bitterli et al. 2020. See [../modern-techniques/restir.md](../modern-techniques/restir.md).
- LTC: Heitz et al. 2016. See [../modern-techniques/area-lights-ltc.md](../modern-techniques/area-lights-ltc.md).
- Separable SSS: Jimenez 2015. See [../modern-techniques/subsurface-scattering.md](../modern-techniques/subsurface-scattering.md).
