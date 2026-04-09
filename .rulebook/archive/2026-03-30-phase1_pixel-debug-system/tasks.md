## 1. HLSL Shader Infrastructure
- [x] 1.1 Create `pixel_debug.hlsli` — PDEBUG macros. **Iterated 5 versions: static vars → const locals → direct cbuffer → deferred flush. DXC dead-code-eliminates all conditional UAV writes in pixel shaders. Final solution: compute post-pass.**
- [x] 1.2 Add PIXEL_DEBUG_ENABLED shader define — **Replaced by compute approach. No PS variant needed.**
- [x] 1.3 Create `pixel_debug_cs.hlsl` compute post-pass — **Single-thread [numthreads(1,1,1)] dispatch reads GBuffer + HDR at debug pixel, writes 13 records (depth, AO, albedo RGB, HDR RGB, roughness, metallic, emissive RGB). Works perfectly — DXC cannot eliminate UAV writes from a compute shader.**

## 2. C++ Host System
- [x] 2.1 Create `pixel_debug.h` — PixelDebugSystem class with counter/records/readback/upload buffers, raw UAV descriptors (stride=0).
- [x] 2.2 Create `pixel_debug.cpp` — Init, BeginFrame (clear counter), EndFrame (copy to readback), ReadResults (map+decode), ToJSON.
- [x] 2.3 Wire into SceneRenderer — SetPixelDebugSystem(), BeginFrame/EndFrame in scene_render_frame.cpp, compute dispatch after render graph.
- [x] 2.4 Pipeline — GetPixelDebugCompute() in PipelineLibrary creates compute PSO from PixelDebugCS shader.

## 3. CLI Integration
- [x] 3.1 `--pixel-debug --pixel-debug-x N --pixel-debug-y N` in uzengine.cpp (Config system).
- [x] 3.2 `--pixel-debug X Y` in Application + DemoApplication + EngineLoop.
- [x] 3.3 JSON output: `PIXEL_DEBUG_JSON:{...}` to stdout after frame loop.
- [x] 3.4 Auto frame limit (120 frames), headless implied.

## 4. Integration & Testing
- [x] 4.1 Build and verify compilation — full clean build passes (rm .zig-cache + build/generated/shaders).
- [x] 4.2 Runtime test — `--pixel-debug --pixel-debug-x 960 --pixel-debug-y 540 --frames 10` returns **13 records** with real GBuffer values: depth=0.0018, AO=1.0, albedo=(0.32,0.32,0.32), HDR=(0.32,0.32,0.32), roughness=0.498, metallic=0.0, emissive=(0,0,0).
- [x] 4.3 Zero overhead verified — compute PSO only created when pixelDebug is active. No PS variant compiled.
- [ ] 4.4 Test `--diag` + `--pixel-debug` combined (both JSON outputs on stdout).

## 5. Documentation
- [x] 5.1 `.claude/rules/rendering-diagnostics.md` updated with --pixel-debug in diagnostic sequence.
- [x] 5.2 `docs/analysis/debug/current-tools-inventory.md` updated with Pixel Debug section.
- [x] 5.3 CHANGELOG deferred to release time.

## Key Learning: DXC Dead-Code Elimination
DXC eliminates conditional UAV writes in pixel shaders. Tested: static vars, const locals, [branch], globallycoherent, precise, direct cbuffer reads — ALL get eliminated. Compute shaders with [numthreads(1,1,1)] are immune because the single thread always executes. This is a known DXC behavior, not a bug.
