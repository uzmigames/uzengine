#pragma once

/// Base interface for every undoable VFX editor mutation (phase 11.9.0).
///
/// Every editor action that mutates authored data — stack edits,
/// parameter inspector edits, curve keyframe edits, burst edits — is
/// wrapped in a concrete `IVfxUndoCommand` subclass and pushed onto the
/// owner's `VfxUndoStack`. Two functions describe the command:
///
/// - `Apply()`    : performs the mutation the first time (and on redo).
/// - `Revert()`   : restores the prior state for undo.
///
/// Commands must hold whatever captured state they need so both
/// directions are deterministic without relying on the live scene (e.g.
/// `StackRemoveModuleCommand` keeps a copy of the removed
/// `VfxModuleInstance` for the revert path).
///
/// # Threading
///
/// Commands run on the editor / main thread. They must not interact
/// with the render thread — all Apply/Revert work is CPU-only mutation
/// of authoring data; any GPU bake happens later via the usual dirty-
/// flag paths.
///
/// @ref Gamma et al. 1994 Command pattern
/// @ref UE5 `FTransaction` (scope-capturing equivalent)

namespace uz {

/// Minimal interface all undoable commands implement.
class IVfxUndoCommand {
public:
    virtual ~IVfxUndoCommand() = default;

    /// Performs the mutation. Called by `VfxUndoStack::Push` implicitly
    /// (via the caller pushing an already-applied command) OR by
    /// `VfxUndoStack::Redo`. Concrete commands document which semantics
    /// they expect — most follow the "pre-applied on construction"
    /// pattern so `Apply()` only needs to run on redo.
    virtual void Apply() = 0;

    /// Reverses the mutation. Called by `VfxUndoStack::Undo`.
    virtual void Revert() = 0;

    /// Stable short name for the command (shown in Undo History UI).
    /// Must be valid for the lifetime of the command.
    [[nodiscard]] virtual const char* Name() const noexcept = 0;

    IVfxUndoCommand() = default;
    IVfxUndoCommand(const IVfxUndoCommand&) = delete;
    IVfxUndoCommand& operator=(const IVfxUndoCommand&) = delete;
};

} // namespace uz
