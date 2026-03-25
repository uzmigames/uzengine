/// UzEngine N-API — Input forwarding bindings.

#include "napi_common.h"

static Napi::Value OnMouseMove(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 2 || !info[0].IsNumber() || !info[1].IsNumber()) {
        Napi::TypeError::New(env, "onMouseMove(x: number, y: number) expected").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    uz_on_mouse_move(static_cast<float>(info[0].As<Napi::Number>().DoubleValue()),
                     static_cast<float>(info[1].As<Napi::Number>().DoubleValue()));
    return env.Undefined();
}

static Napi::Value OnMouseButton(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 2 || !info[0].IsNumber() || !info[1].IsBoolean()) {
        Napi::TypeError::New(env, "onMouseButton(button: number, pressed: boolean) expected")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    uz_on_mouse_button(info[0].As<Napi::Number>().Int32Value(),
                       info[1].As<Napi::Boolean>().Value() ? 1 : 0);
    return env.Undefined();
}

static Napi::Value OnKey(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 2 || !info[0].IsNumber() || !info[1].IsBoolean()) {
        Napi::TypeError::New(env, "onKey(keycode: number, pressed: boolean) expected")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    uz_on_key(info[0].As<Napi::Number>().Int32Value(),
              info[1].As<Napi::Boolean>().Value() ? 1 : 0);
    return env.Undefined();
}

static Napi::Value OnScroll(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "onScroll(delta: number) expected").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    uz_on_scroll(static_cast<float>(info[0].As<Napi::Number>().DoubleValue()));
    return env.Undefined();
}

void RegisterInputBindings(Napi::Env env, Napi::Object exports) {
    exports.Set("onMouseMove",   Napi::Function::New(env, OnMouseMove));
    exports.Set("onMouseButton", Napi::Function::New(env, OnMouseButton));
    exports.Set("onKey",         Napi::Function::New(env, OnKey));
    exports.Set("onScroll",      Napi::Function::New(env, OnScroll));
}
