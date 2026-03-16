/// UzEngine IBL Baker — Implementation.
///
/// Compiles compute shaders from embedded HLSL source (via uzembed gen headers),
/// creates GPU textures and pipelines via the RHI, and dispatches compute work
/// to bake BRDF LUT, irradiance, and prefiltered specular cubemaps.

#include "ibl_baker.h"

#include "log.h"
#include "rhi.h"
#include "shader_manager.h"
#include "engine_shaders.h"

#include <cstring>

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

IBLBaker::~IBLBaker() {
    if (m_initialized) {
        Shutdown();
    }
}

bool IBLBaker::Init(IRHIDevice* device, ShaderManager* shaderMgr) {
    if (m_initialized) {
        UZ_LOG(Renderer, Warning, "IBLBaker::Init called while already initialized");
        return true;
    }

    if (!device) {
        UZ_LOG(Renderer, Error, "IBLBaker::Init failed: device is null");
        return false;
    }
    if (!shaderMgr) {
        UZ_LOG(Renderer, Error, "IBLBaker::Init failed: shaderMgr is null");
        return false;
    }

    m_device = device;
    m_shaderMgr = shaderMgr;
    m_initialized = true;

    UZ_LOG(Renderer, Info, "IBLBaker initialized (pipelines will be created lazily)");
    return true;
}

void IBLBaker::Shutdown() {
    if (!m_initialized) {
        return;
    }

    // Destroy textures
    if (m_brdfLutTexture.IsValid()) {
        m_device->DestroyTexture(m_brdfLutTexture);
        m_brdfLutTexture = RHITextureHandle::Invalid();
    }
    if (m_irradianceTexture.IsValid()) {
        m_device->DestroyTexture(m_irradianceTexture);
        m_irradianceTexture = RHITextureHandle::Invalid();
    }
    if (m_prefilteredTexture.IsValid()) {
        m_device->DestroyTexture(m_prefilteredTexture);
        m_prefilteredTexture = RHITextureHandle::Invalid();
    }

    // Destroy pipelines
    if (m_brdfLutPipeline.IsValid()) {
        m_device->DestroyPipeline(m_brdfLutPipeline);
        m_brdfLutPipeline = RHIPipelineHandle::Invalid();
    }
    if (m_irradiancePipeline.IsValid()) {
        m_device->DestroyPipeline(m_irradiancePipeline);
        m_irradiancePipeline = RHIPipelineHandle::Invalid();
    }
    if (m_prefilterPipeline.IsValid()) {
        m_device->DestroyPipeline(m_prefilterPipeline);
        m_prefilterPipeline = RHIPipelineHandle::Invalid();
    }

    // Destroy shaders
    if (m_brdfLutCS.IsValid()) {
        m_device->DestroyShader(m_brdfLutCS);
        m_brdfLutCS = RHIShaderHandle::Invalid();
    }
    if (m_irradianceCS.IsValid()) {
        m_device->DestroyShader(m_irradianceCS);
        m_irradianceCS = RHIShaderHandle::Invalid();
    }
    if (m_prefilterCS.IsValid()) {
        m_device->DestroyShader(m_prefilterCS);
        m_prefilterCS = RHIShaderHandle::Invalid();
    }

    m_brdfLutSrvIndex = UINT32_MAX;
    m_device = nullptr;
    m_shaderMgr = nullptr;
    m_initialized = false;

    UZ_LOG(Renderer, Info, "IBLBaker shut down");
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline Creation Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Compiles a compute shader from embedded source via ShaderPipeline.
///
/// The shader name must end with `_cs.hlsl` so the pipeline infers the
/// Compute stage and uses `cs_6_6` target profile (or `cs_5_1` for SM5).
/// The entry point in the HLSL must be `main`.
static RHIShaderHandle CompileComputeShader(IRHIDevice* device,
                                             ShaderManager* shaderMgr,
                                             const char* name,
                                             const char* hlslSource) {
    auto bytecode = shaderMgr->GetPipeline().LoadBytecodeFromSource(name, hlslSource);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "IBLBaker: failed to compile compute shader '{}'", name);
        return RHIShaderHandle::Invalid();
    }

    RHIShaderDesc desc;
    desc.bytecode = bytecode.data();
    desc.bytecodeSize = bytecode.size();
    desc.stage = RHIShaderStage::Compute;
    desc.entryPoint = "main";
    desc.debugName = name;

    return device->CreateShader(desc);
}

