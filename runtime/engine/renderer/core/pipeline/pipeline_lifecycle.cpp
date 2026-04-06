/// UzEngine Pipeline Library — Lifecycle, shader helpers, PSO cache, warmup.

#include "pipeline_library.h"
#include "engine_shaders.h"
#include "log.h"
#include "rhi.h"
#include "rhi_pso_cache.h"
#include "shader_manager.h"
#include "vertex.h"
#include "vertex_layout.h"
#include "profiler.h"

UZ_DECLARE_LOG_CATEGORY(Renderer);

UZ_DEFINE_STAT(RHI, PsoCacheHits);
UZ_DEFINE_STAT(RHI, PsoCacheMisses);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

PipelineLibrary::~PipelineLibrary() {
    if (m_initialized) {
        Shutdown();
    }
}

bool PipelineLibrary::Init(IRHIDevice* device, ShaderManager* shaderMgr,
                            const std::string& psoCacheDir) {
    if (m_initialized) {
        UZ_LOG(Renderer, Warning, "PipelineLibrary::Init called while already initialized");
        return true;
    }

    if (!device) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::Init failed: device is null");
        return false;
    }

    if (!shaderMgr) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::Init failed: shaderMgr is null");
        return false;
    }

    m_device = device;
    m_shaderMgr = shaderMgr;
    m_psoCache.Init();

    if (!psoCacheDir.empty()) {
        m_psoDiskCache = std::make_unique<PsoDiskCache>(psoCacheDir);
        UZ_LOG(Renderer, Info, "PipelineLibrary: PSO disk cache enabled at '{}'", psoCacheDir);
    }

    m_initialized = true;

    UZ_LOG(Renderer, Info, "PipelineLibrary initialized (PSOs will be created lazily)");
    return true;
}

