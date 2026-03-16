#pragma once

/// UzEngine RHI Type Definitions — Enums, Descriptors, and Resource Handles.
///
/// This header contains all backend-agnostic type definitions for the Rendering
/// Hardware Interface. No DX12, Vulkan, or Metal types appear here.
///
/// # Handle System
/// GPU resources use typed generational handles (`RHIHandle<Tag>`) to prevent
/// use-after-free and dangling references. Handles are lightweight value types
/// (8 bytes: index + generation) that can be freely copied, stored, and compared.
/// A handle with `generation == 0` is always invalid.
///
/// # Enums
/// Flag enums (`RHIBufferUsage`, `RHITextureUsage`, `RHIResourceState`) support
/// bitwise OR to combine multiple flags. Use `HasFlag()` to test individual bits.
///
/// # Descriptor Structs
/// All `*Desc` structs use aggregate initialization with safe defaults.
/// Only fields that differ from defaults need to be set explicitly.

#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Depth Convention — Reversed-Z (UE5 Parity)
// ─────────────────────────────────────────────────────────────────────────────

/// UzEngine uses reversed-Z depth: near plane maps to 1.0, far plane maps to 0.0.
/// This provides ~100x better depth precision near the camera, eliminating
/// Z-fighting on close geometry. Matches UE5 (RHIDefinitions.h:278-287).
///
/// # Rules
/// - NEVER use raw `0.0f` or `1.0f` for depth clear/compare — use these constants.
/// - Depth test for "closer" uses GreaterEqual (larger value = closer).
/// - Clear depth to kDepthFar (0.0) for the far plane.
/// - Projection matrices output Z in [1, 0] range (near=1, far=0).
constexpr bool     kReversedZ = true;      ///< Compile-time flag: depth buffer is reversed.
constexpr float    kDepthFar  = 0.0f;      ///< Far plane depth value (clear target).
constexpr float    kDepthNear = 1.0f;      ///< Near plane depth value.

/// Shadow maps use standard-Z (0=near, 1=far) even though the main view uses reversed-Z.
/// This matches UE5 (ShadowDepthRendering.cpp:704, ShadowSetup.cpp:907).
/// Shadow ortho projections (XMMatrixOrthographicLH) naturally produce standard-Z.
/// The shadow comparison sampler (SCF_Less) and clear value (1.0) are tuned for this space.
constexpr float    kShadowDepthClear = 1.0f;  ///< Shadow map clear value (standard-Z far plane).
constexpr float    kShadowDepthNear  = 0.0f;  ///< Shadow depth near (standard-Z).
constexpr float    kShadowDepthFar   = 1.0f;  ///< Shadow depth far (standard-Z).

// ─────────────────────────────────────────────────────────────────────────────
// Resource Handles — typed generational IDs for GPU objects
// ─────────────────────────────────────────────────────────────────────────────

/// Typed generational handle for GPU resources.
///
/// Combines a slot index with a generation counter so that stale handles
/// (pointing to freed/reallocated slots) are safely detected at O(1) cost.
/// This is the primary mechanism for preventing use-after-free of GPU resources.
///
/// # Type Safety
/// The `Tag` parameter prevents mixing handle types at compile time — you cannot
/// accidentally pass a `RHIBufferHandle` where a `RHITextureHandle` is expected.
///
/// # Examples
/// ```cpp
/// RHIBufferHandle buffer = device->CreateBuffer(desc);
/// if (buffer.IsValid()) {
///     // Use the buffer...
///     device->DestroyBuffer(buffer);
/// }
/// ```
template <typename Tag>
struct RHIHandle {
    uint32_t index = 0;      ///< Slot index in the backend's resource pool.
    uint32_t generation = 0; ///< Generation counter. 0 means invalid/never-allocated.

    /// Returns true if this handle was ever created (`generation != 0`).
    ///
    /// # Returns
    /// `true` if the handle may reference a valid resource.
    /// Note: a valid handle can still be stale if the resource was destroyed.
    bool IsValid() const { return generation != 0; }

