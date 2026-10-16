# Item 1.2: wall with opening

2026-10-13. Two sequential shipped PROJECT captures completed, exit 0.
This is a measurement, not a leakage/adoption verdict.

```bash
python scripts/rendering/run_probe_opening.py --out .screenshots/rc-probe-support-followups/item12
python scripts/rendering/run_probe_opening.py --out .screenshots/rc-probe-support-followups/item12 --reuse
```

The second command only reanalyses retained data to add both-side origin counts
and log-scale diffuse images; it does not invoke the engine and writes a distinct
`.reuse.json` report. Original report:
`.screenshots/rc-probe-support-followups/item12/probe_opening.json`.

Executable inherited from both manifests: `fnv1a64:534537d32570707f`.
Resolution 640x360, 210 engine frames, measurement frames 200-201.
All fixture settings and labels are defined by `probe_side_reference.py` and
`run_probe_opening.py`; classification 3, relocation 1, visible seeds 1, GDF 256,
mesh SDF 128. No runtime changes were made.

## Fixture

CORE east wall x=[350,450] cm; opening width 280 cm at the room y centre,
z=[0,300] cm. Remaining jambs and lintel are closed meshes with outward faces
and the fixture's .5 albedo. Ground and other rooms are unchanged.

Generated fixtures:

- `scripts/rendering/probe-opening-ext.uzproject`
- `scripts/rendering/probe-opening-core.uzproject`
- `sample/content/scenes/probe-opening-ext.uzscene`
- `sample/content/scenes/probe-opening-core.uzscene`
- `sample/content/meshes/reference/probe-opening-openingsouth.uzmesh`
- `sample/content/meshes/reference/probe-opening-openingnorth.uzmesh`
- `sample/content/meshes/reference/probe-opening-openinglintel.uzmesh`

## Actual measurements

HDR-linear diffuse-indirect luminance, geometry labels from captured world positions:

| Label | Mean | Unsupported |
|---|---:|---:|
| CORE interior face | 0.04230455777952832 | 0% |
| CORE interior floor | 0.06574154656582984 | 0% |
| CORE all interior labels | 0.08834625448339826 | 5.774465114753791% |
| CORE exterior lit face | 0.3221526445693952 | 0.013190871916633689% |
| CORE exterior lit ground | 0.17505323565087894 | 0% |

The first-frame interior floor has 26,326 labelled pixels. **8,553** have at
least one valid, front-facing interpolation-corner trace origin more than
100 cm away whose segment intersects a remaining east-wall box: **9,518**
such geometric segments in total. The exact analytic opening control confirms
a central segment passes through the opening, whereas jamb/lintel segments hit.

## Parent judgment flag: contribution is not established

The geometric count does **not** reconstruct the relocated probe's guard plane,
the shader's exact trilinear/facing/normalisation weights, SH clamping, or the
actual radiance share contributed by that probe. It therefore does not prove
that these blocked candidates contributed to the measured pixels.

The increased interior mean is not a leak proof either: an opening physically
admits light. Establishing error would require an independently solved opening
irradiance and/or measured per-corner contributions including the guard and
weights. This missing observation, not just a policy choice, is flagged to the
parent. No defect/adoption conclusion is made.
