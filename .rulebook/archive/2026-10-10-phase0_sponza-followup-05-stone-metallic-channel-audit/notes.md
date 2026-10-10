# Notes — phase0_sponza-followup-05-stone-metallic-channel-audit

2026-09-08. Content-only change; no engine code touched, no rebuild needed.

## The wiring was right; the data was not

`sample/content/materials/sponza/*.uzmat` follow the glTF metallicRoughness convention correctly — `PBROutput.Metallic ← <tex>.B`, `Roughness ← <tex>.G` — which `docs/engine/12-rendering/32-graph-material-draw-metadata.md` maps to the ORM path. So the suspicion in the proposal ("Metallic sourced from a plain roughness map") was only half right: the source *is* the metallicRoughness map's blue channel, and that channel is dirty.

Measured directly from the PNGs (`scripts/rendering/audit_sponza_metallic.ps1`, every 4th texel):

| Texture | mean B (metallic) | texels > 0.1 |
|---|---|---|
| `arch_stone_wall_01_metallicRoughness.png` | 0.110 | 48 % |
| `brickwall_02_metallicRoughness.png` | 0.112 | 48 % |
| `window_frame_01_metallicRoughness.png` | 0.129 | 56 % |
| `stone_trims_01_metallicRoughness.png` | 0.125 | 54 % |
| `wood_door_01_metallicRoughness.png` | 0.248 | 34 % |
| `floor_01_metallicRoughness.png` | 0.035 | 16 % |
| `ceiling_plaster_02`, `roof_tiles_01`, `wood_01` | 0.000 | 0 % |
| `metal_door_metallicRoughness.png` | **0.816** | 97 % |

Every dielectric in the atrium carried 3–25 % metallic. `metal_door` is the one genuinely metallic material and is left alone.

## Second, larger finding: materials with no Metallic input rendered *fully* metallic

Four materials have no `Metallic` pin at all: `glass`, `lamp_glass_01`, `light_bulb`, `dirt_decal`. `gbuffer_ps.hlsl:186-188` multiplies by `metallicMultiplier` when a metallic texture is bound but assigns it **directly** when none is:

```hlsl
metallic *= metallicMultiplier;   // texture bound
metallic  = metallicMultiplier;   // no texture: use multiplier directly
```

The neutral-material pass (`phase0_sponza-parity-02`) set every multiplier to 1.0, which is neutral only in the first branch. In the second it means metallic = 1.0 — glass, the lamp glass, the light bulb and the floor decal were rendering as pure metal. The pre-port hand calibration had `metallicMultiplier: 0.0` on exactly these four (`sample/reference/sponza-manual-calibration/materials/sponza/`), so the neutral pass introduced it.

`E:\SponzaTest` carries the identical files (byte-for-byte for `arch_stone_wall_01.uzmat`, and `metallicMultiplier: 1.0` on all four), so this is a defect **shared with the reference branch**, not a porting error. Fixing it makes our Sponza correct and slightly different from SponzaTest — the deliberate first divergence from that baseline.

## Fix

`scripts/rendering/audit_sponza_metallic.ps1 -Apply` set `params.metallicMultiplier = 0.0` on the 23 flagged materials (19 dirty-texture + 4 constant-metallic). No graph topology changed: with a texture bound the multiply zeroes it, and without one the assignment zeroes it. `metal_door` and the four already-clean materials were not touched.

## Evidence

| Measurement | Before | After |
|---|---|---|
| `--view-mode metallic`, Camera A: mean /255 | 42.3 | **1.4** |
| Camera A: share > 0.25 | 33.20 % | **0.83 %** |
| Camera A: share > 0.5 | 1.96 % | **0.02 %** |
| Camera C: mean /255 | 59.6 | **4.1** |
| Camera C: share > 0.25 | 41.88 % | **3.93 %** |
| Camera C: share > 0.5 | 18.13 % | **0.56 %** |
| Lit Camera A f640 | 52.0 / 36.5 % | 53.2 / 34.0 % (+2.3 %) |
| Lit Camera C f640 | 90.1 / 15.7 % | 87.7 / 15.3 % (−2.7 %) |
| Re-audit | 19 DIRTY + 4 constant-metallic | **0 DIRTY, 27 CLEAN, 1 METAL** |

The lit deltas are small and in the predicted direction: stone gains a little diffuse (F0 no longer pulled toward the base colour) and loses the metallic specular tint. Side-by-side in `.screenshots/fu05-metallic-antes-depois.jpg`; the metallic view goes from "the whole atrium is metal" to black except the windows (sky background, not a surface) and the metal door.

Residual > 0.25 at Camera C (3.93 %) is the metal door plus its reflections — intentional.

## Regression

- `scripts/rendering/test_material_specular.ps1` 686/686.
- `test_runtime.exe --gtest_filter='*MaterialGraph*'` 39/39. (Note: `MaterialGraphHlslCodegen.Add_ExplicitVariables_ConnectedInputs` failed in the 11:46 run under a wider filter and passes under this one — order-dependent, unrelated to this change.)
- `sample/reference/sponza-manual-calibration/` untouched.

## Open

- The textures themselves still carry the dirty blue channel; the multiplier neutralises them at the material level. Re-exporting the maps with a clean metallic channel would be the asset-side fix.
- `metallicMultiplier` semantics differ between the two `gbuffer_ps.hlsl` branches (multiply vs assign). A material with no metallic input and a non-zero multiplier is silently fully metallic — an engine-side guard (treat "no metallic source" as 0 unless explicitly authored) would prevent this class of bug. Not changed here: it would alter every project's materials, which is out of this task's scope.
