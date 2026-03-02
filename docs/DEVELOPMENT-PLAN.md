# UzEngine — Development Plan

Sequência ordenada de implementação. Cada etapa tem pré-requisitos claros e um resultado concreto e testável.

**Estado atual:** Phase 0 completo — DX12 pyramid + JPEG streaming + IPC/MCP funcionando.

---

## Como ler este documento

- **Etapas dentro de uma fase** podem ser paralelizadas entre agentes/devs
- **Fases são bloqueantes** — Phase N+1 só começa quando Phase N estiver completa
- Cada etapa tem um **resultado entregável** verificável
- `[task-id]` aponta para `.rulebook/tasks/{task-id}/`

---

## Phase 0-PRE — Design Antes de Codar
> Objetivo: Tomar decisões arquiteturais que, se deixadas para depois, custam semanas de refactor.

| # | Etapa | Entregável | Task |
|---|-------|-----------|------|
| 0.1 | Design do Render Graph | `docs/specs/rendering/render-graph.md` finalizado | `phase0-render-graph-design` |
| 0.2 | Design do Framework Data-Driven | `docs/specs/scripting/data-driven-config.md` finalizado | `phase0-data-driven-framework` |
| 0.3 | Design da Infraestrutura de Debug | `docs/specs/engine/debug-infrastructure.md` finalizado | `phase0-debug-infrastructure-design` |
| 0.4 | Design do Async Streaming | `docs/specs/scripting/async-streaming.md` finalizado | `phase0-async-streaming-design` |
| 0.5 | Design do Shared State | `docs/specs/scripting/shared-state.md` finalizado | `phase0-shared-state-design` |

**Saída:** 5 specs de arquitetura aprovados. Nenhum código escrito ainda.

---

## Phase 1 — Engine Playável no Editor
> Objetivo: Conseguir abrir uma cena no editor, rodar um script Lua, ver o resultado em tempo real.

### Bloco 1A — Fundação do Engine (sem isso nada funciona)

| # | Etapa | Entregável | Task |
|---|-------|-----------|------|
| 1.1 | Sistema de cenas (`.uzscene`) | SceneManager, node tree, serialização funcional | `phase1-scene-system` |
| 1.2 | Profiling e debug | Tracy integrado, asserts, overlay ImGui | `phase1-debug-profiling` |
| 1.3 | Crash reporting | SEH handler, minidump, JSON de crash | `phase1-crash-reporting` |
| 1.4 | Render Graph | RgPass/RgTexture, barriers automáticos, PSO cache | `phase1-render-graph` |

### Bloco 1B — Editor (roda em paralelo com 1A após 1.1)

| # | Etapa | Entregável | Task |
|---|-------|-----------|------|
| 1.5 | Painéis do editor | Scene Tree, Inspector, Asset Browser, Play Controls | `phase1-editor-panels` |
| 1.6 | Asset browser | Visualização e importação de assets | `phase1-asset-browser` |
| 1.7 | Gizmos 3D | Translate/Rotate/Scale com ray cast | `phase1-editor-gizmos` |
| 1.8 | Multi-seleção | SelectionManager, box-select, bulk ops | `phase1-multi-select` |
| 1.9 | Undo/Redo | UndoStack, Ctrl+Z/Shift+Z | `phase1-undo-redo` |
| 1.10 | Console de debug | `~` abre console, comandos Lua + C++ | `phase1-console-commands` |

### Bloco 1C — Scripting (roda em paralelo com 1B, depende de 1.1)

| # | Etapa | Entregável | Task |
|---|-------|-----------|------|
| 1.11 | LuaJIT scripting | `on_start`/`on_update`/`on_stop`, sandbox, hot-reload | `phase1-lua-scripting` |
| 1.12 | Coroutines Lua | `ctx.coroutine.wait()`, scheduler integrado | `phase1-lua-coroutines` |
| 1.13 | Module system Lua | `require()` controlado, path resolution | `phase1-lua-module-system` |
| 1.14 | Shared state | `ctx.shared`, change events, serialização | `phase1-shared-state` |
| 1.15 | Timers | `ctx:schedule_after/every/cancel` | `phase1-timer-system` |
| 1.16 | DAP debugger | Breakpoints Lua no VSCode, watches, stack trace | `phase1-lua-dap-server` |

