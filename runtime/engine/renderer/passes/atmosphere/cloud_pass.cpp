/// UzEngine Cloud Pass — Implementation.
///
/// Loads precomputed 3D noise textures from disk (avoiding GPU TDR from heavy
/// compute bakes), creates weather map and cloud output textures, compiles
/// 3 compute pipelines (weather, raymarch, temporal), and dispatches the full
/// volumetric cloud pipeline each frame. The cloud output (half-res RGBA16F)
/// is composited by a subsequent pass before tonemapping.
///
/// Noise textures sourced from three-geospatial (MIT license):
///   - cloud_shape.bin: 128^3 R8 Perlin-Worley combined
///   - cloud_shape_detail.bin: 32^3 R8 inverted Worley FBM
///
/// @ref CloudGlobalShaderParams
/// @ref MainCommon tracing loop
/// @ref Temporal reconstruction

#include "cloud_pass.h"

#include "engine/rhi/rhi.h"
#include "engine/renderer/core/cb_ring_allocator.h"
#include "shader_manager.h"
#include "engine_shaders.h"
#include "file_system.h"
#include "paths.h"
#include "log.h"
#include "profiler.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Root constant / CBV structures — must match HLSL cbuffer layouts exactly
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants for cloud_weather_cs.hlsl.
/// Matches `cbuffer WeatherParams : register(b0)` — 4 DWORDs.
struct CloudWeatherRootConstants {
    float    windOffsetX;
    float    windOffsetY;
    float    globalCoverage;
    uint32_t outputUavIndex;   ///< Bindless descriptor index for output UAV
};

static_assert(sizeof(CloudWeatherRootConstants) == 16,
    "CloudWeatherRootConstants must be 4 DWORDs (16 bytes)");

/// CBV data for cloud_raymarch_cs.hlsl at register(b2).
/// Matches `cbuffer CloudRaymarchCB : register(b2)` — 256-byte aligned for D3D12.
///
/// HLSL packing: float3+float packs into one 16-byte row. float4x4 = 64 bytes.
/// Total layout:
///   invViewProj       : 64 bytes (rows 0-3)
///   cameraPos + time  : 16 bytes (row 4)
///   sunDir + layerBot : 16 bytes (row 5)
///   sunColor + layerH : 16 bytes (row 6)
///   windVel + coverage: 16 bytes (row 7)
///   density, ext, sca, phaseG : 16 bytes (row 8)
///   phaseG2, phaseBlend, detailWS, ambient : 16 bytes (row 9)
///   priSteps, lightSteps, shapeSrv, detailSrv : 16 bytes (row 10)
///   weatherSrv, depthSrv, outW, outH : 16 bytes (row 11)
///   ambientColor + pad : 16 bytes (row 12)
///   = 208 bytes data, padded to 256 for CBV alignment
struct CloudRaymarchCBV {
    // Row 0-3: invViewProj (float4x4, 64 bytes)
    float invViewProj[16];

    // Row 4: cameraPos (float3) + time (float)
    float cameraPos[3];
    float time;

    // Row 5: sunDir (float3) + layerBottomKm (float)
    float sunDir[3];
    float layerBottomKm;

    // Row 6: sunColor (float3) + layerHeightKm (float)
    float sunColor[3];
    float layerHeightKm;

    // Row 7: windVelocity (float3) + coverage (float)
    float windVelocity[3];
    float coverage;

    // Row 8: density, extinction, scatteringAlbedo, phaseG
    float density;
    float extinction;
    float scatteringAlbedo;
    float phaseG;

    // Row 9: phaseG2, phaseBlend, detailWindScale, ambientMultiplier
    float phaseG2;
    float phaseBlend;
    float detailWindScale;
    float ambientMultiplier;

    // Row 10: primarySteps, lightSteps, shapeNoiseSrv, detailNoiseSrv
    uint32_t primarySteps;
    uint32_t lightSteps;
    uint32_t shapeNoiseSrv;
    uint32_t detailNoiseSrv;

    // Row 11: weatherMapSrv, depthSrv, outputWidth, outputHeight
    uint32_t weatherMapSrv;
    uint32_t depthSrv;
    float    outputWidth;
    float    outputHeight;

    // Row 12: ambientColor (float3) + outputUavIndex
    float ambientColor[3];
    uint32_t outputUavIndex;   ///< Bindless descriptor index for cloud output UAV

    // Padding to 256 bytes: 208 bytes data, need 48 more bytes
    float _pad1[12];
};

static_assert(sizeof(CloudRaymarchCBV) == 256,
    "CloudRaymarchCBV must be 256 bytes (D3D12 CB alignment)");

/// CBV data for cloud_temporal_cs.hlsl at register(b2).
/// Matches `cbuffer CloudTemporalCB : register(b2)` — 256-byte aligned.
///
/// Layout:
///   prevViewProj      : 64 bytes (rows 0-3)
///   invViewProj       : 64 bytes (rows 4-7)
///   currentSrv, historySrv, depthSrv, blendFactor : 16 bytes (row 8)
///   outputWidth, outputHeight, pad2               : 16 bytes (row 9)
///   = 160 bytes data, padded to 256
struct CloudTemporalCBV {
    // Rows 0-3: prevViewProj
    float prevViewProj[16];

    // Rows 4-7: invViewProj
    float invViewProj[16];

    // Row 8: uint3 + float
    uint32_t currentSrvIndex;
    uint32_t historySrvIndex;
    uint32_t depthSrvIndex;
    float    blendFactor;

