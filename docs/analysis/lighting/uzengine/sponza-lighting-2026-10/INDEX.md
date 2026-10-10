# Sponza lighting blocks — 2026-10

Analyses of the October 2026 Sponza lighting work: block A (cheap, certain corrections),
block B (structural items 1–7) and the side items that followed. Each document records the
diagnosis, the fix and its validation; the matching Rulebook tasks are archived under
`.rulebook/archive/2026-10-10-phase0_lighting-*`.

## Block A

- [lighting-block-a-2026-10-05.md](lighting-block-a-2026-10-05.md) — cheap, certain corrections.

## Block B

- [lighting-block-b-2026-10.md](lighting-block-b-2026-10.md) — block index and closing sweep.
- [lighting-block-b-01-pass-timings.md](lighting-block-b-01-pass-timings.md) — per-pass GPU timings in `--diag`.
- [lighting-block-b-02-gdf-incremental.md](lighting-block-b-02-gdf-incremental.md) — incremental global distance field.
- [lighting-block-b-03-async-sdf-bake.md](lighting-block-b-03-async-sdf-bake.md) — asynchronous, disk-cached mesh SDF bake.
- [lighting-block-b-04-froxel-fog.md](lighting-block-b-04-froxel-fog.md) — froxel fog correctness and sun shadowing.
- [lighting-block-b-04b-contact-shadows.md](lighting-block-b-04b-contact-shadows.md) — contact shadows: depth linearisation and march.
- [lighting-block-b-04c-depth-prepass.md](lighting-block-b-04c-depth-prepass.md) — the depth prepass that drew nothing.
- [lighting-block-b-05-layered-reflections.md](lighting-block-b-05-layered-reflections.md) — layered reflections (SSR → SDF → IBL).
- [lighting-block-b-05b-env-mis.md](lighting-block-b-05b-env-mis.md) — the environment-MIS specular that was black.
- [lighting-block-b-05c-sky-capture.md](lighting-block-b-05c-sky-capture.md) — the sky capture's sun intensity.
- [lighting-block-b-06-ddgi-dedup.md](lighting-block-b-06-ddgi-dedup.md) — DDGI dedup: one trace per probe ray, second bounce, TLAS reuse.
- [lighting-block-b-07-clustered-lights.md](lighting-block-b-07-clustered-lights.md) — clustered light lists.

## Side items

- [lighting-cluster-spot-cone.md](lighting-cluster-spot-cone.md) — the light cull assigns spots by their cone.
- [lighting-cvars-once.md](lighting-cvars-once.md) — every CVar registered once.
- [lighting-mesh-winding.md](lighting-mesh-winding.md) — sample meshes wound counter-clockwise rendered inside-out.
- [lighting-split-sum-mips.md](lighting-split-sum-mips.md) — split-sum specular on rough metals: mip curve and sky SH frame.
- [lighting-sponza-mesh-decals.md](lighting-sponza-mesh-decals.md) — Sponza's dirt decals as mesh decals.
- [lighting-sponza-recalibration.md](lighting-sponza-recalibration.md) — Sponza's ambient recalibrated after the sky capture fix.
