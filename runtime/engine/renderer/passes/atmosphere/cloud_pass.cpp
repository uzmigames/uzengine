/// UzEngine Cloud Pass — Implementation.
///
/// Creates half-res RGBA16F cloud output, compiles the ray-march compute shader,
/// and dispatches GPU work each frame. The cloud output is composited over the
/// HDR scene by the Cloud_Composite render-graph pass.
///
/// @ref Schneider & Vos 2015 "Real-Time Volumetric Cloudscapes of Horizon Zero Dawn"
/// @ref Hillaire 2020 "A Scalable and Production Ready Sky and Atmosphere Rendering Technique"

#include "cloud_pass.h"

#include "engine/rhi/rhi.h"
#include "cb_ring_allocator.h"
#include "render_graph_builder.h"
#include "render_graph_pass.h"
#include "render_graph_states.h"
#include "pipeline_library.h"
#include "root_slots.h"
#include "engine/asset/vfs.h"
#include "shader_manager.h"
#include "engine_shaders.h"
#include "log.h"
#include "profiler.h"
#include "cvar.h"
#include "engine/math/mat4.h"

#include "paths.h"

#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <algorithm>

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
static auto* cv_CloudProcedural = UZ_CVAR_BOOL("r.Cloud.ProceduralNoise", false, "Compare legacy procedural cloud shape");
static auto* cv_CloudTemporalUpscale = UZ_CVAR_BOOL("r.Clouds.TemporalUpscale", false, "Opt-in quarter-resolution Bayer cloud temporal upscale");

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
    m_fullWidth = width;
    m_fullHeight = height;
    OnResize(width, height);
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
}

void CloudPass::Shutdown() {
    if (!m_initialized) return;

    DestroyOutputTexture();
    DestroyTemporalTextures();

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

    if (m_stbnTexture.IsValid()) {
        m_device->DestroyTexture(m_stbnTexture);
        m_stbnTexture = RHITextureHandle::Invalid();
    }
    m_stbnSrvIndex = UINT32_MAX;

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
    if (m_upscaleRaymarchPipeline.IsValid()) {
        m_device->DestroyPipeline(m_upscaleRaymarchPipeline);
        m_upscaleRaymarchPipeline = RHIPipelineHandle::Invalid();
    }
    if (m_upscaleRaymarchCS.IsValid()) {
        m_device->DestroyShader(m_upscaleRaymarchCS);
        m_upscaleRaymarchCS = RHIShaderHandle::Invalid();
    }
    if (m_temporalPipeline.IsValid()) {
        m_device->DestroyPipeline(m_temporalPipeline);
        m_temporalPipeline = RHIPipelineHandle::Invalid();
    }
    if (m_temporalCS.IsValid()) {
        m_device->DestroyShader(m_temporalCS);
        m_temporalCS = RHIShaderHandle::Invalid();
    }
    m_temporalUpscale = false;

    m_device      = nullptr;
    m_shaderMgr   = nullptr;
    m_initialized = false;
    m_hasNoiseTextures = false;
    m_noiseLoadAttempted = false;

    UZ_LOG(Renderer, Info, "CloudPass shut down");
}

void CloudPass::OnResize(uint32_t width, uint32_t height) {
    const bool temporalUpscale = cv_CloudTemporalUpscale->value != 0;
    const uint32_t divisor = temporalUpscale ? 4 : 2;
    uint32_t halfW = (width + divisor - 1) / divisor;
    uint32_t halfH = (height + divisor - 1) / divisor;

    if (halfW == m_halfWidth && halfH == m_halfHeight
        && width == m_fullWidth && height == m_fullHeight
        && temporalUpscale == m_temporalUpscale) return;

    DestroyOutputTexture();
    DestroyTemporalTextures();
    m_fullWidth = width;
    m_fullHeight = height;
    m_temporalUpscale = temporalUpscale;
    CreateOutputTexture(halfW, halfH);
    if (temporalUpscale) CreateTemporalTextures(width, height);
}

