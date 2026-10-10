# VFX trail UV modes

Three V-coordinate authoring modes for the trail / ribbon mesh builder:
`Stretch`, `Tiled`, `PerRibbon`. Chosen per asset via the new `uv_mode`
JSON key on `.uztrail` files.

Reference: UE5 `ENiagaraRibbonUVMode`.

## Modes

Let `t = distanceFromHead / totalDistance` (normalised arc length in
[0, 1]), `s = strandIndex`, `N = strandCount`, `k = tilesPerRibbon`.

| Mode       | V formula             | Texture addressing | Use case                          |
|------------|-----------------------|--------------------|-----------------------------------|
| `Stretch`  | `V = t`               | Clamp              | One texture copy spans the ribbon.|
| `Tiled`    | `V = t * k`           | **Wrap**           | Texture repeats k times along it. |
| `PerRibbon`| `V = (s + t) / N`     | Clamp              | Per-strand band in a shared atlas.|

`Stretch` is the legacy behaviour; `.uztrail` files written before phase
8.12.0 continue to render byte-for-byte unchanged because the enum default
is `Stretch`.

## JSON binding

```json
{
  "format": "uztrail",
  "material_path": "materials/fire_trail.uzmat",
  "uv_mode": "tiled",
  "tiles_per_ribbon": 4.0
}
```

| Key                 | Default    | Notes                                          |
|---------------------|-----------|------------------------------------------------|
| `uv_mode`           | `"stretch"`| One of `stretch`, `tiled`, `per_ribbon`.       |
| `tiles_per_ribbon`  | `1.0`      | Only used when `uv_mode == "tiled"`, must > 0. |

Unknown `uv_mode` strings and non-positive `tiles_per_ribbon` reject the
load with a `VFX/Error`.

## Tiled sampler requirement

Tiled mode emits `V > 1` by design. The trail texture sampler **must** use
`Wrap` addressing on the V axis, otherwise the repeated sections clamp to
the last row. The default trail sampler (set up elsewhere in the pipeline)
is already `Wrap` on both axes, so authoring works without extra setup.

## PerRibbon semantics

For single-strand trails (`ribbonStrands == 1`), `PerRibbon` degenerates
to `Stretch` — the whole V range is `[0, 1]`. With N strands, strand `s`
samples V in `[s/N, (s+1)/N]`, so the strands share a single atlas
texture where each horizontal band provides a distinct look (rainbow
beam, per-strand gradient, etc.).

## API

```cpp
VfxTrailUVMode mode = VfxTrailUVMode::Tiled;
float tiles = 4.0f;
auto mesh = builder.Build(points, count, camPos,
                          w0, w1, c0, c1,
                          totalDist, pointLifetime,
                          strandCount, strandSpacing,
                          mode, tiles);
```

Tests verify the math directly via `VfxComputeTrailV(mode, t, s, N, k)`
so the per-mode V output can be checked without building a full mesh.

## Tests

`runtime/tests/engine/vfx/test_trail_uv_modes.cpp` covers: string round-
trip (stretch / tiled / per_ribbon / unknown), Stretch endpoints +
midpoint + strand-index independence, Tiled multiplication + zero-clamp
safety + fractional tiles, PerRibbon single-strand degeneracy + two-strand
halves + four-strand quarter bands, JSON loader (default is Stretch,
tiled with tile count, per_ribbon, unknown rejected, negative tiles
rejected).
