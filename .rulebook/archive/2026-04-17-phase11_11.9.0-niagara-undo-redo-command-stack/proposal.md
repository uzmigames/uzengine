# Proposal: phase11_11.9.0-niagara-undo-redo-command-stack

## Why
The current VFX editor has no undo — once you delete a module, rebind a parameter, or move a keyframe, your only recovery is "restart the app" (losing all unsaved work). Every Niagara-like editor has Ctrl+Z / Ctrl+Y. Without this, authors avoid experimenting, which kills the iteration loop the entire phase 11 effort exists to enable.

## What Changes
New `VfxUndoCommand` base + `VfxUndoStack` registry in `runtime/engine/vfx/editor/undo/`. Each mutation in 11.2/11.3/11.4/11.5/11.7/11.8 routes through a command — `StackAddModuleCommand`, `StackRemoveModuleCommand`, `StackReorderCommand`, `ParameterAddCommand`, `ParameterRenameCommand`, `CurveKeyAddCommand`, `CurveKeyMoveCommand`, `BurstAddCommand`, etc. The stack holds up to 100 entries (configurable). Ctrl+Z pops + reverses; Ctrl+Y / Ctrl+Shift+Z re-applies. Save event clears the redo branch but preserves undo history.

## Impact
- Affected specs: docs/engine/25-vfx/ (new: 49-undo-redo.md)
- Affected code: runtime/engine/vfx/editor/undo/* (new); every 11.x widget routes mutations through commands
- Breaking change: NO (additive)
- User benefit: fearless experimentation; 100-deep undo
- Estimated effort: M

## Source
Command pattern (Gamma et al. 1994) + UE5 `FTransaction`.
