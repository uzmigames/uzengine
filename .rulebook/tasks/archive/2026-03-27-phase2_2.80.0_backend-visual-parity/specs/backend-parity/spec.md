# Backend Visual Parity — Spec Delta

## ADDED Requirements

### Requirement: SM6.0 SSR/SSGI Binding
The deferred lighting shader SM5/SM6.0 path MUST bind SSR and SSGI textures via explicit register declarations and produce the same specular/diffuse indirect contributions as the SM6.6 bindless path.

#### Scenario: SSR composited on reflective surface
Given a scene with a metallic floor (roughness < 0.3)
When rendered on DX12 SM6.0
Then screen-space reflections SHALL appear on the floor
And the reflection SHALL match the SM6.6 output within PSNR > 35 dB

#### Scenario: SSGI bounce lighting in shadowed area
Given a scene with a red wall next to a white floor in shadow
When rendered on DX12 SM6.0
Then indirect red bounce light SHALL be visible on the white floor
And the GI contribution SHALL match SM6.6 output

### Requirement: GBuffer Metallic Sampler Parity
The GBuffer pixel shader SM5 path MUST sample the metallic texture with the same sampler and mip bias as the SM6.6 path (linearSampler with mipBias, NOT clampSampler at mip 0).

#### Scenario: Tiling metallic material at distance
Given a tiling metal material with UV > 1.0
When viewed from 500cm distance on SM6.0
Then the metallic channel SHALL NOT exhibit clamp artifacts at tile boundaries
And mip selection SHALL match the SM6.6 path (no aliasing)

### Requirement: Opacity Texture Binding
The GBuffer pass SM5 path MUST bind the opacity texture at register t6 when a material has an opacity map, so alpha-masked materials render correctly.

#### Scenario: Alpha-masked foliage material
Given a material with an opacity mask texture (leaf cutout)
When rendered on DX12 SM6.0 or DX11
Then masked pixels SHALL be discarded (alpha test)
And the silhouette SHALL match the SM6.6 output

### Requirement: DX11 Sampler Completeness
The DX11 backend MUST create and bind all 4 static samplers (s0 anisotropic wrap, s1 PCF shadow, s2 bilinear clamp, s3 bilinear wrap) matching the DX12 root signature.

#### Scenario: IBL BRDF LUT sampling on DX11
Given an IBL-lit scene with the BRDF LUT bound at s2
When rendered on DX11
Then the specular IBL SHALL use bilinear clamp addressing
And the result SHALL match DX12 SM6.6 specular response

### Requirement: DX11 TAA
The DX11 backend MUST support temporal anti-aliasing, either via the existing compute TAA path or a PS-based fallback.

#### Scenario: TAA reduces aliasing on DX11
Given a scene with thin geometry (wires, grass)
When rendered on DX11 with TAA enabled
Then temporal stability SHALL be visible across frames
And edge aliasing SHALL be significantly reduced vs no-TAA

### Requirement: DX11 Bloom
The DX11 backend MUST support bloom effects, either via compute bloom or a PS-based Kawase blur chain.

#### Scenario: Bright emissive object produces bloom on DX11
Given a scene with an emissive light source (intensity > 5.0)
When rendered on DX11
Then a soft glow SHALL surround the bright source
And the bloom SHALL match DX12 SM6.6 output in character

### Requirement: DX11 SRV Multi-Bind
The DX11 command list MUST support binding SRV descriptors to any slot index (0-15), not just slot 0.

#### Scenario: Deferred lighting with 12 SRV slots on DX11
Given the deferred lighting pass requiring t0-t13
When all 14 textures are bound via SetFragmentTexture
Then each slot SHALL contain the correct texture
And the lighting output SHALL match DX12 SM6.6

### Requirement: DX11 3D Texture Support
The DX11 backend MUST support creating and sampling 3D textures for color grading LUT.

#### Scenario: Color grading LUT applied on DX11
Given a 32x32x32 color grading 3D LUT
When the tonemap pass runs on DX11
Then the color grade SHALL be applied to the final image
And the output SHALL match DX12 SM6.6 color grading
