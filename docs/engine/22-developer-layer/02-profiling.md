# Profiling — CPU, GPU, and Memory Timing

> **Status**: Planned (Phase 1–2)
> **Source**: `developer/profiling/`
> **References**: Tracy Profiler (open-source), GPU timing (D3D12), memory tracking via allocators

## Purpose

Real-time performance profiling to find bottlenecks: CPU time per function, GPU time per render pass, memory allocations. All profiling code compiles to nothing in shipping builds (`UZ_SHIPPING=1`).

## Architecture

```
Main Thread                 GPU Thread
  ├── UZ_PROFILE_SCOPE()   ├── RecordGPUTimestamp()
  │   └── Tracy frame      │   └── D3D12 query buffer
  │
  └── Per-frame stats      └── GPU-side timeline
      (60 FPS)
```

## CPU Profiling (Tracy)

### UZ_PROFILE_SCOPE Macro

```cpp
#if UZ_DEV_BUILD
    #include "developer/profiling/profiler.h"
    #define UZ_PROFILE_SCOPE(name) uz::dev::ProfileScope __scope_##__LINE__(name)
#else
    #define UZ_PROFILE_SCOPE(name) ((void)0)  // Zero cost in shipping
#endif

// Usage:
void PhysicsSystem::Update(float dt) {
    UZ_PROFILE_SCOPE("Physics::Update");

    for (auto& body : bodies_) {
        UZ_PROFILE_SCOPE("PhysicsBody::Integrate");
        IntegrateVelocity(body, dt);
    }
}
```

### ProfileScope Class

```cpp
namespace uz::dev {

class ProfileScope {
public:
    /// Create scope with name
    explicit ProfileScope(const char* name) : name_(name) {
        start_time_ = std::chrono::high_resolution_clock::now();
    }

    /// Destroy scope — automatically records duration to Tracy
    ~ProfileScope() {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time_
        ).count();

        // Send to Tracy
        TracyEmitZone(name_, duration_us);
    }

private:
    const char* name_;
    std::chrono::high_resolution_clock::time_point start_time_;
};

}  // namespace uz::dev
```

### Zone Colors and Categories

Zones can be grouped by color for visual organization:

```cpp
// In profiler config
enum class ProfileCategory {
    Physics,     // Blue
    Rendering,   // Green
    Audio,       // Yellow
    Scripting,   // Red
    Other        // Gray
};

#define UZ_PROFILE_SCOPE_PHYSICS(name) \
    uz::dev::ProfileScope __scope(name, uz::dev::ProfileCategory::Physics)

// Usage:
void PhysicsSystem::Update(float dt) {
    UZ_PROFILE_SCOPE_PHYSICS("Physics::Update");
    // Blue zone in Tracy timeline
}
```

## GPU Profiling (D3D12 Timestamp Queries)

### GPU Timestamp Recording

```cpp
namespace uz::dev {

/// Record timestamp in GPU command stream
void RecordGPUTimestamp(const char* pass_name);

/// Get frame's GPU profiling stats
struct GPUProfileFrame {
    std::vector<std::pair<std::string, float>> passes;  // {name, milliseconds}
    float total_ms;
    uint64_t frame_number;
};

GPUProfileFrame GetLastGPUFrame();

}  // namespace uz::dev
```

### Implementation

Uses D3D12 QueryHeap for timestamp queries:

```cpp
namespace uz::dev {

class GPUProfiler {
public:
    void RecordTimestamp(const char* pass_name) {
        // Add timestamp to query buffer
        command_list_->EndQuery(
            timestamp_query_heap_.get(),
            D3D12_QUERY_TYPE_TIMESTAMP,
            query_index_++
        );

        pending_queries_.emplace_back(pass_name, query_index_ - 1);
    }

    void ResolveTimestamps() {
        // Resolve queries from GPU to CPU-readable buffer
        // (happens 1-2 frames after recording due to GPU pipeline)
        command_list_->ResolveQueryData(
            timestamp_query_heap_.get(),
            D3D12_QUERY_TYPE_TIMESTAMP,
            0,
            query_count_,
            readback_buffer_.get(),
            0
        );
    }

    GPUProfileFrame GetFrameStats() {
        GPUProfileFrame frame;

        // Readback buffer contains timestamps
        auto* timestamps = static_cast<uint64_t*>(
            readback_buffer_->Map(0)
        );

        uint64_t gpu_frequency;
        device_->GetTimestampFrequency(&gpu_frequency);

        for (auto& query : pending_queries_) {
            uint64_t delta = timestamps[query.second] - timestamps[query.second - 1];
            float duration_ms = (delta / static_cast<float>(gpu_frequency)) * 1000.0f;
            frame.passes.push_back({query.first, duration_ms});
            frame.total_ms += duration_ms;
        }

        readback_buffer_->Unmap(0);
        return frame;
    }

private:
    ComPtr<ID3D12QueryHeap> timestamp_query_heap_;
    ComPtr<ID3D12Resource> readback_buffer_;
    std::vector<std::pair<std::string, uint32_t>> pending_queries_;
};

}  // namespace uz::dev
```

### Usage in Renderer

```cpp
void Renderer::RenderFrame() {
    uz::dev::RecordGPUTimestamp("GBuffer");
    RenderGBuffer(cmd_list);

    uz::dev::RecordGPUTimestamp("Lighting");
    RenderLighting(cmd_list);

    uz::dev::RecordGPUTimestamp("Composite");
    RenderComposite(cmd_list);

    uz::dev::RecordGPUTimestamp("UI");
    RenderUI(cmd_list);

    // Frame complete — resolve timestamps
    uz::dev::ResolveGPUTimestamps();

    // Display on overlay
    auto frame_stats = uz::dev::GetLastGPUFrame();
    DebugOverlay::ShowGPUStats(frame_stats);
}
```

## Memory Profiling

### Allocation Tracking

Custom allocators track all allocations:

```cpp
namespace uz::dev {

struct MemoryStats {
    size_t total_allocated;        ///< Bytes currently allocated
    size_t total_freed;            ///< Bytes freed (cumulative)
    size_t peak_allocated;         ///< Max bytes ever allocated
    size_t live_allocations;       ///< Count of active allocations

    std::vector<std::pair<std::string, size_t>> top_allocators;  // Category → size
};

MemoryStats GetMemoryStats();

}  // namespace uz::dev
```

### Custom Allocator with Tracking

```cpp
namespace uz::dev {

class ProfilingAllocator {
public:
    void* Allocate(size_t size, const char* category = "Other") {
        void* ptr = malloc(size);

        // Record allocation
        {
            std::lock_guard lock(stats_mutex_);
            allocations_[ptr] = {size, category};
            total_allocated_ += size;
            category_sizes_[category] += size;
            peak_allocated_ = std::max(peak_allocated_, total_allocated_);
        }

        // Send to Tracy
        TracyAllocN(ptr, size, category);

        return ptr;
    }

    void Deallocate(void* ptr) {
        {
            std::lock_guard lock(stats_mutex_);
            auto it = allocations_.find(ptr);
            if (it != allocations_.end()) {
                const auto& alloc = it->second;
                total_allocated_ -= alloc.size;
                category_sizes_[alloc.category] -= alloc.size;
                allocations_.erase(it);
            }
        }

        TracyFree(ptr);
        free(ptr);
    }

    MemoryStats GetStats() const {
        std::lock_guard lock(stats_mutex_);
        MemoryStats stats;
        stats.total_allocated = total_allocated_;
        stats.peak_allocated = peak_allocated_;
        stats.live_allocations = allocations_.size();

        for (auto& [category, size] : category_sizes_) {
            stats.top_allocators.push_back({category, size});
        }

        // Sort by size descending
        std::sort(stats.top_allocators.rbegin(), stats.top_allocators.rend());

        return stats;
    }

private:
    std::unordered_map<void*, std::pair<size_t, std::string>> allocations_;
    std::unordered_map<std::string, size_t> category_sizes_;
    size_t total_allocated_ = 0;
    size_t peak_allocated_ = 0;
    mutable std::mutex stats_mutex_;
};

}  // namespace uz::dev
```

