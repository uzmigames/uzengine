# Proposal: phase8_8.8.0-vfx-snippet-library-ui

## Why
Artists need a library of pre-authored emitter snippets to drag-drop into new systems. Niagara's Emitter Library browses a curated catalog of Fire, Smoke, Sparks, Magic, Impact templates. Depends on tagging (8.2.0) + thumbnails (8.7.0) shipping first.

## What Changes
Build a snippet-library index: a JSON manifest of every .uzvfxsys / .uzfx asset with tags + thumbnail paths. Browser widget filters by tag, previews thumbnail, drops the selected emitter into the active system.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/editor/vfx_snippet_library.h/.cpp
- Breaking change: NO
- User benefit: Artists discover + reuse authored effects without filesystem diving.
- Estimated effort: L

## Source
UE5 UNiagaraSystemEmitterMergeData + Emitter Library UI.
