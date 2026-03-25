/// UzEngine N-API — Viewport + rendering bindings.

#include "napi_common.h"

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

    if (!uz_create_viewport(reinterpret_cast<void*>(static_cast<uintptr_t>(parentHwnd)), width, height)) {
        Napi::Error::New(env, "Failed to create viewport").ThrowAsJavaScriptException();
    }
    return env.Undefined();
}

static Napi::Value DestroyViewport(const Napi::CallbackInfo& info) {
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
    uz_resize_viewport(info[0].As<Napi::Number>().Int32Value(),
                       info[1].As<Napi::Number>().Int32Value());
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
    uz_set_viewport_position(
        info[0].As<Napi::Number>().Int32Value(),
        info[1].As<Napi::Number>().Int32Value(),
        info[2].As<Napi::Number>().Int32Value(),
        info[3].As<Napi::Number>().Int32Value());
    return env.Undefined();
}

// ── Frame Rendering ─────────────────────────────────────────────────────────

static Napi::Value RenderFrame(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    uz_frame_stats stats = uz_render_frame();

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
    uint8_t* data  = nullptr;
    uint32_t size  = 0;

    if (!uz_capture_screenshot(&data, &size) || !data || size == 0) {
        return env.Null();
    }
    Napi::Buffer<uint8_t> buf = Napi::Buffer<uint8_t>::Copy(env, data, size);
    uz_free_buffer(data);
    return buf;
}

static Napi::Value FreeBuffer(const Napi::CallbackInfo& info) {
    return info.Env().Undefined(); // No-op — JS Buffers are GC-managed.
}

// ── Registration ────────────────────────────────────────────────────────────

void RegisterViewportBindings(Napi::Env env, Napi::Object exports) {
    exports.Set("createViewport",      Napi::Function::New(env, CreateViewport));
    exports.Set("destroyViewport",     Napi::Function::New(env, DestroyViewport));
    exports.Set("resizeViewport",      Napi::Function::New(env, ResizeViewport));
    exports.Set("setViewportPosition", Napi::Function::New(env, SetViewportPosition));
    exports.Set("renderFrame",         Napi::Function::New(env, RenderFrame));
    exports.Set("captureScreenshot",   Napi::Function::New(env, CaptureScreenshot));
    exports.Set("freeBuffer",          Napi::Function::New(env, FreeBuffer));
}
