/// Private state and render graph passes of the scene deferred renderer.
///
/// This file is #included inside the SceneRenderer class body. Do not include directly.

// ─────────────────────────────────────────────────────────────────────────────
// scene_renderer_impl.h — Private members of SceneRenderer.
// This file is #included inside the SceneRenderer class body.
// DO NOT include this file directly — include scene_renderer.h instead.
// ─────────────────────────────────────────────────────────────────────────────

    IRHIDevice* m_device = nullptr;
    bool m_editorMode = false;            ///< When true, editor grid + stats overlay are rendered.
    bool m_gameViewMode = false;          ///< When true (G key), all editor overlays are hidden.
    bool m_flipbookDebugEnabled = false;  ///< When true, a debug flipbook quad is drawn in the top-right corner (VFX verification).
    uint32_t m_editorCameraInitFrame = 0;   ///< Frame counter for deferred editor camera init (waits for Lua scripts).
    Entity m_selectedEntity{};            ///< Currently selected entity for editor outline/gizmo rendering.
    uint8_t m_gizmoMode = 0;             ///< GizmoMode: 0=Translate, 1=Rotate, 2=Scale.
    uint8_t m_gizmoSpace = 0;            ///< GizmoSpace: 0=World, 1=Local.
    int m_gizmoHoveredAxis = -1;         ///< Hovered axis: -1=none, 0-6=axis/plane/center.
    int m_gizmoActiveAxis = -1;          ///< Active (dragging) axis: same encoding.
    EditorInputHandler* m_editorInputHandler = nullptr; ///< Non-owning pointer set by the application layer.

    // ── Engine Infrastructure ──────────────────────────────────────────────

    std::unique_ptr<InputSystem> m_input;
    std::unique_ptr<ShaderManager> m_shaderMgr;
    std::unique_ptr<PipelineLibrary> m_pipelines;
    std::unique_ptr<CBRingAllocator> m_cbRing;
    std::unique_ptr<EditorGrid> m_editorGrid;
    std::unique_ptr<OutlinePass> m_outlinePass;
    std::unique_ptr<GizmoRenderer> m_gizmoRenderer;
    std::unique_ptr<StatsOverlay> m_statsOverlay;
    std::unique_ptr<VfxEditorOverlay> m_vfxEditorOverlay;
    std::unique_ptr<CameraManager> m_cameraManager;
    std::unique_ptr<FlyCameraController> m_cameraCtrl;
    std::unique_ptr<ViewportToolbar> m_viewportToolbar;
    bool m_viewportToolbarInitialized = false; ///< True after ViewportToolbar::Init() succeeds.
    std::unique_ptr<TechPanel> m_techPanel;    ///< Runtime lighting-technology switchboard (F3 / r.TechPanel.Show).
    bool m_techPanelInitialized = false;       ///< True after TechPanel::Init() succeeds.
    CameraId m_editorFreeCamId = kInvalidCameraId;     ///< Editor free-cam ID (kInvalidCameraId = not active).
    std::unique_ptr<rg::RgResourcePool> m_rgPool;

    // ── Fallback Textures (SM5 unconditional sampling) ────────────────────

    RHITextureHandle m_fallbackWhite;  ///< 1x1 RGBA8 white (255,255,255,255) — default for missing PBR maps.
    RHITextureHandle m_fallbackBlack;  ///< 1x1 RGBA8 black (0,0,0,255) — default for metallic map.
    RHITextureHandle m_fallbackNormal; ///< 1x1 RGBA8 flat normal (128,128,255,255) — default for missing normal maps.

    // ── GBuffer + Shadow Atlas ─────────────────────────────────────────────

    GBuffer m_gbuffer;
    ShadowAtlas m_shadowAtlas;                     ///< Cascade shadow map depth atlas (dynamic per-frame).
    ShadowCacheManager m_shadowCacheManager;       ///< Manages cached static shadow maps.
    RHITextureHandle m_staticShadowAtlas;           ///< Separate depth atlas for cached static shadows.
    uint32_t m_staticShadowAtlasSrv = UINT32_MAX;  ///< SRV heap index for static shadow atlas.
    LocalLightShadowAtlas m_localShadowAtlas;      ///< Local light (spot/point) shadow depth atlas.

    // ── DBuffer (Deferred Decals) ─────────────────────────────────────────
    /// @ref Valient 2014 "Deferred Rendering in Killzone: Shadow Fall"

    RHITextureHandle m_dbufferA;                    ///< RGBA8Unorm — decal albedo (RGB) + opacity (A).
    RHITextureHandle m_dbufferB;                    ///< RGBA8Unorm — decal normal [0,1] (RGB) + blend (A).
    RHITextureHandle m_dbufferC;                    ///< RGBA8Unorm — roughness (R) + metallic (G) + AO (B) + blend (A).
    uint32_t m_dbufferASrv = UINT32_MAX;            ///< SRV index for DBuffer A.
    uint32_t m_dbufferBSrv = UINT32_MAX;            ///< SRV index for DBuffer B.
    uint32_t m_dbufferCSrv = UINT32_MAX;            ///< SRV index for DBuffer C.
    uint32_t m_dbufferAUav = UINT32_MAX;            ///< UAV index for DBuffer A (unused — apply pass uses GBuffer UAVs).
    uint32_t m_dbufferBUav = UINT32_MAX;            ///< UAV index for DBuffer B (unused).
    uint32_t m_dbufferCUav = UINT32_MAX;            ///< UAV index for DBuffer C (unused).
    std::vector<DecalInfo> m_frameDecals;            ///< Per-frame sorted decal array (alive until Execute completes).

    // ── HDR Scene Color (intermediate before tonemap) ───────────────────
    RHITextureHandle m_hdrSceneColor;           ///< RGBA16F HDR render target for scene color.
    uint32_t m_hdrSceneColorSrv = UINT32_MAX;   ///< SRV for tonemap pass to read.
    bool m_hdrHasContent = false;               ///< True after the first render graph execution fills the HDR target.

    // ── Debug Lighting Split Targets (MRT outputs from deferred lighting) ──
    RHITextureHandle m_debugDirectLight;        ///< RT1: direct lighting (Lo = sun * shadow).
    uint32_t m_debugDirectLightSrv = UINT32_MAX;
    RHITextureHandle m_debugIndirectLight;      ///< RT2: indirect lighting (ambient = SH/GI/IBL).
    uint32_t m_debugIndirectLightSrv = UINT32_MAX;
    RHITextureHandle m_debugShadowMask;         ///< RT3: shadow mask (R=shadow, G=AO).
    uint32_t m_debugShadowMaskSrv = UINT32_MAX;

    // ── Atmosphere LUT ─────────────────────────────────────────────────────

    RHITextureHandle m_transmittanceLut;         ///< 256x64 RGBA16F precomputed transmittance LUT.
    uint32_t m_transmittanceLutSrv = UINT32_MAX; ///< SRV index for sky shader sampling.
    uint32_t m_transmittanceLutUav = UINT32_MAX; ///< UAV index for compute write.
    bool m_transmittanceLutDirty = true;         ///< True if LUT needs to be (re)computed.
    bool m_transmittanceLutFilled = false;       ///< True once the LUT has been computed at least once (state = SRV).

    // ── Aerial Perspective Volume ────────────────────────────────────────

    RHITextureHandle m_apVolume;                   ///< 32x32x16 RGBA16F 3D AP volume texture.
    uint32_t m_apVolumeSrv = UINT32_MAX;           ///< SRV index for deferred lighting sampling.
    uint32_t m_apVolumeUav = UINT32_MAX;           ///< UAV index for compute write.
    bool m_apVolumeFilled = false;                 ///< True once AP volume has been dispatched at least once (state = UAV).

    // ── Multi-Scattering LUT ──────────────────────────────────────────────

    RHITextureHandle m_multiScatterLut;             ///< 32x32 R11G11B10F precomputed multi-scattering LUT.
    uint32_t m_multiScatterLutSrv = UINT32_MAX;     ///< SRV index for sky shader sampling.
    uint32_t m_multiScatterLutUav = UINT32_MAX;     ///< UAV index for compute write.
    bool m_multiScatterLutDirty = true;             ///< True if multi-scatter LUT needs recompute.
    bool m_multiScatterLutFilled = false;           ///< True once the LUT has been computed.

    // ── Bruneton 3D Scattering LUT (256x128x32 RGBA16F) ────────────────
    RHITextureHandle m_scatteringLut3D;             ///< Combined scattering (Rayleigh.rgb + Mie.r packed).
    uint32_t m_scatteringLut3DSrv = UINT32_MAX;     ///< SRV for sky shader lookup.
    uint32_t m_scatteringLut3DUav = UINT32_MAX;     ///< UAV for precompute write.
    bool m_scatteringLut3DDirty = true;             ///< True if LUT needs recompute.
    bool m_scatteringLut3DFilled = false;           ///< True once precomputed.

    // ── Delta Rayleigh 3D (256x128x32 RGBA16F, temp for precompute) ───
    RHITextureHandle m_deltaRayleigh3D;
    uint32_t m_deltaRayleigh3DSrv = UINT32_MAX;
    uint32_t m_deltaRayleigh3DUav = UINT32_MAX;

    // ── Delta Mie 3D (256x128x32 RGBA16F, temp for precompute) ────────
    RHITextureHandle m_deltaMie3D;
    uint32_t m_deltaMie3DSrv = UINT32_MAX;
    uint32_t m_deltaMie3DUav = UINT32_MAX;

    // ── Scattering Density 3D (256x128x32 RGBA16F, temp for precompute)
    RHITextureHandle m_scatteringDensity3D;
    uint32_t m_scatteringDensity3DSrv = UINT32_MAX;
    uint32_t m_scatteringDensity3DUav = UINT32_MAX;

    // ── Irradiance LUT (64x16 RGBA16F) ────────────────────────────────
    RHITextureHandle m_irradianceLut;
    uint32_t m_irradianceLutSrv = UINT32_MAX;
    uint32_t m_irradianceLutUav = UINT32_MAX;

    // ── Delta Irradiance (64x16 RGBA16F, temp for precompute) ──────────
    RHITextureHandle m_deltaIrradiance;
    uint32_t m_deltaIrradianceSrv = UINT32_MAX;
    uint32_t m_deltaIrradianceUav = UINT32_MAX;

    // ── Path Tracer accumulation (Step 6) ──────────────────────────────
    // Ping-pong RGBA32F textures: read prev as SRV, write curr as render
    // target. Sample count + previous view matrix track convergence and
    // camera-cut reset. @ref Kajiya 1986 — Monte Carlo accumulation.
    RHITextureHandle m_ptAccum[2]    = { RHITextureHandle::Invalid(), RHITextureHandle::Invalid() };
    uint32_t         m_ptAccumSrv[2] = { UINT32_MAX, UINT32_MAX };
    uint32_t         m_ptAccumIdx    = 0;       ///< Current write target (flips each frame).
    uint32_t         m_ptAccumSamples = 0;      ///< Accumulated sample count since last reset.
    float            m_ptPrevView[16] = {0};    ///< Previous view matrix (camera-cut detection).
    bool             m_ptHistoryValid = false;  ///< False on first frame / after reset.

    // ── IBL (Image-Based Lighting from sky cubemap) ────────────────────

    std::unique_ptr<IBLBaker> m_iblBaker;            ///< GPU compute pipeline for IBL bake (irradiance + prefiltered).
    RHITextureHandle m_skyCubemap;                 ///< 64x64x6 RGBA16F cubemap for sky capture.
    uint32_t m_skyCubemapSrv = UINT32_MAX;         ///< SRV index for IBL baker input.
    uint32_t m_skyCubemapUav = UINT32_MAX;         ///< UAV index for compute write.
    bool m_skyIBLDirty = true;                     ///< True when sky cubemap needs re-capture.
    /// Sun direction, sky sun colour, angular radius and passes-since-bake of the last sky IBL
    /// bake (AtmosphereFrameContext::skyBakeSignature). -1e30 = not baked yet.
    float m_skyBakeSignature[8] = {-1e30f, -1e30f, -1e30f, -1e30f, -1e30f, -1e30f, -1e30f, 0.f};
    bool m_skyCubemapFilled = false;               ///< True once the sky cubemap has been captured at least once.
    IBLBaker::BakeResult m_iblResult;              ///< Irradiance + prefiltered SRV indices.
    uint32_t m_brdfLutSrvIndex = UINT32_MAX;       ///< BRDF LUT SRV (baked once at init).
    SHCoefficients m_skyIrradianceSH;              ///< L2 SH for sky diffuse irradiance.

    // ── Environment Map Importance Sampling ──────────────────────────────
    std::unique_ptr<EnvMapSampler> m_envMapSampler;  ///< Alias table + luminance pyramid for MIS.
    std::unique_ptr<EnvMISPass>    m_envMISPass;     ///< Temporal accumulation pass for env MIS specular.
    bool m_envMisEnabled = true;                     ///< r.EnvMIS, read once per frame (frame_constants.cpp).

    // ── HDRI environment override ────────────────────────────────────────
    RHITextureHandle m_hdriCubemap;            ///< Cubemap converted from HDRI equirect (owned, must destroy).
    uint32_t m_hdriCubemapSrv = UINT32_MAX;    ///< SRV for the HDRI cubemap.
    bool m_hdriActive = false;                 ///< True when using external HDRI instead of procedural sky.

    // ── Reflection Probes ────────────────────────────────────────────────
    ReflectionProbeRenderer m_reflectionProbes;   ///< Per-probe cubemap capture, IBL bake, and GPU upload.

    // ── Material Graph Pipeline ──────────────────────────────────────────
    std::unique_ptr<MaterialPipeline> m_materialPipeline; ///< Compiles .uzmat graphs into GBuffer PSOs.
    std::unique_ptr<MaterialManager>  m_materialManager;  ///< Loads and caches .uzmat graph files.

    // ── Ray Tracing (DXR) ────────────────────────────────────────────────
    std::unique_ptr<RTManager> m_rtManager;          ///< BLAS/TLAS/SBT lifecycle, gated by r.RayTracing CVar.
    std::unique_ptr<RTUVTestPass> m_rtUVTestPass;    ///< RT UV gradient test pass (r.RayTracing.TestPass).

    // ── Auto-Exposure ──────────────────────────────────────────────────────
    std::unique_ptr<AutoExposure> m_autoExposure;   ///< GPU histogram-based eye adaptation.

    // ── GTAO ─────────────────────────────────────────────────────────────
    std::unique_ptr<GTAOPass> m_gtaoPass;            ///< GTAO ambient occlusion compute pass.

    // ── Bloom ────────────────────────────────────────────────────────────
    std::unique_ptr<BloomPass> m_bloomPass;          ///< GPU compute bloom (threshold + downsample + upsample).

    // ── Lens Flare ──────────────────────────────────────────────────────
    std::unique_ptr<LensFlarePass> m_lensFlarePass;  ///< Screen-space lens flare (ghosts + halo).
    bool m_lensFlareEnabled = false;                  ///< Runtime toggle. Default OFF — enable via cvar or Lua.
    float m_lensFlareIntensity      = 1.0f;          ///< CVar-driven overall lens flare intensity.
    float m_lensFlareGhostIntensity = 0.5f;          ///< CVar-driven ghost contribution multiplier.
    float m_lensFlareHaloRadius     = 0.6f;          ///< CVar-driven halo ring radius (UV).
    float m_lensFlareChromaticShift = 0.02f;         ///< CVar-driven chromatic aberration offset.

    // ── Volumetric Fog (froxel-based) ─────────────────────────────────
    std::unique_ptr<VolumetricFogPass> m_volFogPass;   ///< 160×90×64 froxel fog pipeline (inject + scatter + composite).

    // ── Volumetric Clouds ───────────────────────────────────────────────
    std::unique_ptr<CloudPass> m_cloudPass;           ///< GPU volumetric cloud pipeline (5 compute shaders).
    IRHICommandList* m_cloudCmdList = nullptr;         ///< Dedicated command list for cloud compute dispatches.
    uint32_t m_cloudOutputSrv = UINT32_MAX;           ///< SRV of the final cloud output for compositing.
    std::unique_ptr<CloudPassInput> m_pendingCloudInput; ///< Deferred cloud input for pre-graph compute dispatch.
    bool m_pendingCloudDispatch = false;               ///< True when cloud compute should run before render graph.
    bool m_cloudOutputReady = false;                   ///< True after the first cloud compute dispatch has completed.

    // ── Distance Field Shadows ───────────────────────────────────────────

    DistanceFieldShadowConfig m_dfsConfig;             ///< DFS runtime configuration.
    SdfAtlasManager* m_sdfAtlas = nullptr;             ///< Externally-owned SDF atlas manager (may be null).
    RHITextureHandle m_dfsOutputTexture;               ///< RG16F DFS output (shadow_factor, depth).
    uint32_t m_dfsOutputSrvIndex = UINT32_MAX;         ///< SRV index for lighting pass to read DFS result.
    uint32_t m_dfsOutputUavIndex = UINT32_MAX;         ///< UAV index for cone trace pass to write.
    RHIBufferHandle  m_dfsCulledIndicesBuffer;         ///< StructuredBuffer<uint> culled SDF object indices.
    uint32_t m_dfsCulledIndicesSrvIndex = UINT32_MAX;  ///< SRV for cone trace to read culled list.
    uint32_t m_dfsCulledIndicesUavIndex = UINT32_MAX;  ///< UAV for cull pass to append indices.
    RHIBufferHandle  m_dfsIndirectArgsBuffer;          ///< RWByteAddressBuffer for indirect dispatch args.
    uint32_t m_dfsIndirectArgsUavIndex = UINT32_MAX;   ///< UAV for cull pass to write indirect args.


    // ── Color Grading LUT (3D volume texture) ──────────────────────────────

    static constexpr uint32_t kColorGradingLutSize = 32;

    RHITextureHandle m_colorGradingLut;                ///< 32x32x32 RGBA8 3D LUT for tonemapping.
    uint32_t m_colorGradingLutSrv = UINT32_MAX;        ///< SRV index for tonemap shader sampling.
    uint32_t m_colorGradingLutUav = UINT32_MAX;        ///< UAV index for LUT generation compute write.
    bool m_colorGradingLutDirty = true;                ///< True when LUT needs (re)generation.

    /// Creates the 3D LUT texture. Called from Init().
    void CreateColorGradingLutResources();

    /// Destroys the 3D LUT texture. Called from Shutdown().
    void DestroyColorGradingLutResources();

    // ── Motion Blur ──────────────────────────────────────────────────────

    RHITextureHandle m_motionBlurOutput;               ///< RGBA16F temp texture for motion blur result.
    uint32_t m_motionBlurOutputSrv = UINT32_MAX;       ///< SRV index for subsequent passes to read.
    uint32_t m_motionBlurOutputUav = UINT32_MAX;       ///< UAV index for motion blur compute write.

    /// Tile max texture: RG16F at (width/16) x (height/16), max velocity per tile.
    RHITextureHandle m_motionBlurTileMax;               ///< RG16F tile max velocity texture.
    uint32_t m_motionBlurTileMaxSrv = UINT32_MAX;       ///< SRV for neighbor max pass to read.
    uint32_t m_motionBlurTileMaxUav = UINT32_MAX;       ///< UAV for tile max compute to write.

    /// Neighbor max texture: RG16F at same tile dimensions, 3x3 neighborhood max.
    RHITextureHandle m_motionBlurNeighborMax;            ///< RG16F neighbor max velocity texture.
    uint32_t m_motionBlurNeighborMaxSrv = UINT32_MAX;   ///< SRV for motion blur gather to read.
    uint32_t m_motionBlurNeighborMaxUav = UINT32_MAX;   ///< UAV for neighbor max compute to write.

    float m_motionBlurAmount = 0.5f;                   ///< Motion blur intensity scale.
    float m_motionBlurMax    = 5.0f;                   ///< Maximum blur radius as percentage of viewport width.
    bool  m_motionBlurEnabled = true;                  ///< Runtime toggle for motion blur.

    int32_t m_motionBlurTargetFPS = 30;                ///< Target FPS for velocity time scaling.

    /// Creates motion blur output, tile max, and neighbor max textures.
    /// Called from Init() and Resize().
    void CreateMotionBlurResources(uint32_t width, uint32_t height);

    /// Destroys all motion blur textures. Called from Shutdown() and Resize().
    void DestroyMotionBlurResources();

    // ── CAS (Contrast Adaptive Sharpening) ─────────────────────────────

    bool  m_casEnabled = true;              ///< CAS adaptive sharpening (default ON).
    float m_casSharpenStrength = 0.15f;     ///< TEST 1: CAS only.

    // ── FXAA (LDR intermediate for post-tonemap AA) ────────────────────

    RHITextureHandle m_fxaaLdrIntermediate;             ///< RGBA8Unorm LDR intermediate for CAS/FXAA input.
    uint32_t m_fxaaLdrIntermediateSrv = UINT32_MAX;     ///< SRV for CAS/FXAA to read tonemap output.
    RHIResourceState m_ldrFinalState = RHIResourceState::ShaderResource; ///< LDR state after render graph.

    /// Creates the FXAA LDR intermediate texture at the given resolution.
    /// Called from Init() and Resize().
    void CreateFxaaResources(uint32_t width, uint32_t height);

    /// Destroys the FXAA LDR intermediate texture. Called from Shutdown() and Resize().
    void DestroyFxaaResources();

    // ── Depth of Field ───────────────────────────────────────────────────

    RHITextureHandle m_dofCocTexture;                  ///< R16F signed CoC per pixel.
    uint32_t m_dofCocSrv = UINT32_MAX;                 ///< SRV for DOF blur to read CoC.
    uint32_t m_dofCocUav = UINT32_MAX;                 ///< UAV for DOF setup to write CoC.
    RHITextureHandle m_dofOutputTexture;               ///< RGBA16F blurred DOF result.
    uint32_t m_dofOutputSrv = UINT32_MAX;              ///< SRV for tonemap to read DOF result.
    uint32_t m_dofOutputUav = UINT32_MAX;              ///< UAV for DOF blur to write.

    float m_dofFocusDistance  = 0.0f;                  ///< Focus distance in cm. 0 = disabled.
    float m_dofAperture       = 4.0f;                  ///< F-stop number.
    bool  m_dofEnabled        = false;                 ///< DOF is off by default (default).

    float m_dofFocalRegion    = 0.0f;                  ///< Width of the in-focus zone in cm (centered on FocusDistance).

    float m_dofNearTransition = 300.0f;                ///< Near transition ramp width in cm.

    float m_dofFarTransition  = 500.0f;                ///< Far transition ramp width in cm.

    /// Creates DOF textures. Called from Init() and Resize().
    void CreateDofResources(uint32_t width, uint32_t height);

    /// Destroys DOF textures. Called from Shutdown() and Resize().
    void DestroyDofResources();

    // ── TAA (Temporal Anti-Aliasing) ─────────────────────────────────────────

    RHITextureHandle m_taaHistory[2];          ///< Ping-pong RGBA16F history textures.
    uint32_t m_taaHistorySrv[2] = {UINT32_MAX, UINT32_MAX}; ///< SRV indices for history read.
    uint32_t m_taaHistoryUav[2] = {UINT32_MAX, UINT32_MAX}; ///< UAV indices for history write.
    uint32_t m_taaHistoryIndex = 0;            ///< Index of current write target (0 or 1).
    bool     m_taaEnabled = true;              ///< Enables jitter + temporal upscaler dispatch (DLSS/TSR/TAA).
    Mat4     m_prevViewProj;                   ///< Previous frame's unjittered viewProj for reprojection.

    /// Region dump (diagnostics): writes `<dir>/region_<frame>.bin` from readback slot `slot`
    /// and validates the GPU frame stamps against the frame that slot holds. Every value the
    /// header needs was captured when the copy was queued, so this also runs at shutdown to
    /// flush the frames still in flight (otherwise the last frames of a range would be missing).
    void WriteRegionDumpFile(uint32_t slot);

    // ── Region dump (diagnostics, SetRegionDump) ───────────────────────────
    std::string                    m_regionDumpDir;
    std::vector<RegionRect>        m_regionRects;
    std::vector<uint32_t>          m_regionRectBase;      ///< float4 offset of each rect's records.
    uint32_t         m_regionFirstFrame = 0, m_regionLastFrame = 0;
    uint32_t         m_regionFields = 0;                  ///< Field-group bitmask (region_dump.hlsli).
    uint32_t         m_regionRecordsPerPixel = 0;
    uint64_t         m_regionBufferBytes = 0;
    RHIBufferHandle  m_regionDumpBuffer{};
    RHIBufferHandle  m_regionRectBuffer{};                ///< Upload buffer holding the rect table.
    uint32_t         m_regionDumpUav = UINT32_MAX;
    uint32_t         m_regionRectSrv = UINT32_MAX;
    RHIBufferHandle  m_regionReadback[3]{};
    uint32_t         m_regionReadbackFrame[3] = {UINT32_MAX, UINT32_MAX, UINT32_MAX};
    float            m_regionReadbackJitter[3][2] = {};
    float            m_regionReadbackExposure[3] = {};
    uint32_t         m_regionReadbackJitterIdx[3] = {};
    uint32_t         m_regionReadbackRes[3][4] = {};   ///< renderW, renderH, outputW, outputH per slot.
    Mat4             m_regionReadbackViewProj[3]{};   ///< Unjittered view-projection of the frame in the slot.
    Mat4             m_regionReadbackPrevViewProj[3]{};
    Vec3             m_regionReadbackCamera[3]{};     ///< Camera position of the frame in the slot.
    bool             m_regionReadbackTaa[3] = {};      ///< TAA active on the frame the slot holds.
    bool             m_regionDumpRequested = false;
    bool             m_regionDumpActive = false;
    uint32_t         m_regionStampMismatches = 0;
    uint32_t         m_regionFilesWritten = 0;
    float            m_lastFrameExposure = 1.0f;
    bool     m_prevViewProjValid = false;      ///< False on first frame (no valid previous viewProj).

    // ── CB Update Optimization (dirty flag skip) ────────────────────────
    ShadowDataCB m_prevShadowCB{};              ///< Cached previous frame shadow CB for memcmp skip.
    CBAllocation m_prevShadowAlloc{};            ///< Previous frame shadow CB allocation (reused when skipped).
    bool         m_prevShadowValid = false;      ///< False on first frame.
    uint32_t     m_cbUpdatesSkipped = 0;         ///< Per-frame counter: number of CB sections skipped.
    uint32_t     m_cbUpdatesTotal   = 0;         ///< Per-frame counter: total CB sections considered.

    // ── Temporal Upscaler (ITemporalUpscaler) ────────────────────────────────

    std::unique_ptr<ITemporalUpscaler> m_upscaler;         ///< Active temporal upscaler (TAA or TSR).
    UpscalerType m_upscalerType = UpscalerType::TAA;        ///< Default TAA. DLSS opt-in via r.AntiAliasingMethod=4.

    // ── Dynamic Resolution ───────────────────────────────────────────────────

    std::unique_ptr<DynamicResolutionController> m_dynRes;  ///< GPU-bound adaptive resolution controller.

    /// Creates TAA history textures at the given resolution.
    /// Called from Init() and Resize().
    void CreateTaaResources(uint32_t width, uint32_t height);

    /// Destroys TAA history textures. Called from Shutdown() and Resize().
    void DestroyTaaResources();

    // ── Pass Inspector (render target visualization + validation) ────────────
    PassInspector m_passInspector;  ///< Visual GPU pipeline debugger.

    // ── SSGI (Screen-Space Global Illumination) ─────────────────────────────
    SSGIRenderer m_ssgiRenderer;  ///< GPU SSGI subsystem (5 compute passes).

    // ── SDF-based GI (Global Distance Field + Radiance Cache) ──────────────
    GlobalDistanceField m_globalDistanceField;   ///< 4-cascade clipmap GDF.
    RadianceCache       m_radianceCache;         ///< 16^3 world-space probe grid for diffuse GI.
    bool m_sdfGiEnabled = false;                 ///< Runtime toggle for SDF-based GI.
    float m_giIntensity = 1.0f;                  ///< GI intensity multiplier (default 1.0).

    // ── Light Probe Volume (author-placed irradiance grid) ───────────────
    /// Author-placed L2 SH probe volume — populated each frame from the
    /// `LightProbeVolumeComponent` (auto-rebuild on dirty) and seeded from
    /// the captured sky cubemap. Sampled in deferred lighting via trilinear
    /// blend of 8 surrounding probes.
    /// @ref Tatarchuk 2005 "Irradiance Volumes for Games"
    /// @ref Greger 1998 "The Irradiance Volume"
    IrradianceProbeGrid m_lightProbeGrid;

    // ── DDGI octahedral atlases ───────────────────────────────────────
    /// Owns the DDGI irradiance + depth probe atlases used by the DXR
    /// probe-update CS and the deferred lighting Chebyshev sampler.
    /// @ref Majercik 2019 "Dynamic Diffuse Global Illumination"
    DDGISystem m_ddgiSystem;
    bool m_ddgiSkipLogged = false; ///< One warning per run when r.RayTracing.GI is set but DDGI cannot run (frame_gi_passes.cpp).

    // ── VFX -> DDGI emissive injection (phase16 lumen-gi-contribution) ─
    /// GPU buffer holding up to 32 top-emissive particle samples, fed
    /// into the DDGI probe-update CS so explosions / fire / magic VFX
    /// contribute bounce light to the probe grid. The on-frame gather
    /// + UploadFrame wiring lives in
    /// `SceneRenderer::GatherAndUploadVfxEmissiveSamples`; the actual
    /// probe-update CS reading the buffer is gated by
    /// `phase16_vfx-lumen-gi-hardware-validation` (CB layout extension
    /// requires DX12 to verify).
    VfxDDGIEmissiveFeed m_vfxDDGIEmissiveFeed;

    // ── Brixelizer GI (AMD FidelityFX sparse SDF voxel GI) ─────────────────
    std::unique_ptr<BrixelizerGIPass> m_brixelizerGI;  ///< FFX Brixelizer GI pass (diffuse + specular indirect).

    // ── Clustered light lists (block B.7) ──────────────────────────────────
    RHIBufferHandle  m_clusterListBuffer;           ///< StructuredBuffer<uint>: per cluster a count and kClusterSlotsMax light slots.
    uint64_t         m_clusterListBytes = 0;        ///< Capacity; grows with the render resolution.
    uint32_t         m_clusterListSrv = UINT32_MAX; ///< Read by the deferred lighting (LightingCB::clusterListSrv).
    uint32_t         m_clusterListUav = UINT32_MAX; ///< Written by the cull pass.
    RHIResourceState m_clusterListState = RHIResourceState::Common;

    // ── SSR (Screen-Space Reflections) ─────────────────────────────────────

    RHITextureHandle m_ssrTraceOutput;             ///< RGBA16F trace result (rgb=hit color, a=confidence).
    uint32_t m_ssrTraceOutputSrv = UINT32_MAX;     ///< SRV for resolve pass to read trace result.
    uint32_t m_ssrTraceOutputUav = UINT32_MAX;     ///< UAV for trace pass to write.
    RHITextureHandle m_ssrResolvedOutput;           ///< RGBA16F resolved SSR (rgb=Fresnel-weighted, a=blend weight).
    uint32_t m_ssrResolvedOutputSrv = UINT32_MAX;  ///< SRV for lighting pass to read SSR result.
    uint32_t m_ssrResolvedOutputUav = UINT32_MAX;  ///< UAV for resolve pass to write.
    RHITextureHandle m_ssrHistory[2];              ///< Ping-pong RGBA16F history textures for SSR temporal accumulation.
    uint32_t m_ssrHistorySrv[2] = {UINT32_MAX, UINT32_MAX}; ///< SRV indices for history read.
    uint32_t m_ssrHistoryUav[2] = {UINT32_MAX, UINT32_MAX}; ///< UAV indices for history write.
    uint32_t m_ssrHistoryIndex = 0;                ///< Current write target index (0 or 1), flips each frame.
    bool     m_ssrEnabled = true;                  ///< Runtime toggle for SSR.

    /// Tile classification buffers for indirect SSR dispatch.
    /// @ref AMD FidelityFX SSSR — tile classify + ray compaction + ExecuteIndirect.
    RHIBufferHandle m_ssrRayList;                  ///< StructuredBuffer<uint> — packed pixel coords (max = width*height).
    uint32_t m_ssrRayListUav = UINT32_MAX;         ///< UAV for classify to write, trace to read.
    uint32_t m_ssrRayListSrv = UINT32_MAX;         ///< SRV for trace pass to read ray coords.
    RHIBufferHandle m_ssrRayCounterArgs;           ///< ByteAddressBuffer — [0]=rayCount, [4..15]=DispatchIndirectArgs.
    uint32_t m_ssrRayCounterArgsUav = UINT32_MAX;  ///< UAV for classify/prepare to write.

    /// Hit distance from SSR trace — used by temporal pass for virtual hit reprojection.
    /// @ref AMD FidelityFX SSSR — hit distance stored separately for temporal reprojection.
    RHITextureHandle m_ssrHitDistance;              ///< R16F hit distance (screen-space ray t).
    uint32_t m_ssrHitDistanceSrv = UINT32_MAX;     ///< SRV for temporal pass to read.
    uint32_t m_ssrHitDistanceUav = UINT32_MAX;     ///< UAV for trace pass to write.

    /// STBN (Spatiotemporal Blue Noise) 128x128x64 RG8 volume texture.
    /// Each slice is a blue noise pattern; consecutive slices are temporally decorrelated.
    /// Used by SSR trace for low-discrepancy per-pixel random seeds.
    /// @ref Heitz & Belcour 2019 "Distributing Monte Carlo Errors as Blue Noise"
    RHITextureHandle m_stbnTexture;                ///< RG8Unorm 128x128x64 3D texture.
    uint32_t m_stbnSrv = UINT32_MAX;              ///< SRV for trace pass blue noise lookup.

    /// Previous frame HDR scene color copy — used as SSR trace input to break
    /// the feedback loop (UE5 pattern: read prev TAA output, not current HDR).
    RHITextureHandle m_ssrPrevSceneColor;          ///< RGBA16F copy of prev frame's HDR.
    uint32_t m_ssrPrevSceneColorSrv = UINT32_MAX;  ///< SRV for SSR trace to read.
    bool     m_ssrHasPrevColor = false;            ///< False on first frame (skip SSR).
    float    m_ssrPrevExposure = 1.0f;             ///< Previous frame's exposure for SSR color correction.

    /// Creates SSR output textures at the given resolution.
    /// Called from Init() and Resize().
    void CreateSsrResources(uint32_t width, uint32_t height);

    /// HDR scene color copy for forward translucent pass screen-space refraction.
    /// Copied from the HDR target after deferred lighting, before translucent draws.
    /// @ref Three.js transmissionRenderTarget — same pattern.
    RHITextureHandle m_translucentSceneColor;            ///< RGBA16F copy of current frame HDR.
    uint32_t m_translucentSceneColorSrv = UINT32_MAX;    ///< SRV for translucent shader to sample.

    /// Destroys SSR output textures. Called from Shutdown() and Resize().
    void DestroySsrResources();

    // ── Depth Prepass + Hi-Z Pyramid ────────────────────────────────────────

    bool m_gpuProfileEnabled = false;   ///< When true, GPU timestamp profiling is active and results are logged.

    bool m_depthPrepassEnabled = false; ///< r.DepthPrepass (block B.4c): the depth-only GBuffer pass before the colour pass.

    bool m_gpuOcclusionCullingEnabled = false;  ///< When true, GPU cull pass performs HiZ occlusion test.

    /// Per-mip R32_FLOAT textures for the Hi-Z pyramid.
    /// mipLevels[0] = half-res, mipLevels[1] = quarter-res, etc.
    HiZMipLevel m_hizMipLevels[kHiZMaxMipLevels] = {};

    /// Number of mip levels in the Hi-Z pyramid (depends on render target size).
    uint32_t m_hizMipCount = 0;

    // ── Previous Frame HiZ State (for two-phase occlusion culling) ───────
    // Phase 1 culls against last frame's HiZ to identify "definitely visible"
    // objects. After those draw and the HiZ rebuilds, Phase 2 re-tests Phase 1
    // rejects against the updated HiZ to catch newly revealed objects.
    // @ref Wihlidal 2015 "Optimizing the Graphics Pipeline with Compute" — two-phase HZB

    /// Per-mip SRV descriptor heap indices from the previous frame's HiZ pyramid.
    /// Copied from m_hizMipLevels[].srvIndex at end of each frame.
    uint32_t m_prevHizMipSrvIndices[kHiZMaxMipLevels] = {};

    /// Number of mip levels in the previous frame's HiZ pyramid.
    /// Zero on the first frame (no previous data available).
    uint32_t m_prevHizMipCount = 0;

    /// Previous frame's HiZ mip 0 dimensions. Used to fill Phase 1 cull constants.
    uint32_t m_prevHizWidth  = 0;
    uint32_t m_prevHizHeight = 0;

    // ── GPU Occlusion Culling Buffers (triple-buffered) ──────────────────

    /// Number of per-frame-in-flight buffers (used by instance, bounding sphere,
    /// visible index, and indirect args buffers).
    static constexpr uint32_t kInstanceBufferCount = 3;

    /// Bounding sphere input: StructuredBuffer<float4> (xyz = center, w = radius).
    /// One per frame-in-flight to avoid CPU-GPU sync.
    RHIBufferHandle m_boundingSphereBuffers[kInstanceBufferCount];

    /// SRV descriptor heap indices for bounding sphere buffers.
    uint32_t m_boundingSphereSrvIndices[kInstanceBufferCount] = {UINT32_MAX, UINT32_MAX, UINT32_MAX};

    /// GPU cull output: AppendStructuredBuffer<uint> of visible instance indices.
    RHIBufferHandle m_visibleIndexBuffers[kInstanceBufferCount];

    /// UAV descriptor heap indices for visible index buffers.
    uint32_t m_visibleIndexUavIndices[kInstanceBufferCount] = {UINT32_MAX, UINT32_MAX, UINT32_MAX};

    /// Indirect draw args: RWByteAddressBuffer (20 bytes = DrawIndexedIndirectArgs).
    RHIBufferHandle m_indirectArgsBuffers[kInstanceBufferCount];

    /// UAV descriptor heap indices for indirect args buffers.
    uint32_t m_indirectArgsUavIndices[kInstanceBufferCount] = {UINT32_MAX, UINT32_MAX, UINT32_MAX};

    /// Current capacity of bounding sphere / visible index buffers (in number of instances).
    uint32_t m_gpuCullBufferCapacity = 0;

    /// Creates GPU cull buffers at the given capacity. Called from Init() and when
    /// instance count exceeds current capacity in frame_setup.cpp.
    void CreateGpuCullBuffers(uint32_t capacity);

    /// Destroys all GPU cull buffers. Called from Shutdown().
    void DestroyGpuCullBuffers();

    // ── Phase 2 GPU Occlusion Culling Buffers (triple-buffered) ──────────
    // Separate output buffers for the second cull pass. Phase 2 re-tests ALL
    // instances against the updated HiZ and writes newly-visible indices here.
    // The GBuffer pass issues two indirect draws: Phase 1 visible + Phase 2 visible.

    /// Phase 2 visible index output: AppendStructuredBuffer<uint>.
    RHIBufferHandle m_visibleIndexBuffers2[kInstanceBufferCount];

    /// Phase 2 UAV descriptor heap indices for visible index buffers.
    uint32_t m_visibleIndexUavIndices2[kInstanceBufferCount] = {UINT32_MAX, UINT32_MAX, UINT32_MAX};

    /// Phase 2 indirect draw args: RWByteAddressBuffer (20 bytes).
    RHIBufferHandle m_indirectArgsBuffers2[kInstanceBufferCount];

    /// Phase 2 UAV descriptor heap indices for indirect args buffers.
    uint32_t m_indirectArgsUavIndices2[kInstanceBufferCount] = {UINT32_MAX, UINT32_MAX, UINT32_MAX};

    // ── Mesh Bounds (parallel to m_meshes) ───────────────────────────────

    /// Per-mesh object-space AABB minimum corners, indexed by mesh ID.
    /// Populated during mesh loading from MeshHeader::boundsMin.
    std::vector<Vec3> m_meshBoundsMin;

    /// Per-mesh object-space AABB maximum corners, indexed by mesh ID.
    /// Populated during mesh loading from MeshHeader::boundsMax.
    std::vector<Vec3> m_meshBoundsMax;

    // ── LOD System ────────────────────────────────────────────────────────

    float m_lodBias = 1.0f;  ///< Global LOD bias multiplier (1.0 = default).
    float m_mipBias = 0.0f;  ///< Global mipmap LOD bias override from CVar (-4 to +4).

    // ── CVar-Driven Per-Frame Overrides ──────────────────────────────────
    // These are set by ApplyCVars() and written into PerFrameParams by frame_setup.cpp.

    // ACES tone curve
    float m_tonemapSlope      = 0.88f;  ///< ACES film curve slope (default 0.88).
    float m_tonemapToe        = 0.55f;  ///< ACES film curve toe (default 0.55).
    float m_tonemapShoulder   = 0.26f;  ///< ACES film curve shoulder (default 0.26).
    float m_tonemapBlackClip  = 0.0f;   ///< ACES black clip (default 0.0).
    float m_tonemapWhiteClip  = 0.04f;  ///< ACES white clip (default 0.04).

    // Film effects
    float m_filmGrainIntensity    = 0.0f;   ///< Film grain intensity (0=off).
    float m_vignetteIntensity     = 0.4f;   ///< Vignette darkening (default 0.4).
    float m_chromaticAberration   = 0.0f;   ///< Chromatic aberration / scene fringe (0=off).

    // Contact shadows
    float m_contactShadowLength   = 0.02f;  ///< Screen-space contact shadow ray length (0=off, 0.02=typical).

    // Sky/IBL CVar overrides (applied when no SkyLightComponent is present)
    float m_skylightIntensityCVar = 1.0f;   ///< CVar-driven IBL ambient multiplier.
    float m_skylightMinAmbientCVar = 0.0f;  ///< CVar-driven minimum ambient floor.

    // Radiance cache grid extent
    float m_rcGridExtentCVar      = 5000.0f; ///< CVar-driven probe grid half-extent (cm).

    // ── Render State ───────────────────────────────────────────────────────

    RenderPath m_renderPath = RenderPath::Deferred;
    GBufferDebugMode m_debugMode = GBufferDebugMode::Count;
    DebugViewMode m_debugViewMode = DebugViewMode::Lit;
    ShowFlags m_showFlags{};                           ///< Per-feature render toggles (all enabled by default).
    uint32_t m_shadowDebugMode = 0;   ///< Shadow debug view mode (0=off, 1=grid, 2=readback). See shadow_debug.hlsli.
    uint32_t m_brdfDebugMode = 0;    ///< BRDF debug mode (0=off, 1=split-screen old vs new).
    RHIBufferHandle m_shadowReadbackBuffer;  ///< Persistent readback buffer for shadow atlas diagnostic (mode 2).
    uint32_t m_shadowReadbackRowPitch = 0;   ///< Row pitch of the readback (256-aligned).
    uint32_t m_shadowReadbackWidth = 0;      ///< Atlas width in pixels.
    int m_shadowReadbackPhase = 0;           ///< 0=issue copy, 1=wait+read, 2=done.
    uint32_t m_width = 0;                  ///< Display resolution width (backbuffer / swapchain).
    uint32_t m_height = 0;                 ///< Display resolution height (backbuffer / swapchain).
    /// Diagnostic clamp-box storage for r.TAA.ClampFreezeAfterFrame (0 = min, 1 = max).
    RHITextureHandle m_taaClampBox[2]{};
    uint32_t m_taaClampBoxSrv[2]{UINT32_MAX, UINT32_MAX};
    uint32_t m_taaClampBoxUav[2]{UINT32_MAX, UINT32_MAX};
    bool     m_taaClampBoxInitialised = false;  ///< False until the box has been written once.
    /// Component replay (diagnostic): recorded AO and specular, per frame and per pixel.
    RHIBufferHandle m_componentReplay;
    uint32_t m_componentReplaySrv = UINT32_MAX;
    std::string m_componentReplayLoaded;   ///< Path currently resident ("" = none).

    uint32_t m_renderWidth = 0;            ///< Internal render resolution width (screen percentage applied).
    uint32_t m_renderHeight = 0;           ///< Internal render resolution height (screen percentage applied).

    float m_screenPercentage = 100.f;      ///< Render scale (10-200, default 100 = native resolution).
    float m_totalTime = 0.f;
    bool m_statsOverlayInitialized = false;
    bool m_vfxEditorOverlayInitialized = false;

    // ── Screenshot ─────────────────────────────────────────────────────────

    ScreenshotCapture m_screenshotCapture;  ///< Readback capture helper (lazily initialized).
    std::string m_screenshotPath;           ///< Path to save screenshot. Empty = no pending capture.
    bool m_screenshotPending = false;       ///< True if a screenshot was requested via RequestScreenshot().
    bool m_screenshotSaveDeferred = false;  ///< True after capture commands recorded; save on next fence wait.
    bool m_screenshotBase64 = false;        ///< If true, print base64 PNG to stdout instead of saving to disk.

    // ── Deferred GPU Resource Destruction ──────────────────────────────────
    /// Buffers whose destruction must be deferred until the GPU finishes the
    /// command list that references them (e.g., SDF bake triangle upload buffers).
    /// Flushed at the start of each frame after the previous frame's fence.
    std::vector<RHIBufferHandle> m_deferredDestroyBuffers;

    /// Pending deferred mesh uploads accumulated during SyncWorldToDrawList.
    /// Flushed in a single GPU submission at the end of the sync pass.
    std::vector<DeferredMeshUpload> m_deferredMeshUploads;
    /// Meshes SyncWorldToDrawList skipped this frame because the per-frame load
    /// budget ran out. They live only in ECS components until a later frame
    /// picks them up, which no upload queue can see — so readiness must count them.
    uint32_t m_budgetDeferredMeshCount = 0;

    // ── Deferred SDF Baking ─────────────────────────────────────────────────

    /// Asynchronous, disk-cached mesh SDF bakes (block B.3): worker threads prepare the bakes
    /// and the cache, the GPU half is recorded in the frame's command list and completes by
    /// `m_bakeFence`, signalled after every frame's Execute with `m_bakeFenceValue`.
    std::unique_ptr<MeshSdfBakeQueue> m_sdfBakeQueue;
    IRHIFence* m_bakeFence = nullptr;
    uint64_t   m_bakeFenceValue = 0;
    bool       m_preloading = false;
    /// SDF GI switched on after meshes loaded without fields: the missing bakes are queued a
    /// few per frame, the CPU meshes re-read by path (ServiceSdfBakes).
    bool       m_sdfRetriggerPending = false;
    bool       m_sdfGiEnabledLast = false;
    bool       m_sdfServiceStarted = false;
    uint32_t   m_sdfRetriggerCursor = 0;

    // ── Loaded Assets ──────────────────────────────────────────────────────

    std::vector<GPUMesh> m_meshes;
    std::unordered_map<std::string, uint32_t> m_meshMap;
    std::vector<TextureLoadResult> m_textures;
    std::unordered_map<std::string, uint32_t> m_textureMap;

    // ── Deferred Load Queues (Lua API — safe to enqueue outside render loop) ──

    /// Paths enqueued via `QueueMeshLoad`; flushed at the start of `RenderFrame`.
    std::vector<std::string> m_pendingMeshPaths;

    /// Procedural meshes queued for GPU upload via RegisterProceduralMesh.
    std::vector<std::pair<uint32_t, Mesh>> m_pendingProceduralMeshes;

    /// Paths enqueued via `QueueTextureLoad`; flushed at the start of `RenderFrame`.
    std::vector<std::string> m_pendingTexturePaths;

    // ── Async Texture Decode (background CPU stbi_load) ───────────────────

    /// Worker thread pool for background CPU texture decoding. Started in
    /// Init() with 2 threads, shut down in Shutdown(). Textures are submitted
    /// via Submit() when they need loading, and completed decodes are polled
    /// and batch-uploaded to the GPU at the start of each RenderFrame().
    std::unique_ptr<AsyncTextureDecoder> m_asyncDecoder;

    /// VFS pointer cached from the last RenderFrame call.
    /// Used by QueueTextureLoad (which doesn't receive VFS as a parameter).
    IVFS* m_lastVfs = nullptr;

    /// Paths submitted to the async decoder that are still in-flight.
    /// Maps path -> slot index in m_textures, so we know where to store
    /// the TextureLoadResult when the decode completes.
    std::unordered_map<std::string, uint32_t> m_asyncPendingSlots;

    /// Decoded textures waiting for GPU upload. Fed by PollCompleted(),
    /// drained incrementally (kMaxUploadsPerFrame per frame) to avoid
    /// large WaitIdle stalls when many textures complete at once.
    std::vector<TextureDecodeResult> m_deferredDecoded;

    /// Persistent upload context that accumulates staging buffers for texture
    /// uploads across multiple frames. Flushed when enough textures accumulate
    /// or when all decodes complete — avoids one WaitIdle per texture.
    std::unique_ptr<UploadContext> m_textureBatchUpload;

    /// Number of textures uploaded into m_textureBatchUpload since last flush.
    uint32_t m_textureBatchCount = 0;

    /// Pre-allocated staging ring buffer for .uztexture uploads.
    /// Eliminates per-mip CreateBuffer overhead — suballocates from a single
    /// persistently mapped buffer instead. 16MB per frame, triple-buffered.
    /// Falls back to UploadContext when the ring is not initialized or exhausted.
    /// Heap-allocated because UploadRing is non-movable and SceneRenderer
    /// requires a defaulted move constructor.
    std::unique_ptr<UploadRing> m_uploadRing;

    /// Monotonically increasing frame counter for UploadRing frame cycling.
    /// Distinct from the swapchain frame index because it must survive resets.
    uint32_t m_uploadRingFrameIndex = 0;

    // ── Copy Queue Async Texture Upload ──────────────────────────────────
    // Texture copies are recorded on a dedicated copy command list that
    // executes on the DMA copy queue. This avoids stalling the graphics
    // pipeline with End/Execute/WaitIdle during texture uploads.
    //
    // Flow:
    //   1. Record staging -> texture copies on m_copyCmdList (copy queue)
    //   2. Submit + SignalCopyQueue() -> m_copyFenceValue
    //   3. Next frame: GraphicsWaitForCopy(m_copyFenceValue) on GPU (no CPU stall)
    //   4. Issue CopyDst -> ShaderResource barriers on graphics cmdList
    //   5. Update m_textures[] with real SRV indices

    /// Dedicated copy command list for async texture uploads.
    /// Uses the DMA copy queue — does NOT interrupt the graphics pipeline.
    /// Null if the backend does not support a copy queue (graceful fallback).
    IRHICommandList* m_copyCmdList = nullptr;

    /// Fence value from the last copy queue signal. When non-zero, the
    /// graphics queue must wait on this value before using newly uploaded textures.
    uint64_t m_copyFenceValue = 0;

    /// Texture upload that is in-flight on the copy queue, waiting for the
    /// fence to complete before transitioning to ShaderResource state.
    struct PendingCopyUpload {
        uint32_t slotIndex;       ///< Index into m_textures[] to update.
        TextureLoadResult result; ///< Full result (handle, srvIndex, width, height, mips).
    };

    /// Texture slots whose GPU upload is in-flight on the copy queue.
    /// Populated during FlushPendingLoads, drained at the start of the next
    /// FlushPendingLoads when the copy fence is reached.
    std::vector<PendingCopyUpload> m_pendingCopyUploads;

    /// Cached SRV indices for fallback textures, used as placeholders in
    /// texture slots that are still loading. Set once during Init().
    uint32_t m_fallbackWhiteSrvIndex  = UINT32_MAX;
    uint32_t m_fallbackNormalSrvIndex = UINT32_MAX;

    // ── Constants ──────────────────────────────────────────────────────────

    bool m_hasSkyAtmosphere = false;  ///< True if a SkyAtmosphereComponent was found this frame.
    bool m_hasFog = false;            ///< True if a HeightFogComponent (enabled) was found this frame.

    // ── VFX Runtime Emitters (owned by SceneRenderer, synced from ECS) ──
    /// ParticleEmitter instances created from VfxEmitterComponent ECS data.
    /// Indexed by the component's runtimeEmitterIdx field.
    std::vector<std::unique_ptr<ParticleEmitter>> m_runtimeEmitters;

    /// Compound VfxSystemInstance instances created from VfxSystemComponent
    /// ECS data. Each instance owns N child ParticleEmitter* which are also
    /// registered with the global VFXSystem (but NOT stored in
    /// m_runtimeEmitters — ownership is with the system instance).
    /// Indexed by the component's runtimeInstanceIdx field.
    std::vector<std::unique_ptr<VfxSystemInstance>> m_runtimeSystems;

    /// Hot-reload watcher for .uzfx and .uzvfx files (dev builds only).
    std::unique_ptr<VFXHotReload> m_vfxHotReload;

    /// Content root directory supplied by the application (via SetContentRoot).
    /// Used to initialize the VFX hot-reload watcher. Empty by default —
    /// when empty, hot-reload is skipped. The runtime never hardcodes a
    /// content directory; the application (engine host / editor / demo) is
    /// responsible for supplying one derived from project config, CLI flags,
    /// or similar.
    std::string m_contentRoot;

    // ── GPU Instancing (triple-buffered) ─────────────────────────────────
    RHIBufferHandle m_instanceBuffers[kInstanceBufferCount]; ///< Per-frame instance buffers.
    uint32_t m_instanceBufferCapacity = 0;   ///< Current capacity in number of InstanceData elements.
    uint32_t m_instanceBufferSrvIndices[kInstanceBufferCount] = {UINT32_MAX, UINT32_MAX, UINT32_MAX};

    // Shadow constants moved to shadow_atlas.h (kShadowResolution, kShadowAtlasSize)
    static constexpr uint32_t kCBRingFrames = 4; ///< 4 frames prevents stale data race (3 was insufficient — Frame N overwritten by N+3 before GPU read)
    static constexpr uint64_t kCBRingBytesPerFrame = 2 * 1024 * 1024;

    // ── Internal Helpers ───────────────────────────────────────────────────

    /// Iterates the ECS World and populates a DrawList from entities with
    /// StaticMeshComponent + SceneComponent. Loads meshes/textures on demand.
    void SyncWorldToDrawList(World* world, IVFS* vfs, IRHICommandList* cmdList,
                             DrawList& outDrawList);

    /// Flushes deferred mesh and texture load queues populated by `QueueMeshLoad`
    /// and `QueueTextureLoad`.  Called at the start of each `RenderFrame()`.
    ///
    /// Requires a command list in recording state for GPU uploads.
    void FlushPendingLoads(IRHICommandList* cmdList, IVFS* vfs);

    /// Once per frame, at the end of the sync (block B.3): queues the bakes of meshes that
    /// loaded without SDF GI when it was switched on since, then starts, advances and
    /// completes the asynchronous mesh SDF bakes (`MeshSdfBakeQueue::Service`).
    void ServiceSdfBakes(IRHICommandList* cmdList);

    /// Hands a loaded mesh to the bake queue: bake options captured now, 16-bit indices
    /// widened, the vertex bytes moved out of `cpuMesh`.
    void QueueSdfBake(uint32_t meshIndex, Mesh& cpuMesh);

    /// Creates the shadow atlas depth texture.
    // Shadow atlas creation moved to ShadowAtlas::Init()

    /// Destroys loaded GPU meshes and textures.
    void DestroyAssets();

    /// Creates the Hi-Z pyramid per-mip textures for the given resolution.
    /// Called from Init() and Resize().
    void CreateHiZPyramid(uint32_t width, uint32_t height);

    /// Destroys all Hi-Z pyramid textures. Called from Shutdown() and Resize().
    void DestroyHiZPyramid();

    /// Creates DFS output texture, culled indices buffer, and indirect args buffer.
    /// Called from Init() and Resize().
    void CreateDfsResources(uint32_t width, uint32_t height);

    /// Destroys all DFS GPU resources. Called from Shutdown() and Resize().
    void DestroyDfsResources();

    // ── Frame Pipeline (split across frame/*.cpp) ─────────────────────────
    //
    // RenderFrame() is the orchestrator that calls each of these helpers in
    // sequence.  They are defined in separate translation units under the
    // `frame/` subdirectory to keep each file under ~500 lines.

    /// Sets up per-frame data: camera, input, CB uploads, draw list build,
    /// instancing, and all state that subsequent passes depend on.
    /// Populates `ctx` with everything the pass registration helpers need.
    void SetupFrameData(FrameContext& ctx, rg::RgBuilder& rgBuilder);

    /// Populates PerFrameParams, shadow cascades, local light shadows, and
    /// uploads PerFrameCB to the CB ring. Called from SetupFrameData().
    void PopulatePerFrameConstants(FrameContext& ctx);

    /// Builds the draw list from ECS, batches instances, uploads instance
    /// data, and sets up GPU occlusion culling buffers. Called from SetupFrameData().
    void SyncDrawListAndInstances(FrameContext& ctx);

    /// Synchronizes VfxEmitterComponent ECS entities to live ParticleEmitter
    /// instances in m_runtimeEmitters. Creates emitters for new components,
    /// updates emitting state for existing ones. Called from RenderFrame()
    /// before VFXSystem::Tick().
    void SyncVfxEmitters(World* world, IVFS* vfs);

    /// Registers shadow depth passes (CSM cache-aware + local light shadows).
    void RegisterShadowPasses(rg::RgBuilder& rgBuilder, FrameContext& ctx);

    /// Registers geometry passes: depth prepass, Hi-Z, GBuffer, velocity,
    /// GTAO, DFS, SSR, and deferred lighting (or forward PBR).
    void RegisterGeometryPasses(rg::RgBuilder& rgBuilder, FrameContext& ctx);

    /// Registers lighting-related compute passes: GTAO, DFS, SSR, and the
    /// deferred lighting (or debug view) pass.
    void RegisterLightingPasses(rg::RgBuilder& rgBuilder, FrameContext& ctx);

    // ── RegisterLightingPasses sub-helpers (frame_ao_passes.cpp, frame_gi_passes.cpp,
    //    frame_lighting_pass.cpp, frame_atmosphere_passes.cpp) ───────────────

    /// Registers DBuffer apply pass (merges decals into GBuffer).
    void RegisterDBufferApplyPass(rg::RgBuilder& rgBuilder, FrameContext& ctx);

    /// Registers Ground Truth Ambient Occlusion compute pass.
    void RegisterGTAOPass(rg::RgBuilder& rgBuilder, FrameContext& ctx);

    /// Registers Distance Field Shadow passes (cull + cone trace).
    void RegisterDFSPasses(rg::RgBuilder& rgBuilder, FrameContext& ctx);

    /// Registers Screen-Space Global Illumination compute passes.
    void RegisterSSGIPasses(rg::RgBuilder& rgBuilder, FrameContext& ctx);

    /// Registers SDF-based GI passes (GDF composite + radiance cache trace).
    void RegisterSDFGIPasses(rg::RgBuilder& rgBuilder, FrameContext& ctx);

    /// Registers AMD FidelityFX Brixelizer GI passes (SDF bake + GI dispatch).
    void RegisterBrixelizerGIPasses(rg::RgBuilder& rgBuilder, FrameContext& ctx);

    /// Registers Screen-Space Reflections passes (trace + resolve + temporal).
    void RegisterSSRPasses(rg::RgBuilder& rgBuilder, FrameContext& ctx);

    /// Registers the deferred lighting pass (or debug view / GBuffer debug).
    void RegisterDeferredLightingPass(rg::RgBuilder& rgBuilder, FrameContext& ctx);
    /// Block B.7: the light cull compute pass (clusters of the view frustum -> light index lists).
    void RegisterLightCullPass(rg::RgBuilder& rgBuilder, FrameContext& ctx);

    /// Registers atmosphere, height fog, and volumetric cloud composite passes.
    void RegisterAtmosphereFogCloudPasses(rg::RgBuilder& rgBuilder, FrameContext& ctx);

    /// Copies current HDR scene color to SSGI prev-frame buffer.
    void RegisterSSGIPrevColorCopy(rg::RgBuilder& rgBuilder, FrameContext& ctx);

    /// Registers intermediate render targets with the PassInspector for visualization.
    void RegisterPassInspectorTargets(FrameContext& ctx);

    /// Registers the forward translucent pass that renders materials with
    /// blendMode >= Translucent (glass, transparent, additive).
    /// Runs AFTER deferred lighting (has HDR scene color for screen-space
    /// refraction) and BEFORE particles and post-process.
    void RegisterTranslucentPasses(rg::RgBuilder& rgBuilder, FrameContext& ctx);

    /// Registers post-process passes: bloom, TAA, color grading LUT,
    /// motion blur, DOF, tonemap, and FXAA.
    void RegisterPostProcessPasses(rg::RgBuilder& rgBuilder, FrameContext& ctx);

    /// Registers overlay passes: atmosphere, fog, editor grid, stats overlay,
    /// screenshot capture, and present transition.
    void RegisterOverlayPasses(rg::RgBuilder& rgBuilder, FrameContext& ctx);

    /// Registers CVars in the global CVarRegistry. Called once from Init().
    void RegisterCVars();

    /// Reads CVars and applies changes (screen percentage, upscaler type, dynres).
    /// Called once per frame at the start of SetupFrameData().
    void ApplyCVars();
