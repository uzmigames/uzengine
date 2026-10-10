# Scene Loader — Spec Delta

## ADDED Requirements

### Requirement: Directional Light Full Field Parsing
The scene loader SHALL parse `castShadows`, `atmosphereSunLight`, `lightSourceAngle`,
`shadowCascades`, and `dynamicShadowDistance` from directional light JSON, in addition
to the existing `color` and `intensity` fields. When a field is absent, the loader
MUST preserve the component default.

#### Scenario: Explicit directional fields are honored
Given a scene JSON with a directional light containing `"light_source_angle": 0.53` and `"atmosphere_sun_light": true`
When the scene is loaded
Then the DirectionalLightComponent SHALL have `lightSourceAngle == 0.53` and `atmosphereSunLight == true`

#### Scenario: Absent fields keep component defaults
Given a scene JSON with a directional light containing only `color` and `intensity`
When the scene is loaded
Then `lightSourceAngle` SHALL remain at its component default (1.2)
And `atmosphereSunLight` SHALL remain at its component default (true)

#### Scenario: Unknown light keys warn
Given a scene JSON with a directional light containing an unrecognized key
When the scene is loaded
Then the loader SHALL emit a warning naming the unrecognized key
And loading SHALL succeed

### Requirement: Scene-Serializable Light Key Naming
The loader SHALL accept the snake_case keys used by existing scene files
(`cast_shadows`, `atmosphere_sun_light`, `light_source_angle`) and MUST map them to
the corresponding component fields.

#### Scenario: Snake-case keys from existing scenes
Given `sponza.uzscene` with `"cast_shadows": true` and `"light_source_angle": 0.53`
When the scene is loaded
Then the component SHALL reflect both values
