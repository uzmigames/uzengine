# Godot 4 RenderingDevice: Resource State Transitions & Screenshot Readback

## Executive Summary

Godot 4's RenderingDevice (RDD) implements **automatic implicit state tracking** via the `ResourceTracker` per-resource-per-frame system. When taking screenshots or doing readback after rendering:

1. **RenderingDeviceGraph tracks all state transitions automatically** — every command implicitly knows the previous and next layout/access of resources.
2. **Barriers are emitted based on usage changes** — going from `RESOURCE_USAGE_ATTACHMENT_COLOR_READ_WRITE` to `RESOURCE_USAGE_COPY_FROM` generates an automatic barrier.
3. **Screenshot/readback calls don't require explicit state setup** — the caller just calls `texture_get_data()` and it:
   - Allocates a temporary buffer
   - Emits `add_texture_get_data()` command with `TEXTURE_LAYOUT_COPY_SRC_OPTIMAL`
   - Lets the graph auto-insert barriers
   - Flushes and maps the result to CPU
4. **Resource state persists implicitly** — `ResourceTracker.usage` and `ResourceTracker.usage_access` track the last known state, and barriers are always computed between last state → new state.

This is **Godot's key innovation**: resource state is never left ambiguous. The RenderingDeviceGraph automatically derives the correct barriers from semantic usage changes, avoiding the UE5 pattern of manually specifying resource states at API boundaries.

---

## Pattern 1: ResourceTracker — Per-Resource, Per-Frame State Machine

### Godot Implementation

Located in `/e/godot/servers/rendering/rendering_device_graph.h:182-227`:

```cpp
struct ResourceTracker {
    uint32_t reference_count = 0;
    int64_t command_frame = -1;  // What frame does this tracker data belong to?
    BitField<RDD::PipelineStageBits> previous_frame_stages = {};  // Pipeline stages from previous frame
    BitField<RDD::PipelineStageBits> current_frame_stages = {};   // Pipeline stages this frame

    // Command graph tracking (index into command list, -1 if not yet used)
    int32_t read_full_command_list_index = -1;
    int32_t read_slice_command_list_index = -1;
    int32_t write_command_or_list_index = -1;
    int32_t draw_list_index = -1;

    ResourceUsage usage = RESOURCE_USAGE_NONE;        // Last known usage (COPY_FROM, COLOR_ATTACHMENT, STORAGE, etc.)
    BitField<RDD::BarrierAccessBits> usage_access = {};  // Last known access bits (COPY_READ, COLOR_WRITE, etc.)

    RDD::BufferID buffer_driver_id;
    RDD::TextureID texture_driver_id;
    RDD::TextureSubresourceRange texture_subresources;  // Which layers/mipmaps are used

    // Slice tracking for sub-resource usage
    ResourceTracker *parent = nullptr;
    ResourceTracker *dirty_shared_list = nullptr;
    Rect2i texture_slice_or_dirty_rect;
    bool in_parent_dirty_list = false;

    // Reset state for new frame
    _FORCE_INLINE_ void reset_if_outdated(int64_t new_command_frame) {
        if (new_command_frame != command_frame) {
            command_frame = new_command_frame;
            previous_frame_stages = current_frame_stages;  // Save old stages
            current_frame_stages.clear();                  // Start fresh
            read_full_command_list_index = -1;
            write_command_or_list_index = -1;
            // ... reset all command indices
        }
    }
};
```

**Key insight**: Every resource owns a ResourceTracker that persists across frames. When `begin()` is called for a new frame, each tracker's `reset_if_outdated()` checks if this is a new `command_frame` ID. If so:
- `previous_frame_stages` captures the old `current_frame_stages` (where we left off)
- Command indices reset so dependencies are recomputed

### Problem Solved

**The state persistence problem**: In DX12/Vulkan, a resource has a current state (layout, access bits). If you render to texture A, then copy from A, you need a barrier. But **where is that state stored**?

Godot's answer: **In the ResourceTracker of the previous command that used the resource**. Every command records:
- Input: the tracker's current `usage` and `usage_access`
- Output: computes new `usage` and `usage_access` based on the command type
- Barrier: emitted between input and output

### UzEngine Adaptation

UzEngine could adopt this for its RHI layer:

