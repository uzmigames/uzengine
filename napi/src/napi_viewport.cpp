/// UzEngine N-API — Viewport + rendering bindings.

#include "napi_common.h"
#include "render_thread.h"

// ── Async Render Thread (singleton) ──────────────────────────────────────────

static RenderThread g_renderThread;

RenderThread& UzNapiRenderThread() { return g_renderThread; }

// ── Viewport Lifecycle ──────────────────────────────────────────────────────

static Napi::Value CreateViewport(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 3 || !info[0].IsNumber() || !info[1].IsNumber() || !info[2].IsNumber()) {
        Napi::TypeError::New(env, "createViewport(parentHwnd: number, width: number, height: number) expected")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    double parentHwnd = info[0].As<Napi::Number>().DoubleValue();
    int width         = info[1].As<Napi::Number>().Int32Value();
    int height        = info[2].As<Napi::Number>().Int32Value();

    struct Result { int ok = 0; DWORD seh = 0; };
    auto result = UzNapiOnOwner(env, "createViewport", [parentHwnd, width, height] {
        Result value;
        value.seh = uz_seh_create_viewport(
            reinterpret_cast<void*>(static_cast<uintptr_t>(parentHwnd)), width, height, &value.ok);
        return value;
    });
    if (!result || UzNapiCheckSeh(env, result->seh)) return env.Undefined();

    if (!result->ok) {
        Napi::Error::New(env, "Failed to create viewport").ThrowAsJavaScriptException();
    }
    return env.Undefined();
}

static Napi::Value DestroyViewport(const Napi::CallbackInfo& info) {
    g_renderThread.Stop();
    uz_destroy_viewport();
    return info.Env().Undefined();
}

static Napi::Value ResizeViewport(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 2 || !info[0].IsNumber() || !info[1].IsNumber()) {
        Napi::TypeError::New(env, "resizeViewport(width: number, height: number) expected")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    int w = info[0].As<Napi::Number>().Int32Value();
    int h = info[1].As<Napi::Number>().Int32Value();

    UzNapiOnOwner(env, "resizeViewport", [w, h] {
        uz_resize_viewport(w, h);
        g_renderThread.ArmFrameStream();
    });
    return env.Undefined();
}

static Napi::Value SetViewportPosition(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 4 ||
        !info[0].IsNumber() || !info[1].IsNumber() ||
        !info[2].IsNumber() || !info[3].IsNumber()) {
        Napi::TypeError::New(env, "setViewportPosition(x, y, width, height) expected")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    const int x = info[0].As<Napi::Number>().Int32Value();
    const int y = info[1].As<Napi::Number>().Int32Value();
    const int w = info[2].As<Napi::Number>().Int32Value();
    const int h = info[3].As<Napi::Number>().Int32Value();
    UzNapiOnOwner(env, "setViewportPosition", [x, y, w, h] {
        uz_set_viewport_position(x, y, w, h);
        g_renderThread.ArmFrameStream();
    });
    return env.Undefined();
}

// ── Frame Rendering ─────────────────────────────────────────────────────────

static Napi::Value RenderFrame(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (g_renderThread.IsRunning()) {
        Napi::Error::New(env, "renderFrame unavailable while render loop runs").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    struct Result { uz_frame_stats stats{}; DWORD seh = 0; };
    auto result = UzNapiOnOwner(env, "renderFrame", [] {
        Result value;
        value.seh = uz_seh_render_frame(&value.stats);
        return value;
    });
    if (!result || UzNapiCheckSeh(env, result->seh)) return env.Undefined();
    const auto& stats = result->stats;

    Napi::Object obj = Napi::Object::New(env);
    obj.Set("fps",        Napi::Number::New(env, stats.fps));
    obj.Set("frameMs",    Napi::Number::New(env, stats.frame_ms));
    obj.Set("drawCalls",  Napi::Number::New(env, stats.draw_calls));
    obj.Set("vertices",   Napi::Number::New(env, stats.vertices));
    obj.Set("triangles",  Napi::Number::New(env, stats.triangles));
    obj.Set("frameIndex", Napi::Number::New(env, stats.frame_index));
    return obj;
}

// ── Screenshot ──────────────────────────────────────────────────────────────

static Napi::Value CaptureScreenshot(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    struct Result { std::vector<uint8_t> bytes; DWORD seh = 0; };
    auto result = UzNapiOnOwner(env, "captureScreenshot", [] {
        Result value;
        uint8_t* data = nullptr;
        uint32_t size = 0;
        int ok = 0;
        value.seh = uz_seh_capture_screenshot(&data, &size, &ok);
        if (ok && data && size) value.bytes.assign(data, data + size);
        uz_free_buffer(data);
        return value;
    });
    if (!result || UzNapiCheckSeh(env, result->seh)) return env.Undefined();
    if (result->bytes.empty()) return env.Null();
    return Napi::Buffer<uint8_t>::Copy(env, result->bytes.data(), result->bytes.size());
}

static Napi::Value FreeBuffer(const Napi::CallbackInfo& info) {
    return info.Env().Undefined(); // No-op — JS Buffers are GC-managed.
}

// ── Registration ────────────────────────────────────────────────────────────

// ── H.264 Frame Encoding ────────────────────────────────────────────────

static Napi::Value EncodeFrame(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    struct Result { std::vector<uint8_t> bytes; DWORD seh = 0; };
    auto result = UzNapiOnOwner(env, "encodeFrame", [] {
        Result value;
        const uint8_t* data = nullptr;
        uint32_t size = 0;
        int ok = 0;
        value.seh = uz_seh_encode_frame(&data, &size, &ok);
        if (ok && data && size) value.bytes.assign(data, data + size);
        return value;
    });
    if (!result || UzNapiCheckSeh(env, result->seh)) return env.Undefined();
    if (result->bytes.empty()) return env.Null();
    return Napi::Buffer<uint8_t>::Copy(env, result->bytes.data(), result->bytes.size());
}

static Napi::Value RequestKeyframe(const Napi::CallbackInfo& info) {
    uz_request_keyframe();
    return info.Env().Undefined();
}

static Napi::Value SetEncodeBitrate(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "setEncodeBitrate(kbps: number) expected")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    uz_set_encode_bitrate(static_cast<uint32_t>(info[0].As<Napi::Number>().Int32Value()));
    return env.Undefined();
}

