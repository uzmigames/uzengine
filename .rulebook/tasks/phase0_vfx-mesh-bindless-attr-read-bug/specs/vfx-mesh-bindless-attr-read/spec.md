# VFX Mesh Bindless Attribute Read — Spec Delta

## ADDED Requirements

### Requirement: Mesh-Instance CS Sees Spawn / Update Writes via Bindless SRV

The bindless SRV reads in `particle_mesh_instance_cs.hlsl` MUST observe
the values written by the preceding spawn / update compute dispatches
in the same frame, when accessed through
`ResourceDescriptorHeap[positionsSrvIndex]` (and the corresponding
indices for velocities, ages, lifespans, colors). After the inline VFX
simulation transitions the per-particle attribute buffers from
`UnorderedAccess` to `ShaderResource` state, the bindless reads in the
mesh-instance CS MUST return the data the spawn / update CS wrote, NOT
stale or garbage values.

#### Scenario: Asset-loaded mesh emitter renders discrete particles in scene

Given the demo runs `--vfx-asset vfx/tests/mesh_sort_shards.uzvfx --headless --screenshot <path> --screenshot-frame 90 --frames 120`
And the asset is in its committed state (`scale: [3.0, 3.0, 3.0]`, `velocity: [0, 0, 200]`, `sort_mode: ViewDepth`)
When the screenshot is captured at frame 90
Then the resulting PNG contains DISCRETE, IDENTIFIABLE tetrahedron mesh particles in the spawn region
And the sky+ground baseline composition is preserved (matches the `.sandbox/spline-now.png` overall scene structure)
And the screenshot does NOT show "tetrahedron faces engulfing the camera" or "uniform color filling the viewport"

#### Scenario: Encoded particle position reads as origin range

Given a diagnostic build of `particle_mesh_instance_cs.hlsl` that encodes the bindless-read `pos.xyz` into the per-instance `color` channel (mapping `[-1000, 1000] cm -> [0, 1]`)
And the demo runs the mesh emitter with the asset's committed `velocity: [0, 0, 200]` cm/s and lifetime 5 s
When the rendered screenshot is captured at frame 60
Then the encoded color must read as gray-with-blue-tint (NOT pure white)
And specifically `color.r ≈ 0.5` (`pos.x ≈ 0`), `color.g ≈ 0.5` (`pos.y ≈ 0`), `color.b ∈ [0.5, 0.55]` (`pos.z ∈ [0, 100]` cm)
And NEVER `color = (1, 1, 1)` (which would indicate `pos > 1000` saturation = garbage)

#### Scenario: GPU readback of attribute buffer matches expected spawn values

Given the demo runs with `UZ_VFX_DIAG_ATTR_READBACK` defined
And the asset emitter has spawned at least one particle by frame 5
When the per-frame readback fires immediately after the inline `UAV -> ShaderResource` transition
Then the readback reports `positions[0].xyz` close to `(0, 0, 0..50)` cm
And reports `lifespans[0]` in `[4.5, 5.5]` s (the asset's declared range)
And reports `ages[0]` < `lifespans[0]` (alive)

### Requirement: Mesh Particle Render Path Survives Visual A/B Without Asset Tuning

The visual A/B for mesh particles MUST pass without modifications to
the test asset. Tuning `scale`, `velocity`, `material.blendMode`, or
any other field of `mesh_sort_shards.uzvfx` to mask the rendering bug
is NOT acceptable. The asset's committed values are the contract.

#### Scenario: Reverting tuning experiments still produces visible particles

Given the asset is restored to its committed state from the
`feat/vfx-multi-emitter-priority` branch
When the demo runs and a screenshot is captured at frame 60
Then mesh particles render visibly in the scene without any author-side workarounds