## Frame Mark

Mark frame boundaries for profiler analysis:

```cpp
void Engine::Update() {
    while (running) {
        // ... frame logic ...

        // Mark end of frame for profiler
        uz::dev::FrameMark();  // Tracy sees frame boundaries
    }
}
```

## Debug Overlay Display

```cpp
namespace uz::dev {

void ShowProfilingOverlay() {
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Profiling Stats", nullptr, ImGuiWindowFlags_NoMove)) {
        // CPU stats
        ImGui::Text("CPU Frame: %.2f ms", GetLastFrameTimeMS());
        ImGui::Text("FPS: %.1f", 1000.0f / GetLastFrameTimeMS());

        // GPU stats
        auto gpu_frame = GetLastGPUFrame();
        ImGui::Separator();
        ImGui::Text("GPU Frame: %.2f ms", gpu_frame.total_ms);
        for (auto& [pass_name, duration_ms] : gpu_frame.passes) {
            ImGui::Text("  %s: %.2f ms", pass_name.c_str(), duration_ms);
        }

        // Memory stats
        auto mem_stats = GetMemoryStats();
        ImGui::Separator();
        ImGui::Text("Memory: %.2f MB / %.2f MB (peak: %.2f MB)",
            mem_stats.total_allocated / 1024.0f / 1024.0f,
            mem_stats.total_allocated / 1024.0f / 1024.0f,
            mem_stats.peak_allocated / 1024.0f / 1024.0f
        );

        // Top allocators
        ImGui::Text("Top Allocators:");
        for (auto& [category, size] : mem_stats.top_allocators) {
            if (size > 0) {
                ImGui::Text("  %s: %.2f MB", category.c_str(), size / 1024.0f / 1024.0f);
            }
        }

        ImGui::End();
    }
}

}  // namespace uz::dev
```

## Tracy Integration

Tracy is a real-time profiler that visualizes all recorded zones:

```bash
# Build with Tracy
zig build -Dtracy=true

# Connect Tracy tool while engine runs
# View frame by frame with per-zone timing, memory allocations, locks
```

Tracy dashboard shows:
- CPU zones per thread
- GPU timeline with passes
- Memory allocations over time
- Lock contention
- Streaming of data to external tool

## Comparison

| Feature | Unreal Insights (Tracy) | Godot Profiler | Unity Profiler | **UzEngine** |
|---------|------------------------|-----------------|-----------------|--------------|
| CPU profiling | Tracy (real-time) | Built-in (limited) | Advanced | Tracy |
| GPU profiling | D3D12 timers | Limited | Advanced | D3D12 QueryHeap |
| Memory tracking | Windows Task Manager | Built-in | Memory Profiler | Custom allocator |
| Real-time UI | Yes (Tracy) | Yes (limited) | Yes | ImGui overlay |
| External tool | Yes (Tracy) | No | Yes | Tracy |
| Shipping overhead | Zero (compiled out) | Minimal | Minimal | Zero |

## Implementation Phase

- **Phase 1**: CPU profiling with Tracy, UZ_PROFILE_SCOPE macro, frame marks
- **Phase 2**: GPU timestamp queries, D3D12 QueryHeap integration, memory tracking
- **Phase 3**: ImGui overlay stats display, per-system breakdowns
- **Phase 4**: Frame graph captures, profile export (JSON), remote profiling
- **Phase 5**: Constraint-based profiling (alert if frame > 16.67ms), automated regression detection