bool CloudPass::CreateTemporalTextures(uint32_t width, uint32_t height) {
    RHITextureDesc desc{};
    desc.width = m_halfWidth;
    desc.height = m_halfHeight;
    desc.format = RHIFormat::R32Float;
    desc.usage = RHITextureUsage::ShaderResource | RHITextureUsage::UnorderedAccess;
    desc.debugName = "Cloud_FrontDepth";
    m_frontDepthTexture = m_device->CreateTexture(desc);
    if (!m_frontDepthTexture.IsValid()) return false;
    m_frontDepthSrv = m_device->GetTextureSRVIndex(m_frontDepthTexture);
    m_frontDepthUav = m_device->GetTextureUAVIndex(m_frontDepthTexture);
    desc.width = width;
    desc.height = height;
    desc.format = RHIFormat::RGBA16Float;
    for (uint32_t i = 0; i < 2; ++i) {
        desc.debugName = i == 0 ? "Cloud_History0" : "Cloud_History1";
        m_historyTexture[i] = m_device->CreateTexture(desc);
        if (!m_historyTexture[i].IsValid()) return false;
        m_historySrv[i] = m_device->GetTextureSRVIndex(m_historyTexture[i]);
        m_historyUav[i] = m_device->GetTextureUAVIndex(m_historyTexture[i]);
    }
    UZ_LOG(Renderer, Info, "CloudPass: temporal upscale enabled, quarter={}x{}, history={}x{}",
           m_halfWidth, m_halfHeight, width, height);
    return true;
}

void CloudPass::DestroyTemporalTextures() {
    if (m_frontDepthTexture.IsValid()) m_device->DestroyTexture(m_frontDepthTexture);
    m_frontDepthTexture = RHITextureHandle::Invalid();
    m_frontDepthSrv = m_frontDepthUav = UINT32_MAX;
    for (uint32_t i = 0; i < 2; ++i) {
        if (m_historyTexture[i].IsValid()) m_device->DestroyTexture(m_historyTexture[i]);
        m_historyTexture[i] = RHITextureHandle::Invalid();
        m_historySrv[i] = m_historyUav[i] = UINT32_MAX;
    }
    m_historyValid = false;
    m_historyWrite = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline Creation
// ─────────────────────────────────────────────────────────────────────────────

bool CloudPass::EnsureRaymarchPipeline() {
    auto& pipeline = m_temporalUpscale ? m_upscaleRaymarchPipeline : m_raymarchPipeline;
    auto& shader = m_temporalUpscale ? m_upscaleRaymarchCS : m_raymarchCS;
    if (pipeline.IsValid()) return true;

    auto bytecode = m_shaderMgr->GetBytecode(m_temporalUpscale
        ? EngineShader::CloudRaymarchUpscaleCS : EngineShader::CloudRaymarchCS);
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

    shader = m_device->CreateShader(shaderDesc);
    if (!shader.IsValid()) {
        UZ_LOG(Renderer, Error, "CloudPass: failed to create raymarch shader");
        return false;
    }

    RHIComputePipelineDesc pipeDesc{};
    pipeDesc.computeShader = shader;
    pipeDesc.debugName     = "Cloud_Raymarch";

    pipeline = m_device->CreateComputePipeline(pipeDesc);
    if (!pipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "CloudPass: failed to create raymarch pipeline");
        return false;
    }

    UZ_LOG(Renderer, Info, "CloudPass: raymarch pipeline created (handle={})", pipeline.index);
    return true;
}

bool CloudPass::EnsureTemporalPipeline() {
    if (m_temporalPipeline.IsValid()) return true;
    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::CloudTemporalCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "CloudPass: temporal shader compilation failed");
        return false;
    }
    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage = RHIShaderStage::Compute;
    shaderDesc.entryPoint = "main";
    shaderDesc.debugName = "cloud_temporal_cs";
    m_temporalCS = m_device->CreateShader(shaderDesc);
    if (!m_temporalCS.IsValid()) return false;
    RHIComputePipelineDesc pipeDesc{};
    pipeDesc.computeShader = m_temporalCS;
    pipeDesc.debugName = "Cloud_Temporal";
    m_temporalPipeline = m_device->CreateComputePipeline(pipeDesc);
    UZ_LOG(Renderer, Info, "CloudPass: temporal shader bytecode={} bytes, pipeline={}",
           bytecode.size(), m_temporalPipeline.index);
    return m_temporalPipeline.IsValid();
}

