#pragma once

/// UzEngine RHI Type Definitions
///
/// All enums, descriptor structs, and handle types for the Rendering Hardware Interface.
/// This header is backend-agnostic — no DX12/Vulkan/Metal types appear here.
///
/// # Handle System
/// GPU resources use typed generational handles (RHIHandle<Tag>) to prevent
/// use-after-free and dangling references. Handles are value types with
/// index + generation, matching the pattern in handle_map.h.

#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Resource Handles — typed generational IDs for GPU objects
// ─────────────────────────────────────────────────────────────────────────────

/// Typed generational handle for GPU resources.
///
/// Combines a slot index with a generation counter so that stale handles
/// (pointing to freed/reallocated slots) are safely detected.
template <typename Tag>
struct RHIHandle {
    uint32_t index = 0;      ///< Slot index in backend pool
    uint32_t generation = 0; ///< Version counter (0 = invalid/never allocated)

    /// Returns true if this handle was ever created (generation != 0).
    bool IsValid() const { return generation != 0; }

    /// Creates an invalid handle sentinel.
    static RHIHandle Invalid() { return {0, 0}; }

    bool operator==(RHIHandle other) const { return index == other.index && generation == other.generation; }
    bool operator!=(RHIHandle other) const { return !(*this == other); }
};

struct BufferTag {};
struct TextureTag {};
struct PipelineTag {};
struct ShaderTag {};
struct SamplerTag {};

/// Handle to a GPU buffer (vertex, index, constant, structured, etc.).
using RHIBufferHandle = RHIHandle<BufferTag>;

/// Handle to a GPU texture (render target, depth stencil, SRV, etc.).
using RHITextureHandle = RHIHandle<TextureTag>;

/// Handle to a graphics or compute pipeline state object.
using RHIPipelineHandle = RHIHandle<PipelineTag>;

/// Handle to a compiled shader module (vertex, pixel, compute).
using RHIShaderHandle = RHIHandle<ShaderTag>;

/// Handle to a texture sampler state.
using RHISamplerHandle = RHIHandle<SamplerTag>;

// ─────────────────────────────────────────────────────────────────────────────
// Enums
// ─────────────────────────────────────────────────────────────────────────────

/// GPU resource format for textures, render targets, and vertex attributes.
enum class RHIFormat : uint8_t {
    Unknown = 0,

    // Color formats
    R8Unorm,
    RG8Unorm,
    RGBA8Unorm,
    RGBA8Srgb,
    BGRA8Unorm,
    BGRA8Srgb,
    R16Float,
    RG16Float,
    RGBA16Float,
    R32Float,
    RG32Float,
    RGB32Float,
    RGBA32Float,
    R32Uint,
    RG32Uint,
    RGBA32Uint,

    // Depth/stencil formats
    D16Unorm,
    D32Float,
    D24UnormS8Uint,
    D32FloatS8Uint,
};

/// Buffer usage flags (combinable via bitwise OR).
enum class RHIBufferUsage : uint32_t {
    None          = 0,
    VertexBuffer  = 1 << 0,
    IndexBuffer   = 1 << 1,
    ConstantBuffer = 1 << 2,
    StructuredBuffer = 1 << 3,
    UnorderedAccess = 1 << 4,
    IndirectArgs  = 1 << 5,
    CopySrc       = 1 << 6,
    CopyDst       = 1 << 7,
};

inline RHIBufferUsage operator|(RHIBufferUsage a, RHIBufferUsage b) {
    return static_cast<RHIBufferUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline RHIBufferUsage operator&(RHIBufferUsage a, RHIBufferUsage b) {
    return static_cast<RHIBufferUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline bool HasFlag(RHIBufferUsage flags, RHIBufferUsage test) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(test)) != 0;
}

/// Texture usage flags (combinable via bitwise OR).
enum class RHITextureUsage : uint32_t {
    None            = 0,
    ShaderResource  = 1 << 0,
    RenderTarget    = 1 << 1,
    DepthStencil    = 1 << 2,
    UnorderedAccess = 1 << 3,
    CopySrc         = 1 << 4,
    CopyDst         = 1 << 5,
};

inline RHITextureUsage operator|(RHITextureUsage a, RHITextureUsage b) {
    return static_cast<RHITextureUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline RHITextureUsage operator&(RHITextureUsage a, RHITextureUsage b) {
    return static_cast<RHITextureUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline bool HasFlag(RHITextureUsage flags, RHITextureUsage test) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(test)) != 0;
}

