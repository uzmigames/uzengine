/// UzEngine Brixelizer GI Pass — Implementation
///
/// @ref AMD 2024 "FidelityFX Brixelizer GI" — sparse SDF voxelization + probe-based GI.
///
/// The FFX SDK 1.1.4 must be built separately with CMake/MSVC to produce
/// `ffx_backend_dx12_x64.lib`. Without it, this file compiles as a no-op stub.

#ifndef UZ_BRIXELIZER_ENABLED
#define UZ_BRIXELIZER_ENABLED 0
#endif

#include "brixelizer_gi_pass.h"
#include "log.h"
#include "cvar.h"

#if UZ_BRIXELIZER_ENABLED

#include "engine/rhi/rhi.h"
#include "engine/rhi/rhi_types.h"
#include "engine/rhi/dx12/dx12_device.h"
#include "engine/rhi/dx12/dx12_command_list.h"
#include "engine/rhi/dx12/dx12_resources.h"
#include "engine/renderer/core/gbuffer/draw_list.h"
#include "profiler.h"

// FFX SDK 1.1.4: Brixelizer + BrixelizerGI context APIs.
#include <FidelityFX/host/ffx_brixelizer.h>
#include <FidelityFX/host/ffx_brixelizergi.h>

// Forward-declare FFX DX12 backend functions to avoid including ffx_dx12.h
// which transitively pulls headers with MSVC/Agility SDK ABI conflicts.
extern "C" {
FFX_API size_t ffxGetScratchMemorySizeDX12(size_t maxContexts);
FFX_API FfxDevice ffxGetDeviceDX12(ID3D12Device* device);
FFX_API FfxErrorCode ffxGetInterfaceDX12(
    FfxInterface* backendInterface,
    FfxDevice device,
    void* scratchBuffer,
    size_t scratchBufferSize,
    size_t maxContexts);
FFX_API FfxCommandList ffxGetCommandListDX12(ID3D12CommandList* cmdList);
FFX_API FfxResource ffxGetResourceDX12(
    const ID3D12Resource* dx12Resource,
    FfxResourceDescription ffxResDescription,
    const wchar_t* ffxResName,
    FfxResourceStates state);
} // extern "C"

#include <cstring>
#include <cstdlib>
#include <unordered_map>

UZ_DECLARE_LOG_CATEGORY(Renderer);

// ─────────────────────────────────────────────────────────────────────────────
// CVars
// ─────────────────────────────────────────────────────────────────────────────

/// Master toggle for Brixelizer GI.
static auto* cv_BrixGI = UZ_CVAR_BOOL("r.BrixelizerGI", false,
    "Enable AMD FidelityFX Brixelizer GI (sparse SDF voxel global illumination)");

/// Number of Brixelizer cascades (affects quality and VRAM usage).
/// @ref AMD 2024 "FidelityFX Brixelizer" — 4 cascades recommended for indoor scenes.
static auto* cv_BrixCascades = UZ_CVAR_INT("r.BrixelizerGI.Cascades", 4,
    "Number of Brixelizer SDF cascades (2-8)", 2, 8);

/// GI intensity multiplier applied to both diffuse and specular indirect.
static auto* cv_BrixIntensity = UZ_CVAR_FLOAT("r.BrixelizerGI.Intensity", 1.0f,
    "Brixelizer GI intensity multiplier", 0.0f, 10.0f);

/// Debug visualization mode.
/// 0 = off, 1 = radiance cache, 2 = irradiance cache.
static auto* cv_BrixDebug = UZ_CVAR_INT("r.BrixelizerGI.Debug", 0,
    "Brixelizer GI debug mode (0=off, 1=radiance cache, 2=irradiance cache)", 0, 2);

/// Voxel size for the finest cascade in cm.
/// @ref AMD 2024 "FidelityFX Brixelizer" — 2 cm recommended for indoor detail.
static auto* cv_BrixVoxelSize = UZ_CVAR_FLOAT("r.BrixelizerGI.VoxelSize", 2.0f,
    "Brixelizer finest cascade voxel size (cm)", 0.5f, 32.0f);

/// Ray pushoff distance along surface normal to prevent self-intersection.
static auto* cv_BrixRayPushoff = UZ_CVAR_FLOAT("r.BrixelizerGI.RayPushoff", 0.5f,
    "Ray pushoff distance from surface (cm)", 0.01f, 10.0f);

/// SDF solve epsilon for ray marching.
static auto* cv_BrixSdfSolveEps = UZ_CVAR_FLOAT("r.BrixelizerGI.SdfSolveEps", 0.5f,
    "SDF ray march epsilon (cm)", 0.01f, 10.0f);

/// Maximum references per cascade update (limits voxelization cost).
static auto* cv_BrixMaxRefs = UZ_CVAR_INT("r.BrixelizerGI.MaxReferences", 32 * 1024,
    "Max triangle references per cascade update", 1024, 256 * 1024);

