/// UzEngine Runtime C API
///
/// This header defines the stable C interface exported by the UzEngine runtime.
/// All functions use C calling convention for compatibility with N-API bindings.
///
/// # Thread Safety
/// - `uz_runtime_init()` and `uz_runtime_shutdown()` are NOT thread-safe
/// - `uz_create_viewport()` and `uz_destroy_viewport()` are NOT thread-safe
/// - Call from the main thread only
/// - Frame rendering (`uz_render_frame()`) must be called from the thread that
///   created the viewport
///
/// # Memory Ownership
/// - Returned `const char*` strings point to internal static buffers that are
///   valid until the next call to the same function (single-buffer pattern).
///   Copy the string if you need to keep it.
/// - `uz_capture_screenshot()` allocates memory that the caller must free via
///   `uz_free_buffer()`.
///
/// # Lifecycle
/// ```c
/// uz_runtime_init();
/// uz_create_viewport(parent_hwnd, 1280, 720);
/// while (running) {
///     uz_frame_stats stats = uz_render_frame();
///     // ... use stats ...
/// }
/// uz_destroy_viewport();
/// uz_runtime_shutdown();
/// ```

#pragma once

#include <stdint.h>

// ─────────────────────────────────────────────────────────────────────────────
// DLL export/import macro
// ─────────────────────────────────────────────────────────────────────────────

/// Platform visibility macro for public API symbols.
///
/// Resolves to `__declspec(dllexport)` when building UzEngine as a shared
/// library (`-DUZ_BUILDING_DLL`), `__declspec(dllimport)` when consuming it
/// (`-DUZ_USING_DLL`), or empty for static library builds (the default).
///
/// # Usage
/// ```cpp
/// UZ_API void MyPublicFunction();
/// ```
#if defined(UZ_BUILDING_DLL)
/// DLL export/import visibility (building DLL — export).
#  define UZ_API __declspec(dllexport)
#elif defined(UZ_USING_DLL)
/// DLL export/import visibility (consuming DLL — import).
#  define UZ_API __declspec(dllimport)
#else
/// DLL export/import visibility (static build — no-op).
#  define UZ_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Types
// ─────────────────────────────────────────────────────────────────────────────

/// Frame statistics returned by uz_render_frame().
///
/// All fields are zero if no viewport is active or if the frame was skipped
/// (e.g., window minimized).
typedef struct uz_frame_stats {
    float    fps;           ///< Smoothed frames-per-second (1.0 / frame_ms * 1000).
    float    frame_ms;      ///< Wall-clock frame time in milliseconds.
    uint32_t draw_calls;    ///< Number of draw calls submitted this frame.
    uint32_t vertices;      ///< Total vertices processed this frame.
    uint32_t triangles;     ///< Total triangles processed this frame.
    uint32_t frame_index;   ///< Monotonically increasing frame counter.
    uint32_t device_removed; ///< Non-zero if the GPU device was lost (fatal, unrecoverable).
} uz_frame_stats;

/// Log callback function type.
///
/// Called synchronously on the thread that produced the log message.
/// The callback must not call any uz_* functions (re-entrancy is not supported).
///
/// # Arguments
/// * `level` — Severity: 0=Trace, 1=Debug, 2=Info, 3=Warning, 4=Error, 5=Fatal.
/// * `category` — Subsystem name (e.g., "RHI", "Scene", "Engine"). Never null.
/// * `message` — Formatted log message. Never null.
typedef void (*uz_log_callback)(int level, const char* category, const char* message);

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

/// Returns the version string of the UzEngine runtime.
///
/// # Returns
/// A pointer to a null-terminated string (e.g., "0.7.0").
/// The string is statically allocated and never freed.
UZ_API const char* uz_runtime_version(void);

/// Returns the name of the active rendering backend.
///
/// # Returns
/// Backend identifier string ("dx12", "dx11", "vulkan", "metal", "none").
/// The string is statically allocated and never freed.
UZ_API const char* uz_runtime_backend(void);

