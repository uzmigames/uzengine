# VFX Mesh Particle Visibility — Spec Delta

## ADDED Requirements

### Requirement: Asset-Loaded Mesh Emitter Produces Visible Output

Mesh emitters loaded via `UzfxLoader::InstantiateEmitter` from a
`.uzvfx` asset path MUST produce visibly-rendered particles in the
final HDR composite, equivalent in visibility to preset emitters
configured through `ApplyPreset_*` functions. Visibility is verified
by a screenshot pair (pre-fix vs post-fix) showing the same camera
view with non-zero pixel difference in the spawn region.

#### Scenario: Cyan shards visible in mesh_sort_shards.uzvfx run

Given the demo is launched with `--vfs-root sample/content --vfx-asset vfx/tests/mesh_sort_shards.uzvfx --headless --screenshot <path> --screenshot-frame 90 --frames 120`
When the screenshot is captured at frame 90
Then the resulting PNG contains visibly-rendered cyan tetrahedron mesh shards in the spawn region
And `--pixel-debug` at the spawn centroid reports HDR RGB values distinct from the sky-gradient baseline at the same pixel without the asset

#### Scenario: Asset emitter inherits ParticleEmitter::initialColor

Given a `.uzvfx` asset that does NOT specify `initial_color` in any module
When `UzfxLoader::InstantiateEmitter` populates the runtime `ParticleEmitter`
Then `emitter.initialColor` retains its struct-default value `{1.0f, 0.9f, 0.6f, 1.0f}`
And the spawn CS's `cbInitialColor` is bound from this field, not from `{0,0,0,0}`

#### Scenario: Asset emitter ParticleSpawnCS dispatch fires

Given a mesh-render emitter loaded from `vfx/tests/mesh_sort_shards.uzvfx`
When the per-frame VFX system runs the spawn pass
Then `RegisterParticleSpawnPass` (or equivalent) logs the dispatch group count and CB binding for the emitter at `Info` level on first execution
And the per-particle `colors` SoA buffer contains non-zero RGBA values for alive particles after frame 1
