# Editor Gizmos + Selection — Spec Delta

## ADDED Requirements

### Requirement: Editor Mode Gate
The editor systems (gizmos, selection, outline) SHALL only be active when `SceneRenderer::IsEditorMode()` returns true. When editorMode is false, no editor-specific rendering or input handling SHALL occur.

#### Scenario: Editor mode disabled
Given the engine is running with editorMode=false
When the user clicks on an entity in the viewport
Then no selection occurs and no outline renders

#### Scenario: Editor mode enabled via CLI
Given the engine is started with `--editor` flag
When the scene loads
Then the editor grid, selection system, and gizmo renderer are initialized

### Requirement: Entity Selection via Mouse Click
The SelectionManager SHALL support single-entity selection via mouse left-click. A world-space ray is constructed from the mouse position using the inverse view-projection matrix. The ray is tested against all entities with SceneComponent + StaticMeshComponent using their world-space AABB. The nearest hit entity is selected.

#### Scenario: Select entity by clicking
Given editorMode is true and a cube entity exists at world position (0, 0, 100)
When the user left-clicks on the cube in the viewport
Then SelectionManager::GetSelected() returns the cube entity

#### Scenario: Click empty space deselects
Given an entity is currently selected
When the user left-clicks on empty space (no entity hit)
Then SelectionManager::GetSelected() returns an invalid entity

#### Scenario: Nearest entity selected when overlapping
Given two entities overlap in screen space (entity A at depth 100, entity B at depth 200)
When the user left-clicks on the overlapping area
Then entity A (nearer) is selected

### Requirement: Selection Outline Rendering
The engine SHALL render an orange outline around the selected entity using a stencil-based post-process technique. The outline width SHALL be approximately 2-3 pixels. The outline color SHALL default to #FF8C00 (UE5 orange) and be configurable via CVar `r.Editor.OutlineColor`.

#### Scenario: Outline renders for selected entity
Given entity A is selected in editor mode
When the frame renders
Then a 2-3px orange outline is visible around entity A's silhouette

#### Scenario: No outline when nothing selected
Given no entity is selected
When the frame renders
Then no outline is visible

### Requirement: Transform Gizmo Rendering
The GizmoRenderer SHALL draw a 3D manipulator widget at the selected entity's world position. Three gizmo modes MUST be supported: Translate (arrows), Rotate (circles), Scale (cubes on axes). Each mode displays 3 axis handles colored Red=X, Green=Y, Blue=Z. The gizmo SHALL maintain constant screen-space size (approximately 120px) regardless of camera distance.

#### Scenario: Translate gizmo displays
Given the gizmo mode is Translate and entity A is selected
When the frame renders
Then three colored arrows (R/G/B = X/Y/Z) render at entity A's position

#### Scenario: Gizmo screen-space size is constant
Given the camera moves from 5m to 50m distance from the selected entity
When the frame renders at both distances
Then the gizmo appears the same screen-space size (~120px)

### Requirement: Gizmo Interaction — Translate
When the user clicks and drags a translate gizmo axis handle, the selected entity SHALL move along that axis in world space (or local space if local mode active). The movement distance SHALL correspond to the mouse delta projected onto the axis. Plane handles SHALL constrain movement to the corresponding 2D plane.

#### Scenario: Drag X axis translates along X
Given the gizmo mode is Translate and entity A is at position (0, 0, 0)
When the user drags the X axis handle 100 pixels to the right
Then entity A's position.x increases proportionally to the projected mouse delta

#### Scenario: Snap translation to grid
Given snap is enabled (Ctrl held) with grid size 10cm
When the user drags the X axis handle
Then entity A's position.x snaps to multiples of 10cm

### Requirement: Gizmo Interaction — Rotate
When the user clicks and drags a rotate gizmo circle handle, the selected entity SHALL rotate around that axis. The rotation angle SHALL correspond to the mouse angular displacement around the circle center.

#### Scenario: Drag Y circle rotates around Y
Given the gizmo mode is Rotate and entity A has rotation (0, 0, 0)
When the user drags the Y axis circle handle 45 degrees clockwise
Then entity A's rotation includes a ~45° Y rotation

### Requirement: Gizmo Interaction — Scale
When the user clicks and drags a scale gizmo axis handle, the selected entity SHALL scale along that axis. The center cube handle SHALL apply uniform scale to all three axes.

#### Scenario: Drag center handle scales uniformly
Given the gizmo mode is Scale and entity A has scale (1, 1, 1)
When the user drags the center handle outward
Then entity A's scale increases uniformly (e.g., (1.5, 1.5, 1.5))

### Requirement: Gizmo Mode Switching
The EditorInputHandler SHALL support keyboard shortcuts for gizmo mode switching: W = Translate, E = Rotate, R = Scale. The current mode SHALL be visually distinguishable by the gizmo geometry rendered.

#### Scenario: Press W switches to translate
Given the gizmo mode is Rotate
When the user presses W
Then the gizmo mode changes to Translate and the gizmo visual updates to arrows

### Requirement: World vs Local Space
The gizmo SHALL support World space (axes aligned to world XYZ) and Local space (axes aligned to entity rotation). Space is toggled by pressing the space bar or via Lua API.

#### Scenario: Local space gizmo aligns to entity
Given entity A is rotated 45° around Z and gizmo space is Local
When the translate gizmo renders
Then the X/Y arrows are rotated 45° around Z matching entity orientation

### Requirement: Lua Editor API
The Lua API SHALL expose editor selection and gizmo control under `uz.editor.*`. Functions: `get_selected()`, `select(entity)`, `deselect()`, `set_gizmo_mode(mode)`, `set_gizmo_space(space)`.

#### Scenario: Lua selects entity
Given entity "Cube" exists in the scene
When Lua calls `uz.editor.select(cube_entity)`
Then the outline renders around the cube and the gizmo appears at its position
