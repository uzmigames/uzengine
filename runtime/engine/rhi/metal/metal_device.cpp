/// Metal Device Implementation
///
/// Initializes the Metal device stack: MTLDevice, GPU family detection,
/// command queues, and argument buffers for bindless resource access.
/// All resource Create/Destroy methods are implemented here.
///
/// Uses Objective-C bridging to interop between the forward-declared C++ MTL::
/// types in headers and the actual Objective-C Metal framework types:
/// - `__bridge_retained` when storing ObjC objects into C++ pointers (retains).
/// - `__bridge` when temporarily casting back for use (no ownership change).
/// - Explicit `release` in destructors to match the `__bridge_retained` retain.
///
/// On non-Apple platforms, all methods return safe defaults (nullptr, Invalid handles, 0).

#include "metal_device.h"
#include "metal_command_list.h"
#include "metal_fence.h"
#include "metal_gpu_profiler.h"
#include "metal_swapchain.h"
#include "metal_tracy_gpu.h"

#include "log.h"
#include "profiler.h"

#include <cstring>

#if defined(__APPLE__)
#include <Metal/Metal.h>
#endif

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Maximum number of textures in the bindless argument buffer.
static constexpr uint32_t kMaxTextureSlots  = 16384;

/// Maximum number of samplers in the bindless argument buffer.
static constexpr uint32_t kMaxSamplerSlots  = 2048;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers (Apple-only)
// ─────────────────────────────────────────────────────────────────────────────

#if defined(__APPLE__)

/// Returns true if the format is a depth or depth-stencil format.
[[maybe_unused]]
static bool IsDepthFormat(RHIFormat format) {
    return format == RHIFormat::D16Unorm || format == RHIFormat::D32Float
        || format == RHIFormat::D24UnormS8Uint || format == RHIFormat::D32FloatS8Uint;
}

/// Maps RHIBlendFactor to MTLBlendFactor.
static MTLBlendFactor ToMTLBlendFactor(RHIBlendFactor factor) {
    switch (factor) {
        case RHIBlendFactor::Zero:       return MTLBlendFactorZero;
        case RHIBlendFactor::One:        return MTLBlendFactorOne;
        case RHIBlendFactor::SrcColor:   return MTLBlendFactorSourceColor;
        case RHIBlendFactor::InvSrcColor:return MTLBlendFactorOneMinusSourceColor;
        case RHIBlendFactor::SrcAlpha:   return MTLBlendFactorSourceAlpha;
        case RHIBlendFactor::InvSrcAlpha:return MTLBlendFactorOneMinusSourceAlpha;
        case RHIBlendFactor::DstColor:   return MTLBlendFactorDestinationColor;
        case RHIBlendFactor::InvDstColor:return MTLBlendFactorOneMinusDestinationColor;
        case RHIBlendFactor::DstAlpha:   return MTLBlendFactorDestinationAlpha;
        case RHIBlendFactor::InvDstAlpha:return MTLBlendFactorOneMinusDestinationAlpha;
        default:                         return MTLBlendFactorOne;
    }
}

/// Maps RHIBlendOp to MTLBlendOperation.
static MTLBlendOperation ToMTLBlendOp(RHIBlendOp op) {
    switch (op) {
        case RHIBlendOp::Add:         return MTLBlendOperationAdd;
        case RHIBlendOp::Subtract:    return MTLBlendOperationSubtract;
        case RHIBlendOp::RevSubtract: return MTLBlendOperationReverseSubtract;
        case RHIBlendOp::Min:         return MTLBlendOperationMin;
        case RHIBlendOp::Max:         return MTLBlendOperationMax;
        default:                      return MTLBlendOperationAdd;
    }
}

/// Maps RHICullMode to MTLCullMode -- stored for draw-time use on the encoder.
/// Not used during PSO creation since Metal sets cull mode on the render encoder.
[[maybe_unused]]
static MTLCullMode ToMTLCullMode(RHICullMode mode) {
    switch (mode) {
        case RHICullMode::None:  return MTLCullModeNone;
        case RHICullMode::Front: return MTLCullModeFront;
        case RHICullMode::Back:  return MTLCullModeBack;
        default:                 return MTLCullModeBack;
    }
}

/// Maps RHIFormat to MTLVertexFormat for SM5 input layout vertex descriptors.
///
/// Only a subset of RHIFormat values are valid for vertex attributes.
/// Returns MTLVertexFormatInvalid for depth/stencil or unsupported formats.
static MTLVertexFormat ToMTLVertexFormat(RHIFormat format) {
    switch (format) {
        case RHIFormat::R32Float:    return MTLVertexFormatFloat;
        case RHIFormat::RG32Float:   return MTLVertexFormatFloat2;
        case RHIFormat::RGB32Float:  return MTLVertexFormatFloat3;
        case RHIFormat::RGBA32Float: return MTLVertexFormatFloat4;
        case RHIFormat::R32Uint:     return MTLVertexFormatUInt;
        case RHIFormat::RG32Uint:    return MTLVertexFormatUInt2;
        case RHIFormat::RGBA32Uint:  return MTLVertexFormatUInt4;
        case RHIFormat::RGBA8Unorm:  return MTLVertexFormatUChar4Normalized;
        case RHIFormat::R8Unorm:     return MTLVertexFormatUCharNormalized;
        case RHIFormat::RG8Unorm:    return MTLVertexFormatUChar2Normalized;
        case RHIFormat::R16Float:    return MTLVertexFormatHalf;
        case RHIFormat::RG16Float:   return MTLVertexFormatHalf2;
        case RHIFormat::RGBA16Float: return MTLVertexFormatHalf4;
        default:
            UZ_LOG(MetalRHI, Warning, "ToMTLVertexFormat: unsupported format {} -- returning Invalid",
                   static_cast<int>(format));
            return MTLVertexFormatInvalid;
    }
}

#endif // __APPLE__

// ─────────────────────────────────────────────────────────────────────────────
// Descriptor Index Allocation
// ─────────────────────────────────────────────────────────────────────────────

uint32_t MetalDevice::AllocateSrvIndex() {
    if (!m_freeSrvIndices.empty()) {
        uint32_t index = m_freeSrvIndices.back();
        m_freeSrvIndices.pop_back();
        return index;
    }
    return m_nextSrvIndex++;
}

void MetalDevice::FreeSrvIndex(uint32_t index) {
    m_freeSrvIndices.push_back(index);
}

