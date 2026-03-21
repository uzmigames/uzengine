#pragma once

/// Metal backend resource storage types.
///
/// Each resource type holds the native Metal objects, descriptor indices, and
/// creation parameters. Resources are stored in flat arrays indexed by
/// `RHIHandle::index`. The handle's generation counter validates access.
///
/// # Memory Model
/// Metal manages GPU memory through storage modes:
/// - `MTLStorageModePrivate` — GPU-only (fastest GPU access, no CPU mapping).
/// - `MTLStorageModeShared` — CPU and GPU coherent (upload/readback).
/// - `MTLStorageModeManaged` — CPU-cached, GPU-visible with explicit synchronization (macOS only).
///
/// # Forward Declarations
/// All metal-cpp types are forward-declared to avoid pulling in the Metal
/// framework headers. Backend implementation files include the actual headers.

#include "../rhi_types.h"
#include "../rhi_resource_pool.h"
#include "log.h"

#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// Forward declarations for metal-cpp types (avoids dependency on metal-cpp headers)
// ─────────────────────────────────────────────────────────────────────────────

namespace MTL {
    class Buffer;
    class Texture;
    class Function;
    class Library;
    class RenderPipelineState;
    class ComputePipelineState;
    class SamplerState;
} // namespace MTL

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Log Category
// ─────────────────────────────────────────────────────────────────────────────

UZ_DECLARE_LOG_CATEGORY(MetalRHI);

// ─────────────────────────────────────────────────────────────────────────────
// MetalBuffer — GPU buffer with optional CPU mapping
// ─────────────────────────────────────────────────────────────────────────────

/// Internal storage for a Metal buffer resource.
///
/// Managed by `MetalDevice`'s `ResourcePool<MetalBuffer>`. Not directly
/// visible to application code — accessed only via `RHIBufferHandle`.
///
/// Metal buffers use storage modes instead of separate heap types:
/// - Private storage for GPU-only buffers (Default memory type).
/// - Shared storage for CPU-visible buffers (Upload/Readback memory types).
struct MetalBuffer {
    MTL::Buffer* buffer = nullptr;          ///< Native Metal buffer object
    void*        mappedPtr = nullptr;       ///< CPU mapping for Shared/Managed storage (null for Private)
    uint64_t     gpuAddress = 0;            ///< Buffer GPU address (Metal 3+ gpuAddress property)
    uint32_t     srvIndex = UINT32_MAX;     ///< Argument buffer descriptor index for shader access
    uint32_t     generation = 0;            ///< Matches RHIHandle generation for use-after-free detection
    RHIBufferDesc desc = {};                ///< Creation parameters (size, stride, usage, memory type)
};

// ─────────────────────────────────────────────────────────────────────────────
// MetalTexture — GPU texture with descriptor indices
// ─────────────────────────────────────────────────────────────────────────────

/// Internal storage for a Metal texture resource.
///
/// Managed by `MetalDevice`'s `ResourcePool<MetalTexture>`. Metal textures
/// do not track layout state — the Metal runtime handles hazard tracking
/// automatically within a single command encoder. Cross-encoder dependencies
/// use explicit MTLFence or MTLEvent synchronization.
///
/// # Swapchain Textures
/// Textures obtained from `CAMetalDrawable` set `ownsTexture = false` so the
/// backend does not release them — the drawable pool manages their lifetime.
struct MetalTexture {
    MTL::Texture* texture = nullptr;        ///< Native Metal texture object
    uint32_t      srvIndex = UINT32_MAX;    ///< Argument buffer SRV index (shader read)
    uint32_t      rtvIndex = UINT32_MAX;    ///< Render target index (internal tracking for attachment binding)
    uint32_t      dsvIndex = UINT32_MAX;    ///< Depth-stencil index (internal tracking for depth attachment)
    uint32_t      generation = 0;           ///< Matches RHIHandle generation for use-after-free detection
    RHITextureDesc desc = {};               ///< Creation parameters (dimensions, format, usage)
    bool          ownsTexture = true;       ///< False for swapchain textures from CAMetalDrawable
};

