# Phase 4d — 4.23.0 VFX Async GPU Trace (Depth-Buffer Raycasting)

GPU-based particle collision using screen-space depth buffer raycasting.
No CPU readback; no flat-plane assumption. Each alive particle's velocity
vector is traced against the previous frame's depth buffer in a compute
shader. Collision events (position, normal, reflected velocity) are written
to a UAV event buffer consumed by the main update CS in the same frame.

---

## 1. Depth Buffer SRV Access

- [ ] 1.1 Ensure the depth prepass (or G-buffer pass) writes to
      `DXGI_FORMAT_R32_TYPELESS` depth with SRV format `R32_FLOAT`.
      If the depth resource is currently `DEPTH_STENCIL` without `ALLOW_SHADER_RESOURCE`,
      rebuild it with both flags.
- [ ] 1.2 In `vfx_system.cpp`, accept a `RHITextureHandle depthBufferSrv` parameter
      in `VFXSystem::Render(...)` (injected from the main renderer each frame).
      Register it as a transient SRV for the trace CS.
- [ ] 1.3 Expose the depth buffer SRV index via `VFXSystem::SetDepthBufferSRV(
      int32_t bindlessSrvIndex)` called each frame after the G-buffer pass.

## 2. Depth-Space Trace Compute Shader

- [ ] 2.1 Create `runtime/shaders/runtime/vfx/particle_depth_trace_cs.hlsl`.
      `[numthreads(64,1,1)]` — one thread per alive particle.
      ```hlsl
      /// @ref Laine & Karras 2010 "Efficient Sparse Voxel Octrees" — ray-depth
      ///      intersection via linear depth march
      /// @ref Lagarde & de Rousiers 2014 "Moving Frostbite to PBR" — depth buffer
      ///      reprojection for screen-space effects
      Texture2D<float>           g_depth        : register(t12);
      SamplerState               g_depthSampler : register(s6); // point clamp
      RWStructuredBuffer<TraceHitEvent> g_traceHits : register(u3);
      RWBuffer<uint>             g_traceHitCount : register(u4);
      StructuredBuffer<float3>   g_positions    : register(t0);
      StructuredBuffer<float3>   g_velocities   : register(t1);
      cbuffer TraceCB : register(b7) {
          float4x4 viewProj;        // current frame VP
          float4x4 invViewProj;     // for depth→world reconstruction
          float    dt;              // simulation delta time
          float    traceRadius;     // max trace distance in cm (default 30.0)
          uint     maxSteps;        // ray march steps (default 8)
          uint     aliveCount;
          float    screenWidth;
          float    screenHeight;
          float2   _pad;
      };
      struct TraceHitEvent {
          uint    particleIndex;   // which particle hit something
          float3  hitPosition;     // world-space hit point
          float3  hitNormal;       // estimated surface normal (ddx/ddy of depth)
          float3  reflectedVel;    // pre-computed reflected velocity
      };
      ```
