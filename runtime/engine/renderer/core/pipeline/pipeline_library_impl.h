/// Private implementation of shader cache and lazy-load methods for the render pipeline.
///
/// This file is #included inside the PipelineLibrary class body. Do not include directly.

// ─────────────────────────────────────────────────────────────────────────────
// pipeline_library_impl.h — Private members of PipelineLibrary.
// This file is #included inside the PipelineLibrary class body.
// DO NOT include this file directly — include pipeline_library.h instead.
// ─────────────────────────────────────────────────────────────────────────────

    /// Creates and caches shaders needed for a pipeline if not already compiled.
    /// Returns false if any required shader fails to compile.
    [[nodiscard]] bool EnsureMeshShaders();
    [[nodiscard]] bool EnsureFullscreenShaders();
    [[nodiscard]] bool EnsureGBufferPixelShader();
    [[nodiscard]] bool EnsureLightingPixelShader();
    [[nodiscard]] bool EnsurePixelDebugComputeShader();
    [[nodiscard]] bool EnsureGBufferDebugPixelShader();
    [[nodiscard]] bool EnsureDebugViewPixelShader();
    [[nodiscard]] bool EnsureForwardPixelShader();
    [[nodiscard]] bool EnsureInstancedMeshShaders();
    [[nodiscard]] bool EnsureInstancedShadowShaders();
    [[nodiscard]] bool EnsureShadowShaders();
    [[nodiscard]] bool EnsureSkyAtmospherePixelShader();
    [[nodiscard]] bool EnsureHdriSkyboxPixelShader();
    [[nodiscard]] bool EnsureTonemapPixelShader();
    [[nodiscard]] bool EnsureVelocityPixelShader();
    [[nodiscard]] bool EnsureHeightFogPixelShader();
    [[nodiscard]] bool EnsureTransmittanceLutShader();
    [[nodiscard]] bool EnsureMultiScatterLutShader();
    [[nodiscard]] bool EnsureSingleScatterLutShader();
    [[nodiscard]] bool EnsureScatteringDensityShader();
    [[nodiscard]] bool EnsureIndirectScatteringShader();
    [[nodiscard]] bool EnsureIrradianceLutShader();
    [[nodiscard]] bool EnsureAPVolumeShader();
    [[nodiscard]] bool EnsureSkyCubemapCaptureShader();
    [[nodiscard]] bool EnsureSkyCubemapCapturePSShader();
    [[nodiscard]] bool EnsureHiZPyramidShader();
    [[nodiscard]] bool EnsureDecalShaders();
    [[nodiscard]] bool EnsureDecalApplyShader();
    [[nodiscard]] bool EnsureGpuCullShader();
    [[nodiscard]] bool EnsureDfsCullObjectsShader();
    [[nodiscard]] bool EnsureDfsConeTraceShader();
    [[nodiscard]] bool EnsureTaaResolveShader();
    [[nodiscard]] bool EnsureTsrDilateVelocityShader();
    [[nodiscard]] bool EnsureTsrRejectShadingShader();
    [[nodiscard]] bool EnsureTsrUpdateHistoryShader();
    [[nodiscard]] bool EnsureSsrTraceShader();
    [[nodiscard]] bool EnsureSsrResolveShader();
    [[nodiscard]] bool EnsureSsrTemporalShader();
    [[nodiscard]] bool EnsureColorGradingLutShader();
    [[nodiscard]] bool EnsureMotionBlurShader();
    [[nodiscard]] bool EnsureMotionBlurTileMaxShader();
    [[nodiscard]] bool EnsureMotionBlurNeighborMaxShader();
    [[nodiscard]] bool EnsureDofSetupShader();
    [[nodiscard]] bool EnsureDofBlurShader();
    [[nodiscard]] bool EnsureFxaaPixelShader();
    [[nodiscard]] bool EnsureCasSharpenPixelShader();
    [[nodiscard]] bool EnsureSsgiPrevColorReduceShader();
    [[nodiscard]] bool EnsureSsgiTraceShader();
    [[nodiscard]] bool EnsureSsgiSpatialDenoiseShader();
    [[nodiscard]] bool EnsureSsgiTemporalAccumShader();
    [[nodiscard]] bool EnsureSsgiBilateralUpsampleShader();
    [[nodiscard]] bool EnsureSvgfReprojectShader();
    [[nodiscard]] bool EnsureSvgfFilterMomentsShader();
    [[nodiscard]] bool EnsureSvgfAtrousShader();
    [[nodiscard]] bool EnsureGodRaysShader();
    [[nodiscard]] bool EnsureVisFullscreenPixelShader();
    [[nodiscard]] bool EnsureGdfCompositeShader();
    [[nodiscard]] bool EnsureMeshSdfBakeShader();
    [[nodiscard]] bool EnsureRadianceCacheTraceShader();
    [[nodiscard]] bool EnsureCloudCompositePixelShader();
    [[nodiscard]] bool EnsureVolumetricFogInjectShader();
    [[nodiscard]] bool EnsureVolumetricFogScatterShader();
    [[nodiscard]] bool EnsureVolumetricFogCompositeShader();

    /// Creates a pipeline through the PSO cache. On cache hit, returns the
    /// cached handle without calling the device. On miss, creates via the
    /// device and inserts into the cache.
    [[nodiscard]] RHIPipelineHandle CreateCachedPipeline(const RHIGraphicsPipelineDesc& desc);

    IRHIDevice*    m_device    = nullptr;
    ShaderManager* m_shaderMgr = nullptr;
    bool           m_initialized = false;
    uint32_t       m_failedPipelineCount = 0; ///< Number of pipelines that permanently failed creation.

    // ── PSO content-hash cache ─────────────────────────────────────────
    PsoCache m_psoCache;

    // ── PSO disk cache (optional) ────────────────────────────────────
    std::unique_ptr<PsoDiskCache> m_psoDiskCache;

    // ── Cached shader handles ───────────────────────────────────────────
    RHIShaderHandle m_meshVS           = RHIShaderHandle::Invalid();
    RHIShaderHandle m_meshPS           = RHIShaderHandle::Invalid();
    RHIShaderHandle m_fullscreenVS     = RHIShaderHandle::Invalid();
    RHIShaderHandle m_gbufferPS        = RHIShaderHandle::Invalid();
    RHIShaderHandle m_lightingPS       = RHIShaderHandle::Invalid();
    RHIShaderHandle m_pixelDebugCS     = RHIShaderHandle::Invalid(); ///< PixelDebug compute post-pass shader.
    RHIShaderHandle m_gbufferDebugPS   = RHIShaderHandle::Invalid();
    RHIShaderHandle m_debugViewPS      = RHIShaderHandle::Invalid();
    RHIShaderHandle m_instancedMeshVS  = RHIShaderHandle::Invalid();
    RHIShaderHandle m_instancedShadowVS = RHIShaderHandle::Invalid();
    RHIShaderHandle m_shadowVS         = RHIShaderHandle::Invalid();
    RHIShaderHandle m_skyAtmospherePS      = RHIShaderHandle::Invalid();
    RHIShaderHandle m_hdriSkyboxPS         = RHIShaderHandle::Invalid();
    RHIShaderHandle m_tonemapPS            = RHIShaderHandle::Invalid();
    RHIShaderHandle m_velocityPS           = RHIShaderHandle::Invalid();
    RHIShaderHandle m_heightFogPS          = RHIShaderHandle::Invalid();
    RHIShaderHandle m_cloudCompositePS     = RHIShaderHandle::Invalid();
    RHIShaderHandle m_transmittanceLutCS      = RHIShaderHandle::Invalid();
    RHIShaderHandle m_multiScatterLutCS      = RHIShaderHandle::Invalid();
    RHIShaderHandle m_singleScatterLutCS     = RHIShaderHandle::Invalid();
    RHIShaderHandle m_scatteringDensityCS    = RHIShaderHandle::Invalid();
    RHIShaderHandle m_indirectScatteringCS   = RHIShaderHandle::Invalid();
    RHIShaderHandle m_irradianceLutCS        = RHIShaderHandle::Invalid();
    RHIShaderHandle m_apVolumeCS             = RHIShaderHandle::Invalid();
    RHIShaderHandle m_skyCubemapCaptureCS    = RHIShaderHandle::Invalid();
    RHIShaderHandle m_skyCubemapCapturePSShader = RHIShaderHandle::Invalid();
    RHIShaderHandle m_hizPyramidCS             = RHIShaderHandle::Invalid();
    RHIShaderHandle m_gpuCullCS                = RHIShaderHandle::Invalid();
    RHIShaderHandle m_dfsCullObjectsCS         = RHIShaderHandle::Invalid();
    RHIShaderHandle m_dfsConeTraceCS           = RHIShaderHandle::Invalid();
    RHIShaderHandle m_taaResolveCS             = RHIShaderHandle::Invalid();
    RHIShaderHandle m_tsrDilateVelocityCS      = RHIShaderHandle::Invalid();
    RHIShaderHandle m_tsrRejectShadingCS       = RHIShaderHandle::Invalid();
    RHIShaderHandle m_tsrUpdateHistoryCS       = RHIShaderHandle::Invalid();
    RHIShaderHandle m_ssrTraceCS               = RHIShaderHandle::Invalid();
    RHIShaderHandle m_ssrResolveCS             = RHIShaderHandle::Invalid();
    RHIShaderHandle m_ssrTemporalCS            = RHIShaderHandle::Invalid();
    RHIShaderHandle m_colorGradingLutCS        = RHIShaderHandle::Invalid();
    RHIShaderHandle m_motionBlurCS             = RHIShaderHandle::Invalid();
    RHIShaderHandle m_motionBlurTileMaxCS      = RHIShaderHandle::Invalid();
    RHIShaderHandle m_motionBlurNeighborMaxCS  = RHIShaderHandle::Invalid();
    RHIShaderHandle m_dofSetupCS               = RHIShaderHandle::Invalid();
    RHIShaderHandle m_dofBlurCS                = RHIShaderHandle::Invalid();
    RHIShaderHandle m_fxaaPS                   = RHIShaderHandle::Invalid();
    RHIShaderHandle m_casSharpenPS             = RHIShaderHandle::Invalid();
    RHIShaderHandle m_ssgiPrevColorReduceCS    = RHIShaderHandle::Invalid();
    RHIShaderHandle m_ssgiTraceCS              = RHIShaderHandle::Invalid();
    RHIShaderHandle m_ssgiSpatialDenoiseCS     = RHIShaderHandle::Invalid();
    RHIShaderHandle m_ssgiTemporalAccumCS      = RHIShaderHandle::Invalid();
    RHIShaderHandle m_ssgiBilateralUpsampleCS  = RHIShaderHandle::Invalid();
    RHIShaderHandle m_svgfReprojectCS          = RHIShaderHandle::Invalid();
    RHIShaderHandle m_svgfFilterMomentsCS      = RHIShaderHandle::Invalid();
    RHIShaderHandle m_svgfAtrousCS             = RHIShaderHandle::Invalid();
    RHIShaderHandle m_godRaysCS                = RHIShaderHandle::Invalid();
    RHIShaderHandle m_visFullscreenPS           = RHIShaderHandle::Invalid();
    RHIShaderHandle m_gdfCompositeCS            = RHIShaderHandle::Invalid();
    RHIShaderHandle m_meshSdfBakeCS             = RHIShaderHandle::Invalid();
    RHIShaderHandle m_radianceCacheTraceCS      = RHIShaderHandle::Invalid();
    RHIShaderHandle m_volFogInjectCS            = RHIShaderHandle::Invalid();
    RHIShaderHandle m_volFogScatterCS           = RHIShaderHandle::Invalid();
    RHIShaderHandle m_volFogCompositePS         = RHIShaderHandle::Invalid();
    RHIShaderHandle m_decalVS                   = RHIShaderHandle::Invalid();
    RHIShaderHandle m_decalPS                   = RHIShaderHandle::Invalid();
    RHIShaderHandle m_decalApplyCS              = RHIShaderHandle::Invalid();

    // ── Shader permutation cache (material-dependent PSOs) ─────────────
    /// Caches ForwardPBR, GBuffer, GBufferMasked, Translucent, and Additive
    /// pipelines keyed by ShaderPermutationKey (blend mode + bindless flag).
    /// Replaces the per-variant m_forwardPBR/m_gbuffer/etc. member variables.
    ShaderPermutationCache m_permutationCache;

    // ── Cached pipeline handles (non-material pipelines) ────────────────
    RHIPipelineHandle m_lighting              = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_pixelDebugCompute     = RHIPipelineHandle::Invalid(); ///< PixelDebug compute post-pass PSO.
    RHIPipelineHandle m_gbufferDebug          = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_debugView             = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_shadowDepth           = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_shadowDepthBindless   = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_gbufferInstanced      = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_shadowDepthInstanced  = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_skyAtmosphere         = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_hdriSkybox            = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_tonemap               = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_velocityPass           = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_heightFog             = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_cloudComposite        = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_transmittanceLut      = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_multiScatterLut      = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_singleScatterLut     = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_scatteringDensity    = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_indirectScattering   = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_irradianceLut        = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_apVolume             = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_skyCubemapCapture    = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_skyCubemapCapturePSPipeline = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_depthPrepass          = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_depthPrepassBindless  = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_depthPrepassInstanced = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_hizPyramid            = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_gpuCull              = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_dfsCullObjects        = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_dfsConeTrace          = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_taaResolve            = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_tsrDilateVelocity    = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_tsrRejectShading     = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_tsrUpdateHistory     = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_ssrTrace              = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_ssrResolve            = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_ssrTemporal           = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_colorGradingLut       = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_motionBlur            = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_motionBlurTileMax     = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_motionBlurNeighborMax = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_dofSetup              = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_dofBlur               = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_fxaa                  = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_casSharpen            = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_ssgiPrevColorReduce   = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_ssgiTrace             = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_ssgiSpatialDenoise    = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_ssgiTemporalAccum     = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_ssgiBilateralUpsample = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_svgfReproject         = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_svgfFilterMoments     = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_svgfAtrous            = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_godRays               = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_visFullscreen         = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_gdfComposite          = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_meshSdfBake           = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_radianceCacheTrace    = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_decalRender           = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_decalApply            = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_volFogInject          = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_volFogScatter         = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_volFogComposite       = RHIPipelineHandle::Invalid();
