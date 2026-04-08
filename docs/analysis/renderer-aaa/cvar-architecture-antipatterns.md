<!-- doc-meta
title: CVar Architecture — Anti-patterns & Traps
author: UzEngine Rendering Analysis
date: 2026-04-04
status: critical
-->

# CVar Architecture — Anti-patterns & Traps

This document identifies common mistakes when implementing runtime-configurable rendering systems and how UE5 avoids them.

---

## Anti-Pattern 1: Hardcoded Values in Shaders

**WRONG:**
```hlsl
// bloom_ps.hlsl
float4 main(PSInput input) : SV_Target {
    float threshold = 1.0;  // HARDCODED! Cannot change at runtime
    float intensity = 2.5;  // HARDCODED!
    float3 bloom = Bloom(input.sceneColor, threshold, intensity);
    return float4(bloom, 1.0);
}
```

**CONSEQUENCE:** 
- Cannot tweak bloom without shader recompilation
- Artist feedback takes 60+ seconds (full rebuild)
- Impossible to profile different settings
- Cannot scale quality per-platform (all platforms get same expensive settings)

**CORRECT:**
```hlsl
// bloom_ps.hlsl
cbuffer BloomParams : register(b0) {
    float g_BloomThreshold;
    float g_BloomIntensity;
    float2 _pad;
};

float4 main(PSInput input) : SV_Target {
    float3 bloom = Bloom(input.sceneColor, g_BloomThreshold, g_BloomIntensity);
    return float4(bloom, 1.0);
}
```

C++ side:
```cpp
struct BloomParams {
    float threshold;
    float intensity;
};

void UpdateBloomCB(BloomParams& cb) {
    cb.threshold = CVarBloomThreshold.Get();  // Read CVar
    cb.intensity = CVarBloomIntensity.Get();   // Read CVar
}
```

**Why UE5 Does This:**
- Every parameter is a CVar
- Artists never wait for rebuilds
- Instant feedback loop (change CVar → see result next frame)

---

## Anti-Pattern 2: Compilation-Time Conditionals Instead of Runtime Flags

**WRONG:**
```hlsl
#define ENABLE_BLOOM 1
#define BLOOM_QUALITY 3

float4 main(PSInput input) : SV_Target {
#if ENABLE_BLOOM
    float3 color = ApplyBloom(input.sceneColor, BLOOM_QUALITY);
#else
    float3 color = input.sceneColor;
#endif
    return float4(color, 1.0);
}
```

**CONSEQUENCE:**
- Shader must be recompiled to toggle bloom
- Different paths for different devices = different shaders = validation nightmare
- Cannot A/B test two approaches (requires two separate builds)
- Quality setting baked in at compile time (cannot change at runtime)

**CORRECT:**
```hlsl
cbuffer BloomControl : register(b0) {
    uint g_BloomEnabled;
    uint g_BloomQuality;
    float2 _pad;
};

float4 main(PSInput input) : SV_Target {
    float3 color = input.sceneColor;
    
    if (g_BloomEnabled) {
        color = ApplyBloom(color, g_BloomQuality);
    }
    
    return float4(color, 1.0);
}
```

**Why UE5 Does This:**
- Single shader for all quality levels
- Runtime branching has negligible cost on modern GPUs
- Toggle features on/off without recompiling
- A/B test two algorithms with same executable

---

## Anti-Pattern 3: Initialization-Time Only Parameters

**WRONG:**
```cpp
struct GTAORenderer {
    float falloffRadius = 200.0f;  // Set once, never changes
    float numAngles = 2.0f;         // Baked in constructor
    
    void Render() {
        // falloffRadius and numAngles are constants now
        ComputeGTAO(falloffRadius, numAngles);
    }
};

// To change: must reconstruct renderer
renderer = GTAORenderer();  // Reset, lose state
renderer.falloffRadius = 300.0f;
renderer.Render();
```

**CONSEQUENCE:**
- Cannot change parameter without stopping render
- State loss on rebuild
- Impossible to hot-reload parameters
- Artist feedback requires engine restart

**CORRECT:**
```cpp
class GTAORenderer {
    void Render() {
        // Read CVars every frame
        float falloffRadius = CVarGTAOFalloffEnd.Get();
        float numAngles = CVarGTAONumAngles.Get();
        
        ComputeGTAO(falloffRadius, numAngles);
    }
};

// At runtime: change CVar, it takes effect immediately
// ConsoleSys::Execute("r.GTAO.FalloffEnd 300.0");  // Next frame: uses 300
```