- [ ] 2.2 For each alive particle, trace a ray from `pos` along `normalize(vel)`
      for `maxSteps` linear steps of `traceRadius / maxSteps` cm each:
      ```hlsl
      for (uint s = 0; s < maxSteps; s++) {
          float3 samplePos = pos + normalize(vel) * (traceRadius * (s+1) / maxSteps);
          // Project to screen
          float4 clip = mul(viewProj, float4(samplePos, 1));
          float2 ndc  = clip.xy / clip.w;
          if (any(abs(ndc) > 1.0)) break; // outside frustum
          float2 uv   = ndc * 0.5 + 0.5; uv.y = 1.0 - uv.y;
          float  sceneDepth = g_depth.SampleLevel(g_depthSampler, uv, 0);
          float  rayDepth   = clip.z / clip.w; // NDC Z
          if (rayDepth > sceneDepth + 0.001) {
              // Hit detected — estimate normal via ddx/ddy of depth
              float dL = g_depth.SampleLevel(g_depthSampler, uv + float2(-1.0/screenWidth,0), 0);
              float dR = g_depth.SampleLevel(g_depthSampler, uv + float2( 1.0/screenWidth,0), 0);
              float dU = g_depth.SampleLevel(g_depthSampler, uv + float2(0,-1.0/screenHeight), 0);
              float dD = g_depth.SampleLevel(g_depthSampler, uv + float2(0, 1.0/screenHeight), 0);
              float3 n = normalize(float3(dL - dR, dD - dU, 0.002));
              // write event
              uint slot; InterlockedAdd(g_traceHitCount[0], 1, slot);
              if (slot < aliveCount) {
                  TraceHitEvent ev;
                  ev.particleIndex = particleIdx;
                  ev.hitPosition   = samplePos;
                  ev.hitNormal     = n;
                  ev.reflectedVel  = reflect(vel, n) * restitution;
                  g_traceHits[slot] = ev;
              }
              break;
          }
      }
      ```
- [ ] 2.3 Add `float restitution = 0.4f` to `TraceCB` and use it in `reflectedVel`.

## 3. Collision Event Application CS

- [ ] 3.1 Create `runtime/shaders/runtime/vfx/particle_apply_collisions_cs.hlsl`.
      `[numthreads(64,1,1)]` — one thread per hit event (dispatch count from
      `g_traceHitCount`):
      ```hlsl
      StructuredBuffer<TraceHitEvent>    g_traceHits   : register(t13);
      RWStructuredBuffer<float3>         g_positionsW  : register(u0);
      RWStructuredBuffer<float3>         g_velocitiesW : register(u1);
      ```
      For each event, write:
      ```hlsl
      g_positionsW[ev.particleIndex]  = ev.hitPosition;
      g_velocitiesW[ev.particleIndex] = ev.reflectedVel;
      ```
      UAV write races (two events for same particle) are acceptable — last
      write wins, which is physically reasonable.
- [ ] 3.2 Dispatch as `ceil(hitCount / 64)` groups, where `hitCount` is read
      from `g_traceHitCount` via indirect dispatch args (same pattern as
      `particle_indirect_args_cs.hlsl`). This avoids CPU readback of hit count.

## 4. Render Graph Pass Ordering

- [ ] 4.1 Each frame, when `AsyncGPUTraceModule` is active, the render graph must
      execute in this order:
      1. `G-Buffer / Depth Prepass` — depth buffer written.
      2. `ClearTraceHitCount` — clear `g_traceHitCount` to 0 via `ClearUAV`.
      3. `ParticleDepthTracePass` — CS reads depth SRV, writes hit events.
      4. UAV barrier on `g_traceHits` and `g_traceHitCount`.
      5. `BuildTraceIndirectArgsPass` — tiny CS writes `hitCount` into dispatch args.
      6. `ApplyCollisionsPass` — CS writes corrected pos/vel to SoA.
      7. UAV barrier on `g_positionsW` / `g_velocitiesW`.
      8. `ParticleUpdatePass` — standard update reads corrected SoA.
- [ ] 4.2 Depth texture must transition from
      `D3D12_RESOURCE_STATE_DEPTH_WRITE → PIXEL_SHADER_RESOURCE` before pass 3,
      then back to `DEPTH_WRITE` for the next frame's depth prepass.

## 5. CPU Module

- [x] 5.1 Create `runtime/engine/vfx/modules/async_gpu_trace_module.h/cpp` — implemented IVFXModule with traceRadius=30, maxSteps=8, restitution=0.4, writeDeathDecals=false:
      ```cpp
      struct AsyncGPUTraceModule : IVFXModule {
          float    traceRadius  = 30.0f;   // cm
          uint32_t maxSteps     = 8;
          float    restitution  = 0.4f;
          bool     writeDeathDecals = false; // forward hits to decal system
      };
      ```
