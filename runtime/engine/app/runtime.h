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
// When building the shared library:  compile with -DUZ_BUILDING_DLL
// When consuming the shared library: compile with -DUZ_USING_DLL
// When building the static library:  neither define is set → UZ_API is empty
#if defined(UZ_BUILDING_DLL)
#  define UZ_API __declspec(dllexport)
#elif defined(UZ_USING_DLL)
#  define UZ_API __declspec(dllimport)
#else
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

/// Initializes the UzEngine runtime.
///
/// Sets up the logging system and global engine state. Must be called before
/// any other uz_* function (except uz_runtime_version).
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

#ifdef __cplusplus
}
#endif