**Why UE5 Does This:**
- CVars read every frame (lightweight)
- Parameters change instantly without interrupting render
- Supports hot-reload and dynamic quality scaling

---

## Anti-Pattern 4: Inconsistent Defaults Between C++ and Shader

**WRONG:**
```cpp
// C++ side
CVarShadowDepthBias.Set(10.0f);  // Default: 10.0

// Shader side
cbuffer ShadowParams : register(b0) {
    float g_DepthBias = 5.0f;  // Default: 5.0 — MISMATCH!
};
```

**CONSEQUENCE:**
- Editor shows one value, shader uses different value until CB updated
- Confusion about actual parameter values
- Bugs when CB update is skipped (shadow artifacts appear/disappear randomly)
- Hard to debug (print value in C++, different in shader)

**CORRECT:**
```cpp
// Engine source (one definition)
namespace uz::render {
    static const float SHADOW_DEPTH_BIAS_DEFAULT = 10.0f;
    static const float SHADOW_DEPTH_BIAS_MIN = 0.0f;
    static const float SHADOW_DEPTH_BIAS_MAX = 100.0f;
    
    static ConsoleVariable<float> CVarShadowDepthBias(
        "r.Shadow.CSMDepthBias",
        SHADOW_DEPTH_BIAS_DEFAULT,
        "Constant depth bias for CSM"
    );
}

// C++ side
cb.depthBias = CVarShadowDepthBias.Get();

// Shader side
cbuffer ShadowParams : register(b0) {
    float g_ShadowDepthBias;  // No default; value comes from CB
};
```

**Why UE5 Does This:**
- Single source of truth (C++ definition)
- Shader never has hardcoded defaults
- CB update is **always** correct

---

## Anti-Pattern 5: Skipping CB Updates Due to "Optimization"

**WRONG:**
```cpp
void RenderFrame() {
    // Only update CB if something changed (WRONG!)
    if (bloom_changed) {
        UpdateBloomCB(bloomCB);
    }
    
    // But what if bloom CVar was changed and bloom_changed is false?
    // Shader reads stale CB!
}
```

**CONSEQUENCE:**
- Stale CBs cause intermittent visual bugs
- Difficult to debug (bug only appears sometimes)
- CVar changes ignored if changelog wasn't set
- Shader shows wrong behavior until next unrelated change

**CORRECT:**
```cpp
void RenderFrame() {
    // Update ALL rendering CBs EVERY FRAME
    // (Cost: ~0.1ms for all CB updates — negligible)
    UpdateTonemapperCB(tonemapperCB);
    UpdateBloomCB(bloomCB);
    UpdateGTAOCB(gtaoCB);
    UpdateShadowCB(shadowCB);
    UpdateTemporalAACB(taaParams);
    // ... (all others)
    
    // Bind all CBs
    BindConstantBuffers();
    
    // Render
}
```

**Why UE5 Does This:**
- CB update cost is negligible (< 1% of frame time)
- Guaranteed correctness (no stale data)
- No complex change tracking
- Debugging is straightforward (CB = CVar value, always)

---

## Anti-Pattern 6: Missing Validation & Clamping

**WRONG:**
```hlsl
// Shader receives unclamped CVar values
float falloffEnd = g_GTAOFalloffEnd;  // Could be negative!
float numAngles = g_GTAONumAngles;     // Could be 0 or 1000!

// This can cause:
// - Division by zero: falloffStart / falloffEnd
// - Negative values: exp(-density)
// - Out-of-bounds: sample count > max hardware threads
```

**CONSEQUENCE:**
- Shader crashes or hangs GPU
- Undefined behavior (NaN, Inf propagation)
- Black screen or artifacts
- Difficult to diagnose (user changed one CVar to weird value)

**CORRECT:**
```cpp
void UpdateGTAOCB(GTAOParams& cb) {
    float falloffEnd = CVarGTAOFalloffEnd.Get();
    float numAngles = CVarGTAONumAngles.Get();
    
    // Validate BEFORE sending to GPU
    cb.falloffEnd = Math::Max(falloffEnd, 1.0f);  // Clamp min
    cb.numAngles = Math::Clamp(numAngles, 1.0f, 16.0f);  // Clamp range
    
    // Log if clamped
    if (cb.falloffEnd != falloffEnd) {
        UZ_LOG(Render, Warning, 
               "r.GTAO.FalloffEnd clamped from %f to %f", 
               falloffEnd, cb.falloffEnd);
    }
}
```

