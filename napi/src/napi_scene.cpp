/// UzEngine N-API — Scene + entity + component bindings.

#include "napi_common.h"

// ── Scene I/O ───────────────────────────────────────────────────────────────

static Napi::Value LoadScene(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "loadScene(path: string) expected").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    std::string path = info[0].As<Napi::String>().Utf8Value();
    if (!uz_load_scene(path.c_str())) {
        Napi::Error::New(env, std::string("Failed to load scene: ") + path).ThrowAsJavaScriptException();
    }
    return env.Undefined();
}

static Napi::Value SaveScene(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "saveScene(path: string) expected").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    std::string path = info[0].As<Napi::String>().Utf8Value();
    if (!uz_save_scene(path.c_str())) {
        Napi::Error::New(env, std::string("Failed to save scene: ") + path).ThrowAsJavaScriptException();
    }
    return env.Undefined();
}

static Napi::Value GetSceneTree(const Napi::CallbackInfo& info) {
    const char* json = uz_get_scene_tree();
    if (!json) return info.Env().Null();
    return Napi::String::New(info.Env(), json);
}

// ── Entity ──────────────────────────────────────────────────────────────────

static Napi::Value CreateEntity(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "createEntity(name: string) expected").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    std::string name = info[0].As<Napi::String>().Utf8Value();
    uint32_t id = uz_create_entity(name.c_str());
    if (id == 0) {
        Napi::Error::New(env, std::string("Failed to create entity: ") + name).ThrowAsJavaScriptException();
        return env.Undefined();
    }
    return Napi::Number::New(env, id);
}

static Napi::Value DestroyEntity(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "destroyEntity(entityId: number) expected").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    uz_destroy_entity(static_cast<uint32_t>(info[0].As<Napi::Number>().Uint32Value()));
    return env.Undefined();
}

// ── Component Access ────────────────────────────────────────────────────────

static Napi::Value GetComponentJson(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 2 || !info[0].IsNumber() || !info[1].IsString()) {
        Napi::TypeError::New(env, "getComponentJson(entityId: number, componentType: string) expected")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    uint32_t entityId         = static_cast<uint32_t>(info[0].As<Napi::Number>().Uint32Value());
    std::string componentType = info[1].As<Napi::String>().Utf8Value();
    const char* json = uz_get_component_json(entityId, componentType.c_str());
    if (!json) return env.Null();
    return Napi::String::New(env, json);
}

static Napi::Value SetComponentProperty(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 4 ||
        !info[0].IsNumber() || !info[1].IsString() ||
        !info[2].IsString() || !info[3].IsString()) {
        Napi::TypeError::New(env,
            "setComponentProperty(entityId, componentType, property, valueJson) expected")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    uint32_t entityId         = static_cast<uint32_t>(info[0].As<Napi::Number>().Uint32Value());
    std::string componentType = info[1].As<Napi::String>().Utf8Value();
    std::string property      = info[2].As<Napi::String>().Utf8Value();
    std::string valueJson     = info[3].As<Napi::String>().Utf8Value();

    int ok = uz_set_component_property(entityId, componentType.c_str(),
                                       property.c_str(), valueJson.c_str());
    return Napi::Boolean::New(env, ok != 0);
}

// ── Registration ────────────────────────────────────────────────────────────

void RegisterSceneBindings(Napi::Env env, Napi::Object exports) {
    exports.Set("loadScene",            Napi::Function::New(env, LoadScene));
    exports.Set("saveScene",            Napi::Function::New(env, SaveScene));
    exports.Set("getSceneTree",         Napi::Function::New(env, GetSceneTree));
    exports.Set("createEntity",         Napi::Function::New(env, CreateEntity));
    exports.Set("destroyEntity",        Napi::Function::New(env, DestroyEntity));
    exports.Set("getComponentJson",     Napi::Function::New(env, GetComponentJson));
    exports.Set("setComponentProperty", Napi::Function::New(env, SetComponentProperty));
}