    // Row 9: float2 + outputUavIndex + pad
    float    outputWidth;
    float    outputHeight;
    uint32_t outputUavIndex;  ///< Bindless descriptor index for temporal output UAV
    float    _pad;

    // Padding to 256 bytes: 160 bytes data, need 96 more
    float    _pad2[24];
};

static_assert(sizeof(CloudTemporalCBV) == 256,
    "CloudTemporalCBV must be 256 bytes (D3D12 CB alignment)");

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

CloudPass::~CloudPass() {
    if (m_initialized) {
        Shutdown();
    }
}

bool CloudPass::Init(IRHIDevice* device, ShaderManager* shaderMgr,
                     uint32_t displayWidth, uint32_t displayHeight)
{
    if (m_initialized) {
        UZ_LOG(Renderer, Warning, "CloudPass::Init called while already initialized");
        return true;
    }

    if (!device) {
        UZ_LOG(Renderer, Error, "CloudPass::Init failed: device is null");
        return false;
    }
    if (!shaderMgr) {
        UZ_LOG(Renderer, Error, "CloudPass::Init failed: shaderMgr is null");
        return false;
    }

    m_device    = device;
    m_shaderMgr = shaderMgr;

    // Create noise textures (resolution-independent)
    if (!CreateNoiseTextures()) {
        Shutdown();
        return false;
    }

    // Create weather map (resolution-independent, 512x512)
    if (!CreateWeatherMapTexture()) {
        Shutdown();
        return false;
    }

    // Create half-res cloud output ping-pong textures
    const uint32_t halfW = std::max(displayWidth  / 2, 1u);
    const uint32_t halfH = std::max(displayHeight / 2, 1u);
    if (!CreateCloudOutputTextures(halfW, halfH)) {
        Shutdown();
        return false;
    }

    m_initialized = true;
    UZ_LOG(Renderer, Info,
           "CloudPass initialized (display={}x{}, half={}x{}, shapeNoiseSRV={}, "
           "detailNoiseSRV={}, weatherSRV={})",
           displayWidth, displayHeight, halfW, halfH,
           m_shapeNoiseSrv, m_detailNoiseSrv, m_weatherMapSrv);
    return true;
}

void CloudPass::Shutdown() {
    DestroyCloudOutputTextures();
    DestroyWeatherMapTexture();
    DestroyNoiseTextures();

    if (m_device) {
        // Destroy pipelines
        if (m_weatherMapPipeline.IsValid())  m_device->DestroyPipeline(m_weatherMapPipeline);
        if (m_raymarchPipeline.IsValid())    m_device->DestroyPipeline(m_raymarchPipeline);
        if (m_temporalPipeline.IsValid())    m_device->DestroyPipeline(m_temporalPipeline);

        // Destroy shaders
        if (m_weatherMapCS.IsValid())  m_device->DestroyShader(m_weatherMapCS);
        if (m_raymarchCS.IsValid())    m_device->DestroyShader(m_raymarchCS);
        if (m_temporalCS.IsValid())    m_device->DestroyShader(m_temporalCS);
    }

    m_weatherMapPipeline  = RHIPipelineHandle::Invalid();
    m_raymarchPipeline    = RHIPipelineHandle::Invalid();
    m_temporalPipeline    = RHIPipelineHandle::Invalid();

    m_weatherMapCS  = RHIShaderHandle::Invalid();
    m_raymarchCS    = RHIShaderHandle::Invalid();
    m_temporalCS    = RHIShaderHandle::Invalid();

    m_device    = nullptr;
    m_shaderMgr = nullptr;
    m_initialized  = false;
    m_noisesLoaded = false;
    m_initialCloudBarriersDone  = false;
    m_initialWeatherBarrierDone = false;
    m_frameCount = 0;
    m_cloudHistoryIndex = 0;

    UZ_LOG(Renderer, Info, "CloudPass shut down");
}

void CloudPass::OnResize(uint32_t displayWidth, uint32_t displayHeight) {
    const uint32_t halfW = std::max(displayWidth  / 2, 1u);
    const uint32_t halfH = std::max(displayHeight / 2, 1u);

    if (halfW == m_halfWidth && halfH == m_halfHeight) {
        return;
    }

    DestroyCloudOutputTextures();
    CreateCloudOutputTextures(halfW, halfH);

    // Reset temporal history on resize — history is invalid at new resolution
    m_cloudHistoryIndex = 0;
    m_initialCloudBarriersDone = false;
    m_frameCount = 0;

    UZ_LOG(Renderer, Info, "CloudPass resized to half={}x{}", halfW, halfH);
}

// ─────────────────────────────────────────────────────────────────────────────
// Texture Management
// ─────────────────────────────────────────────────────────────────────────────

