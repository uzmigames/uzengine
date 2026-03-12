# Editor IPC Protocol — Extension ↔ Runtime Communication

> **Status**: Planned (Phase 2)
> **Source**: `runtime/editor/ipc_handler.h`, `extension/editor/ipc_bridge.ts`
> **References**: Electron IPC, VSCode N-API bridge, Godot GDExtension RPC

## Purpose

IPC (Inter-Process Communication) bridges the VSCode extension (TypeScript) with the embedded runtime (C++). All editor commands, viewport interactions, scene operations, asset management, and telemetry flow through this protocol.

## Architecture

```
VSCode Extension (TypeScript)
    │
    ├─ User clicks button in webview panel
    │
    └─ Call runtime.call('method.name', { params })
        │
        └─ N-API Bridge (JavaScript ↔ C++)
            │
            ├─ Serialize to JSON
            ├─ Validate schema
            └─ Dispatch to handler
                │
                └─ Runtime Handler (C++)
                    │
                    ├─ Execute operation (load scene, create entity, etc.)
                    │
                    └─ Return response { success, data?, error? }
                        │
                        └─ Serialize back to JSON
                            │
                            └─ Deliver to extension TypeScript
                                │
                                └─ Update UI / scene state
```

## Message Categories

### 1. Scene Operations

Load, save, create, delete scenes.

```cpp
namespace SceneMessages {
    /// Load a .uzscene file into editor viewport.
    struct LoadScene {
        static constexpr StringView kMethod = "scene.load";
        std::string path;  // e.g., "res://levels/main.uzscene"
        // Response: { success: bool, error?: string }
    };

    /// Save current scene to file.
    struct SaveScene {
        static constexpr StringView kMethod = "scene.save";
        std::string path;
        bool save_as = false;  // true = choose location
        // Response: { success: bool, error?: string, saved_to?: string }
    };

    /// Undo last change.
    struct Undo {
        static constexpr StringView kMethod = "scene.undo";
        // Response: { success: bool, undo_label: string, redo_label: string }
    };

    /// Redo last undone change.
    struct Redo {
        static constexpr StringView kMethod = "scene.redo";
        // Response: { success: bool, undo_label: string, redo_label: string }
    };
}
```

### 2. Entity Operations

Create, delete, modify entities.

```cpp
namespace EntityMessages {
    /// Create new entity in scene.
    struct CreateEntity {
        static constexpr StringView kMethod = "entity.create";
        std::string name = "Entity";
        std::string parent_id;  // Optional parent entity ID
        nlohmann::json initial_properties;  // Components and data
        // Response: { success: bool, entity_id: string, error?: string }
    };

    /// Delete entity.
    struct DeleteEntity {
        static constexpr StringView kMethod = "entity.delete";
        std::string entity_id;
        // Response: { success: bool }
    };

    /// Duplicate entity (including all children and components).
    struct DuplicateEntity {
        static constexpr StringView kMethod = "entity.duplicate";
        std::string entity_id;
        // Response: { success: bool, new_entity_id: string }
    };

    /// Rename entity.
    struct RenameEntity {
        static constexpr StringView kMethod = "entity.rename";
        std::string entity_id;
        std::string new_name;
        // Response: { success: bool }
    };

    /// Get entity data (for hierarchy panel, inspector).
    struct GetEntity {
        static constexpr StringView kMethod = "entity.get";
        std::string entity_id;
        bool include_children = true;
        // Response: { success: bool, entity: { id, name, parent_id, children: [...], components: {...} }, error?: string }
    };
}
```

### 3. Selection & Hierarchy

Sync selected entities between viewport and editor.

```cpp
namespace SelectionMessages {
    /// Select entities in viewport (from extension).
    struct SelectEntities {
        static constexpr StringView kMethod = "selection.set";
        std::vector<std::string> entity_ids;
        bool multi_select = false;  // true = add to existing, false = replace
        // Response: { success: bool }
    };

    /// Query currently selected entities.
    struct GetSelection {
        static constexpr StringView kMethod = "selection.get";
        // Response: { entity_ids: string[], primary_id?: string }
    };

    /// Clear selection.
    struct ClearSelection {
        static constexpr StringView kMethod = "selection.clear";
        // Response: { success: bool }
    };

    /// Fired when user clicks entity in viewport.
    /// (Sent FROM runtime TO extension, not directly called)
    struct SelectionChanged {
        static constexpr StringView kEvent = "on_selection_changed";
        std::vector<std::string> entity_ids;
        std::string primary_id;  // Last selected
    };
}
```

### 4. Transform & Gizmo

Move, rotate, scale entities with gizmo feedback.

