/// UzEngine Cloud Pass — Implementation.
///
/// Creates half-res RGBA16F cloud output, compiles the ray-march compute shader,
/// and dispatches GPU work each frame. The cloud output is composited over the
/// HDR scene by the CloudComposite pass in frame_lighting.cpp.
///
/// @ref Schneider & Vos 2015 "Real-Time Volumetric Cloudscapes of Horizon Zero Dawn"
/// @ref Hillaire 2020 "A Scalable and Production Ready Sky and Atmosphere Rendering Technique"

#include "cloud_pass.h"

#include "engine/rhi/rhi.h"
#include "cb_ring_allocator.h"
#include "engine/asset/vfs.h"
#include "shader_manager.h"
#include "engine_shaders.h"
#include "log.h"
#include "profiler.h"
#include "cvar.h"

#include <cstring>
#include <cstdio>
#include <fstream>

UZ_DECLARE_LOG_CATEGORY(Renderer);

// ── CVars ────────────────────────────────────────────────────────────────
static auto* cv_Clouds = UZ_CVAR_BOOL("r.Clouds", true, "Volumetric clouds");
static auto* cv_CloudNoiseXY     = UZ_CVAR_FLOAT("r.Cloud.NoiseXYScale", 0.12f, "Cloud noise XY coordinate scale", 0.01f, 1.0f);
static auto* cv_CloudNoiseZ      = UZ_CVAR_FLOAT("r.Cloud.NoiseZScale", 3.0f, "Cloud noise Z coordinate scale", 0.5f, 10.0f);
static auto* cv_CloudOctaves     = UZ_CVAR_INT("r.Cloud.ShapeOctaves", 4, "Cloud shape FBM octaves", 1, 6);
static auto* cv_CloudFBMDecay    = UZ_CVAR_FLOAT("r.Cloud.FBMDecay", 0.5f, "FBM amplitude decay per octave", 0.3f, 0.8f);
static auto* cv_CloudFBMLac      = UZ_CVAR_FLOAT("r.Cloud.FBMLacunarity", 2.0f, "FBM frequency multiplier per octave", 1.5f, 4.0f);
static auto* cv_CloudErosion     = UZ_CVAR_FLOAT("r.Cloud.DetailErosion", 0.35f, "Detail noise erosion strength", 0.0f, 1.0f);
static auto* cv_CloudAerialDens  = UZ_CVAR_FLOAT("r.Cloud.AerialPerspectiveDensity", 0.04f, "Aerial perspective fog density", 0.0f, 0.2f);
static auto* cv_CloudDensScale   = UZ_CVAR_FLOAT("r.Cloud.DensityScale", 25.0f, "Cloud density normalization scale", 1.0f, 100.0f);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

CloudPass::~CloudPass() {
    if (m_initialized) {
        Shutdown();
    }
}

bool CloudPass::Init(IRHIDevice* device, ShaderManager* shaderMgr,
                     uint32_t width, uint32_t height) {
    if (m_initialized) {
        UZ_LOG(Renderer, Warning, "CloudPass::Init called while already initialized");
        return true;
    }

    if (!device || !shaderMgr) {
        UZ_LOG(Renderer, Error, "CloudPass::Init: null device or shaderMgr");
        return false;
    }

    m_device    = device;
    m_shaderMgr = shaderMgr;

    // Cloud output is half-resolution for performance
    uint32_t halfW = (width + 1) / 2;
    uint32_t halfH = (height + 1) / 2;

    if (!CreateOutputTexture(halfW, halfH)) {
        return false;
    }

    m_initialized = true;
    UZ_LOG(Renderer, Info, "CloudPass initialized ({}x{} -> half-res {}x{}, SRV={}, UAV={})",
           width, height, halfW, halfH, m_outputSrvIndex, m_outputUavIndex);
    return true;
}