/// Resource state for barrier transitions.
///
/// Backends map these to native states (D3D12_RESOURCE_STATES, VkImageLayout, etc.).
enum class RHIResourceState : uint32_t {
    Undefined         = 0,
    Common            = 1 << 0,
    VertexBuffer      = 1 << 1,
    IndexBuffer       = 1 << 2,
    ConstantBuffer    = 1 << 3,
    ShaderResource    = 1 << 4,
    UnorderedAccess   = 1 << 5,
    RenderTarget      = 1 << 6,
    DepthStencilWrite = 1 << 7,
    DepthStencilRead  = 1 << 8,
    CopySrc           = 1 << 9,
    CopyDst           = 1 << 10,
    Present           = 1 << 11,
    IndirectArgument  = 1 << 12,
};

/// Primitive topology for draw calls.
enum class RHIPrimitiveTopology : uint8_t {
    TriangleList,
    TriangleStrip,
    LineList,
    LineStrip,
    PointList,
};

/// Depth/stencil comparison function.
enum class RHICompareFunc : uint8_t {
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always,
};

/// Blend factor for color/alpha blending.
enum class RHIBlendFactor : uint8_t {
    Zero,
    One,
    SrcColor,
    InvSrcColor,
    SrcAlpha,
    InvSrcAlpha,
    DstColor,
    InvDstColor,
    DstAlpha,
    InvDstAlpha,
};

/// Blend operation.
enum class RHIBlendOp : uint8_t {
    Add,
    Subtract,
    RevSubtract,
    Min,
    Max,
};

/// Triangle cull mode.
enum class RHICullMode : uint8_t {
    None,
    Front,
    Back,
};

/// Polygon fill mode.
enum class RHIFillMode : uint8_t {
    Solid,
    Wireframe,
};

/// Index buffer element format.
enum class RHIIndexFormat : uint8_t {
    Uint16,
    Uint32,
};

/// Shader compilation stage.
enum class RHIShaderStage : uint8_t {
    Vertex,
    Pixel,
    Compute,
};

/// GPU command queue type.
enum class RHIQueueType : uint8_t {
    Graphics,
    Compute,
    Copy,
};

/// Texture sampler filter mode.
enum class RHIFilterMode : uint8_t {
    Point,
    Linear,
    Anisotropic,
};

/// Texture sampler address mode (wrap/clamp/mirror).
enum class RHIAddressMode : uint8_t {
    Wrap,
    Clamp,
    Mirror,
    Border,
};

/// Memory heap type for buffer allocation.
enum class RHIMemoryType : uint8_t {
    Default,   ///< GPU-only (fastest for GPU access)
    Upload,    ///< CPU-writable, GPU-readable (for staging uploads)
    Readback,  ///< GPU-writable, CPU-readable (for GPU readback)
};

// ─────────────────────────────────────────────────────────────────────────────
// Descriptor Structs
// ─────────────────────────────────────────────────────────────────────────────

/// Describes a GPU buffer to create.
struct RHIBufferDesc {
    uint64_t sizeBytes = 0;                          ///< Total size in bytes
    uint32_t stride = 0;                             ///< Element stride (for structured buffers)
    RHIBufferUsage usage = RHIBufferUsage::None;     ///< Usage flags
    RHIMemoryType memory = RHIMemoryType::Default;   ///< Memory heap type
    const char* debugName = nullptr;                 ///< Debug label (stripped in shipping)
};

/// Describes a GPU texture to create.
struct RHITextureDesc {
    uint32_t width = 1;                                 ///< Width in texels
    uint32_t height = 1;                                ///< Height in texels
    uint16_t depthOrArraySize = 1;                      ///< Depth (3D) or array size
    uint16_t mipLevels = 1;                             ///< Number of mip levels
    RHIFormat format = RHIFormat::RGBA8Unorm;           ///< Texel format
    RHITextureUsage usage = RHITextureUsage::ShaderResource; ///< Usage flags
    const char* debugName = nullptr;                    ///< Debug label
};

/// Describes a swap chain to create.
struct RHISwapChainDesc {
    void* windowHandle = nullptr;    ///< Native window handle (HWND on Windows)
    uint32_t width = 0;              ///< Back buffer width
    uint32_t height = 0;             ///< Back buffer height
    uint32_t bufferCount = 3;        ///< Number of back buffers (triple-buffered default)
    RHIFormat format = RHIFormat::RGBA8Unorm; ///< Back buffer format
    bool vsync = true;               ///< Enable vertical sync
};

/// Describes a compiled shader module.
struct RHIShaderDesc {
    const void* bytecode = nullptr;  ///< Pointer to compiled shader bytecode (DXIL/SPIR-V/MSL)
    uint64_t bytecodeSize = 0;       ///< Size of bytecode in bytes
    RHIShaderStage stage = RHIShaderStage::Vertex; ///< Shader stage
    const char* entryPoint = "main"; ///< Entry point function name
    const char* debugName = nullptr; ///< Debug label
};

