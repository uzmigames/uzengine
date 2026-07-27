# Verify existing infra before building: bytecode cache + cook heuristic traps
**Source**: manual
**Date**: 2026-07-27
**Related Task**: phase0_scene-load-optimization
**Tags**: measurement, caching, texture-compression, testing
1) The material-PSO "missing bytecode disk cache" hypothesis was WRONG: ShaderPipeline::LoadBytecodeFromSource already content-hashes (source+defines+backend+bindless) into cache/shaders/<backend>/. Verified empirically by counting cache .bin files across a run (105 -> 105) — the remaining warm-run cost is driver-side PSO creation, a different problem (ID3D12PipelineLibrary). Lesson: measure cache hits before designing a cache.

2) Filename-heuristic texture cooking has a channel-count trap: "metallicRoughness" matched the "roughness" rule and cooked to single-channel BC4, which would zero the G(roughness)/B(metallic) channels the material graphs sample (srcPin G/B). Any combined-map convention needs an explicit multi-channel rule BEFORE the single-channel rules. Verify with the shader sampling code (gbuffer_ps.hlsl reconstructs normal Z from RG, so BC5 normals are safe here).

3) uzengine.exe --headless means "create device and exit" — NOT hidden-window rendering (that is the demo convention). Load-time measurements must run windowed; wall-clock A/B with --frames 1 also lies because loading is amortized over ~35 frames. Use --verbose log timestamps (LoadScene / Pre-load complete / last "async texture uploaded").

4) Piped shell exits lie: `test.exe | tail` returns tail's exit 0 even when the exe fails to load DLLs. test_runtime.exe needs build/bin cwd + ffmpeg/bin on PATH.

5) Pre-existing test failures to not confuse with regressions: VFX spec tests expect Y-up/meters vs Z-up/cm code; SceneLoaderTest camera "active" default (loader defaults true at scene_loader.cpp:280, component says false); RuntimeViewportTest crashes on the FontAtlas D3D12MA teardown leak (reproduced on pre-change binary).