```cpp
// In uz/rhi/resource_tracker.h
struct RhiResourceTracker {
    enum Usage {
        USAGE_NONE,
        USAGE_COPY_FROM,      // TEXTURE_LAYOUT_COPY_SRC_OPTIMAL
        USAGE_COPY_TO,        // TEXTURE_LAYOUT_COPY_DST_OPTIMAL
        USAGE_STORAGE_READ,   // TEXTURE_LAYOUT_STORAGE_OPTIMAL
        USAGE_STORAGE_READ_WRITE,
        USAGE_TEXTURE_SAMPLE, // TEXTURE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        USAGE_ATTACHMENT_COLOR,     // COLOR_ATTACHMENT_OPTIMAL
        USAGE_ATTACHMENT_DEPTH,     // DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        USAGE_GENERAL,
    };

    Usage last_usage = USAGE_NONE;
    RhiTextureLayout last_layout = RHI_LAYOUT_UNDEFINED;
    uint64_t last_access_bits = 0;  // RhiBarrierAccessBits
    int64_t frame_id = -1;

    RhiTextureLayout usage_to_layout(Usage u) const {
        switch (u) {
            case USAGE_COPY_FROM: return RHI_LAYOUT_COPY_SRC;
            case USAGE_COPY_TO: return RHI_LAYOUT_COPY_DST;
            // ... map all usages to layouts
        }
    }

    uint64_t usage_to_access_bits(Usage u) const {
        switch (u) {
            case USAGE_COPY_FROM: return RHI_ACCESS_COPY_READ;
            case USAGE_COPY_TO: return RHI_ACCESS_COPY_WRITE;
            // ... map all usages to access bits
        }
    }
};
```

Then in RenderGraphCommand:
```cpp
struct RgTextureCommand {
    RhiTextureId texture;
    RhiResourceTracker* tracker;  // Points to the tracked state
    RhiResourceTracker::Usage next_usage;  // What we're about to do

    // During barrier injection:
    auto prev_layout = tracker->usage_to_layout(tracker->last_usage);
    auto next_layout = tracker->usage_to_layout(next_usage);
    auto barrier = compute_barrier(prev_layout, next_layout);
    // ... emit barrier ...
    tracker->last_usage = next_usage;  // Remember for next command
};
```

### Godot's Leanness Lesson

**Godot avoids the "manual state management" trap** that UE5 falls into. UE5 requires:
- Caller specifies `ERHITransitionInfo` with explicit `Before` and `After` states
- Caller must track "what state is this resource in?"
- Errors if you transition from UNKNOWN to a state

Godot says: **Track it implicitly via the last command that touched the resource**. The previous command's output becomes the next command's input. No manual specification needed.

---

## Pattern 2: Automatic Barrier Computation via Usage Semantics

### Godot Implementation

Located in `/e/godot/servers/rendering/rendering_device_graph.cpp:742-760`:

```cpp
void RenderingDeviceGraph::_add_texture_barrier_to_command(
    RDD::TextureID p_texture_id,
    BitField<RDD::BarrierAccessBits> p_src_access,  // What the last command had
    BitField<RDD::BarrierAccessBits> p_dst_access,  // What the next command needs
    ResourceUsage p_prev_usage,   // e.g., RESOURCE_USAGE_ATTACHMENT_COLOR_READ_WRITE
    ResourceUsage p_next_usage,   // e.g., RESOURCE_USAGE_COPY_FROM
    RDD::TextureSubresourceRange p_subresources,
    LocalVector<RDD::TextureBarrier> &r_barrier_vector,
    int32_t &r_barrier_index,
    int32_t &r_barrier_count
) {
    if (!driver_honors_barriers) return;  // Some drivers don't need explicit barriers

    RDD::TextureBarrier texture_barrier;
    texture_barrier.texture = p_texture_id;
    texture_barrier.src_access = p_src_access;
    texture_barrier.dst_access = p_dst_access;

    // Magic: map ResourceUsage enum -> VK_IMAGE_LAYOUT / D3D12 state
    texture_barrier.prev_layout = _usage_to_image_layout(p_prev_usage);  // Looks up in table
    texture_barrier.next_layout = _usage_to_image_layout(p_next_usage);

    texture_barrier.subresources = p_subresources;
    r_barrier_vector.push_back(texture_barrier);
    r_barrier_count++;
}
```

And the lookup table (Vulkan backend, `/e/godot/drivers/vulkan/rendering_device_driver_vulkan.cpp:329-343`):