bool CloudPass::CreateOutputTexture(uint32_t halfWidth, uint32_t halfHeight) {
    m_halfWidth  = halfWidth;
    m_halfHeight = halfHeight;

    RHITextureDesc desc{};
    desc.width     = halfWidth;
    desc.height    = halfHeight;
    desc.format    = RHIFormat::RGBA16Float;
    desc.usage     = RHITextureUsage::ShaderResource
                   | RHITextureUsage::UnorderedAccess;
    desc.debugName = "Cloud_Output";

    m_outputTexture = m_device->CreateTexture(desc);
    if (!m_outputTexture.IsValid()) {
        UZ_LOG(Renderer, Error, "CloudPass: failed to create output texture");
        return false;
    }

    m_outputSrvIndex = m_device->GetTextureSRVIndex(m_outputTexture);
    m_outputUavIndex = m_device->GetTextureUAVIndex(m_outputTexture);

    if (m_outputSrvIndex == UINT32_MAX) {
        UZ_LOG(Renderer, Error, "CloudPass: output texture has no SRV descriptor");
        DestroyOutputTexture();
        return false;
    }

    if (m_outputUavIndex == UINT32_MAX) {
        UZ_LOG(Renderer, Error, "CloudPass: output texture has no UAV descriptor");
        DestroyOutputTexture();
        return false;
    }

    UZ_LOG(Renderer, Debug, "CloudPass: output texture created — SRV={}, UAV={}",
           m_outputSrvIndex, m_outputUavIndex);
    return true;
}

void CloudPass::DestroyOutputTexture() {
    if (m_outputTexture.IsValid()) {
        m_device->DestroyTexture(m_outputTexture);
        m_outputTexture = RHITextureHandle::Invalid();
    }
    m_outputSrvIndex = UINT32_MAX;
    m_outputUavIndex = UINT32_MAX;
    m_halfWidth  = 0;
    m_halfHeight = 0;
    m_initialBarriersDone = false;
}

void CloudPass::Shutdown() {
    if (!m_initialized) return;

    DestroyOutputTexture();

    // Destroy noise texture
    if (m_shapeNoiseTexture.IsValid()) {
        m_device->DestroyTexture(m_shapeNoiseTexture);
        m_shapeNoiseTexture = RHITextureHandle::Invalid();
    }
    m_shapeNoiseSrvIndex = UINT32_MAX;

    if (m_detailNoiseTexture.IsValid()) {
        m_device->DestroyTexture(m_detailNoiseTexture);
        m_detailNoiseTexture = RHITextureHandle::Invalid();
    }
    m_detailNoiseSrvIndex = UINT32_MAX;

    // Destroy staging buffers
    for (auto& buf : m_stagingBuffers) {
        if (buf.IsValid()) {
            m_device->DestroyBuffer(buf);
        }
    }
    m_stagingBuffers.clear();

    // Destroy pipeline + shader
    if (m_raymarchPipeline.IsValid()) {
        m_device->DestroyPipeline(m_raymarchPipeline);
        m_raymarchPipeline = RHIPipelineHandle::Invalid();
    }
    if (m_raymarchCS.IsValid()) {
        m_device->DestroyShader(m_raymarchCS);
        m_raymarchCS = RHIShaderHandle::Invalid();
    }

    m_device      = nullptr;
    m_shaderMgr   = nullptr;
    m_initialized = false;
    m_hasNoiseTextures = false;

    UZ_LOG(Renderer, Info, "CloudPass shut down");
}

void CloudPass::OnResize(uint32_t width, uint32_t height) {
    uint32_t halfW = (width + 1) / 2;
    uint32_t halfH = (height + 1) / 2;

    if (halfW == m_halfWidth && halfH == m_halfHeight) return;

    DestroyOutputTexture();
    CreateOutputTexture(halfW, halfH);
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline Creation
// ─────────────────────────────────────────────────────────────────────────────

bool CloudPass::EnsureRaymarchPipeline() {
    if (m_raymarchPipeline.IsValid()) return true;

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::CloudRaymarchCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "CloudPass: cloud_raymarch_cs.hlsl bytecode is empty — shader compilation failed");
        return false;
    }

    UZ_LOG(Renderer, Info, "CloudPass: raymarch shader bytecode = {} bytes", bytecode.size());

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "cloud_raymarch_cs";

    m_raymarchCS = m_device->CreateShader(shaderDesc);
    if (!m_raymarchCS.IsValid()) {
        UZ_LOG(Renderer, Error, "CloudPass: failed to create raymarch shader");
        return false;
    }

    RHIComputePipelineDesc pipeDesc{};
    pipeDesc.computeShader = m_raymarchCS;
    pipeDesc.debugName     = "Cloud_Raymarch";

    m_raymarchPipeline = m_device->CreateComputePipeline(pipeDesc);
    if (!m_raymarchPipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "CloudPass: failed to create raymarch pipeline");
        return false;
    }

    UZ_LOG(Renderer, Info, "CloudPass: raymarch pipeline created (handle={})", m_raymarchPipeline.index);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Noise Texture Loading
