# Notes — phase0_sponza-parity-02-material-graph-energy

## What the authored Sponza data actually contains (checked 2026-09-07)

28 `.uzmat` graphs: one `"blendMode": "Masked"` (`dirt_decal`), one `"blendMode": "Translucent"`
(`glass`), 26 with no key (Opaque by default) — matching SponzaTest's counts. All 28 author
`"twoSided": false`. `specularMultiplier` is 1.0 in 27 of them and **0 in `dirt_decal`**, which is the
material whose fake grazing rim this task removes.

## Decisions taken

**`r.IndirectAO.Exponent` default 2.0 → 1.0.** SponzaTest dropped the `pow(ao, N)` from the ambient
combine entirely, which would have left our CVar dead — the exact defect class this port keeps
fixing (`r.Tonemap.Slope/Toe` were dead the same way). Keeping the `pow` with a default of 1.0 is
numerically identical to their code (`pow(x, 1) == x`) while the knob stays live for scenes that opt
in. The pre-port Sponza calibration used 3.0; no other scene sets it.

**Chrome special case removed.** The old combine bypassed DFG, multi-scatter and specular occlusion
for `metallic > 0.95 && roughness < 0.1`, with a comment blaming a "warm ring artifact caused by
envBRDF angular variation". That artifact came from the incorrect envBRDF fit which task 01 replaced,
so the bypass no longer has a cause — and it was itself an energy error on mirrors.

## Corrections to the task plan

**Item 2.2 was wrong about the pipeline files.** The `pipeline_geometry.cpp` / `pipeline_shadow.cpp` /
`material_pipeline.cpp` hunks in SponzaTest `97acd374` are not two-sided raster state — they are a
**use-after-scope fix**. `VertexLayout::Standard()` was declared inside `if (!bindless) { ... }` while
`desc.inputElements` kept a pointer into it, and `CreateCachedPipeline(desc)` ran after that scope
ended. We had the identical bug in **8 places** (6 in `pipeline_geometry.cpp`, 1 in
`pipeline_shadow.cpp`, 1 in `material_pipeline.cpp`); all now hoist the declaration. It only bites the
non-bindless path, which is why it never showed on DX12.

Two-sidedness needs no raster change here after all: all 28 Sponza materials author `twoSided: false`.
`cmd.twoSided` is now populated from the graph, so a future two-sided material reaches the PSO
selection that already exists.

## Gap found outside the plan: directional light fields were dropped on load

`scene_loader.cpp`'s `ApplyDirectionalLightFields` read only `color` and `intensity`. Everything else
the scene authored was silently ignored:

| Authored key | Component default it kept | Sponza authors |
|---|---|---|
| `light_source_angle` | 1.2 deg | **0.53 deg** (real solar angular diameter) |
| `atmosphere_sun_light` | true | true |
| `shadow_cascades` | 4 | — |
| `cascade_distribution_exponent` | 3.0 | — |
| `dynamic_shadow_distance` | 40000 | — |

So every PCSS penumbra in Sponza was sized for a sun more than twice as wide as the one the scene
describes. Ported SponzaTest's parser including the camelCase aliases and the unknown-key warning, so
a typo in a scene file is no longer a silent no-op.

This also explains a pre-existing test failure catalogued in task 01:
`RenderingParity.DirectionalLightDefaults` expects `lightSourceAngle == 0.5357` and gets 1.2 — the
test was written against the physically correct value and the component default had drifted. The
default itself is left at 1.2 in this task (changing it is a separate call, since it affects every
scene that omits the key); Sponza now gets 0.53 because it authors it.

## Verification

`scripts/test_material_specular.ps1` → **686/686** (SponzaTest reports 367/686 before their fix).
The harness is not a copy of expected values: it extracts the graph-metadata block from our own
`scene_sync.cpp` (anchored on `const auto& graph = *graphForDraw;`) and the F0/F90 assignments from
our `deferred_lighting_ps.hlsl`, then evaluates them against the reference formulation.

DXC compiles `deferred_lighting_ps.hlsl`, `translucent_ps.hlsl` and `gbuffer_ps.hlsl` in the bindless
variant. `zig build` exit 0, `Shader integrity OK (134 shaders verified)`.

## Two more defects surfaced while porting (2026-09-07)

**Graph emissive was multiplied by zero.** `gbuffer_ps.hlsl:271` applies
`emissive *= emissiveColor * emissiveIntensity` to both the fixed-PBR and the graph path, and
`scene_sync.cpp`'s "no material assigned" branch — the branch every graph-only draw fell into — set
both to 0. So a graph that authored emission (lamp bulbs, the window glass constant) had it zeroed
before it left the GBuffer. Opaque graph draws now pass 1.0/1.0 through, since the compiled graph
output already carries its own emission and multiplier; translucent/additive draws take the authored
`Emissive` constant and `emissiveMultiplier` explicitly.

**Two test directories were never built.** `runtime/tests/material/` (1 file, 46 TESTs) and
`runtime/tests/engine/material/` (2 files) are absent from the `addCppDir(test_mod, ...)` list in
`build.zig`, in both repos — so SponzaTest's own blend-mode test was also never executed. Registered
both. They compile and link clean, and running them surfaced one genuine pre-existing failure:
`MaterialGraphSerialization.RoundTrip_NodeCount_Connections_ParamValues` fails with
`ScalarConstant id=0 not found after round-trip`, because expression id 0 collides with the
`IsConnected() { return connectedExprId != 0; }` sentinel. Left for its own task — but visible now.

Four other test directories remain unregistered and were left alone (they look deliberate or need
their own review): `tests/debug_skin_reference` (standalone debug tool), `tests/engine/physics`,
`tests/engine/vfx/scripting`, `tests/thirdparty`.