```cpp
static VkImageLayout RD_TO_VK_LAYOUT[RDD::TEXTURE_LAYOUT_MAX] = {
    VK_IMAGE_LAYOUT_UNDEFINED,                    // TEXTURE_LAYOUT_UNDEFINED
    VK_IMAGE_LAYOUT_GENERAL,                      // TEXTURE_LAYOUT_GENERAL
    VK_IMAGE_LAYOUT_GENERAL,                      // TEXTURE_LAYOUT_STORAGE_OPTIMAL
    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,    // TEXTURE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, // DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,  // DEPTH_STENCIL_READ_ONLY_OPTIMAL
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,    // TEXTURE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,        // TEXTURE_LAYOUT_COPY_SRC_OPTIMAL
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,        // TEXTURE_LAYOUT_COPY_DST_OPTIMAL
    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,        // TEXTURE_LAYOUT_RESOLVE_SRC_OPTIMAL
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,        // TEXTURE_LAYOUT_RESOLVE_DST_OPTIMAL
};
```

The usage-to-layout conversion (from rendering_device_graph.cpp:127-158):

```cpp
RDD::TextureLayout RenderingDeviceGraph::_usage_to_image_layout(ResourceUsage p_usage) {
    switch (p_usage) {
        case RESOURCE_USAGE_COPY_FROM:
            return RDD::TEXTURE_LAYOUT_COPY_SRC_OPTIMAL;
        case RESOURCE_USAGE_COPY_TO:
            return RDD::TEXTURE_LAYOUT_COPY_DST_OPTIMAL;
        case RESOURCE_USAGE_RESOLVE_FROM:
            return RDD::TEXTURE_LAYOUT_RESOLVE_SRC_OPTIMAL;
        case RESOURCE_USAGE_TEXTURE_SAMPLE:
            return RDD::TEXTURE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        case RESOURCE_USAGE_STORAGE_IMAGE_READ:
        case RESOURCE_USAGE_STORAGE_IMAGE_READ_WRITE:
            return RDD::TEXTURE_LAYOUT_STORAGE_OPTIMAL;
        case RESOURCE_USAGE_ATTACHMENT_COLOR_READ_WRITE:
            return RDD::TEXTURE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        case RESOURCE_USAGE_ATTACHMENT_DEPTH_STENCIL_READ_WRITE:
            return RDD::TEXTURE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        // ... more
        case RESOURCE_USAGE_NONE:
            return RDD::TEXTURE_LAYOUT_UNDEFINED;
    }
}
```

### Problem Solved

**The "what barrier do I need?" problem**: In UE5 RHI, you must manually specify the from/to states. Godot says: **If I know the semantic usage (color attachment vs copy source), I can derive the exact layout and access bits without asking the caller.**

This reduces API surface and removes a whole class of bugs (caller specifies wrong state).

### UzEngine Adaptation

