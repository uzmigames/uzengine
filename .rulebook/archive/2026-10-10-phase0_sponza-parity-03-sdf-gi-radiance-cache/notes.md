# Notes — phase0_sponza-parity-03-sdf-gi-radiance-cache

## Tasks 03 and 04 were merged into one pass (decided 2026-09-07)

The plan separated "SDF GI + radiance cache" from "final gather + reflections", but SponzaTest wrote
them as one system and the seam does not exist in their code. `frame_gi_passes.cpp` — the file that
registers the GDF composite and the radiance-cache trace — already reads `perFrame.sdfDiffuseSamples`
and passes `worldGather` / `sceneRevision` into the SSGI renderer. Porting task 03 alone would have
meant inventing an intermediate state neither repo has, and building it twice. Both are being landed
together; task 04's items are tracked in its own file and checked off from this work.

## What could be copied wholesale vs merged

Of the GI files, only two carried our own changes since the merge base, both from the phase16 VFX
work: `radiance_cache.h` and `radiance_cache_trace_cs.hlsl`. Everything else (`mesh_sdf_baker.{h,cpp}`,
`global_distance_field.{h,cpp}`, `radiance_cache.cpp`, the two stub shaders, `ssgi_renderer.{h,cpp}`,
the four SSGI shaders, `constant_buffers.h`, `per_frame_params.h`, `frame_lighting.cpp`) was identical
to the base, so it came across verbatim.

**The merged pieces:**

- `RadianceCacheTraceConstants` is **192 bytes here, not 176**: we append four fields for the
  VFX→DDGI emissive feed (`vfxEmissiveSrvIndex`, `vfxEmissiveSampleCount`, two pads). SponzaTest
  repurposed the struct's `_pad` into `firstWrite`, so the merged layout is their 12 rows plus our
  16-byte tail. `ddgi_probe_update_cs.hlsl` had to be updated for both: `_padTC` renamed to
  `firstWrite`, and the task-01 comment corrected from 176 to 192.
- `frame_gi_passes.cpp` came across whole, then our VFX emissive passthrough was re-applied to the
  DDGI dispatch.
- `frame_constants.cpp` was merged hunk by hunk to keep task 02's `r.IndirectAO.Exponent` default.
- `frame_setup.cpp`: only the `WaitIdle()` before releasing deferred-destroy buffers was taken. The
  rest of their diff there is editor navigation (`InitFromCamera`, `PrepareFrame`, overlay
  shutdown-on-failure) and belongs to task 06.
- `scene_mesh_assets.cpp`: took the two bake CVars (`r.GI.MeshSdf.Resolution`, `.TwoSided`) and the
  new `BakeMeshGPU` arguments. Left the readiness counters for task 05.
- `scene_renderer_init.cpp`: the radiance cache is now created at the CVar extent instead of a
  hardcoded 5000, which is what made producer spacing and consumer interpolation disagree.

## Deliberately not taken yet

`scene_renderer.h` / `scene_render_frame.cpp` `ResetExposureHistory`, `m_budgetDeferredMeshCount`,
`scene_texture_assets.cpp` staging-buffer lifetime, and the `dynamicShadowDistance` parameter on
`SetupShadowFrame` — all task 05. The editor navigation hunks — task 06.

## Regression hunt (2026-09-08): "GI off" got darker after the port — root cause found

**Symptom.** Camera C, same flags as the task-02 capture (`sponza-02-cameraC.png`, 1.6% of
pixels below luminance 16), rendered with the task-03/04 binary gives 15.8% — albedo-following
black patches on stone. Reproducible with the identical command line, so not a configuration change.

**Root cause: `r.RadianceCache=0` never disabled the radiance cache.** `scene_renderer_init.cpp`
assigns `m_sdfGiEnabled` twice: `ApplyCVars()` (line ~1057) computes `cvar && GDF && RC`, but
`Init()` (line ~725) runs afterwards and re-assigns `GDF.IsInitialized() && RC.IsInitialized()`,
dropping the CVar term. Pre-existing, and invisible before this port: with the two SDF shaders as
empty stubs the field was all zeros, the old consumer saw an all-zero probe result and fell back to
sky SH. Once the field became real, the consumer ported in this task (correctly) trusts the probes —
so a scene that believes GI is off is lit by the probe grid **without** the final gather, which is
the coarsest possible configuration: 267 cm probe spacing, validity-rejected near walls, no sky
fallback and no `skyLightMinAmbient` floor. With `--cvar r.RadianceCache=0` the log still showed
`SDF GI: active=true, probesReady=true` and 138 mesh bakes.

Proof: `cmpC-igual-A.png` (project default, RC=1, 138 bakes) and `cmpC-sem-gi.png` (`RC=0`,
0 bakes reported earlier — actually 138 too on re-check) gave identical statistics
(89.7 / 53.0 / 15.8%), i.e. the CVar changed nothing.

Fix: `Init()` now ANDs the CVar (`wantSdfGi && sdfGiReady`) and logs
`SDF GI resources ready but r.RadianceCache=0 — disabled`. Because bake queueing is already gated on
`m_sdfGiEnabled` (`scene_mesh_assets.cpp:107`), the same fix skips the ~37 s of SDF bakes when GI is off.

**What was eliminated on the way (all measured, none the cause):**

