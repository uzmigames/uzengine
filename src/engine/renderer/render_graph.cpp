// render_graph.cpp -- RenderGraph implementation.
//
// Frame-local DAG: resources are declared, passes register read/write deps,
// compile() performs Kahn's toposort + backward dead-pass culling, and
// execute() runs surviving passes in topological order.
//
// Key design decisions:
//   - std::vector for all dynamic storage (frame-level, not hot path)
//   - Max 64 passes, 128 resources (assert-enforced)
//   - Culling walks backward from "output frontier" (force_run passes and
//     passes that write to imported/persistent resources)
//   - Resource lifetimes: [first_use_pass, last_use_pass] computed post-cull

#include "engine/renderer/render_graph.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <queue>
#include <vector>

namespace uz {

// ---------------------------------------------------------------------------
// Internal resource / pass structures
// ---------------------------------------------------------------------------

namespace {

enum class RgResourceKind : uint8_t {
    Texture = 0,
    Buffer  = 1,
};

struct RgResource {
    RgResourceKind kind       = RgResourceKind::Texture;
    bool           imported   = false;
    bool           persistent = false;
    char           name[64]   = {};

    // For imported resources: the RHI handle
    union {
        RhiTexture rhi_texture;
        RhiBuffer  rhi_buffer;
    };

    // Lifetime tracking (pass indices, set during compile)
    uint32_t first_use = UINT32_MAX;
    uint32_t last_use  = 0;

    // Descriptor storage (only one is active based on kind)
    RgTextureDesc tex_desc = {};
    RgBufferDesc  buf_desc = {};
};

struct RgPass {
    char           name[64]    = {};
    RgPassType     type        = RgPassType::Graphics;
    bool           force_run   = false;
    bool           culled      = true;   // Starts culled; compile marks live
    std::string    cull_reason;

    RgPassCallback execute_fn;

    // Dependencies: resource handles this pass reads/writes
    std::vector<uint32_t> texture_reads;
    std::vector<uint32_t> texture_writes;
    std::vector<uint32_t> buffer_reads;
    std::vector<uint32_t> buffer_writes;
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// PIMPL
// ---------------------------------------------------------------------------

struct RenderGraph::Impl {
    std::vector<RgResource>   resources;
    std::vector<RgPass>       passes;
    std::vector<RgPassBuilder> builders;  // One per pass, kept alive for chaining

    // Compile output
    bool                  compiled = false;
    std::vector<uint32_t> execution_order;

