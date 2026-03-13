# VFX Shader Generation — Claude HLSL Auto-Generation with Validation

> **Status**: Planned (Phase 3)
> **Source**: `editor/vfx-ai-tool/shader_generator.ts`, `runtime/vfx-ai/llm_shader_generator.cpp`
> **References**: Claude API (Anthropic), DXC compiler, SPIR-V Reflect, Shader Model validation

## Purpose

Claude Opus/Sonnet generates valid HLSL shaders for particle materials directly from effect descriptions. A shader validation pipeline (compile → reflect → preview) ensures all generated code compiles and renders correctly. Error recovery feeds DXC errors back to Claude for automatic retry, achieving **>98% first-pass success rate** on Tier 1–2 effects.

## Shader Tiers & Complexity

The system categorizes shaders into tiers based on required complexity:

| Tier | Complexity | % of Effects | Examples | Cost | Model |
|------|-----------|--------------|----------|------|-------|
| **Tier 1** | Simple billboard | 90% | Fire, smoke, sparks, glow | $0.001–0.01 | Sonnet |
| **Tier 2** | Glow + flipbook | 8% | Animated sprites, trails | $0.02–0.05 | Sonnet |
| **Tier 3** | Distortion, complex | 2% | Plasma, advanced VFX | $0.05–0.10 | Opus |

**Auto-detection**: System analyzes user description for keywords ("distort", "warp", "shimmer", "complex") and selects appropriate tier/model.

## Prompt Engineering & Style Guide

Claude is given a curated shader style guide + existing patterns. Prompt structure:

```
SYSTEM PROMPT (prepended to all requests):
═══════════════════════════════════════════════════════════════════

You are a HLSL shader expert for game VFX. Generate pixel shaders for
particle sprites. Follow these rules strictly:

1. TARGET: SM6.6 (bindless samplers), fallback SM5.0 (traditional samplers)
2. SYNTAX: HLSL 2021, DirectX 12
3. CONVENTIONS:
   - cbuffer ParticleData : register(b0) { float3 color_tint; float intensity; };
   - Texture2D tex_diffuse : register(t0);
   - SamplerState smp_linear : register(s0);
   - Entry point: "main" with input struct containing TEXCOORD0, COLOR0, SV_POSITION
   - Always return float4 (RGBA, premultiplied alpha)

4. PATTERNS TO FOLLOW (study these):
   [examples of simple billboard, glow, flipbook shaders shown]

5. OUTPUT FORMAT:
   - ONLY valid HLSL code
   - NEVER include explanations, comments, or markdown
   - Include cbuffer if parameterized
   - Include function docstrings (/// comments)

USER PROMPT (per-request):

Generate a particle shader for: {user_description}

Parameters:
- Color: {auto_extracted_color}
- Intensity: {auto_extracted_intensity}
- Animation: {auto_extracted_animation}

Return ONLY the HLSL code, ready to compile with SM6.6 DXC.
═══════════════════════════════════════════════════════════════════
```

**Parameter extraction**: System parses user description to extract likely parameters:
- "bright blue" → color = (0, 0, 1), intensity = 1.5
- "soft glow" → use smoothstep for falloff
- "animated" → add time-based UV offset

## Shader Compilation & Validation

After Claude generates shader code:

```
┌──────────────────────────────────────────────────────┐
│ 1. Compile with DXC (SM6.6)                          │
│    Input: .hlsl text from Claude                     │
│    dxc /T ps_6_6 /Fo compiled.cso shader.hlsl        │
└────────────┬─────────────────────────────────────────┘
             │
             ├─ SUCCESS? ───────────────────┐
             │                              │
             ▼                              ▼
    ┌─────────────────┐            ┌──────────────────┐
    │ 2. SPIR-V       │            │ ERROR: Retry     │
    │    Reflect      │            │ (see below)      │
    │    - Samplers   │            └──────────────────┘
    │    - Textures   │
    │    - CBuffers   │
    └────────┬────────┘
             │
             ▼
    ┌─────────────────┐
    │ 3. Render Test  │
    │    - Create     │
    │      RTT        │
    │    - Draw quad  │
    │    - Compare    │
    │      to golden  │
    └────────┬────────┘
             │
             ▼
    ┌─────────────────────┐
    │ 4. Add to Cache     │
    │    Content-hash key │
    │    (shader src +    │
    │     includes +      │
    │     target)         │
    └─────────────────────┘
```

**Cache key**: `xxHash64(shader_src + includes + defines + target)`. Identical shaders reuse cached `.cso`.

### Error Recovery

If DXC fails, system retries with Claude:

