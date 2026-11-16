# aces-visual

GPU evidence for `phase2_aces-hill-specular-occlusion` items 2.2.1-2.2.3.

```sh
# from the repository root, after `zig build -Doptimize=ReleaseFast` in runtime/
python scripts/rendering/aces-visual/verify.py prepare --out OUT
python scripts/rendering/aces-visual/verify.py capture --out OUT --all
python scripts/rendering/aces-visual/verify.py analyze --out OUT
python scripts/rendering/aces-visual/verify.py montage --out OUT
```

`capture` runs 19 hidden engine runs (each bounded to 180 s) and keeps the frame-120
screenshot plus a region dump of that same frame:

| Runs | Scene | What is varied |
|------|-------|----------------|
| `tone-{quarter,profile,visor}-{default,hill,narkowicz}` | `damaged_helmet` sample | `r.TonemapOperator` unset / 1 / 2 |
| `swatch-{hill,narkowicz}` | helmet + 15 orange/red/amber emissive spheres | `r.TonemapOperator` 1 / 2 |
| `specocc-helmet-{profile,quarter}-{on,off}` | `damaged_helmet` sample | AO input on / forced to 1 |
| `specocc-sphere-{low,high}-{on,off}` | two dielectric spheres on a ground plane | GTAO on / AO input forced to 1 |

Tone and swatch runs use TAA on and bloom off, so the dump's `output` record is exactly the
HDR that the tonemap reads. `analyze` applies `tonemap_ps.hlsl` to it offline and compares the
result with the screenshot. It also compares Hill with
`aces_reference.aces_full_srgb`, a port of the Academy ACES 1.3 CTL (RRT +
ODT.Academy.sRGB_100nits_dim). `python aces_reference.py` self-checks that port against the
CTL tonescale knots and Hill's published matrices.

Spec-occlusion runs dump the deferred pass's lighting records. OFF turns
`r.Material.AOMap` and `r.GTAO` off, which makes the occlusion factor exactly 1. The on/off
ratio of `specular_indirect` is then compared with the `ComputeSpecularOcclusion` formula,
evaluated per pixel. `r.SSR` and `r.GI.Reflections` are pinned off (their defaults) so no
screen-traced radiance enters the specular term.

Outputs: `OUT/analysis.json` and five `montage-*.png` files.