bool CloudPass::CreateNoiseTextures() {
    // ── Shape noise: 128^3 RGBA8 3D texture ──
    // CopyDst is required because we upload precomputed data from a staging buffer.
    {
        RHITextureDesc desc{};
        desc.width            = kShapeNoiseSize;
        desc.height           = kShapeNoiseSize;
        desc.depthOrArraySize = static_cast<uint16_t>(kShapeNoiseSize);
        desc.format           = RHIFormat::RGBA8Unorm;
        desc.usage            = RHITextureUsage::ShaderResource
                              | RHITextureUsage::CopyDst;
        desc.is3D             = true;
        desc.debugName        = "Cloud_ShapeNoise_128";

        m_shapeNoise = m_device->CreateTexture(desc);
        if (!m_shapeNoise.IsValid()) {
            UZ_LOG(Renderer, Error, "CloudPass: failed to create shape noise 3D texture (128^3)");
            return false;
        }

        m_shapeNoiseSrv = m_device->GetTextureSRVIndex(m_shapeNoise);

        if (m_shapeNoiseSrv == UINT32_MAX) {
            UZ_LOG(Renderer, Error,
                   "CloudPass: shape noise SRV descriptor creation failed");
            return false;
        }

        UZ_LOG(Renderer, Debug, "CloudPass: shape noise 3D — SRV={}", m_shapeNoiseSrv);
    }

    // ── Detail noise: 32^3 RGBA8 3D texture ──
    {
        RHITextureDesc desc{};
        desc.width            = kDetailNoiseSize;
        desc.height           = kDetailNoiseSize;
        desc.depthOrArraySize = static_cast<uint16_t>(kDetailNoiseSize);
        desc.format           = RHIFormat::RGBA8Unorm;
        desc.usage            = RHITextureUsage::ShaderResource
                              | RHITextureUsage::CopyDst;
        desc.is3D             = true;
        desc.debugName        = "Cloud_DetailNoise_32";

        m_detailNoise = m_device->CreateTexture(desc);
        if (!m_detailNoise.IsValid()) {
            UZ_LOG(Renderer, Error, "CloudPass: failed to create detail noise 3D texture (32^3)");
            return false;
        }

        m_detailNoiseSrv = m_device->GetTextureSRVIndex(m_detailNoise);

        if (m_detailNoiseSrv == UINT32_MAX) {
            UZ_LOG(Renderer, Error,
                   "CloudPass: detail noise SRV descriptor creation failed");
            return false;
        }

        UZ_LOG(Renderer, Debug, "CloudPass: detail noise 3D — SRV={}", m_detailNoiseSrv);
    }

    return true;
}

bool CloudPass::CreateWeatherMapTexture() {
    RHITextureDesc desc{};
    desc.width     = kWeatherMapSize;
    desc.height    = kWeatherMapSize;
    desc.format    = RHIFormat::RGBA8Unorm;
    desc.usage     = RHITextureUsage::ShaderResource
                   | RHITextureUsage::UnorderedAccess;
    desc.debugName = "Cloud_WeatherMap_512";

    m_weatherMap = m_device->CreateTexture(desc);
    if (!m_weatherMap.IsValid()) {
        UZ_LOG(Renderer, Error, "CloudPass: failed to create weather map texture (512x512)");
        return false;
    }

    m_weatherMapSrv = m_device->GetTextureSRVIndex(m_weatherMap);
    m_weatherMapUav = m_device->GetTextureUAVIndex(m_weatherMap);

    if (m_weatherMapSrv == UINT32_MAX || m_weatherMapUav == UINT32_MAX) {
        UZ_LOG(Renderer, Error,
               "CloudPass: weather map descriptor creation failed (SRV={}, UAV={})",
               m_weatherMapSrv, m_weatherMapUav);
        return false;
    }

    UZ_LOG(Renderer, Debug, "CloudPass: weather map — SRV={}, UAV={}",
           m_weatherMapSrv, m_weatherMapUav);
    return true;
}

bool CloudPass::CreateCloudOutputTextures(uint32_t halfW, uint32_t halfH) {
    m_halfWidth  = halfW;
    m_halfHeight = halfH;

    static const char* kNames[2] = { "Cloud_Output_0", "Cloud_Output_1" };

    for (uint32_t i = 0; i < 2; ++i) {
        RHITextureDesc desc{};
        desc.width     = halfW;
        desc.height    = halfH;
        desc.format    = RHIFormat::RGBA16Float;
        desc.usage     = RHITextureUsage::ShaderResource
                       | RHITextureUsage::UnorderedAccess;
        desc.debugName = kNames[i];

        m_cloudOutput[i] = m_device->CreateTexture(desc);
        if (!m_cloudOutput[i].IsValid()) {
            UZ_LOG(Renderer, Error, "CloudPass: failed to create cloud output texture {} ({}x{})",
                   i, halfW, halfH);
            return false;
        }

        m_cloudOutputSrv[i] = m_device->GetTextureSRVIndex(m_cloudOutput[i]);
        m_cloudOutputUav[i] = m_device->GetTextureUAVIndex(m_cloudOutput[i]);

        if (m_cloudOutputSrv[i] == UINT32_MAX || m_cloudOutputUav[i] == UINT32_MAX) {
            UZ_LOG(Renderer, Error,
                   "CloudPass: cloud output {} descriptor creation failed (SRV={}, UAV={})",
                   i, m_cloudOutputSrv[i], m_cloudOutputUav[i]);
            return false;
        }

        UZ_LOG(Renderer, Debug, "CloudPass: cloud output {} ({}x{}) — SRV={}, UAV={}",
               i, halfW, halfH, m_cloudOutputSrv[i], m_cloudOutputUav[i]);
    }

    return true;
}

