# Proposal: phase8_8.7.0-vfx-asset-thumbnail-capture

## Why
VFX assets in the browser show a generic icon with no visual cue. Niagara auto-captures a thumbnail image by headless-rendering the effect on save. Leverages the baker infrastructure (7.18.0) since it is the same render-to-atlas pattern.

## What Changes
Add a VfxThumbnailCapturer that runs the effect headless for a fixed wall-clock budget (e.g. 0.5s at 60fps), captures the final frame, and saves as PNG alongside the asset. Browser UI loads the PNG at display time.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/baker/ (new vfx_thumbnail.h/.cpp)
- Breaking change: NO
- User benefit: Asset browser shows visual previews without full baking.
- Estimated effort: M

## Source
UE5 UNiagaraThumbnailRenderer.