// ─────────────────────────────────────────────────────────────────────────────
// Noise Texture Loading
// ─────────────────────────────────────────────────────────────────────────────

/// Reads an engine-owned cloud asset ("atmosphere/shape.bin"): the project's VFS first (a project may ship its
/// own), then the engine's content next to the exe (bin/content/atmosphere, installed by build.zig from
/// runtime/content/atmosphere), then the working directory. Each source is checked for existence before it is
/// read, so a missing file costs no warning. Returns an empty vector when no source has it.
static std::vector<uint8_t> ReadCloudAsset(IVFS* vfs, const std::string& relativePath, std::string& sourceOut) {
    if (vfs && vfs->Exists(relativePath)) {
        sourceOut = "project VFS";
        return vfs->Read(relativePath);
    }
    const std::filesystem::path candidates[] = {
        ResolveEngineResource("content/" + relativePath),
        std::filesystem::current_path() / relativePath,
    };
    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(candidate, ec)) continue;
        std::ifstream file(candidate, std::ios::binary | std::ios::ate);
        if (!file.is_open()) continue;
        const auto size = file.tellg();
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(bytes.data()), size);
        sourceOut = candidate.string();
        return bytes;
    }
    return {};
}

void CloudPass::LoadNoiseTextures(IRHICommandList* cmdList, rg::RgStateRegistry* states, IVFS* vfs) {
    if (m_noiseLoadAttempted || !m_initialized || !cmdList) return;
    m_noiseLoadAttempted = true;

    // Scalar R8 source data stays scalar. Build filtered mips on the CPU
    // during this one-time upload; the GPU never bakes noise or waits idle.
    auto load = [&](const char* path, const char* name, uint32_t size, uint32_t depth,
                    bool mipmapped, RHITextureHandle& texture, uint32_t& srv) {
        std::string source;
        auto data = ReadCloudAsset(vfs, path, source);
        if (data.size() != uint64_t(size) * size * depth) {
            UZ_LOG(Renderer, Error, "CloudPass: {} missing or wrong size ({} bytes)", path, data.size());
            return;
        }
        struct Mip { uint64_t offset; uint32_t pitch; };
        std::vector<Mip> mips;
        std::vector<uint8_t> staged;
        uint32_t width = size, slices = depth;
        for (;;) {
            const uint64_t offset = (staged.size() + 511ull) & ~511ull;
            const uint32_t pitch = (width + 255u) & ~255u;
            staged.resize(offset + uint64_t(pitch) * width * slices, 0);
            for (uint32_t z = 0; z < slices; ++z)
                for (uint32_t y = 0; y < width; ++y)
                    std::memcpy(staged.data() + offset + uint64_t(z * width + y) * pitch,
                                data.data() + uint64_t(z * width + y) * width, width);
            mips.push_back({offset, pitch});
            if (!mipmapped || width == 1) break;
            const uint32_t next = width / 2, nextDepth = slices / 2;
            std::vector<uint8_t> filtered(uint64_t(next) * next * nextDepth);
            for (uint32_t z = 0; z < nextDepth; ++z)
                for (uint32_t y = 0; y < next; ++y)
                    for (uint32_t x = 0; x < next; ++x) {
                        uint32_t sum = 0;
                        for (uint32_t dz = 0; dz < 2; ++dz)
                            for (uint32_t dy = 0; dy < 2; ++dy)
                                for (uint32_t dx = 0; dx < 2; ++dx)
                                    sum += data[((z * 2 + dz) * width + y * 2 + dy) * width + x * 2 + dx];
                        filtered[(z * next + y) * next + x] = uint8_t((sum + 4) / 8);
                    }
            data = std::move(filtered);
            width = next;
            slices = nextDepth;
        }
        RHITextureDesc desc{};
        desc.width = size;
        desc.height = size;
        desc.depthOrArraySize = depth;
        desc.mipLevels = static_cast<uint16_t>(mips.size());
        desc.format = RHIFormat::R8Unorm;
        desc.usage = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;
        desc.is3D = true;
        desc.debugName = name;
        texture = m_device->CreateTexture(desc);
        if (!texture.IsValid()) return;
        srv = m_device->GetTextureSRVIndex(texture);
        RHIBufferDesc upload{};
        upload.sizeBytes = staged.size();
        upload.usage = RHIBufferUsage::CopySrc;
        upload.memory = RHIMemoryType::Upload;
        upload.debugName = name;
        auto buffer = m_device->CreateBuffer(upload);
        void* mapped = buffer.IsValid() ? m_device->MapBuffer(buffer) : nullptr;
        if (srv == UINT32_MAX || !mapped) {
            if (mapped) m_device->UnmapBuffer(buffer);
            if (buffer.IsValid()) m_device->DestroyBuffer(buffer);
            m_device->DestroyTexture(texture);
            texture = RHITextureHandle::Invalid();
            srv = UINT32_MAX;
            UZ_LOG(Renderer, Error, "CloudPass: {} upload allocation failed", name);
            return;
        }
        std::memcpy(mapped, staged.data(), staged.size());
        m_device->UnmapBuffer(buffer);
        states->Transition(cmdList, texture, RHIResourceState::CopyDst);
        for (uint32_t mip = 0; mip < mips.size(); ++mip)
            cmdList->CopyBufferToTexture(buffer, mips[mip].offset, mips[mip].pitch, texture, mip, 0);
        states->Transition(cmdList, texture, RHIResourceState::ShaderResource);
        m_stagingBuffers.push_back(buffer);
        UZ_LOG(Renderer, Info, "CloudPass: {} loaded ({}x{}x{} R8, {} mips, SRV={}) from {}",
               name, size, size, depth, mips.size(), srv, source);
    };
    load("atmosphere/shape.bin", "shape noise", 128, 128, true, m_shapeNoiseTexture, m_shapeNoiseSrvIndex);
    load("atmosphere/shape_detail.bin", "detail noise", 32, 32, true, m_detailNoiseTexture, m_detailNoiseSrvIndex);
    load("atmosphere/stbn128.bin", "STBN", 128, 64, false, m_stbnTexture, m_stbnSrvIndex);
    m_hasNoiseTextures = m_shapeNoiseTexture.IsValid() && m_detailNoiseTexture.IsValid() && m_stbnTexture.IsValid();
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
    float minStepSize;
    float maxStepSize;
    float perspectiveStepScale;
    uint32_t stbnSrvIndex;
    uint32_t frameIndex;
    uint32_t proceduralNoise;
    float noiseXYScale;
    float noiseZScale;
    uint32_t shapeOctaves;
    float fbmDecay;
    float fbmLacunarity;
    float detailErosion;
    float densityScale;
    float jitterScale;
    float powderScale;
    float powderExponent;
    uint32_t multiScatterOctaves;
    uint32_t temporalUpscale;
    uint32_t fullWidth;
    uint32_t fullHeight;
    uint32_t frontDepthUav;
    float padding[43];
};