/// Blend state for a single render target.
struct RHIBlendDesc {
    bool blendEnable = false;
    RHIBlendFactor srcColor = RHIBlendFactor::One;
    RHIBlendFactor dstColor = RHIBlendFactor::Zero;
    RHIBlendOp colorOp = RHIBlendOp::Add;
    RHIBlendFactor srcAlpha = RHIBlendFactor::One;
    RHIBlendFactor dstAlpha = RHIBlendFactor::Zero;
    RHIBlendOp alphaOp = RHIBlendOp::Add;
};

/// Depth-stencil state for pipeline creation.
struct RHIDepthStencilDesc {
    bool depthEnable = true;
    bool depthWrite = true;
    RHICompareFunc depthFunc = RHICompareFunc::Less;
};

/// Rasterizer state for pipeline creation.
struct RHIRasterizerDesc {
    RHICullMode cullMode = RHICullMode::Back;
    RHIFillMode fillMode = RHIFillMode::Solid;
    bool frontCounterClockwise = false;
    float depthBias = 0.0f;
    float slopeScaledDepthBias = 0.0f;
};

/// Describes a graphics pipeline state object.
struct RHIGraphicsPipelineDesc {
    RHIShaderHandle vertexShader;                    ///< Vertex shader handle
    RHIShaderHandle pixelShader;                     ///< Pixel shader handle (optional for depth-only)
    RHIPrimitiveTopology topology = RHIPrimitiveTopology::TriangleList;
    RHIRasterizerDesc rasterizer;                    ///< Rasterizer state
    RHIDepthStencilDesc depthStencil;                ///< Depth-stencil state
    RHIBlendDesc renderTargetBlend[8] = {};          ///< Per-RT blend state
    RHIFormat renderTargetFormats[8] = {};           ///< RT formats (Unknown = unused)
    uint32_t renderTargetCount = 1;                  ///< Number of active render targets
    RHIFormat depthStencilFormat = RHIFormat::Unknown; ///< Depth format (Unknown = no depth)
    const char* debugName = nullptr;                 ///< Debug label
};

/// Describes a texture sampler.
struct RHISamplerDesc {
    RHIFilterMode filter = RHIFilterMode::Linear;
    RHIAddressMode addressU = RHIAddressMode::Wrap;
    RHIAddressMode addressV = RHIAddressMode::Wrap;
    RHIAddressMode addressW = RHIAddressMode::Wrap;
    float mipLodBias = 0.0f;
    uint32_t maxAnisotropy = 1;
    RHICompareFunc compareFunc = RHICompareFunc::Never;
    float minLod = 0.0f;
    float maxLod = 1000.0f;
    const char* debugName = nullptr;
};

/// Viewport rectangle.
struct RHIViewport {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float minDepth = 0.0f;
    float maxDepth = 1.0f;
};

/// Scissor rectangle (integer coordinates).
struct RHIScissorRect {
    int32_t left = 0;
    int32_t top = 0;
    int32_t right = 0;
    int32_t bottom = 0;
};

/// Resource barrier descriptor for state transitions.
struct RHIBarrierDesc {
    RHITextureHandle texture;          ///< Texture to transition (or invalid for buffer)
    RHIBufferHandle buffer;            ///< Buffer to transition (or invalid for texture)
    RHIResourceState stateBefore;      ///< State before barrier
    RHIResourceState stateAfter;       ///< State after barrier
};

/// Render pass color attachment.
struct RHIRenderPassColorAttachment {
    RHITextureHandle texture;                   ///< Render target texture
    float clearColor[4] = {0, 0, 0, 1};        ///< Clear color (used if loadOp is Clear)
    bool clear = false;                         ///< True to clear on load
};

/// Render pass depth attachment.
struct RHIRenderPassDepthAttachment {
    RHITextureHandle texture;                   ///< Depth-stencil texture
    float clearDepth = 1.0f;                    ///< Clear depth value
    uint8_t clearStencil = 0;                   ///< Clear stencil value
    bool clear = false;                         ///< True to clear on load
};

/// Describes a render pass (begin/end scope for rendering).
struct RHIRenderPassDesc {
    RHIRenderPassColorAttachment colorAttachments[8] = {};
    uint32_t colorAttachmentCount = 0;
    RHIRenderPassDepthAttachment depthAttachment;
    bool hasDepth = false;
};

/// Memory budget information reported by the device.
struct RHIMemoryBudget {
    uint64_t budgetBytes = 0;     ///< Total memory budget for the adapter
    uint64_t usageBytes = 0;      ///< Current memory usage
};

} // namespace uz