// ─────────────────────────────────────────────────────────────────────────────

void CloudPass::LoadNoiseTextures(IRHICommandList* cmdList, IVFS* vfs) {
    if (m_hasNoiseTextures) return;
    if (!m_initialized || !cmdList) return;

    // Load shape noise — 128^3 R8 (2,097,152 bytes)
    const std::string shapePath = "atmosphere/shape.bin";
    constexpr uint32_t kNoiseRes = 128;
    constexpr uint64_t kExpectedSize = kNoiseRes * kNoiseRes * kNoiseRes; // 2,097,152

    std::vector<uint8_t> r8Data;
    if (vfs) {
        r8Data = vfs->Read(shapePath);
    }
    if (r8Data.empty()) {
        // Fallback: try direct file I/O (dev mode with loose files)
        std::ifstream file(shapePath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            static bool loggedOnce = false;
            if (!loggedOnce) {
                UZ_LOG(Renderer, Warning, "CloudPass: could not load {} — clouds will use procedural noise", shapePath);
                loggedOnce = true;
            }
            return;
        }
        auto fileSize = file.tellg();
        r8Data.resize(static_cast<size_t>(fileSize));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(r8Data.data()), fileSize);
    }
    if (r8Data.size() != kExpectedSize) {
        UZ_LOG(Renderer, Error, "CloudPass: shape.bin size mismatch — expected {}, got {}",
               kExpectedSize, r8Data.size());
        return;
    }

    // Expand R8 -> RGBA8 (replicate to all 4 channels)
    constexpr uint64_t kRGBA8Size = kExpectedSize * 4;
    std::vector<uint8_t> rgba8Data(kRGBA8Size);
    for (uint64_t i = 0; i < kExpectedSize; ++i) {
        uint8_t v = r8Data[i];
        rgba8Data[i * 4 + 0] = v;
        rgba8Data[i * 4 + 1] = v;
        rgba8Data[i * 4 + 2] = v;
        rgba8Data[i * 4 + 3] = v;
    }

    // Create 3D texture
    RHITextureDesc texDesc{};
    texDesc.width            = kNoiseRes;
    texDesc.height           = kNoiseRes;
    texDesc.depthOrArraySize = kNoiseRes;
    texDesc.format           = RHIFormat::RGBA8Unorm;
    texDesc.usage            = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;
    texDesc.is3D             = true;
    texDesc.debugName        = "Cloud_ShapeNoise";

    m_shapeNoiseTexture = m_device->CreateTexture(texDesc);
    if (!m_shapeNoiseTexture.IsValid()) {
        UZ_LOG(Renderer, Error, "CloudPass: failed to create 3D shape noise texture");
        return;
    }

    m_shapeNoiseSrvIndex = m_device->GetTextureSRVIndex(m_shapeNoiseTexture);
    if (m_shapeNoiseSrvIndex == UINT32_MAX) {
        UZ_LOG(Renderer, Error, "CloudPass: shape noise texture has no SRV descriptor");
        m_device->DestroyTexture(m_shapeNoiseTexture);
        m_shapeNoiseTexture = RHITextureHandle::Invalid();
        return;
    }

    // Create staging upload buffer
    // DX12 requires row pitch aligned to D3D12_TEXTURE_DATA_PITCH_ALIGNMENT = 256 bytes.
    // For 128 texels * 4 bytes/texel = 512 bytes/row — already aligned to 256.
    constexpr uint32_t kRowPitch  = kNoiseRes * 4;  // 512 bytes per row
    constexpr uint64_t kSlicePitch = kRowPitch * kNoiseRes; // 65536 bytes per slice
    constexpr uint64_t kStagingSize = kSlicePitch * kNoiseRes; // 8,388,608 bytes total

    RHIBufferDesc bufDesc{};
    bufDesc.sizeBytes = kStagingSize;
    bufDesc.usage     = RHIBufferUsage::CopySrc;
    bufDesc.memory    = RHIMemoryType::Upload;
    bufDesc.debugName = "Cloud_ShapeNoise_Staging";

    auto stagingBuf = m_device->CreateBuffer(bufDesc);
    if (!stagingBuf.IsValid()) {
        UZ_LOG(Renderer, Error, "CloudPass: failed to create staging buffer for noise upload");
        m_device->DestroyTexture(m_shapeNoiseTexture);
        m_shapeNoiseTexture = RHITextureHandle::Invalid();
        m_shapeNoiseSrvIndex = UINT32_MAX;
        return;
    }

    // Map and copy data
    void* mapped = m_device->MapBuffer(stagingBuf);
    if (!mapped) {
        UZ_LOG(Renderer, Error, "CloudPass: failed to map staging buffer");
        m_device->DestroyBuffer(stagingBuf);
        m_device->DestroyTexture(m_shapeNoiseTexture);
        m_shapeNoiseTexture = RHITextureHandle::Invalid();
        m_shapeNoiseSrvIndex = UINT32_MAX;
        return;
    }
    std::memcpy(mapped, rgba8Data.data(), kStagingSize);
    m_device->UnmapBuffer(stagingBuf);

    // Barrier: Common -> CopyDst
    RHIBarrierDesc barrier{};
    barrier.texture     = m_shapeNoiseTexture;
    barrier.stateBefore = RHIResourceState::Common;
    barrier.stateAfter  = RHIResourceState::CopyDst;
    cmdList->ResourceBarrier(&barrier, 1);

    // Copy buffer -> 3D texture (single copy — all depth slices are subresource 0)
    // DX12 3D textures have 1 subresource per mip level containing ALL depth slices.
    // The footprint returned by GetCopyableFootprints includes the full depth extent.
    cmdList->CopyBufferToTexture(stagingBuf, 0, kRowPitch,
                                 m_shapeNoiseTexture, 0, 0);

    // Barrier: CopyDst -> ShaderResource
    barrier.stateBefore = RHIResourceState::CopyDst;
    barrier.stateAfter  = RHIResourceState::ShaderResource;
    cmdList->ResourceBarrier(&barrier, 1);

    // Keep staging buffer alive — will be destroyed in Shutdown()
    m_stagingBuffers.push_back(stagingBuf);

    UZ_LOG(Renderer, Info, "CloudPass: shape noise loaded (128^3 RGBA8, SRV={})", m_shapeNoiseSrvIndex);

    // ── Load detail noise — 32^3 R8 (32,768 bytes) ──
    // VFS asset from runtime/content/atmosphere/shape_detail.bin
    const std::string detailPath = "atmosphere/shape_detail.bin";
    constexpr uint32_t kDetailRes = 32;
    constexpr uint64_t kDetailExpSize = kDetailRes * kDetailRes * kDetailRes; // 32768

    std::vector<uint8_t> detailR8;
    if (vfs) {
        detailR8 = vfs->Read(detailPath);
    }
    if (detailR8.empty()) {
        static bool loggedOnce = false;
        if (!loggedOnce) {
            UZ_LOG(Renderer, Warning, "CloudPass: could not load {} via VFS — detail noise erosion disabled", detailPath);
            loggedOnce = true;
        }
    }

    if (detailR8.size() == kDetailExpSize) {
        // Expand R8 -> RGBA8
        // DX12 requires D3D12_TEXTURE_DATA_PITCH_ALIGNMENT = 256 bytes per row.
        // 32 texels * 4 bytes = 128 bytes/row — must pad to 256 for staging buffer.
        constexpr uint32_t kDetRowPitch        = kDetailRes * 4;       // 128 bytes (source)
        constexpr uint32_t kDetAlignedRowPitch = (kDetRowPitch + 255u) & ~255u; // 256 bytes (aligned)
        constexpr uint64_t kDetAlignedSlice    = kDetAlignedRowPitch * kDetailRes;
        constexpr uint64_t kDetStagingSize     = kDetAlignedSlice * kDetailRes;

        // Expand R8 -> RGBA8 with row padding for DX12 alignment
        std::vector<uint8_t> detailStaged(kDetStagingSize, 0);
        for (uint32_t z = 0; z < kDetailRes; ++z) {
            for (uint32_t y = 0; y < kDetailRes; ++y) {
                uint64_t srcRow = (z * kDetailRes + y) * kDetailRes;
                uint64_t dstRow = z * kDetAlignedSlice + y * kDetAlignedRowPitch;
                for (uint32_t x = 0; x < kDetailRes; ++x) {
                    uint8_t v = detailR8[srcRow + x];
                    detailStaged[dstRow + x * 4 + 0] = v;
                    detailStaged[dstRow + x * 4 + 1] = v;
                    detailStaged[dstRow + x * 4 + 2] = v;
                    detailStaged[dstRow + x * 4 + 3] = v;
                }
            }
        }

        RHITextureDesc detTexDesc{};
        detTexDesc.width            = kDetailRes;
        detTexDesc.height           = kDetailRes;
        detTexDesc.depthOrArraySize = kDetailRes;
        detTexDesc.format           = RHIFormat::RGBA8Unorm;
        detTexDesc.usage            = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;
        detTexDesc.is3D             = true;
        detTexDesc.debugName        = "Cloud_DetailNoise";

        m_detailNoiseTexture = m_device->CreateTexture(detTexDesc);
        if (m_detailNoiseTexture.IsValid()) {
            m_detailNoiseSrvIndex = m_device->GetTextureSRVIndex(m_detailNoiseTexture);

            RHIBufferDesc detBufDesc{};
            detBufDesc.sizeBytes = kDetStagingSize;
            detBufDesc.usage     = RHIBufferUsage::CopySrc;
            detBufDesc.memory    = RHIMemoryType::Upload;
            detBufDesc.debugName = "Cloud_DetailNoise_Staging";

            auto detStaging = m_device->CreateBuffer(detBufDesc);
            if (detStaging.IsValid()) {
                void* detMapped = m_device->MapBuffer(detStaging);
                if (detMapped) {
                    std::memcpy(detMapped, detailStaged.data(), kDetStagingSize);
                    m_device->UnmapBuffer(detStaging);

                    RHIBarrierDesc detBarrier{};
                    detBarrier.texture     = m_detailNoiseTexture;
                    detBarrier.stateBefore = RHIResourceState::Common;
                    detBarrier.stateAfter  = RHIResourceState::CopyDst;
                    cmdList->ResourceBarrier(&detBarrier, 1);

                    cmdList->CopyBufferToTexture(detStaging, 0, kDetAlignedRowPitch,
                                                 m_detailNoiseTexture, 0, 0);

                    detBarrier.stateBefore = RHIResourceState::CopyDst;
                    detBarrier.stateAfter  = RHIResourceState::ShaderResource;
                    cmdList->ResourceBarrier(&detBarrier, 1);

                    m_stagingBuffers.push_back(detStaging);
                    UZ_LOG(Renderer, Info, "CloudPass: detail noise loaded (32^3 RGBA8, SRV={})",
                           m_detailNoiseSrvIndex);
                }
            }
        }
    } else if (!detailR8.empty()) {
        UZ_LOG(Renderer, Warning, "CloudPass: shape_detail.bin size mismatch (expected {}, got {})",
               kDetailExpSize, detailR8.size());
    }

    m_hasNoiseTextures = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Cloud CB layout — matches cbuffer CloudCB : register(b2) in cloud_raymarch_cs.hlsl
