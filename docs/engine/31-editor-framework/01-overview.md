# Editor Framework — VSCode Integration, Gizmos, Selection, and Undo/Redo

> **Status**: Partial (v0.3.0+ — N-API addon, VSCode extension, viewport HWND, IPC dispatch, play/stop mode. Hierarchy/Inspector/Gizmos planned)
> **Source**: `editor/`, extension code in VSCode extension project
> **References**: UE5 FEditorViewportClient, Godot EditorPlugin + EditorInspectorPlugin, Blueprint editor interaction patterns

## Purpose

The Editor Framework provides the VSCode extension integration layer: viewports with 3D gizmos, scene selection and manipulation, undo/redo command pattern, property inspector generation, scene hierarchy view, asset browsing, and editor preferences. This layer bridges the runtime (N-API .node addon) with VSCode webview panels and native HWND viewport.

## Architecture

```
VSCode Extension (TypeScript)
    │
    ├── editor/webview/              — Panel UIs (hierarchy, inspector, asset browser)
    │   ├── hierarchy.ts              — Scene tree, drag-drop reparenting, lock/hide toggles
    │   ├── inspector.ts              — Property grid (auto-generated from §21 Property System)
    │   ├── asset_browser.ts          — Asset search, thumbnails, drag-drop to viewport
    │   └── preferences.ts            — Layout, keybindings, recent files
    │
    ├── editor/viewport/              — HWND 3D Viewport (native)
    │   ├── viewport_client.ts        — Manages scene rendering, input dispatch
    │   ├── gizmo_system.ts           — Translate/Rotate/Scale gizmo rendering & interaction
    │   ├── selection.ts              — Raycast picking, box/lasso select, multi-select (Ctrl/Shift)
    │   ├── viewport_overlays.ts      — Wireframe, collision, navmesh, light radius overlays
    │   └── viewport_preferences.ts   — Snap modes, transform space, viewport shading
    │
    ├── editor/commands/              — Command pattern & undo/redo
    │   ├── command_system.ts         — ICommand interface, execution + undo/redo
    │   ├── undo_stack.ts             — Transaction grouping, history navigation
    │   ├── transform_command.ts      — Move/Rotate/Scale commands
    │   ├── property_command.ts       — Property setter commands
    │   ├── scene_command.ts          — Create/Delete/Reparent/Clone entity commands
    │   └── multiselect_command.ts    — Batch commands on multiple entities
    │
    ├── editor/gizmo/                 — Gizmo logic & rendering
    │   ├── gizmo_types.ts            — Translate/Rotate/Scale gizmo geometry & hit-test
    │   ├── screen_space.ts           — Screen-space handle positioning (always visible size)
    │   ├── axis_constraint.ts        — Constrain movement to X/Y/Z axis or plane
    │   ├── snapping.ts               — Grid snap, surface snap, vertex snap, angle snap
    │   └── gizmo_renderer.ts         — Render gizmo handles with highlight/drag feedback
    │
    ├── editor/selection/             — Selection state & raycast picking
    │   ├── selection_context.ts      — Selected entities, current selection mode
    │   ├── raycast_picker.ts         — Raycast from camera through click point → hit test
    │   ├── box_select.ts             — Drag rectangle to select multiple entities
    │   └── lasso_select.ts           — Free-form selection polygon
    │
    └── editor/hot_reload/            — Runtime ↔ Editor sync
        ├── hot_reload_sync.ts        — IPC: entity state, transform changes, asset reloads
        └── state_preservation.ts     — Save/restore camera, selection, viewport state

Runtime (C++ .node addon)
    │
    └── editor/                       — Editor-only C++ code
        ├── editor_viewport.h/cpp     — Viewport window creation, input polling
        ├── editor_gizmo.h/cpp        — Gizmo rendering in C++ (D3D12 draw calls)
        ├── editor_icon.h/cpp         — CEditorIcon component (sprite gizmo, visible only in editor)
        ├── editor_bounds.h/cpp       — CEditorBounds component (show AABB in wireframe)
        └── editor_components.h       — All editor-only component types
```

## Viewport System

### Viewport Client

The viewport client manages the 3D HWND, renders the scene, dispatches input, and coordinates gizmos.

