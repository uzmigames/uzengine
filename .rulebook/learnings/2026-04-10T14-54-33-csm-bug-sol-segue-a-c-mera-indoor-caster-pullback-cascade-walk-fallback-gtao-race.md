# CSM bug: "sol segue a câmera" indoor — caster pullback + cascade walk fallback + GTAO race
**Source**: manual
**Date**: 2026-04-10
**Tags**: rendering, csm, shadows, gtao, bisect, sponza
Sessão 2026-04-10: debug de "a iluminação muda com o ângulo da câmera no Sponza indoor". Três bugs empilhados, cada um mascarando o próximo.

## Bug #1 (descoberto primeiro, causa secundária): GTAO timing race
- `cv_GTAOEnabled` CVar default = false
- `GTAOSettings::enabled` struct default = true ← mismatch
- `frame_constants.cpp` e `frame_ao_passes.cpp` liam `settings.enabled` ANTES de `GTAOPass::Execute()` sincronizar do CVar
- GTAO rodava apesar de `r.GTAO=0`. Screen-space AO orbitava a câmera ("esfera de luz na câmera")

**Fix**: `GTAOSettings::enabled` default `false`, método `GTAOPass::IsEnabledCVar()` lendo CVar direto, 3 call sites atualizados.

## Bug #2 (descoberto por bisect shader): CSM cascade miss fallback errado
- `SampleShadowCascade` retornava `1.0` (lit) quando pixel saía do frustum da cascade
- Deveria tentar a próxima cascade maior ou retornar sombra
- `CascadeShadow` só amostrava UMA cascade (picked by depth)

**Fix**: sentinela `-1.0` em miss + walk outward pelas cascadas até encontrar uma que cobre (ambas variantes: `shadow.hlsli` e `shadow_pcss.hlsli`)

## Bug #3 (causa raiz principal): CSM caster pullback insuficiente
- Em `shadow_cascade.cpp:294`, `lightEye = center - lightDirNorm * radius` e `orthoFar = 2*radius`
- Shadow rendering volume = apenas a bounding sphere do frustum da câmera
- Paredes externas do Sponza (acima/atrás do frustum) ficavam FORA do volume → não rasterizadas no shadow atlas → sol "vazava" através delas para o interior
- Ao girar câmera, bounding sphere se movia, set de casters incluídos mudava → indoor flickering entre iluminado e em sombra

**Fix**: `kCasterPullbackScale = 8.0f`, `lightEye = center - lightDirNorm * (radius * 8)`, `orthoFar = pullback + radius`. Volume agora captura casters até 8x a distância atrás do frustum.

## Processo de bisect que funcionou
Editei temporariamente o shader `deferred_lighting_ps.hlsl` para forçar `output.hdr = float3(shadow, shadow, shadow)` (pure shadow mask grayscale). Isso imediatamente revelou que:
1. O shadow mask era preto (estável) quando olhava para cima (cascades cobriam os oclusores)
2. O shadow mask virava branco (lit) em bordas ao girar (cascades NÃO cobriam os oclusores)

Sem esse bisect visual eu não teria achado — `--view-mode` só está wired em demo.exe, não em uzengine.exe.

## Lições aplicáveis
1. **CSM sempre precisa de caster pullback substancial** — nunca `pullback = radius`. Padrão UE5 é derivar da scene AABB. Fallback razoável: 8-16x radius.
2. **Shader cascade lookup deve walk outward on miss**, nunca retornar "fully lit" silenciosamente. Caso contrário qualquer pixel fora do frustum "primário" vaza.
3. **Struct defaults DEVEM bater com CVar defaults** para qualquer campo read-early-write-late. Prefixa com método `IsFooCVar()` que lê o CVar direto se houver race possível.
4. **Bugs se empilham em rendering**: o GTAO mascarou o cascade walk, que mascarou o caster pullback. Fix em camada — cada fix revela o próximo bug.
5. **Regra do projeto `rendering-diagnostics.md`**: `--view-mode shadows` não funciona em uzengine.exe, só em demo.exe. Para bisect de shader em uzengine.exe, editar temporariamente o output do shader é válido — mas sempre começa por `--diag` se disponível.

## Arquivos tocados
- `runtime/engine/renderer/passes/postprocess/gtao_pass.h` (+`.cpp`) — default false + IsEnabledCVar
- `runtime/engine/renderer/frame/{frame_constants,frame_ao_passes,frame_lighting_pass}.cpp` — IsEnabledCVar gating
- `runtime/shaders/runtime/lighting/shadow.hlsli` — sentinela + cascade walk
- `runtime/shaders/runtime/lighting/shadow_pcss.hlsli` — sentinela + cascade walk
- `runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl` — H3 fix (diffuse multi-scatter NoV=1)
- `runtime/engine/renderer/shadows/shadow_cascade.cpp` — caster pullback 8x
- `runtime/engine/app/project_loader.{h,cpp}` + `uzengine.cpp` — infra bloco `cvars` em `.uzproject` (usado para bisect rápido)
- `runtime/schemas/uzproject.schema.json` — schema do bloco cvars
- `runtime/tests/engine/app/test_project_loader.cpp` — testes do parser cvars