// ─────────────────────────────────────────────────────────────────────────────

struct alignas(16) CloudCB {
    float invViewProj[16];     // 64 bytes (offset 0)
    float cameraPos[3];        // 12 bytes (offset 64)
    float time;                // 4 bytes  (offset 76)
    float sunDir[3];           // 12 bytes (offset 80)
    float coverage;            // 4 bytes  (offset 92)
    float sunColor[3];         // 12 bytes (offset 96)
    float density;             // 4 bytes  (offset 108)
    float ambientColor[3];     // 12 bytes (offset 112)
    float extinction;          // 4 bytes  (offset 124)
    float scatteringAlbedo;    // 4 bytes  (offset 128)
    float phaseG;              // 4 bytes  (offset 132)
    float phaseG2;             // 4 bytes  (offset 136)
    float phaseBlend;          // 4 bytes  (offset 140)
    float layerBottomKm;       // 4 bytes  (offset 144)
    float layerHeightKm;       // 4 bytes  (offset 148)
    float windVelocity[3];     // 12 bytes (offset 152)
    float detailWindScale;     // 4 bytes  (offset 164)
    float ambientMultiplier;   // 4 bytes  (offset 168)
    uint32_t primarySteps;     // 4 bytes  (offset 172)
    uint32_t lightSteps;       // 4 bytes  (offset 176)
    uint32_t shapeNoiseSrv;    // 4 bytes  (offset 180)
    float globalCoverage;      // 4 bytes  (offset 184)
    // ── Additional tracing/quality parameters ────────────────────────
    float cloudType;                                          // 4 bytes  (offset 188)
    float tracingMaxDistanceKm;                               // 4 bytes  (offset 192)
    float viewSampleCountScale;                               // 4 bytes  (offset 196)
    float reflectionViewSampleCountScale;                     // 4 bytes  (offset 200)
    float shadowViewSampleCountScale;                         // 4 bytes  (offset 204)
    float shadowReflectionViewSampleCountScale;               // 4 bytes  (offset 208)
    float shadowTracingDistance;                               // 4 bytes  (offset 212)
    float stopTracingTransmittanceThreshold;                  // 4 bytes  (offset 216)
    float aerialPerspectiveRayleighScatteringStartDistance;    // 4 bytes  (offset 220)
    float aerialPerspectiveRayleighScatteringFadeDistance;     // 4 bytes  (offset 224)
    float aerialPerspectiveMieScatteringStartDistance;         // 4 bytes  (offset 228)
    float aerialPerspectiveMieScatteringFadeDistance;          // 4 bytes  (offset 232)
    float skyLightCloudBottomOcclusion;                       // 4 bytes  (offset 236)
    uint32_t tracingMaxDistanceMode;                          // 4 bytes  (offset 240)
    float tracingStartMaxDistance;                             // 4 bytes  (offset 244)
    float tracingStartDistanceFromCamera;                     // 4 bytes  (offset 248)
    uint32_t detailNoiseSrv;   // 4 bytes  (offset 252) — detail noise 3D texture SRV
    // ── Shader CVar parameters (Phase 3 — zero hardcoded noise constants) ──
    float noiseXYScale;        // 4 bytes  (offset 256) — CVar r.Cloud.NoiseXYScale (default 0.12)
    float noiseZScale;         // 4 bytes  (offset 260) — CVar r.Cloud.NoiseZScale (default 3.0)
    uint32_t shapeOctaves;     // 4 bytes  (offset 264) — CVar r.Cloud.ShapeOctaves (default 4)
    float fbmDecay;            // 4 bytes  (offset 268) — CVar r.Cloud.FBMDecay (default 0.5)
    float fbmLacunarity;       // 4 bytes  (offset 272) — CVar r.Cloud.FBMLacunarity (default 2.0)
    float detailErosion;       // 4 bytes  (offset 276) — CVar r.Cloud.DetailErosion (default 0.35)
    float aerialPerspDensity;  // 4 bytes  (offset 280) — CVar r.Cloud.AerialPerspectiveDensity (default 0.04)
    float densityScale;        // 4 bytes  (offset 284) — CVar r.Cloud.DensityScale (default 25.0)
    float _pad3[56] = {};      // 224 bytes padding to reach 512 (DX12 CBV must be 256-byte aligned)
};

