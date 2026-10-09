/// Render-graph resource states across frames — see render_graph_states.h.

#include "render_graph_states.h"

#include "../../rhi/rhi.h"
#include "log.h"
#include "engine_assert.h"

#include <utility>

namespace uz::rg {

RgStateRegistry::RgStateRegistry(IRHIDevice* device) : m_device(device) {}

RgStateRegistry::~RgStateRegistry()
{
    if (m_graphsExecuted == 0) {
        return;
    }
    UZ_LOG(Renderer, Info,
           "RenderGraph states (observe): {} graphs; {} distinct explicit import claims disagreed with the "
           "tracked state ({} times); {} explicit double imports",
           m_graphsExecuted, m_mismatches.size(), m_mismatchTotal, m_doubleImportTotal);
    for (const auto& m : m_mismatches) {
        UZ_LOG(Renderer, Info, "RenderGraph states (observe):   {} '{}' claimed {} tracked {} ({}) x{}",
               m.isBuffer ? "buffer" : "texture", m.name, StateName(m.claimed), StateName(m.tracked),
               OriginName(m.origin), m.count);
    }
}

RHIResourceState RgStateRegistry::CreationState(const RHITextureDesc& desc)
{
    // dx12_texture_creation.cpp: D3D12_RESOURCE_STATE_DEPTH_WRITE for depth-stencil textures, COMMON for
    // every other texture.
    return HasFlag(desc.usage, RHITextureUsage::DepthStencil) ? RHIResourceState::DepthStencilWrite
                                                              : RHIResourceState::Common;
}

void RgStateRegistry::BindCommandList(IRHICommandList* list)
{
    if (list != m_list) {
        m_buffers.clear();   // a different list starts with every buffer in Common
        m_list = list;
    }
    m_listSubmits = m_list ? m_list->GetSubmitCount() : 0;
}

void RgStateRegistry::SyncSubmits() const
{
    if (!m_list) {
        return;
    }
    const uint64_t n = m_list->GetSubmitCount();
    if (n != m_listSubmits) {
        m_buffers.clear();   // the list was submitted: its buffers decayed to Common
        m_listSubmits = n;
    }
}

std::optional<RHIResourceState> RgStateRegistry::Find(RHITextureHandle h) const
{
    const auto it = m_textures.find(Key(h));
    if (it == m_textures.end()) {
        return std::nullopt;
    }
    return it->second.state;
}

std::optional<RHIResourceState> RgStateRegistry::Find(RHIBufferHandle h) const
{
    SyncSubmits();
    const auto it = m_buffers.find(Key(h));
    if (it == m_buffers.end()) {
        return std::nullopt;
    }
    return it->second.state;
}

RHIResourceState RgStateRegistry::Get(RHITextureHandle h) const
{
    if (const auto s = Find(h)) {
        return *s;
    }
    return m_device ? CreationState(m_device->GetTextureDesc(h)) : RHIResourceState::Common;
}

RHIResourceState RgStateRegistry::Get(RHIBufferHandle h) const
{
    return Find(h).value_or(RHIResourceState::Common);
}

RgStateOrigin RgStateRegistry::GetOrigin(RHITextureHandle h) const
{
    const auto it = m_textures.find(Key(h));
    return it == m_textures.end() ? RgStateOrigin::Creation : it->second.origin;
}

RgStateOrigin RgStateRegistry::GetOrigin(RHIBufferHandle h) const
{
    SyncSubmits();
    const auto it = m_buffers.find(Key(h));
    return it == m_buffers.end() ? RgStateOrigin::Creation : it->second.origin;
}

void RgStateRegistry::CheckNotExecuting(uint64_t key, bool isBuffer, const char* what) const
{
    if (!m_executing) {
        return;
    }
    const bool imported = isBuffer ? m_executingBuffers.count(key) != 0 : m_executingTextures.count(key) != 0;
    UZ_CHECKF(!imported,
              "RgStateRegistry::%s on a %s the executing render graph imported: declare the access in the "
              "pass instead (the graph would commit a state the callback changed)",
              what, isBuffer ? "buffer" : "texture");
}

void RgStateRegistry::Set(RHITextureHandle h, RHIResourceState s)
{
    CheckNotExecuting(Key(h), false, "Set");
    m_textures[Key(h)] = {s, RgStateOrigin::External};
}

void RgStateRegistry::Set(RHIBufferHandle h, RHIResourceState s)
{
    CheckNotExecuting(Key(h), true, "Set");
    SyncSubmits();
    m_buffers[Key(h)] = {s, RgStateOrigin::External};
}

void RgStateRegistry::Transition(IRHICommandList* cmd, RHITextureHandle h, RHIResourceState after)
{
    CheckNotExecuting(Key(h), false, "Transition");
    const RHIResourceState before = Get(h);
    if (!SameState(before, after)) {
        RHIBarrierDesc b{};
        b.texture = h;
        b.stateBefore = before;
        b.stateAfter = after;
        cmd->ResourceBarrier(&b, 1);
    }
    m_textures[Key(h)] = {after, RgStateOrigin::External};
}

void RgStateRegistry::Transition(IRHICommandList* cmd, RHIBufferHandle h, RHIResourceState after)
{
    CheckNotExecuting(Key(h), true, "Transition");
    const RHIResourceState before = Get(h);
    if (!SameState(before, after)) {
        RHIBarrierDesc b{};
        b.buffer = h;
        b.stateBefore = before;
        b.stateAfter = after;
        cmd->ResourceBarrier(&b, 1);
    }
    m_buffers[Key(h)] = {after, RgStateOrigin::External};
}

void RgStateRegistry::UavBarrier(IRHICommandList* cmd, RHITextureHandle h)
{
    Transition(cmd, h, RHIResourceState::UnorderedAccess);
    RHIBarrierDesc b{};
    b.texture = h;
    b.stateBefore = RHIResourceState::UnorderedAccess;
    b.stateAfter = RHIResourceState::UnorderedAccess;
    cmd->ResourceBarrier(&b, 1);
}

void RgStateRegistry::UavBarrier(IRHICommandList* cmd, RHIBufferHandle h)
{
    Transition(cmd, h, RHIResourceState::UnorderedAccess);
    RHIBarrierDesc b{};
    b.buffer = h;
    b.stateBefore = RHIResourceState::UnorderedAccess;
    b.stateAfter = RHIResourceState::UnorderedAccess;
    cmd->ResourceBarrier(&b, 1);
}

void RgStateRegistry::OnQueueSubmit()
{
    m_buffers.clear();
    m_listSubmits = m_list ? m_list->GetSubmitCount() : 0;
}

void RgStateRegistry::Forget(RHITextureHandle h)
{
    m_textures.erase(Key(h));
}

void RgStateRegistry::CommitFromGraph(RHITextureHandle h, RHIResourceState s)
{
    m_textures[Key(h)] = {s, RgStateOrigin::Graph};
}

void RgStateRegistry::CommitFromGraph(RHIBufferHandle h, RHIResourceState s)
{
    SyncSubmits();
    m_buffers[Key(h)] = {s, RgStateOrigin::Graph};
}

void RgStateRegistry::ReportClaimMismatch(const char* name, bool isBuffer, RHIResourceState claimed,
                                          RHIResourceState tracked, RgStateOrigin origin)
{
    ++m_mismatchTotal;
    std::string key = std::string(name ? name : "?") + '|' + std::to_string(static_cast<uint32_t>(claimed)) +
                      '|' + std::to_string(static_cast<uint32_t>(tracked)) + (isBuffer ? "|b" : "|t");
    const auto it = m_mismatchIndex.find(key);
    if (it != m_mismatchIndex.end()) {
        ++m_mismatches[it->second].count;
        return;
    }
    m_mismatchIndex.emplace(std::move(key), m_mismatches.size());
    m_mismatches.push_back({name ? name : "?", isBuffer, claimed, tracked, origin, 1});
    UZ_LOG(Renderer, Info,
           "RenderGraph states (observe): {} '{}' imported as {}, but the registry tracks {} ({}); the first "
           "barrier of this frame names the wrong state before",
           isBuffer ? "buffer" : "texture", name ? name : "?", StateName(claimed), StateName(tracked),
           OriginName(origin));
}

void RgStateRegistry::ReportDoubleImport(const char* firstName, const char* secondName)
{
    ++m_doubleImportTotal;
    std::string key = std::string(firstName ? firstName : "?") + '|' + (secondName ? secondName : "?");
    if (m_doubleImports.insert(key).second) {
        UZ_LOG(Renderer, Info,
               "RenderGraph states (observe): '{}' and '{}' import the same resource with explicit claims "
               "(two barrier chains in one graph)",
               firstName ? firstName : "?", secondName ? secondName : "?");
    }
}

void RgStateRegistry::BeginGraphExecute(std::vector<uint64_t> textureKeys, std::vector<uint64_t> bufferKeys)
{
    m_executing = true;
    m_executingTextures = std::unordered_set<uint64_t>(textureKeys.begin(), textureKeys.end());
    m_executingBuffers = std::unordered_set<uint64_t>(bufferKeys.begin(), bufferKeys.end());
}

void RgStateRegistry::EndGraphExecute()
{
    m_executing = false;
    m_executingTextures.clear();
    m_executingBuffers.clear();
    ++m_graphsExecuted;
}

size_t RgStateRegistry::GetRecordedBufferCount() const
{
    SyncSubmits();
    return m_buffers.size();
}

const char* RgStateRegistry::StateName(RHIResourceState s)
{
    switch (s) {
        case RHIResourceState::Undefined:         return "Undefined";
        case RHIResourceState::Common:            return "Common";
        case RHIResourceState::VertexBuffer:      return "VertexBuffer";
        case RHIResourceState::IndexBuffer:       return "IndexBuffer";
        case RHIResourceState::ConstantBuffer:    return "ConstantBuffer";
        case RHIResourceState::ShaderResource:    return "ShaderResource";
        case RHIResourceState::UnorderedAccess:   return "UnorderedAccess";
        case RHIResourceState::RenderTarget:      return "RenderTarget";
        case RHIResourceState::DepthStencilWrite: return "DepthStencilWrite";
        case RHIResourceState::DepthStencilRead:  return "DepthStencilRead";
        case RHIResourceState::CopySrc:           return "CopySrc";
        case RHIResourceState::CopyDst:           return "CopyDst";
        case RHIResourceState::Present:           return "Present";
        case RHIResourceState::IndirectArgument:  return "IndirectArgument";
        case RHIResourceState::RaytracingAccelerationStructure: return "RaytracingAccelerationStructure";
    }
    return "(combined)";
}

const char* RgStateRegistry::OriginName(RgStateOrigin o)
{
    switch (o) {
        case RgStateOrigin::Creation: return "creation state";
        case RgStateOrigin::Graph:    return "committed by a graph";
        case RgStateOrigin::External: return "set outside the graph";
    }
    return "?";
}

} // namespace uz::rg
