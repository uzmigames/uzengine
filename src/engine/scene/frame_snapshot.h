#pragma once

// Frame Snapshot — immutable data passed from game thread to render thread (XE-6).
//
// Each frame, the game thread builds a RenderFrameSnapshot containing all visible
// node transforms, lights, and camera data. The render thread consumes an immutable
// copy — it NEVER touches game-thread pointers directly. The DoubleBuffer<T> template
// provides a lock-free swap mechanism for this handoff.
//
// Thread-safety: DoubleBuffer uses a single atomic index for producer/consumer swap.
// The producer writes to the "back" buffer, then calls swap(). The consumer reads
// from the "front" buffer. No mutexes needed.

#include "engine/core/math.h"
#include "engine/core/string_id.h"

#include <atomic>
#include <cstdint>
#include <vector>

namespace uz {

// ============================================================================
// Snapshot data structures
// ============================================================================

/// Immutable snapshot of a single node's render state.
struct NodeSnapshot {
    uint32_t    node_id     = 0;        // Scene node index
    StringId    type_id;                // Component type hash
    Vec3        position    = {};       // World-space position
    Vec3        rotation    = {};       // Euler rotation (degrees)
    Vec3        scale       = {1, 1, 1};
    bool        visible     = true;
};

/// Immutable snapshot of a light source.
struct LightSnapshot {
    uint32_t    node_id     = 0;
    Vec3        position    = {};
    Vec3        direction   = {0, -1, 0};
    Vec3        color       = {1, 1, 1};
    float       intensity   = 1.0f;
    uint8_t     light_type  = 0;        // 0=directional, 1=point, 2=spot
};

/// Camera snapshot — frozen view/projection for one frame.
struct CameraSnapshot {
    Vec3        position    = {};
    Vec3        target      = {0, 0, -1};
    Vec3        up          = {0, 1, 0};
    float       fov_y       = 60.0f;    // Degrees
    float       aspect      = 16.0f / 9.0f;
    float       near_plane  = 0.1f;
    float       far_plane   = 1000.0f;
    Mat4        view        = {};
    Mat4        projection  = {};
};

/// Complete frame snapshot — everything the render thread needs.
struct RenderFrameSnapshot {
    uint64_t                    frame_number  = 0;
    CameraSnapshot              camera;
    std::vector<NodeSnapshot>   nodes;
    std::vector<LightSnapshot>  lights;

    /// Clear all data for reuse.
    void clear() {
        frame_number = 0;
        camera = {};
        nodes.clear();
        lights.clear();
    }
};

// ============================================================================
// DoubleBuffer<T> — lock-free producer/consumer swap
// ============================================================================
//
// The producer writes to back(), then calls swap(). The consumer reads from
// front(). After swap(), the old front becomes the new back (available for
// the producer to write into), and the old back becomes the new front.
//
// This is NOT a general-purpose concurrent container. It assumes:
//   - Exactly ONE producer thread (game thread).
//   - Exactly ONE consumer thread (render thread).
//   - swap() is called by the producer after finishing a write.

template<typename T>
class DoubleBuffer {
public:
    DoubleBuffer() = default;

    /// Returns a mutable reference to the current write buffer (producer side).
    [[nodiscard]] T& back() { return buffers_[write_index_.load(std::memory_order_relaxed)]; }

    /// Returns a const reference to the current read buffer (consumer side).
    [[nodiscard]] const T& front() const { return buffers_[1 - write_index_.load(std::memory_order_acquire)]; }

    /// Swap the buffers. Call after the producer is done writing to back().
    /// After this call, the consumer sees the new data via front().
    void swap() {
        int old = write_index_.load(std::memory_order_relaxed);
        write_index_.store(1 - old, std::memory_order_release);
    }

    /// Direct access to both buffers (for initialization).
    [[nodiscard]] T& buffer(int i) { return buffers_[i]; }

private:
    T                buffers_[2]   = {};
    std::atomic<int> write_index_  = {0};
};

} // namespace uz
