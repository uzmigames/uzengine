/// Metal Device Implementation — Lifecycle, Init, Shutdown, Queries
///
/// Initializes the Metal device stack: MTLDevice, GPU family detection,
/// command queues, and argument buffers for bindless resource access.
/// Resource Create/Destroy methods are in metal_resource_creation.cpp.
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