static_assert(sizeof(CloudCB) == 512, "CloudCB must be 512 bytes for CBV alignment");
// The HLSL copy declares g_windVelocity as three scalars: a float3 at 152 would move to 160 and
// shift the tail by 8 bytes (block B.4).
static_assert(offsetof(CloudCB, windVelocity) == 152);
static_assert(offsetof(CloudCB, detailNoiseSrv) == 252);
static_assert(offsetof(CloudCB, primarySteps) == 172);
static_assert(offsetof(CloudCB, lightSteps) == 176);
static_assert(offsetof(CloudCB, shapeNoiseSrv) == 180);
static_assert(offsetof(CloudCB, minStepSize) == 256);
static_assert(offsetof(CloudCB, stbnSrvIndex) == 268);
static_assert(offsetof(CloudCB, frameIndex) == 272);
static_assert(offsetof(CloudCB, shapeOctaves) == 288);
static_assert(offsetof(CloudCB, powderScale) == 312);
static_assert(offsetof(CloudCB, powderExponent) == 316);
static_assert(offsetof(CloudCB, multiScatterOctaves) == 320);
static_assert(offsetof(CloudCB, temporalUpscale) == 324);
static_assert(offsetof(CloudCB, frontDepthUav) == 336);

struct alignas(16) CloudTemporalCB {
    float invViewProj[16];
    float previousViewProj[16];
    float cameraPos[3];
    uint32_t fullWidth;
    uint32_t fullHeight;
    uint32_t quarterWidth;
    uint32_t quarterHeight;
    uint32_t frameIndex;
    uint32_t historyValid;
    uint32_t currentSrv;
    uint32_t historySrv;
    uint32_t outputUav;
    uint32_t frontDepthSrv;
    uint32_t depthSrv;
    uint32_t velocitySrv;
    float varianceGamma;
    float padding[80];
};
static_assert(sizeof(CloudTemporalCB) == 512);
static_assert(offsetof(CloudTemporalCB, historyValid) == 160);
static_assert(offsetof(CloudTemporalCB, varianceGamma) == 188);

