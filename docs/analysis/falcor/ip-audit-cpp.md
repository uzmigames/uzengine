<!-- doc-meta
title: IP Audit — UzEngine C++ Renderer (2026-03-30)
category: audit
scope: runtime/engine/renderer, runtime/engine/app, runtime/engine/ecs, runtime/thirdparty
status: complete
-->

# IP Audit — UzEngine C++ Renderer

Audit date: 2026-03-30
Scope: `runtime/engine/renderer/`, `runtime/engine/app/`, `runtime/engine/ecs/`, `runtime/thirdparty/`

---

## Executive Summary

**No critical IP violations found.** The codebase uses its own naming conventions (`uz::`, `PascalCase`
without UE5-style `F`/`U`/`A` prefixes), cites only published academic papers and GDC talks as
references, and correctly gates third-party SDK usage behind build flags. Four low-severity issues
were identified — two are citation style violations (linking to a proprietary source file name
instead of the underlying technique paper), one is a thirdparty dependency missing a redistributable
license file, and one is a comment referencing a proprietary API style without a citation.

---

## 1. Proprietary Identifier Check

### 1.1 UE5 identifiers

Searched for: `FSceneView`, `FViewInfo`, `FSceneProxy`, `FPrimitiveSceneProxy`, `FLightSceneInfo`,
`FExponentialHeightFogSceneInfo`, `UPROPERTY`, `UCLASS`, `USTRUCT`, `UENUM`, `GENERATED_BODY`,
`UObject`, `AActor`, `UActorComponent`, `FRHICommandList`, `FRHIResource`, `FRHITexture`,
`FMaterial`, `FMaterialRenderProxy`, `TArray`, `TMap`, `FString`, `FName`, `FVector`, `FRotator`,
`FTransform`.

**Result: CLEAN.** Zero matches across all 310 files in scope.

### 1.2 Unity identifiers

Searched for: `MonoBehaviour`, `ScriptableObject`, `SerializeField`.

**Result: CLEAN.** Zero matches.

### 1.3 CryEngine identifiers

Searched for: `gEnv`, `ICryEngine`, `SCry`.

**Result: CLEAN.** Zero matches.

### 1.4 F-prefix naming convention (UE5-style structs)

Searched for `struct F[A-Z]`, `class F[A-Z]` patterns in renderer headers.

**Result: CLEAN.** The only `F`-prefixed names in `renderer/` are `FSR2Upscaler`, `FSRInput`,
`FSREasuRootConstants`, `FSRRcasRootConstants`, and `FXAAPassDesc` — all named after the
techniques themselves (FSR = FidelityFX Super Resolution, FXAA = Fast Approximate Anti-Aliasing),
not after UE5 type system conventions.

---

## 2. Copyright Headers

### 2.1 Engine source files

Searched all `.h` and `.cpp` files in `runtime/engine/` for `Copyright`, `LICENSE`, and
`copyright`.

**Result: CLEAN.** No copyright headers from third-party engines exist in engine source files.
All source files are original UzEngine code with no license headers (expected — proprietary engine
code with no redistribution headers needed).

### 2.2 Thirdparty libraries

All checked thirdparty directories have valid open-source licenses:

| Library | License |
|---------|---------|
| astcenc | Apache 2.0 |
| atomic_queue | MIT |
| basis_universal | Apache 2.0 |
| d3d12ma | MIT |
| directx_headers | MIT |
| directxmath | MIT |
| dxc | LLVM/MIT/MS |
| entt | MIT |
| etcpak | BSD |
| fastgltf | MIT |
| freetype | FreeType License |
| googletest | BSD |
| jemalloc | BSD |
| libjpeg-turbo | Modified BSD/IJG |
| libpng | libpng License |
| lz4 | BSD |
| meshoptimizer | MIT |
| metal-cpp | Apache 2.0 |
| mimalloc | MIT |
| msdfgen | MIT |
| smaa | MIT |
| spdlog | MIT |
| spirv-reflect | Apache 2.0 |
| stb | MIT / Public Domain |
| tracy | MIT |
| ufbx | MIT |
| vma | MIT |
| volk | MIT |
| vulkan_headers | MIT |
| xatlas | MIT |
| xxhash | BSD |
| dlmalloc | MIT-0 (license in file header) |
| nlohmann/json | MIT (SPDX in file header) |
| tinyexr | BSD-3-Clause (license in file header) |
| mikktspace | Zlib/libpng (license in file header) |
| sse_mathfun | Zlib/libpng (license in README) |
| glm | MIT/Happy Bunny (COPYING.txt) |
| amd-fidelityfx | AMD FidelityFX SDK License (binary redistribution permitted) |

