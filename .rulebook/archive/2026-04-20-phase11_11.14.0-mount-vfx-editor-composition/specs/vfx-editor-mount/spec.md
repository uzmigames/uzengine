# vfx-editor-mount

## ADDED Requirements

### Requirement: Niagara-mode toggle on VfxEditorOverlay

The `VfxEditorOverlay` SHALL expose a `SetNiagaraMode(bool)` /
`IsNiagaraMode()` / `ToggleNiagaraMode()` triad that selects between
the legacy slider tree (`BuildStageTree` / `BuildStackPanel`) and the
Niagara-parity composition rendering path. The mode MUST default to
`false` so that callers who do not opt in see the legacy behavior.

#### Scenario: Default mode is legacy

Given a freshly-constructed `VfxEditorOverlay`
When the caller queries `IsNiagaraMode()`
Then the return value is `false`.

#### Scenario: ToggleNiagaraMode flips the flag

Given a `VfxEditorOverlay` with `IsNiagaraMode() == false`
When the caller invokes `ToggleNiagaraMode()`
Then `IsNiagaraMode()` returns `true`.

### Requirement: Composition widgets are owned members

When Niagara mode is active, the overlay SHALL render the stack
panel, parameter inspector, curves editor, timeline scrubber, and
module picker by routing the overlay's `TextRenderer` and
`BitmapFont` into each widget's `Render()` method. Widget
instances MUST be owned members of the overlay (not heap pointers)
so lifetimes match the overlay itself.

#### Scenario: Stack panel is bound to active emitter

Given a `VfxEditorOverlay` in Niagara mode with a non-null
`ParticleEmitter*`
When `BuildUI(emitter)` runs
Then `m_stackPanel.GetStack()` returns a non-null pointer equal to
`&emitter->stack`.

### Requirement: Layout derives from VfxEditorComposition

The overlay SHALL derive widget rectangles from
`VfxEditorComposition::ComputeLayout(screenW, screenH)` each frame
while Niagara mode is active. Legacy mode retains the existing
`kLeftPanelW`-based pixel layout.

#### Scenario: Stack panel rect width follows layout fraction

Given an overlay sized 1280x720 in Niagara mode with the default
`VfxEditorLayoutConfig` (stackPanelWidthFrac = 0.25)
When the overlay builds its Niagara layout
Then the stack panel rect width equals 320 pixels (25% of 1280).

### Requirement: F11 toggles Niagara mode in VFXViewportApp

The VFX viewport demo application SHALL bind the F11 key to
`overlay->ToggleNiagaraMode()` so the author can switch between
legacy and Niagara rendering without restarting the app. F10
continues to show / hide the overlay entirely and has higher
precedence (F11 only takes effect while the overlay is visible).

#### Scenario: F11 toggles Niagara mode while overlay is visible

Given the overlay is visible and Niagara mode is `false`
When the user presses F11
Then the overlay's `IsNiagaraMode()` returns `true` and the next
`BuildUI` call renders through the composition.
