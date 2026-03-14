# Godot to UzEngine: Constant Buffer Architecture Adaptation Guide

## Current UzEngine State

**From memory**:
- Runtime uses DX12 SM6.6 primary backend (DX11 SM5 fallback)
- Clear-screen demo achieves 6069 FPS on RTX 4090
- No per-frame constant buffers yet (only static geometry)

**Gap**: Need to add dynamic per-frame data (camera, lighting, animation) without stalling GPU.

---

## Recommended Implementation Path

### Phase 1: Basic Ring Buffer (Week 1)

**Goal**: Replace `buffer_update()` stalls with persistent CPU-visible ring buffer.

#### Step 1a: RHI Backend Interface (`engine/rhi/rhi_types.h`)

Add to existing buffer creation API:

```cpp
enum class BufferUsageFlags {
    NONE = 0,
    VERTEX = 1 << 0,
    INDEX = 1 << 1,
    UNIFORM = 1 << 2,
    STORAGE = 1 << 3,
    TRANSFER_SRC = 1 << 4,
    TRANSFER_DST = 1 << 5,
    DYNAMIC_PERSISTENT = 1 << 6,  // NEW: persistent CPU mapping, per-frame updates
};

struct BufferCreateInfo {
    uint64_t size_bytes;
    BufferUsageFlags usage;
    const void *initial_data = nullptr;
    uint32_t initial_data_size = 0;
};
```

**Why DYNAMIC_PERSISTENT flag**: Tells backend to allocate `size * frame_count` and manage ring internally.

#### Step 1b: Backend Implementation (`engine/rhi/backends/dx12/dx12_buffer.cpp`)

```cpp
struct DX12Buffer {
    ComPtr<ID3D12Resource> resource;
    D3D12_GPU_VIRTUAL_ADDRESS gpu_address;

    // For DYNAMIC_PERSISTENT buffers only
    uint8_t *persistent_ptr = nullptr;
    uint32_t frame_idx = 0;
    uint64_t last_frame_mapped = UINT64_MAX;
};

RHI::BufferHandle RHI::buffer_create(const BufferCreateInfo &info) {
    if (info.usage & BufferUsageFlags::DYNAMIC_PERSISTENT) {
        // Allocate size * NUM_FRAMES
        uint64_t actual_size = info.size_bytes * NUM_FRAMES;  // NUM_FRAMES = 2 or 3

        D3D12MA::ALLOCATION_DESC alloc_desc = {};
        alloc_desc.HeapType = D3D12_HEAP_TYPE_UPLOAD;  // Persistent CPU access

        ComPtr<ID3D12Resource> resource;
        ComPtr<D3D12MA::Allocation> allocation;
        allocator->CreateResource(&alloc_desc, &create_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                  nullptr, &allocation, IID_PPV_ARGS(&resource));

        // Map and store persistent pointer
        uint8_t *mapped_ptr = nullptr;
        resource->Map(0, nullptr, (void**)&mapped_ptr);

        DX12Buffer buf;
        buf.resource = resource;
        buf.gpu_address = resource->GetGPUVirtualAddress();
        buf.persistent_ptr = mapped_ptr;
        buf.frame_idx = 0;

        return buffers.insert(buf);
    } else {
        // Non-dynamic: allocate normally
        // ...
    }
}
```

#### Step 1c: Per-Frame Update API (`engine/rhi/rhi.h`)

```cpp
class RHI {
public:
    /// Write to dynamic persistent buffer for current frame.
    /// Safe to call multiple times per frame (only first call advances ring).
    void buffer_update_dynamic(BufferHandle buffer, uint64_t offset,
                               const void *data, uint64_t size);

    /// Advance ring buffer to next frame.
    /// Called once per frame before any drawing.
    void frame_begin();
};
```

**Implementation** (`engine/rhi/backends/dx12/dx12_buffer.cpp`):