/// Overrides the engine resource directory for DLL and shader discovery.
///
/// When the engine runs as a .node addon inside Electron, the host executable
/// is `Code.exe` (e.g., `F:/IDEs/Microsoft VS Code/`). GetModuleFileNameW
/// returns that directory, causing DLL loads for dxcompiler.dll, dxil.dll, and
/// D3D12/ to fail. Call this function with the addon's own directory
/// (e.g., the directory containing `uzengine.node`) before uz_runtime_init()
/// to redirect all resource discovery to the correct location.
///
/// Passing NULL or an empty string clears any previously set override.
///
/// # Arguments
/// * `path` — Absolute path to the directory that contains the engine DLLs
///   and shader artefacts. May be NULL or empty to clear the override.
///
/// # Safety
/// - Must be called before uz_runtime_init() and uz_create_viewport().
/// - NOT thread-safe: call from the main thread only.
UZ_API void uz_set_engine_dir(const char* path);

/// Initializes the UzEngine runtime.
///
/// Sets up the logging system and global engine state. Must be called before
/// any other uz_* function (except uz_runtime_version and uz_set_engine_dir).
///
/// # Returns
/// - 1 on success
/// - 0 on failure (check logs for details)
///
/// # Safety
/// - Must be called exactly once before any other runtime function
/// - NOT thread-safe: call from main thread only
/// - Failure does NOT require shutdown()
UZ_API int uz_runtime_init(void);

/// Shuts down the UzEngine runtime.
///
/// Destroys the viewport (if active), flushes GPU, releases all resources,
/// and shuts down the logging system.
///
/// # Safety
/// - Must be called exactly once if init() succeeded
/// - NOT thread-safe: call from main thread only
/// - Safe to call if init() failed or never called
UZ_API void uz_runtime_shutdown(void);

// ─────────────────────────────────────────────────────────────────────────────
// Viewport
// ─────────────────────────────────────────────────────────────────────────────

/// Creates a viewport with a GPU rendering pipeline.
///
/// Initializes the RHI device, swapchain, command list, per-frame fences,
/// SceneRenderer, World, VFS, and Lua scripting — the full rendering pipeline.
///
/// # Arguments
/// * `parent_hwnd` — Native HWND to create a child window inside. Pass NULL
///   (0) to create a standalone top-level window.
/// * `width` — Initial viewport width in pixels. Must be > 0.
/// * `height` — Initial viewport height in pixels. Must be > 0.
///
/// # Returns
/// - 1 on success (viewport ready for rendering)
/// - 0 on failure (device creation failed, etc. — check logs)
///
/// # Safety
/// - Only one viewport can be active at a time. Call uz_destroy_viewport()
///   before creating a new one.
/// - Returns 0 if a viewport already exists.
UZ_API int uz_create_viewport(void* parent_hwnd, int width, int height);

/// Destroys the viewport and releases all GPU resources.
///
/// Waits for GPU idle, shuts down SceneRenderer, Lua, fences, command list,
/// swapchain, and device. Safe to call if no viewport exists (no-op).
UZ_API void uz_destroy_viewport(void);

/// Resizes the viewport swapchain and all render targets.
///
/// # Arguments
/// * `width` — New width in pixels. Must be > 0.
/// * `height` — New height in pixels. Must be > 0.
///
/// No-op if no viewport exists or if dimensions match current size.
UZ_API void uz_resize_viewport(int width, int height);

/// Repositions and optionally resizes the viewport window.
///
/// Used when the viewport is a child of a VSCode panel that has been moved
/// or resized by the user.
///
/// # Arguments
/// * `x` — New X position in parent-client coordinates.
/// * `y` — New Y position in parent-client coordinates.
/// * `width` — New width in pixels. Must be > 0.
/// * `height` — New height in pixels. Must be > 0.
UZ_API void uz_set_viewport_position(int x, int y, int width, int height);

// ─────────────────────────────────────────────────────────────────────────────
// Frame Rendering
// ─────────────────────────────────────────────────────────────────────────────

/// Renders one frame and returns statistics.
///
/// Drives one iteration of the render loop: fence wait, timing update,
/// Lua script tick, SceneRenderer::RenderFrame, Present, fence signal.
///
/// # Returns
/// Frame statistics for the completed frame. All fields are zero if no
/// viewport is active.
///
/// # Safety
/// Must be called from the thread that created the viewport.
UZ_API uz_frame_stats uz_render_frame(void);

// ─────────────────────────────────────────────────────────────────────────────
// Scene Management
// ─────────────────────────────────────────────────────────────────────────────