| Hypothesis | Test | Result |
|---|---|---|
| Shadow-modulated sky IBL hack (`skyVisibility`) | removed, rebuilt, diffed images | 0.95/255 mean change — not it; kept removed on its own merits |
| Sky π× brighter after task 01 | scene `indirect_lighting_intensity` 1.0 → 0.318 (SH scale 0.32) | identical patches |
| GTAO | `r.GTAO=0` | identical |
| Legacy SSGI in the new renderer | `r.SSGI=0` vs 1 | 15.5% both |
| Task-02 graph draw metadata block | temporary `r.Debug.GraphDrawMetadata` switch, metallic view and lit image | identical (59.6/255 metallic, 15.5% blacks) |
| PerFrameCB layout shift from copied headers | diff of `per_frame_params.h` | fields appended at 852+, padding shrunk, `static_assert` at 976 |
| `sdf_reflections.hlsli` sampler at `s2` colliding with `clampSampler` | read the header | guarded by `#ifndef UZ_SDF_REFLECTION_SAMPLER` |
| `m_ssrHasPrevColor` never set | grep | set in `frame_lighting.cpp:236` and `frame_postprocess.cpp:255`, both trees |
| IBL root constants | `frame_lighting_pass.cpp` | identical to HEAD; IBL bake logs identical across runs |
| Shader-side GI hunks | build with `UZ_BISECT_SDF=0` (include + SDF block removed) | built, superseded by the C++ finding before it was needed |

**Instrument caveats learned (put these in the docs, they cost hours):**
- `--diag` readbacks share textures: `gbuffer.metallic == roughness == normal`, `gbuffer.ao == albedo`,
  and `ssgi.trace/denoised/result == lighting.direct` (a constant 0/1/0.25). 3D targets
  (`gi.gdf_*`) read as zero. Do not use it for metallic, AO, SSGI or GDF questions.
- `--view-mode detaillighting` is **directional NdotL only** (no IBL) — a black floor under a roof is
  normal. `--view-mode reflections` is **SSR only**, not IBL specular, despite the enum comment.
  `lightingonly` is HDR÷albedo, unexposed — near-black at interior levels.
- `--pixel-debug` is dead: `pixel_debug_cs.hlsl` is one of the 32 one-line stubs.
- `--cvar r.SkyLight.IndirectIntensity` / `.Intensity` are overridden by the `SkyLightComponent`
  fields each frame; to test sky intensity, edit the scene.
- Shaders are embedded at build time (`build/generated/shaders/*.gen.h`); the `ShaderVFS` mount
  `/engine → build/bin/shaders/runtime` points at a directory that does not exist. Every shader edit
  needs `zig build`.

**Still true and still open:** the stone reads as **metallic in ~18% of pixels** at Camera C
(`--view-mode metallic`, `dbg-metallic.png`) and as smoother in the same regions
(`dbg-roughness.png`). That comes from the graph shader's own Metallic/Roughness inputs
(`*_metallicRoughness.png`, channels B/G) with the neutral `metallicMultiplier: 1.0`, not from this
port — the pre-port manual calibration hid it with `metallicMultiplier: 0.0`. Under a flat ambient
those regions have no diffuse and only see whatever specular reaches them, which is why they crush
first. Worth its own look: either the texture channels are not what the graph assumes, or Sponza's
grunge really is authored metallic and the manual `×0.0` was masking it.

### Second cause on top of the first: `--cvar` never beat the project file

Fixing the `Init()` overwrite alone changed nothing: the RC=0 run still logged `SDF GI enabled` and
138 bakes. The flush log explained it —

```
CVar applied: r.RadianceCache = 0     <- --cvar (queued during argument parsing)
CVar applied: r.RadianceCache = 0     <- the same, queued twice: BuildConfig calls ApplyCLIOverrides twice
CVar applied: r.RadianceCache = 1     <- the .uzproject block, queued later at project load, applied LAST
```

`Config::m_pendingCvarOverrides` is a `std::vector` applied in insertion order, and the project
block was `emplace_back`-ed after the CLI entries, so **the project always won**. That is the
SponzaTest precedence defect that task 05 item 4.1 had dismissed as "already true" from a code
read — reopened with this evidence. Fix: `Config::AddPendingCvarOverrideDefault()` inserts at the
front; `uzengine.cpp` uses it for the project block, so explicit CLI values stay last.

Both fixes are needed: the `Init()` one so the renderer honours the CVar it reads, the precedence
one so the CVar holds what the user typed.

### Verified (2026-09-08 10:26 build)

| Camera C, RC=0 | mean | sd | pixels < 16 |
|---|---|---|---|
| A — task-02 binary | 87.0 | 45.6 | 1.6% |
| broken (before both fixes) | 89.7 | 53.0 | 15.8% |
| **after both fixes** | 91.6 | 47.2 | **3.7%** |

Flush order `1, 0, 0`; `SDF GI resources ready but r.RadianceCache=0 — disabled`; **0 SDF bakes**
(previously 138, ~37 s of startup spent on a feature the scene had asked to turn off).

The residual 1.6% → 3.7% is not explained yet. The shadow-modulated sky hack cannot be it — it
*darkened* shadowed sky, so removing it can only reduce blacks. Note that the earlier "0.95/255"
measurement of the hack was taken while the RC was secretly active, i.e. while the sky-SH path the
hack lives in was not the one lighting the scene; it says nothing about the hack's effect in the real
RC-off path. If that number is ever needed, restore the block behind a CVar and measure again.
