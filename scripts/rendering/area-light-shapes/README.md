# Disk and capsule reference

Run from the repository root. Requires the existing harness dependencies, NumPy
and Pillow. `prepare` does not start the engine. It emits UZMH v2 meshes and
separate raster/PT scenes and projects under the evidence directory. The checked-in
scenes are source specifications, not standalone runnable projects: use the
expanded projects in `fixtures/`.

```powershell
$v = "scripts/rendering/area-light-shapes/verify.py"
$out = "E:/UzmiEngine/area-ltc-shapes"
python $v prepare --out $out
python $v baseline --out $out --exe E:/path/to/uzengine.exe --cwd E:/UzmiEngine/uzengine
python $v capture --out $out --shape disk --mode raster --exe E:/path/to/uzengine.exe --cwd E:/UzmiEngine/uzengine
python $v capture --out $out --shape disk --mode pt --exe E:/path/to/uzengine.exe --cwd E:/UzmiEngine/uzengine
python $v capture --out $out --shape tube --mode raster --exe E:/path/to/uzengine.exe --cwd E:/UzmiEngine/uzengine
python $v capture --out $out --shape tube --mode pt --exe E:/path/to/uzengine.exe --cwd E:/UzmiEngine/uzengine
python $v analyze --out $out --max-relative-rmse 0.1
python $v montage --out $out
python $v perf --out $out --shape disk --mode raster --exe E:/path/to/uzengine.exe --cwd E:/UzmiEngine/uzengine
python $v perf --out $out --shape tube --mode raster --exe E:/path/to/uzengine.exe --cwd E:/UzmiEngine/uzengine
```

Replace the example executable with a real build. Omit `--exe` to use the runner
default. Every engine invocation has a 170-second timeout; `--timeout` is restricted
to `(0,180]`. Captures are serialized one command at a time. Existing capture
directories are refused, never cleaned. Choose a new evidence root for repeats.
The baseline defaults to `reflection-reference.uzproject`, frame 95 of 100.
`--project PATH` selects another baseline, such as the existing
`scripts/rendering/area-light-ltc.uzproject`. Baseline keeps its project's CVars.

## Geometry and measurement

Both source specs use radiance 6, black environment, floor albedo 0.6,
roughness 1, metallic 0, floor top z=0. Analytic lights have specularScale 0.
Disk full diameters are 120 x 120 cm along local right/up. Tube width is the
120 cm center-segment length, height is the full 30 cm diameter; its end-to-end
length is 150 cm. Rotation `[90,0,0]` puts local right along world Y. Tube is a
closed capsule with hemisphere ends and constant outward radiance, not a flat
stadium or a cylinder with flat end caps. `line` is the loader alias for `tube`;
to exercise it, change the generated raster scene's light type to `line`.

The disk reference is a single downward-facing 256-triangle fan at z=200.
The capsule uses 256 azimuth divisions, 48 latitude divisions per hemisphere,
and the straight center cylinder. Mesh triangle winding agrees with the engine's
clockwise-front convention. Each emitter has a unique mesh asset, separate from
the floor's cube, avoiding the mesh-indexed PT material-table aliasing of a
shared cube fixture.

Raster projects contain only the analytic emitter; PT projects contain only the
matching emissive mesh. The camera is near overhead at `(0,-60,150)`, below the
emitter, looking at the origin; the emitter cannot obscure the floor ROI.
Resolution is 640x360. Only the central 32x32 pool at `(304,164)` is dumped,
at frame 95. Screenshots are display PNGs; error measurement uses linear HDR
`output` records, not decoded PNGs. The camera block unprojects each ROI pixel
onto z=0, independently of PT lighting-record availability.

The reference integrates `rho*Le/pi * cosReceiver*cosEmitter/distance^2 dA`
over the actual smooth surface, with Gauss-Legendre area quadrature and uniform
azimuth midpoint samples. Disk on-axis `F=r^2/(h^2+r^2)` is reported separately:
`F=3600/43600`, `Lo=0.6*6*F`. A second quadrature order reports the maximum
refinement delta. Capsule integration includes the cylinder and both hemispheres.
The default relative RMSE threshold is 10%, a configurable diagnostic threshold,
not a claimed renderer accuracy guarantee. `analyze` writes `analysis.json`
before exiting 1 if a run exceeds it. All four captures must exist.

PT uses 2 bounces and 4 NEE samples. Its intrinsic dielectric Fresnel/specular
and diffuse energy partition remain active at roughness 1; analytic
specularScale does not affect PT. Therefore the PT residual against pure Lambert
includes that material difference, Monte Carlo noise, finite mesh tessellation,
and possible raster range attenuation. It must not be labeled pure LTC error.
There is no PT convergence guarantee at 100 frames, no repeated-seed study, and
no light-edge/glossy validation in this fixture.

Tonemap operator 0, exposure 1, and disabled auto exposure/bloom/GI/AO/reflections/fog
match the existing LTC project's linear setup. TAA is enabled because the HDR
output diagnostic is written by its resolve; disabling TAA invalidates that stamp.
The runner rejects known capture interruptions and shader/device
failures; the region reader rejects invalid stamps.

`perf` records 128 frames with timing starting at 32. It writes `perf.json`
from the manifest's actual `gpu_ms` and `frame_ms`; it fails explicitly when GPU
timing is unavailable. The CPU metric is never substituted for GPU time.
This is one bounded run, not statistical proof of a speedup.
Use `perf --lights-disabled` for the matched zero-light control. `--frame 383`
captures a longer accumulation for convergence checks without overwriting frame 95.
