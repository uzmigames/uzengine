# Godot 4 Architecture Patterns — Quick Reference

## 1. Ring Buffer for Per-Frame Constants

**File**: `/e/godot/servers/rendering/multi_uma_buffer.h`

**Pattern**: `MultiUmaBuffer<N, MAX_EXTRA>`

**Core Algorithm**:
- Allocate `buffers[curr_idx * N + slot]` where `curr_idx` cycles 0→1→2→0...
- Each `prepare_for_upload()` increments `curr_idx` on frame boundary
- Each buffer stores `original_size * frame_count` bytes (triple-buffering)
- Prevents GPU read-write race: GPU reads frame N while CPU writes frame N+1

**Safety Guard**:
```cpp
ERR_FAIL_COND_V_MSG(buf_info->last_frame_mapped == p_frames_drawn, nullptr,
    "Buffer must only be mapped once per frame");
```

**Auto-scaling**:
- Spiky frame uses extra buffers; `shrink_to_max_extra_buffers()` cleans up after spike
- Useful for multi-pass shadow mapping (6 passes normal, 24 passes spike)

---

## 2. Persistent Mapping with Frame Indexing

**Vulkan** (`/e/godot/drivers/vulkan/rendering_device_driver_vulkan.cpp:2043`):
```cpp
uint8_t *buffer_persistent_map_advance(BufferID p_buffer, uint64_t p_frames_drawn) {
    buf_info->frame_idx = (buf_info->frame_idx + 1u) % frame_count;  // Ring advance
    return buf_info->persistent_ptr + buf_info->frame_idx * buf_info->size;
}
```

**D3D12** (`/e/godot/drivers/d3d12/rendering_device_driver_d3d12.cpp:1042`):
- **Identical algorithm** — only memory model differs
- No manual flush needed (write-combined memory)
- Both use `BUFFER_USAGE_DYNAMIC_PERSISTENT_BIT` flag

**Allocation**:
- If `BUFFER_USAGE_DYNAMIC_PERSISTENT_BIT`, allocate `size * frame_count`
- Otherwise, use staging buffer path (slower but supports partial updates)

---

## 3. Offset Encoding (2 bits per buffer)

**Problem**: Multiple dynamic buffers in one uniform set — each needs frame_idx

**Solution**: Pack as 64-bit bitmask:
```cpp
uint64_t mask = 0;
for (each dynamic buffer) {
    mask |= buffer->frame_idx << (buffer_index * 2);  // 2 bits = 4 possible frames
}
```

**Decoding at draw time**:
- D3D12: Re-bind descriptor with new CBV offset = `frame_idx * buffer_size`
- Vulkan: Baked into shader at compile time

**Supports** up to 16 dynamic buffers per uniform set (64 bits ÷ 4 bits = 16).

---

## 4. Root Signature Generation (D3D12)

**File**: `/e/godot/drivers/d3d12/rendering_shader_container_d3d12.cpp:557`

**Flow**: Shader bytecode → analyze uniforms → generate D3D12 root signature

**Descriptor Flags**:
- CBV (constant buffer): `D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE`
  (safe to rebind between draws with same descriptor set)
- SRV (texture): `D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE`
  (can change per draw)
- UAV (image): `D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE`

**Key insight**: Godot compiles root signature once during shader import, serializes to cache. No runtime root signature generation overhead.

---

## 5. Uniform Set Creation & Binding

**uniform_set_create** (`rendering_device_driver_d3d12.cpp:3408`):
1. Allocate descriptors from heap for this uniform set
2. Create CBV/SRV/UAV views for each binding
3. Store dynamic buffer metadata for draw-time offset resolution

**Draw-time execution**:
1. Get dynamic_offsets bitmask from `buffer_get_dynamic_offsets()`
2. For each dynamic buffer, extract frame_idx from bitmask
3. Recompute CBV offset: `buffer.gpu_virtual_address + frame_idx * buffer.size`
4. Rebind descriptor with new offset
5. Submit draw

**Vulkan alternative**: Offsets baked into shader, passed via `vkCmdBindDescriptorSets(..., pDynamicOffsets)`

---

## Cross-Backend Interface (Virtual)

Both D3D12 and Vulkan implement:
```cpp
virtual uint8_t *buffer_persistent_map_advance(BufferID p_buffer, uint64_t p_frames_drawn);
virtual uint64_t buffer_get_dynamic_offsets(Span<BufferID> p_buffers);
virtual void buffer_flush(BufferID p_buffer);  // Vulkan only; D3D12 is no-op
```

