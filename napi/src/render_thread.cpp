/// UzEngine N-API -- Async render thread implementation.
///
/// Drives the render+encode loop on a dedicated thread, delivering results
/// to the Node.js event loop via napi_threadsafe_function (non-blocking).

#include "render_thread.h"
#include "napi_common.h"

#include <objbase.h>  // CoInitializeEx, CoUninitialize

// ── Helpers ──────────────────────────────────────────────────────────────────

namespace {

/// Callback invoked by Napi::ThreadSafeFunction on the Node.js main thread
/// to deliver frame stats. The `data` pointer is a heap-allocated uz_frame_stats
/// that we own and must delete.
void OnFrameCallback(Napi::Env env, Napi::Function fn, uz_frame_stats* stats) {
    if (env == nullptr || fn == nullptr) {
        delete stats;
        return;
    }

    Napi::Object obj = Napi::Object::New(env);
    obj.Set("fps",        Napi::Number::New(env, stats->fps));
    obj.Set("frameMs",    Napi::Number::New(env, stats->frame_ms));
    obj.Set("drawCalls",  Napi::Number::New(env, stats->draw_calls));
    obj.Set("vertices",   Napi::Number::New(env, stats->vertices));
    obj.Set("triangles",  Napi::Number::New(env, stats->triangles));
    obj.Set("frameIndex", Napi::Number::New(env, stats->frame_index));
    fn.Call({obj});
    delete stats;
}

/// Callback invoked by Napi::ThreadSafeFunction on the Node.js main thread
/// to deliver an encoded H.264 frame. The `data` pointer is a heap-allocated
/// vector containing the NAL bytes that we own and must delete.
void OnEncodedFrameCallback(Napi::Env env, Napi::Function fn,
                            std::vector<uint8_t>* nalData) {
    if (env == nullptr || fn == nullptr) {
        delete nalData;
        return;
    }

    // Copy into a V8-managed Buffer so JS can hold it beyond this scope.
    auto buf = Napi::Buffer<uint8_t>::Copy(env, nalData->data(), nalData->size());
    fn.Call({buf});
    delete nalData;
}

}  // namespace

// ── Lifecycle ────────────────────────────────────────────────────────────────

RenderThread::~RenderThread() {
    Stop();
}

bool RenderThread::Start(Napi::Env env, int targetFps,
                         Napi::Function onFrame, Napi::Function onEncodedFrame) {
    // Prevent double-start.
    if (m_running.load(std::memory_order_acquire)) {
        return false;
    }

    m_targetFps = (targetFps > 0) ? targetFps : 60;
    m_stopRequested.store(false, std::memory_order_release);

    // Create ThreadSafeFunctions. MaxQueueSize=0 means unlimited -- we never
    // want the render thread to block waiting for JS to drain the queue.
    // InitialRefcount=1 keeps the TSFN alive until we call Release().
    m_onFrameTsfn = Napi::ThreadSafeFunction::New(
        env,
        onFrame,
        "uzRenderFrameCb",     // resource name (for async_hooks diagnostics)
        0,                     // max queue size (0 = unlimited)
        1,                     // initial thread count
        static_cast<void*>(nullptr),  // release context
        [](Napi::Env, void*, void*) {},  // release callback (no-op)
        static_cast<void*>(nullptr)       // invoke context
    );

    m_onEncodedFrameTsfn = Napi::ThreadSafeFunction::New(
        env,
        onEncodedFrame,
        "uzEncodedFrameCb",
        0,
        1,
        static_cast<void*>(nullptr),
        [](Napi::Env, void*, void*) {},
        static_cast<void*>(nullptr)
    );

    m_running.store(true, std::memory_order_release);
    m_thread = std::thread(&RenderThread::ThreadMain, this);
    return true;
}

void RenderThread::Stop() {
    if (!m_running.load(std::memory_order_acquire)) {
        return;
    }

    m_stopRequested.store(true, std::memory_order_release);

    if (m_thread.joinable()) {
        m_thread.join();
    }

    // Release the TSFNs so Node.js can shut down cleanly. Calling Release()
    // decrements the ref count; once it reaches zero the release callback fires
    // and the Event Loop is allowed to exit.
    m_onFrameTsfn.Release();
    m_onEncodedFrameTsfn.Release();

    m_running.store(false, std::memory_order_release);
}

void RenderThread::RequestResize(int width, int height) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pendingWidth = width;
    m_pendingHeight = height;
    m_resizePending = true;
}

// ── Render Loop ──────────────────────────────────────────────────────────────

void RenderThread::ThreadMain() {
    // Initialize COM for this thread. uz_render_frame() uses Win32 message
    // pumping internally (PeekMessage/DispatchMessage), and DX12 device
    // creation may call into COM. MTA is appropriate for a worker thread.
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comInitialized = SUCCEEDED(hr);

    using Clock = std::chrono::high_resolution_clock;
    const auto frameDuration = std::chrono::nanoseconds(1'000'000'000 / m_targetFps);

    while (!m_stopRequested.load(std::memory_order_acquire)) {
        const auto frameStart = Clock::now();

        // ── Apply pending resize ─────────────────────────────────────────
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_resizePending) {
                uz_resize_viewport(m_pendingWidth, m_pendingHeight);
                m_resizePending = false;
            }
        }

        // ── Render ───────────────────────────────────────────────────────
        // Call the raw C API directly (no SEH wrapper). The SEH wrappers in
        // napi_common.h convert structured exceptions to JS exceptions, which
        // only makes sense on the Node.js main thread. On the render thread
        // a crash should propagate to the OS exception handler / crash reporter.
        uz_frame_stats stats = uz_render_frame();

        // Deliver stats to Node.js. Heap-allocate because the TSFN callback
        // runs asynchronously on the main thread after we return.
        auto* statsCopy = new uz_frame_stats(stats);
        napi_status status = m_onFrameTsfn.NonBlockingCall(statsCopy, OnFrameCallback);
        if (status != napi_ok) {
            // Queue full or TSFN is closing -- discard this frame's stats.
            delete statsCopy;
        }

        // ── Encode ───────────────────────────────────────────────────────
        const uint8_t* encData = nullptr;
        uint32_t encSize = 0;
        int encResult = uz_encode_frame(&encData, &encSize);

        if (encResult && encData != nullptr && encSize > 0) {
            // The encoder's output buffer is only valid until the next
            // uz_encode_frame() call, so copy the NAL data for the async
            // delivery path.
            auto* nalCopy = new std::vector<uint8_t>(encData, encData + encSize);
            napi_status encStatus = m_onEncodedFrameTsfn.NonBlockingCall(
                nalCopy, OnEncodedFrameCallback);
            if (encStatus != napi_ok) {
                delete nalCopy;
            }
        }

        // ── Frame pacing ─────────────────────────────────────────────────
        // Sleep the remainder of the frame budget. This is a simple busy-wait
        // avoidance strategy; sub-ms precision is not critical since the
        // primary goal is to avoid hogging a full CPU core at 100%.
        const auto frameEnd = Clock::now();
        const auto elapsed = frameEnd - frameStart;
        if (elapsed < frameDuration) {
            std::this_thread::sleep_for(frameDuration - elapsed);
        }
    }

    if (comInitialized) {
        CoUninitialize();
    }
}
