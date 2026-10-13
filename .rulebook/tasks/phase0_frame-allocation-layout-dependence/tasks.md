## 1. Localise
- [x] 1.1 A diagnostic layout shift... **Localized 2026-10-13** (architect analysis, docs/analysis/frame-allocation-layout-dependence/01-localization-and-fix.md): mechanism = D3D12MA ALLOCATOR_FLAG_DEFAULT_POOLS_NOT_ZEROED (dx12_device.cpp:704-709); removing the RTUVTestOutput allocation (scene_renderer_init.cpp:627-634, gated by IsReady not r.RayTracing.TestPass) relocates all later resources incl. SkyCubemap + frame-0 RG transients. Descriptor-index path ruled out.
- [x] 1.2 Region-dump per pass... **Ranked candidates** identified by reasoning (file:line cited in the doc): #1 PathTracer samples SkyCubemap in frame 0 before SkyCubemapCapture writes it (progressive mean retains it); #3 APVolume read before first dispatch; plus a transient read-before-write class. NOTE: the empirical per-pass region-dump diff under the layout shift is still OWED (see 3.3) because --rt-test gates only Execute, not the Init allocation.

## 2. Fix
- [x] 2.1 Initialise or clear the resource before its first read. **Done 2026-10-13**: sentinel guards added so the read falls back to the shader's existing sentinel until the resource is filled (steady state unchanged): #1 frame_lighting.cpp PT skyCubeSrvIndex + TLAS-path skyCubemapSrv gated on m_skyCubemapFilled (flat-skylight fallback, path_tracer_ps.hlsl:52); #3 frame_constants.cpp:1004 apVolumeSrvIndex gated on m_apVolumeFilled. Verified: vendored zig build exit 0; owned gtests (PathTracer/Atmosphere/Lighting/RenderGraphStates/GIOccluder) pass.

## 3. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 3.1 Update or create documentation covering the implementation
- [ ] 3.2 Write tests covering the new behavior
- [ ] 3.3 Run tests and confirm they pass. **OWED — empirical gate not run**: the pre-registered gate (references bit-identical WITH vs WITHOUT the allocation-layout shift) needs either an Init-skipping toggle or, better, a r.Debug.PoisonAllocations mode (architect recommendation). GBV/debug-layer deferred on this workstation (0x887A002D); run on a DX12-debug-layer-capable machine. Build + owned gtests are green.
