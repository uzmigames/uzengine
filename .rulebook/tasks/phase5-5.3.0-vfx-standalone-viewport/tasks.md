## Phase 1: Input System & Camera Control

- [x] 1.1 Implement InputState struct (256-key array, 5 mouse buttons, mouse XY + delta, scroll delta, previous frame snapshot). Added with BeginFrame() state swap.
- [x] 1.2 Wire WndProc to capture WM_KEYDOWN/UP, WM_LBUTTONDOWN/UP, WM_RBUTTONDOWN/UP, WM_MBUTTONDOWN/UP, WM_MOUSEWHEEL, WM_MOUSEMOVE. Full event handling with SetCapture/ShowCursor on RMB.
- [x] 1.3 Implement raw mouse input via WM_INPUT (RegisterRawInputDevices + RAWINPUT parsing for unaccelerated delta). Registered in WinMain, parsed in WndProc.
- [x] 1.4 Implement IsKeyDown/JustPressed/JustReleased, IsMouseDown/JustClicked helpers with per-frame state transition.
- [x] 1.5 Implement mouse capture (SetCapture when RMB held, ReleaseCapture on RMB up, ShowCursor toggle).
- [x] 1.6 Implement FPS camera controller: WASD movement (relative to camera forward/right), Space=up, Ctrl=down, Shift=3x sprint.
- [x] 1.7 Implement mouse look (RMB held): yaw/pitch from raw mouse delta, pitch clamped to +/-89 deg.
- [x] 1.8 Implement orbit mode: middle mouse drag to orbit around target, scroll wheel to dolly forward/back.
- [x] 1.9 Implement FOV control: Ctrl+scroll changes FOV (clamped 30-120 deg), displayed in title bar.
- [x] 1.10 Implement camera smoothing/inertia: exponential decay on velocity (damping=10.0).
- [x] 1.11 Wire frame timing: QueryPerformanceCounter delta, update camera with real dt. Title bar shows pos/angles/FOV/FPS every 30 frames.

## Phase 2: Sky & Environment Rendering

- [x] 2.1 Replace current sky shader with Rayleigh+Mie single-scattering atmosphere (Nishita/Preetham). Kasten-Young airmass, HG phase function (g=0.76), sun transmittance for sunset colors.
- [x] 2.2 Implement sun disc with physically-based limb darkening (angular radius 0.00935 rad, smoothstep mask, exponential glow halo).
- [x] 2.3 Implement horizon haze: optical depth increases at low zenith angles producing natural haze, warm tint at sunset from sun transmittance.
- [x] 2.4 Implement time-of-day control: T/Shift+T rotate sun elevation (-10 to 90 deg), displayed in title bar. Light direction computed from spherical coords.
- [x] 2.5 Implement sky ambient adaptation: sphere PS ambient lerps between warm (low sun) and cool blue (high sun) based on lightDir.y.
- [x] 2.6 Implement sky color influence on ground/objects: ambient color derived from sun height, not hardcoded.

## Phase 3: Editor Grid (Engine Parity)

- [x] 3.1 Replace ground plane mesh with infinite grid: fullscreen triangle + ray/XZ-plane intersection at Y=-0.5 in pixel shader. Removed ground mesh entirely.
- [x] 3.2 Implement anti-aliased grid lines: fwidth-based smoothstep for sub-pixel quality (Ben Golus reference).
- [x] 3.3 Implement minor grid (1 unit, thin gray) + major grid (10 unit, thicker white).
- [x] 3.4 Implement distance fade: alpha fades from 50 to 100 units, discard beyond.
- [x] 3.5 Implement colored axis lines: X-axis = red (at Z=0), Z-axis = blue (at X=0), 2px width.
- [x] 3.6 Implement grid depth write: SV_Depth output from clip-space projection, GREATER_EQUAL depth test, objects properly intersect grid.

## Phase 4: Mesh System & Materials