void PipelineLibrary::Shutdown() {
    if (!m_initialized) {
        return;
    }

    // Destroy cached pipelines
    auto destroyPipeline = [this](RHIPipelineHandle& h) {
        if (h.IsValid()) {
            m_device->DestroyPipeline(h);
            h = RHIPipelineHandle::Invalid();
        }
    };

    // Destroy material-dependent PSOs stored in the permutation cache.
    // Enumerate all known keys (5 blend modes x 2 bindless variants = 10 entries max).
    {
        const EBlendMode blendModes[] = {
            EBlendMode::Opaque,      // ForwardPBR + GBuffer
            EBlendMode::Masked,      // GBufferMasked
            EBlendMode::Translucent, // Translucent
            EBlendMode::Additive,    // Additive
        };
        const bool bindlessValues[] = { false, true };

        for (const auto blend : blendModes) {
            for (const bool bl : bindlessValues) {
                // ForwardPBR uses Opaque + a distinct shading model (Unlit) to
                // differentiate from GBuffer's Opaque + DefaultLit key.
                if (blend == EBlendMode::Opaque) {
                    // GBuffer key: DefaultLit + Opaque
                    const ShaderPermutationKey gbufKey(EShadingModel::DefaultLit, blend, bl);
                    auto gbufHandle = m_permutationCache.Lookup(gbufKey);
                    if (gbufHandle.IsValid()) {
                        m_device->DestroyPipeline(gbufHandle);
                    }
                    // ForwardPBR key: Unlit + Opaque
                    const ShaderPermutationKey fwdKey(EShadingModel::Unlit, blend, bl);
                    auto fwdHandle = m_permutationCache.Lookup(fwdKey);
                    if (fwdHandle.IsValid()) {
                        m_device->DestroyPipeline(fwdHandle);
                    }
                } else {
                    const ShaderPermutationKey key(EShadingModel::DefaultLit, blend, bl);
                    auto handle = m_permutationCache.Lookup(key);
                    if (handle.IsValid()) {
                        m_device->DestroyPipeline(handle);
                    }
                }
            }
        }
        m_permutationCache.InvalidateAll();
    }

    destroyPipeline(m_lighting);
    destroyPipeline(m_gbufferDebug);
    destroyPipeline(m_debugView);
    destroyPipeline(m_shadowDepth);
    destroyPipeline(m_shadowDepthBindless);
    destroyPipeline(m_gbufferInstanced);
    destroyPipeline(m_shadowDepthInstanced);
    destroyPipeline(m_skyAtmosphere);
    destroyPipeline(m_tonemap);
    destroyPipeline(m_heightFog);
    destroyPipeline(m_cloudComposite);
    destroyPipeline(m_transmittanceLut);
    destroyPipeline(m_singleScatterLut);
    destroyPipeline(m_scatteringDensity);
    destroyPipeline(m_indirectScattering);
    destroyPipeline(m_irradianceLut);
    destroyPipeline(m_apVolume);
    destroyPipeline(m_skyCubemapCapture);
    destroyPipeline(m_skyCubemapCapturePSPipeline);
    destroyPipeline(m_depthPrepass);
    destroyPipeline(m_depthPrepassBindless);
    destroyPipeline(m_depthPrepassInstanced);
    destroyPipeline(m_hizPyramid);
    destroyPipeline(m_dfsCullObjects);
    destroyPipeline(m_dfsConeTrace);
    destroyPipeline(m_taaResolve);
    destroyPipeline(m_ssrTrace);
    destroyPipeline(m_ssrResolve);
    destroyPipeline(m_colorGradingLut);
    destroyPipeline(m_motionBlur);
    destroyPipeline(m_motionBlurTileMax);
    destroyPipeline(m_motionBlurNeighborMax);
    destroyPipeline(m_dofSetup);
    destroyPipeline(m_dofBlur);
    destroyPipeline(m_fxaa);

    // Destroy cached shaders
    auto destroyShader = [this](RHIShaderHandle& h) {
        if (h.IsValid()) {
            m_device->DestroyShader(h);
            h = RHIShaderHandle::Invalid();
        }
    };

    destroyShader(m_meshVS);
    destroyShader(m_meshPS);
    destroyShader(m_translucentPS);
    destroyShader(m_fullscreenVS);
    destroyShader(m_gbufferPS);
    destroyShader(m_lightingPS);
    destroyShader(m_pixelDebugCS);
    destroyShader(m_gbufferDebugPS);
    destroyShader(m_debugViewPS);
    destroyShader(m_instancedMeshVS);
    destroyShader(m_instancedShadowVS);
    destroyShader(m_shadowVS);
    destroyShader(m_skyAtmospherePS);
    destroyShader(m_tonemapPS);
    destroyShader(m_heightFogPS);
    destroyShader(m_cloudCompositePS);
    destroyShader(m_transmittanceLutCS);
    destroyShader(m_apVolumeCS);
    destroyShader(m_skyCubemapCaptureCS);
    destroyShader(m_skyCubemapCapturePSShader);
    destroyShader(m_hizPyramidCS);
    destroyShader(m_dfsCullObjectsCS);
    destroyShader(m_dfsConeTraceCS);
    destroyShader(m_taaResolveCS);
    destroyShader(m_ssrTraceCS);
    destroyShader(m_ssrResolveCS);
    destroyShader(m_colorGradingLutCS);
    destroyShader(m_motionBlurCS);
    destroyShader(m_motionBlurTileMaxCS);
    destroyShader(m_motionBlurNeighborMaxCS);
    destroyShader(m_dofSetupCS);
    destroyShader(m_dofBlurCS);
    destroyShader(m_velocityPS);
    destroyShader(m_multiScatterLutCS);
    destroyShader(m_singleScatterLutCS);
    destroyShader(m_scatteringDensityCS);
    destroyShader(m_indirectScatteringCS);
    destroyShader(m_irradianceLutCS);
    destroyShader(m_fxaaPS);

    m_psoCache.Shutdown();
    m_psoDiskCache.reset();

    m_device = nullptr;
    m_shaderMgr = nullptr;
    m_initialized = false;

    UZ_LOG(Renderer, Info, "PipelineLibrary shut down");
}

// ─────────────────────────────────────────────────────────────────────────────
// Shader Compilation Helpers
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureMeshShaders() {
    if (!m_meshVS.IsValid()) {
        m_meshVS = m_shaderMgr->CreateShader(EngineShader::MeshVS, RHIShaderStage::Vertex);
        if (!m_meshVS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile MeshVS");
            return false;
        }
    }
    return true;
}