// ─────────────────────────────────────────────────────────────────────────────
// MetalShader — Compiled MSL shader function
// ─────────────────────────────────────────────────────────────────────────────

/// Internal storage for a compiled Metal shader function.
///
/// Metal shaders are compiled from MSL (Metal Shading Language) source into
/// a `MTLLibrary`, then individual functions are extracted by entry point name.
/// The library owns the compiled binary — it must outlive all functions
/// extracted from it.
///
/// # Shader Pipeline
/// HLSL → SPIR-V (canonical) → MSL (via spirv_transpile.c) → MTLLibrary → MTLFunction
struct MetalShader {
    MTL::Function* function = nullptr;      ///< Compiled shader function (entry point)
    MTL::Library*  library = nullptr;       ///< Parent library that owns the compiled MSL binary
    RHIShaderStage stage = RHIShaderStage::Vertex; ///< Shader stage (Vertex, Pixel, Compute)
    uint32_t       generation = 0;          ///< Matches RHIHandle generation for use-after-free detection
};

// ─────────────────────────────────────────────────────────────────────────────
// MetalSampler — Texture sampler state
// ─────────────────────────────────────────────────────────────────────────────

/// Internal storage for a Metal sampler state.
///
/// Metal sampler states are immutable once created. The `samplerIndex` is
/// used for argument buffer binding in the tier 2 bindless model.
struct MetalSampler {
    MTL::SamplerState* sampler = nullptr;       ///< Native Metal sampler state object
    uint32_t           samplerIndex = UINT32_MAX; ///< Argument buffer sampler descriptor index
    uint32_t           generation = 0;          ///< Matches RHIHandle generation for use-after-free detection
};

// ─────────────────────────────────────────────────────────────────────────────
// MetalPipeline — Graphics or compute pipeline state object
// ─────────────────────────────────────────────────────────────────────────────

/// Internal storage for a Metal pipeline state object (PSO).
///
/// Metal separates graphics and compute pipelines into distinct types.
/// A MetalPipeline holds at most one of `renderPipeline` or `computePipeline`.
/// The `topology` field is stored separately because Metal sets primitive
/// topology at draw time, not in the PSO (unlike DX12/Vulkan).
///
/// # Depth-Stencil State
/// Metal does not bake depth-stencil state into the render pipeline descriptor.
/// Depth-stencil state is set on the render command encoder at draw time.
/// The `desc` field preserves the original creation parameters for this purpose.
struct MetalPipeline {
    MTL::RenderPipelineState*  renderPipeline = nullptr;  ///< Graphics pipeline (null for compute)
    MTL::ComputePipelineState* computePipeline = nullptr; ///< Compute pipeline (null for graphics)
    RHIPrimitiveTopology       topology = RHIPrimitiveTopology::TriangleList; ///< Draw-time primitive type
    uint32_t                   generation = 0;            ///< Matches RHIHandle generation
    RHIGraphicsPipelineDesc    desc = {};                 ///< Original creation parameters
};

// ─────────────────────────────────────────────────────────────────────────────
// Format Conversion — RHI enums to Metal integer constants
// ─────────────────────────────────────────────────────────────────────────────