static Napi::Value GetCodecExtradata(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    const uint8_t* data = nullptr;
    uint32_t size       = 0;
    int result          = 0;
    DWORD sehCode = uz_seh_get_codec_extradata(&data, &size, &result);
    if (UzNapiCheckSeh(env, sehCode)) return env.Undefined();

    if (!result || !data || size == 0) {
        return env.Null();
    }
    return Napi::Buffer<uint8_t>::Copy(env, data, size);
}

// ── Async Render Loop ────────────────────────────────────────────────────

/// startRenderLoop(targetFps: number, onFrame: Function, onEncodedFrame: Function): boolean
///
/// Starts a dedicated render thread that calls uz_render_frame() + uz_encode_frame()
/// at the target FPS. Results are delivered to the provided callbacks on the Node.js
/// main thread via napi_threadsafe_function, keeping the event loop unblocked.
static Napi::Value StartRenderLoop(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 3 || !info[0].IsNumber() || !info[1].IsFunction() || !info[2].IsFunction()) {
        Napi::TypeError::New(env,
            "startRenderLoop(targetFps: number, onFrame: Function, onEncodedFrame: Function) expected")
            .ThrowAsJavaScriptException();
        return Napi::Boolean::New(env, false);
    }
    int fps = info[0].As<Napi::Number>().Int32Value();
    auto onFrame = info[1].As<Napi::Function>();
    auto onEncodedFrame = info[2].As<Napi::Function>();

    bool ok = g_renderThread.Start(env, fps, onFrame, onEncodedFrame);
    return Napi::Boolean::New(env, ok);
}

/// stopRenderLoop(): void
///
/// Signals the render thread to stop and blocks until it has joined.
/// Safe to call even if the loop is not running (no-op).
static Napi::Value StopRenderLoop(const Napi::CallbackInfo& info) {
    g_renderThread.Stop();
    return info.Env().Undefined();
}

/// requestResizeAsync(width: number, height: number): void
///
/// Queues a swapchain resize to be applied at the start of the next frame on
/// the render thread. Does not block the caller.
static Napi::Value RequestResizeAsync(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 2 || !info[0].IsNumber() || !info[1].IsNumber()) {
        Napi::TypeError::New(env, "requestResizeAsync(width: number, height: number) expected")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    g_renderThread.RequestResize(
        info[0].As<Napi::Number>().Int32Value(),
        info[1].As<Napi::Number>().Int32Value());
    return env.Undefined();
}

/// isRenderLoopRunning(): boolean
static Napi::Value IsRenderLoopRunning(const Napi::CallbackInfo& info) {
    return Napi::Boolean::New(info.Env(), g_renderThread.IsRunning());
}

// ── Registration ────────────────────────────────────────────────────────

void RegisterViewportBindings(Napi::Env env, Napi::Object exports) {
    exports.Set("createViewport",      Napi::Function::New(env, CreateViewport));
    exports.Set("destroyViewport",     Napi::Function::New(env, DestroyViewport));
    exports.Set("resizeViewport",      Napi::Function::New(env, ResizeViewport));
    exports.Set("setViewportPosition", Napi::Function::New(env, SetViewportPosition));
    exports.Set("renderFrame",         Napi::Function::New(env, RenderFrame));
    exports.Set("captureScreenshot",   Napi::Function::New(env, CaptureScreenshot));
    exports.Set("freeBuffer",          Napi::Function::New(env, FreeBuffer));
    exports.Set("encodeFrame",         Napi::Function::New(env, EncodeFrame));
    exports.Set("requestKeyframe",     Napi::Function::New(env, RequestKeyframe));
    exports.Set("getCodecExtradata",   Napi::Function::New(env, GetCodecExtradata));
    exports.Set("setEncodeBitrate",   Napi::Function::New(env, SetEncodeBitrate));

    // Async render loop (dedicated thread -- does not block the event loop).
    exports.Set("startRenderLoop",      Napi::Function::New(env, StartRenderLoop));
    exports.Set("stopRenderLoop",       Napi::Function::New(env, StopRenderLoop));
    exports.Set("requestResizeAsync",   Napi::Function::New(env, RequestResizeAsync));
    exports.Set("isRenderLoopRunning",  Napi::Function::New(env, IsRenderLoopRunning));
}
