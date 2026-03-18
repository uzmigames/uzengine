<!-- doc-meta
title: Deep Audit — C++ Render Pipeline Wiring
author: cpp-core-expert
date: 2026-03-18
status: complete
-->

# Deep Audit: C++ Render Pipeline Wiring, Barriers, CB Layouts, and Pass Ordering

Audited files: `scene_render_frame.cpp`, `frame_setup.cpp`, `frame_shadows.cpp`,
`frame_geometry.cpp`, `frame_lighting.cpp`, `frame_postprocess.cpp`,
`frame_overlay.cpp`, `scene_lifecycle.cpp`, `scene_renderer.h`,
`constant_buffers.h`, `per_frame_params.h`, `shadow_data_params.h`,
`velocity_pass.h/.cpp`, `local_light_shadow_atlas.h/.cpp`,
and corresponding HLSL shaders.

---

## Summary

Pipeline is well-structured overall. 5 issues found: 1 CRITICAL, 1 HIGH, 1 MEDIUM, 2 LOW.

---

## ISSUE 1: TAA Does NOT Read Velocity Buffer — Reprojection is Broken for Moving Objects

```
ISSUE: TAA resolve shader does NOT consume the velocity buffer at all. It uses
       a stub ComputeMotionVector() that returns jitter offset only. The C++ side
       correctly generates velocity vectors (VelocityPass writes m_gbuffer.velocity),
       but the TAA shader ignores them. The TAA root constants struct has no
       velocitySrvIndex field. Moving objects will ghost/smear instead of being
       correctly reprojected.

FILE: runtime/shaders/runtime/postprocess/taa_resolve_cs.hlsl:106-115
      runtime/engine/renderer/frame/frame_postprocess.cpp:103-130

EXPECTED: TAA root constants should include velocitySrvIndex (the velocity buffer
          SRV), and ComputeMotionVector() should sample the velocity buffer at the
          closest-depth neighbor pixel to get per-pixel motion vectors.
          UE5 TemporalAA.usf:2087-2121 reads the velocity buffer for reprojection.

ACTUAL: The TAA shader comment explicitly says:
        "Since UzEngine does not yet have a velocity buffer (per-object motion),
        we reproject via the depth buffer..."
        But the velocity buffer DOES exist now — VelocityPass generates it.
        The C++ struct TaaConstants has 8 DWORDs:
          jitterX, jitterY, texelSizeX, texelSizeY, blendWeight,
          sceneSrv, depthSrv, historySrv
        There is no velocitySrv field.

FIX: 1. Add velocitySrvIndex to TAARootConstants cbuffer in taa_resolve_cs.hlsl
        (either repurpose the padding DWORD 9, or restructure to fit in 8 DWORDs
        by packing jitter+texelSize as 4 floats in first 4 DWORDs).
     2. In C++ frame_postprocess.cpp TAA_Resolve pass, pass m_gbuffer.velocitySRV
        as the velocitySrvIndex.
     3. In the shader, sample the velocity buffer at the closest-depth neighbor
        pixel instead of returning the jitter offset.

SEVERITY: CRITICAL
```

---

## ISSUE 2: Motion Blur Gather Pass Does NOT Use Neighbor Max Tiles

```
ISSUE: The motion blur pipeline has 3 passes: TileMax, NeighborMax, and Gather.
       The TileMax and NeighborMax passes are correctly implemented and produce
       tile-level max velocity textures. However, the Gather pass (motion_blur_cs.hlsl)
       does NOT read the neighbor max texture at all — it samples velocity directly
       from the velocity buffer per-pixel. The neighbor max texture is computed
       but wasted, and the gather shader cannot early-out on tiles with zero motion
       (a key UE5 optimization).

FILE: runtime/shaders/runtime/postprocess/motion_blur_cs.hlsl:82-95
      runtime/engine/renderer/frame/frame_postprocess.cpp:330-353

EXPECTED: The gather pass root constants should include neighborMaxSrvIndex.
          The shader should read the neighbor max tile to determine max blur
          radius for the tile, enabling early-out for static tiles and correct
          scatter-as-gather sampling radius.
          UE5 MotionBlurApply.usf:269-285 reads VelocityFlattenParameters.TexSize
          to determine MaxVelocityPixels from the neighbor max texture.

ACTUAL: The C++ MBConstants struct passes: scene, vel, depth, w, h, scale, maxPx, uav.
        The HLSL MotionBlurParams cbuffer matches this exactly: sceneSrvIndex,
        velocitySrvIndex, depthSrvIndex, outputWidth, outputHeight,
        motionBlurScale, maxBlurPixels, outputUavIndex.
        No neighborMaxSrvIndex anywhere.

FIX: 1. Add neighborMaxSrvIndex to MotionBlurParams in motion_blur_cs.hlsl.
     2. In C++ frame_postprocess.cpp MotionBlur pass, add
        m_motionBlurNeighborMaxSrv to the root constants.
     3. In the shader, read neighbor max at (threadId / 16) to determine
        per-tile max velocity. Use it for early-out and sample radius capping.
     Note: this requires expanding from 8 to 9 root constant DWORDs, which
     exceeds the compute root signature's 8-DWORD limit. Either restructure
     to pack into 8 DWORDs (combine w/h into a single uint32 via bit packing)
     or upload as a CBV instead.

SEVERITY: HIGH
```

