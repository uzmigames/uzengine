#pragma once

/// Render-graph resource states across frames (phase0_render-graph-state-registry).
///
/// A D3D12 transition names the state the resource is in before it, and the DX12 RHI copies that
/// `stateBefore` into the barrier verbatim: it keeps no state per resource. Until this registry the render
/// graph took an import's state on the caller's word, and the callers predicted it. The audit of
/// 2026-10-08 confirmed 80 cross-frame mismatches (docs/analysis/render-graph-states.md).
///
/// `RgStateRegistry` is the one record of the state each persistent resource is in between graphs:
///   * keyed by the generational handle, so a destroyed and recreated texture is a new key and starts in its
///     creation state, with no reset hook at Resize or any other recreation site;
///   * a texture with no record is in its creation state (DepthStencil usage: DepthStencilWrite, else Common,
///     as dx12_texture_creation.cpp creates it);
///   * buffers decay to Common when their command list is submitted (D3D12 implicit state decay), so a buffer
///     record lives until the bound list's next submit (`BindCommandList`, `IRHICommandList::GetSubmitCount`)
///     or the next explicit `OnQueueSubmit`;
///   * `RgBuilder` resolves tracked imports from it at Compile, reports explicit import claims that disagree
///     with it (the observe mode) and commits the end state of every imported resource it transitioned;
///   * code outside the graph records its raw barriers with `Set` or emits them through `Transition`.
///
/// Single-threaded, like the graph: recording order is the direct queue's state timeline.

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../../rhi/rhi_types.h"

namespace uz {
class IRHIDevice;
class IRHICommandList;
} // namespace uz

namespace uz::rg {

/// Where the registry's belief about a resource comes from (reported by the observe mode).
enum class RgStateOrigin : uint8_t {
    Creation,  ///< No record: the texture's creation state, or Common for a buffer.
    Graph,     ///< Committed by a render graph after Execute.
    External,  ///< Recorded by code outside the graph (`Set` / `Transition`).
};

/// An explicit import claim that disagreed with the registry (observe mode), with how often it did.
struct RgStateClaimMismatch {
    std::string name;
    bool isBuffer = false;
    RHIResourceState claimed = RHIResourceState::Undefined;
    RHIResourceState tracked = RHIResourceState::Undefined;
    RgStateOrigin origin = RgStateOrigin::Creation;  ///< Origin of the tracked state the first time.
    uint64_t count = 0;
};

/// The persistent record of resource states between render graphs. See the file comment.
class RgStateRegistry {
public:
    /// # Arguments
    /// * `device` — answers `GetTextureDesc` for the creation state of a texture with no record. May be
    ///   null (tests): such textures are then taken to be in Common.
    explicit RgStateRegistry(IRHIDevice* device);

    /// Logs the observe-mode summary.
    ~RgStateRegistry();

    RgStateRegistry(const RgStateRegistry&) = delete;
    RgStateRegistry& operator=(const RgStateRegistry&) = delete;

    /// The state the backend creates a texture with this descriptor in.
    [[nodiscard]] static RHIResourceState CreationState(const RHITextureDesc& desc);

    /// Binds the command list whose recording order is the state timeline (the frame's list). Buffer
    /// records are dropped when the bound list changes or when its submit count moves.
    void BindCommandList(IRHICommandList* list);

    // ── Queries ─────────────────────────────────────────────────────────

    /// The recorded state, if any (no creation-state fallback).
    [[nodiscard]] std::optional<RHIResourceState> Find(RHITextureHandle h) const;
    [[nodiscard]] std::optional<RHIResourceState> Find(RHIBufferHandle h) const;

    /// The recorded state, else the creation state (texture) or Common (buffer).
    [[nodiscard]] RHIResourceState Get(RHITextureHandle h) const;
    [[nodiscard]] RHIResourceState Get(RHIBufferHandle h) const;

    /// Where `Get` takes its answer from.
    [[nodiscard]] RgStateOrigin GetOrigin(RHITextureHandle h) const;
    [[nodiscard]] RgStateOrigin GetOrigin(RHIBufferHandle h) const;

    // ── Code outside the graph ──────────────────────────────────────────

    /// Records the state a raw barrier left the resource in.
    void Set(RHITextureHandle h, RHIResourceState s);
    void Set(RHIBufferHandle h, RHIResourceState s);

    /// Emits a transition from the recorded state to `after`; nothing when it is already there.
    void Transition(IRHICommandList* cmd, RHITextureHandle h, RHIResourceState after);
    void Transition(IRHICommandList* cmd, RHIBufferHandle h, RHIResourceState after);

