# Native Dialogs & OS Integration — Spec Delta

## ADDED Requirements

### Requirement: File Open/Save Dialogs
The platform MUST provide native file dialogs via COM `IFileDialog` with filter support and multi-select.

#### Scenario: Open Multiple Images
Given the user triggers "Import Texture"
When `OpenFileDialog("Images|*.png;*.jpg", multiSelect=true)` is called
Then a native Windows file picker appears
And the returned vector contains the selected file paths (empty if cancelled)

### Requirement: Dark Mode Detection
The platform MUST detect whether the system is in dark mode and notify when it changes.

#### Scenario: Detect Dark Mode
Given Windows is set to dark mode
When `IsDarkMode()` is called
Then `true` is returned

#### Scenario: Dark Mode Toggle at Runtime
Given the engine is running
When the user toggles dark mode in Windows Settings
Then `OnDarkModeChanged(isDark)` callback fires

### Requirement: Taskbar Progress
The platform MUST support showing a progress bar in the Windows taskbar via `ITaskbarList3`.

#### Scenario: Show Bake Progress
Given the engine is baking lightmaps (50% complete)
When `SetTaskbarProgress(50, 100)` is called
Then the taskbar icon shows a green progress bar at 50%
And `ClearTaskbarProgress()` removes it when done