### Bloco 1D — ECS e Gameplay Core (depende de 1.1 e 1.11)

| # | Etapa | Entregável | Task |
|---|-------|-----------|------|
| 1.17 | System scheduler | TickGroups, prioridades, dependências entre systems | `phase1-ecs-system-scheduler` |
| 1.18 | Spatial queries (BVH) | `ctx.world:query_sphere/raycast`, O(log n) | `phase1-ecs-spatial-query` |
| 1.19 | Entity templates | `.uzentity` format, instanciação com overrides | `phase1-entity-templates` |

### Bloco 1E — Editor de Qualidade (depende de 1B completo)

| # | Etapa | Entregável | Task |
|---|-------|-----------|------|
| 1.20 | Shader hot-reload | DXC, FileWatcher, erro em VSCode | `phase1-shader-hot-reload` |
| 1.21 | Play mode | Processo separado, start/stop/pause | `phase1-play-mode` |
| 1.22 | Gamepad navigation UI | FocusManager, navegação por d-pad | `phase1-ui-gamepad-navigation` |
| 1.23 | Widget library | `<uz-button>`, `<uz-slider>`, `<uz-list-view>`, etc. | `phase1-ui-widget-library` |

**Saída da Phase 1:** Editor funcional no VSCode. Abrir `.uzscene`, editar nodes, rodar script Lua, ver resultado ao vivo.

---

## Phase 2 — Engine Visual de Qualidade
> Objetivo: Rendering PBR correto, assets importados de ferramentas externas, audio funcional.

### Bloco 2A — Rendering PBR (depende de 1.4 Render Graph)

| # | Etapa | Entregável | Task |
|---|-------|-----------|------|
| 2.1 | PBR Renderer | G-Buffer, GGX BRDF, IBL, deferred lighting | `phase2-pbr-renderer` |
| 2.2 | Shadow maps | CSM 3 cascatas, PCF 3×3 | `phase2-shadow-maps` |
| 2.3 | Point/spot shadows | Dual-paraboloid (point), perspective (spot), atlas | `phase2-point-spot-shadows` |
| 2.4 | SSAO | HBAO+ compute, bilateral blur | `phase2-ssao` |
| 2.5 | Post-process stack | Bloom, tone mapping, auto exposure, vignette, LUT | `phase2-render-postprocess` |
| 2.6 | Deferred decals | DBuffer, projeção em box, bullet holes/sujeira | `phase2-deferred-decals` |
| 2.7 | GPU instancing | BatchBuilder, StructuredBuffer, SV_InstanceID | `phase2-gpu-instancing` |
| 2.8 | Shader variants | Permutation system, PSO cache, DXC offline | `phase2-shader-variants` |
| 2.9 | Shader library | HLSL includes: BRDF, lighting, shadows, skinning | `phase2-shader-library` |

### Bloco 2B — Asset Pipeline (pode rodar em paralelo com 2A)

| # | Etapa | Entregável | Task |
|---|-------|-----------|------|
| 2.10 | glTF import | fastgltf, meshoptimizer LODs, MeshBuffer | `phase2-gltf-import` |
| 2.11 | FBX import | ufbx, `.uzmesh`, preview no editor | `phase2-fbx-import` |
| 2.12 | Texture tools | BC7/BC5/BC6H via DirectXTex, preview | `phase2-texture-tools` |
| 2.13 | Asset PAK | `.uzpak` com zstd, VFS, CLI packer | `phase2-asset-pak` |
| 2.14 | Source asset DB | SQLite, CookKey, dependency graph, incremental import | `phase2-source-asset-db` |

