# N-API Bridge — Function Registration and Typed Array Transfer

> **Status**: Planned (Phase 1–2)
> **Source**: `extension/ipc/napi_bridge.h`, `runtime/ipc/napi_dispatcher.cpp`
> **References**: Node-API (NAPI) v10, Electron N-API compatibility (electron#22799)

## Purpose

The N-API bridge translates between TypeScript extension code and C++ runtime code. It handles function registration (exposing C++ functions as async methods), argument marshaling (scalars, strings, buffers), return value serialization, error propagation, and callback patterns. This bridge is the low-level transport; the IPC protocol (§28-01) sits on top.

## Critical NAPI Rules

**MANDATORY for Electron compatibility:**

1. **`NAPI_DISABLE_CPP_EXCEPTIONS` required** — Electron crashes with exception handling enabled (electron#22799). Use C-style error returns.
2. **Include `${CMAKE_JS_SRC}`** — CMakeLists.txt must include this for delay-load hooks.
3. **Compile for correct Electron ABI** — `--runtime=electron --runtime-version=39.6.0` (matches VSCode 1.111.0).

## NAPI Types Cheat Sheet

```cpp
// Core NAPI types
napi_env env;                  // Execution environment (created per JS call)
napi_value value;              // Any JS value (number, string, object, array, function)
napi_ref reference;            // Persistent reference (survives after call returns)
napi_status status;            // Result code (napi_ok, napi_pending_exception, etc.)

// Common patterns:
napi_typeof(env, value, &type);  // Get type of value
napi_get_value_int32(env, value, &out_int);
napi_get_value_string_utf8(env, value, buffer, size, &result_len);
napi_get_value_external(env, value, &out_ptr);

// Create return values:
napi_create_int32(env, 42, &result);
napi_create_string_utf8(env, "hello", -1, &result);
napi_create_object(env, &result);
napi_create_array(env, &result);
napi_create_arraybuffer(env, size, &data, &result);

// Property access:
napi_set_property(env, obj, "key", value);
napi_get_property(env, obj, "key", &value);

// Call JS function:
napi_call_function(env, this_arg, func, argc, args, &result);
```

## Function Registration

Expose C++ functions as JavaScript async methods:

```cpp
/// Module exports (called once when .node addon loads).
/// This runs once per extension session.
napi_value InitModule(napi_env env, napi_value exports) {
    // Register all C++ functions accessible from TS

    napi_value scene_ns;
    napi_create_object(env, &scene_ns);

    // scene.load(path: string) → Promise<WorldHandle>
    napi_value scene_load_fn;
    NAPI_CALL(env, napi_create_function(
        env,
        "load",
        NAPI_AUTO_LENGTH,
        HandleSceneLoad,  // C++ function
        nullptr,          // context data
        &scene_load_fn
    ));
    NAPI_CALL(env, napi_set_property(env, scene_ns, "load", scene_load_fn));

    // scene.save(world: WorldHandle, path: string) → Promise<void>
    napi_value scene_save_fn;
    NAPI_CALL(env, napi_create_function(
        env, "save", NAPI_AUTO_LENGTH, HandleSceneSave, nullptr, &scene_save_fn
    ));
    NAPI_CALL(env, napi_set_property(env, scene_ns, "save", scene_save_fn));

    // exports.scene = { load, save, ... }
    NAPI_CALL(env, napi_set_property(env, exports, "scene", scene_ns));

    // Repeat for other namespaces (asset, physics, etc.)

    return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, InitModule)
```

## Async Function Handler Pattern

Most IPC calls are asynchronous (scene loading, asset compilation). Use async work queues:

```cpp
/// Async work item (C++ side).
struct AsyncWork {
    napi_async_work work = nullptr;
    napi_deferred deferred = nullptr;  // Resolve/reject JS Promise

    // Request data (copied from JS)
    std::string scene_path;
    std::string error_message;

    // Result data (populated by worker)
    WorldHandle world_handle = nullptr;
    bool success = false;
};

/// Handler for scene.load(path).
napi_value HandleSceneLoad(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value argv[1];
    NAPI_CALL(env, napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr));

    if (argc < 1) {
        napi_throw_error(env, "ERR_MISSING_ARG", "path required");
        return nullptr;
    }

    // Extract path string
    char path_buffer[256];
    size_t path_len;
    NAPI_CALL(env, napi_get_value_string_utf8(env, argv[0], path_buffer, 256, &path_len));

    // Create async work item
    auto* async_work = new AsyncWork();
    async_work->scene_path = std::string(path_buffer, path_len);

    // Create Promise
    NAPI_CALL(env, napi_create_promise(env, &async_work->deferred, &promise_value));

    // Queue async work
    NAPI_CALL(env, napi_create_async_work(
        env,
        nullptr,                 // async_resource
        napi_create_string_utf8(env, "scene.load", -1, nullptr),
        Execute_SceneLoad,       // Work function (runs in thread pool)
        Complete_SceneLoad,      // Completion callback (runs on JS thread)
        async_work,              // Context data
        &async_work->work
    ));

    NAPI_CALL(env, napi_queue_async_work(env, async_work->work));

    return promise_value;
}

/// Executed in worker thread (no NAPI calls allowed here).
void Execute_SceneLoad(napi_env env, void* data) {
    auto* work = static_cast<AsyncWork*>(data);

    try {
        // Load scene (blocking call, safe in worker thread)
        work->world_handle = LoadSceneFromPath(work->scene_path);
        work->success = (work->world_handle != nullptr);
    } catch (const std::exception& e) {
        work->error_message = e.what();
        work->success = false;
    }
}

/// Completion callback (runs on JS thread, NAPI calls allowed).
void Complete_SceneLoad(napi_env env, napi_status status, void* data) {
    auto* work = static_cast<AsyncWork*>(data);

    if (work->success) {
        // Resolve Promise with result
        napi_value world_handle;
        napi_create_external(env, work->world_handle, nullptr, nullptr, &world_handle);
        napi_resolve_deferred(env, work->deferred, world_handle);
    } else {
        // Reject Promise with error
        napi_value error;
        napi_create_error(env, nullptr, work->error_message.c_str(), &error);
        napi_reject_deferred(env, work->deferred, error);
    }

    napi_delete_async_work(env, work->work);
    delete work;
}
```

## Typed Array Transfer (Zero-Copy)

Large buffers (mesh vertex data, texture pixels, physics state) are passed as typed arrays without copying:

```cpp
/// Transfer vertex data via typed array (shared memory).
napi_value HandleMeshCreateFromVertices(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value argv[2];
    NAPI_CALL(env, napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr));

    // argv[0] = { name: string }
    // argv[1] = Float32Array (vertex positions)

    // Extract mesh name
    napi_value name_prop;
    napi_get_property(env, argv[0], "name", &name_prop);
    char name_buffer[256];
    size_t name_len;
    napi_get_value_string_utf8(env, name_prop, name_buffer, 256, &name_len);

    // Extract Float32Array (typed array = shared memory reference)
    bool is_typedarray;
    napi_is_typedarray(env, argv[1], &is_typedarray);
    if (!is_typedarray) {
        napi_throw_error(env, "ERR_INVALID_ARG", "Expected Float32Array");
        return nullptr;
    }

    // Get arraybuffer + offset
    napi_value arraybuffer;
    size_t byte_offset, byte_length;
    napi_get_typedarray_info(env, argv[1],
        nullptr,            // out_type
        &byte_length,       // out_length
        nullptr,            // out_data (don't use directly; use arraybuffer)
        &arraybuffer,       // out_arraybuffer
        &byte_offset        // out_offset
    );

    // Get raw pointer to arraybuffer data
    void* buffer_data;
    size_t buffer_size;
    napi_get_arraybuffer_info(env, arraybuffer, &buffer_data, &buffer_size);

    // Cast to typed array
    float* vertices = reinterpret_cast<float*>(
        reinterpret_cast<uint8_t*>(buffer_data) + byte_offset
    );
    size_t vertex_count = byte_length / sizeof(float) / 3;  // 3 floats per vertex

    // Create mesh (zero-copy: just reference the shared buffer)
    Mesh mesh;
    mesh.name = std::string(name_buffer, name_len);
    mesh.vertices = Span<Vec3>(reinterpret_cast<Vec3*>(vertices), vertex_count);

    // Return opaque handle
    napi_value handle;
    napi_create_external(env, new Mesh(mesh), MeshFinalize, nullptr, &handle);
    return handle;
}

void MeshFinalize(napi_env env, void* finalize_data, void* finalize_hint) {
    delete static_cast<Mesh*>(finalize_data);
}
```

### TypeScript Usage

```typescript
// C++ transfers shared memory as typed array
const meshData = new Float32Array([
    // Vertex positions (x, y, z per vertex)
    0, 0, 0,    // v0
    1, 0, 0,    // v1
    1, 1, 0     // v2
]);

const mesh = await ipc.call("mesh.create_from_vertices", [
    { name: "Triangle" },
    meshData  // Passed by reference, not copied
]);

// Modify meshData on TS side → C++ sees changes immediately
meshData[0] = 5;  // C++ mesh.vertices[0].x = 5
```

## Error Propagation

Runtime exceptions become JavaScript Promise rejections:

```cpp
/// Helper: throw NAPI error.
#define NAPI_CHECK(expr, error_code, message) \
    if (!(expr)) { \
        napi_throw_error(env, error_code, message); \
        return nullptr; \
    }

/// Safe wrapper: catch C++ exception, translate to NAPI error.
template <typename Fn>
napi_value NAPISafely(napi_env env, const Fn& fn) {
    try {
        return fn();
    } catch (const std::out_of_range& e) {
        napi_throw_error(env, "ERR_OUT_OF_RANGE", e.what());
        return nullptr;
    } catch (const std::invalid_argument& e) {
        napi_throw_error(env, "ERR_INVALID_ARG", e.what());
        return nullptr;
    } catch (const std::exception& e) {
        napi_throw_error(env, "ERR_RUNTIME", e.what());
        return nullptr;
    }
}

/// Usage:
napi_value HandleAssetLoad(napi_env env, napi_callback_info info) {
    return NAPISafely(env, [&] {
        char path[256];
        size_t path_len;
        napi_get_value_string_utf8(env, argv[0], path, 256, &path_len);

        auto asset = LoadAssetFromDisk(std::string(path, path_len));

        napi_value result;
        napi_create_external(env, new Asset(asset), AssetFinalize, nullptr, &result);
        return result;
    });
}
```

## Callback Pattern

Register persistent JavaScript callbacks that C++ invokes repeatedly:

```cpp
/// Register a callback to be invoked from C++.
napi_value HandleSetCallback(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value argv[2];
    napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);

    // argv[0] = callback name ("on_file_changed")
    // argv[1] = callback function

    char callback_name[64];
    size_t name_len;
    napi_get_value_string_utf8(env, argv[0], callback_name, 64, &name_len);

    bool is_function;
    napi_is_function(env, argv[1], &is_function);
    if (!is_function) {
        napi_throw_error(env, "ERR_INVALID_ARG", "Callback must be a function");
        return nullptr;
    }

    // Store reference to callback (survives after this call)
    auto callback_key = std::string(callback_name, name_len);
    napi_create_reference(env, argv[1], 1, &g_callbacks[callback_key]);

    return napi_create_undefined(env);
}

/// Call registered JavaScript callback from C++ (e.g., when file changes).
void InvokeCallback(const std::string& callback_name, const std::string& file_path) {
    auto it = g_callbacks.find(callback_name);
    if (it == g_callbacks.end()) return;

    // NOTE: Cannot call directly from worker thread. Must enqueue for JS thread.
    EnqueueCallbackInvocation(callback_name, file_path);
}

// On main JS thread (next event loop iteration):
void ProcessPendingCallbacks(napi_env env) {
    for (const auto& [name, path] : pending_callbacks) {
        auto it = g_callbacks.find(name);
        if (it == g_callbacks.end()) continue;

        napi_value callback;
        napi_get_reference_value(env, it->second, &callback);

        napi_value path_arg;
        napi_create_string_utf8(env, path.c_str(), -1, &path_arg);

        napi_value result;
        napi_call_function(env, napi_get_global(env), callback, 1, &path_arg, &result);
    }
    pending_callbacks.clear();
}
```

### TypeScript Side

```typescript
// Register callback
ipc.on("file_changed", (filePath: string) => {
    console.log(`File changed: ${filePath}`);
});

// C++ can now invoke this callback anytime via FileWatcher
// ipc.invokeCallback("file_changed", "shaders/pbr.hlsl");
```

## Memory Management

Critical: NAPI objects must be cleaned up or memory leaks result.

```cpp
/// Safe RAII wrapper for NAPI references.
class NAPIValueRef {
public:
    NAPIValueRef(napi_env env, napi_value value)
        : env_(env), ref_(nullptr) {
        napi_create_reference(env, value, 1, &ref_);
    }

    ~NAPIValueRef() {
        if (ref_) {
            napi_delete_reference(env_, ref_);
        }
    }

    napi_value Get() const {
        napi_value result;
        napi_get_reference_value(env_, ref_, &result);
        return result;
    }

    // Prevent copies
    NAPIValueRef(const NAPIValueRef&) = delete;
    NAPIValueRef& operator=(const NAPIValueRef&) = delete;

private:
    napi_env env_;
    napi_ref ref_;
};
```

## Macro Helpers

Reduce boilerplate with macros:

```cpp
/// Check NAPI call succeeded; throw if not.
#define NAPI_ASSERT(status, message) \
    if ((status) != napi_ok) { \
        napi_throw_error(env, nullptr, message); \
        return nullptr; \
    }

/// Check condition; throw error if false.
#define NAPI_CHECK(condition, code, message) \
    if (!(condition)) { \
        napi_throw_error(env, code, message); \
        return nullptr; \
    }

/// Helper: create Promise easily.
napi_value CreatePromise(napi_env env, napi_deferred* out_deferred) {
    napi_value promise;
    NAPI_ASSERT(napi_create_promise(env, out_deferred, &promise), "create_promise failed");
    return promise;
}

/// Helper: resolve Promise.
void ResolvePromise(napi_env env, napi_deferred deferred, napi_value result) {
    napi_resolve_deferred(env, deferred, result);
}

/// Helper: reject Promise with error string.
void RejectPromise(napi_env env, napi_deferred deferred, const std::string& error) {
    napi_value error_obj;
    napi_create_error(env, nullptr, error.c_str(), &error_obj);
    napi_reject_deferred(env, deferred, error_obj);
}
```

## Example: Full Handler

```cpp
/// Complete example: scene.save(world, path) → Promise<void>
struct SaveSceneWork : AsyncWork {
    WorldHandle world;
    std::string output_path;
};

napi_value HandleSceneSave(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value argv[2];
    napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);

    NAPI_CHECK(argc >= 2, "ERR_MISSING_ARG", "world and path required");

    // Extract world handle
    void* world_ptr;
    napi_get_value_external(env, argv[0], &world_ptr);
    WorldHandle world = static_cast<WorldHandle>(world_ptr);

    // Extract path
    char path_buf[256];
    size_t path_len;
    napi_get_value_string_utf8(env, argv[1], path_buf, 256, &path_len);

    // Create async work
    auto* work = new SaveSceneWork();
    work->world = world;
    work->output_path = std::string(path_buf, path_len);

    napi_value promise = CreatePromise(env, &work->deferred);

    napi_create_async_work(env, nullptr,
        napi_create_string_utf8(env, "scene.save", -1, nullptr),
        [](napi_env e, void* d) {  // Execute
            auto* w = static_cast<SaveSceneWork*>(d);
            try {
                SaveWorldToFile(w->world, w->output_path);
                w->success = true;
            } catch (const std::exception& ex) {
                w->error_message = ex.what();
            }
        },
        [](napi_env e, napi_status s, void* d) {  // Complete
            auto* w = static_cast<SaveSceneWork*>(d);
            if (w->success) {
                napi_value result;
                napi_create_undefined(e, &result);
                ResolvePromise(e, w->deferred, result);
            } else {
                RejectPromise(e, w->deferred, w->error_message);
            }
            napi_delete_async_work(e, w->work);
            delete w;
        },
        work,
        &work->work
    );

    napi_queue_async_work(env, work->work);
    return promise;
}
```

## Comparison

| Feature | Node.js Native | SWIG | node-gyp | **N-API** |
|---------|---|---|---|---|
| **Language** | C++ | C/C++ | C++ | C++ |
| **Bindings** | Manual | Auto-generated | Manual | Manual |
| **Version compat** | ABI per version | ABI per version | ABI per version | **Stable ABI** |
| **Performance** | Fast | Moderate | Fast | Fast |
| **Complexity** | High | Low | High | Medium |
| **Async support** | napi_async_work | Limited | Limited | **First-class** |
| **Electron compatible** | No (crash) | Untested | Maybe | **Yes (with care)** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 1** | Basic NAPI module init, function registration, typed array transfer, async work queues, error propagation |
| **Phase 1.5** | H.264 frame streaming via typed arrays, callback pattern, zero-copy buffer optimization |
| **Phase 2** | Full IPC dispatcher (all scene/asset/physics methods), memory management audit, NAPI helper library |