void CloudPass::DestroyNoiseTextures() {
    if (!m_device) return;

    // Release any staging buffers that were kept alive for the GPU copy
    for (auto& buf : m_noiseStagingBuffers) {
        if (buf.IsValid()) {
            m_device->DestroyBuffer(buf);
        }
    }
    m_noiseStagingBuffers.clear();

    if (m_shapeNoise.IsValid()) {
        m_device->DestroyTexture(m_shapeNoise);
        m_shapeNoise = RHITextureHandle{};
    }
    m_shapeNoiseSrv = UINT32_MAX;

    if (m_detailNoise.IsValid()) {
        m_device->DestroyTexture(m_detailNoise);
        m_detailNoise = RHITextureHandle{};
    }
    m_detailNoiseSrv = UINT32_MAX;

    m_noisesLoaded = false;
}

void CloudPass::DestroyWeatherMapTexture() {
    if (!m_device) return;

    if (m_weatherMap.IsValid()) {
        m_device->DestroyTexture(m_weatherMap);
        m_weatherMap = RHITextureHandle{};
    }
    m_weatherMapSrv = UINT32_MAX;
    m_weatherMapUav = UINT32_MAX;
    m_initialWeatherBarrierDone = false;
}

void CloudPass::DestroyCloudOutputTextures() {
    if (!m_device) return;

    for (uint32_t i = 0; i < 2; ++i) {
        if (m_cloudOutput[i].IsValid()) {
            m_device->DestroyTexture(m_cloudOutput[i]);
            m_cloudOutput[i] = RHITextureHandle{};
        }
        m_cloudOutputSrv[i] = UINT32_MAX;
        m_cloudOutputUav[i] = UINT32_MAX;
    }
    m_halfWidth  = 0;
    m_halfHeight = 0;
    m_initialCloudBarriersDone = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline Creation (lazy)
// ─────────────────────────────────────────────────────────────────────────────

bool CloudPass::EnsureWeatherMapPipeline() {
    if (m_weatherMapPipeline.IsValid()) return true;

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::CloudWeatherCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "CloudPass: failed to compile cloud_weather_cs.hlsl");
        return false;
    }

    RHIShaderDesc sd{};
    sd.bytecode     = bytecode.data();
    sd.bytecodeSize = bytecode.size();
    sd.stage        = RHIShaderStage::Compute;
    sd.entryPoint   = "main";
    sd.debugName    = "cloud_weather_cs";

    m_weatherMapCS = m_device->CreateShader(sd);
    if (!m_weatherMapCS.IsValid()) {
        UZ_LOG(Renderer, Error, "CloudPass: failed to create weather map shader");
        return false;
    }

    RHIComputePipelineDesc pd{};
    pd.computeShader = m_weatherMapCS;
    pd.debugName     = "CloudPass_WeatherMap";

    m_weatherMapPipeline = m_device->CreateComputePipeline(pd);
    if (!m_weatherMapPipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "CloudPass: failed to create weather map pipeline");
        return false;
    }

    UZ_LOG(Renderer, Info, "CloudPass: weather map pipeline created (shader={}, pipeline={})",
           m_weatherMapCS.index, m_weatherMapPipeline.index);
    return true;
}

bool CloudPass::EnsureRaymarchPipeline() {
    if (m_raymarchPipeline.IsValid()) return true;

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::CloudRaymarchCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "CloudPass: failed to compile cloud_raymarch_cs.hlsl");
        return false;
    }

    RHIShaderDesc sd{};
    sd.bytecode     = bytecode.data();
    sd.bytecodeSize = bytecode.size();
    sd.stage        = RHIShaderStage::Compute;
    sd.entryPoint   = "main";
    sd.debugName    = "cloud_raymarch_cs";

    m_raymarchCS = m_device->CreateShader(sd);
    if (!m_raymarchCS.IsValid()) {
        UZ_LOG(Renderer, Error, "CloudPass: failed to create raymarch shader");
        return false;
    }

    RHIComputePipelineDesc pd{};
    pd.computeShader = m_raymarchCS;
    pd.debugName     = "CloudPass_Raymarch";

    m_raymarchPipeline = m_device->CreateComputePipeline(pd);
    if (!m_raymarchPipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "CloudPass: failed to create raymarch pipeline");
        return false;
    }

    UZ_LOG(Renderer, Info, "CloudPass: raymarch pipeline created (shader={}, pipeline={})",
           m_raymarchCS.index, m_raymarchPipeline.index);
    return true;
}

bool CloudPass::EnsureTemporalPipeline() {
    if (m_temporalPipeline.IsValid()) return true;

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::CloudTemporalCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "CloudPass: failed to compile cloud_temporal_cs.hlsl");
        return false;
    }

    RHIShaderDesc sd{};
    sd.bytecode     = bytecode.data();
    sd.bytecodeSize = bytecode.size();
    sd.stage        = RHIShaderStage::Compute;
    sd.entryPoint   = "main";
    sd.debugName    = "cloud_temporal_cs";

    m_temporalCS = m_device->CreateShader(sd);
    if (!m_temporalCS.IsValid()) {
        UZ_LOG(Renderer, Error, "CloudPass: failed to create temporal shader");
        return false;
    }

    RHIComputePipelineDesc pd{};
    pd.computeShader = m_temporalCS;
    pd.debugName     = "CloudPass_Temporal";

    m_temporalPipeline = m_device->CreateComputePipeline(pd);
    if (!m_temporalPipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "CloudPass: failed to create temporal pipeline");
        return false;
    }

    UZ_LOG(Renderer, Info, "CloudPass: temporal pipeline created (shader={}, pipeline={})",
           m_temporalCS.index, m_temporalPipeline.index);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Noise Loading (from precomputed binary files)
// ─────────────────────────────────────────────────────────────────────────────

