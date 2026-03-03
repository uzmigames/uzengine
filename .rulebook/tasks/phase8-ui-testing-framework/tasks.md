## 1. UI Test Runner Executable

- [ ] 1.1 Create `src/ui/UITestRunner.cpp` implementing `ui-test-runner.exe`: parse a `.uzuitest` TOML test file, launch the engine headlessly, and execute the test sequence
- [ ] 1.2 Add CMake target `ui-test-runner` in `CMakeLists.txt` linking against the engine core, renderer, and IPC client libraries
- [ ] 1.3 Implement headless engine startup in the test runner: initialize DX12 device, UIPass, and IPC server without creating a visible window

## 2. uzuitest Test DSL

- [ ] 2.1 Define the `.uzuitest` TOML schema: sections `[test]` (name, description), `[setup]` (screen to load), `[[step]]` array (action type, parameters, wait condition), and `[assert]` (screenshot baseline path, tolerance)
- [ ] 2.2 Implement TOML parser for `.uzuitest` files in `src/ui/UITestRunner.cpp` using an existing TOML library (add as vcpkg dependency if not present)
- [ ] 2.3 Support action types in `[[step]]`: `type = "hover"` with `{element_id}`, `type = "click"` with `{element_id}`, `type = "key"` with `{key_code}`, `type = "wait_ms"` with `{ms}`, `type = "wait_for_element"` with `{element_id, visible: true/false}`

## 3. IPC Test Harness Commands

- [ ] 3.1 Register IPC command `ui.load_screen` in `src/ipc/IpcServer.cpp` accepting `{ "screen": "path/to/screen.html" }` and loading the HTML document into UIPass
- [ ] 3.2 Register IPC command `ui.simulate_input` accepting `{ "type": "click|hover|key", "element_id": "...", "key_code": N }` and dispatching the corresponding input event to the HTML document
- [ ] 3.3 Register IPC command `ui.wait_for_element` accepting `{ "element_id": "...", "visible": true, "timeout_ms": 2000 }` and polling until the element's visibility matches or the timeout elapses
- [ ] 3.4 Register IPC command `ui.capture_screenshot` that triggers a DX12 frame readback and writes the PNG to a temporary file; return the file path in the IPC response

## 4. Pixel-Diff Comparison

- [ ] 4.1 In `UITestRunner`, implement `compare_screenshots(baseline_path, actual_path, tolerance)` using a pixel-by-pixel diff: count pixels where any channel differs by > 2 units; report failure if differing pixel count > `tolerance * total_pixel_count`
- [ ] 4.2 Default tolerance is 0.005 (0.5% of pixels may differ)
- [ ] 4.3 Generate a diff image highlighting mismatched pixels in red; save to `<test_name>_diff.png` alongside the actual screenshot

## 5. HTML Test Report

- [ ] 5.1 After each test run, generate `tests/ui/results/report.html` listing all tests with pass/fail status, test name, and elapsed time
- [ ] 5.2 For failing tests, embed side-by-side `<img>` tags: baseline on the left, actual in the center, diff on the right
- [ ] 5.3 Link failing test entries to the corresponding `.uzuitest` source file for easy navigation

## 6. VSCode UI Test Results Panel

- [ ] 6.1 Create `vscode-uzengine/src/panels/UITestResultsPanel.ts` implementing a WebView that loads and displays `tests/ui/results/report.html` inline after a test run
- [ ] 6.2 Register the panel with command `uzengine.showUITestResults` in `vscode-uzengine/src/extension.ts`
- [ ] 6.3 Auto-refresh the WebView content when `report.html` changes on disk (use `vscode.workspace.createFileSystemWatcher`)

## 7. CTest Integration

- [ ] 7.1 Add a `ctest` test target `ui_tests` in `CMakeLists.txt` that runs `ui-test-runner.exe` on all `.uzuitest` files in `tests/ui/`
- [ ] 7.2 Configure CTest to fail if `ui-test-runner.exe` exits with a non-zero code

## 8. Testing

- [ ] 8.1 Create `tests/ui/test_uitest_runner.cpp` (unit tests for the runner logic) testing `.uzuitest` TOML parsing: verify a valid file produces the correct step list with all action types
- [ ] 8.2 Test that a missing `[assert]` section causes a parse error reported as a test configuration failure (not a crash)
- [ ] 8.3 Test `compare_screenshots()` with identical images returns pass; with images differing by > 0.5% of pixels returns fail
- [ ] 8.4 Test `compare_screenshots()` generates a diff PNG when images differ
- [ ] 8.5 Test IPC command `ui.load_screen` loads the HTML document into UIPass; verify by querying the DOM for a known element
- [ ] 8.6 Test IPC command `ui.simulate_input` with `type="click"` fires the `onclick` handler on the target element
- [ ] 8.7 Test IPC command `ui.wait_for_element` returns success when the element becomes visible within the timeout; returns failure (timeout) when it does not
- [ ] 8.8 Test UITestResultsPanel TypeScript: load a mock `report.html`; verify the WebView renders without errors
- [ ] 8.9 Achieve >= 95% line and branch coverage across `UITestRunner.cpp` and the IPC test harness commands

## 9. Documentation

- [ ] 9.1 Update `docs/specs/ui-system.md` documenting the `.uzuitest` DSL schema, supported action types, wait conditions, and assertion tolerance
- [ ] 9.2 Update `docs/specs/debug-profiling.md` documenting the `ui_tests` CTest target and CI integration