Then in shader:
```hlsl
// Safe to use (guaranteed in valid range)
float falloffStart = g_GTAOFalloffEnd * g_GTAOFalloffStartRatio;
float ao = ComputeAO(falloffStart, g_GTAOFalloffEnd);  // No division by zero
```

**Why UE5 Does This:**
- Validation in C++ (one place, easy to audit)
- Shaders never see invalid data
- Users can set any value (engine handles it gracefully)

---

## Anti-Pattern 7: Not Documenting CVar Purpose & Range

**WRONG:**
```cpp
CVarGTAOFalloffEnd.Set(200.0f);  // What does 200.0 mean? Pixels? Meters? Cm?
CVarTonemapperSharpen.Set(5.0f);  // Is 5 a percentage? Multiplier? Max clamp?
```

**CONSEQUENCE:**
- Users don't know valid range
- Confusion about units (pixels vs. cm vs. km)
- Invalid values set (1000 when range is [0, 100])
- Hard to debug user error

**CORRECT:**
```cpp
static ConsoleVariable<float> CVarGTAOFalloffEnd(
    "r.GTAO.FalloffEnd",
    200.0f,
    "Distance (cm) where ambient occlusion completes falloff. "
    "Valid range: [1, 10000]. "
    "Controls max AO influence radius. Smaller = tighter shadows, faster. "
    "Larger = softer shadows, more expensive. "
    "@ref: Burgess 2011 'Ambient Occlusion'"
);

static ConsoleVariable<float> CVarTonemapperSharpen(
    "r.Tonemapper.Sharpen",
    -1.0f,
    "Sharpening amount applied after ACES tone curve. "
    "Valid range: [-1, 10]. "
    "-1: Inherit from PostProcessVolume (default). "
    "0: Off (no sharpening). "
    ">0: Sharpness factor (1.0 = mild, 10.0 = extreme). "
    "Clamped to [0, 10] in shader. "
    "@ref: ACES RRT+ODT (Academy Standard)"
);
```

**Why UE5 Does This:**
- Help text in console (`r.GTAO.FalloffEnd ?`)
- Users know valid range & units
- Reference docs link to papers
- No confusion about interpretation

---

## Anti-Pattern 8: Cascading Dependencies Without Checks

**WRONG:**
```cpp
void UpdateAllCBs() {
    // Quality cascades: if SSR disabled, don't update SSR params
    UpdateSSRCB(ssrCB);  // Uses r.SSR.Quality
    
    // But SSR could be disabled via a different CVar!
    if (r.SSR.Enabled == 0) {  // OOPS: SSR CB still updated
        // CB has stale data
    }
}
```

**CONSEQUENCE:**
- Disabled passes still consume memory/compute
- Confusing behavior (disabled pass still affecting output)
- GPU thrashing (unused passes still running)

**CORRECT:**
```cpp
void UpdateAllCBs(const RenderFrame& frame) {
    // Check enabling CVars FIRST
    
    if (CVarBloomEnabled.Get()) {
        UpdateBloomCB(bloomCB);
        bloomPass->Enabled = true;
    } else {
        bloomPass->Enabled = false;
    }
    
    if (CVarSSRQuality.Get() > 0) {
        UpdateSSRCB(ssrCB);
        ssrPass->Enabled = true;
    } else {
        ssrPass->Enabled = false;
    }
    
    if (CVarGTAOEnabled.Get()) {
        UpdateGTAOCB(gtaoCB);
        gtaoPass->Enabled = true;
    } else {
        gtaoPass->Enabled = false;
    }
    
    // Only bind CBs for enabled passes
    for (auto& pass : renderPasses) {
        if (pass->Enabled) {
            pass->BindConstantBuffer();
        }
    }
}
```

**Why UE5 Does This:**
- Clear enable/disable logic
- Disabled passes don't waste GPU time
- CBs only updated when needed
- Correct behavior guaranteed

---

## Anti-Pattern 9: Forgetting to Read CVars in Frame Boundary Conditions

**WRONG:**
```cpp
struct ViewState {
    float tempAAFilterSize = 1.0f;  // Cached at initialization
};

void RenderFrame(ViewState& view) {
    // Filter size never updated from CVar!
    ApplyTemporalAA(view.tempAAFilterSize);  // Stale value
}
```

