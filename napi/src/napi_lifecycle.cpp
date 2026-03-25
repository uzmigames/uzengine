/// UzEngine N-API — Lifecycle bindings (version, backend, init, shutdown).

#include "napi_common.h"

static Napi::Value GetVersion(const Napi::CallbackInfo& info) {
    const char* ver = uz_runtime_version();
    return Napi::String::New(info.Env(), ver ? ver : "");
}

static Napi::Value GetBackend(const Napi::CallbackInfo& info) {
    const char* backend = uz_runtime_backend();
    return Napi::String::New(info.Env(), backend ? backend : "none");
}

static Napi::Value Init(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (!uz_runtime_init()) {
        Napi::Error::New(env, "Engine initialization failed").ThrowAsJavaScriptException();
    }
    return env.Undefined();
}

static Napi::Value Shutdown(const Napi::CallbackInfo& info) {
    uz_runtime_shutdown();
    return info.Env().Undefined();
}

void RegisterLifecycleBindings(Napi::Env env, Napi::Object exports) {
    exports.Set("getVersion", Napi::Function::New(env, GetVersion));
    exports.Set("getBackend", Napi::Function::New(env, GetBackend));
    exports.Set("init",       Napi::Function::New(env, Init));
    exports.Set("shutdown",   Napi::Function::New(env, Shutdown));
}