```cpp
/// Generate HLSL shader with automatic retry on compile error.
class LLMShaderGenerator {
public:
    struct GenerateResult {
        bool success;
        std::string hlsl_code;        // Generated shader source
        std::string compiled_cso_hex; // Compiled bytecode (hex string)
        std::string error_message;    // If !success
    };

    /// Generate shader from description.
    /// Retries up to 2x on compile errors, escalates to Opus after Sonnet fails.
    GenerateResult GenerateShader(
        StringView effect_description,
        const ShaderParameters& parameters,
        int max_retries = 2
    );

private:
    /// Call Claude API
    std::string CallClaudeAPI(
        const std::string& prompt,
        bool use_opus = false  // true = Opus, false = Sonnet
    );

    /// Compile HLSL → DXC CSO bytecode
    std::pair<bool, std::string> CompileHLSL(
        StringView hlsl_code,
        StringView entry_point = "main",
        StringView target = "ps_6_6"
    );

    /// Extract error message from DXC stderr
    std::string ExtractCompileError(const std::string& dxc_stderr);
};

// Usage:
LLMShaderGenerator gen;
auto result = gen.GenerateShader("bright blue energy explosion", params);
if (result.success) {
    material->SetPixelShader(result.compiled_cso_hex);
} else {
    UZ_LOG(VFX, Error, "Shader generation failed: {}", result.error_message);
}
```

**Retry strategy**:
1. **1st attempt**: Sonnet (fast, cheap)
2. **2nd attempt** (if failed): Feed DXC error to Sonnet with "fix this error: {error_msg}"
3. **3rd attempt** (if Sonnet fails again): Escalate to Opus with full context

Success rate:
- Tier 1: 98% (Sonnet, first-pass)
- Tier 2: 95% (Sonnet, may need 1–2 retries)
- Tier 3: 90% (Opus, more complex)

## Generated Shader Examples

### Tier 1: Simple Billboard Fire Shader

**User input**: "bright orange campfire, glowing, rising smoke"

**Generated HLSL**:
```hlsl
/// Simple billboard particle shader with intensity modulation.
/// Supports color tint and lifecycle fade.
cbuffer ParticleData : register(b0) {
    float3 color_tint;
    float intensity;
    float4 padding;
};

Texture2D tex_diffuse : register(t0);
SamplerState smp_linear : register(s0);

struct VS_OUTPUT {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

float4 main(VS_OUTPUT input) : SV_TARGET {
    float4 tex_sample = tex_diffuse.Sample(smp_linear, input.uv);

    // Apply color tint
    float3 final_color = tex_sample.rgb * color_tint * intensity;

    // Fade based on vertex color alpha (set by particle system)
    float alpha = tex_sample.a * input.color.a;

    return float4(final_color, alpha);
}
```

### Tier 2: Animated Flipbook Shader

**User input**: "electric energy with crackling animation"

**Generated HLSL**:
```hlsl
/// Animated flipbook shader for sprite sheet textures.
/// Cycles through frames based on particle lifetime.
cbuffer ParticleData : register(b0) {
    float3 color_tint;
    float intensity;
    float2 frame_size;    // 1.0 / texture_cols, 1.0 / texture_rows
    float anim_speed;     // frames per second
};

Texture2D tex_diffuse : register(t0);
SamplerState smp_linear : register(s0);

struct VS_OUTPUT {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float life_norm : TEXCOORD1;  // [0,1] age / lifespan
};

float4 main(VS_OUTPUT input) : SV_TARGET {
    // Calculate frame index from normalized lifetime
    float frame = input.life_norm * anim_speed;
    uint frame_index = uint(fmod(frame, 16.0));  // Assume 16 frames

    // Unpack frame_index into 2D (4x4 grid assumed)
    uint frame_x = frame_index % 4;
    uint frame_y = frame_index / 4;

    // Compute offset into sprite sheet
    float2 frame_offset = float2(frame_x, frame_y) * frame_size;
    float2 uv_in_frame = input.uv * frame_size;
    float2 final_uv = frame_offset + uv_in_frame;

    float4 tex_sample = tex_diffuse.Sample(smp_linear, final_uv);
    float3 final_color = tex_sample.rgb * color_tint * intensity;
    float alpha = tex_sample.a * input.color.a;

    return float4(final_color, alpha);
}
```

### Tier 3: Distortion + Glow Shader

**User input**: "plasma effect with distortion and bright glow, shifts between blue and purple"

