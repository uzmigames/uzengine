# Proposal: Froxel fog correctness and sun shadowing (lighting block B, item 4)

## Why
The froxel volumetric fog exists (`r.VolumetricFog`, `passes/atmosphere/volumetric_fog_pass.*`,
three shaders) but no scene turns it on, and a read-only map of it on 2026-10-06 found that it
cannot have worked as written:

- Three constant buffers disagree between C++ and HLSL (confirmed with DXC's layout comments):
  `VolumetricFogCB` (`float3 emissive` lands at 192 in HLSL, 184 in C++: the shader reads emissive
  = (0, 0, nearFadeInDistance) and a zero start distance), `HeightFogCB` (`float _fogPad2[2]` takes
  two 16-byte rows, shifting everything from `skyAtmContribColorScale` on by 32 bytes: the height
  fog's sun lobe points along (1, 1, 1) and its sky and sun tints read other fields) and `CloudCB`
  (`float3 g_windVelocity` moves from 152 to 160, shifting the tail by 8 bytes; the shader-side
  buffer is 264 bytes against 256 uploaded).
- The inject shader reconstructs froxel positions by feeding `1 - depth / farZ` to the inverse
  reversed-Z projection, which puts every froxel between 5 and 60 cm from the camera.
- The sun is applied twice (`sunColor * sunIntensity`, where the colour already carries the
  intensity: 16.9x too bright for Sponza's sun) and never shadowed: no cascaded-shadow lookup, no
  light shafts, although the shadow atlas and its constant buffer are ready when the fog passes run.
- The assessment lists "fog without sun shadow (no god rays), sun applied twice; clouds without
  temporal and unread shadow parameters" as the next structural item after the GDF and the bake.

## What Changes
- A layout harness (`scripts/rendering/test_cbuffer_layouts.py`) that compiles each shader with DXC, parses
  the cbuffer offsets from its layout comments and compares them with the C++ struct (extracted
  from the header and compiled with `offsetof`), plus `static_assert`s of the offsets in the three
  headers. The three buffers are made to agree by declaring scalars where HLSL would pad a
  `float3` or an array (the engine's own rule for `RadianceCacheTraceConstants`).
- Froxel placement: world position = camera + ray direction × view depth (ray from the unjittered
  inverse view-projection); the composite linearises depth with the camera's far plane.
- Sun once; the sun in-scatter is shadowed by the cascaded shadow maps (a one-tap volume variant
  of the cascade walk), with `r.VolumetricFog.SunShadow` to switch it off for A/B runs. The inject
  dispatch covers Z directly instead of looping 64 slices per thread.
- A Sponza fog variant (scene + project) and `scripts/rendering/test_froxel_fog.py` with the gates below.
- Out of scope, documented: the height fog / froxel fog double counting over the froxel range, the
  per-centimetre density unit, point/spot injection, temporal reprojection, sky pixels, clouds'
  temporal accumulation and unread parameters (only their constant-buffer layout is fixed here).

## Impact
- Affected specs: froxel-fog-correctness (new); docs/specs/rendering/renderer.md (fog pipeline)
- Affected code: runtime/shaders/runtime/atmosphere/volumetric_fog_{inject_cs,scatter_cs,composite_ps}.hlsl, height_fog_ps.hlsl, cloud_raymarch_cs.hlsl; runtime/shaders/runtime/lighting/shadow.hlsli (volume helper); runtime/engine/renderer/passes/atmosphere/volumetric_fog_pass.{h,cpp}; runtime/engine/renderer/frame/frame_atmosphere_passes.cpp; runtime/engine/renderer/core/params/constant_buffers.h; runtime/engine/renderer/passes/atmosphere/cloud_pass.cpp; new scripts/rendering/test_cbuffer_layouts.py, scripts/rendering/test_froxel_fog.py, scripts/rendering/sponza_fog.py
- Breaking change: NO for scenes without fog (the passes do not run). Scenes with a height fog see its sun lobe and tints read from the right fields for the first time (a bug fix, gated by F1).
- User benefit: a volumetric fog that places its froxels where the camera looks, lit by the sun once and shadowed by the scene, measured, and three constant buffers that cannot silently drift again.
