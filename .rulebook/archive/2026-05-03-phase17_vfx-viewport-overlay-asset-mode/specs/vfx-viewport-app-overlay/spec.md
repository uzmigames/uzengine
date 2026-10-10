# VFX Viewport App Overlay Visibility - Spec Delta

## ADDED Requirements

### Requirement: Niagara-Parity Editor Overlay Visible by Default in Both Modes

The `runtime/demos/vfx_viewport_app/vfx_viewport_app.exe` MUST show the
Niagara-parity editor overlay (`runtime/engine/vfx/editor/`) by default
when launched in EITHER mode:

- Default mode: no `--vfx-asset` argument
- Asset mode: any `--vfx-asset <path>` argument

The overlay MUST be initialised with `SetNiagaraMode(true)` and
`SetLayoutMode(SplitHorizontal)` so the user sees the parameter
inspector, module stack, and splitter widget on the right side of the
viewport.

#### Scenario: Default-mode launch shows the editor overlay

Given the demo is launched with no `--vfx-asset` argument
When `OnStart()` returns
Then `GetVfxEditorOverlay()->IsVisible()` returns true
And `GetVfxEditorOverlay()->GetLayoutMode() == VfxEditorLayoutMode::SplitHorizontal`
And `GetVfxEditorOverlay()->IsNiagaraMode() == true`

#### Scenario: Asset-mode launch shows the editor overlay

Given the demo is launched with `--vfx-asset vfx/presets/magic_orb.uzvfx`
When `OnStart()` returns
Then `GetVfxEditorOverlay()->IsVisible()` returns true
And the overlay's parameter inspector is bound to the loaded asset's emitter
And changing a parameter in the inspector at runtime visibly affects the running effect within 60 frames

### Requirement: Asset-Mode Emitter Manipulation Callbacks Wired

When the demo runs in asset mode AND the editor overlay is visible,
the user MUST be able to use the overlay's emitter manipulation
callbacks (Add Emitter, Duplicate, Delete, Move, Toggle Enable, Select)
on the loaded asset's emitter list. The same lambda implementations
used by default mode are reused.

#### Scenario: Asset-mode user can duplicate the loaded emitter

Given the demo is in asset mode with a single loaded emitter
When the user invokes the overlay's "Duplicate Emitter" action on emitter index 0
Then `m_emitterEntities.size()` increases by 1
And the new emitter has identical parameters to the original
And the new emitter renders alongside the original in the viewport
