<!-- doc-meta
title: UzEngine CVar Implementation Guide — Making Every Shader Parameter Runtime-Configurable
author: UzEngine Architecture Analysis
date: 2026-04-04
status: guide
-->

# UzEngine CVar Implementation Guide

This guide explains how to systematically implement the CVars from the UE5 reference to make UzEngine rendering fully configurable at runtime (no recompilation for tweaking).

## Architecture Pattern: CVar Flow

```
CVar registered in C++ (default value)
    ↓
Read on render thread at initialization
    ↓
Pass to constant buffer struct
    ↓
GPU shader reads from constant buffer
    ↓
At runtime: change CVar → re-bind CB → shader sees new value
```

No recompilation needed. This is the pattern used by every AAA engine.

---

## Step 1: Define CVar in C++

Use UzEngine's console variable system (Zig binding to Windows' IConsoleManager or custom implementation):

```cpp
// In renderer/core/cvar_registry.h or similar
namespace uz::render {

// Tone Mapping
static ConsoleVariable<float> CVarTonemapperSharpen(
    "r.Tonemapper.Sharpen",
    -1.0f,  // default value
    "Sharpening in tonemapper. <0: inherit from post-process volume, 0: off, >0: sharpness amount"
);

static ConsoleVariable<float> CVarTonemapperGamma(
    "r.TonemapperGamma",
    0.0f,  // 0 = use sRGB/Rec709
    "Output gamma. 0: default (sRGB), #: use fixed gamma value"
);

// Bloom
static ConsoleVariable<float> CVarBloomScreenPercentage(
    "r.Bloom.ScreenPercentage",
    100.0f,
    "FFT bloom kernel resolution (% of screen)"
);

static ConsoleVariable<int32_t> CVarBloomCacheKernel(
    "r.Bloom.CacheKernel",
    1,  // bool
    "Cache FFT kernel in spectral domain (0: no, 1: yes)"
);

// GTAO
static ConsoleVariable<float> CVarGTAOFalloffEnd(
    "r.GTAO.FalloffEnd",
    200.0f,  // cm
    "Distance where ambient occlusion completes falloff"
);

// ... (repeat for all CVars from reference)
}
```

### ConsoleVariable Template Implementation

```cpp
template<typename T>
class ConsoleVariable {
public:
    ConsoleVariable(const char* name, T default_value, const char* help_text) 
        : name_(name), value_(default_value), default_value_(default_value) {
        RegisterWithSystem();
    }
    
    T Get() const { return value_; }
    void Set(T new_value) { value_ = new_value; }
    
private:
    const char* name_;
    T value_;
    T default_value_;
    
    void RegisterWithSystem();
};
```

---

## Step 2: Wire CVar into Constant Buffer Structure

Create a struct that mirrors shader-side parameter layout:

```cpp
// In renderer/core/constant_buffers.h
struct TonemapperParams {
    float sharpen;        // r.Tonemapper.Sharpen
    float gamma;          // r.TonemapperGamma
    float _pad0;
    float _pad1;
};

struct BloomParams {
    float screenPercentage;  // r.Bloom.ScreenPercentage
    uint32_t cacheKernel;    // r.Bloom.CacheKernel (0 or 1)
    uint32_t asyncCompute;   // r.Bloom.AsyncCompute
    uint32_t _pad0;
};

struct GTAOParams {
    float falloffEnd;        // r.GTAO.FalloffEnd
    float falloffStartRatio; // r.GTAO.FalloffStartRatio
    float thicknessBlend;    // r.GTAO.ThicknessBlend
    float numAngles;         // r.GTAO.NumAngles
};

struct ShadowParams {
    // Directional (CSM)
    float csmDepthBias;
    float csmSlopeScaleDepthBias;
    float csmReceiverBias;
    float transitionScale;
    
    // Point light
    float pointLightDepthBias;
    float pointLightSlopeDepthBias;
    
    // Spot light
    float spotLightDepthBias;
    float spotLightSlopeDepthBias;
    
    float shadowMaxSlopeScaleDepthBias;
    float _pad0[3];
};

struct TemporalAAParams {
    float filterSize;           // r.TemporalAAFilterSize
    float currentFrameWeight;   // r.TemporalAACurrentFrameWeight
    uint32_t catmullRom;        // r.TemporalAACatmullRom (0 or 1)
    uint32_t quality;           // r.TemporalAA.Quality [0, 3]
};

// ... repeat for all parameter groups
```

