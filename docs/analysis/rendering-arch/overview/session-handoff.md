# Session Handoff — Render Quality Debug (2026-03-22)

## PROBLEMA

O render está escuro e sem cor. Comparando com PoC1 (E:\UzEnginePoC1), o mesmo mesh (mushroom) renderiza com cores vibrantes no PoC1 e quase preto no UzEngine atual.

## O QUE JÁ FOI PROVADO

### 1. Textura está CORRETA
- PNG original: cores vibrantes (vermelho, laranja, marrom quente)
- .uztexture BC7 sRGB: decomprimido e comparado — IDÊNTICO ao PNG original
- Pipeline de import: sRGB=true flag correto, formato BC7_UNORM_SRGB correto
- Prova: `e:/UzEngine/.screenshots/mip0_basecolor_from_pack.png` — cores vibrantes

### 2. GBuffer está CORRETO (valores lineares)
- Albedo no GBuffer: valores lineares baixos (~0.05-0.15) — NORMAL para sRGB→linear conversion
- Quando mostro `pow(albedo, 1/2.2)` direto no shader, as cores VOLTAM
- Prova: `e:/UzEngine/.screenshots/test02_albedo_gamma.png` — cores vibrantes
- Normal, roughness, metallic, AO: todos corretos nos debug views

### 3. Raw albedo * NdotL funciona
- `albedo * NdotL * 10 + albedo * 0.3` sem BRDF: cores vibrantes
- Prova: `e:/UzEngine/.screenshots/diag_raw_lighting.png`

### 4. BRDF completo escurece tudo
- `EvaluateBRDF()` + IBL + tonemap: resultado cinza/escuro
- Prova: `e:/UzEngine/.screenshots/diag_brdf_no_effects.png`

### 5. O que NÃO é o problema
- NÃO é a textura (provado)
- NÃO é o GBuffer (provado)
- NÃO é o sRGB handling (provado)
- NÃO é o tonemap (testei Narkowicz simples, mesmo resultado)

## O QUE PRECISA SER FEITO

Desativar TUDO no deferred lighting e ir religando um por um pra encontrar exatamente qual parte do código escurece a renderização.

### Arquivo principal: `runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl`

O deferred lighting shader calcula `color = ambient + Lo + emissive` onde:
- `Lo` = iluminação direta (BRDF * shadow)
- `ambient` = IBL (diffuse SH + specular cubemap) * skyLightIntensity * AO * specOcc * etc

### Etapas para debug (FAZER UMA POR VEZ, screenshot entre cada):

**NOTA IMPORTANTE:** O deferred lighting escreve no HDR RT (RGBA16F). O tonemap_ps.hlsl depois aplica exposure + ACES + LinearToSrgb. Qualquer `return` do deferred vai passar pelo tonemap. Para bypass total do tonemap, use `return float4(pow(max(valor, 0), 1.0/2.2), 1.0)` pra aplicar gamma manualmente.

```
ETAPA 0: Baseline — retornar albedo com gamma manual
    return float4(pow(albedo, 1.0/2.2), 1.0);
    → DEVE mostrar cores vibrantes (já testado, funciona)

ETAPA 1: Albedo * NdotL simples (sem BRDF)
    float3 L = normalize(lightDir.xyz);
    float NdotL = saturate(dot(N, L));
    return float4(pow(albedo * NdotL * 10.0 + albedo * 0.1, 1.0/2.2), 1.0);
    → DEVE mostrar cores (já testado, funciona)

ETAPA 2: Burley diffuse isolado (sem specular, sem multi-scatter)
    float3 L = normalize(lightDir.xyz);
    float NdotL = saturate(dot(N, L));
    float3 H = normalize(V + L);
    float VdotH = saturate(dot(V, H));
    float NdotV = max(dot(N, V), 0.001);
    float3 kD = (1.0 - metallic);
    float3 diffuse = kD * DiffuseDisneyBurley(albedo, roughness, NdotV, NdotL, VdotH);
    float3 result = diffuse * lightColor.xyz * NdotL;
    return float4(pow(max(result, 0), 1.0/2.2), 1.0);
    → Se escuro: DiffuseDisneyBurley divide por PI, o que é correto
    → Se muito escuro: o lightColor ou NdotL pode estar errado

ETAPA 3: BRDF completo (EvaluateBRDF) sem IBL
    float3 Lo = EvaluateBRDF(N, V, L_dir, lightColor.xyz, albedo, metallic, alpha, F0) * shadow;
    return float4(pow(max(Lo, 0), 1.0/2.2), 1.0);
    → Comparar com etapa 2. Se MUITO mais escuro, o MultiscatterCompensation
    → está reduzindo kD demais (energyComp.w muito alto).
    → NOTA: Eu já mudei o BRDF pra usar (1-F) ao invés de energyComp.w.
    → Verificar se o fix está ativo.

ETAPA 4: Lo + ambient IBL simples (sem specOcc, sem horizon, sem micro-shadow)
    float3 irr = GetSkySHDiffuse(N, skyIrradianceSH);
    float3 kD_ibl = (1 - FresnelSchlickRoughness(NdotV, F0, roughness)) * (1 - metallic);
    float3 diffIBL = kD_ibl * albedo * irr;
    float3 amb = diffIBL * ao * skyLightIntensity;
    float3 total = amb + Lo;
    return float4(pow(max(total, 0), 1.0/2.2), 1.0);
    → Se ok: adicionar specular IBL
    → Se escuro: irradiance SH pode estar fraco

ETAPA 5: Adicionar specular IBL
    (adicionar prefilteredColor + envBRDF ao ambient)
    → Se ok: adicionar specOcc

ETAPA 6: Adicionar specular occlusion
    float specOcc = saturate(pow(NdotV + ao, exp2(-16*roughness - 1)) - 1 + ao);
    → Se muito escuro: specOcc está matando o specular IBL

ETAPA 7: Adicionar horizon occlusion
    specularIBL *= horizon * horizon;
    → Se escuro: horizon occlusion está matando reflexões

ETAPA 8: Adicionar micro-shadow
    float microShadow = saturate(NdotL + 2*ao*ao - 1);
    → Se escuro: micro-shadow combinado com AO escurece demais

ETAPA 9: Adicionar Kulla-Conty multi-scatter no IBL
    FmsEms term
    → Se escuro: multi-scatter pode estar subtraindo energia

ETAPA 10: Remover gamma manual, deixar passar pelo tonemap real
    return float4(total, 1.0);
    → Se escuro: o tonemap (ACES + exposure) está crushando os darks
    → Testar com exposure maior (color *= 3.0 antes do return)

ETAPA 11: Adicionar aerial perspective
    → Se escuro: AP pode estar aplicando transmittance excessiva

ETAPA 12: Adicionar post-process (bloom, DOF, motion blur)
    → Religar um por um nos CVars
```