```cpp
void RHI::buffer_update_dynamic(BufferHandle handle, uint64_t offset,
                                const void *data, uint64_t size) {
    DX12Buffer &buf = buffers[handle];

    UZ_ENSURE(!buf.usage.has_flag(BufferUsageFlags::DYNAMIC_PERSISTENT),
              "buffer_update_dynamic() requires DYNAMIC_PERSISTENT flag");

    UZ_ENSURE(buf.last_frame_mapped != frames_drawn,
              "Buffer mapped twice in same frame (race condition)");

    // Direct memcpy to persistent CPU mapping
    uint8_t *dst = buf.persistent_ptr + buf.frame_idx * buf.size_bytes + offset;
    memcpy(dst, data, size);
    buf.last_frame_mapped = frames_drawn;
}

void RHI::frame_begin() {
    // Advance all dynamic buffers to next frame slot
    for (auto &buf : buffers) {
        if (buf.usage & BufferUsageFlags::DYNAMIC_PERSISTENT) {
            buf.frame_idx = (buf.frame_idx + 1) % NUM_FRAMES;
            buf.last_frame_mapped = UINT64_MAX;
        }
    }
    frames_drawn++;
}
```

#### Step 1d: Descriptor Binding with Frame Offset

**At draw time**, compute CBV with dynamic offset:

```cpp
void RHI::draw_indexed(...) {
    // For each dynamic buffer in this draw's uniform set
    for (const DynamicBuffer &dyn_buf : uniform_set->dynamic_buffers) {
        uint64_t offset = dyn_buf.frame_idx * dyn_buf.size;  // Offset into ring

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
        cbv_desc.BufferLocation = dyn_buf.gpu_address + offset;
        cbv_desc.SizeInBytes = dyn_buf.size;

        device->CreateConstantBufferView(&cbv_desc, descriptor_heap_cpu_handle);
    }

    // ... rest of draw setup ...
}
```

**Testing** (Week 1 end):
- Create simple per-frame constant buffer with camera matrix
- Render quad with per-frame MVP updates
- Verify no GPU stall (should maintain ~6000+ FPS)
- Test double-map guard (should UZ_ENSURE fail)

---

### Phase 2: MultiUmaBuffer Template (Week 2)

**Goal**: Auto-scale ring buffer for spiky frames (shadow mapping, multiple passes).

#### Step 2a: Template Definition (`engine/rhi/dynamic_buffer.h`)

```cpp
/// Auto-growing ring buffer for per-frame data.
/// Useful for multi-pass rendering (shadows, reflections).
///
/// # Example
/// ```cpp
/// MultiDynamicBuffer<2> lights;  // 2 buffer slots per frame
/// lights.set_size(0, sizeof(DirectionalLights));
/// lights.set_size(1, sizeof(OmniLights));
///
/// for (int pass = 0; pass < num_passes; ++pass) {
///     lights.prepare_for_upload();
///     lights.upload(0, directional_data, dir_size);
///     lights.upload(1, omni_data, omni_size);
/// }
/// ```
template <uint32_t NUM_SLOTS, uint32_t MAX_EXTRA_FRAMES = 4>
class MultiDynamicBuffer {
    struct SlotInfo {
        uint64_t size = 0;
    };

    Vector<RHI::BufferHandle> ring;  // Flat: [slot0_frame0, slot1_frame0, slot0_frame1, ...]
    SlotInfo slot_info[NUM_SLOTS];
    uint32_t curr_idx = UINT32_MAX;
    uint64_t last_frame = UINT64_MAX;

public:
    /// Set size for slot (before first use).
    void set_size(uint32_t slot_idx, uint64_t size_bytes) {
        UZ_ENSURE(ring.empty(), "set_size() must be called before first prepare_for_upload()");
        slot_info[slot_idx].size = size_bytes;
    }