/// Loads a .uzscene file into the World.
///
/// Clears the current scene and loads entities from the specified path.
/// The path is resolved through the active VFS (relative to content root).
///
/// # Arguments
/// * `path` — VFS-relative scene path (e.g., "scenes/main.uzscene").
///
/// # Returns
/// - 1 on success
/// - 0 on failure (file not found, parse error, no viewport — check logs)
UZ_API int uz_load_scene(const char* path);

/// Saves the current scene to a .uzscene file.
///
/// Serializes all entities in the World to JSON and writes to disk.
///
/// # Arguments
/// * `path` — Output file path (absolute or relative to working directory).
///
/// # Returns
/// - 1 on success
/// - 0 on failure (I/O error, no viewport — check logs)
UZ_API int uz_save_scene(const char* path);

/// Returns a JSON string describing the entity hierarchy.
///
/// The returned string is valid until the next call to uz_get_scene_tree().
/// Format:
/// ```json
/// { "entities": [
///   { "id": 42, "name": "Player", "children": [...] }
/// ]}
/// ```
///
/// # Returns
/// JSON string, or "{}" if no viewport/world exists.
UZ_API const char* uz_get_scene_tree(void);

/// Creates a new entity with a name.
///
/// # Arguments
/// * `name` — Human-readable name for the entity. Must not be null.
///
/// # Returns
/// Entity ID (slot index) on success. 0 indicates failure (no viewport).
UZ_API uint32_t uz_create_entity(const char* name);

/// Destroys an entity and all its components.
///
/// # Arguments
/// * `entity_id` — Entity slot index returned by uz_create_entity().
///
/// No-op if the entity does not exist or no viewport is active.
UZ_API void uz_destroy_entity(uint32_t entity_id);

/// Returns JSON describing a component attached to an entity.
///
/// The returned string is valid until the next call to uz_get_component_json().
///
/// # Arguments
/// * `entity_id` — Entity slot index.
/// * `component_type` — Component name: "transform", "mesh", "light", etc.
///
/// # Returns
/// JSON string with component data, or "{}" if not found / not implemented.
UZ_API const char* uz_get_component_json(uint32_t entity_id, const char* component_type);

/// Sets a property on a component attached to an entity.
///
/// # Arguments
/// * `entity_id` — Entity slot index.
/// * `component_type` — Component name (e.g., "transform").
/// * `property_name` — Property to set (e.g., "position").
/// * `value_json` — JSON-encoded value (e.g., "[1.0, 2.0, 3.0]").
///
/// # Returns
/// - 1 on success
/// - 0 on failure (entity not found, unknown component/property, parse error)
UZ_API int uz_set_component_property(uint32_t entity_id, const char* component_type,
                                     const char* property_name, const char* value_json);

/// Returns a JSON array of component type names attached to an entity.
///
/// # Arguments
/// * `entity_id` — Entity slot index.
///
/// # Returns
/// JSON array string, e.g. `["transform","mesh","material","light"]`.
/// Empty array `"[]"` if entity not found. Static buffer valid until next call.
UZ_API const char* uz_get_entity_components(uint32_t entity_id);

/// Selects an entity in the editor (outline + gizmo).
///
/// # Arguments
/// * `entity_id` — Entity slot index. Pass UINT32_MAX to deselect.
UZ_API void uz_select_entity(uint32_t entity_id);

/// Returns the currently selected entity ID, or UINT32_MAX if none selected.
UZ_API uint32_t uz_get_selected_entity(void);

// ─────────────────────────────────────────────────────────────────────────────
// Play Mode
// ─────────────────────────────────────────────────────────────────────────────

/// Enters play mode — switches from editor camera to game camera,
/// re-runs Lua on_start scripts, and hides editor overlays (grid, gizmos).
///
/// # Returns
/// 1 on success, 0 if no viewport or already in play mode.
UZ_API int uz_play(void);

/// Exits play mode — restores editor camera, stops game scripts,
/// and re-enables editor overlays.
///
/// # Returns
/// 1 on success, 0 if no viewport or not in play mode.
UZ_API int uz_stop(void);

/// Returns 1 if currently in play mode, 0 otherwise.
UZ_API int uz_is_playing(void);

// ─────────────────────────────────────────────────────────────────────────────
// Input Forwarding
// ─────────────────────────────────────────────────────────────────────────────

/// Forwards a mouse move event to the viewport's input system.
///
/// # Arguments
/// * `x` — Mouse X position in viewport-local pixels.
/// * `y` — Mouse Y position in viewport-local pixels.
UZ_API void uz_on_mouse_move(float x, float y);