```cpp
/// Native D3D12 viewport window for editor.
class EditorViewportClient {
public:
    /// Create viewport HWND, initialize D3D12 swapchain.
    bool Initialize(HWND parent, int width, int height);

    /// Render scene to backbuffer with overlays, gizmos, debug shapes.
    void RenderFrame(const Scene& scene, const SelectionContext& selection);

    /// Set active camera (editor orbit, or attach to entity).
    void SetActiveCamera(const Camera* camera);

    /// Dispatch input to gizmo system, selection, and orbit controls.
    void OnMouseMove(int dx, int dy, bool left_down, bool right_down);
    void OnMouseDown(int x, int y, MouseButton button);
    void OnMouseUp(MouseButton button);
    void OnMouseWheel(int delta);  // Zoom orbit camera
    void OnKeyDown(Key key);
    void OnKeyUp(Key key);
};
```

### Gizmo System

Three-axis manipulation handles for transform editing. Uses screen-space scaling to remain constant-size at any distance.

```cpp
/// Enum specifying which gizmo is active.
enum class GizmoMode : uint8_t {
    None,
    Translate,      // Move along X/Y/Z axes or plane
    Rotate,         // Rotate around X/Y/Z axes
    Scale,          // Scale along X/Y/Z axes
};

/// Gizmo interaction result from mouse input.
struct GizmoInteraction {
    GizmoMode mode;
    uint8_t axis_mask;  // 1=X, 2=Y, 4=Z
    Transform delta;    // Change to apply to entity
    bool is_active;     // true if gizmo is being dragged
};

/// Render and test gizmo interaction for selected entity.
class GizmoSystem {
public:
    /// Update gizmo state based on mouse input and transform space.
    /// Returns interaction if gizmo is being manipulated.
    GizmoInteraction Update(Vec2 mouse_pos, const Transform& entity_transform);

    /// Render gizmo handles (call during viewport render).
    void Render(RHI::CommandList& cmd, const Transform& entity_transform);

    /// Set which transform mode (translate/rotate/scale).
    void SetMode(GizmoMode mode) { mode_ = mode; }

    /// Set space: Local, World, Screen.
    void SetTransformSpace(TransformSpace space) { space_ = space; }

private:
    GizmoMode mode_ = GizmoMode::Translate;
    TransformSpace space_ = TransformSpace::World;
    uint8_t active_axis_ = 0;  // Which axis is being dragged
};
```

## Selection System

### Raycast Picking

Single-click on viewport to select entities. Uses GPU raycast or CPU bounding volume intersection.

```cpp
/// Result of a single raycast pick.
struct PickResult {
    Entity hit_entity;
    float distance;      // Distance from camera to hit point
    Vec3 hit_position;   // World position of hit
};

/// Perform raycast from camera through viewport coordinates.
class RaycastPicker {
public:
    /// Cast ray from camera through normalized device coordinates (0–1, 0–1).
    /// Returns closest hit entity or null.
    PickResult PickSingle(const Scene& scene, const Camera& camera, Vec2 ndc);

    /// Pick all entities under a ray (unsorted).
    std::vector<PickResult> PickAll(const Scene& scene, const Camera& camera, Vec2 ndc);
};
```

### Multi-Select

Support Ctrl+Click for additive selection, Shift+Click for range selection, box drag for area select.

```cpp
/// Manages current selection state and selection modes.
class SelectionContext {
public:
    /// Replace selection with single entity.
    void Select(Entity e);

    /// Add entity to selection (Ctrl+Click).
    void AddToSelection(Entity e);

    /// Remove entity from selection.
    void RemoveFromSelection(Entity e);

    /// Toggle entity in selection.
    void ToggleSelection(Entity e);

    /// Replace selection with entities in box (screen coordinates).
    void BoxSelect(const Scene& scene, const Camera& camera, Rect screen_rect);

    /// Add/Remove entities within lasso polygon.
    void LassoSelect(const Scene& scene, const Camera& camera, std::vector<Vec2> polygon, bool add);

    /// Clear all selection.
    void ClearSelection();

    /// Get selected entities.
    const std::vector<Entity>& GetSelected() const { return selected_; }

    /// Fire event when selection changes (UI listens to update inspector).
    MulticastDelegate<void(const SelectionContext&)> OnSelectionChanged;

private:
    std::vector<Entity> selected_;
};
```

## Undo/Redo System

Command pattern with transaction grouping for multi-command operations.

