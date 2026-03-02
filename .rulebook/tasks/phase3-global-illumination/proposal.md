# Proposal: phase3-global-illumination

## Why

Direct lighting (from `CDirLight`, `CPointLight`, `CSpotLight`) alone produces scenes where all surfaces not directly lit are pure black. Real environments have indirect light: light bounces off walls, floors, and ceilings to illuminate occluded areas. Dynamic Global Illumination (DDGI — Dynamic Diffuse GI) using a probe grid solves this for fully dynamic scenes: an array of probes distributed through the scene each fire rays to sample incoming radiance, accumulate that into irradiance data, and the PBR lighting shader samples the nearest probes to add indirect illumination.

## What Changes

- **`ecs/components.h`** — `CDdgiVolume` component: `probe_grid_extent`, `probe_spacing`, probe counts XYZ
- **`src/engine/renderer/passes/ddgi_pass.h/cpp`** — probe ray dispatch (compute), radiance accumulation, irradiance encoding
- **`shaders/ddgi_probe_trace.hlsl`** — ray casting compute shader (DXR or analytical ray-box intersection)
- **`shaders/ddgi_irradiance_blend.hlsl`** — irradiance update compute shader
- **`shaders/include/ddgi_sample.hlsli`** — irradiance sampling for PBR lighting shader
- **Persistent double-buffered probe textures** — `RgResource` persistent in RenderGraph

## Impact

- Affected specs: `docs/specs/renderer.md` (GI section)
- Dependencies: `phase1-render-graph` (compute passes), `phase2-pbr-renderer` (PBR shader must expose GI plug point)
- Breaking change: NO — additive `CDdgiVolume` component; PBR shader has `#ifdef USE_DDGI` fallback
- User benefit: full dynamic GI for indoor and outdoor environments; zero bake time; AI can place `CDdgiVolume` nodes
