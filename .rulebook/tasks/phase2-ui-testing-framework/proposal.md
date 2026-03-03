# Proposal: phase2-ui-testing-framework

## Why
There is no automated regression testing for game UI. Visual defects — overlapping elements, layouts broken at 4K resolutions, wrong text colors after a theme change — are discovered manually during playtesting and often late in the development cycle. Without a screenshot-based test suite that runs in CI, any refactor to the UI system or CSS files risks silently breaking previously working layouts.

## What Changes
- Implement a screenshot-based UI test runner as a standalone tool (`ui-test-runner.exe`) that launches the engine headlessly, navigates to a named UI screen, and captures a DX12 readback frame of the UI pass output
- Implement pixel-diff comparison with configurable tolerance (default 0.5% of pixels may differ by up to 2 color units) to handle sub-pixel antialiasing variation across GPU vendors
- Add a test DSL (TOML-based `.uzuitest` files) allowing test authors to specify: screen to load, sequence of simulated input events (hover, click, keyboard), wait conditions (element visible, animation complete), and assertion (screenshot matches baseline)
- Add IPC commands for test harness control: `ui.load_screen`, `ui.simulate_input`, `ui.wait_for_element`, `ui.capture_screenshot`
- Generate an HTML test report with side-by-side diff images (baseline / actual / highlighted diff) for any failing tests
- Add a VSCode "UI Test Results" panel that displays the test report inline after a test run, with diff images viewable without leaving the editor
- Integrate the test runner into the CI pipeline via a CMake test target (`ctest -R ui_tests`)

## Impact
- Affected specs: `docs/specs/ui-system.md`, `docs/specs/debug-profiling.md`
- Affected code: `src/ui/UITestRunner.cpp`, `src/ipc/IpcServer.cpp`, `vscode-ext/src/panels/UITestResultsPanel.ts`, `tests/ui/`
- Breaking change: NO
- User benefit: UI regressions are caught automatically before code merges, and designers can write layout tests in plain TOML without C++ knowledge, making quality assurance a first-class part of the UI authoring workflow.
