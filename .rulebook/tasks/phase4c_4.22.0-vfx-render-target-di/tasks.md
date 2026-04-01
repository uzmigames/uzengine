# Phase 4c — 4.22.0 VFX Render Target DI (GPU Feedback Loops)

Persistent GPU feedback: a 2D accumulation texture (RGBA16F) serves as both
a write target and a read source for particles. Each frame, a CS writes
particle contributions (paint) into the accumulation texture; the billboard PS
can read it for feedback effects (persistent trails, screen-space paint). The
texture persists across frames unless explicitly cleared.

---

## 1. Accumulation Texture Allocation

- [ ] 1.1 Create `runtime/engine/vfx/modules/render_target_di_module.h/cpp`.
      Fields:
      ```cpp
      uint32_t         texWidth          = 1280;
      uint32_t         texHeight         = 720;
      float            decayRate         = 0.02f;   // per-frame fade (0=persistent)
      bool             screenSpace       = true;    // map XY to screen coords
      bool             clearOnReset      = true;    // clear when emitter resets
      RHITextureHandle accumTexture      = RHITextureHandle::Invalid();
      int32_t          accumUavIndex     = -1;      // bindless UAV for CS write
      int32_t          accumSrvIndex     = -1;      // bindless SRV for PS read
      ```
- [ ] 1.2 Implement `RenderTargetDIModule::CreateTexture(IRHIDevice* device)`:
      - Format: `DXGI_FORMAT_R16G16B16A16_FLOAT`.
      - Flags: `D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | ALLOW_RENDER_TARGET`.
      - Initial state: `D3D12_RESOURCE_STATE_UNORDERED_ACCESS`.
      - Register UAV: `accumUavIndex = device->RegisterTexture2DUAV(accumTexture)`.
      - Register SRV: `accumSrvIndex = device->RegisterTexture2DSRV(accumTexture)`.
- [ ] 1.3 Implement `RenderTargetDIModule::Clear(RenderGraph& rg)`: add a clear CS
      pass that writes `float4(0,0,0,0)` to all texels via
      `RWTexture2D<float4> g_accum : register(u0)`, dispatched as
      `ceil(width/8) × ceil(height/8)` groups with `[numthreads(8,8,1)]`.

## 2. Particle Paint Compute Shader

- [ ] 2.1 Create `runtime/shaders/runtime/vfx/particle_paint_cs.hlsl`.
      `[numthreads(64,1,1)]` — one thread per alive particle. For each particle:
      ```hlsl
      RWTexture2D<float4>        g_accum      : register(u0);
      StructuredBuffer<float3>   g_positions  : register(t0);
      StructuredBuffer<float4>   g_colors     : register(t5);
      StructuredBuffer<float>    g_sizes      : register(t4);
      cbuffer PaintCB : register(b4) {
          float4x4 viewProj;       // for world→screen projection
          float    decayRate;      // per-frame additive fade-out
          float    paintOpacity;   // contribution per particle (0..1)
          uint     texWidth;
          uint     texHeight;
          uint     aliveCount;
          float3   _pad;
      };
      ```
      Project particle world-space position to screen:
      ```hlsl
      float4 clip = mul(viewProj, float4(pos, 1));
      float2 ndc  = clip.xy / clip.w;
      float2 uv   = ndc * 0.5 + 0.5;
      uv.y = 1.0 - uv.y; // flip Y for texture space
      if (any(uv < 0) || any(uv > 1)) return;
      uint2 px = (uint2)(uv * float2(texWidth, texHeight));
      float4 contribution = color * paintOpacity * (size / 10.0);
      g_accum[px] += contribution; // InterlockedAdd unavailable for float — use UAV store + accept race
      ```
      Note: UAV float add races are acceptable for paint effects (produces
      interesting over-bright blobs rather than correctness errors).
- [ ] 2.2 Add a separate decay pass dispatched as a full-screen CS
      (`[numthreads(8,8,1)]`, `ceil(W/8) × ceil(H/8)` groups):
      ```hlsl
      g_accum[DTid.xy] *= (1.0 - decayRate);
      ```
      This pass runs first each frame, then the paint pass.

## 3. Billboard PS Feedback Read