/// Forwards a mouse button event to the viewport's input system.
///
/// # Arguments
/// * `button` — Button index: 0=Left, 1=Right, 2=Middle.
/// * `pressed` — 1 if button was pressed, 0 if released.
UZ_API void uz_on_mouse_button(int button, int pressed);

/// Forwards a key event to the viewport's input system.
///
/// # Arguments
/// * `keycode` — Virtual key code (Win32 VK_* values).
/// * `pressed` — 1 if key was pressed, 0 if released.
UZ_API void uz_on_key(int keycode, int pressed);

/// Forwards a keyboard event with modifier state from the editor.
///
/// When the engine viewport runs inside VSCode, the native HWND does not
/// receive WM_KEYDOWN for modifier keys because the editor host consumes
/// them.  This extended variant accepts a bitmask so the input system
/// reflects the true modifier state even without OS-level key messages.
///
/// # Arguments
/// * `keycode`   — Win32 VK_* virtual key code.
/// * `pressed`   — 1 = key down, 0 = key up.
/// * `modifiers` — Bitmask: bit 0 = Shift, bit 1 = Ctrl, bit 2 = Alt,
///                 bit 3 = Meta.
UZ_API void uz_on_key_ex(int keycode, int pressed, int modifiers);

/// Forwards a scroll wheel event to the viewport's input system.
///
/// # Arguments
/// * `delta` — Scroll amount (positive = up, negative = down).
UZ_API void uz_on_scroll(float delta);

// ─────────────────────────────────────────────────────────────────────────────
// Logging
// ─────────────────────────────────────────────────────────────────────────────

/// Registers a callback to receive log messages.
///
/// The callback is invoked synchronously on the logging thread. Only one
/// callback can be active at a time — calling this replaces any previous
/// callback. Pass NULL to unregister.
///
/// # Arguments
/// * `callback` — Function pointer, or NULL to unregister.
UZ_API void uz_set_log_callback(uz_log_callback callback);

// ─────────────────────────────────────────────────────────────────────────────
// Screenshot
// ─────────────────────────────────────────────────────────────────────────────

/// Captures the current frame as a PNG image in memory.
///
/// Allocates a buffer containing the PNG data. The caller MUST free the
/// buffer by calling uz_free_buffer() when done.
///
/// # Arguments
/// * `out_data` — Receives a pointer to the PNG data. Set to NULL on failure.
/// * `out_size` — Receives the size of the PNG data in bytes. Set to 0 on failure.
///
/// # Returns
/// - 1 on success
/// - 0 on failure (no viewport, GPU error — check logs)
UZ_API int uz_capture_screenshot(uint8_t** out_data, uint32_t* out_size);

/// Frees a buffer allocated by uz_capture_screenshot().
///
/// # Arguments
/// * `data` — Pointer returned by uz_capture_screenshot(). Passing NULL is safe.
UZ_API void uz_free_buffer(uint8_t* data);

// ─────────────────────────────────────────────────────────────────────────────
// H.264 Frame Encoding
// ─────────────────────────────────────────────────────────────────────────────

/// Encodes the most recently rendered frame as H.264.
///
/// After uz_render_frame() returns, call this to encode the frame's back
/// buffer as H.264 NAL units suitable for WebCodecs VideoDecoder. The output
/// includes Annex-B start codes and is ready for transport.
///
/// The output buffer is owned by the encoder and valid until the next call
/// to uz_encode_frame(). Do NOT free it — it is reused internally.
///
/// # Arguments
/// * `out_data` — Receives a pointer to the encoded H.264 NAL data.
/// * `out_size` — Receives the encoded data size in bytes.
///
/// # Returns
/// - 1 if a frame was produced
/// - 0 if the encoder buffered the frame or an error occurred
UZ_API int uz_encode_frame(const uint8_t** out_data, uint32_t* out_size);

/// Forces the next encoded frame to be an IDR keyframe.
///
/// Call when the WebCodecs decoder needs a clean reference point (e.g.,
/// after a panel is revealed or a decode error occurs).
UZ_API void uz_request_keyframe(void);