```cpp
namespace TransformMessages {
    /// Set entity transform (position, rotation, scale).
    struct SetTransform {
        static constexpr StringView kMethod = "entity.set_transform";
        std::string entity_id;
        Vec3 position;
        Quat rotation;
        Vec3 scale = Vec3(1, 1, 1);
        bool world_space = true;
        // Response: { success: bool, new_transform: { position, rotation, scale } }
    };

    /// Get entity transform.
    struct GetTransform {
        static constexpr StringView kMethod = "entity.get_transform";
        std::string entity_id;
        bool world_space = true;
        // Response: { position: [x,y,z], rotation: [x,y,z,w], scale: [x,y,z] }
    };

    /// Called frequently (60+ Hz) while dragging gizmo.
    /// Optimized for low latency.
    struct UpdateGizmoDrag {
        static constexpr StringView kMethod = "gizmo.drag";
        std::string entity_id;
        std::string gizmo_mode;  // "translate", "rotate", "scale"
        Vec3 delta;
        bool local_space = true;
        // Response: { success: bool, final_transform?: {...} }
    };

    /// Start gizmo interaction (capture initial state).
    struct BeginGizmoDrag {
        static constexpr StringView kMethod = "gizmo.begin_drag";
        std::string entity_id;
        std::string gizmo_mode;
        Vec3 initial_position;
        Quat initial_rotation;
        Vec3 initial_scale;
        // Response: { success: bool }
    };

    /// Finish gizmo interaction (apply final state, create undo).
    struct EndGizmoDrag {
        static constexpr StringView kMethod = "gizmo.end_drag";
        std::string entity_id;
        Vec3 final_position;
        Quat final_rotation;
        Vec3 final_scale;
        // Response: { success: bool, undo_label: string }
    };
}
```

### 5. Property Editing

Get/set component properties.

```cpp
namespace PropertyMessages {
    /// Get component from entity.
    struct GetComponent {
        static constexpr StringView kMethod = "component.get";
        std::string entity_id;
        std::string component_type;  // e.g., "MeshRenderer", "BoxCollider"
        // Response: { success: bool, component: { type, properties: {...} } }
    };

    /// Set component property (single value or batch).
    struct SetProperty {
        static constexpr StringView kMethod = "component.set_property";
        std::string entity_id;
        std::string component_type;
        std::string property_name;
        nlohmann::json value;
        // Response: { success: bool, new_value?: ..., error?: string }
    };

    /// Batch property update (multiple properties at once).
    struct SetProperties {
        static constexpr StringView kMethod = "component.set_properties";
        std::string entity_id;
        std::string component_type;
        std::map<std::string, nlohmann::json> properties;
        // Response: { success: bool, results: { property: { success, value?, error? }, ... } }
    };

    /// Add component to entity.
    struct AddComponent {
        static constexpr StringView kMethod = "component.add";
        std::string entity_id;
        std::string component_type;
        nlohmann::json initial_properties;  // Optional
        // Response: { success: bool, component: {...} }
    };

    /// Remove component from entity.
    struct RemoveComponent {
        static constexpr StringView kMethod = "component.remove";
        std::string entity_id;
        std::string component_type;
        // Response: { success: bool }
    };
}
```

### 6. Asset Operations

Import, list, get metadata about assets.

```cpp
namespace AssetMessages {
    /// List assets in directory.
    struct ListAssets {
        static constexpr StringView kMethod = "asset.list";
        std::string directory;  // e.g., "res://meshes/"
        std::string extension;  // e.g., ".fbx" (optional, empty = all)
        // Response: { success: bool, assets: [{ path, name, type, thumbnail_url? }, ...] }
    };

    /// Get asset metadata.
    struct GetAssetMetadata {
        static constexpr StringView kMethod = "asset.get_metadata";
        std::string asset_path;
        // Response: { success: bool, metadata: { type, size_bytes, import_settings: {...}, dependencies: [...] } }
    };

    /// Get thumbnail for asset (returns URL or base64).
    struct GetAssetThumbnail {
        static constexpr StringView kMethod = "asset.get_thumbnail";
        std::string asset_path;
        int width = 128;
        int height = 128;
        // Response: { success: bool, thumbnail_url: string }
    };

    /// Watch asset file for changes (triggers on-asset-changed event).
    struct WatchAsset {
        static constexpr StringView kMethod = "asset.watch";
        std::string asset_path;
        bool watch = true;
        // Response: { success: bool }
    };

    /// Event fired when watched asset changes.
    struct AssetChanged {
        static constexpr StringView kEvent = "on_asset_changed";
        std::string asset_path;
        std::string change_type;  // "modified", "deleted", "renamed"
    };
}
```

### 7. Viewport Control