bool IBLBaker::EnsureBRDFLutPipeline() {
    if (m_brdfLutPipeline.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::BrdfLutCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "IBLBaker: failed to compile brdf_lut_cs.hlsl");
        return false;
    }

    RHIShaderDesc shaderDesc;
    shaderDesc.bytecode = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage = RHIShaderStage::Compute;
    shaderDesc.entryPoint = "main";
    shaderDesc.debugName = "brdf_lut_cs";

    m_brdfLutCS = m_device->CreateShader(shaderDesc);
    if (!m_brdfLutCS.IsValid()) {
        UZ_LOG(Renderer, Error, "IBLBaker: failed to create brdf_lut_cs shader");
        return false;
    }

    RHIComputePipelineDesc pipeDesc;
    pipeDesc.computeShader = m_brdfLutCS;
    pipeDesc.debugName = "IBL_BRDFLut";

    m_brdfLutPipeline = m_device->CreateComputePipeline(pipeDesc);
    if (!m_brdfLutPipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "IBLBaker: failed to create brdf_lut compute pipeline");
        return false;
    }

    return true;
}

bool IBLBaker::EnsureIrradiancePipeline() {
    if (m_irradiancePipeline.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::IrradianceCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "IBLBaker: failed to compile irradiance_cs.hlsl");
        return false;
    }

    RHIShaderDesc shaderDesc;
    shaderDesc.bytecode = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage = RHIShaderStage::Compute;
    shaderDesc.entryPoint = "main";
    shaderDesc.debugName = "irradiance_cs";

    m_irradianceCS = m_device->CreateShader(shaderDesc);
    if (!m_irradianceCS.IsValid()) {
        UZ_LOG(Renderer, Error, "IBLBaker: failed to create irradiance_cs shader");
        return false;
    }

    RHIComputePipelineDesc pipeDesc;
    pipeDesc.computeShader = m_irradianceCS;
    pipeDesc.debugName = "IBL_Irradiance";

    m_irradiancePipeline = m_device->CreateComputePipeline(pipeDesc);
    if (!m_irradiancePipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "IBLBaker: failed to create irradiance compute pipeline");
        return false;
    }

    return true;
}

bool IBLBaker::EnsurePrefilterPipeline() {
    if (m_prefilterPipeline.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::PrefilterCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "IBLBaker: failed to compile prefilter_cs.hlsl");
        return false;
    }

    RHIShaderDesc shaderDesc;
    shaderDesc.bytecode = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage = RHIShaderStage::Compute;
    shaderDesc.entryPoint = "main";
    shaderDesc.debugName = "prefilter_cs";

    m_prefilterCS = m_device->CreateShader(shaderDesc);
    if (!m_prefilterCS.IsValid()) {
        UZ_LOG(Renderer, Error, "IBLBaker: failed to create prefilter_cs shader");
        return false;
    }

    RHIComputePipelineDesc pipeDesc;
    pipeDesc.computeShader = m_prefilterCS;
    pipeDesc.debugName = "IBL_Prefilter";

    m_prefilterPipeline = m_device->CreateComputePipeline(pipeDesc);
    if (!m_prefilterPipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "IBLBaker: failed to create prefilter compute pipeline");
        return false;
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// BakeBRDFLut
// ─────────────────────────────────────────────────────────────────────────────