    /// Creates an invalid handle sentinel (index=0, generation=0).
    ///
    /// # Returns
    /// A handle that always returns `false` from `IsValid()`.
    [[nodiscard]] static RHIHandle Invalid() { return {0, 0}; }

    bool operator==(RHIHandle other) const { return index == other.index && generation == other.generation; }
    bool operator!=(RHIHandle other) const { return !(*this == other); }
};

/// Tag type for `RHIHandle` — identifies buffer resources.
struct BufferTag {};

/// Tag type for `RHIHandle` — identifies texture resources.
struct TextureTag {};

/// Tag type for `RHIHandle` — identifies pipeline state objects.
struct PipelineTag {};

/// Tag type for `RHIHandle` — identifies compiled shader modules.
struct ShaderTag {};

/// Tag type for `RHIHandle` — identifies texture sampler states.
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
///
/// Maps 1:1 to `DXGI_FORMAT` on DX12, `VkFormat` on Vulkan, `MTLPixelFormat` on Metal.
enum class RHIFormat : uint8_t {
    Unknown = 0,              ///< Invalid/uninitialized format.

    // ── Color formats ────────────────────────────────────────────────────

    R8Unorm,                  ///< Single channel, 8-bit unsigned normalized [0,1].
    RG8Unorm,                 ///< Two channels, 8-bit unsigned normalized.
    RGBA8Unorm,               ///< Four channels, 8-bit unsigned normalized (linear).
    RGBA8Srgb,                ///< Four channels, 8-bit sRGB gamma-encoded.
    BGRA8Unorm,               ///< Four channels, 8-bit BGRA order (swap chain format on Windows).
    BGRA8Srgb,                ///< Four channels, 8-bit BGRA sRGB.
    R16Float,                 ///< Single channel, 16-bit half-float.
    RG16Float,                ///< Two channels, 16-bit half-float.
    RGBA16Float,              ///< Four channels, 16-bit half-float (HDR render targets).
    R32Float,                 ///< Single channel, 32-bit float.
    RG32Float,                ///< Two channels, 32-bit float.
    RGB32Float,               ///< Three channels, 32-bit float (vertex positions).
    RGBA32Float,              ///< Four channels, 32-bit float.
    R32Uint,                  ///< Single channel, 32-bit unsigned integer.
    RG32Uint,                 ///< Two channels, 32-bit unsigned integer.
    RGBA32Uint,               ///< Four channels, 32-bit unsigned integer.

    // ── Depth/stencil formats ────────────────────────────────────────────

    D16Unorm,                 ///< 16-bit depth, no stencil.
    D32Float,                 ///< 32-bit float depth, no stencil.
    D24UnormS8Uint,           ///< 24-bit depth + 8-bit stencil.
    D32FloatS8Uint,           ///< 32-bit float depth + 8-bit stencil (+ 24 padding).
};

/// Returns the number of bytes per texel for the given format.
///
/// # Returns
/// Bytes per texel (1–16), or 0 for `Unknown`.
///
/// # Notes
/// Block-compressed formats are not yet supported.
[[nodiscard]] constexpr uint32_t BytesPerTexel(RHIFormat format) {
    switch (format) {
        case RHIFormat::R8Unorm:        return 1;
        case RHIFormat::RG8Unorm:       return 2;
        case RHIFormat::RGBA8Unorm:     return 4;
        case RHIFormat::RGBA8Srgb:      return 4;
        case RHIFormat::BGRA8Unorm:     return 4;
        case RHIFormat::BGRA8Srgb:      return 4;
        case RHIFormat::R16Float:       return 2;
        case RHIFormat::RG16Float:      return 4;
        case RHIFormat::RGBA16Float:    return 8;
        case RHIFormat::R32Float:       return 4;
        case RHIFormat::RG32Float:      return 8;
        case RHIFormat::RGB32Float:     return 12;
        case RHIFormat::RGBA32Float:    return 16;
        case RHIFormat::R32Uint:        return 4;
        case RHIFormat::RG32Uint:       return 8;
        case RHIFormat::RGBA32Uint:     return 16;
        case RHIFormat::D16Unorm:       return 2;
        case RHIFormat::D32Float:       return 4;
        case RHIFormat::D24UnormS8Uint: return 4;
        case RHIFormat::D32FloatS8Uint: return 8;
        default:                        return 0;
    }
}

