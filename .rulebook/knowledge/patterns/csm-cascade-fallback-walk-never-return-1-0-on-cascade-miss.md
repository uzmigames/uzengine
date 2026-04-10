# CSM cascade fallback walk — never return 1.0 on cascade miss

**Category**: rendering
**Tags**: rendering, csm, shadows, hlsl, shader

## Description

Quando uma função sample-single-cascade (`SampleShadowCascade`) detecta que o pixel está fora do frustum da cascade (projection Z fora de [0,1] OU UV fora de [0,1]), ela DEVE retornar um sentinela (`-1.0`) em vez de `1.0`. O caller (`CascadeShadow`) então caminha outward pelas cascadas a partir da `startCascade` escolhida por depth, e só cai para "fully lit" quando TODAS as cascadas missam.

Retornar `1.0` direto causa vazamento de sol em qualquer pixel fora do frustum da cascade primária — que muda constantemente ao rotacionar/mover a câmera porque o frustum CSM é derivado da view da câmera. Os sintomas são "sol seguindo a câmera" e lighting global oscilando ao rotacionar.

Aplica-se tanto ao PCF normal (`shadow.hlsli`) quanto à variante PCSS/ESM (`shadow_pcss.hlsli`).

## Example

// shadow.hlsli — single-cascade sample
float SampleShadowCascade(float3 worldPos, float3 N, float NdotL, uint cascadeIdx, ...) {
    float4 lsp = mul(float4(biasedPos, 1.0), cascadeViewProj[cascadeIdx]);
    float3 proj = lsp.xyz / lsp.w;
    // OUT OF CASCADE Z RANGE — sentinel, caller must fall back.
    if (proj.z < 0.0 || proj.z > 1.0) return -1.0;

    float2 shadowUV = proj.xy * 0.5 + 0.5;
    shadowUV.y = 1.0 - shadowUV.y;
    if (any(shadowUV < 0.0) || any(shadowUV > 1.0)) return -1.0;  // OUT OF XY

    // ... PCF sampling ...
    return shadow;  // valid [0, 1]
}

// top-level: walk outward until a cascade covers
float CascadeShadow(float3 worldPos, float viewDepth) {
    if (cascadeCount == 0) return 1.0;

    uint startCascade = cascadeCount - 1;
    [unroll] for (uint c = 0; c < kMaxCascades; ++c) {
        if (c >= cascadeCount) break;
        if (viewDepth < GetCascadeSplit(c)) { startCascade = c; break; }
    }

    float shadow = -1.0;
    uint  sel    = startCascade;
    [unroll] for (uint step = 0; step < kMaxCascades; ++step) {
        uint idx = startCascade + step;
        if (idx >= cascadeCount) break;
        float s = SampleShadowCascade(worldPos, N, NdotL, idx, shadowAtlas, shadowSampler);
        if (s >= 0.0) { shadow = s; sel = idx; break; }
    }

    // Only fall back to "fully lit" when EVERY cascade missed.
    if (shadow < 0.0) shadow = 1.0;

    // ... split-boundary blend + shadowFade ...
    return shadow;
}

## When to Use

Sempre que o shader implementar Cascaded Shadow Maps (CSM) com múltiplas cascadas. Todo `SampleShadowCascade` individual deve usar sentinela, e todo `CascadeShadow` top-level deve fazer walk outward.

## When NOT to Use

N/A — é padrão obrigatório para CSM. A única exceção seria se o projeto usa um único shadow map global (não cascaded), onde miss significa literalmente "fora do mundo".
