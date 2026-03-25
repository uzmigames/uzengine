# N-API Foundation — Spec Delta

## ADDED Requirements

### Requirement: Runtime C API Viewport Lifecycle
The runtime C API SHALL expose `uz_create_viewport(parent_hwnd, width, height)` that creates a DX12 device, swapchain, and SceneRenderer. When `parent_hwnd` is 0, a standalone borderless window MUST be created. When non-zero, the viewport HWND MUST be created as a child of the parent. The function MUST return 1 on success, 0 on failure.

#### Scenario: Create standalone viewport
Given no viewport exists
When `uz_create_viewport(0, 1280, 720)` is called
Then a DX12 device and swapchain are created
And a 1280x720 borderless window is visible
And the function returns 1

#### Scenario: Create child viewport
Given a valid parent HWND from VSCode
When `uz_create_viewport(parent_hwnd, 800, 600)` is called
Then a DX12 swapchain is created targeting a child HWND of the parent
And the function returns 1

#### Scenario: Double create rejected
Given a viewport already exists
When `uz_create_viewport(0, 800, 600)` is called
Then the function returns 0
And an error is logged

### Requirement: Frame Rendering
The runtime C API SHALL expose `uz_render_frame()` that renders one frame to the active viewport and returns a `uz_frame_stats` struct containing `fps`, `frame_ms`, `draw_calls`, `vertices`, and `triangles`.

#### Scenario: Render produces valid stats
Given a viewport exists with a loaded scene
When `uz_render_frame()` is called
Then the backbuffer is presented to the swapchain
And the returned stats have fps > 0 and frame_ms > 0

#### Scenario: Render with no scene
Given a viewport exists but no scene is loaded
When `uz_render_frame()` is called
Then a clear color is presented (no crash)
And draw_calls = 0

### Requirement: Scene Loading
The runtime C API SHALL expose `uz_load_scene(path)` that loads a `.uzscene` file via the VFS and SceneLoader. The function MUST return 1 on success, 0 on failure. Scene loading MUST be non-blocking for texture decode (async texture loading).

#### Scenario: Load valid scene
Given a viewport exists
When `uz_load_scene("scenes/pbr_showcase.uzscene")` is called
Then all entities from the scene are created in the World
And the next `uz_render_frame()` shows the scene content
And the function returns 1

#### Scenario: Load invalid path
Given a viewport exists
When `uz_load_scene("nonexistent.uzscene")` is called
Then the function returns 0
And an error is logged
And the current scene is unchanged

### Requirement: Scene Tree Query
The runtime C API SHALL expose `uz_get_scene_tree()` that returns a JSON string representing the entity hierarchy. Each entry MUST include `id` (uint32), `name` (string), and `children` (array). The string is valid until the next call to `uz_get_scene_tree()`.

#### Scenario: Query populated scene
Given a scene with 5 entities is loaded
When `uz_get_scene_tree()` is called
Then a valid JSON array with 5 top-level entries is returned
And each entry has `id`, `name`, and `children` fields

### Requirement: Entity Manipulation
The runtime C API SHALL expose `uz_create_entity(name)` returning the entity ID and `uz_destroy_entity(id)` removing the entity and all its components. Entity IDs MUST be stable within a session.

#### Scenario: Create and destroy
Given a scene is loaded
When `uz_create_entity("NewCube")` returns id=42
And `uz_destroy_entity(42)` is called
Then the entity no longer appears in `uz_get_scene_tree()`

### Requirement: Component Property Access
The runtime C API SHALL expose `uz_get_component_json(entity_id, component_type)` and `uz_set_component_property(entity_id, component_type, property, value_json)` for reading and writing component data. Component types are strings matching the serializer registry (`"transform"`, `"mesh"`, `"light"`, `"sky_atmosphere"`, etc).

#### Scenario: Read transform
Given entity 5 has a transform at position [1, 2, 3]
When `uz_get_component_json(5, "transform")` is called
Then the returned JSON contains `"position": [1, 2, 3]`

#### Scenario: Write transform property
Given entity 5 has a transform
When `uz_set_component_property(5, "transform", "position", "[10, 20, 30]")` is called
Then `uz_get_component_json(5, "transform")` returns position [10, 20, 30]
And the viewport reflects the new position on the next frame

### Requirement: Input Forwarding
The runtime C API SHALL expose `uz_on_mouse_move(x, y)`, `uz_on_mouse_button(button, pressed)`, `uz_on_key(keycode, pressed)`, and `uz_on_scroll(delta)` for forwarding input events from the extension to the runtime's InputSystem and camera controller.

#### Scenario: Mouse move updates camera
Given a viewport with an active FPS camera controller
When `uz_on_mouse_button(1, 1)` (right press) and `uz_on_mouse_move(100, 50)` are called
Then the camera yaw/pitch changes on the next frame

### Requirement: Log Callback
The runtime C API SHALL expose `uz_set_log_callback(fn)` that registers a function pointer invoked on every `UZ_LOG()` call. The callback receives log level (int), category (string), and message (string). Passing NULL MUST unregister the callback.

#### Scenario: Log callback receives messages
Given `uz_set_log_callback(my_fn)` is registered
When `uz_create_viewport(0, 800, 600)` is called
Then `my_fn` is invoked at least once with level=Info

### Requirement: N-API Async Scene Loading
The N-API bridge SHALL expose `loadScene(path)` as a Promise that resolves when the scene is loaded and all synchronous entity creation is complete. Async texture loading MAY continue after resolution.

#### Scenario: Promise resolves on success
Given the addon is initialized
When `await addon.loadScene("scenes/pbr_showcase.uzscene")` is called
Then the Promise resolves with `{ entityCount: N }` where N > 0

#### Scenario: Promise rejects on failure
Given the addon is initialized
When `await addon.loadScene("nonexistent.uzscene")` is called
Then the Promise rejects with an Error containing "failed to load"

### Requirement: N-API Log Streaming
The N-API bridge SHALL expose `setLogCallback(fn)` using `napi_create_threadsafe_function` so that log messages from any runtime thread are safely delivered to the JavaScript main thread.

#### Scenario: Threadsafe callback delivery
Given `addon.setLogCallback((level, category, msg) => { ... })` is registered
When the runtime's render thread calls `UZ_LOG(Renderer, Info, "test")`
Then the JavaScript callback is invoked on the main thread with category="Renderer" and message containing "test"

### Requirement: Extension Viewport Panel
The VSCode extension SHALL provide a `uzengine.openViewport` command that creates a WebViewPanel, extracts the panel's screen geometry, creates a native viewport via N-API, and starts a render loop. The viewport MUST resize when the panel resizes.

#### Scenario: Open viewport command
Given the extension is activated
When the user runs `uzengine.openViewport` from the command palette
Then a WebViewPanel titled "UzEngine Viewport" appears
And a native DX12 viewport renders content behind/over the panel

### Requirement: Extension Engine Mode
The VSCode extension SHALL provide a toggle between "Code Mode" (standard VSCode layout) and "Engine Mode" (viewport center, hierarchy left, inspector right, console bottom). The toggle MUST be accessible via status bar button, command palette, and `Ctrl+Shift+E`.

#### Scenario: Toggle to Engine Mode
Given VSCode is in Code Mode
When the user presses Ctrl+Shift+E
Then the layout changes to: viewport panel (center), scene tree (left sidebar), console (bottom panel)
And the status bar shows "Engine Mode"