---

## ISSUE 3: HDR SceneColor Imported as RenderTarget but Read as ShaderResource by Bloom/TAA

```
ISSUE: In frame_setup.cpp:544, the HDR scene color is imported into the render
       graph with initial state RHIResourceState::RenderTarget:
         ctx.hdrRes = rgBuilder.ImportTexture("HDR_SceneColor", m_hdrSceneColor,
                                              RHIResourceState::RenderTarget);
       But frame_postprocess.cpp bloom/TAA passes declare:
         pb.Read(ctx.hdrRes, RHIResourceState::ShaderResource);
       The render graph compiler should correctly insert a RenderTarget->ShaderResource
       barrier between the lighting pass (which writes hdrRes as RenderTarget) and
       the first post-process pass (which reads it as ShaderResource).

       This is NOT a bug — the render graph handles this correctly. However, there
       is a subtle correctness concern: the auto-exposure pass in frame_setup.cpp:253
       runs BEFORE the render graph is even compiled/executed:
         float autoExp = m_autoExposure->Execute(cmd, m_hdrSceneColorSrv, ...);
       This reads the HDR scene color from the PREVIOUS frame (gated by m_hdrHasContent).
       The texture may still be in RenderTarget state from the previous frame's render
       graph execution. The auto-exposure Execute() call is outside the render graph
       and performs its own barriers internally, so this is safe IF the auto-exposure
       implementation handles the state transition.

       But the initial import state of RenderTarget assumes the texture starts in
       RenderTarget state at the beginning of this frame. If auto-exposure changed
       it to ShaderResource (to read it), then the import state is wrong and the
       render graph may insert a redundant or incorrect barrier.

FILE: runtime/engine/renderer/frame/frame_setup.cpp:253-256 (auto-exposure)
      runtime/engine/renderer/frame/frame_setup.cpp:544 (import state)

EXPECTED: The HDR scene color initial state for the render graph import should
          match the actual state of the texture at the time of import. If
          auto-exposure leaves it in ShaderResource state, import should be
          ShaderResource.

ACTUAL: Import is always RHIResourceState::RenderTarget. Auto-exposure runs
        before the render graph and may leave it in a different state.

FIX: Either:
     (a) Track the actual state after auto-exposure and use it for the import, or
     (b) Ensure auto-exposure transitions the texture back to RenderTarget after
         reading it (simpler but adds a barrier).
     Verify by checking the AutoExposure::Execute implementation for internal
     barrier management.

SEVERITY: MEDIUM
```

---

## ISSUE 4: GBuffer Velocity Imported as ShaderResource but Written by VelocityPass