    /// Emits a UAV barrier ({UnorderedAccess, UnorderedAccess}, which the DX12 RHI turns into one) and
    /// records UnorderedAccess.
    void UavBarrier(IRHICommandList* cmd, RHITextureHandle h);
    void UavBarrier(IRHICommandList* cmd, RHIBufferHandle h);

    /// Drops every buffer record (implicit decay to Common), for a list or backend that does not count
    /// its submits.
    void OnQueueSubmit();

    /// Drops a texture record. Memory only: a destroyed handle's key is never reused (generations).
    void Forget(RHITextureHandle h);

    // ── RgBuilder ───────────────────────────────────────────────────────

    /// Records the end state a render graph left an imported resource in.
    void CommitFromGraph(RHITextureHandle h, RHIResourceState s);
    void CommitFromGraph(RHIBufferHandle h, RHIResourceState s);

    /// Observe mode: an explicit import claim that disagrees with the record. Logged the first time each
    /// (name, claim, tracked state) combination is seen, counted every time.
    void ReportClaimMismatch(const char* name, bool isBuffer, RHIResourceState claimed,
                             RHIResourceState tracked, RgStateOrigin origin);

    /// Observe mode: one handle imported twice with explicit claims in one graph (two barrier chains).
    void ReportDoubleImport(const char* firstName, const char* secondName);

    /// While a graph executes, `Set` / `Transition` / `UavBarrier` on a handle it imported is a fatal
    /// error: the graph would then commit a state its callback silently changed.
    void BeginGraphExecute(std::vector<uint64_t> textureKeys, std::vector<uint64_t> bufferKeys);
    void EndGraphExecute();

    [[nodiscard]] const std::vector<RgStateClaimMismatch>& GetClaimMismatches() const { return m_mismatches; }
    [[nodiscard]] uint64_t GetClaimMismatchTotal() const { return m_mismatchTotal; }
    [[nodiscard]] uint64_t GetDoubleImportTotal() const { return m_doubleImportTotal; }
    [[nodiscard]] uint64_t GetGraphsExecuted() const { return m_graphsExecuted; }
    [[nodiscard]] size_t GetRecordedTextureCount() const { return m_textures.size(); }
    [[nodiscard]] size_t GetRecordedBufferCount() const;

    /// The D3D12 state an RHI state stands for, for comparisons: Present is Common
    /// (D3D12_RESOURCE_STATE_PRESENT and D3D12_RESOURCE_STATE_COMMON are both 0).
    [[nodiscard]] static RHIResourceState Canonical(RHIResourceState s) {
        return s == RHIResourceState::Present ? RHIResourceState::Common : s;
    }
    [[nodiscard]] static bool SameState(RHIResourceState a, RHIResourceState b) {
        return Canonical(a) == Canonical(b);
    }

    [[nodiscard]] static const char* StateName(RHIResourceState s);
    [[nodiscard]] static const char* OriginName(RgStateOrigin o);
    [[nodiscard]] static uint64_t Key(RHITextureHandle h) {
        return (static_cast<uint64_t>(h.generation) << 32) | h.index;
    }
    [[nodiscard]] static uint64_t Key(RHIBufferHandle h) {
        return (static_cast<uint64_t>(h.generation) << 32) | h.index;
    }

private:
    struct Record {
        RHIResourceState state = RHIResourceState::Common;
        RgStateOrigin origin = RgStateOrigin::Graph;
    };

    void SyncSubmits() const;
    void CheckNotExecuting(uint64_t key, bool isBuffer, const char* what) const;

    IRHIDevice* m_device = nullptr;
    IRHICommandList* m_list = nullptr;
    mutable uint64_t m_listSubmits = 0;

    std::unordered_map<uint64_t, Record> m_textures;
    mutable std::unordered_map<uint64_t, Record> m_buffers;  ///< Dropped on submit (SyncSubmits).

    bool m_executing = false;
    std::unordered_set<uint64_t> m_executingTextures;
    std::unordered_set<uint64_t> m_executingBuffers;
    uint64_t m_graphsExecuted = 0;

    std::vector<RgStateClaimMismatch> m_mismatches;
    std::unordered_map<std::string, size_t> m_mismatchIndex;
    uint64_t m_mismatchTotal = 0;
    std::unordered_set<std::string> m_doubleImports;
    uint64_t m_doubleImportTotal = 0;
};

} // namespace uz::rg