bool PipelineLibrary::EnsureFullscreenShaders() {
    if (!m_fullscreenVS.IsValid()) {
        m_fullscreenVS = m_shaderMgr->CreateShader(EngineShader::FullscreenVS, RHIShaderStage::Vertex);
        if (!m_fullscreenVS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile FullscreenVS");
            return false;
        }
    }
    return true;
}

bool PipelineLibrary::EnsureGBufferPixelShader() {
    if (!m_gbufferPS.IsValid()) {
        m_gbufferPS = m_shaderMgr->CreateShader(EngineShader::GbufferPS, RHIShaderStage::Pixel);
        if (!m_gbufferPS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile GbufferPS");
            return false;
        }
    }
    return true;
}

bool PipelineLibrary::EnsureLightingPixelShader() {
    if (!m_lightingPS.IsValid()) {
        m_lightingPS = m_shaderMgr->CreateShader(EngineShader::DeferredLightingPS, RHIShaderStage::Pixel);
        if (!m_lightingPS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile DeferredLightingPS");
            return false;
        }
    }
    return true;
}

bool PipelineLibrary::EnsurePixelDebugComputeShader() {
    if (!m_pixelDebugCS.IsValid()) {
        m_pixelDebugCS = m_shaderMgr->CreateShader(EngineShader::PixelDebugCS, RHIShaderStage::Compute);
        if (!m_pixelDebugCS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile PixelDebugCS");
            return false;
        }
    }
    return true;
}

bool PipelineLibrary::EnsureGBufferDebugPixelShader() {
    if (!m_gbufferDebugPS.IsValid()) {
        m_gbufferDebugPS = m_shaderMgr->CreateShader(EngineShader::GbufferDebugPS, RHIShaderStage::Pixel);
        if (!m_gbufferDebugPS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile GbufferDebugPS");
            return false;
        }
    }
    return true;
}

bool PipelineLibrary::EnsureDebugViewPixelShader() {
    if (!m_debugViewPS.IsValid()) {
        m_debugViewPS = m_shaderMgr->CreateShader(EngineShader::DebugViewPS, RHIShaderStage::Pixel);
        if (!m_debugViewPS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile DebugViewPS");
            return false;
        }
    }
    return true;
}

bool PipelineLibrary::EnsureForwardPixelShader() {
    if (!m_meshPS.IsValid()) {
        m_meshPS = m_shaderMgr->CreateShader(EngineShader::MeshPS, RHIShaderStage::Pixel);
        if (!m_meshPS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile MeshPS");
            return false;
        }
    }
    return true;
}

bool PipelineLibrary::EnsureTranslucentPixelShader() {
    if (!m_translucentPS.IsValid()) {
        m_translucentPS = m_shaderMgr->CreateShader(EngineShader::TranslucentPS, RHIShaderStage::Pixel);
        if (!m_translucentPS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile TranslucentPS");
            return false;
        }
    }
    return true;
}


// PSO Cache Integration
// ─────────────────────────────────────────────────────────────────────────────

