/// Metal Command List Implementation
///
/// Records GPU commands via Metal's encoder-based model. Automatically manages
/// encoder transitions (render/compute/blit) as needed by each command.
/// Command buffers are obtained from the device's graphics queue and committed
/// on Execute(). Triple-buffered rotation via m_frameIndex.

#include "metal_command_list.h"
#include "metal_device.h"
#include "metal_gpu_profiler.h"
#include "metal_resources.h"
#include "metal_tracy_gpu.h"

#include "log.h"
#include "profiler.h"

#if defined(__APPLE__)
#include <Metal/Metal.h>
#endif

#include <cstring>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Returns true if the format is a depth or depth-stencil format.
static bool IsDepthFormat(RHIFormat format) {
    return format == RHIFormat::D16Unorm || format == RHIFormat::D32Float
        || format == RHIFormat::D24UnormS8Uint || format == RHIFormat::D32FloatS8Uint;
}

/// Returns true if the format includes a stencil component.
static bool HasStencil(RHIFormat format) {
    return format == RHIFormat::D24UnormS8Uint || format == RHIFormat::D32FloatS8Uint;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

MetalCommandList::~MetalCommandList() {
#if defined(__APPLE__)
    if (m_commandBuffer) {
        // Release retained command buffer if it was never committed
        CFRelease((__bridge CFTypeRef)((__bridge id<MTLCommandBuffer>)m_commandBuffer));
        m_commandBuffer = nullptr;
    }
#endif
    UZ_LOG(MetalRHI, Info, "MetalCommandList destroyed");
}

bool MetalCommandList::Init(MetalDevice* device, RHIQueueType queueType) {
    UZ_PROFILE_FUNCTION();
    if (!device) {
        UZ_LOG(MetalRHI, Error, "MetalCommandList::Init -- null device");
        return false;
    }

#if defined(__APPLE__)
    m_device = device;
    m_queueType = queueType;
    m_gpuProfiler = device->GetGpuProfiler();

    UZ_LOG(MetalRHI, Info, "MetalCommandList created (queue={})", static_cast<int>(queueType));
    return true;
#else
    UZ_LOG(MetalRHI, Error, "MetalCommandList::Init -- Metal backend not available on this platform");
    return false;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Recording Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void MetalCommandList::Begin() {
    UZ_PROFILE_FUNCTION();

#if defined(__APPLE__)
    if (!m_device) {
        UZ_LOG(MetalRHI, Error, "MetalCommandList::Begin -- no device");
        return;
    }

    // Release any previously retained command buffer that was not committed
    if (m_commandBuffer) {
        CFRelease((__bridge CFTypeRef)((__bridge id<MTLCommandBuffer>)m_commandBuffer));
        m_commandBuffer = nullptr;
    }

    // Acquire a new command buffer from the graphics queue
    id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)m_device->GetGraphicsQueue();
    id<MTLCommandBuffer> cmdBuf = [queue commandBuffer];
    if (!cmdBuf) {
        UZ_LOG(MetalRHI, Error, "MetalCommandList::Begin -- failed to create command buffer");
        return;
    }

    m_commandBuffer = (__bridge_retained MTL::CommandBuffer*)cmdBuf;
    m_activeEncoder = MetalEncoderType::None;
    m_renderEncoder = nullptr;
    m_computeEncoder = nullptr;
    m_blitEncoder = nullptr;
    m_inRenderPass = false;
#endif
}

void MetalCommandList::End() {
    UZ_PROFILE_FUNCTION();

#if defined(__APPLE__)
    // End any active encoder before closing the command buffer
    EndCurrentEncoder();
#endif
}

void MetalCommandList::Execute() {
    UZ_PROFILE_FUNCTION();

#if defined(__APPLE__)
    if (!m_commandBuffer) {
        UZ_LOG(MetalRHI, Error, "MetalCommandList::Execute -- no command buffer");
        return;
    }

    id<MTLCommandBuffer> cmdBuf = (__bridge id<MTLCommandBuffer>)m_commandBuffer;
    [cmdBuf commit];

    // Release our retained reference — the Metal runtime owns it now
    CFRelease((__bridge CFTypeRef)cmdBuf);
    m_commandBuffer = nullptr;

    // Tracy GPU collect — no-op while Tracy GPU zones are disabled.
    // See BeginRenderPass comment for rationale.
    if (m_device->GetTracyGpuContext()) {
        MetalTracyCollect(m_device->GetTracyGpuContext());
    }

    // Rotate to next frame
    m_frameIndex = (m_frameIndex + 1) % kMetalFrameCount;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Encoder Management
// ─────────────────────────────────────────────────────────────────────────────

void MetalCommandList::EndCurrentEncoder() {
#if defined(__APPLE__)
    switch (m_activeEncoder) {
        case MetalEncoderType::Render: {
            if (m_renderEncoder) {
                id<MTLRenderCommandEncoder> enc =
                    (__bridge id<MTLRenderCommandEncoder>)m_renderEncoder;
                [enc endEncoding];
                [enc release]; // Balance the retain in BeginRenderPass
                m_renderEncoder = nullptr;
            }
            break;
        }
        case MetalEncoderType::Compute: {
            if (m_computeEncoder) {
                id<MTLComputeCommandEncoder> enc =
                    (__bridge id<MTLComputeCommandEncoder>)m_computeEncoder;
                [enc endEncoding];
                [enc release]; // Balance the retain in EnsureComputeEncoder
                m_computeEncoder = nullptr;
            }
            break;
        }
        case MetalEncoderType::Blit: {
            if (m_blitEncoder) {
                id<MTLBlitCommandEncoder> enc =
                    (__bridge id<MTLBlitCommandEncoder>)m_blitEncoder;
                [enc endEncoding];
                [enc release]; // Balance the retain in EnsureBlitEncoder
                m_blitEncoder = nullptr;
            }
            break;
        }
        case MetalEncoderType::None:
            break;
    }
    m_activeEncoder = MetalEncoderType::None;
#endif
}

void MetalCommandList::EnsureBlitEncoder() {
#if defined(__APPLE__)
    if (m_activeEncoder == MetalEncoderType::Blit && m_blitEncoder) {
        return;
    }

    EndCurrentEncoder();

    if (!m_commandBuffer) {
        UZ_LOG(MetalRHI, Error, "EnsureBlitEncoder -- no command buffer");
        return;
    }

    id<MTLCommandBuffer> cmdBuf = (__bridge id<MTLCommandBuffer>)m_commandBuffer;
    id<MTLBlitCommandEncoder> enc = [cmdBuf blitCommandEncoder];
    if (!enc) {
        UZ_LOG(MetalRHI, Error, "EnsureBlitEncoder -- failed to create blit encoder");
        return;
    }

    [enc retain]; // Prevent premature autorelease deallocation
    m_blitEncoder = (__bridge MTL::BlitCommandEncoder*)enc;
    m_activeEncoder = MetalEncoderType::Blit;
#endif
}

void MetalCommandList::EnsureComputeEncoder() {
#if defined(__APPLE__)
    if (m_activeEncoder == MetalEncoderType::Compute && m_computeEncoder) {
        return;
    }

    EndCurrentEncoder();

    if (!m_commandBuffer) {
        UZ_LOG(MetalRHI, Error, "EnsureComputeEncoder -- no command buffer");
        return;
    }

    id<MTLCommandBuffer> cmdBuf = (__bridge id<MTLCommandBuffer>)m_commandBuffer;
    id<MTLComputeCommandEncoder> enc = [cmdBuf computeCommandEncoder];
    if (!enc) {
        UZ_LOG(MetalRHI, Error, "EnsureComputeEncoder -- failed to create compute encoder");
        return;
    }

    [enc retain]; // Prevent premature autorelease deallocation
    m_computeEncoder = (__bridge MTL::ComputeCommandEncoder*)enc;
    m_activeEncoder = MetalEncoderType::Compute;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline and Resource Binding
// ─────────────────────────────────────────────────────────────────────────────

void MetalCommandList::SetPipeline(RHIPipelineHandle pipeline) {
    UZ_PROFILE_FUNCTION();

    if (!pipeline.IsValid()) {
        UZ_LOG(MetalRHI, Warning, "SetPipeline -- invalid handle");
        return;
    }

#if defined(__APPLE__)
    auto* mtlPipeline = m_device->GetPipelinePool().Get(pipeline.index, pipeline.generation);
    if (!mtlPipeline) {
        UZ_LOG(MetalRHI, Warning, "SetPipeline -- pipeline not found in pool");
        return;
    }

    m_currentPipeline = pipeline;

    if (mtlPipeline->renderPipeline) {
        // Graphics pipeline — requires an active render encoder
        if (m_activeEncoder != MetalEncoderType::Render || !m_renderEncoder) {
            UZ_LOG(MetalRHI, Warning, "SetPipeline -- no active render encoder; "
                   "call BeginRenderPass() before SetPipeline() for graphics pipelines");
            return;
        }

        id<MTLRenderCommandEncoder> enc =
            (__bridge id<MTLRenderCommandEncoder>)m_renderEncoder;
        id<MTLRenderPipelineState> pso =
            (__bridge id<MTLRenderPipelineState>)mtlPipeline->renderPipeline;
        [enc setRenderPipelineState:pso];

        // Metal does not bake depth-stencil state into the PSO — set it here
        const auto& dsDesc = mtlPipeline->desc.depthStencil;
        if (dsDesc.depthEnable) {
            // Build depth-stencil descriptor at draw time
            MTLDepthStencilDescriptor* dsDescriptor = [[MTLDepthStencilDescriptor alloc] init];
            dsDescriptor.depthCompareFunction =
                static_cast<MTLCompareFunction>(ToMTLCompareFunction(dsDesc.depthFunc));
            dsDescriptor.depthWriteEnabled = dsDesc.depthWrite ? YES : NO;

            id<MTLDevice> mtlDevice = (__bridge id<MTLDevice>)m_device->GetMTLDevice();
            id<MTLDepthStencilState> dsState =
                [mtlDevice newDepthStencilStateWithDescriptor:dsDescriptor];
            [enc setDepthStencilState:dsState];
            [dsDescriptor release];
            [dsState release];
        }

        // Set cull mode from the pipeline's rasterizer state
        const auto& rasterDesc = mtlPipeline->desc.rasterizer;
        MTLCullMode cullMode = MTLCullModeNone;
        switch (rasterDesc.cullMode) {
            case RHICullMode::None:  cullMode = MTLCullModeNone;  break;
            case RHICullMode::Front: cullMode = MTLCullModeFront; break;
            case RHICullMode::Back:  cullMode = MTLCullModeBack;  break;
        }
        [enc setCullMode:cullMode];

        // Set winding order
        MTLWinding winding = rasterDesc.frontCounterClockwise
            ? MTLWindingCounterClockwise
            : MTLWindingClockwise;
        [enc setFrontFacingWinding:winding];

        // Set fill mode
        MTLTriangleFillMode fillMode = (rasterDesc.fillMode == RHIFillMode::Wireframe)
            ? MTLTriangleFillModeLines
            : MTLTriangleFillModeFill;
        [enc setTriangleFillMode:fillMode];

        // Set depth bias if non-zero
        if (rasterDesc.depthBias != 0.0f || rasterDesc.slopeScaledDepthBias != 0.0f) {
            [enc setDepthBias:rasterDesc.depthBias
                   slopeScale:rasterDesc.slopeScaledDepthBias
                   clamp:0.0f];
        }

    } else if (mtlPipeline->computePipeline) {
        // Compute pipeline — ensure compute encoder
        EnsureComputeEncoder();
        if (!m_computeEncoder) return;

        id<MTLComputeCommandEncoder> enc =
            (__bridge id<MTLComputeCommandEncoder>)m_computeEncoder;
        id<MTLComputePipelineState> pso =
            (__bridge id<MTLComputePipelineState>)mtlPipeline->computePipeline;
        [enc setComputePipelineState:pso];
    }
#endif
}

void MetalCommandList::SetVertexBuffer(uint32_t slot, RHIBufferHandle buffer, uint32_t offset) {
    if (!buffer.IsValid()) return;

#if defined(__APPLE__)
    auto* mtlBuffer = m_device->GetBufferPool().Get(buffer.index, buffer.generation);
    if (!mtlBuffer || !mtlBuffer->buffer) {
        UZ_LOG(MetalRHI, Warning, "SetVertexBuffer -- buffer not found in pool");
        return;
    }

    if (m_activeEncoder != MetalEncoderType::Render || !m_renderEncoder) {
        UZ_LOG(MetalRHI, Warning, "SetVertexBuffer -- no active render encoder");
        return;
    }

    id<MTLRenderCommandEncoder> enc =
        (__bridge id<MTLRenderCommandEncoder>)m_renderEncoder;
    id<MTLBuffer> buf = (__bridge id<MTLBuffer>)mtlBuffer->buffer;
    [enc setVertexBuffer:buf offset:offset atIndex:slot];
#endif
}

void MetalCommandList::SetIndexBuffer(RHIBufferHandle buffer, RHIIndexFormat format,
                                       uint32_t offset) {
    // Metal binds the index buffer inline with drawIndexedPrimitives,
    // so we cache the parameters and apply them at DrawIndexed() time.
    m_currentIndexBuffer = buffer;
    m_indexBufferOffset = offset;
    m_indexType = (format == RHIIndexFormat::Uint16) ? 0 : 1;
}

void MetalCommandList::SetViewport(const RHIViewport& viewport) {
#if defined(__APPLE__)
    if (m_activeEncoder != MetalEncoderType::Render || !m_renderEncoder) {
        UZ_LOG(MetalRHI, Warning, "SetViewport -- no active render encoder");
        return;
    }

    id<MTLRenderCommandEncoder> enc =
        (__bridge id<MTLRenderCommandEncoder>)m_renderEncoder;

    MTLViewport vp = {};
    vp.originX = static_cast<double>(viewport.x);
    vp.originY = static_cast<double>(viewport.y);
    vp.width = static_cast<double>(viewport.width);
    vp.height = static_cast<double>(viewport.height);
    vp.znear = static_cast<double>(viewport.minDepth);
    vp.zfar = static_cast<double>(viewport.maxDepth);
    [enc setViewport:vp];
#endif
}

void MetalCommandList::SetScissor(const RHIScissorRect& scissor) {
#if defined(__APPLE__)
    if (m_activeEncoder != MetalEncoderType::Render || !m_renderEncoder) {
        UZ_LOG(MetalRHI, Warning, "SetScissor -- no active render encoder");
        return;
    }

    id<MTLRenderCommandEncoder> enc =
        (__bridge id<MTLRenderCommandEncoder>)m_renderEncoder;

    MTLScissorRect rect = {};
    rect.x = static_cast<NSUInteger>(scissor.left);
    rect.y = static_cast<NSUInteger>(scissor.top);
    rect.width = static_cast<NSUInteger>(scissor.right - scissor.left);
    rect.height = static_cast<NSUInteger>(scissor.bottom - scissor.top);
    [enc setScissorRect:rect];
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Descriptor / Root Parameter Binding
// ─────────────────────────────────────────────────────────────────────────────

void MetalCommandList::SetDescriptorIndex(uint32_t rootParamIndex, uint32_t descriptorIndex) {
#if defined(__APPLE__)
    // Push the descriptor index as a small constant (4 bytes) to the buffer
    // binding slot at rootParamIndex. Metal uses buffer bindings for push
    // constants — setVertexBytes/setFragmentBytes for small inline data.
    if (m_activeEncoder == MetalEncoderType::Render && m_renderEncoder) {
        id<MTLRenderCommandEncoder> enc =
            (__bridge id<MTLRenderCommandEncoder>)m_renderEncoder;
        [enc setVertexBytes:&descriptorIndex
                     length:sizeof(uint32_t)
                    atIndex:rootParamIndex];
        [enc setFragmentBytes:&descriptorIndex
                       length:sizeof(uint32_t)
                      atIndex:rootParamIndex];
    } else if (m_activeEncoder == MetalEncoderType::Compute && m_computeEncoder) {
        id<MTLComputeCommandEncoder> enc =
            (__bridge id<MTLComputeCommandEncoder>)m_computeEncoder;
        [enc setBytes:&descriptorIndex
               length:sizeof(uint32_t)
              atIndex:rootParamIndex];
    }
#endif
}

void MetalCommandList::SetRootConstants(uint32_t rootParamIndex, const uint32_t* data,
                                         uint32_t count, uint32_t offsetIn32BitValues) {
    if (!data || count == 0) return;

#if defined(__APPLE__)
    // Metal's setVertexBytes/setFragmentBytes replaces the entire binding at
    // the given index, so we must account for the offset by adjusting the
    // pointer and total byte length.
    const uint32_t totalBytes = (offsetIn32BitValues + count) * sizeof(uint32_t);

    // Build a small staging buffer on the stack to include the offset region
    // Maximum root constant space is 128 bytes (32 uint32_t values)
    constexpr uint32_t kMaxRootConstants = 32;
    if (offsetIn32BitValues + count > kMaxRootConstants) {
        UZ_LOG(MetalRHI, Warning, "SetRootConstants: exceeds 128-byte root constant range");
        return;
    }

    uint32_t staging[kMaxRootConstants] = {};
    std::memcpy(&staging[offsetIn32BitValues], data, count * sizeof(uint32_t));

    if (m_activeEncoder == MetalEncoderType::Render && m_renderEncoder) {
        id<MTLRenderCommandEncoder> enc =
            (__bridge id<MTLRenderCommandEncoder>)m_renderEncoder;
        [enc setVertexBytes:staging length:totalBytes atIndex:rootParamIndex];
        [enc setFragmentBytes:staging length:totalBytes atIndex:rootParamIndex];
    } else if (m_activeEncoder == MetalEncoderType::Compute && m_computeEncoder) {
        id<MTLComputeCommandEncoder> enc =
            (__bridge id<MTLComputeCommandEncoder>)m_computeEncoder;
        [enc setBytes:staging length:totalBytes atIndex:rootParamIndex];
    }
#endif
}

void MetalCommandList::SetRootCBV(uint32_t rootParamIndex, uint64_t gpuAddress) {
#if defined(__APPLE__)
    if (!m_device) return;

    // Resolve the GPU address to a Metal buffer + offset by scanning the
    // buffer pool for a buffer whose gpuAddress range contains the target.
    auto& slots = m_device->GetBufferPool().Slots();
    id<MTLBuffer> foundBuffer = nil;
    NSUInteger foundOffset = 0;

    for (auto& entry : slots) {
        if (!entry.buffer || entry.generation == 0 || entry.gpuAddress == 0) continue;

        uint64_t start = entry.gpuAddress;
        uint64_t end = start + entry.desc.sizeBytes;
        if (gpuAddress >= start && gpuAddress < end) {
            foundBuffer = (__bridge id<MTLBuffer>)entry.buffer;
            foundOffset = static_cast<NSUInteger>(gpuAddress - start);
            break;
        }
    }

    if (!foundBuffer) {
        UZ_LOG(MetalRHI, Warning,
               "SetRootCBV: no buffer found for GPU address {:#x}", gpuAddress);
        return;
    }

    if (m_activeEncoder == MetalEncoderType::Render && m_renderEncoder) {
        id<MTLRenderCommandEncoder> enc =
            (__bridge id<MTLRenderCommandEncoder>)m_renderEncoder;
        [enc setVertexBuffer:foundBuffer offset:foundOffset atIndex:rootParamIndex];
        [enc setFragmentBuffer:foundBuffer offset:foundOffset atIndex:rootParamIndex];
    } else if (m_activeEncoder == MetalEncoderType::Compute && m_computeEncoder) {
        id<MTLComputeCommandEncoder> enc =
            (__bridge id<MTLComputeCommandEncoder>)m_computeEncoder;
        [enc setBuffer:foundBuffer offset:foundOffset atIndex:rootParamIndex];
    }
#endif
}

void MetalCommandList::SetDescriptorTable(uint32_t rootParamIndex, uint32_t descriptorIndex) {
#if defined(__APPLE__)
    if (!m_device) return;

    // Bind the texture argument buffer and sampler argument buffer at the
    // specified root parameter index. The shader accesses textures via
    // argument buffer tier 2 indexing.
    MTL::Buffer* texArgBuf = m_device->GetTextureArgumentBuffer();
    MTL::Buffer* samArgBuf = m_device->GetSamplerArgumentBuffer();

    if (m_activeEncoder == MetalEncoderType::Render && m_renderEncoder) {
        id<MTLRenderCommandEncoder> enc =
            (__bridge id<MTLRenderCommandEncoder>)m_renderEncoder;

        if (texArgBuf) {
            id<MTLBuffer> argBuf = (__bridge id<MTLBuffer>)texArgBuf;
            [enc setVertexBuffer:argBuf offset:0 atIndex:rootParamIndex];
            [enc setFragmentBuffer:argBuf offset:0 atIndex:rootParamIndex];
        }

        // Bind sampler argument buffer at the next slot
        if (samArgBuf) {
            id<MTLBuffer> samBuf = (__bridge id<MTLBuffer>)samArgBuf;
            [enc setVertexBuffer:samBuf offset:0 atIndex:rootParamIndex + 1];
            [enc setFragmentBuffer:samBuf offset:0 atIndex:rootParamIndex + 1];
        }

        // Also push the descriptor index as a constant so the shader knows
        // which entry in the argument buffer to read
        [enc setVertexBytes:&descriptorIndex
                     length:sizeof(uint32_t)
                    atIndex:rootParamIndex + 2];
        [enc setFragmentBytes:&descriptorIndex
                       length:sizeof(uint32_t)
                      atIndex:rootParamIndex + 2];
    } else if (m_activeEncoder == MetalEncoderType::Compute && m_computeEncoder) {
        id<MTLComputeCommandEncoder> enc =
            (__bridge id<MTLComputeCommandEncoder>)m_computeEncoder;

        if (texArgBuf) {
            id<MTLBuffer> argBuf = (__bridge id<MTLBuffer>)texArgBuf;
            [enc setBuffer:argBuf offset:0 atIndex:rootParamIndex];
        }
        if (samArgBuf) {
            id<MTLBuffer> samBuf = (__bridge id<MTLBuffer>)samArgBuf;
            [enc setBuffer:samBuf offset:0 atIndex:rootParamIndex + 1];
        }
        [enc setBytes:&descriptorIndex
               length:sizeof(uint32_t)
              atIndex:rootParamIndex + 2];
    }
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Direct Fragment Texture/Sampler Binding (Metal-specific)
// ─────────────────────────────────────────────────────────────────────────────

void MetalCommandList::SetFragmentTexture(uint32_t slot, RHITextureHandle texture) {
#if defined(__APPLE__)
    if (m_activeEncoder != MetalEncoderType::Render || !m_renderEncoder) {
        UZ_LOG(MetalRHI, Warning, "SetFragmentTexture -- no active render encoder");
        return;
    }

    auto* tex = m_device->GetTexturePool().Get(texture.index, texture.generation);
    if (!tex || !tex->texture) {
        UZ_LOG(MetalRHI, Warning, "SetFragmentTexture -- texture not found in pool");
        return;
    }

    id<MTLRenderCommandEncoder> enc =
        (__bridge id<MTLRenderCommandEncoder>)m_renderEncoder;
    id<MTLTexture> mtlTex = (__bridge id<MTLTexture>)tex->texture;
    [enc setFragmentTexture:mtlTex atIndex:slot];
#else
    (void)slot; (void)texture;
#endif
}

void MetalCommandList::SetFragmentSampler(uint32_t slot, RHISamplerHandle sampler) {
#if defined(__APPLE__)
    if (m_activeEncoder != MetalEncoderType::Render || !m_renderEncoder) {
        UZ_LOG(MetalRHI, Warning, "SetFragmentSampler -- no active render encoder");
        return;
    }

    auto* sam = m_device->GetSamplerPool().Get(sampler.index, sampler.generation);
    if (!sam || !sam->sampler) {
        UZ_LOG(MetalRHI, Warning, "SetFragmentSampler -- sampler not found in pool");
        return;
    }

    id<MTLRenderCommandEncoder> enc =
        (__bridge id<MTLRenderCommandEncoder>)m_renderEncoder;
    id<MTLSamplerState> mtlSampler = (__bridge id<MTLSamplerState>)sam->sampler;
    [enc setFragmentSamplerState:mtlSampler atIndex:slot];
#else
    (void)slot; (void)sampler;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Draw and Dispatch
// ─────────────────────────────────────────────────────────────────────────────

void MetalCommandList::DrawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                    uint32_t firstIndex, int32_t vertexOffset,
                                    uint32_t firstInstance) {
    UZ_PROFILE_FUNCTION();

#if defined(__APPLE__)
    if (m_activeEncoder != MetalEncoderType::Render || !m_renderEncoder) {
        UZ_LOG(MetalRHI, Warning, "DrawIndexed -- no active render encoder");
        return;
    }

    if (!m_currentIndexBuffer.IsValid()) {
        UZ_LOG(MetalRHI, Warning, "DrawIndexed -- no index buffer bound");
        return;
    }

    auto* idxBufEntry = m_device->GetBufferPool().Get(
        m_currentIndexBuffer.index, m_currentIndexBuffer.generation);
    if (!idxBufEntry || !idxBufEntry->buffer) {
        UZ_LOG(MetalRHI, Warning, "DrawIndexed -- index buffer not found in pool");
        return;
    }

    id<MTLRenderCommandEncoder> enc =
        (__bridge id<MTLRenderCommandEncoder>)m_renderEncoder;
    id<MTLBuffer> idxBuf = (__bridge id<MTLBuffer>)idxBufEntry->buffer;

    MTLIndexType idxType = (m_indexType == 0) ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;

    // Compute the byte offset for firstIndex
    uint32_t indexStride = (m_indexType == 0) ? sizeof(uint16_t) : sizeof(uint32_t);
    NSUInteger indexBufferOffset = m_indexBufferOffset + firstIndex * indexStride;

    // Resolve the primitive type from the currently bound pipeline
    MTLPrimitiveType primitiveType = MTLPrimitiveTypeTriangle;
    if (m_currentPipeline.IsValid()) {
        auto* pipeline = m_device->GetPipelinePool().Get(
            m_currentPipeline.index, m_currentPipeline.generation);
        if (pipeline) {
            primitiveType = static_cast<MTLPrimitiveType>(
                ToMTLPrimitiveType(pipeline->topology));
        }
    }

    [enc drawIndexedPrimitives:primitiveType
                    indexCount:indexCount
                     indexType:idxType
                   indexBuffer:idxBuf
             indexBufferOffset:indexBufferOffset
                 instanceCount:instanceCount
                    baseVertex:vertexOffset
                  baseInstance:firstInstance];
#endif
}

void MetalCommandList::Draw(uint32_t vertexCount, uint32_t instanceCount,
                             uint32_t firstVertex, uint32_t firstInstance) {
    UZ_PROFILE_FUNCTION();

#if defined(__APPLE__)
    if (m_activeEncoder != MetalEncoderType::Render || !m_renderEncoder) {
        UZ_LOG(MetalRHI, Warning, "Draw -- no active render encoder");
        return;
    }

    id<MTLRenderCommandEncoder> enc =
        (__bridge id<MTLRenderCommandEncoder>)m_renderEncoder;

    // Resolve the primitive type from the currently bound pipeline
    MTLPrimitiveType primitiveType = MTLPrimitiveTypeTriangle;
    if (m_currentPipeline.IsValid()) {
        auto* pipeline = m_device->GetPipelinePool().Get(
            m_currentPipeline.index, m_currentPipeline.generation);
        if (pipeline) {
            primitiveType = static_cast<MTLPrimitiveType>(
                ToMTLPrimitiveType(pipeline->topology));
        }
    }

    [enc drawPrimitives:primitiveType
            vertexStart:firstVertex
            vertexCount:vertexCount
          instanceCount:instanceCount
           baseInstance:firstInstance];
#endif
}

void MetalCommandList::Dispatch(uint32_t groupCountX, uint32_t groupCountY,
                                 uint32_t groupCountZ) {
    UZ_PROFILE_FUNCTION();

#if defined(__APPLE__)
    EnsureComputeEncoder();
    if (!m_computeEncoder) return;

    id<MTLComputeCommandEncoder> enc =
        (__bridge id<MTLComputeCommandEncoder>)m_computeEncoder;

    // Query thread execution width from the currently bound compute pipeline
    // to determine a reasonable threads-per-threadgroup.
    // Default to (1,1,1) if no pipeline is bound.
    MTLSize threadsPerGroup = MTLSizeMake(1, 1, 1);
    if (m_currentPipeline.IsValid()) {
        auto* pipeline = m_device->GetPipelinePool().Get(
            m_currentPipeline.index, m_currentPipeline.generation);
        if (pipeline && pipeline->computePipeline) {
            id<MTLComputePipelineState> pso =
                (__bridge id<MTLComputePipelineState>)pipeline->computePipeline;
            NSUInteger execWidth = [pso threadExecutionWidth];
            threadsPerGroup = MTLSizeMake(execWidth, 1, 1);
        }
    }

    MTLSize threadGroups = MTLSizeMake(groupCountX, groupCountY, groupCountZ);
    [enc dispatchThreadgroups:threadGroups threadsPerThreadgroup:threadsPerGroup];
#endif
}

void MetalCommandList::DrawIndexedIndirect(RHIBufferHandle /*argsBuffer*/, uint64_t /*argsOffset*/,
                                            uint32_t /*drawCount*/, uint32_t /*argsStride*/) {
    // TODO: Implement via drawIndexedPrimitives:indirectBuffer:indirectBufferOffset:
    UZ_LOG(RHI, Warning, "DrawIndexedIndirect — Metal stub, not yet implemented");
}

void MetalCommandList::DispatchIndirect(RHIBufferHandle /*argsBuffer*/, uint64_t /*argsOffset*/) {
    // TODO: Implement via dispatchThreadgroups:indirectBuffer:indirectBufferOffset:
    UZ_LOG(RHI, Warning, "DispatchIndirect — Metal stub, not yet implemented");
}

// ─────────────────────────────────────────────────────────────────────────────
// Barriers
// ─────────────────────────────────────────────────────────────────────────────

void MetalCommandList::ResourceBarrier(const RHIBarrierDesc* barriers, uint32_t count) {
    // Metal handles resource hazard tracking automatically within a single
    // command encoder. Cross-encoder dependencies are handled by encoder
    // ordering within the command buffer. This is intentionally a no-op.
    (void)barriers;
    (void)count;
    // Intentionally silent — called 4+ times per frame, trace would flood logs.
}

// ─────────────────────────────────────────────────────────────────────────────
// Clear Operations
// ─────────────────────────────────────────────────────────────────────────────

void MetalCommandList::ClearRenderTarget(RHITextureHandle texture, const float color[4]) {
    UZ_PROFILE_FUNCTION();

    if (!texture.IsValid()) return;

#if defined(__APPLE__)
    auto* tex = m_device->GetTexturePool().Get(texture.index, texture.generation);
    if (!tex || !tex->texture) {
        UZ_LOG(MetalRHI, Warning, "ClearRenderTarget -- texture not found");
        return;
    }

    // Use a temporary render pass with clear load action to clear the texture.
    // This is the idiomatic Metal approach for clearing outside a render pass.
    EndCurrentEncoder();

    if (!m_commandBuffer) {
        UZ_LOG(MetalRHI, Error, "ClearRenderTarget -- no command buffer");
        return;
    }

    id<MTLCommandBuffer> cmdBuf = (__bridge id<MTLCommandBuffer>)m_commandBuffer;
    id<MTLTexture> mtlTex = (__bridge id<MTLTexture>)tex->texture;

    MTLRenderPassDescriptor* rpDesc = [MTLRenderPassDescriptor renderPassDescriptor];
    rpDesc.colorAttachments[0].texture = mtlTex;
    rpDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
    rpDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
    rpDesc.colorAttachments[0].clearColor = MTLClearColorMake(
        color[0], color[1], color[2], color[3]);

    id<MTLRenderCommandEncoder> enc =
        [cmdBuf renderCommandEncoderWithDescriptor:rpDesc];
    if (enc) {
        [enc endEncoding];
    }
#endif
}

void MetalCommandList::ClearDepthStencil(RHITextureHandle texture, float depth,
                                          uint8_t stencil) {
    UZ_PROFILE_FUNCTION();

    if (!texture.IsValid()) return;

#if defined(__APPLE__)
    auto* tex = m_device->GetTexturePool().Get(texture.index, texture.generation);
    if (!tex || !tex->texture) {
        UZ_LOG(MetalRHI, Warning, "ClearDepthStencil -- texture not found");
        return;
    }

    EndCurrentEncoder();

    if (!m_commandBuffer) {
        UZ_LOG(MetalRHI, Error, "ClearDepthStencil -- no command buffer");
        return;
    }

    id<MTLCommandBuffer> cmdBuf = (__bridge id<MTLCommandBuffer>)m_commandBuffer;
    id<MTLTexture> mtlTex = (__bridge id<MTLTexture>)tex->texture;

    MTLRenderPassDescriptor* rpDesc = [MTLRenderPassDescriptor renderPassDescriptor];
    rpDesc.depthAttachment.texture = mtlTex;
    rpDesc.depthAttachment.loadAction = MTLLoadActionClear;
    rpDesc.depthAttachment.storeAction = MTLStoreActionStore;
    rpDesc.depthAttachment.clearDepth = depth;

    if (HasStencil(tex->desc.format)) {
        rpDesc.stencilAttachment.texture = mtlTex;
        rpDesc.stencilAttachment.loadAction = MTLLoadActionClear;
        rpDesc.stencilAttachment.storeAction = MTLStoreActionStore;
        rpDesc.stencilAttachment.clearStencil = stencil;
    }

    id<MTLRenderCommandEncoder> enc =
        [cmdBuf renderCommandEncoderWithDescriptor:rpDesc];
    if (enc) {
        [enc endEncoding];
    }
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Copy Operations
// ─────────────────────────────────────────────────────────────────────────────

void MetalCommandList::CopyBuffer(RHIBufferHandle src, uint64_t srcOffset,
                                   RHIBufferHandle dst, uint64_t dstOffset, uint64_t size) {
    UZ_PROFILE_FUNCTION();

    if (!src.IsValid() || !dst.IsValid()) return;

#if defined(__APPLE__)
    auto* srcBuf = m_device->GetBufferPool().Get(src.index, src.generation);
    auto* dstBuf = m_device->GetBufferPool().Get(dst.index, dst.generation);
    if (!srcBuf || !dstBuf || !srcBuf->buffer || !dstBuf->buffer) {
        UZ_LOG(MetalRHI, Warning, "CopyBuffer -- could not resolve buffer handles");
        return;
    }

    EnsureBlitEncoder();
    if (!m_blitEncoder) return;

    id<MTLBlitCommandEncoder> enc =
        (__bridge id<MTLBlitCommandEncoder>)m_blitEncoder;
    id<MTLBuffer> srcMtl = (__bridge id<MTLBuffer>)srcBuf->buffer;
    id<MTLBuffer> dstMtl = (__bridge id<MTLBuffer>)dstBuf->buffer;

    [enc copyFromBuffer:srcMtl
           sourceOffset:static_cast<NSUInteger>(srcOffset)
               toBuffer:dstMtl
      destinationOffset:static_cast<NSUInteger>(dstOffset)
                   size:static_cast<NSUInteger>(size)];
#endif
}

void MetalCommandList::CopyTexture(RHITextureHandle src, RHITextureHandle dst) {
    UZ_PROFILE_FUNCTION();

    if (!src.IsValid() || !dst.IsValid()) return;

#if defined(__APPLE__)
    auto* srcTex = m_device->GetTexturePool().Get(src.index, src.generation);
    auto* dstTex = m_device->GetTexturePool().Get(dst.index, dst.generation);
    if (!srcTex || !dstTex || !srcTex->texture || !dstTex->texture) {
        UZ_LOG(MetalRHI, Warning, "CopyTexture -- could not resolve texture handles");
        return;
    }

    EnsureBlitEncoder();
    if (!m_blitEncoder) return;

    id<MTLBlitCommandEncoder> enc =
        (__bridge id<MTLBlitCommandEncoder>)m_blitEncoder;
    id<MTLTexture> srcMtl = (__bridge id<MTLTexture>)srcTex->texture;
    id<MTLTexture> dstMtl = (__bridge id<MTLTexture>)dstTex->texture;

    // Copy all mip levels and array slices
    uint32_t mipLevels = srcTex->desc.mipLevels;
    uint32_t arrayLayers = srcTex->desc.depthOrArraySize;

    for (uint32_t mip = 0; mip < mipLevels; ++mip) {
        uint32_t mipWidth = srcTex->desc.width >> mip;
        uint32_t mipHeight = srcTex->desc.height >> mip;
        if (mipWidth == 0) mipWidth = 1;
        if (mipHeight == 0) mipHeight = 1;

        for (uint32_t slice = 0; slice < arrayLayers; ++slice) {
            [enc copyFromTexture:srcMtl
                     sourceSlice:slice
                     sourceLevel:mip
                    sourceOrigin:MTLOriginMake(0, 0, 0)
                      sourceSize:MTLSizeMake(mipWidth, mipHeight, 1)
                       toTexture:dstMtl
                destinationSlice:slice
                destinationLevel:mip
               destinationOrigin:MTLOriginMake(0, 0, 0)];
        }
    }
#endif
}

void MetalCommandList::CopyBufferToTexture(RHIBufferHandle srcBuffer, uint64_t srcOffset,
                                            uint32_t srcRowPitch,
                                            RHITextureHandle dstTexture,
                                            uint32_t mipLevel, uint32_t arraySlice) {
    UZ_PROFILE_FUNCTION();

    if (!srcBuffer.IsValid() || !dstTexture.IsValid()) return;

#if defined(__APPLE__)
    auto* buf = m_device->GetBufferPool().Get(srcBuffer.index, srcBuffer.generation);
    auto* tex = m_device->GetTexturePool().Get(dstTexture.index, dstTexture.generation);
    if (!buf || !tex || !buf->buffer || !tex->texture) {
        UZ_LOG(MetalRHI, Warning, "CopyBufferToTexture -- could not resolve handles");
        return;
    }

    EnsureBlitEncoder();
    if (!m_blitEncoder) return;

    id<MTLBlitCommandEncoder> enc =
        (__bridge id<MTLBlitCommandEncoder>)m_blitEncoder;
    id<MTLBuffer> srcMtl = (__bridge id<MTLBuffer>)buf->buffer;
    id<MTLTexture> dstMtl = (__bridge id<MTLTexture>)tex->texture;

    uint32_t mipWidth = tex->desc.width >> mipLevel;
    uint32_t mipHeight = tex->desc.height >> mipLevel;
    if (mipWidth == 0) mipWidth = 1;
    if (mipHeight == 0) mipHeight = 1;

    // Metal's bytesPerRow must be a multiple of the texel size
    NSUInteger bytesPerRow = static_cast<NSUInteger>(srcRowPitch);
    NSUInteger bytesPerImage = bytesPerRow * mipHeight;

    [enc copyFromBuffer:srcMtl
           sourceOffset:static_cast<NSUInteger>(srcOffset)
      sourceBytesPerRow:bytesPerRow
    sourceBytesPerImage:bytesPerImage
             sourceSize:MTLSizeMake(mipWidth, mipHeight, 1)
              toTexture:dstMtl
       destinationSlice:arraySlice
       destinationLevel:mipLevel
      destinationOrigin:MTLOriginMake(0, 0, 0)];
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Render Pass
// ─────────────────────────────────────────────────────────────────────────────

void MetalCommandList::BeginRenderPass(const RHIRenderPassDesc& desc) {
    UZ_PROFILE_FUNCTION();

#if defined(__APPLE__)
    // End any active encoder before starting a new render pass
    EndCurrentEncoder();

    if (!m_commandBuffer) {
        UZ_LOG(MetalRHI, Error, "BeginRenderPass -- no command buffer");
        return;
    }

    id<MTLCommandBuffer> cmdBuf = (__bridge id<MTLCommandBuffer>)m_commandBuffer;
    MTLRenderPassDescriptor* rpDesc = [MTLRenderPassDescriptor renderPassDescriptor];

    // Configure color attachments
    for (uint32_t i = 0; i < desc.colorAttachmentCount && i < 8; ++i) {
        const auto& att = desc.colorAttachments[i];
        if (!att.texture.IsValid()) continue;

        auto* tex = m_device->GetTexturePool().Get(att.texture.index, att.texture.generation);
        if (!tex || !tex->texture) {
            UZ_LOG(MetalRHI, Warning, "BeginRenderPass -- color attachment {} has no texture", i);
            continue;
        }

        id<MTLTexture> mtlTex = (__bridge id<MTLTexture>)tex->texture;
        rpDesc.colorAttachments[i].texture = mtlTex;
        rpDesc.colorAttachments[i].loadAction = att.clear
            ? MTLLoadActionClear
            : MTLLoadActionLoad;
        rpDesc.colorAttachments[i].storeAction = MTLStoreActionStore;

        if (att.clear) {
            rpDesc.colorAttachments[i].clearColor = MTLClearColorMake(
                att.clearColor[0], att.clearColor[1],
                att.clearColor[2], att.clearColor[3]);
        }
    }

    // Configure depth attachment
    if (desc.hasDepth && desc.depthAttachment.texture.IsValid()) {
        auto* depthTex = m_device->GetTexturePool().Get(
            desc.depthAttachment.texture.index,
            desc.depthAttachment.texture.generation);

        if (depthTex && depthTex->texture) {
            id<MTLTexture> mtlDepth = (__bridge id<MTLTexture>)depthTex->texture;
            rpDesc.depthAttachment.texture = mtlDepth;
            rpDesc.depthAttachment.loadAction = desc.depthAttachment.clear
                ? MTLLoadActionClear
                : MTLLoadActionLoad;
            rpDesc.depthAttachment.storeAction = MTLStoreActionStore;
            rpDesc.depthAttachment.clearDepth = desc.depthAttachment.clearDepth;

            // Configure stencil if the format includes it
            if (HasStencil(depthTex->desc.format)) {
                rpDesc.stencilAttachment.texture = mtlDepth;
                rpDesc.stencilAttachment.loadAction = desc.depthAttachment.clear
                    ? MTLLoadActionClear
                    : MTLLoadActionLoad;
                rpDesc.stencilAttachment.storeAction = MTLStoreActionStore;
                rpDesc.stencilAttachment.clearStencil = desc.depthAttachment.clearStencil;
            }
        }
    }

    // Attach GPU profiler timestamps (only if BeginTimestamp was called).
    // AttachToRenderPass guards with m_hasPendingBegin — safe as no-op otherwise.
    if (m_gpuProfiler) {
        m_gpuProfiler->AttachToRenderPass((__bridge void*)rpDesc);
    }

    // Tracy GPU zones DISABLED — MetalZoneScope modifies sampleBufferAttachments[0]
    // which causes GPU hangs on Apple Silicon. Tracy CPU profiling still works.
    // See metal_device.cpp for details.

    // Create the render command encoder
    id<MTLRenderCommandEncoder> enc =
        [cmdBuf renderCommandEncoderWithDescriptor:rpDesc];
    if (!enc) {
        UZ_LOG(MetalRHI, Error, "BeginRenderPass -- failed to create render encoder");
        return;
    }

    [enc retain]; // Prevent premature autorelease deallocation
    m_renderEncoder = (__bridge MTL::RenderCommandEncoder*)enc;
    m_activeEncoder = MetalEncoderType::Render;
    m_inRenderPass = true;
#endif
}

void MetalCommandList::EndRenderPass() {
    UZ_PROFILE_FUNCTION();

#if defined(__APPLE__)
    if (!m_inRenderPass) return;

    if (m_activeEncoder == MetalEncoderType::Render && m_renderEncoder) {
        id<MTLRenderCommandEncoder> enc =
            (__bridge id<MTLRenderCommandEncoder>)m_renderEncoder;
        [enc endEncoding];
        [enc release]; // Balance the retain in BeginRenderPass
        m_renderEncoder = nullptr;
    }

    // Notify profiler that the render pass has ended
    if (m_gpuProfiler) {
        m_gpuProfiler->OnRenderPassEnd();
    }

    // Tracy GPU zones DISABLED — see BeginRenderPass comment.

    m_activeEncoder = MetalEncoderType::None;
    m_inRenderPass = false;
#endif
}

void MetalCommandList::SetRootCBV(uint32_t rootParamIndex, RHIBufferHandle buffer,
                                  uint64_t offsetBytes, const void* cpuData, uint32_t dataSize) {
    UZ_PROFILE_FUNCTION();

#if defined(__APPLE__)
    if (!m_device) return;

    // If cpuData is provided, upload via setBytes (inline constant buffer).
    // Otherwise, bind the buffer at the given offset.
    if (cpuData && dataSize > 0) {
        if (m_activeEncoder == MetalEncoderType::Render && m_renderEncoder) {
            id<MTLRenderCommandEncoder> enc =
                (__bridge id<MTLRenderCommandEncoder>)m_renderEncoder;
            [enc setVertexBytes:cpuData length:dataSize atIndex:rootParamIndex];
            [enc setFragmentBytes:cpuData length:dataSize atIndex:rootParamIndex];
        } else if (m_activeEncoder == MetalEncoderType::Compute && m_computeEncoder) {
            id<MTLComputeCommandEncoder> enc =
                (__bridge id<MTLComputeCommandEncoder>)m_computeEncoder;
            [enc setBytes:cpuData length:dataSize atIndex:rootParamIndex];
        }
        return;
    }

    // Bind the buffer handle directly
    auto& pool = m_device->GetBufferPool();
    auto* slot = pool.Get(buffer.index, buffer.generation);
    if (!slot || !slot->buffer) {
        UZ_LOG(MetalRHI, Warning, "SetRootCBV: invalid buffer handle");
        return;
    }

    id<MTLBuffer> mtlBuffer = (__bridge id<MTLBuffer>)slot->buffer;
    NSUInteger offset = static_cast<NSUInteger>(offsetBytes);

    if (m_activeEncoder == MetalEncoderType::Render && m_renderEncoder) {
        id<MTLRenderCommandEncoder> enc =
            (__bridge id<MTLRenderCommandEncoder>)m_renderEncoder;
        [enc setVertexBuffer:mtlBuffer offset:offset atIndex:rootParamIndex];
        [enc setFragmentBuffer:mtlBuffer offset:offset atIndex:rootParamIndex];
    } else if (m_activeEncoder == MetalEncoderType::Compute && m_computeEncoder) {
        id<MTLComputeCommandEncoder> enc =
            (__bridge id<MTLComputeCommandEncoder>)m_computeEncoder;
        [enc setBuffer:mtlBuffer offset:offset atIndex:rootParamIndex];
    }
#endif
}

void MetalCommandList::CopyTextureToBuffer(RHITextureHandle srcTexture, uint32_t mipLevel,
                                           RHIBufferHandle dstBuffer, uint64_t dstOffset,
                                           uint32_t dstRowPitch, uint32_t arraySlice) {
    UZ_PROFILE_FUNCTION();

#if defined(__APPLE__)
    if (!m_device) return;

    auto* texSlot = m_device->GetTexturePool().Get(srcTexture.index, srcTexture.generation);
    auto* bufSlot = m_device->GetBufferPool().Get(dstBuffer.index, dstBuffer.generation);
    if (!texSlot || !texSlot->texture || !bufSlot || !bufSlot->buffer) {
        UZ_LOG(MetalRHI, Error, "CopyTextureToBuffer: invalid src texture or dst buffer");
        return;
    }

    EnsureBlitEncoder();
    id<MTLBlitCommandEncoder> enc = (__bridge id<MTLBlitCommandEncoder>)m_blitEncoder;
    id<MTLTexture> srcTex = (__bridge id<MTLTexture>)texSlot->texture;
    id<MTLBuffer> dstBuf = (__bridge id<MTLBuffer>)bufSlot->buffer;

    uint32_t width = texSlot->desc.width >> mipLevel;
    uint32_t height = texSlot->desc.height >> mipLevel;
    if (width == 0) width = 1;
    if (height == 0) height = 1;

    [enc copyFromTexture:srcTex
              sourceSlice:arraySlice
              sourceLevel:mipLevel
             sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:MTLSizeMake(width, height, 1)
                toBuffer:dstBuf
       destinationOffset:static_cast<NSUInteger>(dstOffset)
  destinationBytesPerRow:static_cast<NSUInteger>(dstRowPitch)
destinationBytesPerImage:0];
#endif
}

} // namespace uz
