# Window State Machine — Spec Delta

## ADDED Requirements

### Requirement: Window State Tracking
The `IPlatformWindow` MUST track the current window state as a `WindowState` enum and expose it via `GetState()`.

#### Scenario: State After Minimize
Given a window in Normal state
When the user clicks the minimize button (SC_MINIMIZE)
Then `GetState()` returns `WindowState::Minimized`
And `GetWidth()`/`GetHeight()` return 0

#### Scenario: State After Maximize
Given a window in Normal state
When the user clicks the maximize button (SC_MAXIMIZE)
Then `GetState()` returns `WindowState::Maximized`
And `GetWidth()`/`GetHeight()` return the monitor work area dimensions

### Requirement: Borderless Fullscreen Toggle
The window MUST support toggling to borderless fullscreen on a specified monitor and restoring the previous windowed position.

#### Scenario: Enter Fullscreen
Given a window at position (100,100) with size 1280x720
When `SetFullscreen(0)` is called
Then the window covers the entire primary monitor with no title bar
And `GetState()` returns `WindowState::FullscreenBorderless`

#### Scenario: Exit Fullscreen
Given a window in FullscreenBorderless state (previously at 100,100 1280x720)
When `SetWindowed()` is called
Then the window returns to position (100,100) with size 1280x720
And `GetState()` returns `WindowState::Normal`

### Requirement: Minimum Size Enforcement
The window MUST enforce a configurable minimum size via `WM_GETMINMAXINFO`.

#### Scenario: Resize Below Minimum
Given a window with minimum size 320x240
When the user drags the border to 200x100
Then the window size is clamped to 320x240

### Requirement: Focus Tracking
The window MUST track focus state and expose it via `IsFocused()`.

#### Scenario: Alt-Tab Away
Given a focused window
When the user Alt-Tabs to another application
Then `IsFocused()` returns false
And the state change callback fires

### Requirement: State Persistence
The window MUST save its position, size, and state on destruction and restore them on creation when `rememberPosition` is enabled.

#### Scenario: Remember Position Across Sessions
Given a window with `rememberPosition = true` closed at (200,300) 1600x900 Maximized
When a new window is created with the same config key
Then the window is created at (200,300) 1600x900 in Maximized state
