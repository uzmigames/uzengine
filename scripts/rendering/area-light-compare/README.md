# Area light vs point light, and a large area light's soft shadows

Evidence fixtures for the two visual items of `phase3a-3.33.0-area-lights-ltc` (2.3 and 2.4).
Run from the repository root; needs the flicker harness dependencies, NumPy and Pillow.
`prepare` does not start the engine: it expands the two checked-in source scenes into
raster / path-tracer projects under `--out/fixtures` and copies the two sample meshes next to
them (the loose-file VFS refuses asset paths outside a project's content root). No repository
asset is generated.

```powershell
$v = "scripts/rendering/area-light-compare/verify.py"
$out = "E:/UzmiEngine/_evidence/lighting/area-lights-ltc-finish/runs"
python $v prepare --out $out
foreach ($cam in "hero", "low", "side", "glossy") {
  foreach ($mode in "point", "rect-unshadowed", "rect", "pt") {
    python $v capture --out $out --scene compare --camera $cam --mode $mode
  }
}
foreach ($cam in "oblique", "grazing", "behind") {
  foreach ($mode in "lambert", "off", "on", "pt") {
    python $v capture --out $out --scene large --camera $cam --mode $mode
  }
}
python $v analyze --out $out
python $v montage --out $out
```

Every engine run is hidden, 640x360, bounded by `--timeout` (default and maximum 180 s), and
refuses to overwrite an existing capture directory. `--exe` and `--cwd` default to the runner's
`runtime/build/bin/uzengine.exe` and the repository root.

## Scenes

Both use a black environment, exposure 1, passthrough tonemap (PNG code / 255 = linear radiance,
clipped at 1), TAA on (the HDR `output` region record is written by its resolve), no GI / AO /
SSR / bloom / fog, light BVH on, path tracer 2 bounces (direct lighting) and 4 NEE samples.

`compare.uzscene` (item 2.3) is the stage-1 scene: a 120 x 80 cm rectangle of radiance 6,
200 cm above a roughness-0.3 floor of albedo 0.6, with roughness 0.1 / 0.5 / 0.9 spheres.
Modes: `point` = a point light at the rectangle's centre, inverse square, `sourceRadius` 0, no
shadows, intensity `pi * L * F * h^2` (53 039) so the floor directly below receives the same
irradiance as from the rectangle (`fixtures/point-light-match.json`); `rect-unshadowed` =
the rectangle with `r.Lighting.AreaLightShadows=0`; `rect` = the rectangle at engine defaults
(DXR area shadows on); `pt` = the path tracer with an emissive quad of the same size and
radiance. Cameras: `hero` (the stage-1 view), `low` (grazing), `side` (three-quarter) and
`glossy` (the hero view over a roughness-0.12 floor, where the rectangle's reflection keeps
its shape).

`large.uzscene` (item 2.4): a 360 x 240 cm rectangle of radiance 2.5, 200 cm above a
roughness-1 floor, a 50 x 50 x 60 cm box standing on the floor and a 180 x 6 x 110 cm wall at
y = 190 whose back face lies behind the one-sided light's plane. Modes: `lambert` = the
rectangle with `specularScale` 0 and shadows off (LTC diffuse only, for the analytic check);
`off` = shadows off; `on` = engine defaults; `pt` = the path tracer. Cameras: `oblique`,
`grazing` and `behind` (the wall's back face and the floor shadow behind it).

## Measurement

`analyze` reads the whole-frame linear-HDR `output` record of the measured frame (raster 95,
path tracer 1535 = 1535 accumulated samples) and never PNG codes. Pixel masks come from the
raster run's recorded unjittered camera: each pixel's ray is intersected with the floor
(z = 0), the box and wall, the spheres and the path tracer's emitter quad; a floor pixel is one
whose ray reaches z = 0 first. ROIs are world rectangles on the floor (the pool under the
light, the box shadow, the contact strip in front of the box, open lit floor, the floor behind
the wall), the mirror footprint of the rectangle (floor points whose mirror ray hits the
rectangle, and its 2x dilation), and the wall's back face. On the large scene every unoccluded
floor pixel is also compared with the exact Lambert contour integral of the rectangle
(`rho * L * F`). Visibility is `on / off` for the raster (same shading, shadow term only) and
`pt / lambert` for the path tracer, whose unshadowed result agrees with the LTC-diffuse run on
open floor. Profiles through the box shadow (along x at y = -60, along y at x = 0) and behind
the wall report the 10-90 % transition widths and the ON-vs-PT visibility RMSE.

The path tracer keeps its own dielectric diffuse/specular energy partition
(`(1 - E_spec) * Lambert + GGX`) while the raster adds the LTC specular of the rough dielectric
on top of plain Lambert, so `off`/`on` sit a few percent above both `lambert` and `pt` on open
floor; that residual is the base BRDF convention documented in stage 1, not LTC error. The
`--path-trace` camera at `92a9c95b` matches the raster camera at the same fov (the 2x
tan(fov/2) quirk that `area-light-shadows/verify.py` compensated no longer applies; the montage
diff column and the box silhouette columns in `analysis.json` show the alignment).

`montage` writes `montage-<scene>.png` / `insets-<scene>.png` from the linear PNGs and
`*-display.png` copies with a 2.2 gamma for viewing; the gamma copies are never analysed.
