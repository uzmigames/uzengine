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
#include <cstdint>
#include <mutex>
#include <utility>

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

    // -----------------------------------------------------------------------
    // Camera input state — written by WS text message handler (network thread),
    // read by the render thread each frame.
    // -----------------------------------------------------------------------
    struct CameraInput {
        std::atomic<float> moveForward{0.0f};   // W
        std::atomic<float> moveBack{0.0f};      // S
        std::atomic<float> moveLeft{0.0f};      // A
        std::atomic<float> moveRight{0.0f};     // D
        std::atomic<float> moveUp{0.0f};        // E
        std::atomic<float> moveDown{0.0f};      // Q
        std::atomic<float> mouseDeltaX{0.0f};
        std::atomic<float> mouseDeltaY{0.0f};
        std::atomic<bool>  rightMouseDown{false};
        std::atomic<bool>  shiftDown{false};
        std::atomic<float> scrollDelta{0.0f};

        // Staleness detection: timestamp of the last camera_input message.
        // If no message arrives for >200ms, the fly-cam zeroes all movement
        // (handles WebView focus loss, WebSocket disconnection, key stuck).
        std::atomic<int64_t> lastMessageUs{0};
    } cameraInput;

    // -----------------------------------------------------------------------
    // Viewport resize request — written by WS handler, read by render thread.
    // Packed as a single 64-bit atomic to avoid a race condition where the
    // render thread reads width before height is stored, causing a lost resize.
    // Upper 32 bits = width, lower 32 bits = height.  0 = no pending resize.
    // -----------------------------------------------------------------------
    std::atomic<uint64_t> resizePacked{0};

    /// Pack width and height into a single atomic store.
    void requestResize(int w, int h) {
        const auto packed = (static_cast<uint64_t>(static_cast<uint32_t>(w)) << 32)
                          |  static_cast<uint64_t>(static_cast<uint32_t>(h));
        resizePacked.store(packed, std::memory_order_relaxed);
    }

    /// Atomically read and clear the pending resize.  Returns {0,0} if none.
    std::pair<int,int> consumeResize() {
        const uint64_t packed = resizePacked.exchange(0, std::memory_order_relaxed);
        const int w = static_cast<int>(packed >> 32);
        const int h = static_cast<int>(packed & 0xFFFFFFFF);
        return {w, h};
    }

    // -----------------------------------------------------------------------
    // Derived camera state — written ONLY by render thread in update().
    // -----------------------------------------------------------------------
    float camPosX{0.0f};
    float camPosY{15.0f};
    float camPosZ{-20.0f};
    float camYaw{0.0f};            // radians, 0 = looking along +Z
    float camPitch{-0.6f};         // radians, negative = looking down
    float camSpeed{10.0f};         // units/sec base speed
    float camSensitivity{0.003f};  // radians per pixel of mouse delta

    // -----------------------------------------------------------------------
    // Gizmo interaction state — written by WS handler, read by render thread.
    // -----------------------------------------------------------------------
    std::atomic<uint32_t> selectedNodeId{0};  // 0 = nothing selected
    std::atomic<int>      gizmoMode{0};       // 0=translate, 1=rotate, 2=scale
    std::atomic<int>      gizmoSpace{0};      // 0=world, 1=local
    std::atomic<int>      gizmoActiveAxis{-1}; // -1=none, 0=X, 1=Y, 2=Z
    std::atomic<int>      gizmoHoverAxis{-1};  // axis under mouse cursor (-1=none)
    std::atomic<bool>     gizmoDragging{false};
    // Drag origin in screen space (set on beginDrag)
    std::atomic<float>    gizmoDragStartX{0.0f};
    std::atomic<float>    gizmoDragStartY{0.0f};
    // Node transform at drag start (for applying deltas)
    float gizmoDragOrigPos[3]{0.0f, 0.0f, 0.0f};
    float gizmoDragOrigRot[3]{0.0f, 0.0f, 0.0f};
    float gizmoDragOrigScale[3]{1.0f, 1.0f, 1.0f};

    // -----------------------------------------------------------------------
    // Multi-select gizmo pivot — when useGizmoPivot is true, the transform
    // gizmo is rendered at the centroid rather than at the selected node.
    // Written by WS handler (IPC thread), read by render thread.
    // -----------------------------------------------------------------------
    std::atomic<bool> useGizmoPivot{false};
    std::mutex        gizmoPivotMutex;
    float             gizmoPivotX{0.0f};
    float             gizmoPivotY{0.0f};
    float             gizmoPivotZ{0.0f};
};

} // namespace uz