    /// Allocate new frame's buffers if needed.
    void prepare_for_upload() {
        uint64_t current_frame = RHI::get_frames_drawn();

        if (last_frame != current_frame) {
            curr_idx = 0;
            last_frame = current_frame;
        } else {
            ++curr_idx;  // Same frame, different pass
        }

        // Allocate more buffers if this pass exceeds capacity
        uint32_t needed = (curr_idx + 1) * NUM_SLOTS;
        while (ring.size() < needed && ring.size() < MAX_EXTRA_FRAMES * NUM_SLOTS) {
            for (uint32_t i = 0; i < NUM_SLOTS; ++i) {
                BufferCreateInfo info = {};
                info.size_bytes = slot_info[i].size;
                info.usage = BufferUsageFlags::UNIFORM | BufferUsageFlags::DYNAMIC_PERSISTENT;
                ring.push_back(RHI::buffer_create(info));
            }
        }
    }

    /// Get buffer for slot in current frame.
    RHI::BufferHandle get(uint32_t slot_idx) const {
        UZ_ENSURE(curr_idx != UINT32_MAX, "Call prepare_for_upload() first");
        return ring[curr_idx * NUM_SLOTS + slot_idx];
    }

    /// Upload data to slot in current frame.
    void upload(uint32_t slot_idx, const void *data, uint64_t size) {
        RHI::buffer_update_dynamic(get(slot_idx), 0, data, size);
    }

    void uninit() {
        for (RHI::BufferHandle buf : ring) {
            RHI::buffer_free(buf);
        }
        ring.clear();
    }
};
```

#### Step 2b: Usage in Renderer

```cpp
class Renderer {
    MultiDynamicBuffer<3> light_buffers;  // DirectionalLights, OmniLights, SpotLights

    void init() {
        light_buffers.set_size(0, sizeof(DirectionalLightData) * MAX_DIRECTIONAL);
        light_buffers.set_size(1, sizeof(OmniLightData) * MAX_OMNI);
        light_buffers.set_size(2, sizeof(SpotLightData) * MAX_SPOT);
    }

    void render_frame(const Scene &scene) {
        // Per-pass shadow rendering
        for (uint32_t pass = 0; pass < scene.shadow_passes.size(); ++pass) {
            light_buffers.prepare_for_upload();

            // Update all light data for this pass
            light_buffers.upload(0, scene.directional_lights.data(),
                                 scene.directional_lights.size() * sizeof(DirectionalLightData));
            light_buffers.upload(1, scene.omni_lights.data(),
                                 scene.omni_lights.size() * sizeof(OmniLightData));
            light_buffers.upload(2, scene.spot_lights.data(),
                                 scene.spot_lights.size() * sizeof(SpotLightData));

            // Create/reuse uniform set
            if (!light_sets[pass]) {
                light_sets[pass] = create_uniform_set_from_buffers(light_buffers, 0);
            }

            render_shadow_pass(pass, light_sets[pass]);
        }
    }

    void cleanup() {
        light_buffers.uninit();
    }
};
```

**Testing** (Week 2 end):
- Shadow map renderer with 6 cascades (normal) + spiky 24 passes (stress test)
- Verify `light_buffers.prepare_for_upload()` auto-allocates extra buffers
- Verify cleanup deallocates only up to MAX_EXTRA_FRAMES
- Profile: confirm zero GPU stall even with 24 buffer updates per frame

---

### Phase 3: Offset Encoding (Optional, Week 3)

**Goal**: Support many dynamic buffers without descriptor heap bloat.

Only needed if you have >10 dynamic buffers per uniform set. For most games, basic ring buffer is sufficient.

**Approach**:
- Collect frame_idx bitmask from all dynamic buffers in uniform set
- Pack into 64-bit mask (2 bits per buffer → 32 buffers max)
- At draw time, unpack and compute offsets

**Code** (`engine/rhi/backends/dx12/dx12_dynamic.cpp`):

```cpp
uint64_t compute_dynamic_offsets(const Vector<DynamicBuffer> &buffers) {
    uint64_t mask = 0;
    for (uint32_t i = 0; i < buffers.size(); ++i) {
        mask |= buffers[i].frame_idx << (i * 2);  // 2 bits per buffer
    }
    return mask;
}