static_assert(sizeof(CloudCB) == 512, "CloudCB must be multiple of 256 bytes for DX12 CBV alignment");

// ─────────────────────────────────────────────────────────────────────────────
// Execute
// ─────────────────────────────────────────────────────────────────────────────

void CloudPass::Execute(IRHICommandList* cmdList, CBRingAllocator& cbRing,
                        const CloudPassInput& input) {
    UZ_PROFILE_SCOPE("CloudPass::Execute");

    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "CloudPass::Execute called before Init()");
        return;
    }

    if (m_halfWidth == 0 || m_halfHeight == 0) {
        UZ_LOG(Renderer, Error, "CloudPass::Execute: zero-size output");
        return;
    }

    if (!EnsureRaymarchPipeline()) return;

    // ── Load noise textures on first execution (lazy init) ──
    if (!m_hasNoiseTextures) {
        LoadNoiseTextures(cmdList, m_vfs);
    }

    // ── Transition output texture to UAV for writing ──
    // First frame: Common -> UAV. Subsequent frames: ShaderResource -> UAV
    // (because Execute ends with UAV -> ShaderResource for the composite pass).
    {
        RHIBarrierDesc toUav{};
        toUav.texture     = m_outputTexture;
        toUav.stateBefore = m_initialBarriersDone
            ? RHIResourceState::ShaderResource
            : RHIResourceState::Common;
        toUav.stateAfter  = RHIResourceState::UnorderedAccess;
        cmdList->ResourceBarrier(&toUav, 1);

        if (!m_initialBarriersDone) {
            m_initialBarriersDone = true;
            UZ_LOG(Renderer, Info, "CloudPass: initial barrier — Common -> UAV for output");
        }
    }

    // ── Fill constant buffer ──
    CloudCB cb{};
    std::memcpy(cb.invViewProj, input.invViewProj, 64);
    cb.cameraPos[0]    = input.cameraPos[0];
    cb.cameraPos[1]    = input.cameraPos[1];
    cb.cameraPos[2]    = input.cameraPos[2];
    cb.time            = input.time * 0.1f;
    cb.sunDir[0]       = input.sunDir[0];
    cb.sunDir[1]       = input.sunDir[1];
    cb.sunDir[2]       = input.sunDir[2];
    cb.coverage        = input.coverage;
    cb.sunColor[0]     = input.sunColor[0];
    cb.sunColor[1]     = input.sunColor[1];
    cb.sunColor[2]     = input.sunColor[2];
    cb.density         = input.density;
    cb.ambientColor[0] = input.ambientColor[0];
    cb.ambientColor[1] = input.ambientColor[1];
    cb.ambientColor[2] = input.ambientColor[2];
    cb.extinction      = input.extinction;
    cb.scatteringAlbedo = input.scatteringAlbedo;
    cb.phaseG          = input.phaseG;
    cb.phaseG2         = input.phaseG2;
    cb.phaseBlend      = input.phaseBlend;
    cb.layerBottomKm   = input.layerBottomKm;
    cb.layerHeightKm   = input.layerHeightKm;
    cb.windVelocity[0] = input.windVelocity[0];
    cb.windVelocity[1] = input.windVelocity[1];
    cb.windVelocity[2] = input.windVelocity[2];
    cb.detailWindScale = input.detailWindScale;
    cb.ambientMultiplier = input.ambientMultiplier;
    cb.primarySteps    = input.primarySteps;
    cb.lightSteps      = input.lightSteps;
    cb.shapeNoiseSrv   = m_shapeNoiseSrvIndex;
    cb.globalCoverage  = input.globalCoverage;
    cb.cloudType       = input.cloudType;
    cb.tracingMaxDistanceKm = input.tracingMaxDistanceKm;
    cb.viewSampleCountScale = input.viewSampleCountScale;
    cb.reflectionViewSampleCountScale = input.reflectionViewSampleCountScale;
    cb.shadowViewSampleCountScale = input.shadowViewSampleCountScale;
    cb.shadowReflectionViewSampleCountScale = input.shadowReflectionViewSampleCountScale;
    cb.shadowTracingDistance = input.shadowTracingDistance;
    cb.stopTracingTransmittanceThreshold = input.stopTracingTransmittanceThreshold;
    cb.aerialPerspectiveRayleighScatteringStartDistance = input.aerialPerspectiveRayleighScatteringStartDistance;
    cb.aerialPerspectiveRayleighScatteringFadeDistance = input.aerialPerspectiveRayleighScatteringFadeDistance;
    cb.aerialPerspectiveMieScatteringStartDistance = input.aerialPerspectiveMieScatteringStartDistance;
    cb.aerialPerspectiveMieScatteringFadeDistance = input.aerialPerspectiveMieScatteringFadeDistance;
    cb.skyLightCloudBottomOcclusion = input.skyLightCloudBottomOcclusion;
    cb.tracingMaxDistanceMode = input.tracingMaxDistanceMode;
    cb.tracingStartMaxDistance = input.tracingStartMaxDistance;
    cb.tracingStartDistanceFromCamera = input.tracingStartDistanceFromCamera;
    cb.detailNoiseSrv  = m_detailNoiseSrvIndex;
    cb.noiseXYScale       = cv_CloudNoiseXY->value;
    cb.noiseZScale        = cv_CloudNoiseZ->value;
    cb.shapeOctaves       = static_cast<uint32_t>(cv_CloudOctaves->value);
    cb.fbmDecay           = cv_CloudFBMDecay->value;
    cb.fbmLacunarity      = cv_CloudFBMLac->value;
    cb.detailErosion      = cv_CloudErosion->value;
    cb.aerialPerspDensity = cv_CloudAerialDens->value;
    cb.densityScale       = cv_CloudDensScale->value;

    auto cbAlloc = cbRing.Allocate(sizeof(CloudCB));
    std::memcpy(cbAlloc.cpuPtr, &cb, sizeof(CloudCB));

    // ── Set pipeline and root parameters ──
    cmdList->SetPipeline(m_raymarchPipeline);

    // Root constants at slot 0 (b0): {outputUavIndex, depthSrvIndex, halfWidth, halfHeight}
    // Packed as uint32 to match compute root signature expectations.
    uint32_t rootConst[4] = {
        m_outputUavIndex,
        input.depthSrv,
        m_halfWidth,
        m_halfHeight
    };

    static bool loggedOnce = false;
    if (!loggedOnce) {
        UZ_LOG(Renderer, Info,
               "CloudPass: root constants — outputUAV={}, depthSRV={}, size={}x{}",
               rootConst[0], rootConst[1], rootConst[2], rootConst[3]);
        UZ_LOG(Renderer, Info,
               "CloudPass: CB — cameraKm=({:.6f},{:.6f},{:.6f}), sunDir=({:.3f},{:.3f},{:.3f}), "
               "coverage={:.2f}, density={:.2f}, ext={:.4f}, layerBot={:.1f}, layerH={:.1f}",
               cb.cameraPos[0], cb.cameraPos[1], cb.cameraPos[2],
               cb.sunDir[0], cb.sunDir[1], cb.sunDir[2],
               cb.coverage, cb.density, cb.extinction,
               cb.layerBottomKm, cb.layerHeightKm);
        UZ_LOG(Renderer, Info,
               "CloudPass: CB — sunColor=({:.3f},{:.3f},{:.3f}), ambientColor=({:.3f},{:.3f},{:.3f}), "
               "phaseG={:.2f}, phaseG2={:.2f}, phaseBlend={:.2f}, scatAlbedo={:.2f}, ambMul={:.2f}",
               cb.sunColor[0], cb.sunColor[1], cb.sunColor[2],
               cb.ambientColor[0], cb.ambientColor[1], cb.ambientColor[2],
               cb.phaseG, cb.phaseG2, cb.phaseBlend,
               cb.scatteringAlbedo, cb.ambientMultiplier);
        loggedOnce = true;
    }

    cmdList->SetRootConstants(0, rootConst, 4);

    // CBV at slot 2 (b2): CloudCB
    cmdList->SetRootCBV(2, cbAlloc.bufferHandle, cbAlloc.offsetBytes,
                        cbAlloc.cpuPtr, sizeof(CloudCB));

    // UAV descriptor table at slot 4
    cmdList->SetDescriptorTable(4, m_outputUavIndex);

    // ── Dispatch ──
    constexpr uint32_t kThreadGroupSize = 8;
    uint32_t groupsX = (m_halfWidth  + kThreadGroupSize - 1) / kThreadGroupSize;
    uint32_t groupsY = (m_halfHeight + kThreadGroupSize - 1) / kThreadGroupSize;
    cmdList->Dispatch(groupsX, groupsY, 1);

    // ── Barrier: UAV -> ShaderResource for composite pass to read ──
    RHIBarrierDesc uavToSrv{};
    uavToSrv.texture     = m_outputTexture;
    uavToSrv.stateBefore = RHIResourceState::UnorderedAccess;
    uavToSrv.stateAfter  = RHIResourceState::ShaderResource;
    cmdList->ResourceBarrier(&uavToSrv, 1);

    // Next frame will need SRV -> UAV transition before writing again
    // (handled at the start of next Execute by transitioning back)
}

} // namespace uz