---

## Step 3: Populate CB from CVars

In the render thread initialization or per-frame constant buffer update:

```cpp
void UpdateTonemapperCB(const RenderFrame& frame, TonemapperParams& cb) {
    cb.sharpen = CVarTonemapperSharpen.Get();
    if (cb.sharpen < 0.0f) {
        // Inherit from post-process volume (if exists)
        cb.sharpen = frame.postProcessVolume 
            ? frame.postProcessVolume->tonemapperSharpen 
            : -1.0f;
    }
    cb.sharpen = Math::Clamp(cb.sharpen, 0.0f, 10.0f);  // Enforce shader range
    
    cb.gamma = CVarTonemapperGamma.Get();
    if (cb.gamma == 0.0f) {
        cb.gamma = 2.2f;  // Default sRGB gamma
    }
}

void UpdateBloomCB(BloomParams& cb) {
    cb.screenPercentage = CVarBloomScreenPercentage.Get();
    cb.cacheKernel = CVarBloomCacheKernel.Get() ? 1 : 0;
    cb.asyncCompute = CVarBloomAsyncCompute.Get() ? 1 : 0;
}

void UpdateGTAOCB(GTAOParams& cb) {
    cb.falloffEnd = CVarGTAOFalloffEnd.Get();
    cb.falloffStartRatio = Math::Clamp(CVarGTAOFalloffStartRatio.Get(), 0.0f, 0.999f);
    cb.thicknessBlend = CVarGTAOThicknessBlend.Get();
    cb.numAngles = Math::Clamp(CVarGTAONumAngles.Get(), 1.0f, 16.0f);
}

void UpdateShadowCB(ShadowParams& cb, const DirectionalLight& light) {
    cb.csmDepthBias = CVarCSMDepthBias.Get();
    cb.csmSlopeScaleDepthBias = CVarCSMSlopeScaleDepthBias.Get();
    cb.csmReceiverBias = Math::Clamp(CVarCSMReceiverBias.Get(), 0.0f, 1.0f);
    cb.transitionScale = CVarShadowTransitionScale.Get();
    
    // Light-specific overrides
    if (light.userShadowBias > 0.0f) {
        cb.csmDepthBias *= light.userShadowBias;
    }
}

void UpdateTemporalAACB(TemporalAAParams& cb, const ViewState& viewState) {
    cb.filterSize = CVarTemporalAAFilterSize.Get();
    cb.currentFrameWeight = Math::Clamp(CVarTemporalAACurrentFrameWeight.Get(), 0.01f, 1.0f);
    cb.catmullRom = CVarTemporalAACatmullRom.Get();
    cb.quality = (uint32_t)Math::Clamp(CVarTemporalAAQuality.Get(), 0, 3);
}

// Main update function called once per frame
void UpdateAllRenderingCVars(RenderFrame& frame) {
    UpdateTonemapperCB(frame, frame.tonemapperCB);
    UpdateBloomCB(frame.bloomCB);
    UpdateGTAOCB(frame.gtaoCB);
    UpdateShadowCB(frame.shadowCB, frame.directionalLight);
    UpdateTemporalAACB(frame.taaParams, frame.viewState);
    // ... repeat for all parameter groups
}
```

---

## Step 4: Wire CB into Shader

On the GPU side, read from constant buffer:

