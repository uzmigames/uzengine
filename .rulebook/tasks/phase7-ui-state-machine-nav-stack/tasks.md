## 1. UIScreenManager Core Data Structure

- [ ] 1.1 Create `src/ui/UIScreenManager.h` defining `UIScreenManager` class with a `std::vector<ScreenEntry>` stack where `ScreenEntry` contains: `screen_id` (std::string), `html_document_path` (std::string), `state` (enum `Active | Suspended`), `transition_type` (enum `None | Slide | Fade`), `transition_direction` (enum `In | Out`)
- [ ] 1.2 Implement `UIScreenManager::push_screen(id)` in `src/ui/UIScreenManager.cpp`: if the screen HTML is not in the document cache, load it via the existing HTML document loader; call `suspend()` on the current top-of-stack entry; push the new entry; trigger the enter transition
- [ ] 1.3 Implement `UIScreenManager::pop_screen()`: if the stack has only one entry, do nothing (log a debug warning `"pop_screen() called on last screen — no-op"`); otherwise trigger exit transition on the top entry, pop it on transition completion, call `resume()` on the new top entry
- [ ] 1.4 Implement `UIScreenManager::replace_screen(id)`: pop the top entry without animation, push the new screen without a back-navigation record; used for loading screens and non-reversible transitions
- [ ] 1.5 Implement `UIScreenManager::current_screen() -> std::string` returning the `screen_id` of the top stack entry, or empty string if the stack is empty
- [ ] 1.6 Implement `UIScreenManager::stack_depth() -> int` returning the number of entries in the navigation stack

## 2. Screen HTML Document Caching

- [ ] 2.1 Maintain an `std::unordered_map<std::string, CachedHTMLDoc>` in `UIScreenManager` keyed by `screen_id` where `CachedHTMLDoc` holds the parsed DOM root and any associated Lua script context
- [ ] 2.2 On `push_screen(id)`, check the cache; if missing, load the HTML file from disk and parse it; store in cache; if present, reuse the cached DOM without re-parsing
- [ ] 2.3 Expose `UIScreenManager::preload_screen(id)` to allow preloading screens before they are pushed (e.g., preload the main menu while the loading screen is displayed)

## 3. Transition Animations

- [ ] 3.1 Read the `<meta name="uz-transition" content="slide|fade|none">` tag from each screen's HTML `<head>` section during document load; store the transition type in the `ScreenEntry`
- [ ] 3.2 For `fade` transitions: set the screen's root DOM element opacity from 0 to 1 (enter) or 1 to 0 (exit) over the transition duration using the browser's CSS `transition` property; duration defaults to 250 ms, configurable via `content="fade duration=400"`
- [ ] 3.3 For `slide` transitions: translate the screen's root element from `translateX(100%)` to `translateX(0)` on enter (and reverse on exit); direction defaults to right-to-left, configurable via `content="slide direction=left|right|up|down"`
- [ ] 3.4 Implement transition completion callback: browser JS sends an IPC event `ui.transition_complete {screen_id}` when the CSS transition ends (`transitionend` event); `UIScreenManager` receives this event and commits the pop or completes the push
- [ ] 3.5 For `replace_screen`: skip both enter and exit animations; activate the new screen immediately

## 4. Input System Integration

- [ ] 4.1 In `src/input/InputSystem.cpp`, on Escape key press and gamepad B-button press, check if a `UIScreenManager` is active; if so, call `UIScreenManager::handle_back_input()`
- [ ] 4.2 Implement `UIScreenManager::handle_back_input()`: if a Lua `back_handler` callback is registered, invoke it and check the return value; if the handler returns `true` (consumed), do not call `pop_screen()`; if the handler returns `false` or no handler is registered, call `pop_screen()`
- [ ] 4.3 Ensure `InputSystem` calls the UIScreenManager back handler only when the UI layer has focus (i.e., `UIScreenManager::stack_depth() > 0`); do not intercept Escape when no screen is active

## 5. Screen Lifecycle Callbacks

