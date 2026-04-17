/// Render Graph Type Definitions — Virtual resource handles, descriptors, and pass resource access.
///
/// These types form the public vocabulary of the render graph. Resources are referenced
/// by `RgResourceId` (a virtual handle) during graph construction. Physical RHI handles
/// are only resolved at execution time and provided to pass callbacks via `RgPassResources`.
///
/// # Ownership
/// All render graph types are value types with trivial copy semantics.
/// The `RgBuilder` owns the storage backing descriptors and resource metadata.

#pragma once

#include <cstdint>
#include <functional>

#include "../../rhi/rhi_types.h"

namespace uz::rg {

// ─────────────────────────────────────────────────────────────────────────────
// RgResourceId — virtual handle for graph resources
// ─────────────────────────────────────────────────────────────────────────────

/// Identifies a virtual resource (texture or buffer) within the render graph.
///
/// This is NOT an RHI handle — it is a graph-local index assigned during `CreateTexture()`
/// or `CreateBuffer()`. The render graph resolves `RgResourceId` to a physical
/// `RHITextureHandle` or `RHIBufferHandle` at execution time.
///
/// # Validity
/// A valid resource ID has `index != UINT32_MAX`. Use `IsValid()` to check.
/// Invalid IDs are returned on construction failure or used as sentinels.
struct RgResourceId {
    uint32_t index = UINT32_MAX; ///< Index into the builder's resource array. UINT32_MAX = invalid.

    /// Returns true if this ID references a valid resource in the graph.
    [[nodiscard]] bool IsValid() const { return index != UINT32_MAX; }

    /// Returns an invalid sentinel resource ID.
    [[nodiscard]] static RgResourceId Invalid() { return {}; }

