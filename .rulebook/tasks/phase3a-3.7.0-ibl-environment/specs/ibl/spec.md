# IBL Environment Lighting — Spec Delta

## ADDED Requirements

### Requirement: BRDF Integration LUT
The engine MUST generate a 512x512 R16G16 BRDF integration lookup table via compute shader at engine initialization, encoding the split-sum approximation for varying NdotV (x-axis) and roughness (y-axis).

#### Scenario: LUT Generation
Given the engine initializes with IBL support enabled
When the BRDF LUT bake executes
Then a 512x512 R16G16 texture SHALL be created
And the texture SHALL be reusable across all materials without re-baking

### Requirement: Irradiance Cubemap Convolution
The engine MUST convolve an HDR environment cubemap into a low-frequency irradiance cubemap using cosine-weighted hemisphere sampling for diffuse IBL contribution.

#### Scenario: Irradiance Bake
Given an HDR environment cubemap is loaded
When irradiance convolution executes
Then a 64x64 per-face irradiance cubemap SHALL be produced
And sampling it with a surface normal SHALL return the average incoming radiance from that hemisphere

### Requirement: Prefiltered Specular Cubemap
The engine MUST generate a mip-chain cubemap where each mip level represents increasing roughness, using GGX importance sampling with at least 1024 samples per texel.

#### Scenario: Mip Chain Generation
Given an HDR environment cubemap of 256x256 per face
When prefilter bake executes
Then 5 mip levels SHALL be generated (256, 128, 64, 32, 16)
And mip 0 SHALL be near-mirror reflection
And mip 4 SHALL be heavily blurred (roughness ~1.0)

### Requirement: IBL PBR Integration
The pixel shader MUST replace constant ambient lighting with IBL sampling when SkyLight cubemaps are bound: diffuse from irradiance cubemap, specular from prefiltered cubemap + BRDF LUT.

#### Scenario: Metal Sphere Reflection
Given a fully metallic sphere (metallic=1.0, roughness=0.1) with IBL active
When rendered
Then the sphere surface SHALL show sharp environment reflections from the prefiltered mip 0

#### Scenario: Rough Dielectric
Given a dielectric surface (metallic=0.0, roughness=0.8) with IBL active
When rendered
Then the surface SHALL show soft ambient color from the irradiance cubemap
And specular contribution SHALL be minimal and blurred

#### Scenario: No SkyLight Fallback
Given no SkyLight is present in the scene
When the pixel shader evaluates ambient
Then it SHALL fall back to the constant ambientColor from PerFrameCB
