/// UzEngine N-API — Lifecycle bindings (version, backend, init, shutdown).

#include "napi_common.h"

static Napi::Value NapiGetVersion(const Napi::CallbackInfo& info) {
    const char* ver = uz_runtime_version();
    return Napi::String::New(info.Env(), ver ? ver : "");
}

static Napi::Value NapiGetBackend(const Napi::CallbackInfo& info) {
    const char* backend = uz_runtime_backend();
    return Napi::String::New(info.Env(), backend ? backend : "none");
}

static Napi::Value NapiSetEngineDir(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "setEngineDir(path: string) expected").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    std::string path = info[0].As<Napi::String>().Utf8Value();
    uz_set_engine_dir(path.c_str());
    return env.Undefined();
}

static Napi::Value NapiInit(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    int result = 0;
    DWORD sehCode = uz_seh_runtime_init(&result);
    if (UzNapiCheckSeh(env, sehCode)) return env.Undefined();

    if (!result) {
        Napi::Error::New(env, "Engine initialization failed").ThrowAsJavaScriptException();
    }
    return env.Undefined();
}

static Napi::Value NapiShutdown(const Napi::CallbackInfo& info) {
    uz_runtime_shutdown();
    return info.Env().Undefined();
}

void RegisterLifecycleBindings(Napi::Env env, Napi::Object exports) {
    exports.Set("getVersion",  Napi::Function::New(env, NapiGetVersion));
    exports.Set("getBackend",  Napi::Function::New(env, NapiGetBackend));
    exports.Set("setEngineDir", Napi::Function::New(env, NapiSetEngineDir));
    exports.Set("init",        Napi::Function::New(env, NapiInit));
    exports.Set("shutdown",    Napi::Function::New(env, NapiShutdown));
}
