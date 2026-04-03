# Spec Delta: UE5 Rendering Parameter Parity

## MODIFIED Requirements

### Requirement: Atmosphere Mie Absorption
The system SHALL use Mie absorption coefficient `4.44e-4 km^-1` matching UE5 SkyAtmosphereComponent.cpp:114.

#### Scenario: Mie absorption matches UE5
Given the default SkyAtmosphereComponent
When the atmosphere renders
Then Mie absorption MUST be `4.44e-4` (NOT `4.4e-3`)

### Requirement: Diffuse BRDF Model
The system SHALL use Disney Burley diffuse BRDF matching UE5 BRDF.ush:177-189 instead of pure Lambertian.

#### Scenario: Burley diffuse retroreflection
Given a rough surface (roughness=0.8) at grazing angle (NdotV=0.1)
When the diffuse BRDF is evaluated
Then the result MUST be brighter than Lambertian due to retroreflection term

### Requirement: Directional Light Default Intensity
The system SHALL default directional light intensity to `10.0` matching UE5 DirectionalLightComponent.cpp:1004.

#### Scenario: Default outdoor scene brightness
Given a new directional light with default parameters
When it illuminates a white sphere
Then the illumination MUST match UE5's default directional light brightness

### Requirement: Geometry Function
The system SHALL use Smith Joint Approximation (Heitz 2014) matching UE5 BRDF.ush:346-352 instead of separate Schlick terms.

#### Scenario: IBL specular at high roughness
Given a surface with roughness=0.9
When the IBL specular is evaluated
Then the geometry term MUST use SmithJointApprox, not separate SchlickGGX

### Requirement: IBL k-Remapping
The system SHALL use `k = roughness^2 / 2` for IBL geometry evaluation, and `k = (roughness+1)^2 / 8` for direct lighting, matching UE5 BRDF.ush:329.

#### Scenario: k differs between direct and IBL
Given roughness=0.5
When k is computed for direct lighting
Then k MUST equal (0.5+1)^2/8 = 0.28125
When k is computed for IBL
Then k MUST equal 0.5^2/2 = 0.125

## ADDED Requirements

### Requirement: ACES FilmToneMap
The system SHALL implement UE5's FilmToneMap with 5 artist parameters: FilmSlope (default 0.88), FilmToe (default 0.55), FilmShoulder (default 0.26), FilmBlackClip (default 0.0), FilmWhiteClip (default 0.04).

#### Scenario: Tonemap with default parameters
Given default FilmToneMap parameters
When an HDR value of 1.0 is tonemapped
Then the output MUST match UE5's FilmToneMap output for the same input

### Requirement: GTAO Screen-Space Ambient Occlusion
The system SHALL implement Ground Truth Ambient Occlusion as a compute shader post-process pass.

#### Scenario: AO darkens contact areas
Given a sphere resting on a flat plane
When GTAO is computed
Then the contact area between sphere and plane MUST show darkened occlusion

### Requirement: Bloom Post-Process
The system SHALL implement multi-pass gaussian bloom with configurable threshold and intensity.

#### Scenario: Bright specular highlights bloom
Given a metallic sphere with specular highlight exceeding bloom threshold
When bloom is applied
Then the highlight MUST produce a soft glow halo around the bright area

### Requirement: Multi-Scatter Energy Compensation
The system SHALL compensate for energy loss in single-scatter BRDF via Karis 2013 multi-scatter approximation using the BRDF LUT.

#### Scenario: Rough metals preserve energy
Given a rough metallic surface (roughness=0.9, metallic=1.0)
When IBL specular is evaluated
Then multi-scatter compensation MUST prevent energy darkening at high roughness