void apply_dynamic_offsets(uint64_t offset_mask,
                           const Vector<DynamicBuffer> &buffers,
                           ID3D12GraphicsCommandList *cmd_list) {
    for (uint32_t i = 0; i < buffers.size(); ++i) {
        uint32_t frame_idx = (offset_mask >> (i * 2)) & 0x3;
        uint64_t offset = frame_idx * buffers[i].size;

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbv = {};
        cbv.BufferLocation = buffers[i].gpu_address + offset;
        cbv.SizeInBytes = buffers[i].size;

        device->CreateConstantBufferView(&cbv, descriptor_cpu_handle);
    }
}
```

---

## Integration Checklist

### Code Organization

```
engine/rhi/
├── rhi.h                           (public API)
├── rhi_types.h                     (BufferCreateInfo, etc.)
├── dynamic_buffer.h                (MultiDynamicBuffer<N, MAX_EXTRA>)
└── backends/
    ├── dx12/
    │   ├── dx12_buffer.cpp         (buffer_create, buffer_update_dynamic)
    │   ├── dx12_buffer.h           (DX12Buffer struct, frame_idx)
    │   └── dx12_dynamic.cpp        (offset encoding, NEW)
    └── vulkan/
        ├── vulkan_buffer.cpp       (same interface as dx12)
        └── vulkan_buffer.h
```

### Safety Assertions

Add to all frame-aware code:

```cpp
// At frame start
UZ_ENSURE(current_frame > last_frame, "Frame counter must advance");

// At buffer update
UZ_ENSURE(buffer.last_frame_mapped != frames_drawn,
          "Double-mapped buffer in frame {} (frame_idx={})",
          frames_drawn, buffer.frame_idx);

// At ring advance
UZ_ENSURE(buffer.frame_idx < NUM_FRAMES,
          "Ring buffer overflow (frame_idx={}, NUM_FRAMES={})",
          buffer.frame_idx, NUM_FRAMES);
```

### Performance Profiling

Use Tracy (already integrated) to measure:

```cpp
void RHI::buffer_update_dynamic(BufferHandle h, uint64_t offset,
                                const void *data, uint64_t size) {
    UZ_PROFILE_SCOPE_NAMED("RHI::buffer_update_dynamic");
    // ... memcpy ...
}

void RHI::frame_begin() {
    UZ_PROFILE_SCOPE_NAMED("RHI::frame_begin");
    // ... ring advancement ...
}
```

**Expected baselines**:
- `buffer_update_dynamic()`: < 100 ns (just memcpy)
- `frame_begin()`: < 1 µs (loop over buffers)
- Draw-time offset resolution: < 1 µs (descriptor rebind)

---

## Migration Path for Existing Code

### Current Demo Setup

```cpp
// OLD: Static vertex/index buffers only
struct Mesh {
    RHI::BufferHandle vertex_buffer;
    RHI::BufferHandle index_buffer;
};

void render_mesh(const Mesh &mesh, const Mat4 &mvp) {
    // MVP is on CPU — no way to update GPU per-frame
}
```

### With Ring Buffers

```cpp
// NEW: Per-frame MVP constant buffer
struct FrameData {
    Mat4 mvp;
    Vec4 camera_pos;
};

class Renderer {
    RHI::BufferHandle frame_constants;
    RHI::UniformSet uniform_set;

    void init() {
        BufferCreateInfo info = {};
        info.size_bytes = sizeof(FrameData);
        info.usage = BufferUsageFlags::UNIFORM | BufferUsageFlags::DYNAMIC_PERSISTENT;
        frame_constants = RHI::buffer_create(info);
    }