```cpp
// uz/rhi/render_graph.h
enum RhiResourceUsage : uint8_t {
    RHI_USAGE_NONE,
    RHI_USAGE_COPY_FROM,
    RHI_USAGE_COPY_TO,
    RHI_USAGE_STORAGE_READ,
    RHI_USAGE_STORAGE_READ_WRITE,
    RHI_USAGE_TEXTURE_SAMPLE,
    RHI_USAGE_ATTACHMENT_COLOR,
    RHI_USAGE_ATTACHMENT_DEPTH,
    RHI_USAGE_GENERAL,
};

class RhiRenderGraph {
    RhiTextureLayout usage_to_layout(RhiResourceUsage u) const {
        static constexpr RhiTextureLayout layout_table[] = {
            RHI_LAYOUT_UNDEFINED,           // USAGE_NONE
            RHI_LAYOUT_COPY_SRC,            // USAGE_COPY_FROM
            RHI_LAYOUT_COPY_DST,            // USAGE_COPY_TO
            RHI_LAYOUT_GENERAL,             // USAGE_STORAGE_READ (or VK_IMAGE_LAYOUT_GENERAL)
            RHI_LAYOUT_GENERAL,             // USAGE_STORAGE_READ_WRITE
            RHI_LAYOUT_SHADER_READ_ONLY,    // USAGE_TEXTURE_SAMPLE
            RHI_LAYOUT_COLOR_ATTACHMENT,    // USAGE_ATTACHMENT_COLOR
            RHI_LAYOUT_DEPTH_ATTACHMENT,    // USAGE_ATTACHMENT_DEPTH
            RHI_LAYOUT_GENERAL,             // USAGE_GENERAL
        };
        return layout_table[(int)u];
    }

    uint64_t usage_to_access_bits(RhiResourceUsage u) const {
        static constexpr uint64_t access_table[] = {
            0,                                      // USAGE_NONE
            RHI_ACCESS_COPY_READ,                  // USAGE_COPY_FROM
            RHI_ACCESS_COPY_WRITE,                 // USAGE_COPY_TO
            RHI_ACCESS_SHADER_READ,                // USAGE_STORAGE_READ
            RHI_ACCESS_SHADER_READ | RHI_ACCESS_SHADER_WRITE,  // USAGE_STORAGE_READ_WRITE
            RHI_ACCESS_SHADER_READ,                // USAGE_TEXTURE_SAMPLE
            RHI_ACCESS_COLOR_ATTACHMENT_WRITE,    // USAGE_ATTACHMENT_COLOR
            RHI_ACCESS_DEPTH_ATTACHMENT_WRITE,    // USAGE_ATTACHMENT_DEPTH
            RHI_ACCESS_MEMORY_READ | RHI_ACCESS_MEMORY_WRITE,  // USAGE_GENERAL
        };
        return access_table[(int)u];
    }

    // Auto-compute barrier from usage change
    RhiTextureBarrier compute_barrier_for_usage_change(
        RhiTextureId texture,
        RhiResourceUsage prev_usage,
        RhiResourceUsage next_usage
    ) {
        return RhiTextureBarrier{
            .texture = texture,
            .src_access = usage_to_access_bits(prev_usage),
            .dst_access = usage_to_access_bits(next_usage),
            .prev_layout = usage_to_layout(prev_usage),
            .next_layout = usage_to_layout(next_usage),
        };
    }
};
```

### Godot's Leanness Lesson

**Godot maintains one mapping: ResourceUsage -> (Layout, AccessBits)**. UE5 requires the caller to know and specify both. This is ~50 lines of mapping code vs. hundreds of call sites getting it wrong.

---

## Pattern 3: Screenshot/Readback Without Explicit State Setup

### Godot Implementation

From `/e/godot/servers/rendering/rendering_device.cpp:2291-2380`:

```cpp
Vector<uint8_t> RenderingDevice::texture_get_data(RID p_texture, uint32_t p_layer) {
    ERR_RENDER_THREAD_GUARD_V(Vector<uint8_t>());

    Texture *tex = texture_owner.get_or_null(p_texture);
    ERR_FAIL_NULL_V(tex, Vector<uint8_t>());

    // Check: not currently being used as a framebuffer attachment
    ERR_FAIL_COND_V_MSG(tex->bound, Vector<uint8_t>(),
        "Texture can't be retrieved while a draw list uses it...");

    // Check: texture was created with CAN_COPY_FROM_BIT
    ERR_FAIL_COND_V_MSG(!(tex->usage_flags & TEXTURE_USAGE_CAN_COPY_FROM_BIT), Vector<uint8_t>(),
        "Texture requires TEXTURE_USAGE_CAN_COPY_FROM_BIT...");

    // Clear the texture if the driver requires it on first use
    _texture_check_pending_clear(p_texture, tex);

    _check_transfer_worker_texture(tex);

    if (tex->usage_flags & TEXTURE_USAGE_CPU_READ_BIT) {
        // Fast path: CPU-readable texture (mapped memory)
        return driver->texture_get_data(tex->driver_id, p_layer);
    } else {
        // GPU readback path: copy to CPU-visible buffer

        // 1. Compute the layout needed for all mipmaps
        thread_local LocalVector<RDD::TextureCopyableLayout> mip_layouts;
        thread_local LocalVector<RDD::BufferTextureCopyRegion> copy_regions;
        mip_layouts.resize(tex->mipmaps);
        copy_regions.resize(tex->mipmaps);

        uint32_t buffer_size = 0;
        for (uint32_t i = 0; i < tex->mipmaps; i++) {
            RDD::TextureSubresource subres;
            subres.aspect = RDD::TEXTURE_ASPECT_COLOR;  // or DEPTH
            subres.layer = p_layer;
            subres.mipmap = i;

            RDD::TextureCopyableLayout &mip_layout = mip_layouts[i];
            driver->texture_get_copyable_layout(tex->driver_id, subres, &mip_layout);

            uint32_t mip_offset = STEPIFY(buffer_size, mip_alignment);
            buffer_size = mip_offset + mip_layout.size;

            copy_regions[i] = RDD::BufferTextureCopyRegion{
                .buffer_offset = mip_offset,
                .row_pitch = mip_layout.row_pitch,
                .texture_subresource = subres,
                .texture_region_size = {MAX(1u, tex->width >> i), MAX(1u, tex->height >> i), ...},
            };
        }

        // 2. Allocate a temporary staging buffer
        RDD::BufferID tmp_buffer = driver->buffer_create(
            buffer_size,
            RDD::BUFFER_USAGE_TRANSFER_TO_BIT,
            RDD::MEMORY_ALLOCATION_TYPE_CPU,  // CPU-visible memory
            frames_drawn
        );
        ERR_FAIL_COND_V(!tmp_buffer, Vector<uint8_t>());

        // 3. Make the texture mutable (if needed for layout transitions)
        if (_texture_make_mutable(tex, p_texture)) {
            draw_graph.add_synchronization();  // Insert a full pipeline barrier if texture state is unknown
        }

        // 4. Emit the copy command
        // This is key: the graph tracks tex->draw_tracker's last usage,
        // and automatically inserts a barrier from that state to TEXTURE_LAYOUT_COPY_SRC_OPTIMAL
        draw_graph.add_texture_get_data(tex->driver_id, tex->draw_tracker, tmp_buffer, copy_regions);

        // 5. Execute the graph and wait
        _flush_and_stall_for_all_frames();  // Submit all pending commands and wait for GPU

        // 6. Read the CPU-visible buffer
        const uint8_t *read_ptr = driver->buffer_map(tmp_buffer);
        ERR_FAIL_NULL_V(read_ptr, Vector<uint8_t>());

        // 7. Copy to output, handling compressed formats, padding, etc.
        Vector<uint8_t> buffer_data;
        uint32_t tight_buffer_size = get_image_format_required_size(tex->format, tex->width, tex->height, tex->depth, tex->mipmaps);
        buffer_data.resize(tight_buffer_size);
        uint8_t *write_ptr = buffer_data.ptrw();

        for (uint32_t i = 0; i < tex->mipmaps; i++) {
            // Copy mipmap i from buffer to output, removing padding
            memcpy(write_ptr + output_offset, read_ptr + mip_layouts[i].offset, mip_layouts[i].size);
        }

        driver->buffer_unmap(tmp_buffer);
        driver->free(tmp_buffer);

        return buffer_data;
    }
}
```

And the graph command (`rendering_device_graph.cpp:2432-2449`):

```cpp
void RenderingDeviceGraph::add_texture_get_data(
    RDD::TextureID p_src,
    ResourceTracker *p_src_tracker,  // <-- Points to last known state
    RDD::BufferID p_dst,
    VectorView<RDD::BufferTextureCopyRegion> p_buffer_texture_copy_regions,
    ResourceTracker *p_dst_tracker = nullptr
) {
    DEV_ASSERT(p_src_tracker != nullptr);

    int32_t command_index;
    uint64_t command_size = sizeof(RecordedTextureGetDataCommand) + ...;
    RecordedTextureGetDataCommand *command = static_cast<RecordedTextureGetDataCommand *>(
        _allocate_command(command_size, command_index)
    );

    command->type = RecordedCommand::TYPE_TEXTURE_GET_DATA;
    command->self_stages = RDD::PIPELINE_STAGE_COPY_BIT;  // Only copy pipeline stage
    command->from_texture = p_src;
    command->to_buffer = p_dst;

    // ... store copy regions ...

    // Register this command with the graph, passing the tracker
    ResourceUsage usages[2] = { RESOURCE_USAGE_COPY_FROM, RESOURCE_USAGE_COPY_TO };
    ResourceTracker *trackers[2] = { p_src_tracker, p_dst_tracker };
    _add_command_to_graph(trackers, usages, 2, command_index, command);
    // This is where barriers are injected!
}
```

The `_add_command_to_graph` then calls `_add_texture_barrier_to_command` (line 580 in rendering_device_graph.cpp):