/// Buffer usage flags (combinable via bitwise OR).
///
/// Multiple flags can be combined: `VertexBuffer | CopySrc`.
/// The backend uses these to set appropriate D3D12 resource flags and initial state.
enum class RHIBufferUsage : uint32_t {
    None             = 0,        ///< No usage specified (invalid for creation).
    VertexBuffer     = 1 << 0,   ///< Vertex buffer for input assembler binding.
    IndexBuffer      = 1 << 1,   ///< Index buffer for indexed draw calls.
    ConstantBuffer   = 1 << 2,   ///< Constant buffer (256-byte aligned automatically).
    StructuredBuffer = 1 << 3,   ///< Structured buffer with per-element stride (creates SRV).
    UnorderedAccess  = 1 << 4,   ///< Read-write access from compute/pixel shaders (creates UAV).
    IndirectArgs     = 1 << 5,   ///< Indirect draw/dispatch argument buffer.
    CopySrc          = 1 << 6,   ///< Source for GPU copy operations.
    CopyDst          = 1 << 7,   ///< Destination for GPU copy operations.
};

inline RHIBufferUsage operator|(RHIBufferUsage a, RHIBufferUsage b) {
    return static_cast<RHIBufferUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline RHIBufferUsage operator&(RHIBufferUsage a, RHIBufferUsage b) {
    return static_cast<RHIBufferUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
/// Tests whether a specific usage flag is set.
///
/// # Arguments
/// * `flags` — Combined flags to test against.
/// * `test` — Single flag to check for.
///
/// # Returns
/// `true` if `test` is present in `flags`.
inline bool HasFlag(RHIBufferUsage flags, RHIBufferUsage test) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(test)) != 0;
}

/// Texture usage flags (combinable via bitwise OR).
///
/// Multiple flags can be combined: `ShaderResource | RenderTarget`.
/// The backend creates appropriate descriptors (SRV, RTV, DSV, UAV) based on flags.
enum class RHITextureUsage : uint32_t {
    None            = 0,        ///< No usage specified (invalid for creation).
    ShaderResource  = 1 << 0,   ///< Readable from shaders (creates SRV).
    RenderTarget    = 1 << 1,   ///< Color render target (creates RTV).
    DepthStencil    = 1 << 2,   ///< Depth/stencil target (creates DSV).
    UnorderedAccess = 1 << 3,   ///< Read-write from compute/pixel shaders (creates UAV).
    CopySrc         = 1 << 4,   ///< Source for GPU copy operations.
    CopyDst         = 1 << 5,   ///< Destination for GPU copy operations.
};

inline RHITextureUsage operator|(RHITextureUsage a, RHITextureUsage b) {
    return static_cast<RHITextureUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline RHITextureUsage operator&(RHITextureUsage a, RHITextureUsage b) {
    return static_cast<RHITextureUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
/// Tests whether a specific texture usage flag is set.
inline bool HasFlag(RHITextureUsage flags, RHITextureUsage test) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(test)) != 0;
}

/// Resource state for barrier transitions.
///
/// Backends map these to native states (`D3D12_RESOURCE_STATES`, `VkImageLayout`, etc.).
/// Use `ResourceBarrier()` on a command list to transition resources between states.
enum class RHIResourceState : uint32_t {
    Undefined         = 0,        ///< Resource state is unknown (initial state for some resources).
    Common            = 1 << 0,   ///< General-purpose state (D3D12_RESOURCE_STATE_COMMON).
    VertexBuffer      = 1 << 1,   ///< Bound as vertex buffer.
    IndexBuffer       = 1 << 2,   ///< Bound as index buffer.
    ConstantBuffer    = 1 << 3,   ///< Bound as constant buffer.
    ShaderResource    = 1 << 4,   ///< Read by pixel and non-pixel shaders (SRV).
    UnorderedAccess   = 1 << 5,   ///< Read-write from shaders (UAV).
    RenderTarget      = 1 << 6,   ///< Bound as color render target (RTV).
    DepthStencilWrite = 1 << 7,   ///< Bound as depth-stencil for writing.
    DepthStencilRead  = 1 << 8,   ///< Bound as depth-stencil for read-only access.
    CopySrc           = 1 << 9,   ///< Source of a GPU copy operation.
    CopyDst           = 1 << 10,  ///< Destination of a GPU copy operation.
    Present           = 1 << 11,  ///< Ready for presentation (swap chain back buffer).
    IndirectArgument  = 1 << 12,  ///< Used as indirect draw/dispatch argument buffer.
};

