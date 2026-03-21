# G-Buffer Layout for Deferred PBR (5 Render Targets)

**Category**: architecture
**Tags**: rendering, deferred, gbuffer, pbr, layout

## Description

Standard G-Buffer layout: RT0 (RGBA8) = BaseColor.rgb + Metallic.a, RT1 (RGB10A2) = WorldNormal (octahedral encoding) + Roughness, RT2 (RGBA8) = Emissive.rgb + AO.a, Depth (D32_FLOAT) = Scene depth, Velocity (RG16_FLOAT) = Screen-space velocity for TAA/motion blur. Lighting pass reconstructs world position from depth via fullscreen quad. This layout follows UE5's G-Buffer conventions.

## Example

// G-Buffer write (geometry pass pixel shader)\noutput.GBuffer0 = float4(baseColor, metallic);\noutput.GBuffer1 = float4(EncodeOctahedral(worldNormal), roughness, 0);\noutput.GBuffer2 = float4(emissive, ao);\n\n// G-Buffer read (lighting pass)\nfloat3 baseColor = GBuffer0.Sample(uv).rgb;\nfloat metallic = GBuffer0.Sample(uv).a;\nfloat3 normal = DecodeOctahedral(GBuffer1.Sample(uv).rg);

## When to Use

When implementing new deferred rendering passes. When adding new material properties (must fit existing RT slots or migrate to bindless). When debugging G-Buffer output via debug view modes.

## When NOT to Use

For forward-rendered objects (translucents, special materials). For compute-only passes.
