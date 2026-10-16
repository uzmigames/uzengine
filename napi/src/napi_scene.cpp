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
    auto ok = UzNapiOnOwner(env, "loadScene", [path] { return uz_load_scene(path.c_str()); },
                            std::chrono::seconds(30));
    if (!ok) return env.Undefined();
    if (!*ok) {
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
    auto ok = UzNapiOnOwner(env, "saveScene", [path] { return uz_save_scene(path.c_str()); },
                            std::chrono::seconds(30));
    if (!ok) return env.Undefined();
    if (!*ok) {
        Napi::Error::New(env, std::string("Failed to save scene: ") + path).ThrowAsJavaScriptException();
    }
    return env.Undefined();
}

static Napi::Value GetSceneTree(const Napi::CallbackInfo& info) {
    auto json = UzNapiOnOwner(info.Env(), "getSceneTree", [] {
        return UzNapiCopyString(uz_get_scene_tree());
    });
    if (!json) return info.Env().Undefined();
    if (!*json) return info.Env().Null();
    return Napi::String::New(info.Env(), **json);
}

// ── Entity ──────────────────────────────────────────────────────────────────

static Napi::Value CreateEntity(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "createEntity(name: string) expected").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    std::string name = info[0].As<Napi::String>().Utf8Value();
    auto result = UzNapiOnOwner(env, "createEntity", [name] { return uz_create_entity(name.c_str()); });
    if (!result) return env.Undefined();
    uint32_t id = *result;
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
    const uint32_t id = info[0].As<Napi::Number>().Uint32Value();
    UzNapiOnOwner(env, "destroyEntity", [id] { uz_destroy_entity(id); });
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
    auto json = UzNapiOnOwner(env, "getComponentJson", [entityId, componentType] {
        return UzNapiCopyString(uz_get_component_json(entityId, componentType.c_str()));
    });
    if (!json) return env.Undefined();
    if (!*json) return env.Null();
    return Napi::String::New(env, **json);
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

    auto ok = UzNapiOnOwner(env, "setComponentProperty", [entityId, componentType, property, valueJson] {
        return uz_set_component_property(entityId, componentType.c_str(),
                                         property.c_str(), valueJson.c_str());
    });
    if (!ok) return env.Undefined();
    return Napi::Boolean::New(env, *ok != 0);
}

static Napi::Value GetEntityComponents(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "getEntityComponents(entityId: number) expected")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    uint32_t entityId = static_cast<uint32_t>(info[0].As<Napi::Number>().Uint32Value());
    auto json = UzNapiOnOwner(env, "getEntityComponents", [entityId] {
        return UzNapiCopyString(uz_get_entity_components(entityId));
    });
    if (!json) return env.Undefined();
    if (!*json) return env.Null();
    return Napi::String::New(env, **json);
}

// ── Selection ──────────────────────────────────────────────────────────────

static Napi::Value SelectEntity(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "selectEntity(entityId: number) expected")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    const uint32_t id = info[0].As<Napi::Number>().Uint32Value();
    UzNapiOnOwner(env, "selectEntity", [id] { uz_select_entity(id); });
    return env.Undefined();
}

static Napi::Value GetSelectedEntity(const Napi::CallbackInfo& info) {
    auto id = UzNapiOnOwner(info.Env(), "getSelectedEntity", [] { return uz_get_selected_entity(); });
    if (!id) return info.Env().Undefined();
    return Napi::Number::New(info.Env(), *id);
}

// ── Play Mode ──────────────────────────────────────────────────────────────

static Napi::Value Play(const Napi::CallbackInfo& info) {
    auto ok = UzNapiOnOwner(info.Env(), "play", [] { return uz_play(); });
    if (!ok) return info.Env().Undefined();
    return Napi::Boolean::New(info.Env(), *ok != 0);
}

static Napi::Value Stop(const Napi::CallbackInfo& info) {
    auto ok = UzNapiOnOwner(info.Env(), "stop", [] { return uz_stop(); });
    if (!ok) return info.Env().Undefined();
    return Napi::Boolean::New(info.Env(), *ok != 0);
}

static Napi::Value IsPlaying(const Napi::CallbackInfo& info) {
    auto playing = UzNapiOnOwner(info.Env(), "isPlaying", [] { return uz_is_playing(); });
    if (!playing) return info.Env().Undefined();
    return Napi::Boolean::New(info.Env(), *playing != 0);
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
    exports.Set("getEntityComponents",  Napi::Function::New(env, GetEntityComponents));
    exports.Set("selectEntity",         Napi::Function::New(env, SelectEntity));
    exports.Set("getSelectedEntity",    Napi::Function::New(env, GetSelectedEntity));
    exports.Set("play",                 Napi::Function::New(env, Play));
    exports.Set("stop",                 Napi::Function::New(env, Stop));
    exports.Set("isPlaying",            Napi::Function::New(env, IsPlaying));
}
