## 1. Material graph data model
- [x] 1.1 Add `MaterialGraphBlendMode { Opaque, Masked, Translucent, Additive }`, `blendMode`, `twoSided` to `material_graph.h`; read/write them in `FromJson`/`ToJson` (`material_graph.cpp`, +13).
- [x] 1.2 Authored counts confirmed directly from the files (2026-09-07): 1 `Masked` (`dirt_decal`), 1 `Translucent` (`glass`), 26 with no key = Opaque; all 28 author `twoSided: false`; `specularMultiplier` is 1.0 in 27 and **0** in `dirt_decal`. A load-time log line was not added — the JSON is the source of truth and a per-load count would be noise.

## 2. Draw metadata for graph-only materials
- [x] 2.1 Port the `scene_sync.cpp` hunk: when a mesh has a `material_graph` and no `MaterialInterface`, fill the ordinary PBR draw fields (albedo/normal/ORM roles, constants, blend mode, two-sided) from the graph.
- [x] 2.2 Reassessed — those hunks are NOT two-sided raster state (see notes.md). They are a use-after-scope fix for `VertexLayout::Standard()`, which we had in **8 places**; all hoisted. Two-sided raster needs no change here: all 28 Sponza materials author `twoSided: false`, and `cmd.twoSided` now carries the graph value for when one does. Translucent shadow exclusion is item 2.3. The GDF reading the same metadata is verified in task 03.
- [x] 2.3 Port the translucent shadow-caster exclusion from `97acd374` (moved here from task 01 on 2026-09-07 — these hunks are blend-mode work, not bias work): `if ((drawCmd.materialFlags & 0x3u) >= 2u) continue;` in `shadow_renderer.cpp`, `frame_shadows.cpp` and `passes/lighting/shadow_pass.cpp`, plus the `hasTranslucent` guard that disables the instanced path (`useInstanced = desc.instanced && desc.bindless && !hasTranslucent`, needs `<algorithm>`). Verify glass casts no shadow while stone still does.

## 3. Specular energy
- [x] 3.1 `hlsl_material_translator.cpp`: route `specularMultiplier` into the packed GBuffer alpha; document the `shadingModelID*16 + specular` packing in the translator and in `deferred_lighting_ps.hlsl`.
- [x] 3.2 `deferred_lighting_ps.hlsl`: replace `ComputeF0(baseColor, metallic)` with the F0/F90 block; use `F_Schlick(F0, F90, ·)` in the sun, point and spot loops (keep the spot shadow-atlas sampling — SponzaTest lacks it).
- [x] 3.3 Multi-scatter: `FssEss_d = F0*A + F90*B`, `Favg_d = F0 + (F90 - F0)*0.047619`, same for the NoV=1 diffuse term.
- [x] 3.4 Ambient combine ported, chrome special case removed. Decision: keep the CVar and the `pow`, change its default 2.0 → 1.0 in `frame_constants.cpp`, which is numerically identical to SponzaTest's plain `ao` while leaving the knob live (a dead CVar is the defect class this port keeps fixing). Rationale in notes.md.
- [x] 3.5 `scripts/test_material_specular.ps1` → **686/686** (SponzaTest: 367/686 before their fix). The harness extracts our own `scene_sync.cpp` graph block and the shader F0/F90 assignments, so it validates the port rather than a copy. BRDF 200/200 was already covered by task 01 item 3.3.

## 4. Translucent emission
- [x] 4.1 `translucent_ps.hlsl`: add the emission input path (+11 lines in `97acd374`); the `WindowSunGlow` constant `(8.0, 6.5, 4.5)` must show on the atrium windows.

## 5. Verification
- [x] 5.1 `zig build` exit 0, `Shader integrity OK (134 shaders verified)`, binary relinked (verified by the new string literals being present in the exe, not just the timestamp). Captures: `.screenshots/sponza-02-material-energy.png` (Camera A) and `.screenshots/sponza-02-cameraC.png` (Camera C, via the ported `scripts/sponza-lighting-camera-c.uzproject` + `sponza-gi-camera-c.qa.uzscene`). Camera A mean luminance 59.0 vs 61.2 after task 01, 0.6% crushed, nothing blown; visibly more surface relief on floor and columns. Camera C shows the arcade reading clearly, bright window glass, no white decal patch. `lighting.indirect` avg rose 0.281 → 0.346 in `--diag`, consistent with `r.IndirectAO.Exponent` going 2.0 → 1.0.
- [x] 5.2 Covered analytically instead of by eye: `test_material_specular.ps1` (686/686) evaluates the real packing round trip — graph `specularMultiplier` → `materialFlags` bits[8:15] → `packedAlpha = specular + smID*16` → `saturate(packedAlpha - smID*16)` → `F0 = 0.08 * specular` — across the full input space, including the dirt decal's authored 0. A debug view would show one frame of that; the harness proves every case.



## 6. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 6.1 Documentation: `docs/engine/12-rendering/32-graph-material-draw-metadata.md` (indexed in that folder's README) — authored keys, the materialFlags bit layout, constant/texture input resolution, the F0/F90 formulation, translucency handling and the directional light fields.
- [x] 6.2 Tests: `MaterialGraphSerialization.BlendModeAndTwoSidedRoundTripAndHash` added to `runtime/tests/material/test_material_graph.cpp` (round trip + content-hash sensitivity), plus the ported `scripts/test_material_specular.{cpp,ps1}` harness.
- [x] 6.3 Tests run 2026-09-07. `MaterialGraphSerialization.BlendModeAndTwoSidedRoundTripAndHash` **passes**; the material graph suites are 38/39, and the broader `MaterialGraph*:*BlendMode*` filter is 74/74. `scripts/test_material_specular.ps1` is 686/686.
  - **`runtime/tests/material/` and `runtime/tests/engine/material/` were never registered in `build.zig`** (neither here nor in SponzaTest), so all 46 tests in them — including the one SponzaTest added for this feature — were compiled by nothing and run by nobody. Registered both; they compile and link clean.
  - That surfaced one genuine pre-existing failure: `MaterialGraphSerialization.RoundTrip_NodeCount_Connections_ParamValues` — `ScalarConstant id=0 not found after round-trip`. Unrelated to this task (expression id 0 collides with the `IsConnected() { return connectedExprId != 0; }` sentinel); it needs its own task, and it is now visible instead of silent.
  - The full `zig build test` still fails on the 73 pre-existing failures plus the `MaterialPipeline_ParticleMesh` crash catalogued in task 01; this task adds no new failure to that set.

## 7. Directional light fields dropped on load (found 2026-09-07, not in the original plan)
- [x] 7.1 `scene_loader.cpp::ApplyDirectionalLightFields` read only `color` and `intensity`. Ported SponzaTest's parser: `atmosphere_sun_light`, `light_source_angle`, `shadow_cascades`, `cascade_distribution_exponent`, `dynamic_shadow_distance`, each with a camelCase alias, plus a warning for unrecognized keys. Sponza authors `light_source_angle: 0.53` (real solar angular diameter) and was silently getting the 1.2 degree component default, so every PCSS penumbra was more than twice as wide as authored.
- [x] 7.2 Decide separately whether `DirectionalLightComponent::lightSourceAngle`'s default should move 1.2 → 0.5357 (`RenderingParity.DirectionalLightDefaults` asserts 0.5357 and currently fails). Affects every scene that omits the key, so it is not part of this task. **Decision 2026-09-08: moved to 0.5357°** (`light_component.h`, physical sun disc, UE5 default); `RenderingParity.DirectionalLightDefaults` passes; Sponza sets `light_source_angle` explicitly so its capture is unchanged (51.7 / 37.0 %).
