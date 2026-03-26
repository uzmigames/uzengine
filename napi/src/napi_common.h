/// UzEngine N-API — Shared header for all binding modules.
///
/// Each napi_*.cpp file includes this to get runtime.h + napi.h + common types.
/// The Register*() functions are called from addon.cpp InitModule.
///
/// # SEH Protection
/// All N-API bindings that call into the engine runtime MUST use the
/// UZ_NAPI_SEH_WRAP macro. This catches Windows structured exceptions
/// (access violations, stack overflows, GPU device-removed) and converts
/// them to JavaScript exceptions instead of crashing the Extension Host.

#pragma once

#include "runtime.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <napi.h>

#include <cstdint>
#include <cstdio>
#include <string>

/// SEH-safe wrapper for calling engine C API functions.
///
/// MSVC `__try/__except` cannot coexist with C++ objects that have destructors
/// in the same function scope. This plain-C helper wraps a function call in SEH
/// and returns the exception code (0 = success).
///
/// Use the inline wrappers below (uz_seh_render_frame, etc.) from N-API bindings.
/// If the call crashes, the return value contains the Windows exception code, and
/// the crash handler (if installed) writes minidump + report files.
#ifdef _WIN32

/// Calls uz_render_frame() inside SEH. Returns exception code (0 = OK).
/// On crash, `out_stats` is zeroed.
inline DWORD uz_seh_render_frame(uz_frame_stats* out_stats) {
    __try {
        *out_stats = uz_render_frame();
        return 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        memset(out_stats, 0, sizeof(*out_stats));
        return GetExceptionCode();
    }
}

/// Calls uz_create_viewport() inside SEH. Returns exception code (0 = OK).
/// On crash, `out_result` is set to 0 (failure).
inline DWORD uz_seh_create_viewport(void* parent_hwnd, int w, int h, int* out_result) {
    __try {
        *out_result = uz_create_viewport(parent_hwnd, w, h);
        return 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        *out_result = 0;
        return GetExceptionCode();
    }
}

/// Calls uz_capture_screenshot() inside SEH. Returns exception code (0 = OK).
inline DWORD uz_seh_capture_screenshot(uint8_t** out_data, uint32_t* out_size, int* out_result) {
    __try {
        *out_result = uz_capture_screenshot(out_data, out_size);
        return 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        *out_data = nullptr;
        *out_size = 0;
        *out_result = 0;
        return GetExceptionCode();
    }
}

/// Calls uz_runtime_init() inside SEH. Returns exception code (0 = OK).
inline DWORD uz_seh_runtime_init(int* out_result) {
    __try {
        *out_result = uz_runtime_init();
        return 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        *out_result = 0;
        return GetExceptionCode();
    }
}

/// Helper: checks SEH code and throws JS error if non-zero.
/// Returns true if a crash was detected (caller should return early).
inline bool UzNapiCheckSeh(Napi::Env env, DWORD sehCode) {
    if (sehCode == 0) return false;
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        "UzEngine native crash: SEH exception 0x%08lX. Check native/crashes/ for details.",
        sehCode);
    Napi::Error::New(env, buf).ThrowAsJavaScriptException();
    return true;
}

/// Calls uz_encode_frame() inside SEH. Returns exception code (0 = OK).
inline DWORD uz_seh_encode_frame(const uint8_t** out_data, uint32_t* out_size, int* out_result) {
    __try {
        *out_result = uz_encode_frame(out_data, out_size);
        return 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        *out_data = nullptr;
        *out_size = 0;
        *out_result = 0;
        return GetExceptionCode();
    }
}

/// Calls uz_get_codec_extradata() inside SEH. Returns exception code (0 = OK).
inline DWORD uz_seh_get_codec_extradata(const uint8_t** out_data, uint32_t* out_size, int* out_result) {
    __try {
        *out_result = uz_get_codec_extradata(out_data, out_size);
        return 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        *out_data = nullptr;
        *out_size = 0;
        *out_result = 0;
        return GetExceptionCode();
    }
}

#endif // _WIN32

// ── Module registration (called from addon.cpp) ─────────────────────────────

void RegisterLifecycleBindings(Napi::Env env, Napi::Object exports);
void RegisterViewportBindings(Napi::Env env, Napi::Object exports);
void RegisterSceneBindings(Napi::Env env, Napi::Object exports);
void RegisterInputBindings(Napi::Env env, Napi::Object exports);
void RegisterDiagnosticsBindings(Napi::Env env, Napi::Object exports);