- [x] 4.1 Implement 5 procedural mesh generators: sphere (32x16 UV), cube (24 verts/36 indices), plane (10x10 subdivided), cylinder (24 segments + caps), torus (24x12).
- [x] 4.2 Implement mesh registry: MeshData[5] with VB/IB/VBV/IBV/indexCount/name, g_currentMesh index.
- [x] 4.3 Implement material params in PerFrameCB (expanded to 256 bytes): baseColor, metallic, roughness, AO, emissiveColor, emissiveIntensity. All 4 HLSL cbuffers updated.
- [x] 4.4 Implement 8 material presets: Red Plastic, Gold Metal, Rough Stone, Polished Chrome, Rubber, Copper, Ceramic White, Emissive Blue.
- [x] 4.5 Implement Left/Right arrow keys to cycle materials, name displayed in title bar.
- [x] 4.6 Implement Up/Down arrow keys to cycle meshes (sphere/cube/plane/cylinder/torus), name displayed in title bar.
- [x] 4.7 Implement texture loading via TextureLoader API: --albedo/--normal/--roughness CLI args, PerObjectCB with SRV indices bound at kSlotPerObject, cleanup in OnShutdown.
- [x] 4.8 Add tangent vectors to ProceduralVertex (32→48 bytes) and compute them in all 5 mesh generators via ComputeTangents() using Lengyel's triangle-edge/delta-UV method with Gram-Schmidt orthonormalization and bitangent handedness in tangent.w.
- [x] 4.9 Implement multi-object scene: 3 reference spheres with metallic sweep. Added 3 white unit spheres at x={-3,0,3} z=-3 with metallic={0.0,0.5,1.0} roughness=0.3 in both RenderScenePass and RenderShadowPass. Each sphere gets its own PerObjectCB with translation matrix.

## Phase 5: Shadow Mapping

- [x] 5.1 Shadow map: 2048x2048 R32_TYPELESS (D32_FLOAT DSV + R32_FLOAT SRV), shader-visible SRV heap.
- [x] 5.2 Shadow camera: orthographic from light dir, ComputeShadowViewProj() covering -10..10 XY, 0..40 depth.
- [x] 5.3 Shadow depth pass: transition SRV->DEPTH_WRITE, clear 1.0, draw mesh from light, transition back. Shadow VS reads shadowViewProj from CB.
- [x] 5.4 Shadow sampling: SampleShadowPCF() in sphere PS, SamplerComparisonState at s0, shadow map at t0.
- [x] 5.5 PCF 3x3 soft shadows with texel offsets + edge fade at shadow map borders.
- [x] 5.6 Shadow bias: hardware depth bias in shadow PSO + g_shadowBias adjustable with [/] keys.
- [x] 5.7 Shadow debug view: G key toggles 256x256 shadow map overlay in bottom-left corner. Reuses tonemap PSO with shadow map SRV index and restricted viewport/scissor.

## Phase 6: Post-Processing

- [x] 6.1 HDR render target: RGBA16F at screen res, scene renders to HDR, tonemap pass writes to backbuffer.
- [x] 6.2 ACES filmic tonemapping (Stephen Hill fit) in tonemap PS. Removed per-shader Reinhard + gamma.
- [x] 6.3 Exposure control: +/- keys adjust g_exposure (x1.2 per step), displayed in title bar.
- [x] 6.4 Bloom — Integrated engine BloomPass (compute threshold+downsample+upsample). B key toggle. TonemapIndices expanded to 4 uints (HDR, bloom SRV, bloom intensity bits, lens flare).
- [x] 6.5 Vignette: radial darkening in tonemap PS, V key toggles (0 or 0.4 intensity).
- [x] 6.6 Gamma correction: pow(1/2.2) in tonemap PS only (all other shaders output linear).
- [x] 6.7 Post-process toggle: P key switches between ACES and linear tonemap mode.

## Phase 7: Debug Views & Stats Overlay

- [x] 7.1 Implement debug view mode enum: Lit, Albedo, WorldNormal, Metallic, Roughness, Depth, Wireframe, Unlit (8 modes). DebugView enum + g_debugView global.
- [x] 7.2 Implement G-Buffer capture for debug: forward pass outputs debug data via debugViewMode in PerFrameCB (384 bytes).
- [x] 7.3 Implement debug view shader: sphere PS branches on debugViewMode to output albedo/normal/metallic/roughness/depth/unlit directly.
- [x] 7.4 Implement F1-F8 key binding: each key selects a debug view mode, F1 = Lit (default). JustPressed checks in Update().
- [x] 7.5 Implement wireframe overlay mode: separate PSO with D3D12_FILL_MODE_WIREFRAME + k_WireframePS, drawn on top of solid when F7 active.
- [x] 7.6 Implement stats overlay rendering: title bar text with all stats (no bitmap font — title bar approach for standalone simplicity).
- [x] 7.7 Stats: FPS (EMA smoothed over 60 frames), frame time (ms), draw calls, triangle count. Displayed in title bar.
- [x] 7.8 Stats: camera position (X, Y, Z), yaw/pitch angles, current FOV. Displayed in title bar.
- [x] 7.9 Stats: GPU adapter name (from DXGI_ADAPTER_DESC), VRAM total. Stored at init.
- [x] 7.10 Stats: current material name, current mesh name, current debug view mode. Displayed in title bar.
- [x] 7.11 Implement F12 toggle for stats overlay visibility. g_showStats toggled by F12.