bool CloudPass::LoadAndUploadNoise3D(IRHICommandList* cmd,
                                     const char* filename,
                                     RHITextureHandle dstTexture,
                                     uint32_t dim,
                                     const char* debugLabel)
{
    UZ_PROFILE_SCOPE("CloudPass::LoadAndUploadNoise3D");

    // ── Read the R8 binary file from disk ──
    const std::filesystem::path filePath = uz::GetExeDir() / "assets" / filename;
    const std::string pathStr = filePath.string();

    std::vector<uint8_t> r8Data = uz::FileReadAll(pathStr.c_str());
    if (r8Data.empty()) {
        UZ_LOG(Renderer, Error,
               "CloudPass: failed to read noise file '{}' for {}", pathStr, debugLabel);
        return false;
    }

    const uint64_t expectedR8Size = static_cast<uint64_t>(dim) * dim * dim;
    if (r8Data.size() != expectedR8Size) {
        UZ_LOG(Renderer, Error,
               "CloudPass: {} file size mismatch — expected {} bytes ({}^3 R8), got {}",
               debugLabel, expectedR8Size, dim, r8Data.size());
        return false;
    }

    UZ_LOG(Renderer, Info, "CloudPass: loaded {} from disk ({} bytes)", debugLabel, r8Data.size());

    // ── Expand R8 → RGBA8 on CPU ──
    // The raymarch shader reads all 4 channels (R, G, B, A) for FBM weighting.
    // With single-channel precomputed data, replicate the value to all channels:
    //   R=G=B=A=value → FBM = v*0.625 + v*0.25 + v*0.125 = v
    constexpr uint32_t kBytesPerTexel = 4; // RGBA8

    // DX12 requires 256-byte aligned row pitch for texture uploads.
    // For a 3D texture, the staging layout must match GetCopyableFootprints:
    //   - Each row: width * bytesPerTexel, padded to 256 bytes
    //   - Each slice: alignedRowPitch * height
    //   - Total: alignedRowPitch * height * depth
    const uint32_t srcRowPitch = dim * kBytesPerTexel;
    constexpr uint32_t kRowAlignment = 256;
    const uint32_t alignedRowPitch = (srcRowPitch + kRowAlignment - 1) & ~(kRowAlignment - 1);
    const uint64_t stagingSize = static_cast<uint64_t>(alignedRowPitch) * dim * dim;

    // ── Create staging buffer ──
    RHIBufferDesc stagingDesc{};
    stagingDesc.sizeBytes = stagingSize;
    stagingDesc.usage     = RHIBufferUsage::CopySrc;
    stagingDesc.memory    = RHIMemoryType::Upload;
    stagingDesc.debugName = debugLabel;

    RHIBufferHandle staging = m_device->CreateBuffer(stagingDesc);
    if (!staging.IsValid()) {
        UZ_LOG(Renderer, Error,
               "CloudPass: failed to create staging buffer for {} ({} bytes)",
               debugLabel, stagingSize);
        return false;
    }

    // ── Map staging buffer and copy expanded RGBA8 data ──
    auto* mapped = static_cast<uint8_t*>(m_device->MapBuffer(staging));
    if (!mapped) {
        UZ_LOG(Renderer, Error, "CloudPass: failed to map staging buffer for {}", debugLabel);
        m_device->DestroyBuffer(staging);
        return false;
    }

    // Copy with row-pitch alignment. For each depth slice, for each row,
    // expand R8 texels to RGBA8 and write to the aligned row in the staging buffer.
    const uint8_t* srcPtr = r8Data.data();
    for (uint32_t z = 0; z < dim; ++z) {
        for (uint32_t y = 0; y < dim; ++y) {
            const uint64_t sliceOffset = static_cast<uint64_t>(z) * alignedRowPitch * dim
                                       + static_cast<uint64_t>(y) * alignedRowPitch;
            uint8_t* dstRow = mapped + sliceOffset;

            // Source offset: z * dim * dim + y * dim (row-major, Z outermost)
            const uint64_t srcRowOffset = (static_cast<uint64_t>(z) * dim + y) * dim;
            const uint8_t* srcRow = srcPtr + srcRowOffset;

            for (uint32_t x = 0; x < dim; ++x) {
                const uint8_t v = srcRow[x];
                dstRow[x * 4 + 0] = v; // R
                dstRow[x * 4 + 1] = v; // G
                dstRow[x * 4 + 2] = v; // B
                dstRow[x * 4 + 3] = v; // A
            }
        }
    }

    m_device->UnmapBuffer(staging);

    // ── Barrier: Common → CopyDst ──
    {
        RHIBarrierDesc barrier{};
        barrier.texture     = dstTexture;
        barrier.stateBefore = RHIResourceState::Common;
        barrier.stateAfter  = RHIResourceState::CopyDst;
        cmd->ResourceBarrier(&barrier, 1);
    }

    // ── Record GPU copy: staging buffer → 3D texture (subresource 0 = all slices) ──
    cmd->CopyBufferToTexture(staging, 0, alignedRowPitch, dstTexture, 0, 0);

    // ── Barrier: CopyDst → SRV ──
    {
        RHIBarrierDesc barrier{};
        barrier.texture     = dstTexture;
        barrier.stateBefore = RHIResourceState::CopyDst;
        barrier.stateAfter  = RHIResourceState::ShaderResource;
        cmd->ResourceBarrier(&barrier, 1);
    }

    // Keep staging buffer alive until the GPU finishes the copy.
    // It will be destroyed in DestroyNoiseTextures() or on the next frame after
    // the command list has been submitted and fenced.
    m_noiseStagingBuffers.push_back(staging);

    UZ_LOG(Renderer, Info,
           "CloudPass: uploaded {} to GPU ({}^3, staging={} bytes, alignedRowPitch={})",
           debugLabel, dim, stagingSize, alignedRowPitch);
    return true;
}