- [ ] 5.1 Fire `on_screen_enter` Lua callback in the pushed screen's script context when the enter transition begins
- [ ] 5.2 Fire `on_screen_exit` Lua callback in the popped screen's script context when `pop_screen()` is called (before the exit transition starts)
- [ ] 5.3 Fire `on_screen_suspend` Lua callback on the previous top-of-stack screen when a new screen is pushed on top of it
- [ ] 5.4 Fire `on_screen_resume` Lua callback on the screen that becomes the new top when the screen above it is popped
- [ ] 5.5 Each callback is registered in the screen's Lua script using: `ctx.ui:on_screen_enter(function() ... end)` — bind all four callbacks in `src/scripting/lua_ui.cpp`

## 6. Lua API Bindings

- [ ] 6.1 Add `ctx.ui:push_screen(id)` Lua binding in `src/scripting/lua_ui.cpp` calling `UIScreenManager::push_screen()`
- [ ] 6.2 Add `ctx.ui:pop_screen()` Lua binding
- [ ] 6.3 Add `ctx.ui:replace_screen(id)` Lua binding
- [ ] 6.4 Add `ctx.ui:current_screen() -> string` Lua binding
- [ ] 6.5 Add `ctx.ui:stack_depth() -> int` Lua binding
- [ ] 6.6 Add `ctx.ui:set_back_handler(fn)` Lua binding: registers a per-screen function to intercept back-navigation; replaces any previously registered handler on the current screen

## 7. VSCode UI DevTools Panel

- [ ] 7.1 In `vscode-ext/src/panels/UIDevToolsPanel.ts`, add a "Navigation Stack" section displaying a breadcrumb trail of the current screen stack (e.g., `MainMenu > Settings > Controls`)
- [ ] 7.2 Highlight the active (top) screen in the breadcrumb in bold; suspended screens shown in normal weight
- [ ] 7.3 Update the breadcrumb whenever the `UIScreenManager` fires an IPC event `ui.stack_changed {stack: [...screen_ids...]}`; emit this event from `UIScreenManager` on every push, pop, and replace

## 8. Testing

- [ ] 8.1 Write `tests/ui/test_ui_screen_manager.cpp`: test `push_screen` — stack depth increases from 0 to 1; `current_screen()` returns the pushed ID
- [ ] 8.2 Test `pop_screen` from depth 1 → verify it is a no-op (stack depth remains 1) and a debug warning is logged
- [ ] 8.3 Test `pop_screen` from depth 2 → verify stack depth returns to 1 and `current_screen()` returns the original screen ID
- [ ] 8.4 Test `replace_screen`: push A, replace with B → verify stack depth is 1 and `current_screen()` is B (not A)
- [ ] 8.5 Test HTML cache hit: push same screen ID twice (push A, pop A, push A again) → verify the HTML loader is called only once (use a load-count mock)
- [ ] 8.6 Test lifecycle callbacks: push screen B onto screen A → verify `on_screen_suspend` is called on A and `on_screen_enter` is called on B in that order
- [ ] 8.7 Test `on_screen_resume`: pop B from stack with A below → verify `on_screen_resume` is called on A after `on_screen_exit` is called on B
- [ ] 8.8 Test `set_back_handler` returning `true` (consumed): verify `pop_screen` is NOT called
- [ ] 8.9 Test `set_back_handler` returning `false`: verify `pop_screen` IS called
- [ ] 8.10 Test no back handler registered: Escape key → verify `pop_screen` is called directly
- [ ] 8.11 Test `stack_depth()` after three pushes: verify returns 3; after two pops: verify returns 1
- [ ] 8.12 Verify ≥95% line and branch coverage on `UIScreenManager.cpp` including all transition branches and the no-op pop-from-single guard

## 9. Documentation

- [ ] 9.1 Update `docs/specs/ui-system.md`: document `UIScreenManager` push/pop/replace semantics, HTML caching, `<meta name="uz-transition">` syntax, all four lifecycle callbacks, and Lua API signatures
- [ ] 9.2 Update `docs/specs/input-system.md`: document Escape key and gamepad B-button integration with `UIScreenManager::handle_back_input()` and the `set_back_handler` interception mechanism