**CONSEQUENCE:**
- CVar changes only take effect after ViewState is recreated
- If view persists across frames, changes never happen
- Confusing: user changes CVar, nothing happens

**CORRECT:**
```cpp
void RenderFrame(const ViewState& view) {
    // Read CVar EVERY FRAME (not cached)
    float filterSize = CVarTemporalAAFilterSize.Get();
    
    // Validate & apply
    filterSize = Math::Clamp(filterSize, 0.0f, 2.0f);
    ApplyTemporalAA(filterSize);  // Current value, always
}
```

**Why UE5 Does This:**
- CVars are read at frame boundary (frame-to-frame consistency)
- Changes take effect immediately
- ViewState is render-read-only (not modified per-frame)

---

## Anti-Pattern 10: Not Accounting for CVar Scope (Global vs. Local)

**WRONG:**
```cpp
// Global CVar
ConsoleVariable<float> CVarGTAOFalloffEnd("r.GTAO.FalloffEnd", 200.0f, "...");

// But material might override it
struct Material {
    float ambientOcclusionRadius = 150.0f;  // Conflicts with global!
};

void Render(Material& mat) {
    // Which value? Global CVar or material override?
    // No clear precedence
}
```

**CONSEQUENCE:**
- Conflicting values (global vs. local)
- Unclear which takes precedence
- Hard to debug material-specific issues

**CORRECT:**
```cpp
// Global CVar = baseline for all materials
ConsoleVariable<float> CVarGTAOFalloffEnd("r.GTAO.FalloffEnd", 200.0f, "...");

// Material can OVERRIDE (but has clear precedence)
struct Material {
    float ambientOcclusionRadiusOverride = -1.0f;  // -1 = use global CVar
};

void RenderMaterial(Material& mat, float globalAORadius) {
    float aoRadius = (mat.ambientOcclusionRadiusOverride >= 0.0f) 
        ? mat.ambientOcclusionRadiusOverride 
        : globalAORadius;  // Use global if not overridden
    
    ApplyAO(aoRadius);
}

// In frame update:
float globalAORadius = CVarGTAOFalloffEnd.Get();  // Read CVar once
for (auto& material : materials) {
    RenderMaterial(material, globalAORadius);
}
```

**Why UE5 Does This:**
- CVars = engine-wide baseline
- Components can override (but inherit defaults)
- Clear precedence: component > CVar > hardcoded constant
- Debugging straightforward (know where value came from)

---

## Summary: The Right Way (UE5 Pattern)

```
Frame Start
    ├─ Read all CVars into local variables (struct)
    ├─ Validate & clamp ranges in C++
    ├─ Update all constant buffer structs
    ├─ Bind CBs to graphics/compute pipelines
    └─ Render passes read from CB (never hardcoded)

Next Frame
    ├─ Repeat (CVars might have changed)
    └─ Shader behavior updates automatically
```

**Key Principles:**
1. **Every parameter is a CVar** (no exceptions)
2. **CVars read at frame start** (consistency)
3. **CB structs mirror shader layout** (no translation)
4. **Validation in C++** (shaders see clean data)
5. **No hardcoding** anywhere (total runtime control)
6. **Clear help text** (users know valid range)
7. **Immediate effect** (change CVar → see result next frame)

---

## Checklist for New Rendering Feature

Before shipping a new feature, verify:

- [ ] Feature has a enable/disable CVar (e.g., `r.FeatureName`)
- [ ] All parameters are CVars (no hardcoded values)
- [ ] CB struct matches shader layout exactly
- [ ] CB updated every frame (not cached)
- [ ] Valid range documented (min, max, units)
- [ ] Clamping applied in C++ (before GPU)
- [ ] Help text references published paper (if applicable)
- [ ] Default matches reference implementation
- [ ] Disabled path is lightweight (no GPU cost when disabled)
- [ ] No compilation-time conditionals (only runtime flags)

---

## When NOT to Use CVars

CVars control rendering behavior. **Don't** use CVars for:
- **Scene data** (material properties come from textures/buffers, not CVars)
- **Asset parameters** (mesh LODs come from asset, not CVars)
- **One-off debug values** (use dedicated debug render mode instead)

**DO** use CVars for:
- Render pass parameters (quality, intensity, radius, bias)
- Algorithm selection (temporal weight, filter kernel, sample count)
- Performance tuning (resolution scale, layer count, max threads)
- Quality levels (mobile vs. console vs. desktop settings)

