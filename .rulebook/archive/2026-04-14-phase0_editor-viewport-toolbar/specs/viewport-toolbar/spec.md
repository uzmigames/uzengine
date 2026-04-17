# Editor Viewport Toolbar — Spec Delta

## ADDED Requirements

### Requirement: ShowFlags Bitfield
The renderer SHALL provide a `ShowFlags` 64-bit bitfield struct that controls per-feature visibility in the viewport. Each render pass MUST check its corresponding ShowFlag bit before executing. All flags MUST default to enabled (1).

#### Scenario: Fog Toggle
Given the editor is running with default ShowFlags
When the user sets `ShowFlags::Fog = false`
Then the height fog pass SHALL be skipped
And the scene renders without fog

#### Scenario: Grid Toggle
Given the editor grid is visible
When `ShowFlags::Grid` is set to false
Then `EditorGrid::Draw()` SHALL return without rendering
And the 3D viewport shows no grid

#### Scenario: Default State
Given a fresh SceneRenderer initialization
When no ShowFlags are modified
Then all flags SHALL be enabled (all bits = 1)
And all render passes execute normally

### Requirement: Extended DebugViewModes
The renderer SHALL support at minimum 4 additional view modes beyond the existing 13: DetailLighting, LightingOnly, LitWireframe, and Reflections. Each mode MUST be selectable via `--view-mode <name>` CLI and `SetDebugViewMode()` API.

#### Scenario: Detail Lighting Mode
Given the user selects DetailLighting mode
When the frame renders
Then the output SHALL show directional NdotL without albedo or IBL
And the image is grayscale representing pure light contribution

#### Scenario: Lit Wireframe Mode
Given the user selects LitWireframe mode
When the frame renders
Then the output SHALL show normal PBR lit result with wireframe edges overlaid

### Requirement: Orthographic Camera Presets
The CameraManager SHALL support switching to orthographic views (Top, Front, Left, Right, Back, Bottom) via API and hotkeys. Each preset MUST set the camera to the correct position, orientation, and orthographic projection.

#### Scenario: Top View
Given the user presses ALT+J
When the camera switches to Top view
Then the projection MUST be orthographic
And the camera looks down the -Z axis
And the scene is viewed from above

### Requirement: ViewportToolbarState IPC
The runtime SHALL expose a `toolbar.get_state` IPC command that returns the complete viewport toolbar state as JSON. The VSCode extension MUST be able to read and modify toolbar state via IPC commands.

#### Scenario: Get State
Given the editor is running
When the extension sends `toolbar.get_state`
Then the runtime responds with JSON containing viewMode, showFlags, transformMode, coordSpace, qualityLevel, cameraId, fov, nearPlane, farPlane

#### Scenario: Set View Mode
Given the editor is in Lit mode
When the extension sends `toolbar.set_view_mode "Wireframe"`
Then the runtime switches to Wireframe rendering
And the next `toolbar.get_state` returns `viewMode: "Wireframe"`

### Requirement: Toolbar Profiles
The toolbar SHALL support multiple profiles (Full, MaterialPreview, MeshPreview, Minimal) that control which toolbar sections are available. The active profile MUST be switchable via IPC.

#### Scenario: Material Preview Profile
Given the toolbar is in MaterialPreview profile
When the extension queries available toolbar sections
Then only ViewMode and Lighting sections SHALL be available
And Transform, ShowFlags, and Quality sections SHALL be hidden

### Requirement: Game View Toggle
The editor SHALL support a Game View toggle (G key) that hides all editor overlays (grid, gizmo, selection outline, stats) while preserving game rendering. The toggle MUST be reversible.

#### Scenario: Enter Game View
Given the editor shows grid, gizmo, and stats overlay
When the user presses G
Then grid, gizmo, selection outline, and stats overlay SHALL be hidden
And scene geometry, lighting, and post-processing continue rendering

#### Scenario: Exit Game View
Given the editor is in Game View mode
When the user presses G again
Then all editor overlays SHALL be restored to their previous state