### Mudanças já feitas nesta sessão (na branch `feature/render-quality-improvements`)
- `receiverBias` 0.9→1.0 (sombras mais fortes)
- `Bloom threshold` 1.5→0.8, `intensity` 0.3→0.6
- `GTAO_AO_POWER` 0.3→0.7
- `SSR MaxRoughness` 0.4→0.8 (trace + resolve)
- `SkyLight intensity` 1.0→0.8, `indirect` 1.0→0.8
- `AutoExposure bias` 1.0→0.7
- `Shadow filter` PCSS→Poisson
- **BRDF: removeu MultiscatterCompensation do kD direto, agora usa (1-F)*(1-metallic)**
- **Micro-shadow** re-habilitado com clamp 0.3
- Tonemap: **MUDEI pra Narkowicz ACES temporariamente** (precisa reverter ou confirmar)
- Deferred lighting: **tem um `return float4(Lo, 1.0)` de debug que precisa ser removido**

### ATENÇÃO — Estado atual do código tem hacks de debug:
1. `deferred_lighting_ps.hlsl` tem um `return float4(Lo, 1.0)` antes do combine (REMOVER)
2. `tonemap_ps.hlsl` foi trocado pra Narkowicz ACES (reverter ou manter)
3. `pbr_common.hlsli` EvaluateBRDF mudou kD de `(1-energyComp.w)` pra `(1-F)` (MANTER — PoC1 parity)

### Arquivos relevantes:
- Deferred lighting: `runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl`
- BRDF: `runtime/shaders/runtime/include/pbr_common.hlsli`
- Tonemap: `runtime/shaders/runtime/postprocess/tonemap_ps.hlsl`
- ACES curve: `runtime/shaders/runtime/include/film_tonemap.hlsli`
- Frame setup (CVars): `runtime/engine/renderer/frame/frame_setup.cpp`
- Auto-exposure: `runtime/engine/renderer/passes/postprocess/auto_exposure.cpp`
- Shadow renderer: `runtime/engine/renderer/shadows/shadow_renderer.cpp`
- PoC1 deferred: `E:\UzEnginePoC1\src\engine\renderer\shaders\lighting\deferred_lighting.ps.hlsl`
- PoC1 BRDF: `E:\UzEnginePoC1\src\engine\renderer\shaders\brdf\evaluate.hlsli`
- PoC1 composite: `E:\UzEnginePoC1\src\engine\renderer\shaders\post\composite.ps.hlsl`

### Screenshots de referência:
- Textura original: `e:/UzEngine/.screenshots/mip0_basecolor_from_pack.png`
- Albedo com gamma manual (OK): `e:/UzEngine/.screenshots/test02_albedo_gamma.png`
- Raw lighting sem BRDF (OK): `e:/UzEngine/.screenshots/diag_raw_lighting.png`
- BRDF completo (escuro): `e:/UzEngine/.screenshots/diag_brdf_no_effects.png`
- Demo de referência (FBX test): `runtime/demos/fbx_test/`

### Build:
```bash
cd e:/UzEngine/runtime && rm -rf .zig-cache build/generated/shaders && zig build
cd e:/UzEngine/runtime/demos/fbx_test && rm -rf build/bin/cache && zig build
./build/bin/fbx_test.exe --screenshot e:/UzEngine/.screenshots/test.png --frames 30
```

### Git branch: `feature/render-quality-improvements`
### Commits feitos: 8 commits desde o merge na main