/// Metal pixel format integer values (from MTLPixelFormat enum).
///
/// Defined as named constants to avoid magic numbers while keeping this header
/// free of Metal framework dependencies. Values match the Metal API exactly.
namespace MetalPixelFormat {
    constexpr uint32_t kInvalid         = 0;
    constexpr uint32_t kR8Unorm         = 10;
    constexpr uint32_t kRG8Unorm        = 30;
    constexpr uint32_t kRGBA8Unorm      = 70;
    constexpr uint32_t kRGBA8Srgb       = 71;
    constexpr uint32_t kBGRA8Unorm      = 80;
    constexpr uint32_t kBGRA8Srgb       = 81;
    constexpr uint32_t kR16Float        = 25;
    constexpr uint32_t kRG16Float       = 65;
    constexpr uint32_t kRGBA16Float     = 115;
    constexpr uint32_t kR32Float        = 55;
    constexpr uint32_t kRG32Float       = 105;
    constexpr uint32_t kRGBA32Float     = 125;
    constexpr uint32_t kR32Uint         = 53;
    constexpr uint32_t kRG32Uint        = 103;
    constexpr uint32_t kRGBA32Uint      = 123;
    constexpr uint32_t kDepth16Unorm    = 250;
    constexpr uint32_t kDepth32Float    = 252;
    constexpr uint32_t kDepth24UnormStencil8 = 255;  ///< macOS only (not supported on iOS)
    constexpr uint32_t kDepth32FloatStencil8 = 260;
    // Block-compressed formats (macOS only — not available on iOS/tvOS)
    constexpr uint32_t kBC1_RGBA          = 130;
    constexpr uint32_t kBC1_RGBA_sRGB     = 131;
    constexpr uint32_t kBC3_RGBA          = 134;
    constexpr uint32_t kBC3_RGBA_sRGB     = 135;
    constexpr uint32_t kBC4_RUnorm        = 140;
    constexpr uint32_t kBC5_RGUnorm       = 142;
    constexpr uint32_t kBC7_RGBAUnorm     = 152;
    constexpr uint32_t kBC7_RGBAUnorm_sRGB = 153;
} // namespace MetalPixelFormat

/// Metal storage mode integer values (from MTLStorageMode enum).
namespace MetalStorageMode {
    constexpr uint32_t kShared  = 0;    ///< CPU and GPU coherent (Upload/Readback)
    constexpr uint32_t kManaged = 1;    ///< CPU-cached, explicit sync (macOS only)
    constexpr uint32_t kPrivate = 2;    ///< GPU-only (Default memory type)
} // namespace MetalStorageMode

/// Metal sampler address mode integer values (from MTLSamplerAddressMode enum).
namespace MetalSamplerAddressMode {
    constexpr uint32_t kClampToEdge    = 0;
    constexpr uint32_t kMirrorRepeat   = 1;
    constexpr uint32_t kRepeat         = 2;
    constexpr uint32_t kClampToBorder  = 3;  ///< Requires macOS 10.12+ / iOS 14+
} // namespace MetalSamplerAddressMode

/// Metal sampler min/mag filter integer values (from MTLSamplerMinMagFilter enum).
namespace MetalSamplerFilter {
    constexpr uint32_t kNearest = 0;
    constexpr uint32_t kLinear  = 1;
} // namespace MetalSamplerFilter

/// Metal compare function integer values (from MTLCompareFunction enum).
namespace MetalCompareFunction {
    constexpr uint32_t kNever        = 0;
    constexpr uint32_t kLess         = 1;
    constexpr uint32_t kEqual        = 2;
    constexpr uint32_t kLessEqual    = 3;
    constexpr uint32_t kGreater      = 4;
    constexpr uint32_t kNotEqual     = 5;
    constexpr uint32_t kGreaterEqual = 6;
    constexpr uint32_t kAlways       = 7;
} // namespace MetalCompareFunction

/// Metal primitive type integer values (from MTLPrimitiveType enum).
namespace MetalPrimitiveType {
    constexpr uint32_t kPoint         = 0;
    constexpr uint32_t kLine          = 1;
    constexpr uint32_t kLineStrip     = 2;
    constexpr uint32_t kTriangle      = 3;
    constexpr uint32_t kTriangleStrip = 4;
} // namespace MetalPrimitiveType