/// Primitive topology for draw calls.
///
/// Determines how vertices are assembled into primitives by the input assembler.
enum class RHIPrimitiveTopology : uint8_t {
    TriangleList,   ///< Every 3 vertices form an independent triangle.
    TriangleStrip,  ///< Each vertex after the first two forms a triangle with the previous two.
    LineList,       ///< Every 2 vertices form an independent line segment.
    LineStrip,      ///< Each vertex after the first forms a line with the previous vertex.
    PointList,      ///< Each vertex is rendered as a single point.
};

/// Depth/stencil comparison function.
///
/// Used in depth testing and sampler comparison operations.
enum class RHICompareFunc : uint8_t {
    Never,        ///< Comparison always fails.
    Less,         ///< Passes if source < destination.
    Equal,        ///< Passes if source == destination.
    LessEqual,    ///< Passes if source <= destination.
    Greater,      ///< Passes if source > destination.
    NotEqual,     ///< Passes if source != destination.
    GreaterEqual, ///< Passes if source >= destination.
    Always,       ///< Comparison always passes.
};

/// Blend factor for color/alpha blending equations.
///
/// Multiplied with source or destination color in the blend equation:
/// `result = srcFactor * srcColor OP dstFactor * dstColor`
enum class RHIBlendFactor : uint8_t {
    Zero,          ///< Factor is `(0, 0, 0, 0)`.
    One,           ///< Factor is `(1, 1, 1, 1)`.
    SrcColor,      ///< Factor is the source color `(Rs, Gs, Bs, As)`.
    InvSrcColor,   ///< Factor is `(1-Rs, 1-Gs, 1-Bs, 1-As)`.
    SrcAlpha,      ///< Factor is `(As, As, As, As)`.
    InvSrcAlpha,   ///< Factor is `(1-As, 1-As, 1-As, 1-As)`.
    DstColor,      ///< Factor is the destination color `(Rd, Gd, Bd, Ad)`.
    InvDstColor,   ///< Factor is `(1-Rd, 1-Gd, 1-Bd, 1-Ad)`.
    DstAlpha,      ///< Factor is `(Ad, Ad, Ad, Ad)`.
    InvDstAlpha,   ///< Factor is `(1-Ad, 1-Ad, 1-Ad, 1-Ad)`.
};

/// Blend operation applied between source and destination factors.
enum class RHIBlendOp : uint8_t {
    Add,          ///< `result = src + dst`
    Subtract,     ///< `result = src - dst`
    RevSubtract,  ///< `result = dst - src`
    Min,          ///< `result = min(src, dst)`
    Max,          ///< `result = max(src, dst)`
};

/// Triangle cull mode for the rasterizer.
enum class RHICullMode : uint8_t {
    None,   ///< No culling — both front and back faces are rendered.
    Front,  ///< Cull front-facing triangles.
    Back,   ///< Cull back-facing triangles (most common).
};

/// Polygon fill mode for the rasterizer.
enum class RHIFillMode : uint8_t {
    Solid,      ///< Filled triangles (default).
    Wireframe,  ///< Wireframe rendering (edges only).
};

/// Index buffer element format.
enum class RHIIndexFormat : uint8_t {
    Uint16,  ///< 16-bit indices (max 65535 vertices).
    Uint32,  ///< 32-bit indices (max ~4 billion vertices).
};

/// Shader compilation stage.
enum class RHIShaderStage : uint8_t {
    Vertex,   ///< Vertex shader stage.
    Pixel,    ///< Pixel (fragment) shader stage.
    Compute,  ///< Compute shader stage.
};