### Bloco 2C — Audio (pode rodar em paralelo com 2A)

| # | Etapa | Entregável | Task |
|---|-------|-----------|------|
| 2.15 | XAudio2 engine | 4 buses, voice pool, WAV/OGG, CAudioSource/Listener | `phase2-audio-system` |
| 2.16 | Audio event system | `.uzevent`, `ctx.audio:trigger()`, variações aleatórias | `phase2-audio-event-system` |
| 2.17 | DSP chains por bus | Reverb, EQ, compressor via XAPO | `phase2-audio-dsp-chains` |

### Bloco 2D — UI e Gameplay (pode rodar em paralelo com 2A)

| # | Etapa | Entregável | Task |
|---|-------|-----------|------|
| 2.18 | UI system | uz_ui_html.dll, HTML+CSS game UI, data binding | `phase2-ui-system` |
| 2.19 | UI animation | `ctx.ui:tween/sequence`, easing, page transitions | `phase2-ui-animation` |
| 2.20 | Prefab edit mode | Viewport isolado, breadcrumb, override panel | `phase2-prefab-edit-mode` |
| 2.21 | Physics materials | `.uzphysmat`, friction/restitution reutilizáveis | `phase2-physics-materials` |
| 2.22 | Gameplay ability system | AbilitySpec, cooldowns, costs, EventBus integration | `phase2-gameplay-ability-system` |
| 2.23 | Behavior trees | `.uzbt`, Sequence/Selector, Blackboard, tick loop | `phase2-behavior-trees` |
| 2.24 | AI native features | MCP: scene_generate, shader_assist, script_generate | `phase2-ai-native-features` |

**Saída da Phase 2:** Jogo simples jogável com PBR correto, assets importados do Blender, audio, e UI.

---

## Phase 3 — Engine de Jogo Completo
> Objetivo: Personagens animados, física completa, gameplay systems avançados, audio dinâmico.

### Bloco 3A — Personagens (sequencial: mesh → anim → ragdoll)

| # | Etapa | Entregável | Task |
|---|-------|-----------|------|
| 3.1 | Skeletal mesh | GPU skinning, morph targets, physics asset binding | `phase3-skeletal-mesh` |
| 3.2 | Animation system | State machine, blending, AnimNotify, IK, root motion | `phase3-animation-system` |
| 3.3 | Ragdoll | Jolt ragdoll, active ragdoll motors, blend in/out | `phase3-ragdoll` |

### Bloco 3B — Física Completa (pode rodar em paralelo com 3A)

| # | Etapa | Entregável | Task |
|---|-------|-----------|------|
| 3.4 | Physics system | Jolt rigid bodies, triggers, CharacterController, CCD | `phase3-physics-system` |
| 3.5 | Vehicle physics | VehicleConstraint, suspensão, drivetrain, Ackermann | `phase3-vehicle-physics` |

### Bloco 3C — Rendering Avançado (pode rodar em paralelo, depende de 2A)

| # | Etapa | Entregável | Task |
|---|-------|-----------|------|
| 3.6 | SSR | Hi-Z ray march, reprojection temporal, IBL fallback | `phase3-ssr` |
| 3.7 | TAA | Temporal resolve, Halton jitter, variance clamping | `phase3-taa` |
| 3.8 | Virtual Shadow Maps | VSM atlas, page table, feedback buffer | `phase3-virtual-shadow-maps` |
| 3.9 | Global Illumination | DDGI probes, lightmaps offline, reflection captures | `phase3-global-illumination` |
| 3.10 | Particle system | GPU compute, `.uzfx` format, AnimNotify integration | `phase3-particle-system` |

### Bloco 3D — Gameplay Systems (pode rodar em paralelo)