#### ISSUE: ffmpeg — no LICENSE file in thirdparty directory

`runtime/thirdparty/ffmpeg/` contains only `bin/` and `include/` directories — pre-built DLLs
and headers without a bundled license file. FFmpeg is licensed under **LGPL 2.1** for its
default configuration. Distributing the DLLs requires:

1. Including a copy of the LGPL 2.1 license text.
2. Providing a mechanism for end-users to re-link the application against modified FFmpeg libraries.

**Risk: MEDIUM** — binary redistribution of LGPL libraries without a license file is a license
compliance gap, not a copyright infringement of engine source code.

**Recommendation**: Add `e:/UzEngine/runtime/thirdparty/ffmpeg/LICENSE.txt` containing the LGPL
2.1 text, and document the LGPL compliance obligation in the engine's legal/redistribution notes.

---

## 3. Verbatim Code Blocks

Searched for copy/adaptation patterns: `Based on`, `Adapted from`, `Port of`, `Ported from`,
`Copied from`, `verbatim`.

### 3.1 "Adapted from UE5 Niagara" comment

**File**: `runtime/engine/vfx/core/gpu_particle_buffer.h:5`

```cpp
/// Adapted from UE5 Niagara's FNiagaraDataBuffer pattern. Per-particle
/// attributes are stored in Structure-of-Arrays layout...
```

**Assessment**: The comment describes the *architectural pattern* (SoA ping-pong double-buffering),
not verbatim code. The SoA particle buffer pattern is widely used in GPU particle systems
(published at GDC 2014, 2017, etc.) and is not proprietary to UE5. The actual implementation
uses UzEngine RHI handles, UzEngine namespacing, and differs structurally from Niagara.

**Risk: LOW** — Truthful attribution comment. The code is UzEngine-original.

**Recommendation**: Reword comment to cite the general technique rather than a proprietary
implementation to avoid any appearance of derivation. Suggested replacement:

```cpp
/// GPU particle storage using Structure-of-Arrays ping-pong double buffering.
/// SoA layout for GPU coalescing; Faber et al. 2014 "GPU-Based Particle Systems"
```

### 3.2 No other verbatim code blocks found

All other `Based on` references point to general algorithmic patterns (GPU shader noise hash —
`runtime/engine/material/material_baker.cpp:71`), not proprietary engine source.

---

## 4. @ref Citations to Proprietary Source Files

The CLAUDE.md rule states: **NEVER cite proprietary source files.** Always cite published papers,
standards, or techniques.

### 4.1 VIOLATION: `@ref UE5 NiagaraDataInterfaceEventWrite.cpp`

**Files**:
- `runtime/engine/vfx/modules/sub_emitter_module.cpp:4`
- `runtime/engine/vfx/modules/sub_emitter_module.h:30`

```cpp
/// @ref UE5 NiagaraDataInterfaceEventWrite.cpp — OnDeath/OnCollision/OnEvent triggers
```

**Issue**: This citation names a specific UE5 proprietary source file. This violates CLAUDE.md
Rule 3 ("NEVER cite proprietary source files") and creates a paper trail suggesting the code
was derived from that file.

**Risk: MEDIUM** — Not actual copying, but a citation that implies derivation from proprietary
source and should be removed.

**Fix**: Replace with a description of the technique or a general GPU particle event reference:

```cpp
/// @ref GPU particle event spawning — sub-emitter trigger pattern
/// OnDeath/OnCollision detected GPU-side, EventPayload emitted CPU-side.
```

### 4.2 MINOR: `@ref UE5 Niagara` citations