/// GPU command queue type.
///
/// Different queue types have different capabilities and may execute in parallel.
enum class RHIQueueType : uint8_t {
    Graphics,  ///< Full graphics + compute + copy capability.
    Compute,   ///< Compute + copy only (no rasterization).
    Copy,      ///< DMA copy only (no shader execution).
};

/// Texture sampler filter mode.
enum class RHIFilterMode : uint8_t {
    Point,       ///< Nearest-neighbor filtering (pixelated).
    Linear,      ///< Bilinear/trilinear filtering (smooth).
    Anisotropic, ///< Anisotropic filtering (high quality for oblique surfaces).
};

/// Texture sampler address mode (wrap/clamp/mirror).
///
/// Determines how texture coordinates outside `[0, 1]` are handled.
enum class RHIAddressMode : uint8_t {
    Wrap,    ///< Tile the texture (repeat).
    Clamp,   ///< Clamp to edge color.
    Mirror,  ///< Mirror the texture at boundaries.
    Border,  ///< Use a fixed border color.
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

/// Describes a GPU buffer to create via `IRHIDevice::CreateBuffer()`.
///
/// # Examples
/// ```cpp
/// RHIBufferDesc desc{};
/// desc.sizeBytes = sizeof(Vertex) * vertexCount;
/// desc.stride = sizeof(Vertex);
/// desc.usage = RHIBufferUsage::VertexBuffer;
/// desc.memory = RHIMemoryType::Default;
/// desc.debugName = "MeshVertexBuffer";
/// auto handle = device->CreateBuffer(desc);
/// ```
struct RHIBufferDesc {
    uint64_t sizeBytes = 0;                          ///< Total size in bytes
    uint32_t stride = 0;                             ///< Element stride (for structured buffers)
    RHIBufferUsage usage = RHIBufferUsage::None;     ///< Usage flags
    RHIMemoryType memory = RHIMemoryType::Default;   ///< Memory heap type
    const char* debugName = nullptr;                 ///< Debug label (stripped in shipping)
};

/// Describes a GPU texture to create via `IRHIDevice::CreateTexture()`.
///
/// Supports 2D textures, 2D arrays, and cube maps. The backend automatically
/// creates appropriate descriptor views (SRV, RTV, DSV, UAV) based on usage flags.
struct RHITextureDesc {
    uint32_t width = 1;                                 ///< Width in texels
    uint32_t height = 1;                                ///< Height in texels
    uint16_t depthOrArraySize = 1;                      ///< Depth (3D) or array size
    uint16_t mipLevels = 1;                             ///< Number of mip levels
    RHIFormat format = RHIFormat::RGBA8Unorm;           ///< Texel format
    RHITextureUsage usage = RHITextureUsage::ShaderResource; ///< Usage flags
    const char* debugName = nullptr;                    ///< Debug label
};

/// Describes a swap chain to create via `IRHIDevice::CreateSwapChain()`.
struct RHISwapChainDesc {
    void* windowHandle = nullptr;    ///< Native window handle (HWND on Windows, Window cast to void* on Linux)
    void* displayHandle = nullptr;   ///< Native display handle (unused on Windows, Display* on Linux/X11)
    uint32_t width = 0;              ///< Back buffer width
    uint32_t height = 0;             ///< Back buffer height
    uint32_t bufferCount = 3;        ///< Number of back buffers (triple-buffered default)
    RHIFormat format = RHIFormat::RGBA8Unorm; ///< Back buffer format
    bool vsync = true;               ///< Enable vertical sync
};

/// Describes a compiled shader module to create via `IRHIDevice::CreateShader()`.
///
/// The bytecode pointer is copied internally; the caller's buffer need not persist.
struct RHIShaderDesc {
    const void* bytecode = nullptr;  ///< Pointer to compiled shader bytecode (DXIL/SPIR-V/MSL)
    uint64_t bytecodeSize = 0;       ///< Size of bytecode in bytes
    RHIShaderStage stage = RHIShaderStage::Vertex; ///< Shader stage
    const char* entryPoint = "main"; ///< Entry point function name
    const char* debugName = nullptr; ///< Debug label
};

/// Blend state for a single render target in a graphics pipeline.
///
/// Defaults to opaque (no blending). For alpha blending, set `blendEnable = true`
/// and configure the blend factors.
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
    RHICompareFunc depthFunc = RHICompareFunc::Greater; ///< Reversed-Z: closer = greater depth value.
};

