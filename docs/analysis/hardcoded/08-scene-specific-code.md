# 08 — Scene-Specific References in Runtime Code

**Severity:** MEDIUM
**File focus:** Engine code mentioning `Sponza`, `Mushroom`, `fbx_test`, or other sample-specific content

The runtime should have **zero knowledge** of specific sample assets. Comments and code that reference Sponza / Mushroom / fbx_test are signals that the feature was developed against one scene and not validated elsewhere.

## Findings

| # | Category | File:Line | Snippet | Why Bad | Fix |
|---|----------|-----------|---------|---------|-----|
| 8.1 | Sponza in shadow cascade | [shadow_cascade.cpp:305](../../../runtime/engine/renderer/shadows/shadow_cascade.cpp#L305) | `// (Sponza, 2026-04-10). Rotating the camera moved the sphere` | Comment ties bug context to one scene | Describe bug behavior scene-agnostically |
| 8.2 | Sponza in PCSS shader | [shadow_pcss.hlsli:87](../../../runtime/shaders/runtime/lighting/shadow_pcss.hlsli#L87) | `/// (Sponza bug 2026-04-10).` | Sample-specific reference in shader | Generalize comment |
| 8.3 | Exposure Sponza calibration | [frame_constants.cpp:73-75](../../../runtime/engine/renderer/frame/frame_constants.cpp#L73-L75) | `// matched to indoor Sponza skylight 0.35 / sun 18` | Default value tuned for one scene | See 01-rendering-defaults.md |
| 8.4 | Contact shadow Sponza | [frame_constants.cpp:40-41](../../../runtime/engine/renderer/frame/frame_constants.cpp#L40-L41) | `// large interiors like Sponza` | Comment reveals tuning bias | See 01-rendering-defaults.md |
| 8.5 | SSGI Sponza comment | [ssgi_renderer.cpp:35](../../../runtime/engine/renderer/gi/ssgi_renderer.cpp#L35) | `// Sponza columns ~60cm wide` | Defaults justified by one scene's geometry | See 01-rendering-defaults.md |
| 8.6 | Scene tuning defaults | Various | Skylight, saturation, tonemap all Sponza-matched | Cumulative Sponza bias | See 01-rendering-defaults.md |

## Grep Audit Commands

Run periodically (suggested CI check):

```bash
# Any reference to sample scene names in runtime code
grep -rn -i 'sponza\|mushroom\|fbx_test' runtime/engine runtime/shaders

# Scene-specific absolute paths
grep -rn 'sample/\|sample\\\\' runtime/engine runtime/tools

# Sample-specific entity/asset names
grep -rn -i 'mainsun\|mushroommonster\|lion_statue' runtime/engine
```

Expected output after cleanup: **zero matches in `runtime/engine` and `runtime/shaders`**. Matches are acceptable in:
- `sample/` (project content)
- `tests/` (fixture names)
- `docs/` (this analysis directory)

## Generalization Guide

When a bug is fixed against a specific scene, rewrite the comment to describe the *behavior*, not the *scene*:

### Before
```cpp
// Sphere-bounding sweep produces visible jitter when the sun grazes
// the east wall of Sponza at sunset (2026-04-10).
```

### After
```cpp
// Sphere-bounding sweep produces visible jitter when the sun direction
// is near-parallel to cascade slice boundaries at grazing angles.
// Repro: rotate directional light to ~5° elevation over any scene with
// long horizontal surfaces.
```

The second form:
1. Describes the *condition* (grazing angle + horizontal surfaces)
2. Provides *reproducible* repro without the sample
3. Ages gracefully — still meaningful when the sample changes

## Verifying with CI

Add to the project's lint step:

```bash
#!/usr/bin/env bash
# tools/ci/check-no-sample-refs.sh
set -e

BAD=$(grep -rni -E 'sponza|mushroom|fbx_test' \
    runtime/engine runtime/shaders runtime/tools \
    --include='*.cpp' --include='*.h' --include='*.hlsl' --include='*.hlsli' \
    | grep -v '// OK: ' || true)

if [ -n "$BAD" ]; then
    echo "ERROR: Sample-specific references found in runtime:"
    echo "$BAD"
    exit 1
fi
```

Allow annotated exceptions with `// OK: <reason>`, e.g.:
```cpp
// OK: test fixture name intentionally references sponza scene
const char* kSponzaTestScene = "tests/fixtures/sponza_mini.uzscene";
```

## Follow-Up Audit

After this cleanup, a second sweep should check for:
- `MainCamera`, `SunLight`, `Player` — sample-specific entity names baked into runtime
- Hardcoded tag strings (`"player"`, `"enemy"`, `"interactable"`) in game systems
- Hardcoded event names (`"game.started"`, `"level.loaded"`)

These patterns weren't found in this pass but are common leakage points in ECS / scripting systems.