RHIPipelineHandle PipelineLibrary::CreateCachedPipeline(const RHIGraphicsPipelineDesc& desc) {
    PsoCacheKey key = ComputePsoCacheKey(desc);

    // In-memory cache check
    auto cached = m_psoCache.Lookup(key);
    if (cached.IsValid()) {
        UZ_STAT_INCREMENT(RHI, PsoCacheHits);
        UZ_LOG(Renderer, Debug, "PsoCache hit for '{}'", desc.debugName ? desc.debugName : "<unnamed>");
        return cached;
    }

    // Disk cache check — load blob if available
    std::optional<std::vector<uint8_t>> diskBlob;
    RHIGraphicsPipelineDesc descWithBlob = desc;

    if (m_psoDiskCache) {
        diskBlob = m_psoDiskCache->Lookup(key.hash);
        if (diskBlob && !diskBlob->empty()) {
            descWithBlob.cachedBlobData = diskBlob->data();
            descWithBlob.cachedBlobSize = diskBlob->size();
            UZ_LOG(Renderer, Debug, "PsoDiskCache hit for '{}' ({} bytes)",
                   desc.debugName ? desc.debugName : "<unnamed>", diskBlob->size());
        }
    }

    auto handle = m_device->CreateGraphicsPipeline(descWithBlob);
    if (handle.IsValid()) {
        m_psoCache.Insert(key, handle);
        UZ_STAT_INCREMENT(RHI, PsoCacheMisses);

        // Store blob to disk if we didn't load one (or if load was stale and
        // the driver compiled from scratch anyway)
        if (m_psoDiskCache && !diskBlob) {
            auto blob = m_device->GetPipelineCachedBlob(handle);
            if (!blob.empty()) {
                m_psoDiskCache->Store(key.hash, blob.data(), blob.size());
            }
        }

        UZ_LOG(Renderer, Debug, "PsoCache miss for '{}' — created and cached", desc.debugName ? desc.debugName : "<unnamed>");
        return handle;
    }

    // Return Failed sentinel so callers never retry this pipeline.
    // Without this, a failed PSO creation would be retried every frame,
    // producing an infinite error log loop and eventual device removal.
    UZ_LOG(Renderer, Error, "CreateCachedPipeline: permanently failed for '{}' — will not retry",
           desc.debugName ? desc.debugName : "<unnamed>");
    ++m_failedPipelineCount;
    return RHIPipelineHandle::Failed();
}

// ─────────────────────────────────────────────────────────────────────────────
// Warmup — Pre-create all standard pipelines
// ─────────────────────────────────────────────────────────────────────────────