    // Separate counters for texture/buffer handles so they don't alias
    // Textures use handle range [0, tex_count), buffers use [tex_offset, ...)
    // Actually both live in resources[] -- we just tag them by kind.
    uint32_t texture_count = 0;
    uint32_t buffer_count  = 0;
};

// ---------------------------------------------------------------------------
// RgPassBuilder
// ---------------------------------------------------------------------------

RgPassBuilder& RgPassBuilder::read(RgTextureHandle h) {
    assert(rg && rg->impl_);
    assert(h < static_cast<uint32_t>(rg->impl_->resources.size()));
    auto& pass = rg->impl_->passes[pass_index];
    pass.texture_reads.push_back(h);
    return *this;
}

RgPassBuilder& RgPassBuilder::write(RgTextureHandle h) {
    assert(rg && rg->impl_);
    assert(h < static_cast<uint32_t>(rg->impl_->resources.size()));
    auto& pass = rg->impl_->passes[pass_index];
    pass.texture_writes.push_back(h);
    return *this;
}

RgPassBuilder& RgPassBuilder::read_buffer(RgBufferHandle h) {
    assert(rg && rg->impl_);
    assert(h < static_cast<uint32_t>(rg->impl_->resources.size()));
    auto& pass = rg->impl_->passes[pass_index];
    pass.buffer_reads.push_back(h);
    return *this;
}

RgPassBuilder& RgPassBuilder::write_buffer(RgBufferHandle h) {
    assert(rg && rg->impl_);
    assert(h < static_cast<uint32_t>(rg->impl_->resources.size()));
    auto& pass = rg->impl_->passes[pass_index];
    pass.buffer_writes.push_back(h);
    return *this;
}

RgPassBuilder& RgPassBuilder::force_run() {
    assert(rg && rg->impl_);
    rg->impl_->passes[pass_index].force_run = true;
    return *this;
}

// ---------------------------------------------------------------------------
// Construction / Destruction / Move
// ---------------------------------------------------------------------------

RenderGraph::RenderGraph()
    : impl_(new Impl)
{
}

RenderGraph::~RenderGraph() {
    delete impl_;
}

RenderGraph::RenderGraph(RenderGraph&& other) noexcept
    : impl_(other.impl_)
{
    other.impl_ = nullptr;
}

RenderGraph& RenderGraph::operator=(RenderGraph&& other) noexcept {
    if (this != &other) {
        delete impl_;
        impl_ = other.impl_;
        other.impl_ = nullptr;
    }
    return *this;
}

// ---------------------------------------------------------------------------
// Resource registration
// ---------------------------------------------------------------------------

RgTextureHandle RenderGraph::create_texture(const RgTextureDesc& desc) {
    assert(impl_->resources.size() < kRgMaxResources);

    const auto handle = static_cast<uint32_t>(impl_->resources.size());
    RgResource res;
    res.kind       = RgResourceKind::Texture;
    res.imported   = false;
    res.persistent = desc.persistent;
    res.tex_desc   = desc;
    res.rhi_texture = RHI_NULL;
    std::strncpy(res.name, desc.name, sizeof(res.name) - 1);
    res.name[sizeof(res.name) - 1] = '\0';

    impl_->resources.push_back(res);
    impl_->texture_count++;
    return handle;
}

RgBufferHandle RenderGraph::create_buffer(const RgBufferDesc& desc) {
    assert(impl_->resources.size() < kRgMaxResources);

    const auto handle = static_cast<uint32_t>(impl_->resources.size());
    RgResource res;
    res.kind       = RgResourceKind::Buffer;
    res.imported   = false;
    res.persistent = desc.persistent;
    res.buf_desc   = desc;
    res.rhi_buffer = RHI_NULL;
    std::strncpy(res.name, desc.name, sizeof(res.name) - 1);
    res.name[sizeof(res.name) - 1] = '\0';

    impl_->resources.push_back(res);
    impl_->buffer_count++;
    return handle;
}

RgTextureHandle RenderGraph::import_texture(const char* name,
                                             RhiTexture handle) {
    assert(impl_->resources.size() < kRgMaxResources);

    const auto idx = static_cast<uint32_t>(impl_->resources.size());
    RgResource res;
    res.kind        = RgResourceKind::Texture;
    res.imported    = true;
    res.persistent  = true;
    res.rhi_texture = handle;
    if (name) {
        std::strncpy(res.name, name, sizeof(res.name) - 1);
        res.name[sizeof(res.name) - 1] = '\0';
    }

    impl_->resources.push_back(res);
    impl_->texture_count++;
    return idx;
}

RgBufferHandle RenderGraph::import_buffer(const char* name, RhiBuffer handle) {
    assert(impl_->resources.size() < kRgMaxResources);

    const auto idx = static_cast<uint32_t>(impl_->resources.size());
    RgResource res;
    res.kind       = RgResourceKind::Buffer;
    res.imported   = true;
    res.persistent = true;
    res.rhi_buffer = handle;
    if (name) {
        std::strncpy(res.name, name, sizeof(res.name) - 1);
        res.name[sizeof(res.name) - 1] = '\0';
    }

    impl_->resources.push_back(res);
    impl_->buffer_count++;
    return idx;
}

// ---------------------------------------------------------------------------
// Pass registration
// ---------------------------------------------------------------------------

RgPassBuilder& RenderGraph::add_pass(
    const char* name,
    RgPassType type,
    std::function<void(RgPassBuilder&)> setup_fn,
    RgPassCallback execute_fn)
{
    assert(impl_->passes.size() < kRgMaxPasses);

    const auto idx = static_cast<uint32_t>(impl_->passes.size());

    RgPass pass;
    if (name) {
        std::strncpy(pass.name, name, sizeof(pass.name) - 1);
        pass.name[sizeof(pass.name) - 1] = '\0';
    }
    pass.type       = type;
    pass.execute_fn = std::move(execute_fn);
    impl_->passes.push_back(std::move(pass));

    // Create a builder for this pass
    RgPassBuilder builder;
    builder.pass_index = idx;
    builder.rg         = this;
    impl_->builders.push_back(builder);

    // Call setup function so the caller can declare dependencies
    if (setup_fn) {
        setup_fn(impl_->builders.back());
    }

    return impl_->builders.back();
}

// ---------------------------------------------------------------------------
// Compilation
// ---------------------------------------------------------------------------

bool RenderGraph::compile() {
    auto& passes    = impl_->passes;
    auto& resources = impl_->resources;
    const auto num_passes = static_cast<uint32_t>(passes.size());

    impl_->compiled = true;
    impl_->execution_order.clear();

    if (num_passes == 0) {
        return true;
    }

    // Step 1: Reset all passes to culled
    for (auto& p : passes) {
        p.culled      = true;
        p.cull_reason = "output not consumed";
    }

    // Step 2: Build "resource -> writer passes" and "resource -> reader passes"
    // maps so we can walk the dependency graph backward.
    //
    // writer_of[resource] = list of pass indices that write this resource
    // readers_of[resource] = list of pass indices that read this resource

    const auto num_resources = static_cast<uint32_t>(resources.size());
    std::vector<std::vector<uint32_t>> writers_of(num_resources);
    std::vector<std::vector<uint32_t>> readers_of(num_resources);

    for (uint32_t pi = 0; pi < num_passes; ++pi) {
        const auto& p = passes[pi];
        for (auto r : p.texture_writes) {
            writers_of[r].push_back(pi);
        }
        for (auto r : p.buffer_writes) {
            writers_of[r].push_back(pi);
        }
        for (auto r : p.texture_reads) {
            readers_of[r].push_back(pi);
        }
        for (auto r : p.buffer_reads) {
            readers_of[r].push_back(pi);
        }
    }

    // Step 3: Build forward adjacency (pass -> dependent passes).
    // If pass A writes resource R and pass B reads resource R, then A -> B.
    std::vector<std::vector<uint32_t>> forward_adj(num_passes);
    std::vector<uint32_t> in_degree(num_passes, 0);

    for (uint32_t ri = 0; ri < num_resources; ++ri) {
        for (uint32_t writer : writers_of[ri]) {
            for (uint32_t reader : readers_of[ri]) {
                // Avoid self-edges and duplicates
                if (writer != reader) {
                    forward_adj[writer].push_back(reader);
                }
            }
        }
    }

    // Deduplicate forward adjacency and compute in-degrees
    for (uint32_t pi = 0; pi < num_passes; ++pi) {
        auto& adj = forward_adj[pi];
        std::sort(adj.begin(), adj.end());
        adj.erase(std::unique(adj.begin(), adj.end()), adj.end());
        for (uint32_t dep : adj) {
            in_degree[dep]++;
        }
    }

    // Step 4: Topological sort (Kahn's algorithm)
    std::vector<uint32_t> topo_order;
    topo_order.reserve(num_passes);

    std::queue<uint32_t> queue;
    for (uint32_t pi = 0; pi < num_passes; ++pi) {
        if (in_degree[pi] == 0) {
            queue.push(pi);
        }
    }

    while (!queue.empty()) {
        uint32_t pi = queue.front();
        queue.pop();
        topo_order.push_back(pi);

        for (uint32_t dep : forward_adj[pi]) {
            in_degree[dep]--;
            if (in_degree[dep] == 0) {
                queue.push(dep);
            }
        }
    }

    // Cycle detection: if topo_order doesn't contain all passes, there's a cycle
    if (topo_order.size() != num_passes) {
        // Mark remaining passes as culled with cycle reason
        for (uint32_t pi = 0; pi < num_passes; ++pi) {
            if (std::find(topo_order.begin(), topo_order.end(), pi) ==
                topo_order.end()) {
                passes[pi].cull_reason = "cycle in dependency graph";
            }
        }
        return false;
    }

    // Step 5: Backward walk to determine live passes.
    // The "output frontier" consists of:
    //   a) force_run passes
    //   b) passes that write to imported/persistent resources
    //
    // We walk backward through dependencies: if a pass is live and reads
    // resource R, then any pass that writes R is also live.

    // Build reverse adjacency (pass <- passes it depends on)
    // If pass B reads resource R and pass A writes R, then reverse_adj[B]
    // contains A (B depends on A).
    std::vector<std::vector<uint32_t>> reverse_adj(num_passes);
    for (uint32_t pi = 0; pi < num_passes; ++pi) {
        for (uint32_t dep : forward_adj[pi]) {
            reverse_adj[dep].push_back(pi);
        }
    }

    // Seed the live set with frontier passes
    std::vector<bool> live(num_passes, false);
    std::queue<uint32_t> live_queue;

    for (uint32_t pi = 0; pi < num_passes; ++pi) {
        bool is_frontier = false;

        if (passes[pi].force_run) {
            is_frontier = true;
        }

        // Check if pass writes to an imported/persistent resource
        if (!is_frontier) {
            for (auto r : passes[pi].texture_writes) {
                if (resources[r].imported || resources[r].persistent) {
                    is_frontier = true;
                    break;
                }
            }
        }
        if (!is_frontier) {
            for (auto r : passes[pi].buffer_writes) {
                if (resources[r].imported || resources[r].persistent) {
                    is_frontier = true;
                    break;
                }
            }
        }

        if (is_frontier) {
            live[pi] = true;
            live_queue.push(pi);
        }
    }

    // Propagate liveness backward: if pass B is live and depends on pass A,
    // then A is also live.
    while (!live_queue.empty()) {
        uint32_t pi = live_queue.front();
        live_queue.pop();

        // This pass is live. Any pass that produces a resource we read is
        // also live.
        for (auto dep : reverse_adj[pi]) {
            if (!live[dep]) {
                live[dep] = true;
                live_queue.push(dep);
            }
        }
    }

    // Step 6: Mark live/culled and build execution order
    for (uint32_t pi = 0; pi < num_passes; ++pi) {
        passes[pi].culled = !live[pi];
        if (live[pi]) {
            passes[pi].cull_reason.clear();
        }
    }

    // Execution order = topological order, but only live passes
    impl_->execution_order.clear();
    impl_->execution_order.reserve(num_passes);
    for (uint32_t pi : topo_order) {
        if (live[pi]) {
            impl_->execution_order.push_back(pi);
        }
    }

    // Step 7: Compute resource lifetimes based on execution order
    for (auto& r : resources) {
        r.first_use = UINT32_MAX;
        r.last_use  = 0;
    }

    for (uint32_t order_idx = 0;
         order_idx < static_cast<uint32_t>(impl_->execution_order.size());
         ++order_idx)
    {
        const uint32_t pi = impl_->execution_order[order_idx];
        const auto& p = passes[pi];

        auto touch = [&](uint32_t ri) {
            auto& r = resources[ri];
            if (order_idx < r.first_use) r.first_use = order_idx;
            if (order_idx > r.last_use)  r.last_use  = order_idx;
        };

        for (auto r : p.texture_reads)  touch(r);
        for (auto r : p.texture_writes) touch(r);
        for (auto r : p.buffer_reads)   touch(r);
        for (auto r : p.buffer_writes)  touch(r);
    }

    return true;
}

// ---------------------------------------------------------------------------
// Execution
// ---------------------------------------------------------------------------

void RenderGraph::execute(RhiCmdList cmd_list) {
    if (!impl_->compiled) {
        return;
    }

    for (uint32_t pi : impl_->execution_order) {
        const auto& pass = impl_->passes[pi];
        if (pass.execute_fn) {
            pass.execute_fn(cmd_list);
        }
    }
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

void RenderGraph::reset() {
    impl_->resources.clear();
    impl_->passes.clear();
    impl_->builders.clear();
    impl_->execution_order.clear();
    impl_->compiled      = false;
    impl_->texture_count = 0;
    impl_->buffer_count  = 0;
}

// ---------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------

uint32_t RenderGraph::pass_count() const {
    return static_cast<uint32_t>(impl_->passes.size());
}

uint32_t RenderGraph::live_pass_count() const {
    return static_cast<uint32_t>(impl_->execution_order.size());
}

uint32_t RenderGraph::texture_count() const {
    return impl_->texture_count;
}

uint32_t RenderGraph::buffer_count() const {
    return impl_->buffer_count;
}

const uint32_t* RenderGraph::execution_order() const {
    if (impl_->execution_order.empty()) {
        return nullptr;
    }
    return impl_->execution_order.data();
}

bool RenderGraph::is_pass_culled(uint32_t pass_index) const {
    if (pass_index >= static_cast<uint32_t>(impl_->passes.size())) {
        return true;
    }
    return impl_->passes[pass_index].culled;
}

const char* RenderGraph::pass_name(uint32_t pass_index) const {
    if (pass_index >= static_cast<uint32_t>(impl_->passes.size())) {
        return "";
    }
    return impl_->passes[pass_index].name;
}

std::string RenderGraph::cull_reason(uint32_t pass_index) const {
    if (pass_index >= static_cast<uint32_t>(impl_->passes.size())) {
        return "invalid pass index";
    }
    return impl_->passes[pass_index].cull_reason;
}

} // namespace uz