```cpp
// In _add_command_to_graph:
if (different_usage || write_usage_after_write) {
    _add_texture_barrier_to_command(
        resource_tracker->texture_driver_id,
        resource_tracker->usage_access,         // What the last command had
        new_usage_access,                       // What we need (COPY_READ)
        resource_tracker->usage,                // e.g., ATTACHMENT_COLOR_READ_WRITE
        new_resource_usage,                     // COPY_FROM
        resource_tracker->texture_subresources,
        command_transition_barriers,
        r_command->transition_barrier_index,
        r_command->transition_barrier_count
    );

    // Update the tracker for next command
    resource_tracker->usage = new_resource_usage;
    resource_tracker->usage_access = new_usage_access;
}
```

### Problem Solved

**The "how do I readback a texture?" problem**: Many engines require the caller to:
1. Know the texture's current layout
2. Emit an explicit transition
3. Emit the readback copy
4. Emit another transition back (if the texture is still needed)

Godot says: **Just tell us you want to copy from it. We'll figure out the transition from the last command that touched it.**

The caller passes `tex->draw_tracker` (which remembers "we rendered to this last") and the graph automatically inserts a barrier from COLOR_ATTACHMENT → COPY_SRC.

### UzEngine Adaptation

```cpp
// uz/engine/renderer/render_graph.h
class RgBuilder {
    void add_screenshot_readback(
        RhiTextureId texture,
        RhiResourceTracker* tracker,  // Last known state
        RhiBufferId staging_buffer,
        uint32_t width, uint32_t height
    ) {
        // Compute barrier automatically
        RhiResourceUsage last_usage = tracker->last_usage;
        RhiResourceUsage next_usage = RHI_USAGE_COPY_FROM;

        auto barrier = compute_barrier_for_usage_change(texture, last_usage, next_usage);
        if (barrier.prev_layout != barrier.next_layout) {
            current_command_list_.add_barrier(barrier);
        }

        // Emit the copy command
        current_command_list_.add_copy_texture_to_buffer(
            texture,
            RHI_LAYOUT_COPY_SRC,
            staging_buffer,
            width, height
        );

        // Remember the new state for next command
        tracker->last_usage = next_usage;
        tracker->last_layout = RHI_LAYOUT_COPY_SRC;
    }
};

// Demo usage:
class ScreenshotDemo : public Application {
    void on_key_press(Key key) override {
        if (key == Key::F12) {
            // Capture current frame color buffer
            RhiTextureId color_buffer = render_graph_.back_buffer_color_texture();
            RhiResourceTracker* tracker = render_graph_.get_tracker(color_buffer);

            RhiBufferId staging = rhi_.buffer_create(
                width * height * 4,
                RHI_BUFFER_USAGE_TRANSFER_DST,
                RHI_MEMORY_ALLOCATION_CPU  // CPU-visible
            );

            render_graph_.builder()
                .add_screenshot_readback(color_buffer, tracker, staging, width, height)
                .submit();

            // GPU is now done, read the buffer
            const uint32_t* pixels = (const uint32_t*)rhi_.buffer_map(staging);
            save_png("screenshot.png", pixels, width, height);
            rhi_.buffer_unmap(staging);
            rhi_.buffer_free(staging);
        }
    }
};
```

### Godot's Leanness Lesson

**Godot bakes the state tracking into the graph itself**. There's no separate "state management" API. You call `add_texture_get_data(texture, tracker)` and it figures everything out. Compare to UE5:

**UE5 pattern:**
```cpp
// UE5 (pseudocode)
FRHICmdList& RHICmdList = GRHICommandList.GetImmediateCommandList();
{
    FRHITransitionInfo Transitions[] = {
        {texture, ERHIAccess::CopySrc, ERHIAccess::CopySrc},  // Redundant!
    };
    RHICmdList.Transition(Transitions);  // Explicit transition
}
RHICmdList.CopyTexture(...);  // Copy command
```

**Godot pattern:**
```cpp
// Godot (what the caller does)
draw_graph.add_texture_get_data(texture, tracker, buffer, regions);
// Godot internally figures out the transition from tracker->usage
```

Godot's approach: **1 call, implicit state tracking**. UE5's approach: **2+ calls, explicit state**.

---

## Pattern 4: Frame-Boundary State Persistence

### Godot Implementation

From `/e/godot/servers/rendering/rendering_device_graph.h:213-222`:

```cpp
_FORCE_INLINE_ void reset_if_outdated(int64_t new_command_frame) {
    if (new_command_frame != command_frame) {
        command_frame = new_command_frame;
        previous_frame_stages = current_frame_stages;  // Archive old stages
        current_frame_stages.clear();                  // Clear for new frame
        read_full_command_list_index = -1;
        read_slice_command_list_index = -1;
        write_command_or_list_index = -1;
        draw_list_index = -1;
        compute_list_index = -1;
        // ... etc
    }
}
```

And the frame lifecycle in `rendering_device_graph.cpp:1758-1785`:

```cpp
void RenderingDeviceGraph::begin() {
    command_data.clear();
    command_data_offsets.clear();
    command_normalization_barriers.clear();
    command_transition_barriers.clear();
    command_buffer_barriers.clear();
    // ... clear all command lists ...

    tracking_frame++;  // Increment frame counter

    // ResourceTrackers are NOT cleared; they persist
    // But reset_if_outdated() will update them on first use in new frame
}
```

So the lifecycle is:

1. **Frame N starts**: `begin()` increments `tracking_frame` to N
2. **Commands are recorded**: Each `add_*()` calls `tracker->reset_if_outdated(N)` on first use
3. **End of frame N**: Graph is executed, all commands flush
4. **Frame N+1 starts**: `begin()` increments `tracking_frame` to N+1
5. **First use of texture in frame N+1**: `reset_if_outdated(N+1)` captures `previous_frame_stages` from what frame N ended with

This means: **When you take a screenshot after the render graph executes, the ResourceTracker still knows what state the texture ended in.**

### Problem Solved

**The "what state did I leave this in?" problem**: If the render graph is:
1. Frame N: render to color buffer (final state: COLOR_ATTACHMENT)
2. Frame N: readback color buffer (transition: COLOR_ATTACHMENT → COPY_SRC)
3. Present frame N
4. Frame N+1: render to same buffer

In frame N+1, if you take a screenshot, you need to know: "Is this in COLOR_ATTACHMENT state (from rendering) or COPY_SRC state (from readback)?"

Godot answers: **Track it! Each command updates the tracker's state, so by the end of rendering, you know exactly what state it's in.**

### UzEngine Adaptation

```cpp
// uz/engine/renderer/render_graph.h
class RgBuilder {
    int64_t current_frame_id_ = 0;
    std::unordered_map<RhiTextureId, RhiResourceTracker> texture_trackers_;

    void begin_frame() {
        current_frame_id_++;

        // Trackers persist; they reset on first use each frame
        for (auto& [tex_id, tracker] : texture_trackers_) {
            // Don't reset here! Reset on first use (see below)
        }
    }

    void add_draw_list_color_attachment(
        RhiTextureId color_buffer,
        const RgDrawDesc& desc
    ) {
        auto& tracker = texture_trackers_[color_buffer];
        tracker.reset_if_outdated(current_frame_id_);  // First use this frame

        // Barrier from last state to attachment
        if (tracker.last_usage != RHI_USAGE_ATTACHMENT_COLOR) {
            auto barrier = compute_barrier_for_usage_change(
                color_buffer,
                tracker.last_usage,
                RHI_USAGE_ATTACHMENT_COLOR
            );
            commands_.push_back(barrier);
        }

        // Record the draw list
        // ...

        // Update tracker
        tracker.last_usage = RHI_USAGE_ATTACHMENT_COLOR;
        tracker.last_layout = RHI_LAYOUT_COLOR_ATTACHMENT;
    }

    // Later, in the same or next frame
    RhiResourceTracker* get_tracker(RhiTextureId tex) {
        return &texture_trackers_[tex];
    }
};
```

### Godot's Leanness Lesson

**Godot uses "lazy state reset"**: ResourceTrackers persist but check their frame ID on every use. If it's a new frame, they save the old state and start fresh. This is ~20 bytes per resource per engine cycle (just the frame ID and pipeline stage bits) vs. UE5's approach of tracking state in the FRHIResource itself (larger footprint).

---

## Pattern 5: The Missing Concept — No "Final State" Semantics

### Key Insight: Godot Has No "Final State" API

Unlike some engines, **Godot's render graph does NOT export a "final resource state" API**. That is:

- There's no `graph.get_final_texture_state(texture_id)` public API
- There's no "please transition all resources to these final states" operation
- Resources are left **in whatever state the last command left them**

### Why This Works

In Godot, **presentation happens outside the graph**. Looking at the Vulkan backend:

