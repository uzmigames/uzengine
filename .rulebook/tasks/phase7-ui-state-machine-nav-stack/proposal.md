# Proposal: phase2-ui-state-machine-nav-stack

## Why
There is no navigation stack for UI screens. In a typical game flow — Main Menu → Settings → Controls → Rebind Key — pressing Escape to navigate back must be manually coded for every transition, each Lua script tracking where it came from. This creates duplicated back-navigation logic across every game project, is easy to get wrong (double-pop, orphaned screens), and provides no consistent animation model for screen transitions. A managed navigation stack is a prerequisite for any game with more than two UI screens.

## What Changes
- Implement `UIScreenManager` in uz_ui_html.dll: an explicit push/pop stack where each entry is a named screen ID corresponding to a loaded HTML document
- `push_screen(id)`: loads the target screen HTML if not cached, runs a configurable enter transition animation (slide, fade, or none, declared in the screen's `<meta name="uz-transition">` tag), suspends (but does not unload) the previous screen
- `pop_screen()`: runs the reverse exit transition on the top screen, resumes the screen below it; popping the last screen on the stack is a no-op
- `replace_screen(id)`: pops current screen without animation and pushes new screen, used for non-back-navigable transitions (e.g., loading screen to main menu)
- Wire Escape key and gamepad B-button from the input system to automatically call `pop_screen()` on the active UIScreenManager, with a Lua override `ctx.ui:set_back_handler(fn)` to intercept and suppress the default behavior (e.g., show "are you sure?" dialogs)
- Add Lua API: `ctx.ui:push_screen(id)`, `ctx.ui:pop_screen()`, `ctx.ui:replace_screen(id)`, `ctx.ui:current_screen()`, `ctx.ui:stack_depth()`
- Add screen lifecycle callbacks: `on_screen_enter(fn)`, `on_screen_exit(fn)`, `on_screen_suspend(fn)`, `on_screen_resume(fn)` available in each screen's Lua script
- Expose screen history in the VSCode UI DevTools panel as a breadcrumb trail showing the current navigation path

## Impact
- Affected specs: `docs/specs/ui-system.md`, `docs/specs/input-system.md`
- Affected code: `src/ui/uz_ui_html.dll`, `src/ui/UIScreenManager.cpp`, `src/input/InputSystem.cpp`, `src/ipc/IpcServer.cpp`, `vscode-ext/src/panels/UIDevToolsPanel.ts`
- Breaking change: NO (existing single-screen UI usage continues to work; screen manager is opt-in)
- User benefit: Multi-screen game UIs are built with a consistent navigation model — no per-game back-stack code, no missed Escape handlers, and transition animations are declared per-screen rather than manually coded in every transition callback.
