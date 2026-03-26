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

    int result = 0;
    DWORD sehCode = uz_seh_create_viewport(
        reinterpret_cast<void*>(static_cast<uintptr_t>(parentHwnd)),
        width, height, &result);
    if (UzNapiCheckSeh(env, sehCode)) return env.Undefined();

    if (!result) {
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

    uz_frame_stats stats = {};
    DWORD sehCode = uz_seh_render_frame(&stats);
    if (UzNapiCheckSeh(env, sehCode)) return env.Undefined();

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
    int result     = 0;
    DWORD sehCode = uz_seh_capture_screenshot(&data, &size, &result);
    if (UzNapiCheckSeh(env, sehCode)) return env.Undefined();

    if (!result || !data || size == 0) {
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

// ── H.264 Frame Encoding ────────────────────────────────────────────────

static Napi::Value EncodeFrame(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    const uint8_t* data = nullptr;
    uint32_t size       = 0;
    int result          = 0;
    DWORD sehCode = uz_seh_encode_frame(&data, &size, &result);
    if (UzNapiCheckSeh(env, sehCode)) return env.Undefined();

    if (!result || !data || size == 0) {
        return env.Null();
    }
    return Napi::Buffer<uint8_t>::Copy(env, data, size);
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
}
