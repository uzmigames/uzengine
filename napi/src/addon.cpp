#include <napi.h>
#include "uz_runtime.h"

Napi::Value GetVersion(const Napi::CallbackInfo& info) {
    return Napi::String::New(info.Env(), uz_runtime_version());
}

Napi::Value GetBackend(const Napi::CallbackInfo& info) {
    return Napi::String::New(info.Env(), uz_runtime_backend());
}

Napi::Value Init(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (!uz_runtime_init()) {
        Napi::Error::New(env, "Engine initialization failed").ThrowAsJavaScriptException();
    }
    return env.Undefined();
}

Napi::Value Shutdown(const Napi::CallbackInfo& info) {
    uz_runtime_shutdown();
    return info.Env().Undefined();
}

Napi::Object InitModule(Napi::Env env, Napi::Object exports) {
    exports.Set("getVersion", Napi::Function::New(env, GetVersion));
    exports.Set("getBackend", Napi::Function::New(env, GetBackend));
    exports.Set("init", Napi::Function::New(env, Init));
    exports.Set("shutdown", Napi::Function::New(env, Shutdown));
    return exports;
}

NODE_API_MODULE(uzengine, InitModule)
