# Proposal: phase12_12.9.0-niagara-system-overview-panel

## Why

In the current Niagara-mode layout, the right panel shows the
parameter list — but that duplicates work done by the center panel's
parameter inspector. The parameter list belongs in the center
(contextual to the selected module); the right panel should host
Niagara's **System Overview** — a compact list of every emitter in
the active VfxSystem with quick actions per emitter.

Without a system overview, the author can only switch emitters
through the top tab bar (12.7) — there is no way to reorder, rename,
duplicate, or delete an emitter from the editor.

## What Changes

1. **New `VfxSystemOverviewWidget`** in
   `engine/vfx/editor/vfx_system_overview_widget.{h,cpp}`:
   - Binds to a `VfxSystemComponent*` (or a flat
     `std::vector<ParticleEmitter*>` pointer list for testing).
   - Emits one `EmitterRow` layout item per emitter with sub-rects:
     drag handle, enable checkbox, name, particle count readout,
     move-up / move-down / duplicate / delete buttons.
   - An `AddEmitterRow` at the bottom with `+ Add Emitter` button.

2. **Right panel host** — `VfxEditorOverlay::BuildNiagaraUI`
   replaces the parameter-list render of the right panel with a
   call to `m_systemOverview.Render`.

3. **Callbacks** — `onEmitterSelected`, `onEmitterEnableToggled`,
   `onEmitterMoved`, `onEmitterDuplicated`, `onEmitterDeleted`,
   `onAddEmitterRequested`. Caller-applied contract identical to
   phase 11.2.

4. **Running/stopped dot** — each row shows a colored dot:
   `stateSuccess` green when the emitter is running + alive,
   `stateWarning` amber when paused, `textDim` grey when disabled.

5. **Particle count readout** — right-aligned "N" in `textDim` showing
   the current alive particle count (source: `emitter->aliveParticles`).

## Impact

- Affected specs: `specs/vfx-system-overview-panel/spec.md` (new).
- Affected code:
  - `runtime/engine/vfx/editor/vfx_system_overview_widget.{h,cpp}` —
    new.
  - `runtime/engine/vfx/editor/vfx_editor_overlay.{h,cpp}` — swap
    right-panel render from inspector to overview.
  - `runtime/engine/vfx/editor/vfx_editor_composition.{h,cpp}` —
    `BindSystemOverview` added.
  - `runtime/tests/engine/vfx/test_vfx_system_overview.cpp` — new.
  - `docs/engine/25-vfx/61-system-overview.md` — new.
- Breaking change: MINOR — right-panel content changes role;
  downstream tests that asserted the parameter inspector was rendered
  in the right panel are updated.
- User benefit: first-class emitter management from the editor
  (reorder, duplicate, delete, add) without touching .uzvfxsys
  files manually.
