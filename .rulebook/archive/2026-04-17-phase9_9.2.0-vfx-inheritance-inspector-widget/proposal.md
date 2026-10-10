# Proposal: phase9_9.2.0-vfx-inheritance-inspector-widget

## Why
Phase 8.3.0 shipped the JSON inheritance resolver + hot-reload cascade, but authors can't see which modules come from a parent vs their own overrides. The inspector needs to distinguish "inherited" from "overridden" so the mental model is legible.

## What Changes
Add a parent picker (file path with asset-browser completion) to the `.uzvfx` inspector; render the resolved module stack with coloured bullets (inherited = grey, override = accent). "Revert" button per override pops it back to the parent value.

## Impact
- Affected specs: docs/engine/25-vfx/20-emitter-inheritance.md
- Affected code: runtime/engine/vfx/editor/* + VfxNodeGraphAsset inspector
- Breaking change: NO
- User benefit: Legible parent→child module list; one-click revert.
- Estimated effort: M

## Source
UE5 Blueprint inheritance indicators + Niagara override markers.
