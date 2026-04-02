<!-- doc-meta
title: VFX Engine Duplication Audit
author: Claude Opus 4.6
date: 2026-04-02
tags: [vfx, audit, refactor, architecture]
-->

# VFX Engine Duplication Audit

O VFX system (PR #7) reimplementa funcionalidade que já existe no engine core.
Este documento lista todas as duplicações encontradas, organizadas por severidade.

---

## CRITICAL — Reimplementação direta de sistemas do engine

### D1. VFXMaterial::LoadTexture — reimplementa TextureLoader + MaterialManager

**Arquivo**: `engine/vfx/material/vfx_material.cpp:94`

```cpp
auto result = TextureLoader::Load(device, cmdList, spriteTexturePath, isSRGB);
```

O VFX chama `TextureLoader::Load` diretamente, gerenciando SRV index, sRGB flag,
e flipbook detection manualmente. O engine tem `MaterialManager` que faz tudo isso
automaticamente via `Material::SetTexturePath()`.

**Fix**: VFXMaterial herda MaterialInterface (já feito). Remover `LoadTexture()` e
usar `MaterialManager::LoadTexture(slot, path)`. A auto-detection de flipbook grid
vira um callback `OnTextureLoaded` que inspeciona o filename.

### D2. VFX file I/O — std::ifstream/ofstream em vez de VFS

**Arquivos**:
- `engine/vfx/asset/vfx_graph.cpp:185` — `std::ifstream` para ler .uzfx
- `demos/vfx_viewport/vfx_preset_serializer.cpp:48,301` — `std::ofstream`/`std::ifstream`

O engine tem `IVFS` (Virtual File System) com `LooseFileVFS` e `PakVFS`.
Usar filesystem diretamente ignora o VFS e quebra quando assets estão empacotados.

**Fix**: Substituir `std::ifstream` por `vfs->Read(path)` e `std::ofstream` por
`vfs->Write(path, data)`. O `VFXGraph::Load` deve aceitar um `IVFS*`.

### D3. DecalManager usa std::mt19937 — engine tem PCGRandom

**Arquivo**: `engine/vfx/decals/decal_manager.h:284`

```cpp
std::mt19937 rng_;
```

O engine tem `PCGRandom` (PCG-XSH-RR-64/32) que é mais rápido e tem melhor
qualidade estatística que mt19937 para jogos. O VFX decal manager cria o seu
próprio `std::mt19937` com `std::random_device` seed.

**Fix**: Substituir `std::mt19937 rng_` por `PCGRandom rng_` e adaptar as calls.

---

## MAJOR — Padrões não-standard que deviam seguir o engine

### D4. VFXEventQueue — engine tem EventBus<T>

**Arquivo**: `engine/vfx/events/vfx_event.h`

O VFX define o seu próprio `VFXEventQueue` com `Enqueue`/`Drain` para eventos
como OnDeath, OnCollision, OnBurst. O engine tem `EventBus<T>` com
`Publish`/`Subscribe` type-safe.

**Avaliação**: A VFXEventQueue é GPU-readback-driven (os eventos vêm do compute
shader via readback buffer), então não pode usar o EventBus diretamente — os
eventos precisam ser drenados em batch do GPU. **Parcialmente justificado**.
O ideal seria que o VFXEventQueue publicasse no EventBus após o drain.

### D5. Zero UZ_PROFILE_SCOPE — 45 ficheiros .cpp sem profiling

**Todos os 45 ficheiros .cpp** do VFX system não têm nenhum `UZ_PROFILE_SCOPE`.
O engine usa Tracy para profiling e a macro `UZ_PROFILE_SCOPE("name")` é
obrigatória em hot paths. Sem profiling, não há visibilidade de performance
no Tracy profiler.

**Fix**: Adicionar `UZ_PROFILE_SCOPE` em:
- `GPUParticleBuffer::Allocate/Free`
- `VFXSystem::Update/Render`
- `ModuleCompositor::ComposeUpdateShader/ComposeSpawnShader`
- `GPUParticleSort::Execute`
- `DecalManager::SpawnDecal/Update`
- `TrailMeshBuilder::BuildMesh`
- `VFXGraph::Load/LoadFromString`
- `VFXColorRamp::BakeToRGBA16F`

### D6. VFX JSON parsing — deve usar engine asset pipeline

**Arquivos**: `vfx_graph.cpp`, `vfx_composite.cpp`

O VFX faz `json::parse()` diretamente. O engine tem um asset import pipeline
com `AssetImporter` → `AssetCache` → `.uztexture`/`.uzmesh` etc. O formato
`.uzfx` deveria ter um `VFXImporter` registrado no `AssetImporterRegistry`
para beneficiar de:
- Cache de importação (não re-parseia se não mudou)
- VFS integration
- Depgraph para hot-reload

**Fix (futuro)**: Criar `VFXImporter : public IAssetImporter` e registrar
no `AssetImporterRegistry`. Para já, pelo menos trocar `std::ifstream` por VFS.

### D7. Multiple UZ_DEFINE_LOG_CATEGORY(VFX) locations

**Arquivo**: `engine/vfx/core/gpu_particle_buffer.cpp:12`

```cpp
UZ_DEFINE_LOG_CATEGORY(VFX, Trace);
```

Só pode haver UM `UZ_DEFINE_LOG_CATEGORY(VFX)` em todo o engine. Se outro
ficheiro também definir, causa duplicate symbol. Deve haver exatamente um
`.cpp` com o DEFINE e todos os outros usam `UZ_DECLARE_LOG_CATEGORY(VFX)`.

**Fix**: Verificar que apenas `gpu_particle_buffer.cpp` (ou um `vfx_system.cpp`)
tem o DEFINE. Todos os outros devem usar DECLARE.

---

## MINOR — Convenções não seguidas

### D8. Demo usa TextureLoader::Load diretamente

**Arquivo**: `demos/vfx_viewport/vfx_viewport_demo.cpp:365`

O demo carrega texturas (albedo, normal, roughness) via `TextureLoader::Load`
em vez de criar `Material` objects e deixar o `MaterialManager` gerenciar.

**Avaliação**: Aceitável para um demo standalone. O demo é código de teste,
não engine code. Baixa prioridade.

### D9. VFX Config structs — podem ser ECS components futuramente

**Arquivos**: `VFXEmitterConfig`, `CollisionModuleConfig`, `MeshRenderModuleConfig`, etc.

Estas structs são configuração estática. No padrão Unreal/Godot, seriam
`UNiagaraEmitter` components no ECS. No UzEngine, `VfxEmitterComponent` já
existe em `engine/ecs/environment/` — as configs deviam fluir daí.

**Fix (futuro)**: Unificar `VFXEmitterConfig` com `VfxEmitterComponent` properties.

### D10. Color ramp baking — custom implementation

**Arquivo**: `engine/vfx/curves/vfx_color_ramp.cpp`

Implementa gradient stops → RGBA16F LUT baking. O engine não tem um sistema
de gradient/LUT genérico equivalente, então esta implementação é **justificada**.
Não é duplicação — é funcionalidade nova.

---

## Resumo de Ação

| ID | Severidade | Ação | Esforço |
|----|-----------|-------|---------|
| D1 | CRITICAL | Remover LoadTexture, usar MaterialManager | 1h |
| D2 | CRITICAL | std::ifstream → VFS | 30min |
| D3 | CRITICAL | std::mt19937 → PCGRandom | 15min |
| D4 | MAJOR | VFXEventQueue publish no EventBus após drain | 1h |
| D5 | MAJOR | Adicionar UZ_PROFILE_SCOPE em 45 ficheiros | 2h |
| D6 | MAJOR | VFXImporter no AssetImporterRegistry | 4h (futuro) |
| D7 | MAJOR | Garantir single DEFINE para LogVFX | 15min |
| D8 | MINOR | Demo OK como está | - |
| D9 | MINOR | Unificar com ECS components (futuro) | 8h (futuro) |
| D10 | - | Justificado, não é duplicação | - |
