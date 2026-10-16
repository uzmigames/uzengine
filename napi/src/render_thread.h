/// UzEngine N-API -- Async render thread.
///
/// Moves the render+encode loop off the Node.js main thread onto a dedicated
/// std::thread. Completed frames (stats and encoded H.264 NALs) are delivered
/// back to JavaScript via napi_threadsafe_function, which queues a call on
/// Node's event loop without blocking it.
///
/// # Why
/// uz_render_frame() blocks 8-16 ms per frame. Running it on the Node.js main
/// thread stalls the entire event loop, freezing UI updates, IPC, and Extension
/// Host communication. The async thread eliminates this bottleneck.
///
/// # Thread Safety Contract
/// - While started, RuntimeState access and input execute only on ThreadMain(),
///   between frames. Synchronous requests use bounded owner rendezvous.
/// - Without a loop, requests and input run inline on the Node main thread.
/// - Resize is queued from the main thread and applied at frame start under
///   the render thread's control, avoiding mid-frame swapchain mutation.
/// - Start() and Stop() must be called from the Node.js main thread.

#pragma once

#include <napi.h>
#include "../../runtime/engine/app/owner_command_queue.h"
#include "../../runtime/engine/containers/ring_buffer.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

// Forward declaration -- full definition lives in runtime.h.
struct uz_frame_stats;

struct InputEvent {
    enum class Type { MouseMove, MouseButton, Key, KeyEx, Scroll };
    Type type = Type::MouseMove;
    float x = 0, y = 0;
    int code = 0, pressed = 0, modifiers = 0;
};

/// Dedicated render thread that drives uz_render_frame() + uz_encode_frame()
/// off the Node.js event loop, delivering results via ThreadSafeFunctions.
class RenderThread {
public:
    RenderThread() = default;
    ~RenderThread();

    // Non-copyable, non-movable -- singleton-like lifetime tied to the module.
    RenderThread(const RenderThread&) = delete;
    RenderThread& operator=(const RenderThread&) = delete;
    RenderThread(RenderThread&&) = delete;
    RenderThread& operator=(RenderThread&&) = delete;

    /// Starts the render loop at the specified target FPS.
    ///
    /// `onFrame` is called on the Node.js main thread with a stats object for
    /// every completed frame. `onEncodedFrame` is called with a Buffer<uint8_t>
    /// containing the H.264 NAL units whenever the encoder produces output.
    ///
    /// Returns false if already running or if TSFN creation fails.
    bool Start(Napi::Env env, int targetFps,
               Napi::Function onFrame, Napi::Function onEncodedFrame);

    /// Signals the render thread to stop and blocks until it joins.
    /// Safe to call from any thread. No-op if not running.
    void Stop();

    /// Queues a swapchain resize to be applied at the start of the next frame.
    /// Thread-safe -- can be called from the Node.js main thread at any time.
    void RequestResize(int width, int height);

    uz::OwnerStatus RunOnOwner(std::function<void()> fn,
                              std::chrono::milliseconds completionBudget);
    void PushInput(const InputEvent& event);
    [[nodiscard]] uint64_t DroppedInputCount() const noexcept { return m_droppedInput.load(); }

    /// Returns true if the render thread is actively looping.
    [[nodiscard]] bool IsRunning() const noexcept { return m_running.load(std::memory_order_acquire); }

private:
    /// The render loop body, executed entirely on m_thread.
    /// Initializes COM, then loops: apply resize -> render -> encode -> pace.
    void ThreadMain();

    std::thread m_thread;
    uz::OwnerCommandQueue m_commands;
    uz::RingBuffer<InputEvent, 256> m_input;
    std::atomic<uint64_t> m_droppedInput{0};
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    int m_targetFps = 60;

    // Resize requests (protected by m_mutex, consumed at frame start).
    std::mutex m_mutex;
    int m_pendingWidth = 0;
    int m_pendingHeight = 0;
    bool m_resizePending = false;

    // ThreadSafeFunction handles for delivering data back to Node.js.
    // These are created from the main thread in Start() and released in Stop().
    Napi::ThreadSafeFunction m_onFrameTsfn;
    Napi::ThreadSafeFunction m_onEncodedFrameTsfn;
};