void CloudPass::LoadNoiseTextures(IRHICommandList* cmd) {
    UZ_PROFILE_SCOPE("CloudPass::LoadNoiseTextures");

    if (m_noisesLoaded) {
        return;
    }

    if (!cmd) {
        UZ_LOG(Renderer, Error, "CloudPass::LoadNoiseTextures: null command list");
        return;
    }

    // ── Load shape noise: 128^3 R8 → RGBA8 ──
    if (!LoadAndUploadNoise3D(cmd, "cloud_shape.bin", m_shapeNoise,
                              kShapeNoiseSize, "CloudShapeNoise")) {
        UZ_LOG(Renderer, Error, "CloudPass::LoadNoiseTextures: shape noise load failed");
        return;
    }

    // ── Load detail noise: 32^3 R8 → RGBA8 ──
    if (!LoadAndUploadNoise3D(cmd, "cloud_shape_detail.bin", m_detailNoise,
                              kDetailNoiseSize, "CloudDetailNoise")) {
        UZ_LOG(Renderer, Error, "CloudPass::LoadNoiseTextures: detail noise load failed");
        return;
    }

    m_noisesLoaded = true;
    UZ_LOG(Renderer, Info, "CloudPass: noise textures loaded from disk successfully");
}

// ─────────────────────────────────────────────────────────────────────────────
// Execute — per-frame cloud pipeline
// ─────────────────────────────────────────────────────────────────────────────