```cpp
/// Base interface for undoable commands.
class ICommand {
public:
    virtual ~ICommand() = default;

    /// Apply the command (first execution).
    virtual bool Execute() = 0;

    /// Undo the command (revert to state before Execute).
    virtual void Undo() = 0;

    /// Redo the command (re-apply after Undo).
    virtual void Redo() { Execute(); }

    /// Optional: human-readable label for undo history ("Move entity", "Set property").
    virtual StringView GetLabel() const { return "Command"; }
};

/// Examples of concrete commands.

class TransformCommand : public ICommand {
public:
    TransformCommand(Entity e, const Transform& old_transform, const Transform& new_transform)
        : entity_(e), old_transform_(old_transform), new_transform_(new_transform) {}

    bool Execute() override {
        auto* transform = entity_.Get<TransformComponent>();
        if (!transform) return false;
        transform->Set(new_transform_);
        return true;
    }

    void Undo() override {
        auto* transform = entity_.Get<TransformComponent>();
        if (transform) transform->Set(old_transform_);
    }

    StringView GetLabel() const override { return "Move/Rotate/Scale"; }

private:
    Entity entity_;
    Transform old_transform_, new_transform_;
};

class CreateEntityCommand : public ICommand {
public:
    CreateEntityCommand(Scene& scene, StringView name)
        : scene_(scene), entity_name_(name), entity_(nullptr) {}

    bool Execute() override {
        entity_ = scene_.Create();
        if (entity_) entity_.Get<NameComponent>()->Set(entity_name_);
        return entity_ != nullptr;
    }

    void Undo() override {
        if (entity_) scene_.Destroy(entity_);
    }

    StringView GetLabel() const override { return "Create entity"; }

private:
    Scene& scene_;
    std::string entity_name_;
    Entity entity_;
};

/// Undo/Redo stack with transaction support.
class UndoStack {
public:
    /// Execute a command and push to undo stack.
    bool ExecuteCommand(std::unique_ptr<ICommand> cmd) {
        if (!cmd->Execute()) return false;

        undo_stack_.push(std::move(cmd));
        redo_stack_ = {};  // Clear redo on new command
        return true;
    }

    /// Undo last command.
    bool Undo() {
        if (undo_stack_.empty()) return false;
        auto cmd = std::move(undo_stack_.top());
        undo_stack_.pop();
        cmd->Undo();
        redo_stack_.push(std::move(cmd));
        return true;
    }

    /// Redo last undone command.
    bool Redo() {
        if (redo_stack_.empty()) return false;
        auto cmd = std::move(redo_stack_.top());
        redo_stack_.pop();
        cmd->Redo();
        undo_stack_.push(std::move(cmd));
        return true;
    }

    /// Group multiple commands into one undo step.
    class Transaction {
    public:
        Transaction(UndoStack& stack) : stack_(stack), commands_() {}
        ~Transaction() {
            if (!commands_.empty()) {
                auto group = std::make_unique<CompositeCommand>(std::move(commands_));
                stack_.ExecuteCommand(std::move(group));
            }
        }

        void Add(std::unique_ptr<ICommand> cmd) {
            commands_.push_back(std::move(cmd));
        }

    private:
        UndoStack& stack_;
        std::vector<std::unique_ptr<ICommand>> commands_;
    };

    Transaction BeginTransaction() { return Transaction(*this); }

private:
    std::stack<std::unique_ptr<ICommand>> undo_stack_, redo_stack_;
};
```

## Inspector Panel

Property grid auto-generated from the Property System (§21). Displays component properties, allows editing with live updates.

```cpp
/// Auto-generate property widgets from component reflection metadata.
class InspectorPanel {
public:
    /// Render property grid for entity.
    /// Generates rows from all components' Property::Descriptor metadata.
    void ShowEntity(Entity e);

    /// Render property grid for component.
    void ShowComponent(Entity e, Component* comp);

    /// Handle property value changes via inspector UI.
    /// Delegates to PropertyCommand for undo/redo.
    void OnPropertyChanged(Entity e, StringView component_type, StringView property_name, const Variant& new_value);
};
```

## Editor-Only Components

Components that exist only in editor builds and are stripped from shipped games.

```cpp
/// Editor gizmo icon (colored sphere with label).
/// Visible only when UZ_EDITOR_BUILD=1; stripped from shipping.
class CEditorIcon : public Component {
public:
    /// Icon color (RGBA).
    uint32_t color = 0xFFFFFF00u;

    /// Icon label (e.g., "Spawn Point", "Camera Waypoint").
    std::string label;

    /// Scale of icon sphere in world space.
    float scale = 1.0f;
};

/// Show axis-aligned bounding box in wireframe (collision debug).
class CEditorBounds : public Component {
public:
    /// Draw bounds in this color.
    uint32_t color = 0xFF00FF00u;  // green

    /// Type of bounds: AABB (box), sphere, capsule, convex.
    enum Type : uint8_t { AABB, Sphere, Capsule, Convex };
    Type type = AABB;
};
```

## Viewport Overlays

Visualization modes for debugging and level layout.