## Phase 8: Screenshot & Headless Mode

- [x] 8.1 Implement screenshot capture: F9 saves current frame as BMP to screenshots/ directory. BMP format (no stb dependency).
- [x] 8.2 Implement --screenshot-stdout: encode BMP as base64, print with SCREENSHOT_BASE64: prefix.
- [x] 8.3 Implement --headless mode: create window but don't ShowWindow, render normally. --headless flag.
- [x] 8.4 Implement --frames N: auto-exit after N frames. g_maxFrames + g_frameCount check.
- [x] 8.5 Implement --screenshot-frame N: capture screenshot at frame N (combine with --frames for automated testing).
- [x] 8.6 Implement command-line argument parsing: ParseCommandLine() handles --headless, --frames, --screenshot, --screenshot-stdout, --screenshot-frame.

## Phase 9: VFX System Integration

- [x] 9.1 Port GPUParticleBuffer (SoA ping-pong structured buffers) into viewport — 50K particles, 6 SoA channels x2 sets, freeList/aliveCount/indirectArgs bookkeeping, expanded SRV heap to 64 entries, upload-buffer initialization.
- [x] 9.2 Port particle spawn compute shader (particle_spawn_cs.hlsl) — embedded k_ParticleSpawnCS, PCG hash RNG, cone-spread velocity, atomic freeList pop, compute root sig + PSO.
- [x] 9.3 Port particle update compute shader (particle_update_cs.hlsl) — gravity/damping physics, lifetime kill with freeList push, alpha/size fade, reads alive count from indirect args buffer.
- [x] 9.4 Port indirect args compute shader (particle_indirect_args_cs.hlsl) — single-thread dispatch, writes DrawInstanced args + DispatchIndirect args.
- [x] 9.5 Port billboard vertex/pixel shaders (particle_billboard_vs/ps.hlsl) — SV_VertexID billboards, camera-facing quads from invViewProj, soft circle PS, alpha blend PSO, ExecuteIndirect draw, Tab toggle.
- [x] 9.6 Implement soft particle depth fade — depth copy resource (R32_FLOAT), CopyResource before billboard pass, LinearizeDepth in PS, 30cm world-space fade distance.
- [x] 9.7 Port particle sort compute — bitonic merge sort with local (groupshared 256) + global merge phases, sign-bit-flipped distance keys for back-to-front, sortIndices read in billboard VS.
- [x] 9.8 Port flipbook animation — sub-UV atlas sampling with blend. Fixed root signature mismatch (VS/PS shared b0 and b2 structs unified into VFXBillboardConstants/VFXBillboardCB), added flipbook fields to VFXEmitterConfig, wired depth buffer SRV to PS, added flipbook presets (Fire Fountain=4x4 linear, Magic Orb=2x2 random).
- [x] 9.9 Port trail/ribbon renderer — adapted trail shaders to shared root signature (TrailRootConstants at b0, PerFrameCB at b1), added bindless/SM5 paths, integrated TrailSystem into demo with orbiting fire trail, dynamic upload VB/IB, Y key toggle.
- [x] 9.10 Port decal system — forward decal projection with OBB projector, DecalObjectCB at b2, DecalVS + DecalForwardPS, 4 demo ground decals, D key toggle.
- [x] 9.11 Port distortion pass — procedural heat-haze accumulation (DistortionHazePS) + fullscreen resolve (DistortionResolvePS), HDR copy + warp, X key toggle.
- [x] 9.12 Implement VFX emitter presets — 5 presets: Fire Fountain, Smoke Plume, Sparks, Magic Orb, Explosion Debris with distinct configs.
- [x] 9.13 Implement emitter cycling — Tab cycles presets (Shift+Tab toggles on/off), preset name shown in title bar.
- [x] 9.14 Implement VFX parameter tweaking — keys 1-9 adjust spawn rate/size/gravity/spread/lifetime/velocity/damping/radius, Shift=decrease, 90-frame display timer in title bar.