Change viewport rendering, camera, screenshots.

```cpp
namespace ViewportMessages {
    /// Set viewport size (canvas resolution).
    struct SetViewportSize {
        static constexpr StringView kMethod = "viewport.set_size";
        uint32_t width, height;
        // Response: { success: bool }
    };

    /// Set viewport camera transform.
    struct SetCameraTransform {
        static constexpr StringView kMethod = "viewport.set_camera";
        Vec3 position;
        Quat rotation;
        float fov = 60.0f;
        // Response: { success: bool }
    };

    /// Get viewport camera state.
    struct GetCameraTransform {
        static constexpr StringView kMethod = "viewport.get_camera";
        // Response: { position: [x,y,z], rotation: [x,y,z,w], fov: 60.0 }
    };

    /// Switch viewport rendering mode.
    struct SetRenderMode {
        static constexpr StringView kMethod = "viewport.set_render_mode";
        std::string mode;  // "lit", "wireframe", "normals", "metallic", etc.
        // Response: { success: bool }
    };

    /// Capture viewport screenshot.
    struct TakeScreenshot {
        static constexpr StringView kMethod = "viewport.screenshot";
        bool include_ui = false;
        // Response: { success: bool, image_base64: string, error?: string }
    };

    /// Focus camera on entity/selection.
    struct FocusSelection {
        static constexpr StringView kMethod = "viewport.focus_selection";
        // Response: { success: bool }
    };

    /// Frame all entities in view.
    struct FrameAll {
        static constexpr StringView kMethod = "viewport.frame_all";
        // Response: { success: bool }
    };
}
```

### 8. Hot-Reload Notifications

Inform extension when assets/scripts change.

```cpp
namespace HotReloadMessages {
    /// Event: Asset was recompiled/reimported.
    struct AssetHotReloaded {
        static constexpr StringView kEvent = "on_asset_hot_reload";
        std::string asset_path;
        std::string asset_type;  // "shader", "texture", "mesh"
    };

    /// Event: Script/Lua file was reloaded.
    struct ScriptReloaded {
        static constexpr StringView kEvent = "on_script_reloaded";
        std::string script_path;
        bool success;
        std::string error;  // If !success
    };

    /// Event: Shader compiled successfully/failed.
    struct ShaderCompiled {
        static constexpr StringView kEvent = "on_shader_compiled";
        std::string shader_path;
        bool success;
        std::string error;  // If !success
    };
}
```

### 9. Error & Diagnostics

Report runtime errors to extension (which shows in VSCode Diagnostics API).

```cpp
namespace DiagnosticsMessages {
    /// Event: Runtime error occurred.
    struct RuntimeError {
        static constexpr StringView kEvent = "on_runtime_error";
        std::string message;
        std::string source_file;
        uint32_t line_number;
        std::string category;  // "Shader", "Script", "Physics", "Asset"
        std::string severity;  // "error", "warning", "info"
    };

    /// Query current errors (for Diagnostics panel).
    struct GetErrors {
        static constexpr StringView kMethod = "diagnostics.get_errors";
        std::string category;  // Optional filter
        // Response: { success: bool, errors: [{ message, file, line, severity }, ...] }
    };

    /// Clear errors for a file.
    struct ClearErrors {
        static constexpr StringView kMethod = "diagnostics.clear_errors";
        std::string file_path;
        // Response: { success: bool }
    };
}
```

## Protocol Design

### Reliability

All messages require a response within a timeout (default 5 seconds):

```cpp
class IPCHandler {
public:
    /// Send request and wait for response.
    /// Returns false if timeout or handler error.
    bool Call(StringView method, const nlohmann::json& params,
              nlohmann::json& out_response, uint32_t timeout_ms = 5000) {
        uint32_t request_id = next_request_id_++;
        auto [promise, future] = CreatePromise<nlohmann::json>();

        {
            std::lock_guard<std::mutex> lock(pending_requests_mutex_);
            pending_requests_[request_id] = promise;
        }

        // Send request to N-API bridge
        SendIPCMessage(method, request_id, params);

        // Wait for response with timeout
        auto status = future.wait_for(std::chrono::milliseconds(timeout_ms));
        if (status == std::future_status::timeout) {
            UZ_LOG(Editor, Error, "IPC timeout for method: {}", method);
            return false;
        }

        try {
            out_response = future.get();
            return true;
        } catch (const std::exception& e) {
            UZ_LOG(Editor, Error, "IPC error: {}", e.what());
            return false;
        }
    }

private:
    uint32_t next_request_id_ = 1;
    std::map<uint32_t, std::promise<nlohmann::json>> pending_requests_;
    std::mutex pending_requests_mutex_;
};
```

### Batching