/// Rasterizer state for pipeline creation.
struct RHIRasterizerDesc {
    RHICullMode cullMode = RHICullMode::Back;
    RHIFillMode fillMode = RHIFillMode::Solid;
    bool frontCounterClockwise = false;
    float depthBias = 0.0f;
    float slopeScaledDepthBias = 0.0f;
};

/// Describes a graphics pipeline state object (PSO) to create via
/// `IRHIDevice::CreateGraphicsPipeline()`.
///
/// Combines shaders, rasterizer state, depth-stencil state, blend state,
/// render target formats, and topology into a single immutable PSO.
///
/// Semantic name for a vertex input element.
///
/// Matches HLSL semantics (POSITION, NORMAL, TEXCOORD, etc.).
/// Used by the Input Assembler when SM < 6.6 (no bindless vertex fetch).
enum class RHIInputSemantic : uint8_t {
    Position,   ///< POSITION — vertex position (float2/float3/float4)
    Normal,     ///< NORMAL — surface normal (float3)
    Texcoord,   ///< TEXCOORD — texture coordinate (float2/float3/float4)
    Tangent,    ///< TANGENT — surface tangent (float3/float4)
    Color,      ///< COLOR — vertex color (float4)
};

/// Returns the HLSL semantic string for an input semantic enum value.
///
/// # Returns
/// A null-terminated string like `"POSITION"`, `"NORMAL"`, etc.
[[nodiscard]] constexpr const char* InputSemanticToString(RHIInputSemantic semantic) {
    switch (semantic) {
        case RHIInputSemantic::Position: return "POSITION";
        case RHIInputSemantic::Normal:   return "NORMAL";
        case RHIInputSemantic::Texcoord: return "TEXCOORD";
        case RHIInputSemantic::Tangent:  return "TANGENT";
        case RHIInputSemantic::Color:    return "COLOR";
    }
    return "POSITION";
}

/// Describes a single vertex input element for the Input Assembler.
///
/// Used only for SM5 fallback pipelines — SM6.6 bindless pipelines
/// fetch vertex data from SRV buffers directly in the shader.
///
/// # Examples
/// ```cpp
/// RHIInputElementDesc elements[] = {
///     { RHIInputSemantic::Position, 0, RHIFormat::RGB32Float,  0, 0 },
///     { RHIInputSemantic::Normal,   0, RHIFormat::RGB32Float,  0, 12 },
///     { RHIInputSemantic::Texcoord, 0, RHIFormat::RG32Float,   0, 24 },
/// };
/// ```
struct RHIInputElementDesc {
    RHIInputSemantic semantic = RHIInputSemantic::Position; ///< HLSL semantic name
    uint32_t semanticIndex = 0;  ///< Semantic index (e.g., TEXCOORD0 vs TEXCOORD1)
    RHIFormat format = RHIFormat::RGB32Float; ///< Element format
    uint32_t inputSlot = 0;      ///< Input assembler slot (0–15)
    uint32_t byteOffset = 0;     ///< Byte offset within the vertex stride
};

