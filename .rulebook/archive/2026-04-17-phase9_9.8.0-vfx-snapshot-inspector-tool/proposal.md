# Proposal: phase9_9.8.0-vfx-snapshot-inspector-tool

## Why
Phase 8.19.0 shipped the `.uzvfxsnap` file format + loader + stdout dump. An interactive inspector tool is still missing so authors can't visually step through a captured emitter state.

## What Changes
New `uzvfxinspect` CLI + standalone viewer app. CLI dumps the snapshot to stdout or JSON. Viewer renders the particle cloud as a camera-orbital point cloud with per-channel colouring; a scrubber shows per-voxel values for Float3 channels.

## Impact
- Affected specs: docs/engine/25-vfx/36-emitter-snapshot.md
- Affected code: new `runtime/tools/uzvfxinspect/` binary
- Breaking change: NO
- User benefit: Visual replay of captured particle states for debugging.
- Estimated effort: L

## Source
UE5 Niagara Debugger snapshot viewer.
