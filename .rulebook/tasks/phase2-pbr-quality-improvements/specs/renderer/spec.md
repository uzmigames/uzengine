# Spec Delta: renderer — PBR Quality Improvements

## ADDED Requirements

### Requirement: Scene Light Data Pipeline
The renderer MUST pass `LightState` (direction, color, intensity) collected from the active scene
to `StaticMeshRenderer::draw()` on every frame. The `LightFrameCB` GPU constant buffer SHALL be
populated from `LightState` values, not hardcoded defaults. Camera world position SHALL be
derived from the active camera transform and passed alongside the light state.

#### Scenario: Sun node drives shader lighting
Given a scene file with a `DirectionalLight` node named `Sun` with `light_intensity = 3.0`
  and `light_color = 1.0 0.95 0.88`
When the renderer draws a frame
Then the `LightFrameCB` light intensity slot SHALL equal `3.0`
  and the color slots SHALL equal `(1.0, 0.95, 0.88)`
  and the specular highlight SHALL shift position when the Sun node rotation changes

#### Scenario: Default light when no directional light exists
Given a scene file with no `DirectionalLight` node
When the renderer calls `light_collect_primary()`
Then the returned `LightState` SHALL use a safe fallback (e.g., direction `(0,-1,0)`,
  intensity `1.0`, color `(1,1,1)`) so the scene is still visible

#### Scenario: Camera position used for specular
Given a camera at world position `(0, 2, -4)`
When the renderer draws a PBR mesh
Then `LightFrameCB` camera position slots SHALL equal `(0.0, 2.0, -4.0)`
  so that the Fresnel and specular lobe are computed from the correct viewpoint

### Requirement: Parallax Occlusion Mapping
The PBR pixel shader SHALL implement Parallax Occlusion Mapping (POM) when a material provides
a displacement texture and a non-zero `parallaxScale`. POM MUST use a steep raymarching loop
with dynamic layer count (min 8, max 32 based on NdotV) followed by 4-step binary search
refinement. The resulting UV offset SHALL be applied to all downstream texture samples
(albedo, normal map, roughness, AO). POM SHALL be a no-op when `parallaxScale == 0.0` so that
materials without a displacement map incur no extra shader cost.

#### Scenario: POM active with displacement texture
Given a material with `displacement_texture = assets/textures/stonebricks_disp.png`
  and `parallax_scale = 0.04`
When the camera views the surface at a grazing angle
Then the mortar joints between bricks SHALL appear recessed relative to the brick faces
  and UV coordinates near silhouette edges SHALL be offset toward the view direction

#### Scenario: POM inactive without displacement texture
Given a material with no `displacement_texture` field (or `parallax_scale = 0.0`)
When the PBR pixel shader runs
Then the POM loop SHALL NOT execute (parallaxScale guard is false)
  and texture samples SHALL use the original interpolated UV unchanged

#### Scenario: POM layer count scales with view angle
Given `parallaxScale = 0.04` and a displacement texture
When `NdotV` equals `1.0` (camera looks straight at surface)
Then the POM loop SHALL use `minLayers = 8`
When `NdotV` approaches `0.0` (grazing angle)
Then the POM loop SHALL use up to `maxLayers = 32`

### Requirement: Displacement Texture Material Slot
The `.uzmat` material format SHALL support a `displacement_texture` path field and a
`parallax_scale` float field. The material loader MUST populate `MaterialData.displacement_path`
and `MaterialData.parallax_scale` when these keys are present. When absent, `parallax_scale`
SHALL default to `0.0` (POM disabled).

#### Scenario: Material file with displacement fields
Given a `.uzmat` file containing:
  `displacement_texture = assets/textures/stonebricks_disp.png`
  `parallax_scale = 0.04`
When `MaterialManager::load()` parses the file
Then `MaterialData.displacement_path` SHALL equal the resolved absolute path
  and `MaterialData.parallax_scale` SHALL equal `0.04f`

#### Scenario: Material file without displacement fields
Given a `.uzmat` file with no `displacement_texture` or `parallax_scale` keys
When `MaterialManager::load()` parses the file
Then `MaterialData.displacement_path` SHALL be an empty string
  and `MaterialData.parallax_scale` SHALL equal `0.0f`

## MODIFIED Requirements

### Requirement: StaticMeshRenderer Draw Interface
`StaticMeshRenderer::draw()` MUST accept `const LightState& light` and
`const uz::Vec3& cameraPos` parameters in addition to the existing `RhiCmdList` and
`Mat4 viewProj`. Callers SHALL provide these values from the renderer's per-frame light
collection and camera state. Hardcoded lighting constants in the draw method are FORBIDDEN.
