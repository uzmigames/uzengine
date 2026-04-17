# Hardcoded Anti-Patterns Analysis

**Date:** 2026-04-16
**Scope:** `runtime/` (C++, HLSL, tools, asset pipeline)
**Objective:** Identify code where sample-specific values / paths / assumptions leaked into the engine runtime, breaking the agnostic design goal.

## Agnostic Runtime Principle

The UzEngine runtime MUST be **project-agnostic**. The `sample/` project should be swappable with any other project without source changes. The runtime must not know:

- Specific asset names (`Sponza`, `MushroomMonster`, `fbx_test`)
- Specific file paths (`content/`, `sample/`, `meshes/<X>/`)
- Scene-specific tuning (exposure calibrated to one scene's HDR sky, shadow atlas sized to one scene's light count)
- Fixed capacity limits that don't scale with content

## Findings Summary (86 total)

| File | Topic | Count | Severity |
|------|-------|-------|----------|
| [01-rendering-defaults.md](./01-rendering-defaults.md) | Scene-tuned rendering constants | 12 | HIGH |
| [02-capacity-limits.md](./02-capacity-limits.md) | Fixed compile-time caps | 11 | HIGH |
| [03-content-paths.md](./03-content-paths.md) | Hardcoded paths/DLLs/roots | 9 | HIGH |
| [04-shader-magic-constants.md](./04-shader-magic-constants.md) | HLSL literals without `@ref` | 14 | MEDIUM |
| [05-rhi-backend-leaks.md](./05-rhi-backend-leaks.md) | DX12 types above `rhi.h` | 6 | HIGH |
| [06-asset-pipeline.md](./06-asset-pipeline.md) | Texture naming, units, axes | 14 | MEDIUM |
| [07-ecs-and-animation.md](./07-ecs-and-animation.md) | Component defaults, skeletal mesh | 14 | MEDIUM |
| [08-scene-specific-code.md](./08-scene-specific-code.md) | `Sponza`/`Mushroom` strings in runtime | 6 | MEDIUM |

## Top 10 Critical Issues (Ranked)

| Rank | Finding | File | Impact |
|------|---------|------|--------|
| 1 | Exposure `3.0f` calibrated for Sponza skylight | [frame_constants.cpp:73](../../../runtime/engine/renderer/frame/frame_constants.cpp#L73) | Every new project ships visually wrong |
| 2 | SSGI hardcoded 5km depth assumption | [ssgi_renderer.cpp:32](../../../runtime/engine/renderer/gi/ssgi_renderer.cpp#L32) | GI broken outside cm-scale interiors |
| 3 | `"content/"` hardcoded in VFX hot-reload init | [scene_renderer_init.cpp:837](../../../runtime/engine/renderer/scene/scene_renderer_init.cpp#L837) | Runtime not swappable |
| 4 | DX12 headers in public `dx12_device.h` | [dx12_device.h:28](../../../runtime/engine/renderer/rhi/dx12_device.h#L28) | RHI abstraction broken — D3D12 leaks |
| 5 | `kMaxInstances = 16384` compile-time | [instance_batcher.h:48](../../../runtime/engine/renderer/core/instance_batcher.h#L48) | Large scenes can't scale |
| 6 | `kMaxBones = 256` fixed | [skeleton.h:36](../../../runtime/engine/animation/skeleton.h#L36) | Multi-character / crowd systems capped |
| 7 | Texture slot detection by filename keyword | [uzcook.cpp:176-274](../../../runtime/tools/uzcook.cpp#L176-L274) | Non-EN / custom naming breaks silently |
| 8 | Unit scale forced to cm (`0.01f`) | [uzfbx.cpp:88](../../../runtime/tools/uzfbx.cpp#L88) | Meter-based projects triple-scaled |
| 9 | Shadow atlas `2048` / `4096` fixed | [shadow_renderer.cpp:19](../../../runtime/engine/renderer/shadows/shadow_renderer.cpp#L19) | VRAM wasted on small scenes, insufficient on large |
| 10 | `F0 = 0.04` duplicated in 5+ shaders | [deferred_lighting_ps.hlsl:823](../../../runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl#L823) | Roughness floor inconsistent (`0.04` vs `0.045`) |

## Recommended Migration Strategy

### Phase 1 — Project config (high ROI, low risk)
Move sample-specific defaults out of `frame_constants.cpp` into `sample/project.uzproj` as a `PostProcessVolume` default. Runtime reads project config at load time.

**Targets:** Exposure, shadow atlas size, tonemap params, color saturation, skylight intensity, film grain.

### Phase 2 — Dynamic capacities
Replace `constexpr` caps with config-driven values (or dynamic growth).

**Targets:** `kMaxInstances`, `kMaxBones`, `kMaxProbes`, `kTotalProbes`, VFX event buffer, shadow atlas size, descriptor heap sizes.

### Phase 3 — Path/string indirection
Remove hardcoded `"content/"` and `"meshes/"` directory conventions from runtime; derive from `.uzproj`.

**Targets:** VFX hot-reload root, config `content_root` default, cloud shape path, uzcook cache path, sidecar extension.

### Phase 4 — RHI hygiene
Move `<d3d12.h>` include into `dx12_device.cpp`. Use opaque handles in `dx12_device.h`. Verify no `ComPtr<ID3D12*>` leaks above `rhi.h`.

### Phase 5 — Shader constants table
Centralize duplicated magic numbers (`F0 = 0.04`, roughness floor, sRGB curve constants) into `shaders/runtime/common/material_constants.hlsli` with `@ref` citations per AGENTS.override.md rendering rules.

### Phase 6 — Asset pipeline config
Make texture slot detection, shader stage detection, unit/axis conversions driven by `.uzproj` or per-importer manifest. Default to current behavior for backward compatibility.

## Methodology

3 parallel deep scans via Explore agents, covering:
1. **Renderer + tools + config** — frame constants, shadow, GI, tools CLI
2. **ECS + scene loader + animation** — newly-added skeletal mesh system + legacy ECS
3. **Shaders + RHI** — HLSL magic numbers, DX12 backend boundary

Grep patterns focused on:
- Scene-specific strings (`sponza`, `mushroom`, `fbx_test`)
- Absolute paths (`C:\`, `E:\`, `W:\`)
- Resolution literals (`1920`, `1080`, `16/9`)
- Backend type leaks (`ID3D12`, `D3D12_`, `VkDevice`)
- Material/PBR literals without `@ref` comments
- Fixed capacity `constexpr` definitions

Findings excluded as "acceptable" (not logged):
- File-format magic bytes in parsers
- GPU alignment constants justified by hardware (256B CB align)
- Standards-based literals with proper `@ref` citation (e.g., sRGB IEC 61966-2-1)
