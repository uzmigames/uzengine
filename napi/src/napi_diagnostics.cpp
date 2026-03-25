/// UzEngine N-API — Diagnostics bindings (logging, editor mode, config).

#include "napi_common.h"

// ── Log Callback (ThreadSafeFunction) ───────────────────────────────────────

static Napi::ThreadSafeFunction s_logTsfn;

static void LogCallbackBridge(int level, const char* category, const char* message) {
    if (!s_logTsfn) return;
    std::string cat(category ? category : "");
    std::string msg(message ? message : "");
    int lvl = level;
    s_logTsfn.NonBlockingCall(
        [lvl, cat = std::move(cat), msg = std::move(msg)](Napi::Env env, Napi::Function jsCallback) {
            jsCallback.Call({
                Napi::Number::New(env, lvl),
                Napi::String::New(env, cat),
                Napi::String::New(env, msg)
            });
        }
    );
}

static Napi::Value SetLogCallback(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || info[0].IsNull() || info[0].IsUndefined()) {
        uz_set_log_callback(nullptr);
        if (s_logTsfn) {
            s_logTsfn.Release();
            s_logTsfn = Napi::ThreadSafeFunction();
        }
        return env.Undefined();
    }
    if (!info[0].IsFunction()) {
        Napi::TypeError::New(env, "setLogCallback(callback: Function | null) expected")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    if (s_logTsfn) {
        uz_set_log_callback(nullptr);
        s_logTsfn.Release();
        s_logTsfn = Napi::ThreadSafeFunction();
    }
    Napi::Function fn = info[0].As<Napi::Function>();
    s_logTsfn = Napi::ThreadSafeFunction::New(env, fn, "UzLogCallback", 256, 1);
    uz_set_log_callback(LogCallbackBridge);
    return env.Undefined();
}

// ── Editor Configuration ────────────────────────────────────────────────────

static Napi::Value SetEditorMode(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsBoolean()) {
        Napi::TypeError::New(env, "setEditorMode(enabled: boolean) expected").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    uz_set_editor_mode(info[0].As<Napi::Boolean>().Value() ? 1 : 0);
    return env.Undefined();
}

static Napi::Value SetContentRoot(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "setContentRoot(path: string) expected").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    std::string path = info[0].As<Napi::String>().Utf8Value();
    uz_set_content_root(path.c_str());
    return env.Undefined();
}

// ── Registration ────────────────────────────────────────────────────────────

void RegisterDiagnosticsBindings(Napi::Env env, Napi::Object exports) {
    exports.Set("setLogCallback", Napi::Function::New(env, SetLogCallback));
    exports.Set("setEditorMode",  Napi::Function::New(env, SetEditorMode));
    exports.Set("setContentRoot", Napi::Function::New(env, SetContentRoot));
}