/// Updates the H.264 encoder bitrate for adaptive quality control.
///
/// Lower bitrate during high motion for faster encoding, higher on static
/// scenes for maximum quality. Takes effect gradually via the rate controller.
///
/// # Arguments
/// * `kbps` — Target bitrate in kilobits per second.
UZ_API void uz_set_encode_bitrate(uint32_t kbps);

/// Returns the codec description (extradata) needed by WebCodecs.
///
/// The extradata contains SPS/PPS NAL units that must be passed to the
/// VideoDecoder's configure() call as the `description` field. The buffer
/// is owned by the encoder and valid for the encoder's lifetime.
///
/// # Arguments
/// * `out_data` — Receives a pointer to the extradata bytes.
/// * `out_size` — Receives the extradata size in bytes.
///
/// # Returns
/// - 1 on success
/// - 0 if the encoder is not initialized
UZ_API int uz_get_codec_extradata(const uint8_t** out_data, uint32_t* out_size);

// ─────────────────────────────────────────────────────────────────────────────
// Editor Configuration
// ─────────────────────────────────────────────────────────────────────────────

/// Enables or disables editor mode.
///
/// When enabled, the viewport shows editor overlays: grid, stats, gizmos.
/// When disabled, the viewport shows only the game view.
///
/// # Arguments
/// * `enabled` — 1 to enable editor mode, 0 to disable.
UZ_API void uz_set_editor_mode(int enabled);

/// Sets the content root path for the VFS.
///
/// All scene/asset paths are resolved relative to this root. Must be called
/// before uz_load_scene() if using a non-default content root.
///
/// # Arguments
/// * `path` — Absolute path to the content directory.
UZ_API void uz_set_content_root(const char* path);

// ─────────────────────────────────────────────────────────────────────────────
// VFX scalability + sequencer hooks
// ─────────────────────────────────────────────────────────────────────────────
//
// All four functions resolve the target emitter via
// `VFXSystem::FindEmitterByName`. `name` is matched against
// `ParticleEmitter::name`; emitters without a name are unreachable from
// this API. Each function is a no-op (logged as warning) when the runtime
// has no active viewport / VFXSystem or when no emitter matches `name`.

/// Requests a deterministic re-warm of the named emitter to `time_seconds`.
///
/// The next sim tick resets the emitter's GPU state and replays it from
/// t=0 to the target via fixed-rate sub-ticks. See
/// `ParticleEmitter::Seek` for the exact semantics. Negative
/// `time_seconds` clamps to 0 (full reset, no warmup).
///
/// # Arguments
/// * `name` — Stable identifier matched against `ParticleEmitter::name`.
///            Must be a non-null, null-terminated UTF-8 string.
/// * `time_seconds` — Replay duration. Clamped to >= 0 internally.
///
/// # Returns
/// - 1 if the emitter was found and the seek was queued.
/// - 0 otherwise (no runtime, no VFXSystem, or no matching emitter).
UZ_API int uz_vfx_seek(const char* name, float time_seconds);

/// Pauses the named emitter's simulation.
///
/// While paused, the emitter's existing alive particles freeze in place
/// (sim dt is forced to 0) but the render path keeps drawing them.
/// Independent from emission state — to also stop spawning new particles
/// the caller should additionally toggle `isEmitting` via Lua.
///
/// # Arguments
/// * `name` — Emitter name.
///
/// # Returns
/// - 1 if the emitter was found and paused (or was already paused).
/// - 0 otherwise.
UZ_API int uz_vfx_pause(const char* name);

/// Resumes the named emitter's simulation.
///
/// Inverse of `uz_vfx_pause`. Idempotent — calling on an already-running
/// emitter is a no-op success.
UZ_API int uz_vfx_resume(const char* name);

/// Sets the per-emitter dt multiplier.
///
/// 1.0 = real-time (default). 0.5 = half-speed. 2.0 = double-speed.
/// Negative values clamp to 0. Applied AFTER the global `r.VFX.TimeScale`
/// so a sequencer can slow a specific emitter against a global slow-mo
/// backdrop, or fast-forward an effect while everything else runs at
/// normal speed.
///
/// # Arguments
/// * `name` — Emitter name.
/// * `scale` — New time-scale value. Clamped to [0, +inf).
///
/// # Returns
/// - 1 if the emitter was found and the time-scale was applied.
/// - 0 otherwise.
UZ_API int uz_vfx_set_time_scale(const char* name, float scale);

#ifdef __cplusplus
}
#endif