uint32_t MetalDevice::AllocateSamplerIndex() {
    if (!m_freeSamplerIndices.empty()) {
        uint32_t index = m_freeSamplerIndices.back();
        m_freeSamplerIndices.pop_back();
        return index;
    }
    return m_nextSamplerIndex++;
}

void MetalDevice::FreeSamplerIndex(uint32_t index) {
    m_freeSamplerIndices.push_back(index);
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

MetalDevice::MetalDevice() = default;

MetalDevice::~MetalDevice() {
    WaitIdle();

    // Destroy GPU profiling contexts before releasing GPU resources
    if (m_tracyGpuCtx) {
        MetalTracyDestroy(m_tracyGpuCtx);
        m_tracyGpuCtx = nullptr;
    }
    if (m_gpuProfiler) {
        m_gpuProfiler->Shutdown();
        m_gpuProfiler.reset();
    }

#if defined(__APPLE__)
    // Release all pooled resources in reverse creation order
    for (auto& buf : m_bufferPool.Slots()) {
        if (buf.buffer) {
            [(__bridge id<MTLBuffer>)buf.buffer release];
            buf.buffer = nullptr;
        }
    }
    for (auto& tex : m_texturePool.Slots()) {
        if (tex.ownsTexture && tex.texture) {
            [(__bridge id<MTLTexture>)tex.texture release];
            tex.texture = nullptr;
        }
    }
    for (auto& shader : m_shaderPool.Slots()) {
        if (shader.function) {
            [(__bridge id<MTLFunction>)shader.function release];
            shader.function = nullptr;
        }
        if (shader.library) {
            [(__bridge id<MTLLibrary>)shader.library release];
            shader.library = nullptr;
        }
    }
    for (auto& sampler : m_samplerPool.Slots()) {
        if (sampler.sampler) {
            [(__bridge id<MTLSamplerState>)sampler.sampler release];
            sampler.sampler = nullptr;
        }
    }
    for (auto& pipeline : m_pipelinePool.Slots()) {
        if (pipeline.renderPipeline) {
            [(__bridge id<MTLRenderPipelineState>)pipeline.renderPipeline release];
            pipeline.renderPipeline = nullptr;
        }
        if (pipeline.computePipeline) {
            [(__bridge id<MTLComputePipelineState>)pipeline.computePipeline release];
            pipeline.computePipeline = nullptr;
        }
    }

    // Release argument buffers
    if (m_textureArgBuffer) {
        [(__bridge id<MTLBuffer>)m_textureArgBuffer release];
        m_textureArgBuffer = nullptr;
    }
    if (m_samplerArgBuffer) {
        [(__bridge id<MTLBuffer>)m_samplerArgBuffer release];
        m_samplerArgBuffer = nullptr;
    }

    // Release command queues
    if (m_transferQueue && m_transferQueue != m_graphicsQueue) {
        [(__bridge id<MTLCommandQueue>)m_transferQueue release];
    }
    m_transferQueue = nullptr;

    if (m_graphicsQueue) {
        [(__bridge id<MTLCommandQueue>)m_graphicsQueue release];
        m_graphicsQueue = nullptr;
    }

    // Release device last
    if (m_device) {
        [(__bridge id<MTLDevice>)m_device release];
        m_device = nullptr;
    }
#endif

    UZ_LOG(MetalRHI, Info, "MetalDevice destroyed");
}

bool MetalDevice::Init(const RHIDeviceDesc& desc) {
    UZ_PROFILE_FUNCTION();
    m_validationEnabled = desc.enableValidation;

    if (!CreateDevice())          return false;
    if (!DetectGPUFamily())       return false;
    if (!CreateCommandQueues())   return false;
    if (!CreateArgumentBuffers()) return false;

    // Initialize GPU profiler (graceful degradation if counter sampling unsupported)
    m_gpuProfiler = std::make_unique<MetalGpuProfiler>();
    if (!m_gpuProfiler->Init(this)) {
        UZ_LOG(MetalRHI, Warning, "GPU profiler initialization failed -- profiling disabled");
        m_gpuProfiler.reset();
    } else if (!m_gpuProfiler->IsSupported()) {
        UZ_LOG(MetalRHI, Info, "GPU profiler: counter sampling not supported on this device");
    }

    // Tracy Metal GPU zones are DISABLED. Tracy's MetalZoneScope modifies
    // sampleBufferAttachments[0] on the render pass descriptor, which causes
    // unrecoverable GPU hangs on Apple Silicon (confirmed on M3 Pro).
    // Tracy CPU profiling (UZ_PROFILE_SCOPE etc.) remains fully functional.
    // The custom MetalGpuProfiler provides GPU timestamps when needed.
    m_tracyGpuCtx = nullptr;

    // Apply forced shader model if requested
    if (desc.forceShaderModel.has_value() && *desc.forceShaderModel < m_shaderModel) {
        UZ_LOG(MetalRHI, Warning, "Shader model forced: {} -> {}",
               ShaderModelToString(m_shaderModel), ShaderModelToString(*desc.forceShaderModel));
        m_shaderModel = *desc.forceShaderModel;
    }

    RHIMemoryBudget budget = GetMemoryBudget();
    UZ_LOG(MetalRHI, Info, "MetalDevice initialized: adapter='{}', family={}, SM={}, VRAM={:.0f}MB, "
           "argBufferTier2={}",
           m_adapterName, m_gpuFamilyStr, ShaderModelToString(m_shaderModel),
           static_cast<double>(budget.budgetBytes) / (1024.0 * 1024.0),
           m_supportsArgumentBuffersTier2);

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Initialization Steps
// ─────────────────────────────────────────────────────────────────────────────

bool MetalDevice::CreateDevice() {
    UZ_PROFILE_FUNCTION();

#if defined(__APPLE__)
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) {
        UZ_LOG(MetalRHI, Error, "MTLCreateSystemDefaultDevice returned nil -- no Metal-capable GPU found");
        return false;
    }

    m_adapterName = [device.name UTF8String];
    m_device = (__bridge_retained MTL::Device*)device;

    UZ_LOG(MetalRHI, Info, "Metal device created: '{}'", m_adapterName);
    return true;
#else
    UZ_LOG(MetalRHI, Error, "Metal backend not available on this platform");
    return false;
#endif
}

bool MetalDevice::DetectGPUFamily() {
    UZ_PROFILE_FUNCTION();

#if defined(__APPLE__)
    id<MTLDevice> device = (__bridge id<MTLDevice>)m_device;
    if (!device) return false;

    // Detect Apple GPU family (Apple Silicon: A-series, M-series).
    // Higher families indicate newer GPU architectures with more features.
    //
    // Apple7 (A14, M1+) -> SM6_6 (full bindless via argument buffers tier 2)
    // Apple6 (A13)       -> SM6_2 (fp16)
    // Apple5 (A12)       -> SM6_0
    // Mac2 (AMD/Intel)   -> SM6_6 if arg buffers tier 2, SM6_0 otherwise
    // Mac1               -> SM6_0
    if ([device supportsFamily:MTLGPUFamilyApple7]) {
        m_gpuFamilyStr = "Apple7+";
        m_shaderModel = ShaderModel::SM6_6;
    } else if ([device supportsFamily:MTLGPUFamilyApple6]) {
        m_gpuFamilyStr = "Apple6";
        m_shaderModel = ShaderModel::SM6_2;
    } else if ([device supportsFamily:MTLGPUFamilyApple5]) {
        m_gpuFamilyStr = "Apple5";
        m_shaderModel = ShaderModel::SM6_0;
    } else if ([device supportsFamily:MTLGPUFamilyMac2]) {
        m_gpuFamilyStr = "Mac2";
        m_shaderModel = ShaderModel::SM6_6;
    } else if ([device supportsFamily:MTLGPUFamilyMac1]) {
        m_gpuFamilyStr = "Mac1";
        m_shaderModel = ShaderModel::SM6_0;
    } else {
        m_gpuFamilyStr = "Unknown";
        m_shaderModel = ShaderModel::SM6_0;
        UZ_LOG(MetalRHI, Warning, "Unknown GPU family -- falling back to SM6.0");
    }

    // Detect argument buffer tier.
    // Tier 2 enables full bindless with arbitrary indexing into argument buffers.
    // Tier 1 has limited access patterns and requires bound resources.
    MTLArgumentBuffersTier tier = device.argumentBuffersSupport;
    m_supportsArgumentBuffersTier2 = (tier == MTLArgumentBuffersTier2);

    // Mac2 without tier 2 cannot do bindless
    if (m_gpuFamilyStr == "Mac2" && !m_supportsArgumentBuffersTier2) {
        m_shaderModel = ShaderModel::SM6_0;
        UZ_LOG(MetalRHI, Warning, "Mac2 GPU without argument buffer tier 2 -- downgrading to SM6.0");
    }

    UZ_LOG(MetalRHI, Info, "GPU family: {}, argumentBuffersTier={}, shaderModel={}",
           m_gpuFamilyStr, m_supportsArgumentBuffersTier2 ? 2 : 1,
           ShaderModelToString(m_shaderModel));
    return true;
#else
    m_gpuFamilyStr = "None";
    m_shaderModel = ShaderModel::SM6_0;
    return false;
#endif
}

bool MetalDevice::CreateCommandQueues() {
    UZ_PROFILE_FUNCTION();

#if defined(__APPLE__)
    id<MTLDevice> device = (__bridge id<MTLDevice>)m_device;
    if (!device) return false;

    // Primary graphics queue -- handles render, compute, and blit commands
    id<MTLCommandQueue> graphicsQueue = [device newCommandQueue];
    if (!graphicsQueue) {
        UZ_LOG(MetalRHI, Error, "Failed to create graphics command queue");
        return false;
    }
    graphicsQueue.label = @"uz.graphics";
    m_graphicsQueue = (__bridge_retained MTL::CommandQueue*)graphicsQueue;

    // Dedicated transfer queue -- for async uploads and readbacks.
    // Metal doesn't have explicit queue families like Vulkan, but separate
    // command queues enable concurrent submission.
    id<MTLCommandQueue> transferQueue = [device newCommandQueue];
    if (!transferQueue) {
        UZ_LOG(MetalRHI, Warning, "Failed to create transfer queue -- using graphics queue for transfers");
        m_transferQueue = m_graphicsQueue;
    } else {
        transferQueue.label = @"uz.transfer";
        m_transferQueue = (__bridge_retained MTL::CommandQueue*)transferQueue;
    }

    UZ_LOG(MetalRHI, Info, "Command queues created (graphics + transfer)");
    return true;
#else
    return false;
#endif
}

bool MetalDevice::CreateArgumentBuffers() {
    UZ_PROFILE_FUNCTION();

#if defined(__APPLE__)
    if (!m_supportsArgumentBuffersTier2) {
        UZ_LOG(MetalRHI, Info, "Argument buffer tier 2 not supported -- skipping bindless setup");
        return true;
    }

    id<MTLDevice> device = (__bridge id<MTLDevice>)m_device;
    if (!device) return false;

    // Texture argument buffer: holds texture references for bindless access.
    // Shaders index into this buffer with `textures[srvIndex]`.
    // Each texture reference is 8 bytes (GPU resource ID).
    NSUInteger textureArgSize = kMaxTextureSlots * 8;
    id<MTLBuffer> texArgBuf = [device newBufferWithLength:textureArgSize
                                                  options:MTLResourceStorageModeShared];
    if (!texArgBuf) {
        UZ_LOG(MetalRHI, Error, "Failed to create texture argument buffer (size={})", textureArgSize);
        return false;
    }
    texArgBuf.label = @"uz.argbuf.textures";
    m_textureArgBuffer = (__bridge_retained MTL::Buffer*)texArgBuf;

    // Sampler argument buffer: holds sampler state references.
    NSUInteger samplerArgSize = kMaxSamplerSlots * 8;
    id<MTLBuffer> samArgBuf = [device newBufferWithLength:samplerArgSize
                                                  options:MTLResourceStorageModeShared];
    if (!samArgBuf) {
        UZ_LOG(MetalRHI, Error, "Failed to create sampler argument buffer (size={})", samplerArgSize);
        return false;
    }
    samArgBuf.label = @"uz.argbuf.samplers";
    m_samplerArgBuffer = (__bridge_retained MTL::Buffer*)samArgBuf;

    UZ_LOG(MetalRHI, Info, "Argument buffers created: textures={} slots, samplers={} slots",
           kMaxTextureSlots, kMaxSamplerSlots);
    return true;
#else
    return true;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Buffer Operations
// ─────────────────────────────────────────────────────────────────────────────

RHIBufferHandle MetalDevice::CreateBuffer(const RHIBufferDesc& desc) {
    UZ_PROFILE_FUNCTION();

#if defined(__APPLE__)
    id<MTLDevice> device = (__bridge id<MTLDevice>)m_device;
    if (!device) {
        UZ_LOG(MetalRHI, Error, "CreateBuffer: device not initialized");
        return RHIBufferHandle::Invalid();
    }

    if (desc.sizeBytes == 0) {
        UZ_LOG(MetalRHI, Error, "CreateBuffer: zero size buffer requested (name='{}')",
               desc.debugName ? desc.debugName : "");
        return RHIBufferHandle::Invalid();
    }

    // Map RHIMemoryType to MTLResourceOptions.
    // Private = GPU-only (fastest GPU access, no CPU mapping).
    // Shared  = CPU and GPU coherent (for Upload/Readback).
    MTLResourceOptions options = 0;
    uint32_t storageMode = ToMTLStorageMode(desc.memory);
    switch (storageMode) {
        case MetalStorageMode::kPrivate:
            options = MTLResourceStorageModePrivate;
            break;
        case MetalStorageMode::kShared:
            options = MTLResourceStorageModeShared;
            break;
        case MetalStorageMode::kManaged:
            options = MTLResourceStorageModeManaged;
            break;
    }

    id<MTLBuffer> buffer = [device newBufferWithLength:desc.sizeBytes options:options];
    if (!buffer) {
        UZ_LOG(MetalRHI, Error, "MTLDevice::newBuffer failed: size={}, storageMode={} (name='{}')",
               desc.sizeBytes, storageMode, desc.debugName ? desc.debugName : "");
        return RHIBufferHandle::Invalid();
    }

    // Debug label
    if (desc.debugName) {
        buffer.label = [NSString stringWithUTF8String:desc.debugName];
    }

    // Allocate pool slot
    uint32_t generation = 0;
    uint32_t index = m_bufferPool.Allocate(generation);
    MetalBuffer* slot = m_bufferPool.Get(index, generation);
    if (!slot) {
        [buffer release];
        UZ_LOG(MetalRHI, Error, "CreateBuffer: pool allocation failed");
        return RHIBufferHandle::Invalid();
    }

    slot->buffer = (__bridge_retained MTL::Buffer*)buffer;
    slot->desc = desc;
    slot->generation = generation;

    // CPU mapping for Shared/Managed storage modes
    if (desc.memory == RHIMemoryType::Upload || desc.memory == RHIMemoryType::Readback) {
        slot->mappedPtr = buffer.contents;
    }

    // GPU address (Metal 3+ / Apple7+)
    if (@available(macOS 13.0, iOS 16.0, *)) {
        slot->gpuAddress = buffer.gpuAddress;
    }

    // Assign logical SRV index for structured buffers
    if (HasFlag(desc.usage, RHIBufferUsage::StructuredBuffer) && desc.stride > 0) {
        slot->srvIndex = AllocateSrvIndex();
    }

    UZ_LOG(MetalRHI, Debug, "CreateBuffer: size={}, stride={}, storageMode={} (name='{}')",
           desc.sizeBytes, desc.stride, storageMode, desc.debugName ? desc.debugName : "");

    return {index, generation};
#else
    (void)desc;
    UZ_LOG(MetalRHI, Error, "CreateBuffer: Metal backend not available on this platform");
    return RHIBufferHandle::Invalid();
#endif
}

void MetalDevice::DestroyBuffer(RHIBufferHandle handle) {
    UZ_PROFILE_FUNCTION();
    MetalBuffer* buf = m_bufferPool.Get(handle.index, handle.generation);
    if (!buf) {
        UZ_LOG(MetalRHI, Warning, "DestroyBuffer: invalid handle (idx={}, gen={})",
               handle.index, handle.generation);
        return;
    }

    // Return SRV index to free list
    if (buf->srvIndex != UINT32_MAX) {
        FreeSrvIndex(buf->srvIndex);
        buf->srvIndex = UINT32_MAX;
    }

#if defined(__APPLE__)
    if (buf->buffer) {
        [(__bridge id<MTLBuffer>)buf->buffer release];
        buf->buffer = nullptr;
    }
#endif

    buf->mappedPtr = nullptr;
    buf->gpuAddress = 0;
    m_bufferPool.Free(handle.index);
}

void* MetalDevice::MapBuffer(RHIBufferHandle handle) {
    MetalBuffer* buf = m_bufferPool.Get(handle.index, handle.generation);
    if (!buf) return nullptr;

    // Persistently mapped for Shared/Managed storage
    if (buf->mappedPtr) return buf->mappedPtr;

    // GPU-only buffers cannot be mapped
    if (buf->desc.memory == RHIMemoryType::Default) return nullptr;

#if defined(__APPLE__)
    // Fallback: query contents pointer
    if (buf->buffer) {
        id<MTLBuffer> mtlBuf = (__bridge id<MTLBuffer>)buf->buffer;
        return mtlBuf.contents;
    }
#endif
    return nullptr;
}

/// No-op on Metal. Shared-storage buffers are persistently mapped.
/// Metal does not require explicit unmap/map pairing.
void MetalDevice::UnmapBuffer(RHIBufferHandle /*handle*/) {
    // Metal Shared/Managed storage buffers are persistently mapped.
    // No unmap operation is needed.
}

uint32_t MetalDevice::GetBufferSRVIndex(RHIBufferHandle handle) const {
    const MetalBuffer* buf = m_bufferPool.Get(handle.index, handle.generation);
    if (!buf) return UINT32_MAX;
    return buf->srvIndex;
}

uint32_t MetalDevice::GetBufferUAVIndex(RHIBufferHandle handle) const {
    // Metal uses argument buffers, not bindless UAV indices
    (void)handle;
    return UINT32_MAX;
}

uint64_t MetalDevice::GetBufferGPUAddress(RHIBufferHandle handle) const {
    const MetalBuffer* buf = m_bufferPool.Get(handle.index, handle.generation);
    if (!buf) return 0;
    return buf->gpuAddress;
}

// ─────────────────────────────────────────────────────────────────────────────
// Texture Operations
// ─────────────────────────────────────────────────────────────────────────────

RHITextureHandle MetalDevice::CreateTexture(const RHITextureDesc& desc) {
    UZ_PROFILE_FUNCTION();

#if defined(__APPLE__)
    id<MTLDevice> device = (__bridge id<MTLDevice>)m_device;
    if (!device) {
        UZ_LOG(MetalRHI, Error, "CreateTexture: device not initialized");
        return RHITextureHandle::Invalid();
    }

    MTLTextureDescriptor* texDesc = [[MTLTextureDescriptor alloc] init];

    // Dimensions
    texDesc.width = desc.width;
    texDesc.height = desc.height;
    texDesc.depth = 1;
    texDesc.mipmapLevelCount = desc.mipLevels;
    texDesc.arrayLength = desc.depthOrArraySize;

    // Pixel format
    texDesc.pixelFormat = static_cast<MTLPixelFormat>(ToMTLPixelFormat(desc.format));

    // Texture type
    if (desc.depthOrArraySize == 6) {
        texDesc.textureType = MTLTextureTypeCube;
    } else if (desc.depthOrArraySize > 1) {
        texDesc.textureType = MTLTextureType2DArray;
    } else {
        texDesc.textureType = MTLTextureType2D;
    }

    // Usage flags
    MTLTextureUsage usage = MTLTextureUsageUnknown;
    if (HasFlag(desc.usage, RHITextureUsage::ShaderResource))  usage |= MTLTextureUsageShaderRead;
    if (HasFlag(desc.usage, RHITextureUsage::RenderTarget))    usage |= MTLTextureUsageRenderTarget;
    if (HasFlag(desc.usage, RHITextureUsage::DepthStencil))    usage |= MTLTextureUsageRenderTarget;
    if (HasFlag(desc.usage, RHITextureUsage::UnorderedAccess)) usage |= MTLTextureUsageShaderWrite;
    texDesc.usage = usage;

    // Storage mode: render targets and depth-stencil are GPU-only (Private).
    // Shader-read-only textures are also Private (data uploaded via blit encoder).
    texDesc.storageMode = MTLStorageModePrivate;

    id<MTLTexture> texture = [device newTextureWithDescriptor:texDesc];
    [texDesc release];

    if (!texture) {
        UZ_LOG(MetalRHI, Error, "MTLDevice::newTexture failed: {}x{}, format={} (name='{}')",
               desc.width, desc.height, static_cast<int>(desc.format),
               desc.debugName ? desc.debugName : "");
        return RHITextureHandle::Invalid();
    }

    // Debug label
    if (desc.debugName) {
        texture.label = [NSString stringWithUTF8String:desc.debugName];
    }

    // Allocate pool slot
    uint32_t generation = 0;
    uint32_t index = m_texturePool.Allocate(generation);
    MetalTexture* slot = m_texturePool.Get(index, generation);
    if (!slot) {
        [texture release];
        UZ_LOG(MetalRHI, Error, "CreateTexture: pool allocation failed");
        return RHITextureHandle::Invalid();
    }

    slot->texture = (__bridge_retained MTL::Texture*)texture;
    slot->desc = desc;
    slot->generation = generation;
    slot->ownsTexture = true;

    // Assign logical descriptor indices
    if (HasFlag(desc.usage, RHITextureUsage::ShaderResource)) {
        slot->srvIndex = AllocateSrvIndex();
    }
    if (HasFlag(desc.usage, RHITextureUsage::RenderTarget)) {
        slot->rtvIndex = AllocateSrvIndex();
    }
    if (HasFlag(desc.usage, RHITextureUsage::DepthStencil)) {
        slot->dsvIndex = AllocateSrvIndex();
    }

    UZ_LOG(MetalRHI, Debug, "CreateTexture: {}x{}, mips={}, format={} (name='{}')",
           desc.width, desc.height, desc.mipLevels, static_cast<int>(desc.format),
           desc.debugName ? desc.debugName : "");

    return {index, generation};
#else
    (void)desc;
    UZ_LOG(MetalRHI, Error, "CreateTexture: Metal backend not available on this platform");
    return RHITextureHandle::Invalid();
#endif
}

void MetalDevice::DestroyTexture(RHITextureHandle handle) {
    UZ_PROFILE_FUNCTION();
    MetalTexture* tex = m_texturePool.Get(handle.index, handle.generation);
    if (!tex) {
        UZ_LOG(MetalRHI, Warning, "DestroyTexture: invalid handle (idx={}, gen={})",
               handle.index, handle.generation);
        return;
    }

    // Return descriptor indices to free lists
    if (tex->srvIndex != UINT32_MAX) {
        FreeSrvIndex(tex->srvIndex);
        tex->srvIndex = UINT32_MAX;
    }
    if (tex->rtvIndex != UINT32_MAX) {
        FreeSrvIndex(tex->rtvIndex);
        tex->rtvIndex = UINT32_MAX;
    }
    if (tex->dsvIndex != UINT32_MAX) {
        FreeSrvIndex(tex->dsvIndex);
        tex->dsvIndex = UINT32_MAX;
    }

#if defined(__APPLE__)
    if (tex->ownsTexture && tex->texture) {
        [(__bridge id<MTLTexture>)tex->texture release];
        tex->texture = nullptr;
    }
#endif

    m_texturePool.Free(handle.index);
}

uint32_t MetalDevice::GetTextureSRVIndex(RHITextureHandle handle) const {
    const MetalTexture* tex = m_texturePool.Get(handle.index, handle.generation);
    if (!tex) return UINT32_MAX;
    return tex->srvIndex;
}

uint32_t MetalDevice::GetTextureUAVIndex(RHITextureHandle /*handle*/) const {
    // Metal UAV (read-write texture) requires MTLTextureUsageShaderWrite and
    // argument buffer binding. Returns UINT32_MAX (no UAV) until Metal compute
    // pipelines are wired with read-write texture descriptors.
    return UINT32_MAX;
}

RHITextureDesc MetalDevice::GetTextureDesc(RHITextureHandle handle) const {
    const MetalTexture* tex = m_texturePool.Get(handle.index, handle.generation);
    if (!tex) return {};
    return tex->desc;
}

// ─────────────────────────────────────────────────────────────────────────────
// Shader Operations
// ─────────────────────────────────────────────────────────────────────────────

RHIShaderHandle MetalDevice::CreateShader(const RHIShaderDesc& desc) {
    UZ_PROFILE_FUNCTION();
    if (!desc.bytecode || desc.bytecodeSize == 0) {
        UZ_LOG(MetalRHI, Error, "CreateShader: null/empty bytecode (name='{}')",
               desc.debugName ? desc.debugName : "");
        return RHIShaderHandle::Invalid();
    }

#if defined(__APPLE__)
    id<MTLDevice> device = (__bridge id<MTLDevice>)m_device;
    if (!device) {
        UZ_LOG(MetalRHI, Error, "CreateShader: device not initialized");
        return RHIShaderHandle::Invalid();
    }

    // Metal receives MSL source text (not binary bytecode like DXIL/SPIR-V).
    // The bytecode pointer contains MSL source as a UTF-8 string.
    NSString* source = [[NSString alloc] initWithBytes:desc.bytecode
                                                length:static_cast<NSUInteger>(desc.bytecodeSize)
                                              encoding:NSUTF8StringEncoding];
    if (!source) {
        UZ_LOG(MetalRHI, Error, "CreateShader: failed to decode MSL source as UTF-8 (name='{}')",
               desc.debugName ? desc.debugName : "");
        return RHIShaderHandle::Invalid();
    }

    // Compile MSL source into a library
    MTLCompileOptions* compileOpts = [[MTLCompileOptions alloc] init];
    compileOpts.fastMathEnabled = YES;
    compileOpts.languageVersion = MTLLanguageVersion3_0;

    NSError* error = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:source options:compileOpts error:&error];
    [compileOpts release];
    [source release];

    if (!library) {
        NSString* errorStr = error ? [error localizedDescription] : @"unknown error";
        UZ_LOG(MetalRHI, Error, "CreateShader: MSL compilation failed (name='{}'): {}",
               desc.debugName ? desc.debugName : "",
               [errorStr UTF8String]);
        return RHIShaderHandle::Invalid();
    }

    // Extract the entry point function.
    // SPIRV-Cross generates "main0" as the default entry point for MSL.
    const char* entryName = desc.entryPoint ? desc.entryPoint : "main0";
    NSString* entryPoint = [NSString stringWithUTF8String:entryName];
    id<MTLFunction> function = [library newFunctionWithName:entryPoint];
    if (!function) {
        UZ_LOG(MetalRHI, Error, "CreateShader: entry point '{}' not found in MSL library (name='{}')",
               entryName, desc.debugName ? desc.debugName : "");
        [library release];
        return RHIShaderHandle::Invalid();
    }

    // Allocate pool slot
    uint32_t generation = 0;
    uint32_t index = m_shaderPool.Allocate(generation);
    MetalShader* slot = m_shaderPool.Get(index, generation);
    if (!slot) {
        [function release];
        [library release];
        UZ_LOG(MetalRHI, Error, "CreateShader: pool allocation failed");
        return RHIShaderHandle::Invalid();
    }

    slot->function = (__bridge_retained MTL::Function*)function;
    slot->library = (__bridge_retained MTL::Library*)library;
    slot->stage = desc.stage;
    slot->generation = generation;

    UZ_LOG(MetalRHI, Debug, "CreateShader: stage={}, entry='{}', size={} (name='{}')",
           static_cast<int>(desc.stage), entryName,
           desc.bytecodeSize, desc.debugName ? desc.debugName : "");

    return {index, generation};
#else
    (void)desc;
    UZ_LOG(MetalRHI, Error, "CreateShader: Metal backend not available on this platform");
    return RHIShaderHandle::Invalid();
#endif
}

void MetalDevice::DestroyShader(RHIShaderHandle handle) {
    UZ_PROFILE_FUNCTION();
    MetalShader* shader = m_shaderPool.Get(handle.index, handle.generation);
    if (!shader) {
        UZ_LOG(MetalRHI, Warning, "DestroyShader: invalid handle (idx={}, gen={})",
               handle.index, handle.generation);
        return;
    }

#if defined(__APPLE__)
    if (shader->function) {
        [(__bridge id<MTLFunction>)shader->function release];
        shader->function = nullptr;
    }
    if (shader->library) {
        [(__bridge id<MTLLibrary>)shader->library release];
        shader->library = nullptr;
    }
#endif

    m_shaderPool.Free(handle.index);
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline Operations
// ─────────────────────────────────────────────────────────────────────────────

RHIPipelineHandle MetalDevice::CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) {
    UZ_PROFILE_FUNCTION();

#if defined(__APPLE__)
    id<MTLDevice> device = (__bridge id<MTLDevice>)m_device;
    if (!device) {
        UZ_LOG(MetalRHI, Error, "CreateGraphicsPipeline: device not initialized");
        return RHIPipelineHandle::Invalid();
    }

    // Validate vertex shader
    MetalShader* vsShader = m_shaderPool.Get(desc.vertexShader.index, desc.vertexShader.generation);
    if (!vsShader || !vsShader->function) {
        UZ_LOG(MetalRHI, Error, "CreateGraphicsPipeline: invalid vertex shader");
        return RHIPipelineHandle::Invalid();
    }

    MTLRenderPipelineDescriptor* pipelineDesc = [[MTLRenderPipelineDescriptor alloc] init];

    // Shader functions
    pipelineDesc.vertexFunction = (__bridge id<MTLFunction>)vsShader->function;

    if (desc.pixelShader.IsValid()) {
        MetalShader* psShader = m_shaderPool.Get(desc.pixelShader.index, desc.pixelShader.generation);
        if (psShader && psShader->function) {
            pipelineDesc.fragmentFunction = (__bridge id<MTLFunction>)psShader->function;
        }
    }

    // Color attachment formats and blend state
    for (uint32_t i = 0; i < desc.renderTargetCount; ++i) {
        MTLPixelFormat format = static_cast<MTLPixelFormat>(ToMTLPixelFormat(desc.renderTargetFormats[i]));
        pipelineDesc.colorAttachments[i].pixelFormat = format;

        const auto& blend = desc.renderTargetBlend[i];
        pipelineDesc.colorAttachments[i].blendingEnabled = blend.blendEnable ? YES : NO;
        if (blend.blendEnable) {
            pipelineDesc.colorAttachments[i].sourceRGBBlendFactor      = ToMTLBlendFactor(blend.srcColor);
            pipelineDesc.colorAttachments[i].destinationRGBBlendFactor = ToMTLBlendFactor(blend.dstColor);
            pipelineDesc.colorAttachments[i].rgbBlendOperation         = ToMTLBlendOp(blend.colorOp);
            pipelineDesc.colorAttachments[i].sourceAlphaBlendFactor    = ToMTLBlendFactor(blend.srcAlpha);
            pipelineDesc.colorAttachments[i].destinationAlphaBlendFactor = ToMTLBlendFactor(blend.dstAlpha);
            pipelineDesc.colorAttachments[i].alphaBlendOperation       = ToMTLBlendOp(blend.alphaOp);
        }
        pipelineDesc.colorAttachments[i].writeMask = MTLColorWriteMaskAll;
    }

    // Depth attachment format
    if (desc.depthStencilFormat != RHIFormat::Unknown) {
        pipelineDesc.depthAttachmentPixelFormat =
            static_cast<MTLPixelFormat>(ToMTLPixelFormat(desc.depthStencilFormat));

        // Stencil attachment format (same as depth for combined depth-stencil formats)
        if (desc.depthStencilFormat == RHIFormat::D24UnormS8Uint
            || desc.depthStencilFormat == RHIFormat::D32FloatS8Uint) {
            pipelineDesc.stencilAttachmentPixelFormat =
                static_cast<MTLPixelFormat>(ToMTLPixelFormat(desc.depthStencilFormat));
        }
    }

    // Vertex descriptor (SM5 fallback path with input assembler)
    if (desc.inputElementCount > 0 && desc.inputElements) {
        MTLVertexDescriptor* vertexDesc = [[MTLVertexDescriptor alloc] init];

        for (uint32_t i = 0; i < desc.inputElementCount; ++i) {
            const auto& elem = desc.inputElements[i];
            vertexDesc.attributes[i].format = ToMTLVertexFormat(elem.format);
            vertexDesc.attributes[i].offset = elem.byteOffset;
            vertexDesc.attributes[i].bufferIndex = elem.inputSlot;
        }

        if (desc.vertexStride > 0) {
            vertexDesc.layouts[0].stride = desc.vertexStride;
            vertexDesc.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
            vertexDesc.layouts[0].stepRate = 1;
        }

        pipelineDesc.vertexDescriptor = vertexDesc;
        [vertexDesc release];
    }

    // Debug label
    if (desc.debugName) {
        pipelineDesc.label = [NSString stringWithUTF8String:desc.debugName];
    }

    // Create the render pipeline state
    NSError* error = nil;
    id<MTLRenderPipelineState> pipelineState =
        [device newRenderPipelineStateWithDescriptor:pipelineDesc error:&error];
    [pipelineDesc release];

    if (!pipelineState) {
        NSString* errorStr = error ? [error localizedDescription] : @"unknown error";
        UZ_LOG(MetalRHI, Error, "CreateGraphicsPipeline: MTLRenderPipelineState creation failed "
               "(name='{}'): {}", desc.debugName ? desc.debugName : "", [errorStr UTF8String]);
        return RHIPipelineHandle::Invalid();
    }

    // Allocate pool slot
    uint32_t generation = 0;
    uint32_t index = m_pipelinePool.Allocate(generation);
    MetalPipeline* slot = m_pipelinePool.Get(index, generation);
    if (!slot) {
        [pipelineState release];
        UZ_LOG(MetalRHI, Error, "CreateGraphicsPipeline: pool allocation failed");
        return RHIPipelineHandle::Invalid();
    }

    slot->renderPipeline = (__bridge_retained MTL::RenderPipelineState*)pipelineState;
    slot->computePipeline = nullptr;
    slot->topology = desc.topology;
    slot->generation = generation;
    slot->desc = desc;

    UZ_LOG(MetalRHI, Debug, "CreateGraphicsPipeline: {} RT(s) (name='{}')",
           desc.renderTargetCount, desc.debugName ? desc.debugName : "");

    return {index, generation};
#else
    (void)desc;
    UZ_LOG(MetalRHI, Error, "CreateGraphicsPipeline: Metal backend not available on this platform");
    return RHIPipelineHandle::Invalid();
#endif
}

RHIPipelineHandle MetalDevice::CreateComputePipeline(const RHIComputePipelineDesc& desc) {
    UZ_LOG(MetalRHI, Warning, "CreateComputePipeline: not yet implemented in Metal backend (name='{}')",
           desc.debugName ? desc.debugName : "");
    return RHIPipelineHandle::Invalid();
}

void MetalDevice::DestroyPipeline(RHIPipelineHandle handle) {
    UZ_PROFILE_FUNCTION();
    MetalPipeline* pipeline = m_pipelinePool.Get(handle.index, handle.generation);
    if (!pipeline) {
        UZ_LOG(MetalRHI, Warning, "DestroyPipeline: invalid handle (idx={}, gen={})",
               handle.index, handle.generation);
        return;
    }

#if defined(__APPLE__)
    if (pipeline->renderPipeline) {
        [(__bridge id<MTLRenderPipelineState>)pipeline->renderPipeline release];
        pipeline->renderPipeline = nullptr;
    }
    if (pipeline->computePipeline) {
        [(__bridge id<MTLComputePipelineState>)pipeline->computePipeline release];
        pipeline->computePipeline = nullptr;
    }
#endif

    m_pipelinePool.Free(handle.index);
}

// ─────────────────────────────────────────────────────────────────────────────
// Sampler Operations
// ─────────────────────────────────────────────────────────────────────────────

RHISamplerHandle MetalDevice::CreateSampler(const RHISamplerDesc& desc) {
    UZ_PROFILE_FUNCTION();

#if defined(__APPLE__)
    id<MTLDevice> device = (__bridge id<MTLDevice>)m_device;
    if (!device) {
        UZ_LOG(MetalRHI, Error, "CreateSampler: device not initialized");
        return RHISamplerHandle::Invalid();
    }

    MTLSamplerDescriptor* samplerDesc = [[MTLSamplerDescriptor alloc] init];

    // Filter modes
    samplerDesc.minFilter = static_cast<MTLSamplerMinMagFilter>(ToMTLSamplerMinMagFilter(desc.filter));
    samplerDesc.magFilter = static_cast<MTLSamplerMinMagFilter>(ToMTLSamplerMinMagFilter(desc.filter));

    // Mip filter
    switch (desc.filter) {
        case RHIFilterMode::Point:
            samplerDesc.mipFilter = MTLSamplerMipFilterNearest;
            break;
        case RHIFilterMode::Linear:
        case RHIFilterMode::Anisotropic:
            samplerDesc.mipFilter = MTLSamplerMipFilterLinear;
            break;
    }

    // Anisotropy
    if (desc.filter == RHIFilterMode::Anisotropic) {
        samplerDesc.maxAnisotropy = desc.maxAnisotropy;
    }

    // Address modes
    samplerDesc.sAddressMode = static_cast<MTLSamplerAddressMode>(ToMTLSamplerAddressMode(desc.addressU));
    samplerDesc.tAddressMode = static_cast<MTLSamplerAddressMode>(ToMTLSamplerAddressMode(desc.addressV));
    samplerDesc.rAddressMode = static_cast<MTLSamplerAddressMode>(ToMTLSamplerAddressMode(desc.addressW));

    // LOD range
    samplerDesc.lodMinClamp = desc.minLod;
    samplerDesc.lodMaxClamp = desc.maxLod;

    // Compare function
    if (desc.compareFunc != RHICompareFunc::Never) {
        samplerDesc.compareFunction =
            static_cast<MTLCompareFunction>(ToMTLCompareFunction(desc.compareFunc));
    }

    // Debug label
    if (desc.debugName) {
        samplerDesc.label = [NSString stringWithUTF8String:desc.debugName];
    }

    id<MTLSamplerState> sampler = [device newSamplerStateWithDescriptor:samplerDesc];
    [samplerDesc release];

    if (!sampler) {
        UZ_LOG(MetalRHI, Error, "MTLDevice::newSamplerState failed (name='{}')",
               desc.debugName ? desc.debugName : "");
        return RHISamplerHandle::Invalid();
    }

    // Allocate pool slot
    uint32_t generation = 0;
    uint32_t index = m_samplerPool.Allocate(generation);
    MetalSampler* slot = m_samplerPool.Get(index, generation);
    if (!slot) {
        [sampler release];
        UZ_LOG(MetalRHI, Error, "CreateSampler: pool allocation failed");
        return RHISamplerHandle::Invalid();
    }

    slot->sampler = (__bridge_retained MTL::SamplerState*)sampler;
    slot->samplerIndex = AllocateSamplerIndex();
    slot->generation = generation;

    UZ_LOG(MetalRHI, Debug, "CreateSampler: index={} (name='{}')",
           slot->samplerIndex, desc.debugName ? desc.debugName : "");

    return {index, generation};
#else
    (void)desc;
    UZ_LOG(MetalRHI, Error, "CreateSampler: Metal backend not available on this platform");
    return RHISamplerHandle::Invalid();
#endif
}

void MetalDevice::DestroySampler(RHISamplerHandle handle) {
    UZ_PROFILE_FUNCTION();
    MetalSampler* sampler = m_samplerPool.Get(handle.index, handle.generation);
    if (!sampler) {
        UZ_LOG(MetalRHI, Warning, "DestroySampler: invalid handle (idx={}, gen={})",
               handle.index, handle.generation);
        return;
    }

    // Return sampler index to free list
    if (sampler->samplerIndex != UINT32_MAX) {
        FreeSamplerIndex(sampler->samplerIndex);
        sampler->samplerIndex = UINT32_MAX;
    }

#if defined(__APPLE__)
    if (sampler->sampler) {
        [(__bridge id<MTLSamplerState>)sampler->sampler release];
        sampler->sampler = nullptr;
    }
#endif

    m_samplerPool.Free(handle.index);
}

uint32_t MetalDevice::GetSamplerIndex(RHISamplerHandle handle) const {
    const MetalSampler* sampler = m_samplerPool.Get(handle.index, handle.generation);
    if (!sampler) return UINT32_MAX;
    return sampler->samplerIndex;
}

// ─────────────────────────────────────────────────────────────────────────────
// Swap Chain Operations
// ─────────────────────────────────────────────────────────────────────────────

IRHISwapChain* MetalDevice::CreateSwapChain(const RHISwapChainDesc& desc) {
    UZ_PROFILE_FUNCTION();

    auto* swapChain = new MetalSwapChain();
    if (!swapChain->Init(this, desc)) {
        UZ_LOG(MetalRHI, Error, "MetalDevice::CreateSwapChain -- Init failed");
        delete swapChain;
        return nullptr;
    }

    return swapChain;
}

void MetalDevice::DestroySwapChain(IRHISwapChain* swapChain) {
    UZ_PROFILE_FUNCTION();
    delete swapChain;
}

// ─────────────────────────────────────────────────────────────────────────────
// Fence Operations
// ─────────────────────────────────────────────────────────────────────────────

IRHIFence* MetalDevice::CreateFence() {
    auto* fence = new MetalFence();
    if (!fence->Init(this)) {
        UZ_LOG(MetalRHI, Error, "MetalFence::Init failed");
        delete fence;
        return nullptr;
    }
    return fence;
}

void MetalDevice::DestroyFence(IRHIFence* fence) {
    delete fence;
}

// ─────────────────────────────────────────────────────────────────────────────
// Command List Operations
// ─────────────────────────────────────────────────────────────────────────────

IRHICommandList* MetalDevice::CreateCommandList(RHIQueueType queue) {
    UZ_PROFILE_FUNCTION();

    auto* cmdList = new MetalCommandList();
    if (!cmdList->Init(this, queue)) {
        UZ_LOG(MetalRHI, Error, "MetalDevice::CreateCommandList -- Init failed");
        delete cmdList;
        return nullptr;
    }
    return cmdList;
}

void MetalDevice::DestroyCommandList(IRHICommandList* cmdList) {
    delete cmdList;
}

// ─────────────────────────────────────────────────────────────────────────────
// Synchronization
// ─────────────────────────────────────────────────────────────────────────────

void MetalDevice::WaitIdle() {
    UZ_PROFILE_FUNCTION();

#if defined(__APPLE__)
    if (!m_graphicsQueue) return;

    // Submit an empty command buffer and wait for completion.
    // This stalls the GPU pipeline completely -- use sparingly.
    id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)m_graphicsQueue;
    id<MTLCommandBuffer> cmdBuf = [queue commandBuffer];
    if (cmdBuf) {
        [cmdBuf commit];
        [cmdBuf waitUntilCompleted];
    }
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Device Queries
// ─────────────────────────────────────────────────────────────────────────────

const char* MetalDevice::GetAdapterName() const {
    return m_adapterName.c_str();
}

const char* MetalDevice::GetFeatureLevel() const {
    return m_gpuFamilyStr.c_str();
}

RHIMemoryBudget MetalDevice::GetMemoryBudget() const {
#if defined(__APPLE__)
    if (!m_device) return {};

    id<MTLDevice> device = (__bridge id<MTLDevice>)m_device;

    RHIMemoryBudget budget = {};
    budget.budgetBytes = device.recommendedMaxWorkingSetSize;
    budget.usageBytes = device.currentAllocatedSize;
    return budget;
#else
    return {};
#endif
}

} // namespace uz