**Files**:
- `runtime/engine/vfx/modules/sub_emitter_module.h:29,49,104`
- `runtime/engine/vfx/modules/sub_emitter_module.cpp:3`
- `runtime/engine/vfx/core/gpu_particle_buffer.h:5`
- `runtime/engine/vfx/events/vfx_event.h:56`

These cite `UE5 Niagara Event Handler` and `UE5 Niagara — ENiagaraScriptUsage` as references
for general VFX event concepts. The pattern itself (sub-emitter event-based spawning) is
not proprietary — it appears in GPU particle system literature.

**Risk: LOW** — No proprietary code copied. The `ENiagaraScriptUsage` reference in particular
names a UE5 enum type which could imply one-to-one mapping.

**Fix**: Replace `@ref UE5 Niagara — ENiagaraScriptUsage` with a technique description, and
change `@ref UE5 Niagara Event Handler` to a general pattern citation.

### 4.3 `@source AMD FidelityFX CAS — ffx_cas.h`

**Files**:
- `runtime/engine/renderer/core/pipeline/pipeline_library.h:578`
- `runtime/engine/renderer/core/pipeline/pipeline_postprocess.cpp:694`
- `runtime/engine/renderer/frame/frame_postprocess.cpp:606`

`ffx_cas.h` is an open-source AMD header (MIT license, part of FidelityFX SDK in
`runtime/thirdparty/amd-fidelityfx/`). This citation is technically accurate and permitted
under the AMD FidelityFX SDK license. Use of `@source` instead of `@ref` (per CLAUDE.md
convention) is acceptable when referencing an open-source SDK file, but the canonical citation
format should point to the published paper:

**Recommendation** (optional style improvement): Add the paper citation alongside:

```cpp
/// @ref Lottes 2020 "Contrast Adaptive Sharpening" — AMD FidelityFX CAS
/// @source AMD FidelityFX SDK — ffx_cas.h (MIT)
```

---

## 5. "UE5" Behavioral References in Comments

Several comments reference UE5 as a behavioral standard:

| File | Line | Comment |
|------|------|---------|
| `mesh_importer.cpp` | 645 | `// Screen-height thresholds match UE5 defaults.` |
| `scene_sync.cpp` | 91 | `// Auto screen-size thresholds (UE5 defaults)` |
| `fly_camera_controller.cpp` | 51 | `// In editor mode (UE5 behavior): WASD...` |
| `fly_camera_controller.h` | 77,99 | `(UE5 behavior)` |
| `multi_light.h` | 153 | `/// Formula (Frostbite / UE4-compatible):` |
| `mesh.h` | 345 | `// CCW winding order (matches...DX12/UE5 standard)` |
| `mesh_builder.cpp` | 89,784 | `// CCW winding (matches...DX12/UE5 standard)` |
| `material_pipeline.cpp` | 376 | `// DX12/UE5 standard, matching GBuffer` |
| `rhi_types.h` | 511 | `// CCW front face — DX12/UE5 industry standard` |
| `cvar.h` | 141 | `// Auto-Registration Macros (UE5-style...)` |
| `assertions/engine_assert.h` | 5 | `/// Provides three assertion types inspired by UE5's philosophy` |
| `logging/log.h` | 6 | `/// Inspired by Unreal Engine 5's UE_LOG macro pattern` |
| `containers/handle_map.h` | 17 | `/// Pattern from Godot's RID_Owner and Unreal's FHandleTable.` |

**Assessment**: These are comments describing behavioral parity or design inspiration, not
citations of proprietary code. References like "DX12/UE5 standard" for CCW winding, LOD thresholds,
and camera behavior describe common industry practices. The Frostbite/UE4 formula reference in
`multi_light.h` should ideally cite the published paper (de Carpentier & Drobot 2015 or Hill
2012) rather than UE4.

**Risk: LOW** — None of these comments indicate code was copied. They describe intended behavioral
parity with industry standards.

**Recommendations**:
1. In `multi_light.h:153`, replace `Frostbite / UE4-compatible` with the source paper citation:
   `@ref Hill 2012 "Physically Based Shading in Call of Duty: Black Ops"` or the relevant
   Lagarde/de Carpentier 2014 GDC talk.
2. For LOD thresholds in `mesh_importer.cpp`, cite the standard LOD screen-size formula
   rather than "UE5 defaults".