/// Describes a graphics pipeline state object (shaders, rasterizer, blend, etc.).
///
/// # SM6.6 Bindless
/// When running SM6.6+, `inputElementCount` should be 0 — vertex data is
/// pulled from SRV buffers in the shader via `ResourceDescriptorHeap[index]`.
///
/// # SM5 Fallback
/// When running SM5.x, provide `inputElements` and `inputElementCount` to
/// describe the vertex layout for the Input Assembler. The engine automatically
/// selects the correct path based on the negotiated shader model.
///
/// `vertexShader` is required; `pixelShader` is optional (depth-only passes).
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
    const RHIInputElementDesc* inputElements = nullptr; ///< Vertex input layout (SM5 only, nullptr for bindless)
    uint32_t inputElementCount = 0;                  ///< Number of input elements (0 for SM6.6 bindless)
    uint32_t vertexStride = 0;                       ///< Vertex stride in bytes (SM5 only, 0 for bindless)
    const char* debugName = nullptr;                 ///< Debug label

    /// Optional cached PSO blob from a previous compilation.
    ///
    /// DX12: from `ID3D12PipelineState::GetCachedBlob()`.
    /// Vulkan: from `VkPipelineCache` (handled natively, not used here).
    /// Metal: ignored.
    ///
    /// When provided, the driver can skip shader compilation and use the
    /// cached state directly. These fields are NOT included in `PsoCacheKey`
    /// computation (they are not part of PSO identity).
    const void* cachedBlobData = nullptr;             ///< Pointer to cached PSO blob (optional)
    size_t cachedBlobSize = 0;                        ///< Size of cached PSO blob in bytes
};

/// Describes a compute pipeline state object (PSO).
///
/// Only requires a compute shader — no rasterizer, blend, or depth-stencil state.
/// The compute pipeline shares the same global root signature as graphics pipelines.
///
/// # Examples
/// ```cpp
/// RHIComputePipelineDesc desc{};
/// desc.computeShader = csHandle;
/// desc.debugName = "DownsampleCS";
/// auto pipeline = device->CreateComputePipeline(desc);
/// ```
struct RHIComputePipelineDesc {
    RHIShaderHandle computeShader;              ///< Compute shader handle (required, must be RHIShaderStage::Compute)
    const char* debugName = nullptr;            ///< Debug label (stripped in shipping)
    const void* cachedBlobData = nullptr;       ///< Optional cached PSO blob from a previous compilation
    size_t cachedBlobSize = 0;                  ///< Size of cached PSO blob in bytes
};

/// Describes a texture sampler to create via `IRHIDevice::CreateSampler()`.
///
/// Defaults to linear filtering, wrap addressing, full LOD range.
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

/// Viewport rectangle for the rasterizer, in floating-point pixel coordinates.
struct RHIViewport {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float minDepth = 0.0f;
    float maxDepth = 1.0f;
};

/// Scissor rectangle in integer pixel coordinates for pixel clipping.
struct RHIScissorRect {
    int32_t left = 0;
    int32_t top = 0;
    int32_t right = 0;
    int32_t bottom = 0;
};

/// Resource barrier descriptor for state transitions.
///
/// Exactly one of `texture` or `buffer` should be valid; the other should be `Invalid()`.
struct RHIBarrierDesc {
    RHITextureHandle texture;          ///< Texture to transition (or invalid for buffer)
    RHIBufferHandle buffer;            ///< Buffer to transition (or invalid for texture)
    RHIResourceState stateBefore;      ///< State before barrier
    RHIResourceState stateAfter;       ///< State after barrier
};

/// Render pass color attachment descriptor.
///
/// Specifies a color render target and optional clear-on-load behavior.
struct RHIRenderPassColorAttachment {
    RHITextureHandle texture;                   ///< Render target texture
    float clearColor[4] = {0, 0, 0, 1};        ///< Clear color (used if loadOp is Clear)
    bool clear = false;                         ///< True to clear on load
};

/// Render pass depth-stencil attachment descriptor.
///
/// Specifies a depth-stencil target and optional clear-on-load behavior.
struct RHIRenderPassDepthAttachment {
    RHITextureHandle texture;                   ///< Depth-stencil texture
    float clearDepth = kDepthFar;               ///< Clear depth value (defaults to reversed-Z far=0.0; shadow passes override to kShadowDepthClear=1.0).
    uint8_t clearStencil = 0;                   ///< Clear stencil value
    bool clear = false;                         ///< True to clear on load
};

/// Describes a render pass scope for `IRHICommandList::BeginRenderPass()`.
///
/// Supports up to 8 color attachments and an optional depth-stencil attachment.
struct RHIRenderPassDesc {
    RHIRenderPassColorAttachment colorAttachments[8] = {};
    uint32_t colorAttachmentCount = 0;
    RHIRenderPassDepthAttachment depthAttachment;
    bool hasDepth = false;
};

