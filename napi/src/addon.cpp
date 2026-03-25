/// UzEngine N-API Addon — Module entry point.
///
/// Delegates all bindings to domain-specific modules:
///   napi_lifecycle.cpp    — version, backend, init, shutdown
///   napi_viewport.cpp     — viewport create/destroy/resize, render, screenshot
///   napi_scene.cpp        — scene load/save, entity CRUD, component access
///   napi_input.cpp        — mouse, keyboard, scroll forwarding
///   napi_diagnostics.cpp  — log callback, editor mode, content root

#include "napi_common.h"

Napi::Object InitModule(Napi::Env env, Napi::Object exports) {
    RegisterLifecycleBindings(env, exports);
    RegisterViewportBindings(env, exports);
    RegisterSceneBindings(env, exports);
    RegisterInputBindings(env, exports);
    RegisterDiagnosticsBindings(env, exports);
    return exports;
}

NODE_API_MODULE(uzengine, InitModule)