| # | Etapa | Entregável | Task |
|---|-------|-----------|------|
| 3.11 | Input system | Enhanced Input, XInput, runtime rebinding | `phase3-input-system` |
| 3.12 | Camera system | SpringArm, modos, shake, DoF, `.uzcam` | `phase3-camera-system` |
| 3.13 | Event bus | C++ EventBus<T>, Lua signals, engine events | `phase3-event-bus` |
| 3.14 | Tags & groups | CTags bitset<64>, find_by_tag, Lua API | `phase3-tags-groups` |
| 3.15 | Save system | Msgpack, slots, autosave, thumbnails, Lua API | `phase3-save-system` |
| 3.16 | Localization | CSV i18n, runtime locale switch, font fallback | `phase3-localization` |
| 3.17 | World streaming | Chunks de cena, streaming volumes, memory budget | `phase3-world-streaming` |

### Bloco 3E — Audio Avançado (pode rodar em paralelo)

| # | Etapa | Entregável | Task |
|---|-------|-----------|------|
| 3.18 | Adaptive music | Stems sincronizadas, crossfade, transições quantizadas | `phase3-adaptive-music` |
| 3.19 | Audio occlusion | Raycast-based LPF, smooth lerp, update rate config | `phase3-audio-occlusion` |
| 3.20 | Dialogue system | `.uzdialogue`, voice acting, subtitles, ducking, Lua | `phase3-dialogue-system` |

**Saída da Phase 3:** Engine com personagens animados, física completa, audio dinâmico, mundo streamável. Pronto para um jogo AAA simples.

---

## Phase 4 — Engine de Produção
> Objetivo: Build para distribuição, sistemas avançados (navmesh, terrain, plugins, destruição).

| # | Etapa | Entregável | Task |
|---|-------|-----------|------|
| 4.1 | Build pipeline | Cook → Pack → Stage → NSIS installer | `phase4-build-pipeline` |
| 4.2 | NavMesh | Recast/Detour, NavMeshAgent, RVO2, debug draw | `phase4-navmesh` |
| 4.3 | Terrain system | `.uzterrain`, heightmap, splatmap, LOD chunks, water | `phase4-terrain-system` |
| 4.4 | Plugin system | `.uzplugin`, stable C ABI, hot-reload, IPC namespace | `phase4-plugin-system` |
| 4.5 | Destructible meshes | Voronoi fracture, debris pool, DestructibleBrokenEvent | `phase4-destructible-meshes` |
| 4.6 | Cloth simulation | Jolt SoftBody, skeleton blend, wind zones, LOD | `phase4-cloth-simulation` |
| 4.7 | Virtual texturing | Page table, tile cache, streaming, shader integration | `phase4-virtual-texturing` |
| 4.8 | Visual scripting | Node graph editor, Lua code gen, hot-reload | `phase4-visual-scripting` |

**Saída da Phase 4:** Engine completo, jogo exportável para Windows, ecossistema de plugins.

---

## Visão Geral em Uma Tabela

```
Phase 0-PRE │ 5 design tasks           │ Arquitetura definida, sem código
Phase 1     │ 23 tasks (4 blocos)      │ Editor funcional, Lua rodando
Phase 2     │ 24 tasks (4 blocos)      │ PBR + assets + audio + UI
Phase 3     │ 20 tasks (5 blocos)      │ Personagens + física + GI + gameplay completo
Phase 4     │  8 tasks                 │ Build + navmesh + terrain + plugins
─────────────────────────────────────────────────────────────
Total       │ 80 tasks                 │
```

---

## Critério de "Concluído" por Phase

| Phase | Critério |
|-------|----------|
| 0-PRE | Todos os 5 specs revisados e aprovados |
| 1 | Abrir `.uzscene`, editar no editor, rodar Lua, ver resultado em tempo real |
| 2 | Importar glTF do Blender, renderizar PBR correto, tocar áudio, mostrar UI |
| 3 | Personagem animado com física, navega no mundo, salva progresso, audio adaptativo |
| 4 | `UzBuild --package` gera instalador Windows, jogo roda standalone |