void CloudPass::Execute(IRHICommandList* cmd, CBRingAllocator& cbRing,
                        const CloudPassInput& input)
{
    UZ_PROFILE_SCOPE("CloudPass::Execute");

    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "CloudPass::Execute called before Init()");
        return;
    }

    if (!cmd) {
        UZ_LOG(Renderer, Error, "CloudPass::Execute: null command list");
        return;
    }

    // Cloud compute shaders require bindless (ResourceDescriptorHeap).
    // On SM5/DX11, clouds are not available.
    if (!m_device->IsBindless()) {
        static bool loggedOnce = false;
        if (!loggedOnce) {
            UZ_LOG(Renderer, Warning,
                   "CloudPass: skipped — volumetric clouds require bindless (SM6.6+)");
            loggedOnce = true;
        }
        return;
    }

    if (m_halfWidth == 0 || m_halfHeight == 0) {
        UZ_LOG(Renderer, Error, "CloudPass::Execute: zero-size cloud output — aborting");
        return;
    }

    if (input.depthSrv == UINT32_MAX) {
        UZ_LOG(Renderer, Error, "CloudPass::Execute: depthSrv is UINT32_MAX");
        return;
    }

    // ── Step 0: Load noise textures from disk if needed (one-time) ──
    if (!m_noisesLoaded) {
        LoadNoiseTextures(cmd);
        if (!m_noisesLoaded) {
            UZ_LOG(Renderer, Error, "CloudPass::Execute: noise load failed, skipping clouds");
            return;
        }
    }

    // Ensure all per-frame pipelines are compiled
    if (!EnsureWeatherMapPipeline() || !EnsureRaymarchPipeline() || !EnsureTemporalPipeline()) {
        UZ_LOG(Renderer, Error, "CloudPass::Execute: pipeline compilation failed");
        return;
    }


    // First-frame diagnostic dump
    static bool loggedFirstFrame = false;
    if (!loggedFirstFrame) {
        UZ_LOG(Renderer, Info,
               "CloudPass: first execution — half={}x{}, depthSRV={}, shapeSRV={}, "
               "detailSRV={}, weatherSRV={}, steps={}/{}, coverage={:.2f}",
               m_halfWidth, m_halfHeight, input.depthSrv,
               m_shapeNoiseSrv, m_detailNoiseSrv, m_weatherMapSrv,
               input.primarySteps, input.lightSteps, input.coverage);
        loggedFirstFrame = true;
    }

    // Determine ping-pong indices:
    // currentIdx  = where this frame's raymarch writes
    // historyIdx  = where the previous frame's result lives
    const uint32_t currentIdx = m_cloudHistoryIndex;
    const uint32_t historyIdx = 1 - m_cloudHistoryIndex;

    // ══════════════════════════════════════════════════════════════════════
    // STEP 1: Update Weather Map
    // ══════════════════════════════════════════════════════════════════════
    {
        UZ_PROFILE_SCOPE("CloudPass::WeatherMap");

        // Initial barrier: Common → UAV on first frame
        if (!m_initialWeatherBarrierDone) {
            RHIBarrierDesc barrier{};
            barrier.texture     = m_weatherMap;
            barrier.stateBefore = RHIResourceState::Common;
            barrier.stateAfter  = RHIResourceState::UnorderedAccess;
            cmd->ResourceBarrier(&barrier, 1);
            m_initialWeatherBarrierDone = true;
        } else {
            // Subsequent frames: SRV → UAV (was left in SRV from last frame's raymarch read)
            RHIBarrierDesc barrier{};
            barrier.texture     = m_weatherMap;
            barrier.stateBefore = RHIResourceState::ShaderResource;
            barrier.stateAfter  = RHIResourceState::UnorderedAccess;
            cmd->ResourceBarrier(&barrier, 1);
        }

        cmd->SetPipeline(m_weatherMapPipeline);

        // Wind offset = time * windVelocity.xy (accumulated UV drift)
        CloudWeatherRootConstants rc{};
        rc.windOffsetX     = input.time * input.windVelocity[0];
        rc.windOffsetY     = input.time * input.windVelocity[1];
        rc.globalCoverage  = input.globalCoverage;
        rc.outputUavIndex  = m_weatherMapUav;

        cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 4);

        // 512 / 8 = 64 groups per dimension
        const uint32_t groups = kWeatherMapSize / kGroupSize;
        cmd->Dispatch(groups, groups, 1);
    }

    // ── Barrier: weather map UAV → SRV ──
    {
        RHIBarrierDesc barrier{};
        barrier.texture     = m_weatherMap;
        barrier.stateBefore = RHIResourceState::UnorderedAccess;
        barrier.stateAfter  = RHIResourceState::ShaderResource;
        cmd->ResourceBarrier(&barrier, 1);
    }


    // ══════════════════════════════════════════════════════════════════════
    // STEP 2: Ray March Clouds (half-res)
    // ══════════════════════════════════════════════════════════════════════
    {
        UZ_PROFILE_SCOPE("CloudPass::Raymarch");

        // Initial barrier: Common → UAV for cloud output on first frame
        if (!m_initialCloudBarriersDone) {
            RHIBarrierDesc barriers[2] = {};
            barriers[0].texture     = m_cloudOutput[0];
            barriers[0].stateBefore = RHIResourceState::Common;
            barriers[0].stateAfter  = RHIResourceState::UnorderedAccess;
            barriers[1].texture     = m_cloudOutput[1];
            barriers[1].stateBefore = RHIResourceState::Common;
            barriers[1].stateAfter  = RHIResourceState::UnorderedAccess;
            cmd->ResourceBarrier(barriers, 2);
            m_initialCloudBarriersDone = true;
        } else {
            // Transition current output from SRV → UAV (was SRV from previous frame's
            // temporal read, or the initial UAV if this is the first real dispatch)
            RHIBarrierDesc barrier{};
            barrier.texture     = m_cloudOutput[currentIdx];
            barrier.stateBefore = RHIResourceState::ShaderResource;
            barrier.stateAfter  = RHIResourceState::UnorderedAccess;
            cmd->ResourceBarrier(&barrier, 1);
        }

        cmd->SetPipeline(m_raymarchPipeline);

        // Fill the CBV at register(b2) via CBRingAllocator
        CloudRaymarchCBV cb{};
        std::memcpy(cb.invViewProj,   input.invViewProj,   sizeof(cb.invViewProj));
        std::memcpy(cb.cameraPos,     input.cameraPos,     sizeof(cb.cameraPos));
        cb.time = input.time;
        std::memcpy(cb.sunDir,        input.sunDir,        sizeof(cb.sunDir));
        cb.layerBottomKm = input.layerBottomKm;
        std::memcpy(cb.sunColor,      input.sunColor,      sizeof(cb.sunColor));
        cb.layerHeightKm = input.layerHeightKm;
        std::memcpy(cb.windVelocity,  input.windVelocity,  sizeof(cb.windVelocity));
        cb.coverage          = input.coverage;
        cb.density           = input.density;
        cb.extinction        = input.extinction;
        cb.scatteringAlbedo  = input.scatteringAlbedo;
        cb.phaseG            = input.phaseG;
        cb.phaseG2           = input.phaseG2;
        cb.phaseBlend        = input.phaseBlend;
        cb.detailWindScale   = input.detailWindScale;
        cb.ambientMultiplier = input.ambientMultiplier;
        cb.primarySteps      = input.primarySteps;
        cb.lightSteps        = input.lightSteps;
        cb.shapeNoiseSrv     = m_shapeNoiseSrv;
        cb.detailNoiseSrv    = m_detailNoiseSrv;
        cb.weatherMapSrv     = m_weatherMapSrv;
        cb.depthSrv          = input.depthSrv;
        cb.outputWidth       = static_cast<float>(m_halfWidth);
        cb.outputHeight      = static_cast<float>(m_halfHeight);
        std::memcpy(cb.ambientColor,  input.ambientColor,  sizeof(cb.ambientColor));
        cb.outputUavIndex    = m_cloudOutputUav[currentIdx];

        auto alloc = cbRing.Allocate(sizeof(CloudRaymarchCBV));
        if (!alloc.cpuPtr) {
            UZ_LOG(Renderer, Error, "CloudPass: CB ring allocation failed for raymarch CBV");
            return;
        }
        std::memcpy(alloc.cpuPtr, &cb, sizeof(CloudRaymarchCBV));

        // Bind CBV at root param 2 (register b2) via cross-backend SetRootCBV
        cmd->SetRootCBV(2, alloc.bufferHandle, alloc.offsetBytes, alloc.cpuPtr,
                        sizeof(CloudRaymarchCBV));

        // Cloud output UAV index passed via CB field (cb.outputUavIndex)
        // for bindless ResourceDescriptorHeap access — NOT via SetDescriptorTable.

        const uint32_t groupsX = (m_halfWidth  + kGroupSize - 1) / kGroupSize;
        const uint32_t groupsY = (m_halfHeight + kGroupSize - 1) / kGroupSize;
        cmd->Dispatch(groupsX, groupsY, 1);
    }

    // ── Barrier: current cloud output UAV → SRV (for temporal read) ──
    {
        RHIBarrierDesc barrier{};
        barrier.texture     = m_cloudOutput[currentIdx];
        barrier.stateBefore = RHIResourceState::UnorderedAccess;
        barrier.stateAfter  = RHIResourceState::ShaderResource;
        cmd->ResourceBarrier(&barrier, 1);
    }

    // ══════════════════════════════════════════════════════════════════════
    // STEP 3: Temporal Filter
    // ══════════════════════════════════════════════════════════════════════
    //
    // Reads:  current frame cloud (SRV) + history cloud (SRV) + depth (SRV)
    // Writes: history slot (UAV) — overwrites history with blended result
    //
    // On the first frame (m_frameCount == 0), the history buffer is uninitialized.
    // The temporal shader handles this via blendFactor = 1.0 (use current only).
    {
        UZ_PROFILE_SCOPE("CloudPass::TemporalFilter");

        // Transition history output to UAV for temporal write
        // On first frame after init, it's already in UAV from the initial barrier.
        // On subsequent frames, it was left in SRV from last frame's temporal output read.
        if (m_frameCount > 0) {
            RHIBarrierDesc barrier{};
            barrier.texture     = m_cloudOutput[historyIdx];
            barrier.stateBefore = RHIResourceState::ShaderResource;
            barrier.stateAfter  = RHIResourceState::UnorderedAccess;
            cmd->ResourceBarrier(&barrier, 1);
        }

        cmd->SetPipeline(m_temporalPipeline);

        CloudTemporalCBV cb{};
        std::memcpy(cb.prevViewProj, input.prevViewProj, sizeof(cb.prevViewProj));
        std::memcpy(cb.invViewProj,  input.invViewProj,  sizeof(cb.invViewProj));
        cb.currentSrvIndex = m_cloudOutputSrv[currentIdx];
        cb.historySrvIndex = m_cloudOutputSrv[historyIdx];
        cb.depthSrvIndex   = input.depthSrv;
        // On first frame, use blend factor 1.0 (current only, no history to blend with)
        cb.blendFactor     = (m_frameCount == 0) ? 1.0f : input.temporalBlendFactor;
        cb.outputWidth     = static_cast<float>(m_halfWidth);
        cb.outputHeight    = static_cast<float>(m_halfHeight);
        cb.outputUavIndex  = m_cloudOutputUav[historyIdx];

        auto alloc = cbRing.Allocate(sizeof(CloudTemporalCBV));
        if (!alloc.cpuPtr) {
            UZ_LOG(Renderer, Error, "CloudPass: CB ring allocation failed for temporal CBV");
            return;
        }
        std::memcpy(alloc.cpuPtr, &cb, sizeof(CloudTemporalCBV));

        cmd->SetRootCBV(2, alloc.bufferHandle, alloc.offsetBytes, alloc.cpuPtr,
                        sizeof(CloudTemporalCBV));

        // Temporal output UAV index passed via CB field for bindless access.

        const uint32_t groupsX = (m_halfWidth  + kGroupSize - 1) / kGroupSize;
        const uint32_t groupsY = (m_halfHeight + kGroupSize - 1) / kGroupSize;
        cmd->Dispatch(groupsX, groupsY, 1);
    }

    // ── Barrier: temporal output (history slot) UAV → SRV ──
    // The temporal output is the final cloud result for this frame. Transition
    // to SRV so the compositing pass (or tonemap) can read it.
    {
        RHIBarrierDesc barrier{};
        barrier.texture     = m_cloudOutput[historyIdx];
        barrier.stateBefore = RHIResourceState::UnorderedAccess;
        barrier.stateAfter  = RHIResourceState::ShaderResource;
        cmd->ResourceBarrier(&barrier, 1);
    }

    // ── Flip ping-pong ──
    // After this frame:
    //   historyIdx (now SRV) = temporally filtered result (the "output" for compositing)
    //   currentIdx (now SRV) = this frame's raw raymarch result
    // Next frame: m_cloudHistoryIndex swaps, so the new "history" is this frame's
    // temporal output, and the new "current" slot gets overwritten by the new raymarch.
    m_cloudHistoryIndex = 1 - m_cloudHistoryIndex;
    m_frameCount++;
}

// ─────────────────────────────────────────────────────────────────────────────
// Accessors
// ─────────────────────────────────────────────────────────────────────────────

uint32_t CloudPass::GetCloudOutputSrv() const noexcept {
    if (!m_initialized || m_frameCount == 0) {
        return UINT32_MAX;
    }

    // The temporal filter wrote its output into the history slot. After the
    // ping-pong flip, the history slot is at (1 - m_cloudHistoryIndex).
    // But we flipped already, so the previous history index = m_cloudHistoryIndex.
    // Wait — let's think carefully:
    //   Before flip: currentIdx = old m_cloudHistoryIndex
    //                historyIdx = 1 - old m_cloudHistoryIndex
    //   Temporal wrote into historyIdx.
    //   After flip:  m_cloudHistoryIndex = 1 - old = historyIdx
    //
    // So the temporal output is at the NEW m_cloudHistoryIndex.
    // That's the SRV we want to expose.
    return m_cloudOutputSrv[m_cloudHistoryIndex];
}

} // namespace uz
