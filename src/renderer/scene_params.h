#pragma once

// ---------------------------------------------------------------------------
// scene_params.h -- Thread-safe scene parameters shared between render and
// IPC threads.
//
// Atomics are used for single-value parameters (lock-free reads from the
// render thread).  The background color uses a mutex because all three
// components must update atomically as a group.
// ---------------------------------------------------------------------------

#include <atomic>
#include <mutex>

namespace uz {

struct SceneParams {
    // Rotation speeds per axis (rad/s).
    std::atomic<float> rotX{0.0f};
    std::atomic<float> rotY{1.0f};
    std::atomic<float> rotZ{0.0f};

    // Pause the render loop (skip draw calls).
    std::atomic<bool> paused{false};

    // Background clear color -- mutex-protected because all three channels
    // must be read/written as a coherent triple.
    std::mutex bgMutex;
    float bgR{0.1f};
    float bgG{0.1f};
    float bgB{0.1f};

    // Geometry type: 0 = Pyramid (default), 1 = Cube (expandable).
    std::atomic<int> geometryType{0};
};

} // namespace uz
