#pragma once

/// Render Graph Pass — Declares resource dependencies and execution callback.
///
/// Each `RgPass` represents a single render, compute, or copy operation in the DAG.
/// Passes declare which resources they read and write via `RgPassBuilder`, and provide
/// an execution callback that records GPU commands using resolved physical handles.
///
/// # Pass Lifecycle
/// 1. User calls `RgBuilder::AddPass(name, setup_fn)`.
/// 2. `setup_fn` receives an `RgPassBuilder&` to declare reads/writes and set the callback.
/// 3. After `Compile()`, passes are topologically sorted and barriers are scheduled.
/// 4. During `Execute()`, each non-culled pass's callback is invoked with resolved resources.

#include <vector>
#include "render_graph_types.h"

namespace uz::rg {

// ─────────────────────────────────────────────────────────────────────────────
// RgPass — internal pass representation
// ─────────────────────────────────────────────────────────────────────────────

/// Internal representation of a render graph pass.
///
/// Stores the pass name, resource access declarations, scheduled barriers,
/// the execution callback, and culling state. Constructed by `RgPassBuilder`
/// during `AddPass()`.
struct RgPass {
    const char* name = nullptr;                  ///< Debug name for logging and visualization.
    std::vector<RgResourceAccess> accesses;      ///< All resource reads and writes declared by this pass.
    std::vector<RgBarrier> barriers;             ///< Barriers to emit before this pass executes.
    RgPassExecuteFn executeFn;                   ///< Callback that records GPU commands.
    bool culled = false;                         ///< True if pass was culled (no live output consumers).
    uint32_t sortIndex = UINT32_MAX;             ///< Position in the topologically sorted execution order.
};

// ─────────────────────────────────────────────────────────────────────────────
// RgPassBuilder — setup interface passed to AddPass callbacks
// ─────────────────────────────────────────────────────────────────────────────

/// Builder interface provided to `AddPass()` setup callbacks.
///
/// Allows the callback to declare which resources the pass reads and writes,
/// and to set the execution callback. Each `Read()` or `Write()` call appends
/// to the pass's access list, which the compiler uses for dependency analysis
/// and barrier scheduling.
///
/// # Thread Safety
/// Not thread-safe. Only one thread should call methods on a given `RgPassBuilder`.
///
/// # Lifetime
/// Valid only for the duration of the `AddPass()` setup callback.
class RgPassBuilder {
public:
    /// Constructs a pass builder targeting the given pass.
    ///
    /// # Arguments
    /// * `pass` — The pass being configured (owned by the builder).
    explicit RgPassBuilder(RgPass& pass) : m_pass(pass) {}

    /// Declares that this pass reads a resource in the given state.
    ///
    /// The compiler will insert a barrier before this pass if the resource's
    /// prior state differs from `state`.
    ///
    /// # Arguments
    /// * `id` — Virtual resource handle (must have been created or imported).
    /// * `state` — The `RHIResourceState` this pass requires for reading
    ///   (e.g., `ShaderResource`, `CopySrc`, `IndirectArgument`).
    ///
    /// # Returns
    /// The same `id` for chaining convenience.
    RgResourceId Read(RgResourceId id, RHIResourceState state);

    /// Declares that this pass writes to a resource in the given state.
    ///
    /// The compiler will insert a barrier before this pass if the resource's
    /// prior state differs from `state`. Writing also updates the resource's
    /// tracked state for subsequent passes.
    ///
    /// # Arguments
    /// * `id` — Virtual resource handle (must have been created or imported).
    /// * `state` — The `RHIResourceState` this pass requires for writing
    ///   (e.g., `RenderTarget`, `DepthStencilWrite`, `UnorderedAccess`, `CopyDst`).
    ///
    /// # Returns
    /// The same `id` for chaining convenience.
    RgResourceId Write(RgResourceId id, RHIResourceState state);

    /// Sets the execution callback for this pass.
    ///
    /// The callback is invoked during `Execute()` with a command list and
    /// resolved resource handles. Only one callback may be set per pass —
    /// calling this again overwrites the previous callback.
    ///
    /// # Arguments
    /// * `fn` — The execution callback. Must not be null.
    void SetExecutionCallback(RgPassExecuteFn fn);

private:
    RgPass& m_pass;
};

} // namespace uz::rg
