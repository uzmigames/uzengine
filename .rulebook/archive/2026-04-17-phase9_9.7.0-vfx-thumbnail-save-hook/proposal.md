# Proposal: phase9_9.7.0-vfx-thumbnail-save-hook

## Why
Phase 8.7.0 shipped `VfxThumbnailCapturer` but the editor never invokes it — no `.thumb.png` files get written, so the 9.4.0 snippet browser always renders the fallback icon.

## What Changes
Hook `VfxThumbnailCapturer` into the VFX editor's Save pipeline: on every `.uzvfxsys` / `.uzfx` save, run a headless capture (128×128, frame 15, seed 42), write the `.thumb.png` next to the asset. Add a "Re-capture thumbnail" action in the inspector for manual refresh.

## Impact
- Affected specs: docs/engine/25-vfx/24-thumbnail-capture.md
- Affected code: VFX editor save handler + renderer headless-frame hook
- Breaking change: NO
- User benefit: Asset browser thumbnails appear automatically after save.
- Estimated effort: M

## Source
UE5 asset-thumbnail-on-save flow.
