/// UzEngine RenderWorld — Generational proxy pool implementation.

#include "render_world.h"
#include "engine_assert.h"

#include <cstring>

namespace uz {

UZ_DEFINE_LOG_CATEGORY(RenderWorld, Trace);

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

bool RenderWorld::IsHandleValid(RenderProxyHandle handle) const {
    if (!handle.IsValid()) {
        return false;
    }
    if (handle.index >= m_generations.size()) {
        return false;
    }
    // Generation mismatch means the slot was reused after this handle was issued.
    return m_generations[handle.index] == handle.generation;
}

// ─────────────────────────────────────────────────────────────────────────────
// Proxy Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

RenderProxyHandle RenderWorld::CreateProxy(const RenderProxyDesc& desc) {
    uint32_t slotIndex = 0;

    if (!m_freeSlots.empty()) {
        // Reuse the most recently freed slot (LIFO — better cache reuse).
        slotIndex = m_freeSlots.back();
        m_freeSlots.pop_back();

        // Generation was already bumped during DestroyProxy; read it back.
        RenderProxy& slot = m_proxies[slotIndex];
        slot = RenderProxy{};
        slot.handle.index = slotIndex;
        slot.handle.generation = m_generations[slotIndex];
        slot.type = desc.type;
        slot.worldMatrix = desc.worldMatrix;
        slot.visible = desc.visible;
        slot.castShadows = desc.castShadows;
        slot.renderLayer = desc.renderLayer;
    } else {
        // No free slot — allocate a new one.
        slotIndex = static_cast<uint32_t>(m_proxies.size());

        RenderProxy proxy{};
        proxy.handle.index = slotIndex;
        proxy.handle.generation = 0;
        proxy.type = desc.type;
        proxy.worldMatrix = desc.worldMatrix;
        proxy.visible = desc.visible;
        proxy.castShadows = desc.castShadows;
        proxy.renderLayer = desc.renderLayer;

        m_proxies.push_back(proxy);
        m_generations.push_back(0);
    }

    ++m_liveCount;

    RenderProxyHandle handle;
    handle.index = slotIndex;
    handle.generation = m_generations[slotIndex];

    UZ_LOG(RenderWorld, Trace,
        "CreateProxy: index={} generation={} type={}",
        handle.index, handle.generation, static_cast<uint32_t>(desc.type));

    return handle;
}

void RenderWorld::DestroyProxy(RenderProxyHandle handle) {
    if (!IsHandleValid(handle)) {
        UZ_LOG(RenderWorld, Warning,
            "DestroyProxy: invalid or stale handle index={} generation={}",
            handle.index, handle.generation);
        return;
    }

    // Bump generation to invalidate all existing handles to this slot.
    ++m_generations[handle.index];

    // Mark the proxy as logically dead. The slot data is left in place
    // (will be overwritten on next CreateProxy reuse) to avoid a pointless memset.
    m_proxies[handle.index].visible = false;
    m_proxies[handle.index].handle.index = UINT32_MAX; // sentinel: slot is free

    m_freeSlots.push_back(handle.index);
    --m_liveCount;

    UZ_LOG(RenderWorld, Trace,
        "DestroyProxy: index={} generation={} (bumped to {})",
        handle.index, handle.generation, m_generations[handle.index]);
}

// ─────────────────────────────────────────────────────────────────────────────
// Proxy Access
// ─────────────────────────────────────────────────────────────────────────────

RenderProxy* RenderWorld::GetProxy(RenderProxyHandle handle) {
    if (!IsHandleValid(handle)) {
        return nullptr;
    }
    return &m_proxies[handle.index];
}

const RenderProxy* RenderWorld::GetProxy(RenderProxyHandle handle) const {
    if (!IsHandleValid(handle)) {
        return nullptr;
    }
    return &m_proxies[handle.index];
}

// ─────────────────────────────────────────────────────────────────────────────
// Proxy Updates
// ─────────────────────────────────────────────────────────────────────────────

void RenderWorld::SetTransform(RenderProxyHandle handle, const Mat4& worldMatrix) {
    RenderProxy* proxy = GetProxy(handle);
    if (!proxy) {
        UZ_LOG(RenderWorld, Warning,
            "SetTransform: invalid or stale handle index={} generation={}",
            handle.index, handle.generation);
        return;
    }
    proxy->worldMatrix = worldMatrix;
}

void RenderWorld::SetVisible(RenderProxyHandle handle, bool visible) {
    RenderProxy* proxy = GetProxy(handle);
    if (!proxy) {
        UZ_LOG(RenderWorld, Warning,
            "SetVisible: invalid or stale handle index={} generation={}",
            handle.index, handle.generation);
        return;
    }
    proxy->visible = visible;
}

// ─────────────────────────────────────────────────────────────────────────────
// Queries
// ─────────────────────────────────────────────────────────────────────────────

size_t RenderWorld::ProxyCount() const {
    return m_liveCount;
}

void RenderWorld::Clear() {
    m_proxies.clear();
    m_generations.clear();
    m_freeSlots.clear();
    m_liveCount = 0;

    UZ_LOG(RenderWorld, Trace, "Clear: all proxies removed");
}

} // namespace uz
