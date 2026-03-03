## 1. Console Class (`src/engine/debug/console.h/.cpp`)

- [x] 1.1 Define `Console` class with `register_cmd()`, `unregister_cmd()`, `execute(line)`, `complete(partial)`
- [x] 1.2 Implement command registration with name + handler (`std::function<std::string(std::span<std::string_view>)>`)
- [x] 1.3 Implement `execute()` — tokenize with quote support, dispatch to handler
- [x] 1.4 Implement `complete()` — prefix-match registered command names
- [x] 1.5 Built-in `help` command — list all registered commands
- [x] 1.6 Built-in `clear` command — clear scrollback buffer

## 2. History & Scrollback

- [x] 2.1 Scrollback buffer capped at 512 lines (ring buffer or trimmed vector)
- [x] 2.2 Command history capped at 64 entries, deduplicates consecutive identical commands
- [x] 2.3 `get_scrollback()` returns const reference to scrollback
- [x] 2.4 `get_history()` returns const reference to history

## 3. Argument Parsing

- [x] 3.1 `parse_line()` tokenizes with single/double quote support
- [x] 3.2 Escaped quotes inside quoted strings handled correctly
- [x] 3.3 Empty lines and whitespace-only lines handled gracefully

## 4. Tests (`tests/debug/test_console.cpp`)

- [x] 4.1 73 tests covering registration, execution, completion, history, scrollback, edge cases
- [x] 4.2 All tests pass — 763/763 total (after integration)

## 5. CMakeLists.txt Integration

- [x] 5.1 `src/engine/debug/console.cpp` added to `UzEngineTests`
- [x] 5.2 `tests/debug/test_console.cpp` added to `UzEngineTests`
