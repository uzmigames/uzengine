<!-- doc-meta
title: Review PR #7 — GPU VFX Particle System (Niagara Parity)
author: Claude Opus 4.6
date: 2026-04-01
tags: [review, vfx, particles, gpu, architecture]
-->

# Review PR #7 — GPU VFX Particle System (Niagara Parity)

**PR**: uzmigames/uzengine#7
**Autor**: ShadoWzk1 (Guilherme Souza)
**Branch**: `feat/vfx-system` → `main`
**Tamanho**: +178.853 / -88.889 linhas, 510 arquivos
**Data da revisão**: 2026-04-01

---

## Resumo Executivo

O PR implementa um sistema de partículas GPU com arquitetura inspirada no Niagara
(Unreal Engine 5). A infraestrutura C++ é bem projetada — abstração RHI limpa,
integração correta com o render graph, sistema de budget/LOD/significance de nível
production, e um compositor de módulos extensível. No entanto, o PR **não está pronto
para merge** por razões fundamentais: todos os compute shaders são stubs vazios
(o GPU não faz nada), o sistema de coordenadas está errado (Y-up em engine Z-up),
as unidades são metros em vez de centímetros, e o branch está muito atrás do `main`
com conflitos garantidos em shaders e constant buffers.

**Veredito: REWORK NEEDED**

---

## Índice

