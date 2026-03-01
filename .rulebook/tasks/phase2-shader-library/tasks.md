## 1. Include Directory Structure

- [ ] 1.1 Create `shaders/include/` directory
- [ ] 1.2 Create `shaders/include/common.hlsli` -- PI, EPSILON, TWO_PI, HALF_PI, INV_PI, sq(), safe_normalize(), remap(), reconstruct_world_pos(), oct_encode/decode
- [ ] 1.3 Create `shaders/include/brdf.hlsli` -- D_GGX, G_SchlickGGX, G_Smith, G_Smith_IBL, F_Schlick, F_SchlickRoughness, CookTorrance
- [ ] 1.4 Create `shaders/include/lighting.hlsli` -- DirLight/PointLight/SpotLight structs, attenuation functions, eval_dir_light, eval_point_light, eval_spot_light
- [ ] 1.5 Create `shaders/include/shadow.hlsli` -- shadow_pcf_2x2, shadow_pcf_3x3, select_cascade, world_to_shadow
- [ ] 1.6 Create `shaders/include/skinning.hlsli` -- SkinVertex struct, skin_vertex (4 bone influences), MAX_BONES constant
- [ ] 1.7 Create `shaders/include/tonemapping.hlsli` -- ACES_Filmic, Reinhard, Reinhard_Extended, linear_to_srgb, srgb_to_linear
- [ ] 1.8 Create `shaders/include/noise.hlsli` -- hash11, hash21, hash33, perlin_noise, simplex_noise, fbm
- [ ] 1.9 All files use `#ifndef`/`#define`/`#endif` include guards with `UZ_<NAME>_HLSLI` pattern

## 2. GlobalParams Cbuffer

- [ ] 2.1 Create `shaders/include/global_cbuffer.hlsli` -- `cbuffer GlobalParams : register(b15)` with g_Time, g_DeltaTime, g_ScreenSize, g_WindDirection, g_WindStrength, g_FogColor, g_FogDensity, g_FogStart, g_FogEnd, g_FogMode
- [ ] 2.2 Add `compute_fog_factor()` and `apply_fog()` helper functions in global_cbuffer.hlsli
- [ ] 2.3 Create `src/renderer/global_params.h` -- `GlobalParamsData` C++ struct (64 bytes, matches cbuffer layout)
- [ ] 2.4 `static_assert(sizeof(GlobalParamsData) == 64)` to catch layout drift
- [ ] 2.5 `update_global_params()` function: fill struct from FrameContext (time, delta, screen size, wind, fog)
- [ ] 2.6 Upload GlobalParamsData to GPU via root CBV at b15 each frame

## 3. Root Signature Update

- [ ] 3.1 Add Root Parameter 4: CBV at register b15 (GlobalParams)
- [ ] 3.2 Ensure b15 is always bound before any pass executes
- [ ] 3.3 Update root signature serialization and PSO creation

## 4. DXC Include Path Configuration

- [ ] 4.1 Add `set_include_path(const char* path)` to ShaderCompiler API
- [ ] 4.2 Pass `-I shaders/include` to DXC compiler arguments
- [ ] 4.3 Verify `#include "common.hlsli"` resolves from any shader location

## 5. Include Dependency Tracking

- [ ] 5.1 Create `src/renderer/shader_compiler/include_tracker.h/cpp` -- `IncludeTracker` class
- [ ] 5.2 Forward map: `shader_path -> Array<StringId>` of included files
- [ ] 5.3 Reverse map: `include_path -> Array<StringId>` of dependent shaders
- [ ] 5.4 `update(shader_path, includes)` -- called after each successful compilation
- [ ] 5.5 `get_dependents(include_path) -> Span<StringId>` -- for hot-reload cascade
- [ ] 5.6 Extract include list from DXC compilation output (IDxcResult extra outputs)
- [ ] 5.7 Handle transitive includes: if brdf.hlsli includes common.hlsli, changing common.hlsli triggers recompilation of shaders using brdf.hlsli

## 6. Hot-Reload for Include Files

- [ ] 6.1 File watcher monitors `shaders/include/*.hlsli` for changes
- [ ] 6.2 On change: call `include_tracker.get_dependents(changed_file)`
- [ ] 6.3 For each dependent shader: recompile all cached variants (delegates to variant system)
- [ ] 6.4 Log cascade: "common.hlsli changed -> recompiling pbr.hlsl (4 variants), unlit.hlsl (1 variant)"

## 7. IPC Handlers

- [ ] 7.1 `shader.setGlobalParam` -- set a single global parameter by name
- [ ] 7.2 `shader.getGlobalParams` -- return all current global parameter values as JSON
- [ ] 7.3 `shader.getIncludes` -- return include dependency list for a shader

## 8. Update Built-in Shaders

- [ ] 8.1 Refactor `shaders/builtin/pbr.hlsl` to use `#include "brdf.hlsli"` and `#include "lighting.hlsli"`
- [ ] 8.2 Refactor `shaders/builtin/deferred_lighting.hlsl` to use shared includes
- [ ] 8.3 Refactor `shaders/builtin/shadow_depth.hlsl` to use `#include "common.hlsli"`
- [ ] 8.4 Refactor all other built-in shaders to use common.hlsli where applicable
- [ ] 8.5 Remove duplicated BRDF/lighting code from individual shaders

## 9. Testing

- [ ] 9.1 Unit test: include_tracker forward/reverse map correctness
- [ ] 9.2 Unit test: transitive dependency tracking (A includes B includes C, change C triggers A)
- [ ] 9.3 Integration test: compile pbr.hlsl with `-I shaders/include`, verify includes resolve
- [ ] 9.4 Integration test: modify common.hlsli, verify all dependent shaders recompile
- [ ] 9.5 Integration test: GlobalParams cbuffer values readable in shader (verify g_Time increments)
- [ ] 9.6 Integration test: fog computation produces correct visual output
- [ ] 9.7 Shader compilation test: each .hlsli compiles without errors when included alone

## 10. Documentation

- [ ] 10.1 Update `docs/specs/shader-library.md` with any implementation changes
- [ ] 10.2 Update `docs/specs/shader-system.md` to reference include library
- [ ] 10.3 Update `docs/modules/renderer/README.md` shader_compiler subsystem
- [ ] 10.4 Add inline documentation comments in each .hlsli file explaining every function