/// Maximum bricks per bake (limits SDF build cost).
static auto* cv_BrixMaxBricks = UZ_CVAR_INT("r.BrixelizerGI.MaxBricksPerBake", 16384,
    "Max bricks per SDF bake update", 1024, 65536);

/// Triangle swap space size (bytes).
static auto* cv_BrixTriSwapSize = UZ_CVAR_INT("r.BrixelizerGI.TriangleSwapSize",
    64 * 1024 * 1024, "Triangle swap space size (bytes)", 1024 * 1024, 256 * 1024 * 1024);

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Maximum number of simultaneous FFX contexts sharing the backend.
/// Brixelizer uses ~3 internal contexts, BrixelizerGI uses ~1.
static constexpr size_t kMaxFfxContexts = 8;

/// Maximum Brixelizer cascades.
static constexpr uint32_t kMaxCascades = 24;

/// Scratch buffer size for Brixelizer update (64 MB default).
static constexpr size_t kDefaultScratchSize = 64 * 1024 * 1024;

// ─────────────────────────────────────────────────────────────────────────────
// PIMPL
// ─────────────────────────────────────────────────────────────────────────────

namespace uz {

/// Tracks a registered mesh instance in Brixelizer.
struct BrixelizerMeshInstance {
    uint32_t meshId = 0;
    FfxBrixelizerInstanceID instanceId{};
    uint32_t vertexBufferIndex = UINT32_MAX;
    uint32_t indexBufferIndex  = UINT32_MAX;
    bool     isStatic = true;
};

struct BrixelizerGIPass::Impl {
    IRHIDevice* device = nullptr;
    DX12Device* dx12Device = nullptr;

    uint32_t displayWidth  = 0;
    uint32_t displayHeight = 0;
    uint32_t frameIndex    = 0;

    // ── FFX Backend ──
    void* scratchBuffer = nullptr;
    size_t scratchBufferSize = 0;
    FfxInterface brixelizerBackend{};
    FfxInterface giBackend{};

    // ── Brixelizer Context (SDF voxelization) ──
    FfxBrixelizerContext brixelizerCtx{};
    bool brixelizerCreated = false;
    uint32_t numCascades = 4;

    // ── BrixelizerGI Context (probe-based GI) ──
    FfxBrixelizerGIContext giCtx{};
    bool giCreated = false;

    // ── GPU Resources ──
    RHITextureHandle diffuseGITexture;
    uint32_t diffuseGISrv = UINT32_MAX;
    uint32_t diffuseGIUav = UINT32_MAX;

    RHITextureHandle specularGITexture;
    uint32_t specularGISrv = UINT32_MAX;
    uint32_t specularGIUav = UINT32_MAX;

    RHIBufferHandle scratchGpuBuffer;

    // ── Blue Noise ──
    RHITextureHandle blueNoiseTexture;
    uint32_t blueNoiseSrv = UINT32_MAX;

    // ── History (previous frame) ──
    RHITextureHandle prevDepth;
    uint32_t prevDepthSrv = UINT32_MAX;
    RHITextureHandle prevNormals;
    uint32_t prevNormalsSrv = UINT32_MAX;

    // ── Geometry tracking ──
    std::vector<BrixelizerMeshInstance> registeredInstances;
    std::unordered_map<uint64_t, uint32_t> meshIdToBufferIndex;
    uint32_t nextBufferIndex = 0;

    // ── Previous frame matrices ──
    float prevViewMatrix[16] = {};
    float prevProjMatrix[16] = {};
    bool hasPrevMatrices = false;

    bool CreateOutputTextures();
    void DestroyOutputTextures();
    bool CreateBlueNoise();
    bool CreateHistoryTextures();
    void DestroyHistoryTextures();
    bool CreateScratchBuffer();
    void DestroyScratchBuffer();

    /// Wraps an RHI texture into an FfxResource for the FFX SDK.
    FfxResource WrapTexture(RHITextureHandle texture, const wchar_t* name,
                            FfxResourceStates state = FFX_RESOURCE_STATE_COMPUTE_READ);