1. [Problemas Críticos (CRITICAL)](#1-problemas-críticos)
2. [Problemas Maiores (MAJOR)](#2-problemas-maiores)
3. [Problemas Menores (MINOR)](#3-problemas-menores)
4. [Pontos Positivos (GOOD)](#4-pontos-positivos)
5. [Comparação AAA (Godot / Unreal)](#5-comparação-aaa)
6. [Recomendação Final](#6-recomendação-final)

---

## 1. Problemas Críticos

Problemas que **impedem o merge** — o sistema não funciona ou viola regras fundamentais
do engine.

### C1. Todos os compute shaders VFX são stubs vazios

**Severidade**: BLOCKER — o sistema inteiro é não-funcional no GPU.

Todos os cinco compute shaders que compõem o pipeline de simulação GPU são arquivos
de uma linha com corpo vazio:

```hlsl
// particle_spawn_cs.hlsl, particle_update_cs.hlsl, particle_sort_cs.hlsl,
// particle_indirect_args_cs.hlsl, particle_mesh_instance_cs.hlsl
[numthreads(8,8,1)] void main(uint3 DTid:SV_DispatchThreadID){}
```

Os vertex e pixel shaders também são triviais: `particle_billboard_vs.hlsl` gera
um triângulo fullscreen sem dados de partícula, `particle_billboard_ps.hlsl` retorna
preto `float4(0,0,0,1)`. O mesmo para `particle_mesh_vs.hlsl`, `particle_mesh_ps.hlsl`,
`trail_vs.hlsl` e `trail_ps.hlsl`.

**Consequência**: Cada dispatch que a infraestrutura C++ emite não faz nada. Nenhuma
partícula é spawned, movida, sorted ou renderizada. O sistema VFX inteiro é
silenciosamente não-funcional.

**Arquivos afetados**:
- `runtime/shaders/runtime/vfx/particle_spawn_cs.hlsl`
- `runtime/shaders/runtime/vfx/particle_update_cs.hlsl`
- `runtime/shaders/runtime/vfx/particle_sort_cs.hlsl`
- `runtime/shaders/runtime/vfx/particle_indirect_args_cs.hlsl`
- `runtime/shaders/runtime/vfx/particle_mesh_instance_cs.hlsl`
- `runtime/shaders/runtime/vfx/particle_billboard_vs.hlsl`
- `runtime/shaders/runtime/vfx/particle_billboard_ps.hlsl`
- `runtime/shaders/runtime/vfx/trail_vs.hlsl`
- `runtime/shaders/runtime/vfx/trail_ps.hlsl`

**Regra violada**: CLAUDE.md Regra 4 — "No stubs, TODOs, placeholders. No empty
function bodies."

**Correção**: Implementar a lógica real em cada compute shader. O spawn CS deve
alocar do free list, inicializar atributos, e incrementar o alive counter. O update
CS deve integrar velocidade, aplicar gravidade/drag, e avançar idade. O sort CS deve
gerar depth keys e executar bitonic merge. O indirect args CS deve escrever structs
`DrawIndexedIndirect` e `DispatchIndirect`.

---

### C2. Module compositor gera entry point `CSMain` — vai falhar na compilação

**Severidade**: BLOCKER — qualquer shader composto falha silenciosamente.

`runtime/engine/vfx/modules/module_compositor.cpp`, linhas 54-56:
```cpp
constexpr const char* kUpdateDispatchHeader =
    "[numthreads(256, 1, 1)]\n"
    "void CSMain(uint3 tid : SV_DispatchThreadID) {\n";
```

E linhas 61-63:
```cpp
constexpr const char* kSpawnDispatchHeader =
    "[numthreads(256, 1, 1)]\n"
    "void CSMain(uint3 tid : SV_DispatchThreadID) {\n";
```

O `ShaderManager` do UzEngine compila todo HLSL com `entryPoint = "main"`. Qualquer
shader emitido pelo compositor vai falhar na compilação DXC com "error: missing entry
point definition 'main'" porque a função se chama `CSMain`. A falha é **silenciosa**
em runtime — o PSO não é criado e o dispatch não produz trabalho no GPU.

**Regra violada**: `.claude/rules/shader-entry-point.md` — "Shader entry point is
ALWAYS `main` — never PSMain/VSMain/CSMain."

**Correção**: Trocar `"void CSMain(..."` por `"void main(..."` nas linhas 56 e 63
de `module_compositor.cpp`. Atualizar o comentário em `module_compositor.h:55`.

---

### C3. Shaders compostos são gerados mas nunca compilados em PSO

**Severidade**: BLOCKER — o sistema de módulos é dead code.

`runtime/engine/renderer/passes/vfx/particle_simulation_pass.cpp`, linhas 355-361:
```cpp
// TODO(3.61.0): Compile composed HLSL via DXC runtime compilation when available.
// Once DXC is integrated, the composed HLSL should be compiled into a PSO here
// and stored in the EmitterModuleStack (or a PipelineLibrary cache keyed by
// composedCacheKey). Until then, the pass falls through to the hardcoded
// pipelines in ParticleSimulationPassDesc.
```

`ComposeEmitterShaders()` gera strings HLSL via o `ModuleCompositor` e armazena em
`EmitterModuleStack::composedUpdateHLSL` e `composedSpawnHLSL`. Essas strings **nunca
são passadas para um shader compiler**. O pass sempre cai para os stubs vazios (C1).

Combinando C1, C2 e C3:
- Emitters sem módulos → dispatch dos stubs vazios → nada acontece
- Emitters com módulos → HLSL composto com entry point errado (C2) → nunca compilado
  (C3) → cai para os mesmos stubs vazios → nada acontece

**Regra violada**: CLAUDE.md Regra 4 — "No TODOs — if it needs to be done, do it NOW."

**Correção**: Integrar compilação DXC runtime. Quando `ComposeEmitterShaders()` produz
novo HLSL, compilar via `ShaderManager::CompileFromSource()`, criar compute PSO, e
armazenar no `EmitterModuleStack` com key `composedCacheKey`.

---

### C4. Gravidade Y-down em engine Z-up

**Severidade**: BLOCKER — partículas caem na direção errada.

O UzEngine é **Z-up, X-forward, left-handed** com unidades em **centímetros**. Três
locais hardcodam gravidade Y-down:

`runtime/engine/vfx/components/particle_emitter.h:86`:
```cpp
Vec3 spawnVelocity = {0.0f, 10.0f, 0.0f};  // Y-up: errado
```

`runtime/engine/vfx/components/particle_emitter.h:102`:
```cpp
Vec3 gravity = {0.0f, -9.81f, 0.0f};  // Y-down: errado
```

`runtime/engine/vfx/modules/update_module.h:54`:
```cpp
Vec3 gravity{0.0f, -9.81f, 0.0f};  // Y-down: errado
```

A tabela em `update_module.h:29` documenta o default errado:
```
/// | gravity | Vec3 | (0, -9.81, 0) | Constant acceleration (m/s^2) |
```

O JSON de exemplo em `vfx_graph.h:17` também:
```json
{ "type": "update", "config": { "gravity": [0,-9.81,0] } }
```

**Consequência**: Qualquer emitter colocado numa cena Z-up vai spawnar partículas
que voam ao longo do eixo Y e caem ao longo de Y. Como a geometria, câmera e física
(Jolt) do engine são Z-up, o comportamento visual é completamente errado.

**Regra violada**: CLAUDE.md architecture.md — "Z-up, X-forward, left-handed. NEVER
use Y-up when porting from external sources."

**Correção**:
- `particle_emitter.h:86` → `Vec3 spawnVelocity = {0.0f, 0.0f, 1000.0f};` (10 m/s em cm)
- `particle_emitter.h:102` → `Vec3 gravity = {0.0f, 0.0f, -981.0f};` (cm/s²)
- `update_module.h:54` → `Vec3 gravity{0.0f, 0.0f, -981.0f};`
- Atualizar tabela, JSON de exemplo, e todos os presets

---

### C5. PerFrameCB — 3 point lights inline conflitam com sistema existente

**Severidade**: BLOCKER em merge — conflito arquitetural com main.

`runtime/engine/renderer/core/params/constant_buffers.h:160-195`: O PR adiciona 38
floats individuais para 3 point lights diretamente no PerFrameCB:

```cpp
uint32_t numPointLights = 0;
float    pointLight0PosX = 0.0f;
float    pointLight0PosY = 0.0f;
// ... 36 mais campos ...
```

`runtime/shaders/runtime/geometry/mesh_ps.hlsl`: Adiciona `packoffset(c33-c39)` para
os point lights.

**Problemas**:
1. O branch `main` já tem sistema de multi-light via StructuredBuffer com clustered
   shading. Este PR adiciona 3 lights inline no CB — **conflito arquitetural direto**.
2. O padrão AAA (Unreal/Godot) usa StructuredBuffer com índices, não inline no CB.
3. Hardcodar 3 lights num CB de 768 bytes desperdiça espaço para cenas sem point
   lights e limita cenas que precisam de mais de 3.
4. O `mesh_ps.hlsl` no main é completamente diferente (PBR BRDF, IBL, tonemap) —
   merge vai ter **conflitos massivos**.

**Correção**: Remover os point lights inline do PerFrameCB. Usar o sistema de
StructuredBuffer que já existe no main. Se o VFX precisa de lit particles, ler os
lights do mesmo StructuredBuffer.

---

### C6. Mudanças não-VFX no PR — feature creep

**Severidade**: BLOCKER para merge limpo.

O PR modifica arquivos que não têm relação com VFX:

- **`runtime/shaders/editor/grid_ps.hlsl`**: Adiciona "screen-space ground reflection"
  ao grid editor. Modifica o `GridCB` layout (`_pad0` → `reflectionHdrSrv`), quebrando
  compatibilidade com o main.

- **`runtime/engine/ecs/environment/sky_atmosphere_component.h`**: Simplifica/altera
  defaults do componente que foi extensivamente trabalhado no main (Falcor parity).

- **`runtime/engine/ecs/environment/sky_light_component.h`**: Idem — defaults alterados
  que conflitam com o trabalho de parity do main.

- **`runtime/engine/rhi/dx12/dx12_agility.cpp`**: Modificação que conflita com main.

- **`runtime/engine/scripting/lua_vm.cpp`**: Mudanças no LuaVM não relacionadas com VFX.

**Correção**: Reverter todas as mudanças não-VFX. Cada feature deve ser um PR separado.

---

## 2. Problemas Maiores

Problemas que **devem ser corrigidos** mas não impedem a compilação.

### M1. Todas as unidades são metros — engine usa centímetros

O sistema VFX inteiro usa metros como unidade de distância:

| Local | Valor | Deveria ser |
|-------|-------|-------------|
| `vfx_lod.h:100` — LOD distances | `{50, 100, 200}` m | `{5000, 10000, 20000}` cm |
| `vfx_significance_manager.cpp:19` — distance falloff | `0.01` /m | `0.0001` /cm |
| `collision_module.h:80` — collision offset | `0.01` m (1cm) | `1.0` cm |
| `collision_module.h:94` — min bounce speed | `0.1` m/s | `10.0` cm/s |
| `update_module.h:54` — gravity | `9.81` m/s² | `981.0` cm/s² |
| Trails width/arc-length | metros | centímetros |

**Consequência prática**: LOD thresholds de 50/100/200 em cm correspondem a 0.5m/1m/2m
— partículas vão para LOD Culled quando a câmera está a 2 metros. O collision offset
de 0.01 cm (0.1mm) causa z-fighting persistente.

**Regra violada**: CLAUDE.md architecture.md — "Distances: Centimeters."

---

### M2. `decal_deferred_ps.hlsl` retorna magenta debug em produção

`runtime/shaders/runtime/vfx/decal_deferred_ps.hlsl:6-10`:
```hlsl
float4 main(float4 positionCS : SV_Position) : SV_Target {
    // For now, output a visible debug color so we can tell the path is active.
    return float4(1.0, 0.0, 1.0, 0.5); // Magenta debug output
}
```

Qualquer decal VFX vai renderizar um quad magenta semi-transparente.

**Regra violada**: CLAUDE.md Regra 4 — "No placeholders."

---

### M3. VFXCurve::BakeToTexture aloca textura GPU mas nunca faz upload

`runtime/engine/vfx/curves/vfx_curve.cpp:299`:
```cpp
// TODO(vfx): Upload sampled curve data to the texture.
// Currently the texture is created but not filled because the mock RHI
// device returns nullptr from MapBuffer.
bakedTexture_ = handle;
```

Módulos que fazem sample desta textura (size-over-life, color-over-life) vão ler
memória GPU não-inicializada — output visual indefinido sem erro.

---

### M4. VFX graph ignora toda a config dos módulos no JSON

`runtime/engine/vfx/asset/vfx_graph.cpp:75-77`:
```cpp
// TODO: Apply "config" object to module's public fields once the
// module-config deserialization system is implemented.
modules.push_back(std::move(mod));
```

Ao carregar um `.uzfx`, o parser cria cada módulo pelo nome mas **descarta** o objeto
`"config"` JSON. Todos os módulos ficam com defaults, independente do que o artista
definiu.

---

### M5. Thread group size inconsistente

| Fonte | Tamanho | Layout |
|-------|---------|--------|
| Stubs (.hlsl) | 64 | `[numthreads(8,8,1)]` — 2D layout para dados 1D |
| Compositor | 256 | `[numthreads(256,1,1)]` — 1D |
| Dispatch (C++) | 64 | `kParticleThreadGroupSize = 64` |
| Sort (C++) | 256 | `kSortGroupSize = 256` |

Quando os shaders reais forem implementados, o dispatch math vai produzir contagens
de groups erradas se o shader declarar 256 threads mas o C++ dividir por 64.

---

### M6. `vfx_viewport_demo.cpp` — 3757 linhas

Mais que o dobro do limite do engine (1500 linhas). Precisa ser dividido em ~3 arquivos.

Outros arquivos grandes:
- `vfx_viewport_vfx.cpp` — 1736 linhas (acima do limite)
- `vfx_editor_ui.cpp` — 1049 linhas (OK mas próximo)

---

### M7. Slang headers adicionados sem documentação

O PR adiciona `runtime/thirdparty/slang/` — dependência nova não registrada em
`scripts/setup.ps1` nem documentada no `thirdparty/README.md`.

**Regra violada**: `.claude/rules/thirdparty.md` — "never manually clone/download;
use scripts/setup.ps1"

---

### M8. Build artifacts commitados

- `build2/CMakeFiles/` — arquivos CMake de build
- `build_decal_chk/CMakeFiles/` — idem
- `.claude/agent-memory/` — memória interna de agentes AI

Estes diretórios não deviam estar no PR.

---

### M9. Sem integração com o sistema de materiais do engine

O VFX system define sua própria struct `VFXMaterial` (`engine/vfx/material/vfx_material.h`)
com campos fixos (albedo texture, emissive color, blend mode) que são completamente
isolados do sistema de materiais do engine (`PBRMaterial`, `MaterialBuffer`).

**O que falta**:

1. **Material Instance**: Partículas não suportam `MaterialInstance` — não é possível
   criar variações de um material base (ex: fogo com cores diferentes) sem duplicar
   a configuração inteira. No Unreal, cada emitter referencia um `MaterialInstanceDynamic`
   que herda do material base e permite override de parâmetros por emitter. No Godot,
   o `ParticleProcessMaterial` é um `Resource` que pode ser instanciado.

2. **Material Graph / Shader Graph**: O sistema VFX não se integra com nenhum sistema
   de material graph. O pixel shader (`particle_billboard_ps.hlsl`) tem lógica de
   shading hardcoded — não há como um artista criar materiais customizados para
   partículas (ex: dissolve, distortion, refraction, subsurface) sem editar HLSL
   manualmente. No Unreal, partículas usam o mesmo Material Editor que meshes. No
   Godot, `VisualShader` funciona tanto para meshes quanto para partículas.

3. **Material Slots nos VFX Nodes**: Os nodes/módulos do VFX (sprite renderer, mesh
   renderer, trail renderer) deveriam aceitar um `MaterialHandle` ou `MaterialSlot`
   do sistema de materiais unificado do engine, não uma struct `VFXMaterial` separada.
   Isso permitiria:
   - Reutilizar materiais PBR existentes em partículas
   - Material instancing com parameter overrides por emitter
   - Hot-reload de materiais afetar partículas automaticamente
   - Shader variants (lit/unlit/translucent/distortion) via o mesmo sistema

**Padrão AAA (Unreal Niagara)**:
- `UNiagaraMeshRendererProperties::OverrideMaterials` → array de `UMaterialInterface*`
- `UNiagaraSpriteRendererProperties::Material` → `UMaterialInterface*`
- Cada renderer module referencia materiais do sistema unificado
- `MaterialInstanceDynamic` permite override de parâmetros em runtime via Blueprints

**Padrão Godot**:
- `GPUParticles3D::draw_pass_N_material` → `Material` resource
- `ParticleProcessMaterial` para simulação, qualquer `ShaderMaterial` para rendering
- Material resources são instanciáveis e editáveis no inspector

**Correção recomendada**:
- Remover `VFXMaterial` como struct isolada
- Sprite/mesh/trail renderers devem aceitar `MaterialHandle` do sistema unificado
- Criar `VFXMaterialInstance` que herda do material base e adiciona overrides
  (particle color, age, custom data) como parâmetros dinâmicos
- O pixel shader VFX deve ser gerado pelo mesmo pipeline de material graph, com
  inputs extras (particle age, normalized lifetime, custom0-3, flipbook frame)

---

### M10. Flipbook sprite animation incompleto

O PR menciona flipbook sprite animation na descrição e possui um header
`vfx_flipbook.hlsli`, mas a implementação está incompleta:

1. **Sem SubUV blending**: O sistema calcula o frame index do flipbook mas não faz
   interpolação entre frames adjacentes. Engines AAA fazem blend linear entre o
   frame atual e o próximo para evitar o "pop" visual na transição:
   ```hlsl
   // Padrão Niagara — SubUV interpolation
   float4 colorA = SampleFlipbook(uv, frameIndex);
   float4 colorB = SampleFlipbook(uv, frameIndex + 1);
   float4 result = lerp(colorA, colorB, frameFraction);
   ```
   Sem isso, partículas com flipbook (fogo, fumaça, explosão) têm transições
   discretas visíveis — inaceitável para qualidade AAA.

2. **Sem auto-detect de grid layout**: O `VFXMaterial` tem campos `flipbookRows` e
   `flipbookCols` que precisam ser configurados manualmente. Engines modernos
   detectam o layout automaticamente a partir dos metadados da textura ou de uma
   naming convention (ex: `fire_4x4.png` → 4 rows, 4 cols).

3. **Sem motion vectors entre frames**: Para partículas com TAA ativo, flipbook
   sprites precisam gerar motion vectors corretos entre frames. Sem isso, o TAA
   vai causar ghosting nas transições de frame. O Unreal resolve isso com
   `SubUV_MotionBlur` que interpola UVs no velocity buffer.

4. **Sem random start frame**: Quando múltiplas partículas usam o mesmo flipbook,
   todas começam no frame 0 simultaneamente — criando um efeito de "marching ants"
   onde todas as partículas estão em sync. O padrão é `randomStartFrame` como
   opção no emitter para que cada partícula comece num frame aleatório.

5. **Sem integração com texture atlas**: O flipbook assume uma textura dedicada por
   emitter. Em produção AAA, múltiplos flipbooks são empacotados num texture atlas
   compartilhado para reduzir draw calls e descriptor usage. O sistema deveria
   suportar UV remapping via atlas metadata.

**Padrão Unreal Niagara**:
- `SubUVAnimation` module com blending, random offset, curve-driven playback
- `SubUV_MotionBlur` para TAA-compatible motion vectors
- `SubImage` system integrado com o texture atlas pipeline
- `USubUVAnimation` asset com import settings (rows, cols, mode)

**Padrão Godot**:
- `ParticleProcessMaterial::anim_speed_*` — velocidade controlável por curve
- `ParticleProcessMaterial::anim_offset_*` — random start offset
- Blending entre frames via material shader

**Correção recomendada**:
- Implementar SubUV blending no `particle_billboard_ps.hlsl` (lerp entre frames)
- Adicionar `randomStartFrame` como parâmetro do emitter
- Gerar motion vectors corretos para flipbook frames no velocity buffer
- Documentar o flipbook pipeline com `@ref` para o paper/técnica usada

---

## 3. Problemas Menores

### m1. Sprite render module documenta features como "planned" / "placeholder"

`sprite_render_module.h:39,81,84`: Usa "placeholder curve IDs" e "planned features"
na documentação — implica implementação incompleta.

### m2. Sub-emitter usa `Position.z < 0` como teste de colisão placeholder

`sub_emitter_module.h:25`: Hardcoda Z < 0 como "chão" — quebra para qualquer cena
onde o chão não está em Z=0.

### m3. `FormatDebugName` retorna pointer para buffer thread_local reutilizado

`gpu_particle_buffer.cpp:77-82`: O pointer para `buf[128]` estático pode ser
sobrescrito entre chamadas consecutivas. Nomes de debug no PIX/RenderDoc podem
mostrar o nome errado.

### m4. Demo `vfx_showcase` é um executável no-op

`demos/vfx_showcase/main.cpp:19-24`: Imprime uma mensagem e sai. Não é um teste
visual funcional.

---

## 4. Pontos Positivos

O PR demonstra qualidade em várias áreas:

### ✅ Arquitetura VFX bem projetada
- Separação correta: `engine/vfx/` com módulos independentes (spawn, update, sort,
  render, trails, curves, budget, lod, asset)
- Padrão Niagara correto: module compositor, module stack UI, extensibilidade
- SoA ping-pong double-buffer para GPU particles — padrão da indústria
- Pipeline compute correto: spawn → update → sort → indirect args → render

### ✅ Bindless SM6.6 rendering
- Billboard expansion via SV_VertexID sem Input Assembler
- Trail vertex fetch via StructuredBuffer bindless
- Padrão moderno e correto para DX12

### ✅ Zero DX12 direto no código VFX
- Todo o código VFX usa a abstração RHI corretamente
- Nenhuma referência a `ID3D12*`, `ComPtr<>`, `D3D12_*` ou `DXGI_*`
- Boa separação de responsabilidades

### ✅ Entry points corretos nos shaders
- **Todos** os shaders `.hlsl` usam `main` como entry point (verificado em 19 arquivos)
- Citations `@ref` presentes (Bridson 2007 curl noise, Karis 2013 attenuation)

### ✅ Sistema de budget/LOD/significance production-grade
- `VFXSignificanceManager` com distance + visibility scoring
- `VFXLodEvaluator` com 4 tiers (Full/Reduced/Minimal/Culled)
- Screen-space pixel coverage para LOD decisions
- Budget enforcement com ranking e culling

### ✅ Render graph integration correta
- `frame_particles.cpp` integra-se corretamente no render graph existente
- Barriers e resource states geridos pelo graph, não manualmente
- First-frame edge case tratado corretamente no ping-pong

### ✅ GPU readback ring buffer para sub-emitters
- Triple-buffered readback para eventos CPU-side
- Padrão correto para async GPU→CPU communication

### ✅ Feature set completo (na infraestrutura C++)
- 8 spawn shapes (point, sphere, box, ring, cone, cylinder, disc, line)
- Curl noise, vortex, wind forces
- Ground collision com bounce/friction
- Kill volumes (AABB containment)
- Sub-emitter delayed burst
- Frustum culling
- Color/size over lifetime com GPU LUT ramps
- Trail/ribbon system
- Editor UI com presets JSON
- Lua bindings para scripting

---

## 5. Comparação AAA

### vs Unreal Engine 5 — Niagara

| Feature | Niagara | PR #7 |
|---------|---------|-------|
| GPU spawn/update pipeline | Implementação completa | Stubs vazios (C1) |
| Module composition → PSO | Visual graph + hot-reload | HLSL gerado, nunca compilado (C3) |
| Bitonic depth sort | Sim | Infra only, shader stub |
| LOD com significance | Completo (distance + screen) | Infra completa, unidades erradas (M1) |
| GPU readback para sub-emitters | Async ring | Triple-buffered, correto ✅ |
| Ribbon trails | Completo | Sistema existe, shader stub |
| Mesh particles | Instanced mesh | Buffer infra only, shader stub |
| Distortion pass | GBuffer distort completo | Pass infra, shader stub |
| Curve-over-life (GPU) | LUT sampling | Criada mas não uploaded (M3) |
| Collision response | Depth + SDF | Módulo + HLSL snippet, nunca dispatched |
| Coordinate system | Z-up, centímetros | Y-up, metros ❌ |
| Light integration | StructuredBuffer clustered | 3 lights inline no CB ❌ |
| **Material system** | Material Editor unificado | VFXMaterial isolado ❌ (M9) |
| **Material instances** | MaterialInstanceDynamic | Não suportado ❌ (M9) |
| **Material graph** | Mesmo node graph p/ mesh e VFX | Não integrado ❌ (M9) |
| **Flipbook SubUV blend** | SubUVAnimation + MotionBlur | Sem blending ❌ (M10) |
| **Flipbook random start** | Sim, com curve offset | Não suportado ❌ (M10) |
| **Flipbook motion vectors** | SubUV_MotionBlur (TAA) | Não suportado ❌ (M10) |

**Resumo Niagara**: A arquitetura C++ do PR é comparável ao Niagara em design. O que
falta é a implementação GPU (shaders) e a integração com o pipeline de lighting existente.

### vs Godot 4 — GPUParticles3D

| Feature | Godot GPUParticles3D | PR #7 |
|---------|---------------------|-------|
| GPU simulation | GLSL compute (completo) | Stubs vazios |
| Material system | ShaderMaterial unificado | VFXMaterial isolado ❌ |
| Material instances | Resource instancing | Não suportado ❌ |
| Spawn shapes | Limitado (sphere, box) | 8 shapes ✅ (melhor que Godot) |
| LOD | Distance fade simples | LOD + significance ✅ (melhor) |
| Sub-emitters | Sim | Sim, com GPU readback ✅ |
| Trail/ribbon | Trail material node | Trail system ✅ |
| Curl noise | Via noise texture | Bridson analytical ✅ |
| Flipbook blending | anim_speed + anim_offset | Sem blending ❌ |
| Coordinate system | Z-up (correto para Godot) | Y-up ❌ |
| Units | Metros (correto para Godot) | Metros em engine cm ❌ |
| Bindless rendering | Não (bind-based) | SM6.6 bindless ✅ (melhor) |

**Resumo Godot**: O PR supera o Godot em features de infraestrutura (LOD, significance,
sub-emitter readback, bindless, spawn shapes). Mas o Godot tem shaders funcionais.

---

## 6. Recomendação Final

### Veredito: **REWORK NEEDED** — Não fazer merge.

### Caminho recomendado:

**Opção A — Merge da infra C++ como scaffold (preferida)**:
1. Criar um novo PR só com o código C++ VFX (`engine/vfx/`, `engine/renderer/passes/vfx/`,
   `engine/renderer/frame/frame_particles.cpp`)
2. Remover TODAS as mudanças não-VFX (grid, sky, cbuffer point lights, lua_vm, agility)
3. Remover build artifacts e agent memory
4. Fix Y-up → Z-up e metros → centímetros
5. Fix `CSMain` → `main` no compositor
6. Manter os stubs com um `UZ_LOG(VFX, Warning, "Shader not implemented")` em vez de
   corpo vazio — pelo menos o dev sabe que não funciona
7. PR de follow-up para implementar os shaders reais

**Opção B — Rework completo (melhor resultado final)**:
1. Rebase sobre `main` e resolver todos os conflitos
2. Implementar TODOS os compute/render shaders
3. Compilar shaders compostos em PSO (eliminar o TODO)
4. Integrar com o sistema de lighting existente (StructuredBuffer, não inline CB)
5. Fix coordenadas, unidades, entry points
6. Dividir arquivos grandes (>1500 linhas)
7. Eliminar todos os TODOs e placeholders
8. Documentar slang em setup.ps1
9. Integrar com o sistema de materiais unificado (MaterialHandle, MaterialInstance)
10. Implementar flipbook SubUV blending, random start frame, e motion vectors
11. Remover `VFXMaterial` isolado — renderers devem usar o material system do engine

### Estimativa de esforço:
- Opção A: ~2-3 dias (principalmente cleanup e rebase)
- Opção B: ~1-2 semanas (implementação completa dos shaders + integração)

### Nota para o autor:
A arquitetura C++ é genuinamente boa. O budget/significance manager, o module
compositor, o ping-pong buffer state tracking, e a integração com o render graph
são de nível production. O problema principal é que a parte GPU (shaders) ficou
como stubs. A recomendação é completar os shaders antes do merge — o trabalho
difícil (a arquitetura) já está feito.
