#pragma once

// render_graph.h -- Frame-local DAG for GPU resource management and pass
// scheduling.
//
// Declares resources (RgTexture, RgBuffer), passes (RgPass), and the compiler
// that performs topological sort, dead pass culling, and resource lifetime
// tracking.
//
// Backend-agnostic: uses RHI handles (RhiTexture, RhiBuffer) from rhi.h.
// On platforms without a GPU backend, execute() calls pass callbacks with
// RHI_NULL as the command list.

extern "C" {
#include "engine/renderer/rhi/rhi.h"
}

#include <cstdint>
#include <functional>
#include <string>

namespace uz {

// --- Resource handles (typed indices) ----------------------------------------

using RgTextureHandle = uint32_t;
using RgBufferHandle  = uint32_t;
inline constexpr uint32_t kRgHandleInvalid = UINT32_MAX;

// --- Limits ------------------------------------------------------------------

inline constexpr uint32_t kRgMaxPasses     = 64;
inline constexpr uint32_t kRgMaxResources  = 128;

// --- Resource descriptors ----------------------------------------------------

enum class RgFormat : uint8_t {
    Unknown = 0,
    RGBA8   = 1,
    RGBA16F = 2,
    R32F    = 3,
    D32F    = 4,
    RG16F   = 5,
    BC7     = 6,
};

struct RgTextureDesc {
    uint32_t width            = 1;
    uint32_t height           = 1;
    uint32_t mip_levels       = 1;
    RgFormat format           = RgFormat::RGBA8;
    bool     is_render_target = true;
    bool     is_depth_stencil = false;
    bool     is_uav           = false;     // Unordered access
    bool     persistent       = false;     // Persistent resources skip aliasing
    char     name[64]         = {};
};

struct RgBufferDesc {
    uint64_t byte_size   = 0;
    uint32_t stride      = 0;    // 0 = raw buffer
    bool     is_uav      = false;
    bool     persistent  = false;
    char     name[64]    = {};
};

// --- Pass types --------------------------------------------------------------

enum class RgPassType : uint8_t {
    Graphics = 0,
    Compute  = 1,
    Copy     = 2,
};

// Pass callback receives a command list handle (RHI_NULL on non-GPU platforms).
using RgPassCallback = std::function<void(RhiCmdList)>;

// --- RgPassBuilder (used inside RenderGraph::add_pass callback) ---------------

class RenderGraph;

struct RgPassBuilder {
    // Declare a read dependency on a texture (SRV / input).
    RgPassBuilder& read(RgTextureHandle h);

    // Declare a write dependency on a texture (RTV / UAV).
    RgPassBuilder& write(RgTextureHandle h);

    // Declare a read dependency on a buffer (CBV/SRV).
    RgPassBuilder& read_buffer(RgBufferHandle h);

    // Declare a write dependency on a buffer (UAV).
    RgPassBuilder& write_buffer(RgBufferHandle h);

    // Mark pass as always-executed (not culled even if output unused).
    RgPassBuilder& force_run();

    // Internal -- index into RenderGraph::passes_
    uint32_t     pass_index = 0;
    RenderGraph* rg         = nullptr;
};

// --- RenderGraph -------------------------------------------------------------

class RenderGraph {
public:
    RenderGraph();
    ~RenderGraph();

    // Non-copyable; movable
    RenderGraph(const RenderGraph&)            = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;
    RenderGraph(RenderGraph&& other) noexcept;
    RenderGraph& operator=(RenderGraph&& other) noexcept;

    // Resource registration ---------------------------------------------------

    // Create a transient texture resource (lifetime managed by the graph).
    [[nodiscard]] RgTextureHandle create_texture(const RgTextureDesc& desc);

    // Create a transient buffer resource.
    [[nodiscard]] RgBufferHandle create_buffer(const RgBufferDesc& desc);

    // Register a persistent texture (e.g. swapchain backbuffer, GBuffer).
    // Persistent resources are not aliased or culled.
    [[nodiscard]] RgTextureHandle import_texture(const char* name,
                                                  RhiTexture handle);

    // Register a persistent buffer (e.g. per-frame constant buffer).
    [[nodiscard]] RgBufferHandle import_buffer(const char* name,
                                                RhiBuffer handle);

    // Pass registration -------------------------------------------------------

    // Add a rendering pass. The setup_fn receives a RgPassBuilder to declare
    // read/write dependencies. Returns a reference to the builder (for chaining).
    RgPassBuilder& add_pass(const char* name, RgPassType type,
                            std::function<void(RgPassBuilder&)> setup_fn,
                            RgPassCallback execute_fn);

    // Compilation -------------------------------------------------------------

    // Compile the graph: topological sort, dead pass culling, resource lifetime
    // assignment. Must be called before execute(). Returns true if valid.
    [[nodiscard]] bool compile();

    // Execute the compiled graph. On non-GPU platforms: calls execute_fn for
    // each live pass with RHI_NULL as the command list (for testing).
    void execute(RhiCmdList cmd_list = RHI_NULL);

    // Reset for next frame (clears all transient resources and passes).
    void reset();

    // Query -------------------------------------------------------------------

    // Returns the number of passes added (before or after compile).
    [[nodiscard]] uint32_t pass_count() const;

    // Returns the number of passes that survived culling (after compile).
    [[nodiscard]] uint32_t live_pass_count() const;

    // Returns the number of textures (transient + imported).
    [[nodiscard]] uint32_t texture_count() const;

    // Returns the number of buffers (transient + imported).
    [[nodiscard]] uint32_t buffer_count() const;

    // Returns the execution order of live passes (indices into pass list).
    // Only valid after compile().
    [[nodiscard]] const uint32_t* execution_order() const;

    // Returns whether a pass was culled (false = alive, true = culled).
    // Index is the order passes were added.
    [[nodiscard]] bool is_pass_culled(uint32_t pass_index) const;

    // Returns the name of a pass by index.
    [[nodiscard]] const char* pass_name(uint32_t pass_index) const;

    // Diagnostics: returns a description of why a pass was culled, or empty.
    [[nodiscard]] std::string cull_reason(uint32_t pass_index) const;

    // §4.5: Returns the name of the currently executing pass (set during
    // execute()). Returns "" outside of execute(). Thread-safe read.
    [[nodiscard]] const char* active_pass_name() const;

    // §6: Returns the texture descriptor for a given handle.
    [[nodiscard]] const RgTextureDesc* texture_desc(RgTextureHandle h) const;

    friend struct RgPassBuilder;

private:
    struct Impl;
    Impl* impl_;
};

} // namespace uz