    void render_frame(const Scene &scene) {
        FrameData frame_data = {};
        frame_data.mvp = scene.camera.view_proj_matrix();
        frame_data.camera_pos = scene.camera.position;

        // Update constant buffer (ring buffer handles frame sync)
        RHI::buffer_update_dynamic(frame_constants, 0, &frame_data, sizeof(FrameData));

        // Draw all meshes with same (now per-frame) constants
        for (const Mesh &mesh : scene.meshes) {
            RHI::draw_indexed(mesh.index_count, 1, 0, 0, 0, uniform_set);
        }
    }
};
```

**Porting effort**: ~2 hours for demo (simple → frame-aware constants).

---

## Common Pitfalls

| Pitfall | Symptom | Fix |
|---------|---------|-----|
| Calling `buffer_update_dynamic()` twice per buffer per frame | `last_frame_mapped == frames_drawn` assertion | Merge updates into single call |
| Not calling `frame_begin()` | Ring buffer stuck at frame 0 | Add `RHI::frame_begin()` at start of render loop |
| Using static buffer flag on per-frame data | GPU stalls, low FPS | Use `DYNAMIC_PERSISTENT` flag |
| Overflow past ring buffer capacity | Crashes when exceed NUM_FRAMES | Increase NUM_FRAMES or use MultiDynamicBuffer<> |
| Descriptor heap exhaustion (many dynamic buffers) | Descriptor allocation fails | Implement offset encoding (Phase 3) |

---

## Reference: Godot Implementation Map

| Godot Code | UzEngine Equivalent |
|------------|-------------------|
| `MultiUmaBuffer<N, MAX>` | `MultiDynamicBuffer<N, MAX>` template |
| `RenderingDevice::buffer_update()` | `RHI::buffer_update_dynamic()` |
| `buffer_persistent_map_advance()` | `frame_idx` ring arithmetic in backend |
| `BUFFER_USAGE_DYNAMIC_PERSISTENT_BIT` | `BufferUsageFlags::DYNAMIC_PERSISTENT` |
| `buffer_get_dynamic_offsets()` | `compute_dynamic_offsets()` (Phase 3) |
| `uniform_set_create()` | `create_uniform_set()` with dynamic buffer tracking |
| Shader reflection → root signature | Already in DXC pipeline |
| `_draw_list_end()` descriptor rebinding | Apply in `draw_indexed()` |

---

## Success Criteria

**Phase 1 (Week 1)**:
- [ ] Ring buffer allocates `size * NUM_FRAMES`
- [ ] `buffer_update_dynamic()` writes to current frame slot
- [ ] `frame_begin()` advances ring
- [ ] Double-map guard fires (UZ_ENSURE)
- [ ] Demo maintains 6000+ FPS with per-frame camera updates

**Phase 2 (Week 2)**:
- [ ] `MultiDynamicBuffer<N>` allocates/deallocates dynamically
- [ ] Shadow map renderer uses for multi-pass light data
- [ ] Spike (24 passes) allocates extras, then shrinks
- [ ] `set_size()` before first `prepare_for_upload()` enforced

**Phase 3 (Week 3, optional)**:
- [ ] Offset encoding packs frame_idx into 64-bit bitmask
- [ ] Draw-time unpacking computes correct CBV offsets
- [ ] Supports 32 dynamic buffers per uniform set
- [ ] No extra descriptor heap allocations

---

## Conclusion

Godot's ring buffer pattern is straightforward and proven. Adopting it in UzEngine will:
1. Eliminate GPU stalls from per-frame constant buffer updates
2. Support multi-pass rendering (shadows, reflections) without allocation overhead
3. Keep game code simple (one-liner `buffer_update_dynamic()`)
4. Scale across DX12/Vulkan with identical interface

Expected outcome: **Seamless frame-rate scaling from 100s to 1000s of dynamic buffers per frame**, with zero GPU synchronization overhead.
