# Proposal: phase10_10.9.0-renderer-headless-frame-readback

## Why
Phase 9.7 (thumbnail capture) needs a small off-screen RTV + CPU readback so the editor save-hook can grab a 128×128 PNG. The snapshot inspector viewer (9.8 part 2) needs the same primitive to render point-cloud frames. No such helper exists today — demo.exe uses a full-screen swapchain path that's tied to a window.

## What Changes
Add `RendererHeadlessFrameCapture` service: allocate an RTV of configurable size/format, invoke a caller-supplied render callback into it, trigger a GPU→CPU readback via a staging buffer, and return `std::vector<uint8_t>` of PNG bytes (via the existing PNG encoder). Completely decoupled from the swapchain.

## Impact
- Affected specs: docs/engine/renderer/
- Affected code: new runtime/engine/renderer/headless/*
- Breaking change: NO
- User benefit: Foundation for thumbnails, snapshot viewers, CI screenshot bots.
- Estimated effort: M

## Source
Standard off-screen render + readback pattern.