uint32_t PipelineLibrary::WarmupAll() {
    UZ_PROFILE_FUNCTION();

    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::WarmupAll called before Init");
        return 0;
    }

    const bool bindless = m_device->IsBindless();
    uint32_t count = 0;

    auto warmup = [&](RHIPipelineHandle h, const char* name) {
        if (h.IsValid()) {
            ++count;
        } else {
            UZ_LOG(Renderer, Warning, "WarmupAll: failed to create '{}'", name);
        }
    };

    auto warmupOptional = [&](RHIPipelineHandle h, const char* name) {
        if (h.IsValid()) {
            ++count;
        } else {
            UZ_LOG(Renderer, Warning,
                   "WarmupAll: optional pipeline '{}' failed to create (non-fatal)", name);
        }
    };

    warmup(GetForwardPBR(bindless), "ForwardPBR");
    warmup(GetGBuffer(bindless), "GBuffer");
    warmup(GetGBufferMasked(bindless), "GBufferMasked");
    warmup(GetTranslucent(bindless), "Translucent");
    warmup(GetAdditive(bindless), "Additive");
    warmup(GetLighting(), "Lighting");
    warmup(GetGBufferDebug(), "GBufferDebug");
    warmup(GetDebugView(), "DebugView");
    warmup(GetShadowDepth(bindless), "ShadowDepth");
    warmup(GetSkyAtmosphere(), "SkyAtmosphere");
    warmup(GetVelocityPass(), "VelocityBuffer");
    warmup(GetHeightFog(), "HeightFog");
    warmup(GetCloudComposite(), "CloudComposite");
    warmup(GetTransmittanceLut(), "TransmittanceLUT");
    warmup(GetSkyCubemapCapture(), "SkyCubemapCapture");
    warmup(GetSkyCubemapCapturePS(), "SkyCubemapCapturePS");
    warmup(GetDepthPrepass(bindless), "DepthPrepass");
    warmup(GetHiZPyramid(), "HiZPyramid");

    // GPU culling and distance field shadows use compute pipelines that may
    // fail PSO creation when root signature layout doesn't match the shader
    // (e.g., bindless root sig + non-bindless shader). These are optional
    // features created lazily on first use — skip warmup to avoid error logs.
    // warmupOptional(GetGpuCull(), "GpuCull");
    // warmupOptional(GetDfsCullObjects(), "DFS_CullObjects");
    // warmupOptional(GetDfsConeTrace(), "DFS_ConeTrace");
    warmup(GetSsrTrace(), "SSR_Trace");
    warmup(GetSsrResolve(), "SSR_Resolve");
    warmup(GetSsrTemporal(), "SSR_Temporal");
    warmup(GetSsrSpatialDenoise(), "SSR_SpatialDenoise");
    warmup(GetMotionBlur(), "MotionBlur");
    warmup(GetMotionBlurTileMax(), "MotionBlurTileMax");
    warmup(GetMotionBlurNeighborMax(), "MotionBlurNeighborMax");
    warmup(GetFxaa(), "FXAA");
    warmup(GetCasSharpen(), "CAS_Sharpen");

    // ── Atmosphere / LUT pipelines ──────────────────────────────────────
    warmup(GetMultiScatterLut(), "MultiScatterLUT");
    warmup(GetAerialPerspectiveVolume(), "AerialPerspectiveVolume");

    // ── Tonemap / Color grading ─────────────────────────────────────────
    warmup(GetTonemap(), "Tonemap");
    warmup(GetColorGradingLut(), "ColorGradingLUT");

    // ── TAA / TSR ───────────────────────────────────────────────────────
    warmup(GetTaaResolve(), "TAA_Resolve");
    warmup(GetTsrDilateVelocity(), "TSR_DilateVelocity");
    warmup(GetTsrRejectShading(), "TSR_RejectShading");
    warmup(GetTsrUpdateHistory(), "TSR_UpdateHistory");

    // ── DoF ─────────────────────────────────────────────────────────────
    warmup(GetDofSetup(), "DoF_Setup");
    warmup(GetDofBlur(), "DoF_Blur");

    // ── Instanced variants (optional — non-instanced path is always available) ──
    // These require SM6.1+ and may fail on downlevel feature levels. Failures
    // are non-fatal: the renderer falls back to the per-draw path.
    {
        auto warmupInstanced = [&](RHIPipelineHandle h, const char* name) {
            if (h.IsValid()) {
                ++count;
            } else {
                UZ_LOG(Renderer, Warning,
                       "WarmupAll: instanced pipeline '{}' failed to create "
                       "(non-fatal, will use per-draw fallback)", name);
            }
        };
        warmupInstanced(GetGBufferInstanced(), "GBuffer_Instanced");
        warmupInstanced(GetShadowDepthInstanced(), "ShadowDepth_Instanced");
        warmupInstanced(GetDepthPrepassInstanced(), "DepthPrepass_Instanced");
    }

    // ── Fullscreen debug ────────────────────────────────────────────────
    warmup(GetVisFullscreen(), "Vis_Fullscreen");

    // ── GI pipelines (SSGI + GDF + Radiance Cache + SDF) ────────────────
    warmup(GetSsgiPrevColorReduce(), "SSGI_PrevColorReduce");
    warmup(GetSsgiTrace(), "SSGI_Trace");
    warmup(GetSsgiSpatialDenoise(), "SSGI_SpatialDenoise");
    warmup(GetSsgiTemporalAccum(), "SSGI_TemporalAccum");
    warmup(GetSsgiBilateralUpsample(), "SSGI_BilateralUpsample");
    warmup(GetSvgfReproject(), "SVGF_Reproject");
    warmup(GetSvgfFilterMoments(), "SVGF_FilterMoments");
    warmup(GetSvgfAtrous(), "SVGF_ATrous");
    warmup(GetGodRays(), "GodRays");
    warmup(GetGdfComposite(), "GDF_Composite");
    warmup(GetMeshSdfBake(), "MeshSDF_Bake");
    warmup(GetRadianceCacheTrace(), "RadianceCache_Trace");

    // SM5 non-bindless variants are NOT warmed up on DX12 bindless devices.
    // DX12's root signature is bindless-only — non-bindless PSOs fail with
    // E_INVALIDARG because the shader expects slot-based descriptor tables
    // that the bindless root signature does not provide. These variants are
    // only needed on DX11/Vulkan where the primary path is non-bindless.

    UZ_LOG(Renderer, Info, "PipelineLibrary: warmed up {} pipelines", count);
    return count;
}

} // namespace uz