// ─────────────────────────────────────────────────────────────────────────────
// Conversion Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Converts an RHIFormat to the corresponding Metal pixel format integer value.
///
/// # Arguments
/// * `format` — RHI-agnostic format enum.
///
/// # Returns
/// The `MTLPixelFormat` integer value, or `MetalPixelFormat::kInvalid` (0) for `Unknown`.
[[nodiscard]] constexpr uint32_t ToMTLPixelFormat(RHIFormat format) {
    switch (format) {
        case RHIFormat::Unknown:         return MetalPixelFormat::kInvalid;
        case RHIFormat::R8Unorm:         return MetalPixelFormat::kR8Unorm;
        case RHIFormat::RG8Unorm:        return MetalPixelFormat::kRG8Unorm;
        case RHIFormat::RGBA8Unorm:      return MetalPixelFormat::kRGBA8Unorm;
        case RHIFormat::RGBA8Srgb:       return MetalPixelFormat::kRGBA8Srgb;
        case RHIFormat::BGRA8Unorm:      return MetalPixelFormat::kBGRA8Unorm;
        case RHIFormat::BGRA8Srgb:       return MetalPixelFormat::kBGRA8Srgb;
        case RHIFormat::R16Float:        return MetalPixelFormat::kR16Float;
        case RHIFormat::RG16Float:       return MetalPixelFormat::kRG16Float;
        case RHIFormat::RGBA16Float:     return MetalPixelFormat::kRGBA16Float;
        case RHIFormat::R32Float:        return MetalPixelFormat::kR32Float;
        case RHIFormat::RG32Float:       return MetalPixelFormat::kRG32Float;
        case RHIFormat::RGB32Float:      return MetalPixelFormat::kRGBA32Float; // Metal has no RGB32Float; promote to RGBA32
        case RHIFormat::RGBA32Float:     return MetalPixelFormat::kRGBA32Float;
        case RHIFormat::R32Uint:         return MetalPixelFormat::kR32Uint;
        case RHIFormat::RG32Uint:        return MetalPixelFormat::kRG32Uint;
        case RHIFormat::RGBA32Uint:      return MetalPixelFormat::kRGBA32Uint;
        case RHIFormat::D16Unorm:        return MetalPixelFormat::kDepth16Unorm;
        case RHIFormat::D32Float:        return MetalPixelFormat::kDepth32Float;
        case RHIFormat::D24UnormS8Uint:  return MetalPixelFormat::kDepth24UnormStencil8;
        case RHIFormat::D32FloatS8Uint:  return MetalPixelFormat::kDepth32FloatStencil8;
        case RHIFormat::BC1Unorm:        return MetalPixelFormat::kBC1_RGBA;
        case RHIFormat::BC1Srgb:         return MetalPixelFormat::kBC1_RGBA_sRGB;
        case RHIFormat::BC3Unorm:        return MetalPixelFormat::kBC3_RGBA;
        case RHIFormat::BC3Srgb:         return MetalPixelFormat::kBC3_RGBA_sRGB;
        case RHIFormat::BC4Unorm:        return MetalPixelFormat::kBC4_RUnorm;
        case RHIFormat::BC5Unorm:        return MetalPixelFormat::kBC5_RGUnorm;
        case RHIFormat::BC7Unorm:        return MetalPixelFormat::kBC7_RGBAUnorm;
        case RHIFormat::BC7Srgb:         return MetalPixelFormat::kBC7_RGBAUnorm_sRGB;
    }
    return MetalPixelFormat::kInvalid;
}

/// Converts an RHIMemoryType to the corresponding Metal storage mode integer value.
///
/// # Arguments
/// * `memory` — RHI memory type enum.
///
/// # Returns
/// The `MTLStorageMode` integer value:
/// - `Default` → `Private` (2) — GPU-only, fastest GPU access.
/// - `Upload` → `Shared` (0) — CPU-writable, GPU-readable.
/// - `Readback` → `Shared` (0) — GPU-writable, CPU-readable.
[[nodiscard]] constexpr uint32_t ToMTLStorageMode(RHIMemoryType memory) {
    switch (memory) {
        case RHIMemoryType::Default:  return MetalStorageMode::kPrivate;
        case RHIMemoryType::Upload:   return MetalStorageMode::kShared;
        case RHIMemoryType::Readback: return MetalStorageMode::kShared;
    }
    return MetalStorageMode::kPrivate;
}