```hlsl
// In shaders/runtime/include/render_params.hlsli
cbuffer TonemapperParams : register(b0) {
    float g_TonemapperSharpen;
    float g_TonemapperGamma;
    float2 _pad0;
};

cbuffer BloomParams : register(b1) {
    float g_BloomScreenPercentage;
    uint g_BloomCacheKernel;
    uint g_BloomAsyncCompute;
    uint _pad0;
};

cbuffer GTAOParams : register(b2) {
    float g_GTAOFalloffEnd;
    float g_GTAOFalloffStartRatio;
    float g_GTAOThicknessBlend;
    float g_GTAONumAngles;
};

cbuffer ShadowParams : register(b3) {
    float g_ShadowCSMDepthBias;
    float g_ShadowCSMSlopeScaleDepthBias;
    float g_ShadowCSMReceiverBias;
    float g_ShadowTransitionScale;
    float g_ShadowPointLightDepthBias;
    float g_ShadowPointLightSlopeScaleDepthBias;
    float g_ShadowSpotLightDepthBias;
    float g_ShadowSpotLightSlopeDepthBias;
    float g_ShadowMaxSlopeScaleDepthBias;
    float3 _pad;
};

cbuffer TemporalAAParams : register(b4) {
    float g_TAAFilterSize;
    float g_TAACurrentFrameWeight;
    uint g_TAACatmullRom;
    uint g_TAAQuality;
};
```

Then use in shaders:

```hlsl
// tone_map_ps.hlsl
float4 main(PSInput input) : SV_Target {
    float3 hdrColor = input.color.rgb;
    
    // Apply ACES tone curve
    float3 tonemapped = ACESFitted(hdrColor);
    
    // Apply sharpness (r.Tonemapper.Sharpen)
    if (g_TonemapperSharpen > 0.0) {
        float3 sharpened = tonemapped * (1.0 + g_TonemapperSharpen);
        float3 original = tonemapped;
        tonemapped = lerp(original, sharpened, saturate(g_TonemapperSharpen / 10.0));
    }
    
    // Apply gamma (r.TonemapperGamma)
    if (g_TonemapperGamma > 0.0) {
        tonemapped = pow(tonemapped, 1.0 / g_TonemapperGamma);
    }
    
    return float4(tonemapped, 1.0);
}

// gtao_cs.hlsl
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    float falloffEnd = g_GTAOFalloffEnd;
    float falloffStartRatio = g_GTAOFalloffStartRatio;
    float falloffStart = falloffEnd * falloffStartRatio;
    
    float ao = ComputeGTAO(Depth, Normal, falloffStart, falloffEnd);
    
    // Apply thickness blend (r.GTAO.ThicknessBlend)
    float thicknessBlend = saturate(1.0 - (g_GTAOThicknessBlend * g_GTAOThicknessBlend));
    ao = lerp(1.0, ao, thicknessBlend);
    
    OutAO[DTid.xy] = float4(ao, 0, 0, 0);
}

// shadow_depth_ps.hlsl
float ComputeDepthBias(float3 normal, float3 lightDir) {
    float constantBias = g_ShadowCSMDepthBias;
    float slopeBias = g_ShadowCSMSlopeScaleDepthBias;
    
    float NoL = saturate(dot(normal, -lightDir));
    float angle = acos(NoL);
    
    float totalBias = constantBias + slopeBias * tan(angle);
    totalBias = min(totalBias, g_ShadowMaxSlopeScaleDepthBias);
    
    return totalBias;
}
```

---

## Step 5: Validation & Clamping

Always validate CVar values in the update function:

```cpp
void ValidateCVarRange(const char* cvarName, float& value, float minVal, float maxVal) {
    if (value < minVal || value > maxVal) {
        UZ_LOG(Render, Warning, "CVar %s out of range [%f, %f]. Value: %f. Clamping.", 
               cvarName, minVal, maxVal, value);
        value = Math::Clamp(value, minVal, maxVal);
    }
}

void UpdateGTAOCB(GTAOParams& cb) {
    cb.falloffEnd = CVarGTAOFalloffEnd.Get();
    ValidateCVarRange("r.GTAO.FalloffEnd", cb.falloffEnd, 1.0f, 10000.0f);
    
    cb.falloffStartRatio = CVarGTAOFalloffStartRatio.Get();
    ValidateCVarRange("r.GTAO.FalloffStartRatio", cb.falloffStartRatio, 0.0f, 0.999f);
    
    cb.numAngles = CVarGTAONumAngles.Get();
    ValidateCVarRange("r.GTAO.NumAngles", cb.numAngles, 1.0f, 16.0f);
}
```

---

## Step 6: Console Command Registration

Register CVars with the engine's console so they can be modified at runtime:

```cpp
void RegisterRenderingCVars() {
    // Tone mapping
    GetConsoleManager()->RegisterVariable(
        "r.Tonemapper.Sharpen",
        CVarTonemapperSharpen.Get(),
        "Sharpening in tonemapper. <0: inherit from post-process volume, 0: off",
        [](float val) { CVarTonemapperSharpen.Set(val); }
    );
    
    // Bloom
    GetConsoleManager()->RegisterVariable(
        "r.Bloom.ScreenPercentage",
        CVarBloomScreenPercentage.Get(),
        "FFT bloom kernel resolution (% of screen)",
        [](float val) { CVarBloomScreenPercentage.Set(Math::Max(val, 10.0f)); }
    );
    
    // GTAO
    GetConsoleManager()->RegisterVariable(
        "r.GTAO.FalloffEnd",
        CVarGTAOFalloffEnd.Get(),
        "Distance where AO completes falloff (cm)",
        [](float val) { CVarGTAOFalloffEnd.Set(Math::Max(val, 1.0f)); }
    );
    
    // ... repeat for all CVars
}
```

At engine startup:

```cpp
int main() {
    Engine engine;
    RegisterRenderingCVars();  // Register all CVars
    // ... rest of startup
}
```

---

## Step 7: Runtime Changes

At the Lua/editor console level, users can now modify CVars:

```
> r.Tonemapper.Sharpen 2.0
> r.GTAO.FalloffEnd 300.0
> r.Bloom.ScreenPercentage 80.0
```

Or from C++:

```cpp
GetConsoleManager()->ExecuteCommand("r.Tonemapper.Sharpen 2.0");

// Or programmatically:
CVarTonemapperSharpen.Set(2.0f);
```

Next frame, the updated CBs are used in shaders (no recompilation).

---

## Implementation Roadmap by Priority

### Phase 1: Core Rendering (Highest Impact)

- [ ] `r.Tonemapper.Sharpen` — tone mapping quality
- [ ] `r.TonemapperGamma` — display gamma
- [ ] `r.TemporalAAFilterSize` — TAA quality
- [ ] `r.TemporalAACurrentFrameWeight` — TAA blur/jitter tradeoff
- [ ] `r.GTAO.FalloffEnd` — AO radius
- [ ] `r.GTAO.NumAngles` — AO quality
- [ ] `r.Shadow.CSMDepthBias` — shadow artifact control

### Phase 2: Advanced Features

- [ ] `r.Bloom.*` — bloom control (threshold, intensity, quality)
- [ ] `r.SSR.Quality` — reflection quality
- [ ] `r.SSGI.Quality` — GI quality
- [ ] `r.SkyAtmosphere.*` — atmosphere detail
- [ ] `r.VolumetricFog.*` — fog parameters

### Phase 3: Special Cases

- [ ] `r.VolumetricCloud.*` — cloud detail
- [ ] `r.HairStrands.*` — hair-specific
- [ ] `r.RayTracing.*` — ray tracing features
- [ ] `r.Lumen.*` — Lumen GI

---

## Expected Outcome

After completing this implementation:

1. **Every rendering parameter is runtime-configurable** — no code changes needed to tweak
2. **Matches UE5's level of control** — artists can iterate without recompiling
3. **Shaders reference only CBs** — no hardcoded values anywhere
4. **Performance tuning is trivial** — change CVar, immediately see result in editor/play mode
5. **Quality scaling is possible** — different settings per platform (r.TemporalAA.Quality=1 on mobile, 3 on desktop)

---

## Anti-Patterns to Avoid

**FORBIDDEN:**
- ❌ Hardcoding parameter values in shaders (e.g., `float bias = 10.0;`)
- ❌ Commenting out CVars to "simplify"
- ❌ Different defaults between C++ and shader (they MUST match)
- ❌ Forgetting to validate ranges (can cause NaN, Inf, or shader errors)
- ❌ Not documenting what each CVar does

**REQUIRED:**
- ✅ Every CVar has a default value from published reference
- ✅ Every CVar flows through CB struct
- ✅ Every CB field has a shader counterpart
- ✅ Every shader reads from CB, never hardcoded
- ✅ Ranges validated in C++ before passing to GPU
- ✅ Clear help text explaining the parameter