## Phase 9.5: Refactor to DemoApplication Pattern

- [x] 9.5.1 Refactored monolithic `tools/uz_viewport.cpp` (5095 lines, raw DX12, SM5.0/FXC) into `demos/vfx_viewport/` using DemoApplication base class with engine RHI/ShaderManager/PipelineLibrary/CBRingAllocator.
- [x] 9.5.2 Created 9 files: build.zig, build.zig.zon, main.cpp, vfx_viewport_demo.h/.cpp, vfx_viewport_mesh.h/.cpp, vfx_viewport_vfx.h/.cpp — all under 750 lines each.
- [x] 9.5.3 Deleted `tools/uz_viewport.cpp` and removed CMakeLists.txt uz_viewport target (lines 686-696).
- [x] 9.5.4 Verified API correctness against actual RHI headers: RHIGraphicsPipelineDesc/RHIComputePipelineDesc, CreateGraphicsPipeline/CreateComputePipeline, RHIIndexFormat::Uint32, SetVertexBuffer(slot, buffer), EditorGrid::Draw(cmd, cbRing, camera, ...), StatsOverlay::BeginFrame/Render, ScreenshotCapture::CaptureBackBuffer(cmd, swapChain).

## Phase 10: Advanced Features & Polish

- [x] 10.1 Implement multi-light support: 3 colored point lights (red/blue/green) with Karis 2013 inverse-square attenuation in mesh_ps.hlsl, PerFrameCB point light fields at c33.w-c39, L key toggle.
- [x] 10.2 Implement environment rotation: R key (15 deg step, Shift+R reverse) rotates sun direction around Y axis via azimuth angle, wraps [0, 360).
- [x] 10.3 Implement ground reflection: screen-space planar SSR on grid via HDR copy before grid pass, Schlick Fresnel (F0=0.04), reflected camera projection, bindless sampling with mip 2.0 blur, fade-modulated blending.
- [x] 10.4 Implement object rotation: Alt+LMB drag rotates preview mesh (yaw from horizontal, pitch from vertical drag, clamped ±89°), applied via Mat4::RotationX/Y to PerObjectCB model + normalMatrix.
- [x] 10.5 Implement background color presets: 5 presets (Sky/DarkGray/Black/White/NeutralGray) cycled with C key (B was taken by bloom). Non-sky presets skip sky shader, clear to preset color.
- [x] 10.6 Implement window resize handling: already handled by DemoApplication base class (WM_SIZE → swapchain resize → OnResize callback → DestroySizeDependent/CreateSizeDependent + bloom Resize).
- [x] 10.7 Implement DPI awareness: already implemented in PlatformWindow::Create() — calls SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2).
- [x] 10.8 Implement fullscreen toggle: Alt+Enter calls PlatformWindow::SetFullscreen()/SetWindowed(), uses existing DPI-aware borderless fullscreen infrastructure.
- [x] 10.9 Implement help overlay: H key prints comprehensive keyboard shortcut reference to console via UZ_LOG (27 bindings listed).
- [x] 10.10 Implement RenderDoc integration: RenderDocInit() + SetCaptureOptions() in main.cpp before device creation, F11 key triggers RenderDocTriggerCapture() in HandleInput().

## Bugfix: Build & PSO Blockers