/// Converts an RHIAddressMode to the corresponding Metal sampler address mode integer value.
///
/// # Arguments
/// * `mode` — RHI address mode enum.
///
/// # Returns
/// The `MTLSamplerAddressMode` integer value.
[[nodiscard]] constexpr uint32_t ToMTLSamplerAddressMode(RHIAddressMode mode) {
    switch (mode) {
        case RHIAddressMode::Wrap:   return MetalSamplerAddressMode::kRepeat;
        case RHIAddressMode::Clamp:  return MetalSamplerAddressMode::kClampToEdge;
        case RHIAddressMode::Mirror: return MetalSamplerAddressMode::kMirrorRepeat;
        case RHIAddressMode::Border: return MetalSamplerAddressMode::kClampToBorder;
    }
    return MetalSamplerAddressMode::kClampToEdge;
}

/// Converts an RHIFilterMode to the corresponding Metal sampler min/mag filter integer value.
///
/// # Arguments
/// * `filter` — RHI filter mode enum.
///
/// # Returns
/// The `MTLSamplerMinMagFilter` integer value:
/// - `Point` → `Nearest` (0).
/// - `Linear` → `Linear` (1).
/// - `Anisotropic` → `Linear` (1) — anisotropy is controlled by `maxAnisotropy`, not the filter enum.
[[nodiscard]] constexpr uint32_t ToMTLSamplerMinMagFilter(RHIFilterMode filter) {
    switch (filter) {
        case RHIFilterMode::Point:       return MetalSamplerFilter::kNearest;
        case RHIFilterMode::Linear:      return MetalSamplerFilter::kLinear;
        case RHIFilterMode::Anisotropic: return MetalSamplerFilter::kLinear;
    }
    return MetalSamplerFilter::kNearest;
}

/// Converts an RHICompareFunc to the corresponding Metal compare function integer value.
///
/// # Arguments
/// * `func` — RHI comparison function enum.
///
/// # Returns
/// The `MTLCompareFunction` integer value (0–7).
[[nodiscard]] constexpr uint32_t ToMTLCompareFunction(RHICompareFunc func) {
    switch (func) {
        case RHICompareFunc::Never:        return MetalCompareFunction::kNever;
        case RHICompareFunc::Less:         return MetalCompareFunction::kLess;
        case RHICompareFunc::Equal:        return MetalCompareFunction::kEqual;
        case RHICompareFunc::LessEqual:    return MetalCompareFunction::kLessEqual;
        case RHICompareFunc::Greater:      return MetalCompareFunction::kGreater;
        case RHICompareFunc::NotEqual:     return MetalCompareFunction::kNotEqual;
        case RHICompareFunc::GreaterEqual: return MetalCompareFunction::kGreaterEqual;
        case RHICompareFunc::Always:       return MetalCompareFunction::kAlways;
    }
    return MetalCompareFunction::kNever;
}

/// Converts an RHIPrimitiveTopology to the corresponding Metal primitive type integer value.
///
/// # Arguments
/// * `topology` — RHI primitive topology enum.
///
/// # Returns
/// The `MTLPrimitiveType` integer value.
[[nodiscard]] constexpr uint32_t ToMTLPrimitiveType(RHIPrimitiveTopology topology) {
    switch (topology) {
        case RHIPrimitiveTopology::TriangleList:  return MetalPrimitiveType::kTriangle;
        case RHIPrimitiveTopology::TriangleStrip: return MetalPrimitiveType::kTriangleStrip;
        case RHIPrimitiveTopology::LineList:       return MetalPrimitiveType::kLine;
        case RHIPrimitiveTopology::LineStrip:      return MetalPrimitiveType::kLineStrip;
        case RHIPrimitiveTopology::PointList:      return MetalPrimitiveType::kPoint;
    }
    return MetalPrimitiveType::kTriangle;
}

} // namespace uz
