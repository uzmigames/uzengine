# Proposal: Uniform Buffer System — UE5-Style Shader Parameter Binding (Phase 0, P0)

## Why

UzEngine's current shader binding is broken for DX11 and Vulkan — IBL, auto-exposure, and shadows are all non-functional on these backends because:

1. **Manual register assignment**: Every shader declares `register(t0)`, `register(t1)`, etc. individually. Adding a new texture requires coordinating C++ pass code, HLSL declarations, and root signature layout across every backend.
2. **No texture binding for SM5 path**: The deferred lighting shader's IBL path is gated behind `#ifdef UZ_BINDLESS`. Without bindless, DX11/Vulkan can't access irradiance cubemaps, prefiltered specular, or BRDF LUTs — resulting in black scenes.
3. **Fragile CB layout**: `ShadowDataCB` already hit a critical layout mismatch (`float[8]` in HLSL = 128 bytes vs C++ 32 bytes) that broke shadows for weeks. Every CB addition risks the same bug.
4. **No separation of CB data vs resource bindings**: UE5 separates scalar data (→ CBV) from texture/sampler pointers (→ SRV/sampler slots) automatically. UzEngine crams everything into manual CBVs and hopes the layouts match.

UE5 solves this with a Uniform Buffer System: a single C++ struct definition drives both the CBV upload AND the texture/sampler binding, with metadata that the RHI resolves per-backend. Source: UE5 `ShaderParameterStruct.h`, `FReflectionUniformParameters` in `ReflectionEnvironment.h:25-36`.

## What Changes

### New Systems

1. **`ShaderParameterMetadata`** — Describes the layout of a shader parameter struct: which members are scalar (go into CBV) and which are resources (bound as SRV/UAV/sampler). Generated at compile time from macros.
2. **`ShaderParameterBinder`** — Takes a filled C++ struct + its metadata, uploads scalar data to a CB ring allocation, and binds textures/samplers via the RHI command list per-backend.
3. **`UZ_BEGIN_SHADER_PARAMETERS` / `UZ_END_SHADER_PARAMETERS` macros** — Declare a struct with metadata generation. Members tagged as `UZ_SHADER_PARAMETER` (scalar → CBV), `UZ_SHADER_PARAMETER_TEXTURE` (→ SRV slot), `UZ_SHADER_PARAMETER_SAMPLER` (→ sampler slot).

### Refactoring

4. **Migrate `PerFrameCB`** to shader parameter struct — first migration target, used by every pass.
5. **Migrate `ShadowDataCB`** — eliminate float4[2] packing hack, system handles HLSL layout.
6. **Migrate deferred lighting pass** — replace `#ifdef UZ_BINDLESS` texture access with uniform buffer texture members that work on all backends.
7. **Add IBL textures to lighting params** — irradiance cubemap, prefiltered specular, BRDF LUT as texture members. Automatically bound via `SetFragmentTexture` on DX11, descriptor heap on DX12, descriptor set on Vulkan.

## Impact
- Affected specs: `docs/specs/rendering/renderer.md`, `docs/specs/engine/core-layer.md`
- Affected code: `runtime/engine/rhi/`, `runtime/engine/renderer/core/constant_buffers.h`, `runtime/engine/renderer/core/root_slots.h`, all render passes, all HLSL shaders
- Breaking change: YES — incremental migration, one pass at a time
- User benefit: DX11 and Vulkan render identically to DX12
- Enables: DX11/Vulkan visual parity, safe shader hot-reload, material system, Lua shader parameters