/// Arguments for an indirect indexed draw call (matches GPU layout).
///
/// Layout is identical to `D3D12_DRAW_INDEXED_ARGUMENTS` and
/// `VkDrawIndexedIndirectCommand`. Must be tightly packed (20 bytes).
/// Written by GPU compute shaders for GPU-driven rendering.
struct RHIDrawIndexedIndirectArgs {
    uint32_t indexCountPerInstance;   ///< Number of indices per instance.
    uint32_t instanceCount;          ///< Number of instances to draw.
    uint32_t startIndexLocation;     ///< Offset into the index buffer.
    int32_t  baseVertexLocation;     ///< Value added to each index before fetching.
    uint32_t startInstanceLocation;  ///< Offset into per-instance data.
};

/// Arguments for an indirect dispatch call (matches GPU layout).
///
/// Layout is identical to `D3D12_DISPATCH_ARGUMENTS` and
/// `VkDispatchIndirectCommand`. Must be tightly packed (12 bytes).
struct RHIDispatchIndirectArgs {
    uint32_t threadGroupCountX;  ///< Thread groups in X dimension.
    uint32_t threadGroupCountY;  ///< Thread groups in Y dimension.
    uint32_t threadGroupCountZ;  ///< Thread groups in Z dimension.
};

/// Memory budget information reported by the device.
struct RHIMemoryBudget {
    uint64_t budgetBytes = 0;     ///< Total memory budget for the adapter
    uint64_t usageBytes = 0;      ///< Current memory usage
};

// ─────────────────────────────────────────────────────────────────────────────
// Root Signature Layout — backend-agnostic register mapping
// ─────────────────────────────────────────────────────────────────────────────

/// Describes a single root parameter binding point.
///
/// Maps a root parameter index (used by `SetRootCBV`, `SetRootConstants`, etc.)
/// to an HLSL register number. DX12 encodes this in the root signature natively;
/// DX11 needs it to translate root param indices to `VSSetConstantBuffers` slots.
///
/// # Examples
/// ```cpp
/// // Root param 3 maps to HLSL register b4 (shadow data)
/// RHIRootParamDesc param{};
/// param.type = RHIRootParamDesc::Type::CBV;
/// param.shaderRegister = 4;  // b4
/// ```
struct RHIRootParamDesc {
    /// The type of root parameter.
    enum class Type : uint8_t {
        Constants,   ///< Inline root constants (maps to a cbuffer register).
        CBV,         ///< Root constant buffer view.
        SRV_Table,   ///< Descriptor table of SRVs.
        UAV_Table,   ///< Descriptor table of UAVs.
    };

    Type type = Type::CBV;           ///< Parameter type.
    uint32_t shaderRegister = 0;     ///< HLSL register number (e.g., 4 for b4, 0 for t0).
    uint32_t registerSpace = 0;      ///< Register space (usually 0).
    uint32_t num32BitValues = 0;     ///< Number of 32-bit values (only for Type::Constants).
};

/// Describes the root signature layout — shared by all backends.
///
/// Built once at device init time. DX11 uses this to translate root parameter
/// indices to HLSL register numbers when binding constant buffers. DX12 builds
/// this alongside the native root signature for validation and parity.
///
/// The layout must match the DX12 root signature parameter order exactly:
/// ```
/// param[0] = { Constants, b0, space0, 4 values }
/// param[1] = { CBV,       b1, space0 }  // PerFrame
/// param[2] = { CBV,       b2, space0 }  // PerObject
/// param[3] = { CBV,       b4, space0 }  // Shadow
/// param[4] = { UAV_Table, u0, space0 }
/// param[5] = { SRV_Table, t0, space0 }  // SM5 only
/// ```
struct RHIRootSignatureDesc {
    static constexpr uint32_t kMaxParams = 8;
    RHIRootParamDesc params[kMaxParams] = {};
    uint32_t paramCount = 0;
};

} // namespace uz