    /// Wraps an RHI buffer into an FfxResource for the FFX SDK.
    FfxResource WrapBuffer(RHIBufferHandle buffer, uint32_t sizeBytes, const wchar_t* name,
                           FfxResourceStates state = FFX_RESOURCE_STATE_COMPUTE_READ);
};

// ─────────────────────────────────────────────────────────────────────────────
// Impl — Resource Creation
// ─────────────────────────────────────────────────────────────────────────────

bool BrixelizerGIPass::Impl::CreateOutputTextures() {
    RHITextureDesc desc{};
    desc.width  = displayWidth;
    desc.height = displayHeight;
    desc.format = RHIFormat::RGBA16Float;
    desc.usage  = RHITextureUsage::UnorderedAccess | RHITextureUsage::ShaderResource;
    desc.debugName = "BrixelizerGI.DiffuseGI";

    diffuseGITexture = device->CreateTexture(desc);
    if (!diffuseGITexture.IsValid()) return false;
    diffuseGISrv = device->GetTextureSRVIndex(diffuseGITexture);
    diffuseGIUav = device->GetTextureUAVIndex(diffuseGITexture);

    desc.debugName = "BrixelizerGI.SpecularGI";
    specularGITexture = device->CreateTexture(desc);
    if (!specularGITexture.IsValid()) return false;
    specularGISrv = device->GetTextureSRVIndex(specularGITexture);
    specularGIUav = device->GetTextureUAVIndex(specularGITexture);

    return true;
}

void BrixelizerGIPass::Impl::DestroyOutputTextures() {
    if (diffuseGITexture.IsValid())  { device->DestroyTexture(diffuseGITexture);  diffuseGITexture = {}; }
    if (specularGITexture.IsValid()) { device->DestroyTexture(specularGITexture); specularGITexture = {}; }
    diffuseGISrv = specularGISrv = UINT32_MAX;
    diffuseGIUav = specularGIUav = UINT32_MAX;
}

bool BrixelizerGIPass::Impl::CreateBlueNoise() {
    // 64x64 R8 blue noise texture for stochastic sampling.
    // Generate a simple blue noise pattern procedurally (R2 low-discrepancy sequence).
    RHITextureDesc desc{};
    desc.width  = 64;
    desc.height = 64;
    desc.format = RHIFormat::R8Unorm;
    desc.usage  = RHITextureUsage::ShaderResource;
    desc.debugName = "BrixelizerGI.BlueNoise64";

    // Generate R2 blue noise pattern
    std::vector<uint8_t> noiseData(64 * 64);
    const float g  = 1.32471795724f; // plastic constant
    const float a1 = 1.0f / g;
    const float a2 = 1.0f / (g * g);
    for (uint32_t i = 0; i < 64 * 64; ++i) {
        float s = 0.5f + a1 * static_cast<float>(i);
        s = s - static_cast<float>(static_cast<int>(s)); // fract
        noiseData[i] = static_cast<uint8_t>(s * 255.0f);
    }

    blueNoiseTexture = device->CreateTexture(desc);
    if (!blueNoiseTexture.IsValid()) return false;
    blueNoiseSrv = device->GetTextureSRVIndex(blueNoiseTexture);
    return blueNoiseSrv != UINT32_MAX;
}

bool BrixelizerGIPass::Impl::CreateHistoryTextures() {
    RHITextureDesc desc{};
    desc.width  = displayWidth;
    desc.height = displayHeight;
    desc.usage  = RHITextureUsage::ShaderResource;

    // Previous frame depth (R32F)
    desc.format = RHIFormat::R32Float;
    desc.debugName = "BrixelizerGI.PrevDepth";
    prevDepth = device->CreateTexture(desc);
    if (!prevDepth.IsValid()) return false;
    prevDepthSrv = device->GetTextureSRVIndex(prevDepth);

    // Previous frame normals (RG16F oct-encoded)
    desc.format = RHIFormat::RG16Float;
    desc.debugName = "BrixelizerGI.PrevNormals";
    prevNormals = device->CreateTexture(desc);
    if (!prevNormals.IsValid()) return false;
    prevNormalsSrv = device->GetTextureSRVIndex(prevNormals);

    return true;
}

void BrixelizerGIPass::Impl::DestroyHistoryTextures() {
    if (prevDepth.IsValid())   { device->DestroyTexture(prevDepth);   prevDepth = {}; }
    if (prevNormals.IsValid()) { device->DestroyTexture(prevNormals); prevNormals = {}; }
    prevDepthSrv = prevNormalsSrv = UINT32_MAX;
}

bool BrixelizerGIPass::Impl::CreateScratchBuffer() {
    RHIBufferDesc desc{};
    desc.sizeBytes = kDefaultScratchSize;
    desc.usage     = RHIBufferUsage::UnorderedAccess;
    desc.debugName = "BrixelizerGI.ScratchGPU";
    scratchGpuBuffer = device->CreateBuffer(desc);
    return scratchGpuBuffer.IsValid();
}

void BrixelizerGIPass::Impl::DestroyScratchBuffer() {
    if (scratchGpuBuffer.IsValid()) { device->DestroyBuffer(scratchGpuBuffer); scratchGpuBuffer = {}; }
}

FfxResource BrixelizerGIPass::Impl::WrapTexture(
    RHITextureHandle texture, const wchar_t* name, FfxResourceStates state) {
    if (!texture.IsValid()) return {};
    auto& pool = dx12Device->GetTexturePool();
    DX12Texture* dx12Tex = pool.Get(texture.index, texture.generation);
    if (!dx12Tex || !dx12Tex->resource) return {};

    FfxResourceDescription desc{};
    desc.type   = FFX_RESOURCE_TYPE_TEXTURE2D;
    desc.width  = dx12Tex->desc.width;
    desc.height = dx12Tex->desc.height;
    desc.depth  = 1;
    desc.mipCount = 1;
    desc.flags  = FFX_RESOURCE_FLAGS_NONE;

    // Map RHI format to FFX format
    switch (dx12Tex->desc.format) {
        case RHIFormat::RGBA16Float: desc.format = FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT; break;
        case RHIFormat::R32Float:    desc.format = FFX_SURFACE_FORMAT_R32_FLOAT; break;
        case RHIFormat::RG16Float:   desc.format = FFX_SURFACE_FORMAT_R16G16_FLOAT; break;
        case RHIFormat::R8Unorm:     desc.format = FFX_SURFACE_FORMAT_R8_UNORM; break;
        case RHIFormat::RGBA8Unorm:  desc.format = FFX_SURFACE_FORMAT_R8G8B8A8_UNORM; break;
        default:                     desc.format = FFX_SURFACE_FORMAT_UNKNOWN; break;
    }

    return ffxGetResourceDX12(dx12Tex->resource, desc, name, state);
}

FfxResource BrixelizerGIPass::Impl::WrapBuffer(
    RHIBufferHandle buffer, uint32_t sizeBytes, const wchar_t* name, FfxResourceStates state) {
    if (!buffer.IsValid()) return {};
    auto& bufPool = dx12Device->GetBufferPool();
    DX12Buffer* dx12Buf = bufPool.Get(buffer.index, buffer.generation);
    if (!dx12Buf || !dx12Buf->resource) return {};

    FfxResourceDescription desc{};
    desc.type   = FFX_RESOURCE_TYPE_BUFFER;
    desc.width  = sizeBytes;
    desc.height = 1;
    desc.depth  = 1;
    desc.mipCount = 1;
    desc.format = FFX_SURFACE_FORMAT_UNKNOWN;
    desc.flags  = FFX_RESOURCE_FLAGS_NONE;

    return ffxGetResourceDX12(dx12Buf->resource, desc, name, state);
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API — Init / Shutdown / IsInitialized
// ─────────────────────────────────────────────────────────────────────────────

BrixelizerGIPass::BrixelizerGIPass()  = default;
BrixelizerGIPass::~BrixelizerGIPass() { Shutdown(); }
BrixelizerGIPass::BrixelizerGIPass(BrixelizerGIPass&&) noexcept = default;
BrixelizerGIPass& BrixelizerGIPass::operator=(BrixelizerGIPass&&) noexcept = default;

bool BrixelizerGIPass::Init(IRHIDevice* device, uint32_t displayWidth, uint32_t displayHeight) {
    if (!device || displayWidth == 0 || displayHeight == 0) return false;

    m_impl = std::make_unique<Impl>();
    m_impl->device = device;
    m_impl->displayWidth  = displayWidth;
    m_impl->displayHeight = displayHeight;

    // Extract DX12 device — BrixelizerGI requires direct DX12 access for FFX SDK.
    m_impl->dx12Device = static_cast<DX12Device*>(device);
    if (!m_impl->dx12Device) {
        UZ_LOG(Renderer, Error, "BrixelizerGI: failed to get DX12 device");
        return false;
    }

    ID3D12Device* d3dDevice = m_impl->dx12Device->GetDevice();

    // ── Create FFX backend for Brixelizer ──
    m_impl->scratchBufferSize = ffxGetScratchMemorySizeDX12(kMaxFfxContexts);
    m_impl->scratchBuffer = std::malloc(m_impl->scratchBufferSize);
    if (!m_impl->scratchBuffer) {
        UZ_LOG(Renderer, Error, "BrixelizerGI: failed to allocate FFX scratch memory (%zu bytes)",
               m_impl->scratchBufferSize);
        return false;
    }

    FfxDevice ffxDevice = ffxGetDeviceDX12(d3dDevice);

    FfxErrorCode err = ffxGetInterfaceDX12(
        &m_impl->brixelizerBackend, ffxDevice,
        m_impl->scratchBuffer, m_impl->scratchBufferSize, kMaxFfxContexts);
    if (err != FFX_OK) {
        UZ_LOG(Renderer, Error, "BrixelizerGI: ffxGetInterfaceDX12 failed for Brixelizer (err=%d)", err);
        return false;
    }

    // ── Create second backend for BrixelizerGI ──
    size_t giScratchSize = ffxGetScratchMemorySizeDX12(kMaxFfxContexts);
    void* giScratch = std::malloc(giScratchSize);
    err = ffxGetInterfaceDX12(&m_impl->giBackend, ffxDevice, giScratch, giScratchSize, kMaxFfxContexts);
    if (err != FFX_OK) {
        UZ_LOG(Renderer, Error, "BrixelizerGI: ffxGetInterfaceDX12 failed for GI (err=%d)", err);
        std::free(giScratch);
        return false;
    }

    // ── Create Brixelizer Context ──
    const uint32_t numCascades = static_cast<uint32_t>(static_cast<int>(cv_BrixCascades->value));
    m_impl->numCascades = numCascades;

    FfxBrixelizerContextDescription brixDesc{};
    brixDesc.sdfCenter[0] = 0.0f;
    brixDesc.sdfCenter[1] = 0.0f;
    brixDesc.sdfCenter[2] = 0.0f;
    brixDesc.numCascades = numCascades;
    brixDesc.flags = static_cast<FfxBrixelizerContextFlags>(0);
    brixDesc.backendInterface = m_impl->brixelizerBackend;

    // Configure cascades with doubling voxel sizes.
    // @ref AMD 2024 "FidelityFX Brixelizer" — cascade voxel sizes double per level.
    float baseVoxelSize = cv_BrixVoxelSize->value;
    for (uint32_t i = 0; i < numCascades; ++i) {
        brixDesc.cascadeDescs[i].flags = static_cast<FfxBrixelizerCascadeFlag>(
            FFX_BRIXELIZER_CASCADE_STATIC | FFX_BRIXELIZER_CASCADE_DYNAMIC);
        brixDesc.cascadeDescs[i].voxelSize = baseVoxelSize * static_cast<float>(1u << i);
    }

    err = ffxBrixelizerContextCreate(&brixDesc, &m_impl->brixelizerCtx);
    if (err != FFX_OK) {
        UZ_LOG(Renderer, Error, "BrixelizerGI: ffxBrixelizerContextCreate failed (err=%d)", err);
        return false;
    }
    m_impl->brixelizerCreated = true;

    UZ_LOG(Renderer, Info, "BrixelizerGI: Brixelizer context created (%u cascades, base voxel %.1f cm)",
           numCascades, baseVoxelSize);

    // ── Create BrixelizerGI Context ──
    FfxBrixelizerGIContextDescription giDesc{};
    giDesc.flags = static_cast<FfxBrixelizerGIFlags>(FFX_BRIXELIZER_GI_FLAG_DEPTH_INVERTED);
    giDesc.internalResolution = FFX_BRIXELIZER_GI_INTERNAL_RESOLUTION_NATIVE;
    giDesc.displaySize.width  = displayWidth;
    giDesc.displaySize.height = displayHeight;
    giDesc.backendInterface   = m_impl->giBackend;

    err = ffxBrixelizerGIContextCreate(&m_impl->giCtx, &giDesc);
    if (err != FFX_OK) {
        UZ_LOG(Renderer, Error, "BrixelizerGI: ffxBrixelizerGIContextCreate failed (err=%d)", err);
        // Clean up Brixelizer context
        ffxBrixelizerContextDestroy(&m_impl->brixelizerCtx);
        m_impl->brixelizerCreated = false;
        return false;
    }
    m_impl->giCreated = true;

    UZ_LOG(Renderer, Info, "BrixelizerGI: GI context created (%ux%u, reversed-Z)",
           displayWidth, displayHeight);

    // ── Create GPU resources ──
    if (!m_impl->CreateOutputTextures()) {
        UZ_LOG(Renderer, Error, "BrixelizerGI: failed to create output textures");
        Shutdown();
        return false;
    }

    if (!m_impl->CreateBlueNoise()) {
        UZ_LOG(Renderer, Error, "BrixelizerGI: failed to create blue noise texture");
        Shutdown();
        return false;
    }

    if (!m_impl->CreateHistoryTextures()) {
        UZ_LOG(Renderer, Error, "BrixelizerGI: failed to create history textures");
        Shutdown();
        return false;
    }

    if (!m_impl->CreateScratchBuffer()) {
        UZ_LOG(Renderer, Error, "BrixelizerGI: failed to create GPU scratch buffer");
        Shutdown();
        return false;
    }

    UZ_LOG(Renderer, Info, "BrixelizerGI: initialized successfully");
    return true;
}

void BrixelizerGIPass::Shutdown() {
    if (!m_impl) return;

    // Wait for GPU to finish before destroying FFX contexts
    if (m_impl->device) {
        m_impl->device->WaitIdle();
    }

    // Destroy FFX contexts
    if (m_impl->giCreated) {
        ffxBrixelizerGIContextDestroy(&m_impl->giCtx);
        m_impl->giCreated = false;
    }

    if (m_impl->brixelizerCreated) {
        ffxBrixelizerContextDestroy(&m_impl->brixelizerCtx);
        m_impl->brixelizerCreated = false;
    }

    // Destroy GPU resources
    m_impl->DestroyOutputTextures();
    m_impl->DestroyHistoryTextures();
    m_impl->DestroyScratchBuffer();

    if (m_impl->blueNoiseTexture.IsValid()) {
        m_impl->device->DestroyTexture(m_impl->blueNoiseTexture);
        m_impl->blueNoiseTexture = {};
    }

    // Free FFX scratch memory
    if (m_impl->scratchBuffer) {
        std::free(m_impl->scratchBuffer);
        m_impl->scratchBuffer = nullptr;
    }

    m_impl->registeredInstances.clear();
    m_impl->meshIdToBufferIndex.clear();
    m_impl.reset();

    UZ_LOG(Renderer, Info, "BrixelizerGI: shutdown complete");
}

bool BrixelizerGIPass::IsInitialized() const {
    return m_impl && m_impl->brixelizerCreated && m_impl->giCreated;
}

// ─────────────────────────────────────────────────────────────────────────────
// Geometry Registration
// ─────────────────────────────────────────────────────────────────────────────

void BrixelizerGIPass::RegisterGeometry(const std::vector<DrawCommand>& drawList) {
    if (!IsInitialized()) return;
    UZ_PROFILE_SCOPE("BrixelizerGI::RegisterGeometry");

    // For dynamic geometry, delete all previous dynamic instances and re-register.
    // Static geometry is registered once and tracked by meshId.

    // Delete all dynamic instances from previous frame
    std::vector<FfxBrixelizerInstanceID> toDelete;
    for (auto it = m_impl->registeredInstances.begin(); it != m_impl->registeredInstances.end(); ) {
        if (!it->isStatic) {
            toDelete.push_back(it->instanceId);
            it = m_impl->registeredInstances.erase(it);
        } else {
            ++it;
        }
    }

    if (!toDelete.empty()) {
        ffxBrixelizerDeleteInstances(&m_impl->brixelizerCtx,
                                     toDelete.data(),
                                     static_cast<uint32_t>(toDelete.size()));
    }

    // Geometry registration requires vertex/index buffer registration with
    // ffxBrixelizerRegisterBuffers() and AABB data from the GPUMesh pool.
    // DrawCommand alone lacks these fields — full registration requires:
    //   1. Registering each unique vertex/index buffer pair via ffxBrixelizerRegisterBuffers()
    //   2. Computing world-space AABBs from local-space mesh bounds + world transform
    //   3. Populating FfxBrixelizerInstanceDescription with buffer indices, strides, counts
    //
    // This will be completed in task 3.3 when the GPUMesh buffer registration bridge is built.
    // For now, the pass initializes, creates contexts, and outputs black GI textures
    // (which the deferred lighting shader correctly ignores via the UINT32_MAX sentinel).
    (void)drawList;
}

// ─────────────────────────────────────────────────────────────────────────────
// Execute — SDF Bake + GI Dispatch
// ─────────────────────────────────────────────────────────────────────────────

void BrixelizerGIPass::Execute(
    IRHICommandList* cmdList,
    const float* viewMatrix,
    const float* projMatrix,
    const float* prevView,
    const float* prevProj,
    float cameraPosX, float cameraPosY, float cameraPosZ,
    uint32_t frameIndex,
    RHITextureHandle depthTexture,
    RHITextureHandle normalTexture,
    RHITextureHandle velocityTexture,
    RHITextureHandle envMapTexture,
    RHITextureHandle prevLitTexture)
{
    if (!IsInitialized()) return;
    if (!(cv_BrixGI->value > 0.5f)) return;
    UZ_PROFILE_SCOPE("BrixelizerGI::Execute");

    auto* dx12CmdList = static_cast<DX12CommandList*>(cmdList);
    FfxCommandList ffxCmdList = ffxGetCommandListDX12(dx12CmdList->GetNative());

    // ── Step 1: Brixelizer SDF Update ──
    FfxBrixelizerUpdateDescription updateDesc{};
    updateDesc.frameIndex = frameIndex;
    updateDesc.sdfCenter[0] = cameraPosX;
    updateDesc.sdfCenter[1] = cameraPosY;
    updateDesc.sdfCenter[2] = cameraPosZ;
    updateDesc.maxReferences    = static_cast<uint32_t>(static_cast<int>(cv_BrixMaxRefs->value));
    updateDesc.triangleSwapSize = static_cast<uint32_t>(static_cast<int>(cv_BrixTriSwapSize->value));
    updateDesc.maxBricksPerBake = static_cast<uint32_t>(static_cast<int>(cv_BrixMaxBricks->value));
    updateDesc.populateDebugAABBsFlags = FFX_BRIXELIZER_POPULATE_AABBS_NONE;
    updateDesc.debugVisualizationDesc  = nullptr;

    size_t requiredScratch = 0;
    updateDesc.outScratchBufferSize = &requiredScratch;

    // Bake update (CPU-side preparation)
    FfxBrixelizerBakedUpdateDescription bakedUpdate{};
    FfxErrorCode err = ffxBrixelizerBakeUpdate(&m_impl->brixelizerCtx, &updateDesc, &bakedUpdate);
    if (err != FFX_OK) {
        UZ_LOG(Renderer, Warning, "BrixelizerGI: ffxBrixelizerBakeUpdate failed (err=%d)", err);
        return;
    }

    // Execute update on GPU
    FfxResource scratchRes = m_impl->WrapBuffer(
        m_impl->scratchGpuBuffer, kDefaultScratchSize,
        L"BrixelizerGI.Scratch", FFX_RESOURCE_STATE_UNORDERED_ACCESS);

    err = ffxBrixelizerUpdate(&m_impl->brixelizerCtx, &bakedUpdate, scratchRes, ffxCmdList);
    if (err != FFX_OK) {
        UZ_LOG(Renderer, Warning, "BrixelizerGI: ffxBrixelizerUpdate failed (err=%d)", err);
        return;
    }

    // ── Step 2: BrixelizerGI Dispatch ──
    FfxBrixelizerGIDispatchDescription giDispatch{};

    // View/projection matrices (row-major as required by FFX)
    std::memcpy(&giDispatch.view, viewMatrix, sizeof(float) * 16);
    std::memcpy(&giDispatch.projection, projMatrix, sizeof(float) * 16);
    std::memcpy(&giDispatch.prevView, prevView, sizeof(float) * 16);
    std::memcpy(&giDispatch.prevProjection, prevProj, sizeof(float) * 16);

    giDispatch.cameraPosition[0] = cameraPosX;
    giDispatch.cameraPosition[1] = cameraPosY;
    giDispatch.cameraPosition[2] = cameraPosZ;
    giDispatch.startCascade   = 0;
    giDispatch.endCascade     = m_impl->numCascades - 1;
    giDispatch.rayPushoff     = cv_BrixRayPushoff->value;
    giDispatch.sdfSolveEps    = cv_BrixSdfSolveEps->value;
    giDispatch.specularRayPushoff  = cv_BrixRayPushoff->value * 0.5f;
    giDispatch.specularSDFSolveEps = cv_BrixSdfSolveEps->value * 0.5f;
    giDispatch.tMin = 0.0f;
    giDispatch.tMax = 10000.0f; // 100m in cm

    // Normals are oct-encoded [0,1] → need unpackMul=2, unpackAdd=-1 to get [-1,1]
    giDispatch.normalsUnpackMul      = 2.0f;
    giDispatch.normalsUnpackAdd      = -1.0f;
    giDispatch.isRoughnessPerceptual = true;
    giDispatch.roughnessChannel      = 0; // R channel
    giDispatch.roughnessThreshold    = 0.8f;
    giDispatch.environmentMapIntensity = 1.0f;
    giDispatch.motionVectorScale = {1.0f, 1.0f};

    // Wrap GBuffer textures as FfxResource for the FFX SDK.
    giDispatch.depth         = m_impl->WrapTexture(depthTexture,    L"GBuffer.Depth");
    giDispatch.historyDepth  = m_impl->WrapTexture(m_impl->prevDepth, L"PrevDepth");
    giDispatch.normal        = m_impl->WrapTexture(normalTexture,   L"GBuffer.Normal");
    giDispatch.historyNormal = m_impl->WrapTexture(m_impl->prevNormals, L"PrevNormal");
    giDispatch.roughness     = m_impl->WrapTexture(normalTexture,   L"GBuffer.Roughness"); // roughness packed in normal.a
    giDispatch.motionVectors = m_impl->WrapTexture(velocityTexture, L"MotionVectors");
    giDispatch.noiseTexture  = m_impl->WrapTexture(m_impl->blueNoiseTexture, L"BlueNoise");
    giDispatch.environmentMap = m_impl->WrapTexture(envMapTexture,  L"EnvMap");
    giDispatch.prevLitOutput  = m_impl->WrapTexture(prevLitTexture, L"PrevLitOutput");

    // Output UAVs
    giDispatch.outputDiffuseGI  = m_impl->WrapTexture(
        m_impl->diffuseGITexture, L"DiffuseGI", FFX_RESOURCE_STATE_UNORDERED_ACCESS);
    giDispatch.outputSpecularGI = m_impl->WrapTexture(
        m_impl->specularGITexture, L"SpecularGI", FFX_RESOURCE_STATE_UNORDERED_ACCESS);

    // Get Brixelizer internal resources for GI ray marching
    FfxBrixelizerRawContext* rawCtx = nullptr;
    ffxBrixelizerGetRawContext(&m_impl->brixelizerCtx, &rawCtx);
    giDispatch.brixelizerContext = rawCtx;

    // Brixelizer internal resources (SDF atlas, brick AABBs, cascade trees/maps)
    // are populated by the raw context. The FFX SDK sample uses
    // ffxBrixelizerRawGetSdfAtlas(), ffxBrixelizerRawGetBrickAABBs(), etc.
    // These internal API functions are resolved at link time from ffx_backend_dx12.lib.
    // Resources left as null-initialized FfxResource{} until the SDK lib is linked.

    // Dispatch GI
    err = ffxBrixelizerGIContextDispatch(&m_impl->giCtx, &giDispatch, ffxCmdList);
    if (err != FFX_OK) {
        UZ_LOG(Renderer, Warning, "BrixelizerGI: ffxBrixelizerGIContextDispatch failed (err=%d)", err);
        return;
    }

    // Store current matrices as prev for next frame
    std::memcpy(m_impl->prevViewMatrix, viewMatrix, sizeof(float) * 16);
    std::memcpy(m_impl->prevProjMatrix, projMatrix, sizeof(float) * 16);
    m_impl->hasPrevMatrices = true;
    m_impl->frameIndex = frameIndex;
}

// ─────────────────────────────────────────────────────────────────────────────
// Resize
// ─────────────────────────────────────────────────────────────────────────────

void BrixelizerGIPass::OnResize(uint32_t newWidth, uint32_t newHeight) {
    if (!m_impl || newWidth == 0 || newHeight == 0) return;
    if (newWidth == m_impl->displayWidth && newHeight == m_impl->displayHeight) return;

    UZ_LOG(Renderer, Info, "BrixelizerGI: resizing to %ux%u", newWidth, newHeight);

    m_impl->displayWidth  = newWidth;
    m_impl->displayHeight = newHeight;

    // Recreate GI context with new resolution
    if (m_impl->giCreated) {
        m_impl->device->WaitIdle();
        ffxBrixelizerGIContextDestroy(&m_impl->giCtx);
        m_impl->giCreated = false;

        FfxBrixelizerGIContextDescription giDesc{};
        giDesc.flags = static_cast<FfxBrixelizerGIFlags>(FFX_BRIXELIZER_GI_FLAG_DEPTH_INVERTED);
        giDesc.internalResolution = FFX_BRIXELIZER_GI_INTERNAL_RESOLUTION_NATIVE;
        giDesc.displaySize.width  = newWidth;
        giDesc.displaySize.height = newHeight;
        giDesc.backendInterface   = m_impl->giBackend;

        FfxErrorCode err = ffxBrixelizerGIContextCreate(&m_impl->giCtx, &giDesc);
        if (err == FFX_OK) {
            m_impl->giCreated = true;
        } else {
            UZ_LOG(Renderer, Error, "BrixelizerGI: failed to recreate GI context on resize (err=%d)", err);
        }
    }

    // Recreate display-resolution textures
    m_impl->DestroyOutputTextures();
    m_impl->CreateOutputTextures();
    m_impl->DestroyHistoryTextures();
    m_impl->CreateHistoryTextures();
}

// ─────────────────────────────────────────────────────────────────────────────
// Accessors
// ─────────────────────────────────────────────────────────────────────────────

uint32_t BrixelizerGIPass::GetDiffuseGISrv() const {
    return m_impl ? m_impl->diffuseGISrv : UINT32_MAX;
}

uint32_t BrixelizerGIPass::GetSpecularGISrv() const {
    return m_impl ? m_impl->specularGISrv : UINT32_MAX;
}

uint32_t BrixelizerGIPass::GetDiffuseGIUav() const {
    return m_impl ? m_impl->diffuseGIUav : UINT32_MAX;
}

uint32_t BrixelizerGIPass::GetSpecularGIUav() const {
    return m_impl ? m_impl->specularGIUav : UINT32_MAX;
}

} // namespace uz

#else // !UZ_BRIXELIZER_ENABLED — no-op stub

namespace uz {

struct BrixelizerGIPass::Impl {};

BrixelizerGIPass::BrixelizerGIPass()  = default;
BrixelizerGIPass::~BrixelizerGIPass() = default;
BrixelizerGIPass::BrixelizerGIPass(BrixelizerGIPass&&) noexcept = default;
BrixelizerGIPass& BrixelizerGIPass::operator=(BrixelizerGIPass&&) noexcept = default;

bool BrixelizerGIPass::Init(IRHIDevice*, uint32_t, uint32_t) {
    UZ_LOG(Renderer, Warning,
        "BrixelizerGI: UZ_BRIXELIZER_ENABLED=0 — FFX SDK 1.1 not linked. "
        "Build the FFX SDK: cd runtime/thirdparty/amd-fidelityfx-1.1/sdk && "
        "cmake -B build -DFFX_API_BACKEND=DX12_X64 -DFFX_ALL=ON && cmake --build build --config Release");
    return false;
}

void BrixelizerGIPass::Shutdown() {}
bool BrixelizerGIPass::IsInitialized() const { return false; }
void BrixelizerGIPass::RegisterGeometry(const std::vector<DrawCommand>&) {}
void BrixelizerGIPass::Execute(IRHICommandList*, const float*, const float*,
    const float*, const float*, float, float, float, uint32_t,
    RHITextureHandle, RHITextureHandle, RHITextureHandle, RHITextureHandle, RHITextureHandle) {}
void BrixelizerGIPass::OnResize(uint32_t, uint32_t) {}
uint32_t BrixelizerGIPass::GetDiffuseGISrv() const { return UINT32_MAX; }
uint32_t BrixelizerGIPass::GetSpecularGISrv() const { return UINT32_MAX; }
uint32_t BrixelizerGIPass::GetDiffuseGIUav() const { return UINT32_MAX; }
uint32_t BrixelizerGIPass::GetSpecularGIUav() const { return UINT32_MAX; }

} // namespace uz

#endif // UZ_BRIXELIZER_ENABLED