```
ISSUE: In frame_setup.cpp:550, the velocity buffer is imported with initial state
       ShaderResource:
         ctx.velocityRes = rgBuilder.ImportTexture("GBuf_Velocity",
             m_gbuffer.velocity, RHIResourceState::ShaderResource);
       The VelocityPass (frame_geometry.cpp:144) writes to it as RenderTarget:
         pb.Write(desc.velocityTarget, RHIResourceState::RenderTarget);
       The render graph handles the ShaderResource->RenderTarget transition correctly.

       However, at the start of the NEXT frame, the velocity buffer may still be
       in whatever state the previous frame's last consumer left it in. Since the
       velocity buffer is consumed by motion blur and GTAO temporal as ShaderResource,
       it should end the frame in ShaderResource state — which matches the import
       state. This is correct.

       The real question is: on the FIRST frame (before any VelocityPass has run),
       the velocity buffer has never been written. The velocity texture was created
       with RenderTarget|ShaderResource usage but never cleared. Downstream passes
       (motion blur tile max) will read uninitialized data from the velocity buffer
       on the first frame where m_prevViewProjValid is false (velocity pass is
       skipped on frame 0). This produces undefined motion vectors.

FILE: runtime/engine/renderer/frame/frame_geometry.cpp:122
      runtime/engine/renderer/frame/frame_setup.cpp:550

EXPECTED: The velocity buffer should be cleared to zero on the first frame, or
          motion blur should be gated on m_prevViewProjValid just as the velocity
          pass is.

ACTUAL: Motion blur activation (frame_setup.cpp:566-568) only checks:
        m_motionBlurEnabled && m_motionBlurOutput.IsValid()
        && m_motionBlurTileMax.IsValid() && m_motionBlurNeighborMax.IsValid()
        && m_gbuffer.velocity.IsValid() && ctx.bindless && m_shadowDebugMode == 0
        It does NOT check m_prevViewProjValid. On the first frame, the velocity
        buffer is uninitialized. The velocity pass is correctly skipped (line 122
        checks m_prevViewProjValid), but motion blur still runs on garbage data.

FIX: Add m_prevViewProjValid to the motionBlurActive condition:
     ctx.motionBlurActive = m_motionBlurEnabled && m_prevViewProjValid
                          && m_motionBlurOutput.IsValid() && ...

SEVERITY: LOW (only affects first frame — usually invisible)
```

---

## ISSUE 5: Static loggedOnce Variables in Lambda Captures (Render Graph Callbacks)

```
ISSUE: Multiple render graph pass callbacks use `static bool loggedOnce = false`
       inside lambda bodies (e.g., frame_shadows.cpp:450, frame_lighting.cpp:52,
       velocity_pass.cpp:88). These are function-local statics inside lambdas,
       which work correctly in practice (one-time logging). However, if the
       SceneRenderer were ever instantiated multiple times (e.g., multiple
       viewports or headless + interactive), the static would be shared across
       all instances, suppressing logging for the second instance.

FILE: Multiple locations in frame_*.cpp and pass implementations.

EXPECTED: For truly per-instance first-frame logging, use a member bool on the
          SceneRenderer or the pass object.

ACTUAL: Uses function-local statics inside lambdas. Works for single-instance
        use but is technically incorrect for multi-instance scenarios.

FIX: Replace static bool locals with per-instance flags where multi-renderer
     support is needed. Low priority since only one SceneRenderer exists today.

SEVERITY: LOW
```

---

## Verified Correct (No Issues Found)

### Resource Barrier Management
- **Shadow atlas barriers**: Correctly managed by render graph (Write with DepthStencilWrite,
  Read with ShaderResource). The render graph compiler inserts appropriate transitions.
- **DFS barriers**: Properly managed inline in the compute pass callbacks:
  - Indirect args: UAV -> CopyDst -> UAV (clear) -> UAV sync barriers -> SRV (post-trace)
  - DFS output: UAV -> SRV transition after cone trace (frame_lighting.cpp:401-405)
- **SSR barriers**: Both trace and resolve passes properly transition textures:
  - Trace output: SRV -> UAV (write) -> SRV (for resolve to read)
  - Resolved output: SRV -> UAV (write) -> SRV (for lighting to read)
- **TAA history ping-pong**: Write target: SRV -> UAV (compute write) -> SRV (tonemap read).
  Read target: UAV -> SRV (compute read). Transitions are correct and paired.
- **FXAA intermediate**: Correctly imported as ShaderResource, written as RenderTarget
  by tonemap, read as ShaderResource by FXAA.
- **Bloom**: Read hdrRes as ShaderResource — bloom internally manages its own mip barriers.
- **Motion blur tile/neighbor**: Proper SRV -> UAV -> SRV transitions per pass.
- **Color grading LUT**: SRV -> UAV (compute gen) -> SRV (tonemap sample).
- **DOF CoC and output**: SRV -> UAV -> SRV for both CoC and blur output.

### CB Layout Parity (C++ vs HLSL)
- **PerFrameCB** (512 bytes): Verified byte-for-byte match between C++ struct and
  PerFrameParams. All fields at correct offsets per the layout comments.