| Overlay Mode | Purpose |
|---|---|
| **Wireframe** | Show triangle wireframe over final render |
| **Collision** | Display physics shapes (rigid bodies, colliders) |
| **Navigation** | Show navmesh, waypoints, and path for selected AI |
| **Light Radius** | Wireframe sphere showing light influence radius |
| **Bounds** | AABB for all entities with CEditorBounds |

## Snapping Modes

Constrain movement, rotation, and scale to discrete increments.

```cpp
struct SnapSettings {
    /// Grid snap size (meters). 0 = disabled.
    float grid_snap = 0.25f;

    /// Angle snap (degrees). 0 = disabled.
    float angle_snap = 15.0f;

    /// Snap to nearest surface (raycast from gizmo). Expensive; off by default.
    bool surface_snap = false;

    /// Snap to nearest vertex of nearby meshes.
    bool vertex_snap = false;

    /// Enable all snapping modes.
    bool enable_snapping = true;
};
```

## Transform Space

Control whether gizmo operates in local or world coordinates.

```cpp
enum class TransformSpace : uint8_t {
    Local,      // Gizmo axes aligned to entity's local frame
    World,      // Gizmo axes fixed to world XYZ
    Screen,     // Gizmo axes aligned to camera view (less common)
};
```

## Comparison

| Feature | Unreal 5 | Godot 4 | Unity | **UzEngine** |
|---------|----------|---------|-------|--------------|
| Viewport | FEditorViewportClient (Slate) | EditorViewport3D (GDExtension) | SceneView (C#) | **EditorViewportClient (HWND + D3D12)** |
| Gizmos | FWidget + FAxis (screen-space, interactive) | Node3DGizmo (custom) | Handle.Slider, Handle.PositionHandle | **GizmoSystem (screen-space handles, axis constraints)** |
| Selection | Click + Marquee (Shift/Ctrl) | Click + Marquee | Click + Shift/Ctrl | **RaycastPicker + BoxSelect/LassoSelect** |
| Undo/Redo | FTransaction + Modify() | EditorUndoRedo (ref-counted) | Undo.RecordObject | **UndoStack + ICommand + Transaction** |
| Inspector | SDetailsView (auto-layout from UClass) | EditorInspectorPlugin (generated from @export) | PropertyDrawer (reflection) | **InspectorPanel (Property System §21)** |
| Hierarchy | World Outliner (tree + visibility toggles) | SceneTreeDock (drag-drop reparent) | Hierarchy window (drag-drop) | **HierarchyPanel (tree + lock/hide per-entity)** |

## Visual Node-Graph Editors

UzEngine includes a shared node-graph framework (§40.09) used by multiple specialized editors:

- **[Material Editor (§40.10)](10-material-editor.md)** — Visual material authoring via connected nodes (Math, Texture, Output)
- **[Animation Controller (§40.11)](11-animcontroller-editor.md)** — State machines with transitions and blend spaces
- **[Control Rig (§40.12)](12-controlrig-editor.md)** — IK chains and bone constraints for rigging
- **[Node-Graph Framework (§40.09)](09-node-graph-framework.md)** — Shared foundation (pan/zoom, virtual rendering, compilation pipeline, undo/redo)

## Specialized Visual Editors

Advanced editors for content creation and game logic:

- **[VFX Graph Editor (§40.13)](13-vfx-editor.md)** — Particle system authoring with spawn/physics/render modules, live preview, parameter curves
- **[Sequencer/Timeline Editor (§40.14)](14-sequencer-editor.md)** — Cinematic sequencing with camera cuts, skeletal animation, audio sync, possessables/spawnables
- **[Behavior Tree Editor (§40.15)](15-behavior-tree-editor.md)** — AI decision authoring with composites, decorators, tasks, blackboard debugging
- **[PCG Graph Editor (§40.16)](16-pcg-editor.md)** — Procedural world generation (scatter, filter, transform, output to foliage/meshes)
- **[Debug Tools (§40.17)](17-debug-tools.md)** — Asset preview (animation/texture/mesh/shader), physics/audio/navmesh visualization, performance HUD

See each editor's documentation for specialized features.

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 2** | Viewport client, basic gizmo (translate), selection (raycast pick), undo/redo, material graph foundation, animation controller basic states |
| **Phase 3** | Rotate/Scale gizmos, box/lasso select, inspector panel, multi-select with Ctrl/Shift, snapping, viewport overlays, hot-reload coordination, material editor live preview, animation blend spaces, control rig IK |
| **Phase 4** | Multi-viewport (split view), orthographic projections, editor preferences persistence, advanced selection tools, material functions, advanced constraints, retargeting |