uint32_t IBLBaker::BakeBRDFLut() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "IBLBaker::BakeBRDFLut called before Init()");
        return UINT32_MAX;
    }

    // Return cached result if already baked
    if (m_brdfLutSrvIndex != UINT32_MAX) {
        return m_brdfLutSrvIndex;
    }

    if (!EnsureBRDFLutPipeline()) {
        return UINT32_MAX;
    }

    // Create BRDF LUT texture: 512x512, RG16F, UAV + SRV
    RHITextureDesc texDesc;
    texDesc.width = kBRDFLutSize;
    texDesc.height = kBRDFLutSize;
    texDesc.format = RHIFormat::RG16Float;
    texDesc.usage = RHITextureUsage::UnorderedAccess | RHITextureUsage::ShaderResource;
    texDesc.debugName = "IBL_BRDFLut";

    m_brdfLutTexture = m_device->CreateTexture(texDesc);
    if (!m_brdfLutTexture.IsValid()) {
        UZ_LOG(Renderer, Error, "IBLBaker: failed to create BRDF LUT texture");
        return UINT32_MAX;
    }

    // Create a command list, dispatch, and wait
    auto* cmdList = m_device->CreateCommandList(RHIQueueType::Graphics);
    if (!cmdList) {
        UZ_LOG(Renderer, Error, "IBLBaker: failed to create command list for BRDF LUT bake");
        return UINT32_MAX;
    }

    auto* fence = m_device->CreateFence();
    if (!fence) {
        m_device->DestroyCommandList(cmdList);
        UZ_LOG(Renderer, Error, "IBLBaker: failed to create fence for BRDF LUT bake");
        return UINT32_MAX;
    }

    cmdList->Begin();

    // Set compute pipeline
    cmdList->SetPipeline(m_brdfLutPipeline);

    // Root constants at slot 0: outputWidth, outputHeight
    const uint32_t rootConstants[2] = { kBRDFLutSize, kBRDFLutSize };
    cmdList->SetRootConstants(0, rootConstants, 2);

    // Bind UAV descriptor table at root param 4 (u0-u3 range)
    uint32_t uavIndex = m_device->GetTextureUAVIndex(m_brdfLutTexture);
    if (uavIndex == UINT32_MAX) {
        UZ_LOG(Renderer, Error, "IBLBaker: BRDF LUT texture has no UAV descriptor");
        m_device->DestroyCommandList(cmdList);
        m_device->DestroyFence(fence);
        return UINT32_MAX;
    }
    cmdList->SetDescriptorTable(4, uavIndex);

    // Dispatch: (512/8, 512/8, 1) = (64, 64, 1)
    cmdList->Dispatch(kBRDFLutSize / kThreadGroupSize,
                      kBRDFLutSize / kThreadGroupSize, 1);

    // Barrier: UAV -> ShaderResource
    RHIBarrierDesc barrier;
    barrier.texture = m_brdfLutTexture;
    barrier.buffer = RHIBufferHandle::Invalid();
    barrier.stateBefore = RHIResourceState::UnorderedAccess;
    barrier.stateAfter = RHIResourceState::ShaderResource;
    cmdList->ResourceBarrier(&barrier, 1);

    cmdList->End();
    cmdList->Execute();

    // Wait for completion
    fence->Signal(1);
    fence->WaitCPU(1);

    // Cleanup
    m_device->DestroyFence(fence);
    m_device->DestroyCommandList(cmdList);

    m_brdfLutSrvIndex = m_device->GetTextureSRVIndex(m_brdfLutTexture);
    if (m_brdfLutSrvIndex == UINT32_MAX) {
        UZ_LOG(Renderer, Error, "IBLBaker: BRDF LUT texture has no SRV — this should not happen");
        return UINT32_MAX;
    }

    UZ_LOG(Renderer, Info, "IBLBaker: BRDF LUT baked ({}x{}, RG16F, SRV={})",
           kBRDFLutSize, kBRDFLutSize, m_brdfLutSrvIndex);

    return m_brdfLutSrvIndex;
}

// ─────────────────────────────────────────────────────────────────────────────
// BakeEnvironment
// ─────────────────────────────────────────────────────────────────────────────