- [x] B.1 Fix screenshot capture not saving. Root cause: CaptureBackBuffer() used RenderTarget→CopySrc barrier but DX12 tracked state was Present/Common (implicit DXGI promotion after Present()). Changed to CaptureFromPresent() which uses Present→CopySrc→Present, matching the tracked state. Added diagnostic logging.
- [x] B.2 Fix staging buffer lifetime violation causing DEVICE_HUNG (0x887A0006). Root cause: upload.ReleaseStaging() in OnInit() destroyed ID3D12Resource staging objects while the command list was still open; the command list is only submitted by demo_application.cpp after OnInit() returns. Fix: flush the command list (End/Execute/WaitIdle) before ReleaseStaging(), then re-open it (Begin()) so the caller can continue recording.
- [ ] B.3 Fix ShadowDepth_Bindless PSO creation failure (hr=0x887A0005): VS-only pipeline with DSV=D32_FLOAT, no PS, no RT. Investigate root signature compatibility, input layout mismatch, or DSV format issue with Agility SDK 1.619.1 (SM6.9). Check if shadow_depth_vs.hlsl root constants match the global bindless root signature.
- [ ] B.4 Fix ForwardPBR_Bindless PSO creation failure (hr=0x887A0005): mesh_vs + mesh_ps pipeline with 1 RT (R16G16B16A16_FLOAT) + DSV (D32_FLOAT). Same 0x887A0005 error as ShadowDepth. Likely shared root cause — investigate vertex input layout, root signature, or shader bytecode incompatibility.
- [ ] B.5 Fix BloomPass_Threshold compute PSO creation failure (hr=0x887A0005): bloom_threshold_cs.hlsl compute shader. Investigate if compute root signature differs from graphics, or if shader uses unsupported SM6.8+ features.
- [ ] B.6 Fix VFX_TrailPSO creation failure (hr=0x80070057 E_INVALIDARG): trail_vs + trail_ps pipeline. Different error code from other PSOs — likely PSO desc configuration error (input layout, topology, blend state). Inspect trail vertex layout and pipeline descriptor fields.
- [ ] B.7 Agility SDK version: verify D3D12SDKVersion=619 matches installed 1.619.1 DLLs in D3D12/ subdirectory. Confirm D3D12Core.dll is loaded (check log for Agility SDK activation message).
- [x] B.8 Fix zero particles spawning: free list and bookkeeping buffers never initialized with data. Added UploadInitialFreeList() — staging copy of [0..maxParticles-1] to m_freeList, maxParticles to m_freeListHead, 0 to m_aliveCount. Called from OnInit() after VFX Init(). Staging buffers stored as members, destroyed in Shutdown().
- [x] B.9 Fix invisible particles (stride mismatch): spawn/update compute shaders declared position/velocity buffers as `RWStructuredBuffer<float3>` (stride 12) but C++ created buffers with stride 16 (float4) and billboard VS read as `StructuredBuffer<float4>`. With SM6.6 bindless, HLSL type determines indexing stride, so element[1] at byte 12 (CS) vs byte 16 (VS) caused total data misalignment. Fixed all VFX compute shaders (spawn, update, sort, mesh_instance) to use float4 for positions/velocities, packing float3 with w=0.

## Testing

- [ ] T.1 Visual test: camera WASD movement works in all directions, sprint multiplier visible
- [ ] T.2 Visual test: mouse look (RMB) rotates smoothly, pitch clamp at +/-89 deg
- [ ] T.3 Visual test: orbit mode (MMB) rotates around center, scroll zoom works
- [ ] T.4 Visual test: sky renders with blue gradient, sun disc visible, time-of-day changes color
- [ ] T.5 Visual test: infinite grid visible with minor/major lines, distance fade, axis colors
- [ ] T.6 Visual test: PBR sphere shows correct specular highlight, material presets look distinct
- [ ] T.7 Visual test: shadows visible on ground plane, PCF soft edges, no acne
- [ ] T.8 Visual test: bloom visible on bright specular highlights and emissive materials
- [ ] T.9 Visual test: debug views (albedo, normals, roughness, depth) show correct data
- [ ] T.10 Visual test: stats overlay shows FPS, frame time, camera info
- [ ] T.11 Visual test: VFX particles spawn, animate, and render with soft depth fade
- [ ] T.12 Visual test: VFX trails render as ribbon strips behind moving particles
- [ ] T.13 Visual test: screenshot capture produces valid PNG file
- [ ] T.14 Headless test: --headless --screenshot-stdout --frames 10 produces base64 PNG on stdout

## Documentation

- [x] D.1 Create docs/guides/vfx-viewport-controls.md — keyboard shortcuts, mouse controls, CLI arguments. Complete reference with camera, mesh/material, lighting, post-processing, shadow, VFX, utility controls + all CLI args.
- [x] D.2 Create docs/guides/vfx-viewport-architecture.md — rendering pipeline (15-stage frame order), CB layout (768-byte PerFrameCB), root sig, GPU resources, VFX compute pipeline, material/emitter presets.
- [x] D.3 Update docs/engine/25-vfx/README.md with viewport documentation links. Added links to vfx-viewport-controls.md and vfx-viewport-architecture.md, fixed stale tools/uz_viewport.exe path to demos/vfx_viewport/.
- [x] D.4 Add VFX system section to project README with feature list (particles, trails, decals, distortion, flipbook, soft depth fade), viewport demo entry in directory structure, and links to controls/architecture docs. Screenshot image pending build (thirdparty DLLs needed).