- [ ] 3.1 In `particle_billboard_ps.hlsl`, add (guarded by `#define USE_FEEDBACK_READ 1`):
      ```hlsl
      Texture2D<float4> g_accumFeedback : register(t12);
      SamplerState      g_fbSampler     : register(s5); // bilinear clamp
      cbuffer FeedbackCB : register(b7) {
          float feedbackStrength; // 0..1 blend weight
          float3 _pad;
      };
      ```
      After computing particle color:
      ```hlsl
      #if USE_FEEDBACK_READ
          float2 screenUV = input.position.xy / float2(screenWidth, screenHeight);
          float4 feedback = g_accumFeedback.SampleLevel(g_fbSampler, screenUV, 0);
          color.rgb = lerp(color.rgb, color.rgb + feedback.rgb, feedbackStrength);
      #endif
      ```
- [ ] 3.2 The read in the PS uses the SRV (`accumSrvIndex`) while the paint CS
      uses the UAV (`accumUavIndex`). Ensure the resource barrier in the render
      graph transitions the texture from `UNORDERED_ACCESS` → `PIXEL_SHADER_RESOURCE`
      between the paint CS and the billboard draw call. Add the reverse transition
      after the draw.

## 4. Render Graph Pass Ordering

- [ ] 4.1 Each frame (when `RenderTargetDIModule` is active), the render graph
      must execute in this order:
      1. `DecayPass` — fade accumulation texture (UAV)
      2. `PaintPass` — write particle contributions (UAV)
      3. UAV barrier on `accumTexture`
      4. Resource barrier: `UAV → PIXEL_SHADER_RESOURCE`
      5. `BillboardDrawPass` — PS reads SRV for feedback
      6. Resource barrier: `PIXEL_SHADER_RESOURCE → UAV` (ready for next frame)
- [ ] 4.2 In `vfx_system.cpp`, add `AddPaintPasses(RenderGraph& rg, EmitterHandle e)`
      that inserts the four passes above when the emitter has a `RenderTargetDIModule`.

## 5. CPU Module Parameters

- [ ] 5.1 Add to `RenderTargetDIModule`:
      ```cpp
      float paintOpacity   = 0.05f;  // per-particle contribution per frame
      float feedbackStrength = 0.3f; // how much accum texture influences billboard color
      ```
- [ ] 5.2 `Serialize` / `Deserialize` round-trips all configuration fields.

## 6. Demo Integration

- [ ] 6.1 Add preset "Particle Paint" in `vfx_viewport_demo.cpp`:
      `decayRate=0.01` (slow fade), `paintOpacity=0.1`, `feedbackStrength=0.5`,
      `texWidth=1280`, `texHeight=720`. Particles spawn from mouse cursor position
      (or animated circle path in headless mode).
- [ ] 6.2 Add preset "Persistent Trails":
      `decayRate=0.0` (no decay — fully persistent), `paintOpacity=0.02`,
      particles moving in a sine wave pattern, leaving a visible trail texture.
- [ ] 6.3 Add ImGui controls: `decayRate` (0–1 slider), `paintOpacity` (0–1),
      `feedbackStrength` (0–1), "Clear Now" button.
- [ ] 6.4 Display the accumulation texture in a small ImGui preview window
      using `ImGui::Image(accumSrvGpuHandle, ImVec2(320, 180))`.

## 7. Build and Verification

- [ ] 7.1 `cd runtime && zig build --clean && zig build` — no errors.
- [ ] 7.2 `cd demos/vfx_viewport && zig build --clean && zig build`.
- [ ] 7.3 Capture "Persistent Trails" at frame 60: the accumulation texture
      should show a visible trail of paint strokes from particle paths.
- [ ] 7.4 Verify barrier ordering: capture with RenderDoc (if available) and
      confirm no validation warnings about missing barriers on `accumTexture`.

## 8. Documentation

- [ ] 8.1 Add to `particle_paint_cs.hlsl`:
      ```hlsl
      /// @ref Shiffman 2012 "The Nature of Code" — particle trail accumulation pattern
      /// @constants decayRate: per-frame fade factor; 0.0 = fully persistent,
      ///            1.0 = cleared each frame. Typical: 0.01-0.05 for visible trails.
      ```
- [ ] 8.2 Document UAV float-add race condition in `particle_paint_cs.hlsl`:
      the accepted race produces over-bright pixels when multiple particles hit
      the same texel in one frame, which is an acceptable visual artefact for
      paint effects. For precise accumulation, use a one-particle-per-cell grid.
