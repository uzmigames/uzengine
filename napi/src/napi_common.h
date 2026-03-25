/// UzEngine N-API — Shared header for all binding modules.
///
/// Each napi_*.cpp file includes this to get runtime.h + napi.h + common types.
/// The Register*() functions are called from addon.cpp InitModule.

#pragma once

#include "runtime.h"

#include <napi.h>

#include <cstdint>
#include <string>

// ── Module registration (called from addon.cpp) ─────────────────────────────

void RegisterLifecycleBindings(Napi::Env env, Napi::Object exports);
void RegisterViewportBindings(Napi::Env env, Napi::Object exports);
void RegisterSceneBindings(Napi::Env env, Napi::Object exports);
void RegisterInputBindings(Napi::Env env, Napi::Object exports);
void RegisterDiagnosticsBindings(Napi::Env env, Napi::Object exports);
