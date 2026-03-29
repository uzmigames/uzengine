# DEVICE REMOVED Fix Plan

## Problem
`uz_encode_frame()` causes DEVICE REMOVED by doing readback from the swapchain backbuffer after Present(). After Present(), the backbuffer index rotates and the old buffer is owned by the display driver.

## Solution (from dx12-frame-capture-research.md)
Render to intermediate LDR texture → copy to BOTH readback buffer AND backbuffer → Present.

All copies in the SAME command list, before Present(). No separate command list for encode.

## Files to Modify

### 1. `runtime/engine/renderer/scene/scene_renderer_impl.h`
- Add `RHITextureHandle GetLdrIntermediate() const` getter
- Ensure `m_fxaaLdrIntermediate` is always allocated (not just when FXAA/CAS active)

### 2. `runtime/engine/renderer/frame/frame_setup.cpp`
- Always allocate `m_fxaaLdrIntermediate` during Init/Resize

### 3. `runtime/engine/renderer/frame/frame_postprocess.cpp`
- Always tonemap to `m_fxaaLdrIntermediate` (not directly to backbuffer)
- After tonemap, copy intermediate → backbuffer

### 4. `runtime/engine/app/runtime.cpp`
- In `uz_render_frame()`: after RenderFrame() but before Present(), do:
  - Get LDR intermediate from SceneRenderer
  - Copy it to FrameReadback buffer (same cmdList)
- In `uz_encode_frame()`: only read from readback buffer + encode (no GPU commands)
- Remove `encodeCmdList` entirely

### 5. `runtime/engine/renderer/scene/scene_render_frame.cpp`
- Export the command list state so `uz_render_frame` can add readback commands before End+Execute
- OR: add a callback/hook that runs before End+Execute

## Key Constraint
The readback copy MUST be in the same command list as the render work, BEFORE End+Execute+Present. This eliminates all cross-command-list conflicts.