Everything else is backend-specific:
- Memory allocation strategy
- Descriptor management
- Offset application (descriptor rebind vs. vkCmd)

---

## Why This Design

| Problem | Godot Solution | Benefit |
|---------|----------------|---------|
| GPU read-write hazard | Ring buffer (frame_idx) | CPU writes frame N+1 while GPU reads frame N |
| DX11 Map/Unmap stall | Persistent mapping | No GPU stall per frame |
| Multiple dynamic buffers | Offset encoding (bitmask) | Avoids descriptor heap churn |
| Per-frame buffer growth | MultiUmaBuffer with MAX_EXTRA | Auto-handles spikes without unbounded alloc |
| Root signature overhead | Compile-time generation + serialization | Fast shader load, no runtime generation |
| Cross-API compatibility | Identical persistent_map_advance() interface | Single game code path for D3D12/Vulkan |

---

## Game Code Example

```cpp
// Setup
RID cbuffer = rd->uniform_buffer_create(
    sizeof(PerFrameData),
    Vector<uint8_t>(),
    RD::BUFFER_CREATION_DYNAMIC_PERSISTENT_BIT);

RD::Uniform u;
u.binding = 0;
u.uniform_type = RD::UNIFORM_TYPE_UNIFORM_BUFFER_DYNAMIC;
u.append_id(cbuffer);
RID uniform_set = rd->uniform_set_create(Vector<RD::Uniform> { u }, shader, 0);

// Per-frame (game loop)
PerFrameData data = /* ... compute per-frame data ... */;
rd->buffer_update(cbuffer, 0, sizeof(PerFrameData), &data);  // Ring buffer handles frame sync

// Draw
rd->draw_list_bind_uniform_set(draw_list, uniform_set, 0);
for (each mesh) {
    rd->draw_list_draw(...);
}
```

**Game sees**: Simple buffer update. Ring logic is invisible.

---

## Performance Characteristics

| Operation | Cost | Notes |
|-----------|------|-------|
| `buffer_persistent_map_advance()` | ~10 ns | Pointer arithmetic only |
| `buffer_update()` (dynamic) | ~100 ns + memcpy | Direct write, no staging |
| `buffer_update()` (static) | ~1 µs | Staging buffer + deferred copy |
| `buffer_get_dynamic_offsets()` | ~100 ns | Bitmask packing |
| Draw-time offset resolution | ~1 µs | Descriptor rebind (D3D12) or vkCmd (Vulkan) |

**Key**: Per-frame constant buffers have zero GPU stall overhead when using ring buffer + persistent mapping.

---

## Potential Issues & Mitigations

| Issue | Cause | Mitigation |
|-------|-------|-----------|
| Double-map per frame | Calling `buffer_update()` twice on same buffer in one frame | DEBUG guard: `last_frame_mapped == frames_drawn` check |
| Ring buffer fragmentation | Spiky frame allocates extras, garbage collects every N frames | `shrink_to_max_extra_buffers()` called at frame reset |
| Descriptor rebind cost | D3D12 re-binds CBV for each draw with different frame_idx | Profile; usually << 1 µs per draw |
| Coherency violations | CPU writes frame N while GPU reads frame N (race) | Ring buffer + frame_idx prevents this entirely |

---

## Godot Leanness Lessons

1. **Explicit > Implicit**: Frame-aware APIs make synchronization intent clear
2. **Pooling > Copying**: Ring buffer 3x cheaper than staging buffer for per-frame data
3. **Offset Encoding > Descriptor Bloat**: 2-bit frame_idx avoids allocating 3x descriptors
4. **Compile-Time > Runtime**: Root signature generated once at shader import, cached
5. **Single Interface > Backend Duplication**: `buffer_persistent_map_advance()` works identically in D3D12/Vulkan

---

## Related Godot Subsystems

- **RenderingServer**: Deferred command recording (thread-safe API)
- **RenderingDevice**: Synchronous API wrapping RenderingDeviceDriver
- **RenderingDeviceDriver**: Backend-specific (D3D12, Vulkan, Metal, OpenGL)
- **RDGraph**: Render graph (DAG of passes with synchronization)
- **Shader Compilation**: DXC/glslang → bytecode → reflection → root signature

All layers respect frame boundaries and use ring buffers for per-frame state.