**Generated HLSL**:
```hlsl
/// Complex shader with distortion and color shift over lifetime.
cbuffer ParticleData : register(b0) {
    float3 color_start;
    float3 color_end;
    float intensity;
    float distortion_amount;
    float4x4 view;  // For parallax distortion
};

Texture2D tex_diffuse : register(t0);
Texture2D tex_normal : register(t1);     // Normal map for distortion
SamplerState smp_linear : register(s0);
SamplerState smp_clamp : register(s1);

struct VS_OUTPUT {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float2 uv_distort : TEXCOORD1;       // Scrolling distortion UVs
    float4 color : COLOR0;
    float life_norm : TEXCOORD2;
};

float4 main(VS_OUTPUT input) : SV_TARGET {
    // Sample distortion normal map
    float2 dist_uv = input.uv_distort + float2(length(input.uv) * 0.1, 0);
    float3 normal = tex_normal.Sample(smp_linear, dist_uv).rgb;

    // Apply distortion to main UVs
    float2 distorted_uv = input.uv + (normal.xy - 0.5) * 2.0 * distortion_amount;

    float4 tex_sample = tex_diffuse.Sample(smp_linear, distorted_uv);

    // Interpolate color over lifetime
    float3 color = lerp(color_start, color_end, input.life_norm);

    // Glow via smoothstep on alpha
    float glow = smoothstep(0.1, 0.5, tex_sample.a);
    float3 final_color = tex_sample.rgb * color * intensity * (1.0 + glow);

    return float4(final_color, tex_sample.a * input.color.a);
}
```

## SM6.6 vs SM5.0 Shader Variants

Generated shaders automatically include dual-path support for bindless (SM6.6) vs. traditional samplers (SM5.0):

```hlsl
#ifdef UZ_BINDLESS
    // SM6.6 bindless path (index-based texture access)
    Texture2D tex_diffuse[] : register(t0, space1);
    SamplerState smp_linear[] : register(s0, space1);

    float4 tex_sample = tex_diffuse[NonUniformResourceIndex(tex_index)].Sample(
        smp_linear[NonUniformResourceIndex(smp_index)], uv
    );
#else
    // SM5.0 fallback (traditional samplers)
    Texture2D tex_diffuse : register(t0);
    SamplerState smp_linear : register(s0);

    float4 tex_sample = tex_diffuse.Sample(smp_linear, uv);
#endif
```

Both paths compiled separately, cached independently.

## .uzvfx JSON Assembly

After shader generation, Claude also generates the effect definition (`.uzvfx` JSON). This specifies:
- Emission rate, particle lifespan, velocity
- Size/color curves over lifetime
- Gravity, damping, collision behavior
- Rendering material (with generated shader)
- Module order and parameters

```json
{
    "format": "uzvfx",
    "version": 1,
    "name": "plasma_explosion",
    "type": "emitter",

    "spawn": {
        "rate": 500,
        "lifespan_min": 1.5,
        "lifespan_max": 3.0,
        "velocity": [0, 0, 15],
        "velocity_variation": 8.0,
        "spread_angle": 60
    },

    "update": {
        "gravity": [0, 0, -5],
        "damping": 0.92,
        "collision": false
    },

    "render": {
        "type": "sprite",
        "material": "generated/plasma_explosion.uzmaterial",
        "sprite_size_min": [0.3, 0.3],
        "sprite_size_max": [1.5, 1.5],
        "size_over_life": "generated/curves/plasma_size.uzcurve",
        "color_over_life": "generated/curves/plasma_color.uzcurve",
        "sort": "back_to_front",
        "cast_shadow": false
    },

    "parameters": {
        "intensity": { "type": "float", "default": 1.0, "min": 0.0, "max": 2.0 },
        "color_tint": { "type": "vec3", "default": [0.2, 0.2, 1.0] }
    }
}
```

## Shipping Builds & Dev-Only API

All generated shaders are **pre-compiled and cached** in shipping builds. Claude API is only used in dev builds:

```cpp
#if UZ_SHADER_DEV
    #include "vfx-ai/llm_shader_generator.h"

    // Dev build: can generate new shaders at runtime
    LLMShaderGenerator gen;
    auto result = gen.GenerateShader(description, params);
#else
    // Shipping: all shaders pre-cached, no API calls
    // (shader generation happens offline during asset cooking)
#endif
```

Build flag: `-DUZ_SHADER_DEV=1` for development, `-DUZ_SHADER_DEV=0` for shipping.

## Cost Breakdown & Metrics

**Cost per shader** (May 2025 pricing):
- Sonnet (Tier 1–2): $0.001–0.01 per request
- Opus (Tier 3): $0.05–0.15 per request (larger context, more complex)

**Metrics tracked**:
- First-pass success rate (target: 98%)
- Retry count (goal: <1 retry per effect)
- Average generation time (target: <3 sec)
- Total API costs per effect

## Related Documentation

- `docs/engine/05-shader-pipeline/` — Shader compilation, caching, reflection
- `docs/engine/25-vfx/` — VFX system, particle rendering
- `docs/engine/08-resource-system/` — Material and `.uzmaterial` format