    bool operator==(RgResourceId other) const { return index == other.index; }
    bool operator!=(RgResourceId other) const { return index != other.index; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Resource kind — texture vs buffer discrimination
// ─────────────────────────────────────────────────────────────────────────────

/// Discriminates between texture and buffer resources in the graph.
enum class RgResourceKind : uint8_t {
    Texture, ///< Resource is a texture (2D, cube, array).
    Buffer,  ///< Resource is a buffer (vertex, index, constant, structured, UAV).
};

// ─────────────────────────────────────────────────────────────────────────────
// RgTextureDesc / RgBufferDesc — virtual resource descriptors
// ─────────────────────────────────────────────────────────────────────────────

/// Describes a virtual texture to be created by the render graph.
///
/// Similar to `RHITextureDesc` but without `debugName` — the graph auto-generates
/// debug names from the pass name and resource name.
///
/// # Defaults
/// - 1x1 single-mip RGBA8Unorm with ShaderResource usage.
struct RgTextureDesc {
    uint32_t width = 1;                                        ///< Width in texels.
    uint32_t height = 1;                                       ///< Height in texels.
    uint16_t depthOrArraySize = 1;                             ///< Depth (3D) or array layers.
    uint16_t mipLevels = 1;                                    ///< Number of mip levels.
    RHIFormat format = RHIFormat::RGBA8Unorm;                  ///< Texel format.
    RHITextureUsage usage = RHITextureUsage::ShaderResource;   ///< Combined usage flags.

    /// When true, the texture is a volumetric 3D texture and
    /// `depthOrArraySize` is interpreted as depth. When false, the
    /// texture is a 2D texture (array when `depthOrArraySize > 1`).
    /// Added in phase 10.6.0 to enable transient 3D textures for the
    /// volumetric renderer pipeline (phase 9.10).
    /// @ref RHITextureDesc::is3D
    bool is3D = false;
};

/// Describes a virtual buffer to be created by the render graph.
///
/// Similar to `RHIBufferDesc` but without `debugName` or `memory` — the graph
/// manages memory allocation and naming internally.
///
/// # Defaults
/// - 0-byte buffer with no usage (must be configured before use).
struct RgBufferDesc {
    uint64_t sizeBytes = 0;                          ///< Total size in bytes.
    uint32_t stride = 0;                             ///< Element stride (for structured buffers).
    RHIBufferUsage usage = RHIBufferUsage::None;     ///< Combined usage flags.
};

// ─────────────────────────────────────────────────────────────────────────────
// RgResourceEntry — internal metadata for a graph resource
// ─────────────────────────────────────────────────────────────────────────────

/// Internal bookkeeping for a single resource in the render graph.
///
/// Tracks the virtual descriptor, resolved physical handle, lifetime span,
/// and whether the resource was imported (externally owned) or transient (graph-owned).
struct RgResourceEntry {
    const char* name = nullptr;                      ///< Debug name (string literal or arena-allocated).
    RgResourceKind kind = RgResourceKind::Texture;   ///< Texture or buffer.

    RgTextureDesc textureDesc{};                     ///< Valid when `kind == Texture`.
    RgBufferDesc bufferDesc{};                       ///< Valid when `kind == Buffer`.

    RHITextureHandle physicalTexture;                ///< Resolved RHI handle (valid after compile/execute).
    RHIBufferHandle physicalBuffer;                  ///< Resolved RHI handle (valid after compile/execute).

    RHIResourceState initialState = RHIResourceState::Undefined; ///< State at graph entry (imported) or creation.
    RHIResourceState currentState = RHIResourceState::Undefined; ///< Tracked state during barrier scheduling.

    bool imported = false;  ///< True if the resource was imported (not owned by the graph).
    bool culled = false;    ///< True if this resource is unreferenced and will be skipped.

    uint32_t firstPassIndex = UINT32_MAX; ///< Index of the first pass that touches this resource.
    uint32_t lastPassIndex = 0;           ///< Index of the last pass that touches this resource.
    uint32_t refCount = 0;                ///< Number of passes that read this resource.
};

// ─────────────────────────────────────────────────────────────────────────────
// RgPassResources — resolved handles provided to pass execution callbacks
// ─────────────────────────────────────────────────────────────────────────────

class RgBuilder;

/// Provides resolved physical RHI handles to a pass execution callback.
///
/// Pass callbacks receive a `const RgPassResources&` and use `GetTexture()` / `GetBuffer()`
/// to obtain the physical handles for their declared resources. Accessing a resource
/// that was not declared as a read or write by the pass is undefined behavior.
///
/// # Lifetime
/// Valid only for the duration of the pass execution callback. Do not store references.
class RgPassResources {
public:
    /// Constructs pass resources backed by the given builder's resource array.
    ///
    /// # Arguments
    /// * `builder` — The owning builder (provides resource entry lookup).
    explicit RgPassResources(const RgBuilder& builder) : m_builder(builder) {}

    /// Returns the physical RHI texture handle for a virtual resource.
    ///
    /// # Arguments
    /// * `id` — A resource previously declared as a read or write by this pass.
    ///
    /// # Returns
    /// A valid `RHITextureHandle`. Asserts if `id` is invalid or not a texture.
    [[nodiscard]] RHITextureHandle GetTexture(RgResourceId id) const;

    /// Returns the physical RHI buffer handle for a virtual resource.
    ///
    /// # Arguments
    /// * `id` — A resource previously declared as a read or write by this pass.
    ///
    /// # Returns
    /// A valid `RHIBufferHandle`. Asserts if `id` is invalid or not a buffer.
    [[nodiscard]] RHIBufferHandle GetBuffer(RgResourceId id) const;

private:
    const RgBuilder& m_builder;
};

// ─────────────────────────────────────────────────────────────────────────────
// RgPassExecuteFn — pass execution callback signature
// ─────────────────────────────────────────────────────────────────────────────

} // namespace uz::rg

namespace uz { class IRHICommandList; }

namespace uz::rg {

/// Callback invoked when a render graph pass is executed.
///
/// # Arguments
/// * `IRHICommandList*` — The command list to record GPU commands into.
/// * `const RgPassResources&` — Resolved physical handles for declared resources.
using RgPassExecuteFn = std::function<void(uz::IRHICommandList*, const RgPassResources&)>;

// ─────────────────────────────────────────────────────────────────────────────
// RgResourceAccess — per-pass resource usage record
// ─────────────────────────────────────────────────────────────────────────────

/// Records how a single pass accesses a single resource.
///
/// Used internally by the compiler to determine barrier transitions.
struct RgResourceAccess {
    RgResourceId resourceId;                                ///< Which resource is accessed.
    RHIResourceState state = RHIResourceState::Undefined;   ///< The state the pass needs the resource in.
    bool isWrite = false;                                   ///< True if the pass writes to the resource.
};

// ─────────────────────────────────────────────────────────────────────────────
// RgBarrier — scheduled barrier emitted by the compiler
// ─────────────────────────────────────────────────────────────────────────────

/// A barrier scheduled by the render graph compiler to be emitted before a pass.
///
/// Translates to a single `RHIBarrierDesc` at execution time once physical handles
/// are resolved.
struct RgBarrier {
    RgResourceId resourceId;                                ///< Resource to transition.
    RHIResourceState stateBefore = RHIResourceState::Undefined; ///< State before the barrier.
    RHIResourceState stateAfter = RHIResourceState::Undefined;  ///< State after the barrier.
};

} // namespace uz::rg