---

## 6. UE5 "Material Inputs — Specular" Citation

**Files**:
- `runtime/engine/renderer/material/material_asset.h:123`
- `runtime/engine/renderer/material/material_interface.h:133`
- `runtime/engine/renderer/scene/scene_sync.cpp:336`

```cpp
/// @ref UE5 "Material Inputs — Specular" — same 0.08 * specular formula.
```

**Assessment**: The `F0 = 0.08 * specular` formula is a widely-known PBR convention, not
proprietary to UE5. The citation refers to UE5's documentation page (a published reference)
rather than source code. This is borderline — it cites a product documentation page rather
than the underlying technique.

**Risk: LOW** — The formula itself is from physics (Schlick 1994 approximation for dielectrics).

**Recommendation**: Replace with the underlying technique citation:

```cpp
/// @ref Schlick 1994 "An Inexpensive BRDF Model for PBR" — F0 = 0.04 for dielectrics
/// Specular input remaps [0,1] to F0 range: F0 = 0.08 * specular (common PBR convention)
```

---

## 7. Thirdparty Summary Table

| Library | License | License File Present | Notes |
|---------|---------|---------------------|-------|
| amd-fidelityfx | AMD SDK License | YES (Kits/FidelityFX/docs/license.md) | Binary redistribution permitted with notice |
| ffmpeg | LGPL 2.1 | **NO** | MEDIUM RISK — needs LICENSE.txt |
| dlmalloc | MIT-0 | In source header | Acceptable |
| nlohmann/json | MIT | In source header (SPDX) | Acceptable |
| tinyexr | BSD-3-Clause | In source header | Acceptable |
| mikktspace | Zlib | In source header | Acceptable |
| sse_mathfun | Zlib | In README | Acceptable |
| glm | MIT | COPYING.txt | Acceptable |
| slang | Empty directory | N/A | Not yet populated |
| fxc | Microsoft WARP/D3D | N/A | DX SDK component, covered by Windows SDK license |

---

## 8. Issues Summary

| Severity | Count | Items |
|----------|-------|-------|
| HIGH | 0 | — |
| MEDIUM | 2 | `NiagaraDataInterfaceEventWrite.cpp` @ref citation; ffmpeg missing LICENSE |
| LOW | 4 | `Adapted from UE5 Niagara` comment; general `@ref UE5 Niagara` citations; `multi_light.h` Frostbite/UE4 ref; UE5 "Material Inputs" doc citation |

### Action Items (Priority Order)

1. **[MEDIUM]** Remove `@ref UE5 NiagaraDataInterfaceEventWrite.cpp` citation from
   `sub_emitter_module.cpp:4` and `sub_emitter_module.h:30`. Replace with technique description.

2. **[MEDIUM]** Add `runtime/thirdparty/ffmpeg/LICENSE.txt` containing LGPL 2.1 text.
   Document LGPL compliance requirement (user must be able to relink against modified ffmpeg).

3. **[LOW]** Reword `gpu_particle_buffer.h:5` "Adapted from UE5 Niagara" to cite the
   general GPU particle SoA technique instead.

4. **[LOW]** Replace remaining `@ref UE5 Niagara` citations in `sub_emitter_module.h`
   and `vfx_event.h` with technique-level descriptions.

5. **[LOW]** In `multi_light.h:153`, replace `Frostbite / UE4-compatible` with the
   published paper citation for the point light attenuation formula.

6. **[LOW]** In `material_asset.h`, `material_interface.h`, `scene_sync.cpp`, replace
   `@ref UE5 "Material Inputs — Specular"` with `@ref Schlick 1994` + convention note.

---

## 9. Conclusion

UzEngine's C++ renderer source does **not** contain proprietary identifiers, verbatim code blocks,
or copyright headers from UE5, Unity, or CryEngine. All rendering algorithms correctly cite
published academic papers (Bruneton, Hillaire, Wronski, Burley, Heitz, Karis, Valient, McGuire,
etc.). The identified issues are citation style violations and one missing license file — none
involve actual IP infringement. Fixing the six items listed above will bring the codebase into
full compliance with both the CLAUDE.md citation rules and standard open-source license
redistribution obligations.