- [x] 5.2 `GetShaderCode()` returns empty — this module hooks into the render graph
      directly, not into the particle update HLSL snippet system.
- [x] 5.3 ApplyJsonConfig round-trips all four fields (traceRadius, maxSteps, restitution, writeDeathDecals).
- [x] 5.4 Registered in `register_builtin_modules.cpp` as `"async_gpu_trace"`.

## 6. Optional: Decal Integration

- [ ] 6.1 If `writeDeathDecals == true`, treat each `TraceHitEvent` as a decal
      spawn event: copy `hitPosition` and `hitNormal` into the `g_deathEventBuffer`
      from task 4.19.0. The `hitNormal` from the depth trace is far more accurate
      than the default world-up normal used by the basic death decal system.
- [ ] 6.2 Add a `CopyTraceHitsToDeathEvents` CS pass after `ApplyCollisionsPass`
      that copies `TraceHitEvent` entries with their normals into
      `g_deathEvents` (append, respect `maxDeathDecalsPerFrame` cap).

## 7. Demo Integration

- [ ] 7.1 Add preset "Bounce on Geometry" in `vfx_viewport_demo.cpp`:
      - Place a tilted box mesh in the scene as obstacle.
      - `AsyncGPUTraceModule` attached: `traceRadius=20.0`, `maxSteps=8`,
        `restitution=0.5`.
      - Particles fall from above and bounce off the tilted box surface.
- [ ] 7.2 Add preset "Depth Trace + Decals":
      `writeDeathDecals=true`, `restitution=0.05` (nearly no bounce),
      scorch decal material. Particles should stick to box surfaces and
      leave marks with correct normals.
- [ ] 7.3 ImGui controls: `traceRadius` slider (5–100 cm), `maxSteps` int
      (1–16), `restitution` slider (0–1), `writeDeathDecals` checkbox.
- [ ] 7.4 Display `g_traceHitCount` read-back value in ImGui (use a separate
      1-frame delayed readback buffer purely for debugging display).

## 8. Build and Verification

- [ ] 8.1 `cd runtime && zig build --clean && zig build` — no errors.
- [ ] 8.2 `cd demos/vfx_viewport && zig build --clean && zig build`.
- [ ] 8.3 Capture "Bounce on Geometry" at frame 20: particles must visibly
      deflect off the tilted box surface — not pass through it and not
      cluster at the box center.
- [ ] 8.4 Capture "Depth Trace + Decals" at frame 40: decal marks must appear
      on the box surface with normals matching the box face directions
      (verify by checking marks are not all aligned with world-up).
- [ ] 8.5 Confirm no device-removed errors from barrier omissions (run with
      D3D12 Debug Layer + GPU-Based Validation in debug build).

## 9. Documentation

- [ ] 9.1 In `particle_depth_trace_cs.hlsl` header:
      ```hlsl
      /// @ref Laine & Karras 2010 "Efficient Sparse Voxel Octrees" —
      ///      linear ray march against a depth field
      /// @ref Lagarde & de Rousiers 2014 "Moving Frostbite to PBR" —
      ///      depth buffer reprojection for screen-space effects
      /// @constants maxSteps: 8 default — enough for 30cm radius at 3.75cm steps.
      ///            Increase to 16 for sharp-edged geometry; 8 suffices for smooth.
      ```
- [ ] 9.2 Document the known limitation: screen-space tracing misses geometry
      behind other objects (standard SSR limitation). Particles near occluded
      surfaces may tunnel. Future improvement: world-space BVH trace.
- [ ] 9.3 Document the depth normal estimation accuracy: `(dL-dR, dD-dU, epsilon)`
      is a finite-difference approximation, not the true geometric normal.
      Accuracy degrades for grazing angles and at silhouette edges.

## 10. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 10.1 Update or create documentation covering the implementation
- [ ] 10.2 Write tests covering the new behavior
- [ ] 10.3 Run tests and confirm they pass