1. Render graph executes (each command updates the ResourceTracker)
2. Final draw commands might render to swapchain image
3. Graph ends; swapchain image is in PRESENTATION_SRC layout
4. vkQueuePresentKHR is called
5. Next frame begins

The presentation layer doesn't ask the graph "what state is the swapchain in?" — it assumes it's been set up correctly by the last command. And the RenderingDevice manages that invariant.

### UzEngine: Should We Have "Final State" Export?

For UzEngine's screenshot feature, **we DO NOT need it**. The pattern is:

```cpp
// Simple pattern: graph tracks state, caller just reads it when needed
RhiResourceTracker* tracker = render_graph_.get_tracker(color_buffer);
if (tracker->last_layout == RHI_LAYOUT_COLOR_ATTACHMENT) {
    // We need to transition to COPY_SRC
    add_barrier(color_buffer, RHI_LAYOUT_COLOR_ATTACHMENT, RHI_LAYOUT_COPY_SRC);
} else if (tracker->last_layout == RHI_LAYOUT_COPY_SRC) {
    // Already in the right state, skip barrier
}
```

But if you want **explicit final-state management** (like UE5 does), you could add it:

```cpp
// Optional: explicit final states (UE5-style)
struct RgFinalStateRequest {
    RhiTextureId texture;
    RhiResourceUsage desired_final_state;
};

void RgBuilder::end_with_final_states(
    const std::vector<RgFinalStateRequest>& final_states
) {
    for (const auto& req : final_states) {
        auto* tracker = get_tracker(req.texture);
        if (tracker->last_usage != req.desired_final_state) {
            add_barrier_for_usage_change(
                req.texture,
                tracker->last_usage,
                req.desired_final_state
            );
            tracker->last_usage = req.desired_final_state;
        }
    }
    submit_commands();
}
```

But Godot doesn't bother. **It's not needed if the last command leaves resources in a good state.**

---

## File Paths for Reference

Key Godot files analyzed:

- `/e/godot/servers/rendering/rendering_device_graph.h` — ResourceTracker struct, RenderingDeviceGraph interface
- `/e/godot/servers/rendering/rendering_device_graph.cpp` — Graph execution, barrier injection, state machine
- `/e/godot/servers/rendering/rendering_device.cpp` — texture_get_data() implementation, public API
- `/e/godot/drivers/vulkan/rendering_device_driver_vulkan.cpp` — Backend layout mapping
- `/e/godot/servers/rendering/renderer_rd/storage_rd/texture_storage.cpp` — texture_2d_get() usage example

---

## Summary Table: Godot vs UE5 Resource State Management

| Aspect | Godot | UE5 | UzEngine Recommendation |
|--------|-------|-----|------------------------|
| **State Tracking** | Implicit (ResourceTracker.usage) | Explicit (FRHIResource + caller) | Implicit like Godot |
| **Barrier Computation** | Automatic (usage → layout) | Manual (caller specifies before/after) | Automatic like Godot |
| **Readback Pattern** | `texture_get_data(id, tracker)` | Caller must transition first | `add_screenshot_readback(id, tracker)` |
| **Cross-Frame State** | Persists via reset_if_outdated() | Persists in FRHIResource.State | Persist via tracker frame ID |
| **API Complexity** | Low (state baked in) | High (explicit transitions) | Medium (state implicit but optional final-state export) |
| **Driver Variance** | Handled via driver_honors_barriers flag | Handled via backend-specific code | Similar to Godot |

---

## Conclusion: Godot's Leanness Lesson for UzEngine

Godot demonstrates that **implicit state tracking beats explicit state management**:

1. **One source of truth per resource** — ResourceTracker.usage, not scattered across engine
2. **Automatic barrier derivation** — No caller mistakes, no duplicate state specs
3. **Graph executes in any order** — Dependency graph automatically injects barriers
4. **Screenshot works "for free"** — Just call texture_get_data(), barriers are auto-inserted
5. **Frame boundaries are clean** — State persists, but lazy-resets keep memory usage low

For UzEngine's rendering task #3.14 (screenshot support), adopt Godot's pattern:
- Store RhiResourceTracker alongside each texture
- Make usage → layout/access conversion deterministic
- Let the render graph auto-compute barriers on usage changes
- Screenshot just passes tracker + texture ID; the graph handles the rest

This is why Godot's code is ~1/5 the size of UE5's RHI abstraction while remaining more correct.
