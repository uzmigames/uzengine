/// Render Graph Builder — Compilation and Execution Implementation
///
/// Implements the frame-level DAG builder that registers render passes, declares
/// resources, compiles dependencies via topological sort, performs dead-code pass
/// culling, and schedules resource barriers. Manages transient resource allocation
/// via pool acquire/release and executes the sorted pass list with barriers.

#include "render_graph_builder.h"
#include "render_graph_resources.h"
#include "render_graph_states.h"

#include "../../rhi/rhi.h"
#include "log.h"
#include "engine_assert.h"

#include <algorithm>
#include <queue>
#include <unordered_set>

namespace uz::rg {

// ─────────────────────────────────────────────────────────────────────────────
// RgPassBuilder — Read / Write / SetExecutionCallback
// ─────────────────────────────────────────────────────────────────────────────

RgResourceId RgPassBuilder::Read(RgResourceId id, RHIResourceState state)
{
    UZ_CHECK(id.IsValid());
    m_pass.accesses.push_back({ id, state, false });
    return id;
}

RgResourceId RgPassBuilder::Write(RgResourceId id, RHIResourceState state)
{
    UZ_CHECK(id.IsValid());
    m_pass.accesses.push_back({ id, state, true });
    return id;
}

void RgPassBuilder::SetExecutionCallback(RgPassExecuteFn fn)
{
    UZ_CHECK(fn != nullptr);
    m_pass.executeFn = std::move(fn);
}

// ─────────────────────────────────────────────────────────────────────────────
// RgPassResources — GetTexture / GetBuffer
// ─────────────────────────────────────────────────────────────────────────────

RHITextureHandle RgPassResources::GetTexture(RgResourceId id) const
{
    const auto& entry = m_builder.GetResource(id);
    UZ_CHECK(entry.kind == RgResourceKind::Texture);
    UZ_CHECK(entry.physicalTexture.IsValid());
    return entry.physicalTexture;
}

RHIBufferHandle RgPassResources::GetBuffer(RgResourceId id) const
{
    const auto& entry = m_builder.GetResource(id);
    UZ_CHECK(entry.kind == RgResourceKind::Buffer);
    UZ_CHECK(entry.physicalBuffer.IsValid());
    return entry.physicalBuffer;
}

// ─────────────────────────────────────────────────────────────────────────────
// RgBuilder — Construction
// ─────────────────────────────────────────────────────────────────────────────

RgBuilder::RgBuilder(IRHIDevice* device, RgResourcePool* pool, RgStateRegistry* states)
    : m_device(device), m_pool(pool), m_states(states)
{
    UZ_CHECK(device != nullptr);
    UZ_CHECK(pool != nullptr);
}

RgBuilder::~RgBuilder()
{
    // If executed, resources were already released. If not, nothing to clean up
    // (transient resources were never acquired).
}

// ─────────────────────────────────────────────────────────────────────────────
// Resource Declaration
// ─────────────────────────────────────────────────────────────────────────────

RgResourceId RgBuilder::CreateTexture(const char* name, const RgTextureDesc& desc)
{
    UZ_CHECK(!m_compiled);

    RgResourceEntry entry{};
    entry.name = name;
    entry.kind = RgResourceKind::Texture;
    entry.textureDesc = desc;
    entry.imported = false;
    entry.initialState = RHIResourceState::Undefined;
    entry.currentState = RHIResourceState::Undefined;

    auto id = RgResourceId{ static_cast<uint32_t>(m_resources.size()) };
    m_resources.push_back(entry);

    UZ_LOG(Renderer, Trace, "RenderGraph: created transient texture '{}' ({}x{}, id={})",
           name ? name : "?", desc.width, desc.height, id.index);
    return id;
}

RgResourceId RgBuilder::CreateTransient3D(const char* name,
                                            uint32_t width, uint32_t height, uint32_t depth,
                                            RHIFormat format, RHITextureUsage usage)
{
    UZ_CHECK(!m_compiled);
    UZ_CHECK(width > 0u && height > 0u && depth > 0u);

    RgTextureDesc desc{};
    desc.width            = width;
    desc.height           = height;
    desc.depthOrArraySize = static_cast<uint16_t>(depth);
    desc.mipLevels        = 1u;
    desc.format           = format;
    desc.usage            = usage;
    desc.is3D             = true;
    return CreateTexture(name, desc);
}

RgResourceId RgBuilder::CreateBuffer(const char* name, const RgBufferDesc& desc)
{
    UZ_CHECK(!m_compiled);

    RgResourceEntry entry{};
    entry.name = name;
    entry.kind = RgResourceKind::Buffer;
    entry.bufferDesc = desc;
    entry.imported = false;
    entry.initialState = RHIResourceState::Undefined;
    entry.currentState = RHIResourceState::Undefined;

    auto id = RgResourceId{ static_cast<uint32_t>(m_resources.size()) };
    m_resources.push_back(entry);

    UZ_LOG(Renderer, Trace, "RenderGraph: created transient buffer '{}' ({} bytes, id={})",
           name ? name : "?", desc.sizeBytes, id.index);
    return id;
}

RgResourceId RgBuilder::ImportTexture(const char* name, RHITextureHandle handle,
                                       RHIResourceState currentState)
{
    UZ_CHECK(!m_compiled);
    UZ_CHECK(handle.IsValid());

    // Registry: a handle imported before in this graph. Onto a tracked import, this one joins its barrier
    // chain; two explicit claims keep two chains (the behaviour before the registry) and are reported.
    const auto known = m_states ? m_importedTextureIds.find(RgStateRegistry::Key(handle))
                                : m_importedTextureIds.end();
    if (known != m_importedTextureIds.end()) {
        const RgResourceEntry& first = m_resources[known->second.index];
        if (first.stateTracked) {
            return known->second;
        }
        m_states->ReportDoubleImport(first.name, name);
    }

    RgResourceEntry entry{};
    entry.name = name;
    entry.kind = RgResourceKind::Texture;
    entry.physicalTexture = handle;
    entry.imported = true;
    entry.initialState = currentState;
    entry.currentState = currentState;

    // Populate texture desc from the physical handle for debugging.
    entry.textureDesc.width = 0;
    entry.textureDesc.height = 0;

    auto id = RgResourceId{ static_cast<uint32_t>(m_resources.size()) };
    m_resources.push_back(entry);
    if (m_states) {
        m_importedTextureIds.emplace(RgStateRegistry::Key(handle), id);
    }

    return id;
}

RgResourceId RgBuilder::ImportBuffer(const char* name, RHIBufferHandle handle,
                                      RHIResourceState currentState)
{
    UZ_CHECK(!m_compiled);
    UZ_CHECK(handle.IsValid());

    const auto known = m_states ? m_importedBufferIds.find(RgStateRegistry::Key(handle))
                                : m_importedBufferIds.end();
    if (known != m_importedBufferIds.end()) {
        const RgResourceEntry& first = m_resources[known->second.index];
        if (first.stateTracked) {
            return known->second;
        }
        m_states->ReportDoubleImport(first.name, name);
    }

    RgResourceEntry entry{};
    entry.name = name;
    entry.kind = RgResourceKind::Buffer;
    entry.physicalBuffer = handle;
    entry.imported = true;
    entry.initialState = currentState;
    entry.currentState = currentState;

    auto id = RgResourceId{ static_cast<uint32_t>(m_resources.size()) };
    m_resources.push_back(entry);
    if (m_states) {
        m_importedBufferIds.emplace(RgStateRegistry::Key(handle), id);
    }

    return id;
}

RgResourceId RgBuilder::ImportTrackedTexture(const char* name, RHITextureHandle handle)
{
    UZ_CHECK(!m_compiled);
    UZ_CHECK(handle.IsValid());
    UZ_CHECKF(m_states != nullptr, "ImportTrackedTexture(%s) needs an RgStateRegistry", name ? name : "?");

    const auto known = m_importedTextureIds.find(RgStateRegistry::Key(handle));
    if (known != m_importedTextureIds.end()) {
        return known->second;   // one id, one barrier chain per resource
    }

    RgResourceEntry entry{};
    entry.name = name;
    entry.kind = RgResourceKind::Texture;
    entry.physicalTexture = handle;
    entry.imported = true;
    entry.stateTracked = true;   // initialState resolved at Compile
    entry.initialState = RHIResourceState::Undefined;
    entry.currentState = RHIResourceState::Undefined;
    entry.textureDesc.width = 0;
    entry.textureDesc.height = 0;

    auto id = RgResourceId{ static_cast<uint32_t>(m_resources.size()) };
    m_resources.push_back(entry);
    m_importedTextureIds.emplace(RgStateRegistry::Key(handle), id);
    return id;
}

RgResourceId RgBuilder::ImportTrackedBuffer(const char* name, RHIBufferHandle handle)
{
    UZ_CHECK(!m_compiled);
    UZ_CHECK(handle.IsValid());
    UZ_CHECKF(m_states != nullptr, "ImportTrackedBuffer(%s) needs an RgStateRegistry", name ? name : "?");

    const auto known = m_importedBufferIds.find(RgStateRegistry::Key(handle));
    if (known != m_importedBufferIds.end()) {
        return known->second;
    }

    RgResourceEntry entry{};
    entry.name = name;
    entry.kind = RgResourceKind::Buffer;
    entry.physicalBuffer = handle;
    entry.imported = true;
    entry.stateTracked = true;
    entry.initialState = RHIResourceState::Undefined;
    entry.currentState = RHIResourceState::Undefined;

    auto id = RgResourceId{ static_cast<uint32_t>(m_resources.size()) };
    m_resources.push_back(entry);
    m_importedBufferIds.emplace(RgStateRegistry::Key(handle), id);
    return id;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pass Registration
// ─────────────────────────────────────────────────────────────────────────────

void RgBuilder::AddPass(const char* name, const std::function<void(RgPassBuilder&)>& setupFn)
{
    UZ_CHECK(!m_compiled);
    UZ_CHECK(name != nullptr);
    UZ_CHECK(setupFn != nullptr);

    RgPass pass{};
    pass.name = name;

    RgPassBuilder pb(pass);
    setupFn(pb);

    UZ_CHECK(pass.executeFn != nullptr);

    m_passes.push_back(std::move(pass));
}

// ─────────────────────────────────────────────────────────────────────────────
// Compilation
// ─────────────────────────────────────────────────────────────────────────────

void RgBuilder::Compile()
{
    UZ_CHECK(!m_compiled);

    ResolveTrackedImportStates();
    TopologicalSort();
    ComputeLifetimes();
    CullPasses();
    ScheduleBarriers();
    ObserveImportClaims();

    m_compiled = true;

}

/// Topological sort via Kahn's algorithm.
///
/// Builds adjacency from resource writes to reads: if pass A writes resource R
/// and pass B reads resource R, then A must execute before B.
void RgBuilder::TopologicalSort()
{
    const uint32_t passCount = static_cast<uint32_t>(m_passes.size());
    if (passCount == 0) return;

    // adjacency[i] = list of pass indices that must come after pass i.
    std::vector<std::vector<uint32_t>> adjacency(passCount);
    std::vector<uint32_t> inDegree(passCount, 0);

    // Helper to add a unique directed edge.
    auto addEdge = [&](uint32_t from, uint32_t to) {
        auto& adj = adjacency[from];
        if (std::find(adj.begin(), adj.end(), to) == adj.end()) {
            adj.push_back(to);
            ++inDegree[to];
        }
    };

    // Build per-resource access chains in declaration order.
    // For each resource, track the last pass that accessed it (read or write).
    // Any subsequent access depends on the previous one to preserve ordering.
    // This handles write→read, read→write, and write→write dependencies.
    std::vector<uint32_t> lastAccess(m_resources.size(), UINT32_MAX);

    for (uint32_t pi = 0; pi < passCount; ++pi) {
        for (const auto& access : m_passes[pi].accesses) {
            if (!access.resourceId.IsValid()) continue;
            uint32_t ri = access.resourceId.index;

            uint32_t prev = lastAccess[ri];
            if (prev != UINT32_MAX && prev != pi) {
                addEdge(prev, pi);
            }
            lastAccess[ri] = pi;
        }
    }

    // Kahn's algorithm.
    std::queue<uint32_t> queue;
    for (uint32_t i = 0; i < passCount; ++i) {
        if (inDegree[i] == 0) {
            queue.push(i);
        }
    }

    m_sortedPassIndices.clear();
    m_sortedPassIndices.reserve(passCount);

    while (!queue.empty()) {
        uint32_t current = queue.front();
        queue.pop();
        m_sortedPassIndices.push_back(current);

        for (uint32_t neighbor : adjacency[current]) {
            --inDegree[neighbor];
            if (inDegree[neighbor] == 0) {
                queue.push(neighbor);
            }
        }
    }

    UZ_CHECK(m_sortedPassIndices.size() == passCount);

    // Assign sort indices to passes.
    for (uint32_t sortIdx = 0; sortIdx < passCount; ++sortIdx) {
        m_passes[m_sortedPassIndices[sortIdx]].sortIndex = sortIdx;
    }
}

/// Computes the lifetime span [firstPass, lastPass] for each resource.
void RgBuilder::ComputeLifetimes()
{
    for (auto& res : m_resources) {
        res.firstPassIndex = UINT32_MAX;
        res.lastPassIndex = 0;
        res.refCount = 0;
    }

    for (uint32_t sortIdx = 0; sortIdx < static_cast<uint32_t>(m_sortedPassIndices.size()); ++sortIdx) {
        const auto& pass = m_passes[m_sortedPassIndices[sortIdx]];

        for (const auto& access : pass.accesses) {
            if (!access.resourceId.IsValid()) continue;

            auto& res = m_resources[access.resourceId.index];
            if (sortIdx < res.firstPassIndex) res.firstPassIndex = sortIdx;
            if (sortIdx > res.lastPassIndex) res.lastPassIndex = sortIdx;
            if (!access.isWrite) ++res.refCount;
        }
    }
}

/// Backward-propagation pass culling.
///
/// A pass is culled if ALL of its written resources are:
/// - Not imported (external), AND
/// - Not read by any non-culled pass.
///
/// Culling propagates backward: if culling a pass removes the last reader
/// of another resource, the producer of that resource may also be culled.
void RgBuilder::CullPasses()
{
    // Mark resources that are "live" (imported or read by at least one pass).
    std::vector<bool> resourceLive(m_resources.size(), false);

    // Imported resources are always live.
    for (uint32_t ri = 0; ri < static_cast<uint32_t>(m_resources.size()); ++ri) {
        if (m_resources[ri].imported) {
            resourceLive[ri] = true;
        }
    }

    // Mark resources read by any pass as live.
    for (const auto& pass : m_passes) {
        for (const auto& access : pass.accesses) {
            if (!access.isWrite && access.resourceId.IsValid()) {
                resourceLive[access.resourceId.index] = true;
            }
        }
    }

    // Iterate backward through sorted order, culling passes whose writes are all dead.
    bool changed = true;
    while (changed) {
        changed = false;
        for (int sortIdx = static_cast<int>(m_sortedPassIndices.size()) - 1; sortIdx >= 0; --sortIdx) {
            auto& pass = m_passes[m_sortedPassIndices[sortIdx]];
            if (pass.culled) continue;

            // Check if all written resources are dead.
            bool allWritesDead = true;
            bool hasWrites = false;
            for (const auto& access : pass.accesses) {
                if (access.isWrite && access.resourceId.IsValid()) {
                    hasWrites = true;
                    if (resourceLive[access.resourceId.index]) {
                        allWritesDead = false;
                        break;
                    }
                }
            }

            // Passes with no writes are side-effect passes — don't cull them.
            if (!hasWrites) continue;

            if (allWritesDead) {
                pass.culled = true;
                changed = true;

                // Mark all written resources as culled (no live consumer).
                for (const auto& access : pass.accesses) {
                    if (access.isWrite && access.resourceId.IsValid()) {
                        auto& res = m_resources[access.resourceId.index];
                        if (!res.imported) {
                            res.culled = true;
                        }
                    }
                }

                // Un-reference all resources this pass reads.
                for (const auto& access : pass.accesses) {
                    if (!access.isWrite && access.resourceId.IsValid()) {
                        auto& res = m_resources[access.resourceId.index];
                        if (res.refCount > 0) --res.refCount;
                        if (res.refCount == 0 && !res.imported) {
                            resourceLive[access.resourceId.index] = false;
                            res.culled = true;
                        }
                    }
                }

                UZ_LOG(Renderer, Trace, "RenderGraph: culled pass '{}'", pass.name ? pass.name : "?");
            }
        }
    }
}

/// Schedules barriers by walking sorted passes and tracking resource state.
///
/// For each resource access, if the required state differs from the resource's
/// current tracked state, a barrier is appended to the consuming pass.
void RgBuilder::ScheduleBarriers()
{
    // Reset current state to initial state for all resources.
    for (auto& res : m_resources) {
        res.currentState = res.initialState;
    }

    for (uint32_t sortIdx = 0; sortIdx < static_cast<uint32_t>(m_sortedPassIndices.size()); ++sortIdx) {
        auto& pass = m_passes[m_sortedPassIndices[sortIdx]];
        if (pass.culled) continue;

        pass.barriers.clear();

        for (const auto& access : pass.accesses) {
            if (!access.resourceId.IsValid()) continue;

            auto& res = m_resources[access.resourceId.index];
            if (res.culled) continue;

            if (res.currentState != access.state) {
                // Skip no-op transitions from Undefined (resource just created).
                // The RHI handles initial state internally for new resources.
                if (res.currentState != RHIResourceState::Undefined) {
                    pass.barriers.push_back({
                        access.resourceId,
                        res.currentState,
                        access.state
                    });
                    res.lastBarrierSortIndex = sortIdx;
                }

                res.currentState = access.state;
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Execution
// ─────────────────────────────────────────────────────────────────────────────

void RgBuilder::Execute(IRHICommandList* cmdList)
{
    UZ_CHECK(m_compiled);
    UZ_CHECK(!m_executed);
    UZ_CHECK(cmdList != nullptr);

    m_executed = true;

    AcquirePhysicalResources();

    if (m_states) {
        std::vector<uint64_t> textureKeys, bufferKeys;
        for (const auto& res : m_resources) {
            if (!res.imported) continue;
            if (res.kind == RgResourceKind::Texture) textureKeys.push_back(RgStateRegistry::Key(res.physicalTexture));
            else                                     bufferKeys.push_back(RgStateRegistry::Key(res.physicalBuffer));
        }
        m_states->BeginGraphExecute(std::move(textureKeys), std::move(bufferKeys));
    }

    RgPassResources passResources(*this);

    for (uint32_t sortIdx = 0; sortIdx < static_cast<uint32_t>(m_sortedPassIndices.size()); ++sortIdx) {
        auto& pass = m_passes[m_sortedPassIndices[sortIdx]];
        if (pass.culled) continue;

        // Emit barriers.
        if (!pass.barriers.empty()) {
            std::vector<RHIBarrierDesc> rhiBarriers;
            rhiBarriers.reserve(pass.barriers.size());

            for (const auto& barrier : pass.barriers) {
                if (!barrier.resourceId.IsValid()) continue;
                // Skip no-op barriers (DX12 rejects same before/after state).
                if (barrier.stateBefore == barrier.stateAfter) continue;

                const auto& res = m_resources[barrier.resourceId.index];

                RHIBarrierDesc desc{};
                desc.stateBefore = barrier.stateBefore;
                desc.stateAfter = barrier.stateAfter;

                if (res.kind == RgResourceKind::Texture) {
                    desc.texture = res.physicalTexture;
                } else {
                    desc.buffer = res.physicalBuffer;
                }

                rhiBarriers.push_back(desc);
            }

            if (!rhiBarriers.empty()) {
                cmdList->ResourceBarrier(rhiBarriers.data(), static_cast<uint32_t>(rhiBarriers.size()));
            }
        }

        if (m_gpuProfileEnabled) {
            m_device->GpuProfilerBeginTimestamp(cmdList, pass.name);
        }

        pass.executeFn(cmdList, passResources);

        if (m_gpuProfileEnabled) {
            m_device->GpuProfilerEndTimestamp(cmdList, pass.name);
        }
    }

    if (m_states) {
        m_states->EndGraphExecute();
        CommitImportStates();
    }

    ReleasePhysicalResources();
}

// ─────────────────────────────────────────────────────────────────────────────
// Resource-state registry (phase0_render-graph-state-registry)
// ─────────────────────────────────────────────────────────────────────────────

void RgBuilder::ResolveTrackedImportStates()
{
    if (!m_states) return;
    for (auto& res : m_resources) {
        if (!res.imported || !res.stateTracked) continue;
        res.initialState = (res.kind == RgResourceKind::Texture) ? m_states->Get(res.physicalTexture)
                                                                 : m_states->Get(res.physicalBuffer);
        res.currentState = res.initialState;
    }
}

void RgBuilder::ObserveImportClaims()
{
    if (!m_states) return;
    std::vector<bool> accessed(m_resources.size(), false);
    for (const auto& pass : m_passes) {
        if (pass.culled) continue;
        for (const auto& access : pass.accesses) {
            if (access.resourceId.IsValid()) accessed[access.resourceId.index] = true;
        }
    }
    for (uint32_t ri = 0; ri < static_cast<uint32_t>(m_resources.size()); ++ri) {
        const auto& res = m_resources[ri];
        if (!res.imported || res.stateTracked || !accessed[ri]) continue;
        const bool isTexture = res.kind == RgResourceKind::Texture;
        const RHIResourceState tracked = isTexture ? m_states->Get(res.physicalTexture)
                                                   : m_states->Get(res.physicalBuffer);
        if (RgStateRegistry::SameState(res.initialState, tracked)) continue;   // Present is Common in D3D12
        const RgStateOrigin origin = isTexture ? m_states->GetOrigin(res.physicalTexture)
                                               : m_states->GetOrigin(res.physicalBuffer);
        m_states->ReportClaimMismatch(res.name, !isTexture, res.initialState, tracked, origin);
        ++m_claimMismatches;
    }
}

void RgBuilder::CommitImportStates()
{
    // Only resources the graph transitioned: after a barrier the resource is in its stateAfter whatever
    // stateBefore claimed, so the end state is known. An explicit import the graph never transitioned is
    // in whatever state it was in, which its claim may misstate. A handle imported twice (two explicit
    // chains) ends in the state of the chain whose last barrier executed last.
    struct Latest { uint32_t sortIdx; uint32_t resource; };
    std::unordered_map<uint64_t, Latest> textures, buffers;
    for (uint32_t ri = 0; ri < static_cast<uint32_t>(m_resources.size()); ++ri) {
        const auto& res = m_resources[ri];
        if (!res.imported || res.lastBarrierSortIndex == UINT32_MAX) continue;
        const bool isTexture = res.kind == RgResourceKind::Texture;
        auto& latest = isTexture ? textures : buffers;
        const uint64_t key = isTexture ? RgStateRegistry::Key(res.physicalTexture)
                                       : RgStateRegistry::Key(res.physicalBuffer);
        auto it = latest.find(key);
        if (it == latest.end() || res.lastBarrierSortIndex > it->second.sortIdx) {
            latest[key] = {res.lastBarrierSortIndex, ri};
        }
    }
    for (const auto& entry : textures) {
        const auto& res = m_resources[entry.second.resource];
        m_states->CommitFromGraph(res.physicalTexture, res.currentState);
    }
    for (const auto& entry : buffers) {
        const auto& res = m_resources[entry.second.resource];
        m_states->CommitFromGraph(res.physicalBuffer, res.currentState);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Physical Resource Management
// ─────────────────────────────────────────────────────────────────────────────

/// Acquires physical RHI handles for all non-imported, non-culled transient resources.
void RgBuilder::AcquirePhysicalResources()
{
    for (auto& res : m_resources) {
        if (res.imported || res.culled) continue;

        if (res.kind == RgResourceKind::Texture) {
            res.physicalTexture = m_pool->AcquireTexture(res.textureDesc, res.name);
        } else {
            res.physicalBuffer = m_pool->AcquireBuffer(res.bufferDesc, res.name);
        }
    }
}

/// Returns all transient physical resources back to the pool.
void RgBuilder::ReleasePhysicalResources()
{
    for (auto& res : m_resources) {
        if (res.imported || res.culled) continue;

        if (res.kind == RgResourceKind::Texture) {
            m_pool->ReleaseTexture(res.physicalTexture, res.textureDesc);
            res.physicalTexture = RHITextureHandle::Invalid();
        } else {
            m_pool->ReleaseBuffer(res.physicalBuffer, res.bufferDesc);
            res.physicalBuffer = RHIBufferHandle::Invalid();
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Queries
// ─────────────────────────────────────────────────────────────────────────────

uint32_t RgBuilder::GetPassCount() const
{
    return static_cast<uint32_t>(m_passes.size());
}

uint32_t RgBuilder::GetCulledPassCount() const
{
    uint32_t count = 0;
    for (const auto& pass : m_passes) {
        if (pass.culled) ++count;
    }
    return count;
}

uint32_t RgBuilder::GetBarrierCount() const
{
    uint32_t count = 0;
    for (const auto& pass : m_passes) {
        if (!pass.culled) {
            count += static_cast<uint32_t>(pass.barriers.size());
        }
    }
    return count;
}

uint32_t RgBuilder::GetResourceCount() const
{
    return static_cast<uint32_t>(m_resources.size());
}

const RgResourceEntry& RgBuilder::GetResource(RgResourceId id) const
{
    UZ_CHECK(id.IsValid() && id.index < m_resources.size());
    return m_resources[id.index];
}

} // namespace uz::rg