// ─────────────────────────────────────────────────────────────────────────────
// RegisterPasses
// ─────────────────────────────────────────────────────────────────────────────

void CloudPass::RegisterPasses(rg::RgBuilder& rgBuilder, rg::RgStateRegistry* states,
    IRHICommandList* frameList, rg::RgResourceId hdrRes, rg::RgResourceId depthRes,
    const CBAllocation& perFrameAlloc, CBRingAllocator& cbRing, PipelineLibrary* pipelines,
    bool bindless, uint32_t renderWidth, uint32_t renderHeight, const CloudPassInput& input) {
    UZ_PROFILE_SCOPE("CloudPass::RegisterPasses");
    if (!m_initialized || !bindless) return;
    if (m_temporalUpscale != (cv_CloudTemporalUpscale->value != 0)) {
        // Changing the opt-in mode retires resources used by earlier frames.
        // This is not a per-frame synchronization point.
        m_device->WaitIdle();
        OnResize(renderWidth, renderHeight);
    }
    if (!EnsureRaymarchPipeline()) return;
    if (m_temporalUpscale && (!m_frontDepthTexture.IsValid()
        || !m_historyTexture[0].IsValid() || !m_historyTexture[1].IsValid()
        || !EnsureTemporalPipeline())) return;

    // Registration occurs on the open frame list, before graph execution.
    if (!m_noiseLoadAttempted) LoadNoiseTextures(frameList, states, m_vfs);

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
    cb.minStepSize = std::max(0.001f, input.minStepSize);
    cb.maxStepSize = std::max(cb.minStepSize, input.maxStepSize);
    cb.perspectiveStepScale = std::max(1.0f, input.perspectiveStepScale);
    cb.stbnSrvIndex = m_stbnSrvIndex;
    cb.frameIndex = input.frameIndex;
    cb.proceduralNoise = cv_CloudProcedural->value != 0;
    cb.noiseXYScale = cv_CloudNoiseXY->value;
    cb.noiseZScale = cv_CloudNoiseZ->value;
    cb.shapeOctaves = static_cast<uint32_t>(cv_CloudOctaves->value);
    cb.fbmDecay = cv_CloudFBMDecay->value;
    cb.fbmLacunarity = cv_CloudFBMLac->value;
    cb.detailErosion = cv_CloudErosion->value;
    cb.densityScale = cv_CloudDensScale->value;
    cb.jitterScale = input.jitterScale;
    cb.powderScale = std::clamp(input.powderScale, 0.0f, 1.0f);
    cb.powderExponent = std::max(0.0f, input.powderExponent);
    cb.multiScatterOctaves = std::clamp(input.multiScatterOctaves, 1u, 8u);
    if (m_temporalUpscale) {
        cb.temporalUpscale = 1;
        cb.fullWidth = renderWidth;
        cb.fullHeight = renderHeight;
        cb.frontDepthUav = m_frontDepthUav;
    }

    auto cbAlloc = cbRing.Allocate(sizeof(CloudCB));
    std::memcpy(cbAlloc.cpuPtr, &cb, sizeof(CloudCB));

    // ── Set pipeline and root parameters ──
    // Root constants at slot 0 (b0): {outputUavIndex, depthSrvIndex, halfWidth, halfHeight}
    // Packed as uint32 to match compute root signature expectations.
    uint32_t rootConst[12] = {
        m_outputUavIndex,
        input.depthSrv,
        m_halfWidth,
        m_halfHeight,
        0, 0, 0, 0, 0, 0, 0, 0
    };

    static bool loggedOnce = false;
    if (!loggedOnce) {
        UZ_LOG(Renderer, Info,
               "CloudPass: adaptive CB — bytes={}, minStep={}, maxStep={}, perspectiveScale={}, STBN={}, shape={}, iterations=256",
               sizeof(CloudCB), cb.minStepSize, cb.maxStepSize, cb.perspectiveStepScale, cb.stbnSrvIndex, cb.shapeNoiseSrv);
        UZ_LOG(Renderer, Info,
               "CloudPass: lighting — multiScatterOctaves={}, powderScale={}, powderExponent={}, skyLightScale={}",
               cb.multiScatterOctaves, cb.powderScale, cb.powderExponent, cb.ambientMultiplier);
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

    static_assert(sizeof(rootConst) / sizeof(rootConst[0]) <= 12);
    const auto cloudOut = rgBuilder.ImportTrackedTexture("Cloud_Output", m_outputTexture);
    const auto frontDepth = m_temporalUpscale
        ? rgBuilder.ImportTrackedTexture("Cloud_FrontDepth", m_frontDepthTexture) : rg::RgResourceId{};
    const auto shapeRes = m_shapeNoiseTexture.IsValid()
        ? rgBuilder.ImportTrackedTexture("Cloud_ShapeNoise", m_shapeNoiseTexture) : rg::RgResourceId{};
    const auto detailRes = m_detailNoiseTexture.IsValid()
        ? rgBuilder.ImportTrackedTexture("Cloud_DetailNoise", m_detailNoiseTexture) : rg::RgResourceId{};
    const auto stbnRes = m_stbnTexture.IsValid()
        ? rgBuilder.ImportTrackedTexture("Cloud_STBN", m_stbnTexture) : rg::RgResourceId{};
    const auto raymarchPipeline = m_temporalUpscale ? m_upscaleRaymarchPipeline : m_raymarchPipeline;

    rgBuilder.AddPass("Cloud_Raymarch", [=, this](rg::RgPassBuilder& pb) {
        pb.Write(cloudOut, RHIResourceState::UnorderedAccess);
        if (frontDepth.IsValid()) pb.Write(frontDepth, RHIResourceState::UnorderedAccess);
        pb.Read(depthRes, RHIResourceState::ShaderResource);
        if (shapeRes.IsValid()) pb.Read(shapeRes, RHIResourceState::ShaderResource);
        if (detailRes.IsValid()) pb.Read(detailRes, RHIResourceState::ShaderResource);
        if (stbnRes.IsValid()) pb.Read(stbnRes, RHIResourceState::ShaderResource);
        pb.SetExecutionCallback([=, this](IRHICommandList* cmd, const rg::RgPassResources&) {
            UZ_PROFILE_SCOPE("Cloud::Raymarch");
            cmd->SetPipeline(raymarchPipeline);
            cmd->SetRootConstants(kSlotRootConstants, rootConst, 12);
            cmd->SetRootCBV(kSlotPerObject, cbAlloc.gpuAddr);
            cmd->Dispatch((m_halfWidth + 7) / 8, (m_halfHeight + 7) / 8, 1);
        });
    });

    auto compositeInput = cloudOut;
    uint32_t compositeSrv = m_outputSrvIndex;
    if (m_temporalUpscale) {
        const uint32_t write = m_historyWrite;
        const uint32_t read = write ^ 1u;
        const bool validHistory = m_historyValid && input.frameIndex == m_previousFrame + 1;
        const auto historyRead = rgBuilder.ImportTrackedTexture(
            read == 0 ? "Cloud_History0" : "Cloud_History1", m_historyTexture[read]);
        const auto historyWrite = rgBuilder.ImportTrackedTexture(
            write == 0 ? "Cloud_History0" : "Cloud_History1", m_historyTexture[write]);
        CloudTemporalCB temporal{};
        std::memcpy(temporal.invViewProj, input.invViewProj, 64);
        std::memcpy(temporal.previousViewProj, m_previousViewProj, 64);
        std::memcpy(temporal.cameraPos, input.cameraPos, 12);
        temporal.fullWidth = renderWidth;
        temporal.fullHeight = renderHeight;
        temporal.quarterWidth = m_halfWidth;
        temporal.quarterHeight = m_halfHeight;
        temporal.frameIndex = input.frameIndex;
        temporal.historyValid = validHistory;
        temporal.currentSrv = m_outputSrvIndex;
        temporal.historySrv = m_historySrv[read];
        temporal.outputUav = m_historyUav[write];
        temporal.frontDepthSrv = m_frontDepthSrv;
        temporal.depthSrv = input.depthSrv;
        temporal.velocitySrv = input.velocitySrv;
        temporal.varianceGamma = 1.25f;
        const auto temporalAlloc = cbRing.Allocate(sizeof(temporal));
        std::memcpy(temporalAlloc.cpuPtr, &temporal, sizeof(temporal));
        rgBuilder.AddPass("Cloud_Temporal", [=, this](rg::RgPassBuilder& pb) {
            pb.Read(cloudOut, RHIResourceState::ShaderResource);
            pb.Read(frontDepth, RHIResourceState::ShaderResource);
            pb.Read(depthRes, RHIResourceState::ShaderResource);
            pb.Read(input.velocityRes, RHIResourceState::ShaderResource);
            if (validHistory) pb.Read(historyRead, RHIResourceState::ShaderResource);
            pb.Write(historyWrite, RHIResourceState::UnorderedAccess);
            pb.SetExecutionCallback([=, this](IRHICommandList* cmd, const rg::RgPassResources&) {
                cmd->SetPipeline(m_temporalPipeline);
                const uint32_t temporalRC[12]{};
                cmd->SetRootConstants(kSlotRootConstants, temporalRC, 12);
                cmd->SetRootCBV(kSlotPerObject, temporalAlloc.gpuAddr);
                cmd->Dispatch((renderWidth + 7) / 8, (renderHeight + 7) / 8, 1);
            });
        });
        compositeInput = historyWrite;
        compositeSrv = m_historySrv[write];
        const Mat4 currentViewProj = Mat4(input.invViewProj).Inverse();
        std::memcpy(m_previousViewProj, &currentViewProj, 64);
        m_previousFrame = input.frameIndex;
        m_historyValid = true;
        m_historyWrite ^= 1u;
    }

    const auto compositePSO = pipelines->GetCloudComposite();
    if (compositePSO.IsValid()) {
        rgBuilder.AddPass("Cloud_Composite", [=, this](rg::RgPassBuilder& pb) {
            pb.Write(hdrRes, RHIResourceState::RenderTarget);
            pb.Read(compositeInput, RHIResourceState::ShaderResource);
            pb.Read(depthRes, RHIResourceState::ShaderResource);
            pb.SetExecutionCallback([=, this](IRHICommandList* cmd, const rg::RgPassResources& res) {
                UZ_PROFILE_SCOPE("Cloud::Composite");
                RHIRenderPassDesc passDesc{};
                passDesc.colorAttachmentCount = 1;
                passDesc.colorAttachments[0].texture = res.GetTexture(hdrRes);
                passDesc.colorAttachments[0].clear = false;
                passDesc.hasDepth = false;
                cmd->BeginRenderPass(passDesc);
                cmd->SetPipeline(compositePSO);
                const uint32_t compositeRC[4] = { compositeSrv, input.depthSrv, 0, 0 };
                cmd->SetRootConstants(kSlotRootConstants, compositeRC, 4);
                cmd->SetRootCBV(kSlotPerFrame, perFrameAlloc.gpuAddr);
                RHIViewport vp{};
                vp.width = static_cast<float>(renderWidth);
                vp.height = static_cast<float>(renderHeight);
                vp.maxDepth = 1.0f;
                cmd->SetViewport(vp);
                RHIScissorRect sc{};
                sc.right = static_cast<int32_t>(renderWidth);
                sc.bottom = static_cast<int32_t>(renderHeight);
                cmd->SetScissor(sc);
                cmd->Draw(3);
                cmd->EndRenderPass();
            });
        });
    }
}

} // namespace uz
