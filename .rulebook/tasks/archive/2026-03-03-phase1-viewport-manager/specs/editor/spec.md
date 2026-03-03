## ADDED Requirements

### Requirement: Viewport Lifecycle State Machine
The `ViewportRegistry` SHALL maintain a state machine for each viewport with states INACTIVE, ACTIVE, SUSPENDED, and DESTROYED. State transitions SHALL follow: INACTIVE → ACTIVE (on first setActive true), ACTIVE ↔ SUSPENDED (on setActive toggle), any → DESTROYED (on destroy). Illegal transitions SHALL be rejected and logged as warnings.

#### Scenario: Activate an inactive viewport
Given a viewport has been created and is in INACTIVE state
When `viewport_set_active(id, true)` is called
Then the viewport transitions to ACTIVE state and is included in the next render tick

#### Scenario: Suspend an active viewport
Given a viewport is in ACTIVE state and rendering frames
When `viewport_set_active(id, false)` is called
Then the viewport transitions to SUSPENDED state and is excluded from all subsequent render ticks with zero GPU cost

#### Scenario: Resume a suspended viewport
Given a viewport is in SUSPENDED state
When `viewport_set_active(id, true)` is called
Then the viewport transitions to ACTIVE state and resumes rendering on the next tick without reloading scene state

---

### Requirement: Zero GPU Cost for Inactive Viewports
The `RenderScheduler` SHALL skip all render work (render graph execution, JPEG encode, WebSocket send) for viewports in INACTIVE or SUSPENDED state. The CPU cost of iterating skipped viewports MUST NOT exceed 1 microsecond per viewport per frame.

#### Scenario: Multiple suspended viewports
Given three viewports are SUSPENDED and one is ACTIVE
When the engine render loop executes one frame
Then only the ACTIVE viewport executes render graph, JPEG encode, and WebSocket send; the three SUSPENDED viewports perform no GPU operations

---

### Requirement: VRAM Budget Enforcement
The `ViewportRegistry` SHALL track per-viewport VRAM usage and enforce a configurable total budget (default 512 MB). When a `viewport_set_active` call would cause total ACTIVE viewport VRAM to exceed the budget, the registry SHALL auto-suspend the oldest ACTIVE viewport and emit a `viewport.suspended` push event to the IPC client before activating the requested viewport.

#### Scenario: VRAM budget exceeded on activation
Given three viewports are ACTIVE consuming 480 MB total
When a fourth viewport requests activation with 80 MB of render targets
Then the oldest ACTIVE viewport is auto-suspended, a `viewport.suspended` push event is sent, and the fourth viewport transitions to ACTIVE

---

### Requirement: Maximum Active Viewport Cap
The `ViewportRegistry` SHALL enforce a hard cap of 4 simultaneously ACTIVE viewports. A `viewport_set_active` call that would exceed this cap SHALL return an error code `VIEWPORT_ERR_CAP_REACHED` without modifying any viewport state.

#### Scenario: Exceeding the active cap
Given 4 viewports are already ACTIVE
When a fifth viewport calls `viewport_set_active(id, true)`
Then the call returns `VIEWPORT_ERR_CAP_REACHED` and all existing viewports remain in their current states

---

### Requirement: Heartbeat Watchdog Auto-Suspend
The `ViewportRegistry` SHALL track the timestamp of the last IPC message received. If no IPC message is received for 30 consecutive seconds, the registry SHALL suspend all ACTIVE viewports and log a warning. Viewports SHALL remain suspended until the next IPC message is received and explicit `viewport_set_active` calls are made.

#### Scenario: IPC silence triggers watchdog
Given two viewports are ACTIVE and no IPC message has been received for 31 seconds
When `viewport_registry_tick()` executes
Then both ACTIVE viewports transition to SUSPENDED state and a warning is logged with the elapsed silence duration

---

### Requirement: Multi-Viewport WebSocket Multiplexing
Every JPEG frame sent over the WebSocket connection SHALL be prefixed with a 4-byte little-endian `viewport_id` field. The TypeScript `ViewportManager` SHALL read this prefix and dispatch each frame to the `ViewportPanel` instance with the matching id. The existing JPEG frame length-prefix protocol SHALL be preserved after the viewport_id prefix.

#### Scenario: Frame routing by viewport id
Given viewports with id=1 and id=2 are both ACTIVE
When both viewports produce a JPEG frame in the same engine tick
Then the WebSocket sends two messages: one prefixed with bytes [0x01, 0x00, 0x00, 0x00] and one prefixed with [0x02, 0x00, 0x00, 0x00], and the TypeScript side delivers each frame to the correct webview canvas

---

### Requirement: VSCode Tab Visibility Integration
The TypeScript `ViewportPanel` class SHALL subscribe to `vscode.WebviewPanel.onDidChangeViewState`. When the panel becomes inactive (hidden behind another tab), it SHALL call `ViewportManager.setActive(id, false)`. When the panel becomes active (brought to foreground), it SHALL call `ViewportManager.setActive(id, true)`.

#### Scenario: Tab hidden by switching to another panel
Given a ViewportPanel is ACTIVE and rendering
When the user switches focus to a different VSCode tab, making the ViewportPanel inactive
Then `ViewportManager.setActive(id, false)` is called within one VSCode event loop tick, and the engine suspends the viewport on the next IPC poll

---

### Requirement: Window Focus Auto-Suspend
The VSCode extension SHALL subscribe to `vscode.window.onDidChangeWindowState`. When the window loses focus (`focused === false`), `ViewportManager.suspendAll()` SHALL be called. When the window regains focus (`focused === true`), `ViewportManager.resumeVisible()` SHALL be called to reactivate only panels whose `webviewPanel.visible` property is true.

#### Scenario: VSCode window loses focus
Given one viewport is ACTIVE and the VSCode window has focus
When the developer switches to another application
Then `suspendAll()` sends `viewport.setActive(id, false)` for all ACTIVE viewports, and the engine suspends all render work

#### Scenario: VSCode window regains focus
Given all viewports were suspended by window focus loss
When the developer returns focus to VSCode
Then `resumeVisible()` sends `viewport.setActive(id, true)` only for panels with `webviewPanel.visible === true`, leaving hidden tabs suspended