- **PerObjectCB** (256 bytes): Scalars-first layout (indices at offset 0, matrices at
  offset 32) correctly avoids std140 matrix padding issues.
- **ShadowDataCB** (2304 bytes): Layout verified — cascadeViewProj, splits, biases,
  scalar params, localShadows. static_assert confirms size.
- **VelocityCB** (256 bytes): C++ prevViewProj + screenWidth/screenHeight matches
  HLSL cbuffer VelocityCB at b2 exactly.
- **LightingCB** (4608 bytes): PointLightData (48B) x 64 + counts + SpotLightData (80B) x 16.
  static_assert confirms alignment.

### Pass Ordering
Pipeline order is correct and matches UE5's deferred pipeline:
1. **Setup** (frame_setup.cpp): Camera, CB uploads, draw list, instancing, shadow cascades
2. **Shadows** (frame_shadows.cpp): CSM (cache-aware two-pass) + local light shadows
3. **Geometry** (frame_geometry.cpp): Depth prepass -> Hi-Z -> GBuffer -> Velocity
4. **Lighting** (frame_lighting.cpp): GTAO -> DFS cull+trace -> SSR trace+resolve -> Deferred lighting
5. **Post-process** (frame_postprocess.cpp): Bloom -> TAA -> Color grading LUT -> Motion blur (3 passes) -> DOF -> Tonemap -> FXAA
6. **Overlay** (frame_overlay.cpp): Atmosphere -> Fog -> Editor grid + stats -> Present

### Root Signature Slot Usage
- Slot 0 (kSlotRootConstants / b0): Root constants — correctly used for descriptor indices.
- Slot 1 (kSlotPerFrame / b1): PerFrameCB — consistently bound as root CBV across all passes.
- Slot 2 (kSlotPerObject / b2): PerObjectCB / LightingCB / VelocityCB — correctly reused
  for different data in different passes (per-draw vs fullscreen).
- Shadow CBV (kSlotShadow / b3): Shadow per-cascade viewProj — correctly used.
- Descriptor table slots 4-5: UAV and SRV tables for compute passes — consistent.

### Memory Management / Leak Check
- All textures created in Init() are destroyed in Shutdown(). Verified:
  - GBuffer, HDR scene color, transmittance LUT, multi-scatter LUT, sky cubemap,
    Hi-Z pyramid, TAA history (2), SSR trace+resolve, color grading LUT,
    motion blur (output + tile max + neighbor max), DOF (CoC + output),
    FXAA intermediate, DFS (output + culled indices + indirect args),
    shadow atlas, static shadow atlas, local shadow atlas, fallback textures (3),
    instance buffers (3), IBL baker resources, auto-exposure resources,
    GTAO resources, bloom resources, screenshot readback, shadow readback.
- Resize() correctly destroys and recreates resolution-dependent resources.
- WaitIdle() called at top of Shutdown() to prevent dangling GPU references.

### Triple-Buffering Correctness
- Instance buffers: kInstanceBufferCount=3, indexed by frameIndex%3.
  CPU writes to slot N while GPU reads N-2. Fence sync in the app layer
  guarantees frame N-2 is complete. Capacity growth calls WaitIdle() before
  destroying all 3 slots — correct.
- CB ring: kCBRingFrames=3, BeginFrame(frameIndex) handles ring rotation.
  Each frame gets its own 2MB region.

### Auto-Exposure Readback Safety
- Auto-exposure runs before the render graph on the previous frame's HDR content
  (gated by m_hdrHasContent). This produces a natural 1-frame latency for exposure
  adaptation, which is correct behavior (UE5 also has 1-frame latency for eye
  adaptation). No GPU stall — the previous frame's data is already available.

---

## Fix Status (2026-03-18)

| # | Issue | Status |
|---|-------|--------|
| 1 | TAA does not read velocity buffer | **FIXED** — TaaCB at b2 includes velocitySrvIndex, shader reads velocity buffer |
| 2 | Motion blur gather ignores neighbor max | **FIXED** — neighborMaxSrvIndex added to root constants, shader early-outs on zero-motion tiles |
| 3 | HDR texture state ambiguity | NOT FIXED (needs AutoExposure barrier verification) |
| 4 | Motion blur runs on first frame garbage | **FIXED** — m_prevViewProjValid added to motionBlurActive condition |
| 5 | Static loggedOnce in lambdas | NOT FIXED (low priority, single renderer instance) |