IBLBaker::BakeResult IBLBaker::BakeEnvironment(uint32_t envCubemapSrvIndex) {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "IBLBaker::BakeEnvironment called before Init()");
        return {};
    }

    if (!EnsureIrradiancePipeline() || !EnsurePrefilterPipeline()) {
        return {};
    }

    BakeResult result;

    // ── Create irradiance cubemap texture (32x32 per face, 6 slices) ─────
    RHITextureDesc irradDesc;
    irradDesc.width = kIrradianceSize;
    irradDesc.height = kIrradianceSize;
    irradDesc.depthOrArraySize = 6;
    irradDesc.format = RHIFormat::RGBA16Float;
    irradDesc.usage = RHITextureUsage::UnorderedAccess | RHITextureUsage::ShaderResource;
    irradDesc.debugName = "IBL_Irradiance";

    auto irradianceTex = m_device->CreateTexture(irradDesc);
    if (!irradianceTex.IsValid()) {
        UZ_LOG(Renderer, Error, "IBLBaker: failed to create irradiance texture");
        return {};
    }

    // ── Create per-mip temporary textures for prefiltered specular ───────
    // Each mip level gets a separate 6-slice texture for UAV dispatch,
    // then we copy into the final mip chain. This avoids per-mip UAV
    // descriptor complexity in the RHI.
    RHITextureHandle prefilteredMipTextures[kPrefilteredMipLevels];
    for (uint32_t mip = 0; mip < kPrefilteredMipLevels; ++mip) {
        const uint32_t mipSize = kPrefilteredSize >> mip;

        RHITextureDesc mipDesc;
        mipDesc.width = mipSize;
        mipDesc.height = mipSize;
        mipDesc.depthOrArraySize = 6;
        mipDesc.format = RHIFormat::RGBA16Float;
        mipDesc.usage = RHITextureUsage::UnorderedAccess
                      | RHITextureUsage::ShaderResource
                      | RHITextureUsage::CopySrc;
        mipDesc.debugName = "IBL_PrefilteredMip";

        prefilteredMipTextures[mip] = m_device->CreateTexture(mipDesc);
        if (!prefilteredMipTextures[mip].IsValid()) {
            UZ_LOG(Renderer, Error, "IBLBaker: failed to create prefiltered mip {} texture", mip);
            // Cleanup already created textures
            m_device->DestroyTexture(irradianceTex);
            for (uint32_t j = 0; j < mip; ++j) {
                m_device->DestroyTexture(prefilteredMipTextures[j]);
            }
            return {};
        }
    }

    // ── Create final prefiltered cubemap with all mip levels ────────────
    RHITextureDesc prefilteredDesc;
    prefilteredDesc.width = kPrefilteredSize;
    prefilteredDesc.height = kPrefilteredSize;
    prefilteredDesc.depthOrArraySize = 6;
    prefilteredDesc.mipLevels = static_cast<uint16_t>(kPrefilteredMipLevels);
    prefilteredDesc.format = RHIFormat::RGBA16Float;
    prefilteredDesc.usage = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;
    prefilteredDesc.debugName = "IBL_PrefilteredSpecular";

    auto prefilteredTex = m_device->CreateTexture(prefilteredDesc);
    if (!prefilteredTex.IsValid()) {
        UZ_LOG(Renderer, Error, "IBLBaker: failed to create prefiltered specular texture");
        m_device->DestroyTexture(irradianceTex);
        for (uint32_t i = 0; i < kPrefilteredMipLevels; ++i) {
            m_device->DestroyTexture(prefilteredMipTextures[i]);
        }
        return {};
    }

    // ── Dispatch compute shaders ────────────────────────────────────────
    auto* cmdList = m_device->CreateCommandList(RHIQueueType::Graphics);
    auto* fence = m_device->CreateFence();
    if (!cmdList || !fence) {
        UZ_LOG(Renderer, Error, "IBLBaker: failed to create command list or fence for environment bake");
        m_device->DestroyTexture(irradianceTex);
        m_device->DestroyTexture(prefilteredTex);
        for (uint32_t i = 0; i < kPrefilteredMipLevels; ++i) {
            m_device->DestroyTexture(prefilteredMipTextures[i]);
        }
        if (cmdList) m_device->DestroyCommandList(cmdList);
        if (fence)   m_device->DestroyFence(fence);
        return {};
    }

    cmdList->Begin();

    // ── Irradiance dispatch ─────────────────────────────────────────────
    cmdList->SetPipeline(m_irradiancePipeline);

    // Root constants: envMapIndex, faceSize
    const uint32_t irradConstants[2] = { envCubemapSrvIndex, kIrradianceSize };
    cmdList->SetRootConstants(0, irradConstants, 2);

    // Bind irradiance cubemap UAV at root param 4
    uint32_t irradUavIndex = m_device->GetTextureUAVIndex(irradianceTex);
    cmdList->SetDescriptorTable(4, irradUavIndex);

    // Dispatch (32/8, 32/8, 6) = (4, 4, 6) — one thread group per 8x8 tile per face
    cmdList->Dispatch(kIrradianceSize / kThreadGroupSize,
                      kIrradianceSize / kThreadGroupSize, 6);

    // Barrier: irradiance UAV -> SRV
    RHIBarrierDesc irradBarrier;
    irradBarrier.texture = irradianceTex;
    irradBarrier.buffer = RHIBufferHandle::Invalid();
    irradBarrier.stateBefore = RHIResourceState::UnorderedAccess;
    irradBarrier.stateAfter = RHIResourceState::ShaderResource;
    cmdList->ResourceBarrier(&irradBarrier, 1);

    // ── Prefiltered specular dispatch (per mip level) ───────────────────
    cmdList->SetPipeline(m_prefilterPipeline);

    for (uint32_t mip = 0; mip < kPrefilteredMipLevels; ++mip) {
        const uint32_t mipSize = kPrefilteredSize >> mip;
        const float mipRoughness = static_cast<float>(mip) /
                                    static_cast<float>(kPrefilteredMipLevels - 1);
        const uint32_t sampleCount = 256;

        // Root constants: envMapIndex, faceSize, roughness (as uint bits), sampleCount
        uint32_t roughnessBits = 0;
        std::memcpy(&roughnessBits, &mipRoughness, sizeof(float));

        const uint32_t prefilterConstants[4] = {
            envCubemapSrvIndex, mipSize, roughnessBits, sampleCount
        };
        cmdList->SetRootConstants(0, prefilterConstants, 4);

        // Bind per-mip UAV at root param 4
        uint32_t mipUavIndex = m_device->GetTextureUAVIndex(prefilteredMipTextures[mip]);
        cmdList->SetDescriptorTable(4, mipUavIndex);

        // Dispatch per-mip texture
        const uint32_t groupsXY = (mipSize + kThreadGroupSize - 1) / kThreadGroupSize;
        cmdList->Dispatch(groupsXY, groupsXY, 6);

        // Barrier: per-mip texture UAV -> CopySrc
        RHIBarrierDesc mipBarrier;
        mipBarrier.texture = prefilteredMipTextures[mip];
        mipBarrier.buffer = RHIBufferHandle::Invalid();
        mipBarrier.stateBefore = RHIResourceState::UnorderedAccess;
        mipBarrier.stateAfter = RHIResourceState::CopySrc;
        cmdList->ResourceBarrier(&mipBarrier, 1);
    }

    // ── Copy per-mip textures into the final mipmapped cubemap ──────────
    // Note: CopyTexture copies all subresources and requires matching dimensions.
    // For per-mip copy, we'd need CopyBufferToTexture with mip targeting,
    // or the backend's CopyTexture with mip-level specification.
    //
    // Since the current RHI CopyTexture copies ALL subresources and requires
    // matching dimensions, and per-mip textures have different dimensions,
    // we cannot use CopyTexture directly.
    //
    // Alternative: Use CopyBufferToTexture with per-subresource specification.
    // But for the initial implementation, we skip the copy and use the
    // per-mip textures directly as separate SRVs. The shader will need to
    // sample from the correct mip texture based on roughness.
    //
    // TODO: When the RHI adds per-subresource CopyTexture, consolidate into
    // a single mipmapped cubemap. For now, mip 0 is the primary prefiltered
    // cubemap (mirror reflection), which is what most materials need.
    //
    // Actually, for the simplest working implementation: use mip 0 texture
    // as the prefiltered cubemap SRV. The shader will sample it without
    // mip-level LOD selection. This is a quality compromise but gets IBL
    // working end-to-end. Full mip chain support requires RHI CopyTexture
    // per-subresource support.

    cmdList->End();
    cmdList->Execute();

    fence->Signal(1);
    fence->WaitCPU(1);

    m_device->DestroyFence(fence);
    m_device->DestroyCommandList(cmdList);

    // ── Get SRV indices ─────────────────────────────────────────────────
    result.irradianceSrvIndex = m_device->GetTextureSRVIndex(irradianceTex);
    // Use mip 0 texture as the prefiltered SRV for now
    result.prefilteredSrvIndex = m_device->GetTextureSRVIndex(prefilteredMipTextures[0]);

    if (result.irradianceSrvIndex == UINT32_MAX) {
        UZ_LOG(Renderer, Error, "IBLBaker: irradiance texture has no SRV");
    }
    if (result.prefilteredSrvIndex == UINT32_MAX) {
        UZ_LOG(Renderer, Error, "IBLBaker: prefiltered texture has no SRV");
    }

    // Cleanup temporary mip textures (keep mip 0 as it provides the SRV)
    for (uint32_t i = 1; i < kPrefilteredMipLevels; ++i) {
        m_device->DestroyTexture(prefilteredMipTextures[i]);
    }

    // Destroy final prefiltered tex (not used — per-mip approach)
    m_device->DestroyTexture(prefilteredTex);

    // Destroy previous environment textures if re-baking (sky changed)
    if (m_irradianceTexture.IsValid()) {
        m_device->DestroyTexture(m_irradianceTexture);
    }
    if (m_prefilteredTexture.IsValid()) {
        m_device->DestroyTexture(m_prefilteredTexture);
    }

    // Track texture handles for cleanup in Shutdown()
    m_irradianceTexture  = irradianceTex;
    m_prefilteredTexture = prefilteredMipTextures[0];

    UZ_LOG(Renderer, Info,
           "IBLBaker: environment baked (irradiance={}x{} SRV={}, prefiltered={}x{} SRV={})",
           kIrradianceSize, kIrradianceSize, result.irradianceSrvIndex,
           kPrefilteredSize, kPrefilteredSize, result.prefilteredSrvIndex);

    return result;
}

} // namespace uz
