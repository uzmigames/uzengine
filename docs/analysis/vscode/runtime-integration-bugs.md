# VSCode Runtime Integration — Bug Analysis (Summary)

## Critical Bugs (direct cause of symptoms)

### BUG-1: drawCalls=0 is hardcoded
- `runtime.cpp:703` — stats.draw_calls never assigned

### BUG-2: Black screen — command list double-Begin (ROOT CAUSE)
- `render_thread.cpp:155-169` + `runtime.cpp:1078-1082`
- `uz_render_frame()` submits commands + signals fence
- `uz_encode_frame()` immediately calls `cmdList->Begin()` which resets the allocator WITHOUT waiting for GPU
- D3D12 command allocator hazard — silently corrupts swapchain with validation=false
- Fix: separate command list for encode path

### BUG-3: Resize crash 0xC0000409
- Concurrent access to frameReadback/h264Encoder between IPC thread and render thread

### BUG-4: getCodecExtradata returns null (called before first encode)
- sendCodecExtradata() called before startRenderLoop() — encoder hasn't produced extradata yet
- Fix: send extradata after first frame arrives

### BUG-5: Codec string mismatch
- scene-editor: `avc1.42001e`, viewport: `avc1.42E01F`

### BUG-6: Keyframe detector misses NAL types 7/8

### BUG-7: postMessage missing transferable list

### BUG-8: Log callback registered after viewport creation — all init errors invisible
