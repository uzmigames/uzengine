## 1. Setup
- [x] 1.1 Create docs/engine/ directory structure (30 numbered folders)
- [x] 1.2 Create docs/engine/README.md master index

## 2. Tier 1 — Already Built / In-Progress (01-06)
- [x] 2.1 Write 01-core/ (paths, logging, assertions, config, memory)
- [x] 2.2 Write 02-math/ (types, transform, DirectXMath integration)
- [x] 2.3 Write 03-rhi/ (overview, dx12 backend, device resources, PSO, descriptors, barriers, cmd lists, swap chain, debug, future backends)
- [x] 2.4 Write 04-observability/ (log system, CPU profiling, GPU profiling, stat counters, crash reporting)
- [x] 2.5 Write 05-shader-pipeline/ (overview, VFS, compilation, cache, hot-reload, reflection)
- [x] 2.6 Write 06-build-toolchain/ (zig build, thirdparty catalog, N-API addon, build configs)

## 3. Tier 2 — Next Phase Foundation (07-12)
- [x] 3.1 Write 07-platform/ (window, input raw, file I/O)
- [x] 3.2 Write 08-resource-system/ (registry, formats, import, cook, streaming)
- [x] 3.3 Write 09-application-lifecycle/ (bootstrap, subsystems, tick, game instance, plugins)
- [x] 3.4 Write 10-entity-component/ (world, component model, subsystems, events, tick, behaviors)
- [x] 3.5 Write 11-scene/ (format, serialization, prefabs, loading pipeline)
- [x] 3.6 Write 12-rendering/ (render world, visibility pipeline, frame pipeline)

## 4. Tier 3 — Gameplay Systems (13-21)
- [x] 4.1 Write 13-input-system/ (actions, contexts, rebinding)
- [x] 4.2 Write 14-camera/ (system, shake, player controller)
- [x] 4.3 Write 15-scripting/ (LuaJIT, script runner, Lua API, hot-reload)
- [x] 4.4 Write 16-physics/ (Jolt world, rigid bodies, queries, character controller, triggers)
- [x] 4.5 Write 17-audio/ (miniaudio, spatial, components, formats)
- [x] 4.6 Write 18-ui-system/ (HTML/CSS, data binding, rendering, console)
- [x] 4.7 Write 19-animation/ (skeletal, skinning, state machines, morph, IK, root motion)
- [x] 4.8 Write 20-gameplay/ (abilities, behavior trees, save, navigation, data tables)
- [x] 4.9 Write 21-property-system/ (reflection, serialization, reactivity)

## 5. Tier 4 — Developer & Future (22-30)
- [x] 5.1 Write 22-developer-layer/ (overview, profiling, debug draw, hot-reload, automation, console, imgui)
- [x] 5.2 Write 23-localization/ (i18n)
- [x] 5.3 Write 24-networking/ (architecture, transport)
- [x] 5.4 Write 25-vfx/ (particles, vfx graph, decals)
- [x] 5.5 Write 26-ai-content-pipeline/ (LLM, procgen, dialogue)
- [x] 5.6 Write 27-platform-services/ (Steam, cross-platform)
- [x] 5.7 Write 28-ipc-protocol/ (overview, messages, frame streaming)
- [x] 5.8 Write 29-design-decisions/ (rationale, POC lessons, engine comparison)
- [x] 5.9 Write 30-game-examples/ (Pong, FPS, Flappy Bird, Turret Defense)

## 6. Finalization
- [ ] 6.1 Verify all content from ENGINE.md is covered
- [ ] 6.2 Verify all content from ARCHITECTURE.md is covered
- [ ] 6.3 Verify all content from DEVELOPER.md is covered
- [ ] 6.4 Add redirects/deprecation notices to original files
