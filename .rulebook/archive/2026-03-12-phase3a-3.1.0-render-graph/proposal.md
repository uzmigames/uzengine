# Proposal: 3.1.0 — Render Graph

## Why
Every demo manages barriers, resource states, and pass ordering manually. This is error-prone and blocks all advanced rendering (deferred, shadows, post-process). A DAG-based render graph automates barrier insertion and enables transient resource aliasing, reducing VRAM usage.

## What Changes
- RgBuilder API: declare passes, textures, buffers in a frame DAG
- RgPass: read/write resource declarations with execution callbacks
- Automatic RHI barrier scheduling from resource state tracking
- Transient resource aliasing via lifetime analysis and memory pool reuse
- Pass culling: unused outputs → pass removed from execution
- Frame resource pooling across frames
- ALL rendering after this milestone goes through graph — no manual ResourceBarrier calls

## Impact
- Affected specs: `docs/engine/12-rendering/04-render-graph.md`
- Affected code: `runtime/core/renderer/render_graph/`, all existing demo render loops
- Breaking change: YES — manual barrier code removed, all passes must use graph
- User benefit: Zero manual barriers, automatic optimization, foundation for deferred/shadows/post-process
