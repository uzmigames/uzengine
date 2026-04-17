# Session 2026-03-28 — Findings

## What Works
- Process isolation (worker child process)
- H.264 encoding + WebCodecs decoding (Annex-B in-band SPS/PPS)
- Scene loading (21 entities, 9 mesh entities in SyncWorldToDrawList)
- Log callback retrofitting (BUG-8 fixed — all C++ logs now visible)
- Tab switch lifecycle (handleReady runs once)

## Root Cause: DEVICE REMOVED
The `encodeCmdList` captures the backbuffer BEFORE Present(), but:
1. The render `cmdList` submits commands and the GPU is still executing them
2. `encodeCmdList->Begin()` runs immediately after — no fence wait
3. Both command lists access the same backbuffer simultaneously
4. DX12 detects the conflict → `CommandList::Close failed E_INVALIDARG`
5. This causes `DEVICE REMOVED (0x887A0001)`
6. Every subsequent GPU operation fails in an infinite loop

## Fix Required
The readback must happen AFTER the render GPU work completes:
- Option A: Move readback back to `uz_encode_frame()` (after Present), but wait on the frame fence first
- Option B: Use a copy queue (separate from direct queue) for the readback
- Option C: Do the readback inside `SceneRenderer::RenderFrame()` before the final Present transition

## VFS Path Bug
`setContentRoot` is called after `createViewport`. The VFS during `SceneRenderer::Init()` uses the worker's CWD (VSCode install dir). Lua scripts fail: `'F:\IDEs\Microsoft VS Code/scripts/cube_rotator.lua'`.

Fix: Call `setContentRoot` BEFORE `createViewport` in scene-editor-provider.ts.
