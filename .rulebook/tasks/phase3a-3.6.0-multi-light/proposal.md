# Proposal: Multi-Light Shading — Point + Spot Lights (Phase 3A)

## Why
The forward PBR pass evaluates only a single directional light with constant ambient. RenderWorld already stores PointLight and SpotLight proxy data (intensity, color, range, cone angles) but the shader ignores them entirely. The 0.1 POC supported 8 point lights and spot lights with inverse-square attenuation, colored lighting, and smooth boundary falloff. Without multi-light support, scenes look flat and unrealistic — this is the single biggest visual gap vs 0.1.

## What Changes

### New Systems
1. **LightBuffer** — Structured GPU buffer holding up to 8 active lights (type, position, direction, color, intensity, range, cone angles) uploaded per frame
2. **Point light evaluation** — Inverse-square falloff with smooth boundary attenuation in mesh_ps.hlsl, accumulating radiance from all active point lights
3. **Spot light evaluation** — Inner/outer cone angle falloff multiplied with point light attenuation, directional cone masking
4. **CPU light culling** — Distance-based culling against camera frustum, selecting top-N lights by screen contribution (intensity / distance^2)
5. **Multi-light demo** — Scene with directional sun + colored point lights + spot lights demonstrating dynamic lighting

### Refactoring
- PerFrameCB expands from single lightDir/lightColor/ambientColor to a light count + light array structure
- mesh_ps.hlsl lighting loop replaces single-light evaluation with N-light accumulation
- Deferred lighting pass (3.3.0) updated to also iterate the light array

## Impact
- Affected specs: `docs/engine/12-rendering/06-lighting.md`
- Affected code: `runtime/shaders/runtime/mesh_ps.hlsl`, `runtime/engine/renderer/constant_buffers.h`, forward/deferred passes
- Breaking change: YES — PerFrameCB layout changes (light array replaces single light fields)
- User benefit: Realistic multi-source lighting, colored point lights, flashlight/spotlight effects
- Enables: IBL (3.7.0), shadow atlas for point/spot lights (future)