Group multiple related operations into a single IPC call to reduce latency:

```typescript
// extension/editor/ipc_batch.ts

class IPCBatcher {
    private batch: Array<{ method: string; params: any }> = [];
    private batchTimer: NodeJS.Timeout | null = null;

    /// Queue operation for batching.
    queue(method: string, params: any) {
        this.batch.push({ method, params });

        // Auto-flush after 5ms (typical frame time)
        if (!this.batchTimer) {
            this.batchTimer = setTimeout(() => this.flush(), 5);
        }
    }

    /// Flush batched operations.
    async flush() {
        if (this.batch.length === 0) return;

        const batch = this.batch;
        this.batch = [];
        this.batchTimer = null;

        // Send as single IPC call
        const response = await this.runtimeBridge.call('batch.execute', {
            operations: batch,
        });

        // Process results
        for (let i = 0; i < response.results.length; i++) {
            // Handle individual result
        }
    }
}
```

Example: Setting multiple properties at once:

```cpp
struct BatchExecute {
    static constexpr StringView kMethod = "batch.execute";
    std::vector<nlohmann::json> operations;  // [{method, params}, ...]
    // Response: { success: bool, results: [{success, data?, error?}, ...] }
};
```

### Performance Optimization

#### Gizmo Drag Latency

Gizmo drag is the most latency-sensitive operation. Optimize by:
1. **Frequent polling** — Don't wait for scene to validate, update locally
2. **Predictive rendering** — Show expected position before server confirms
3. **Server prediction** — Runtime detects drag pattern and predicts next position

```typescript
// Fast-path gizmo drag:
let predictedTransform = currentTransform;

onMouseMove(deltaX: number, deltaY: number) {
    // Update UI immediately (predictive)
    predictedTransform = CalculateNewTransform(currentTransform, deltaX, deltaY);
    RenderViewport(predictedTransform);

    // Send to runtime (async)
    this.runtimeBridge.call('gizmo.drag', {
        entity_id: selectedEntity,
        delta: ScreenToDelta(deltaX, deltaY),
    }).then(response => {
        // Server confirms transform
        currentTransform = response.new_transform;
    });
}
```

#### Viewport Framerate

Frame streaming uses H.264 compression (see [28-ipc-protocol/](../28-ipc-protocol/)):

```cpp
// Runtime renders frame, encodes H.264, sends to webview
class ViewportFrameStreamer {
public:
    void RenderFrame() {
        // Render scene to backbuffer
        render_graph_.Execute();

        // Encode to H.264 (hardware-accelerated on D3D12)
        encoder_.EncodeFrame(backbuffer_);

        // Send H.264 NALUs to extension
        SendEncodedFrame(encoder_.GetLastFrame());
    }
};
```

## Message Format

All IPC messages are JSON:

```json
{
  "id": 42,
  "method": "entity.create",
  "params": {
    "name": "Player",
    "parent_id": "scene_root"
  }
}
```

Response:

```json
{
  "id": 42,
  "success": true,
  "data": {
    "entity_id": "entity_123",
    "name": "Player"
  }
}
```

Error response:

```json
{
  "id": 42,
  "success": false,
  "error": "Entity not found"
}
```

## Comparison

| Feature | UE5 (Monolithic) | Godot (GDExtension) | VSCode (Electron) | **UzEngine** |
|---|---|---|---|---|
| **Process model** | Single process | Single + extension process | Renderer + main process | **In-process N-API (no subprocess)** |
| **IPC latency** | ~0 (same process) | ~1-5ms (pipe IPC) | ~5-10ms (Electron IPC) | **~0.5-2ms (N-API in-process)** |
| **Frame streaming** | Monolithic viewport | Forwarding via plugin | WebCodecs (H.264) | **H.264 via WebCodecs (120 FPS)** |
| **Message protocol** | C++ objects (shared memory) | Variant serialization | JSON (slow) | **JSON (fast via V8 serializer)** |
| **Hot-reload support** | Native (same DLL) | Via GDExtension reload | Extension reload | **Asset watches + Lua reload events** |
| **Reliability** | Native crash = editor crash | Extension crash = standalone process | Tab crash, safe from main | **N-API crash = graceful error + recovery** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 2** | Scene load/save/undo/redo; Entity create/delete/rename; Transform get/set; Camera control; Viewport screenshots; Selection sync; Error reporting |
| **Phase 3** | Property editing (get/set/batch); Component add/remove; Asset metadata; Gizmo drag (low-latency); Hot-reload events (script, shader); Frame streaming |
| **Phase 4** | Asset import/cook pipeline; Batch operations; Selection optimization (multi-edit); Diagnostic panel integration; Plugin IPC extension system |

