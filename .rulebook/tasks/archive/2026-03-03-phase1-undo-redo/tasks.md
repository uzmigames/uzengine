## 1. ICommand Interface (`src/engine/editor/undo_redo.h`)

- [x] 1.1 Define `ICommand` abstract interface: `execute()`, `undo()`, `try_merge(ICommand*)`, `name()`
- [x] 1.2 Define `UndoStack` class with bounded capacity (default 256)
- [x] 1.3 `UndoStack::execute(cmd)` — push, truncate redo tail, cap at max size
- [x] 1.4 `UndoStack::undo()` — call current command `undo()`, move pointer back
- [x] 1.5 `UndoStack::redo()` — advance pointer, call `execute()`
- [x] 1.6 `UndoStack::clear()` — reset stack and pointer
- [x] 1.7 `UndoStack::begin_group()` / `end_group()` — batch multiple commands as one undo entry
- [x] 1.8 `UndoStack::peek_undo()` / `peek_redo()` — inspect without executing

## 2. GroupCommand

- [x] 2.1 `GroupCommand` (anonymous namespace) — holds vector of commands
- [x] 2.2 `execute()` replays commands in forward order
- [x] 2.3 `undo()` replays commands in reverse order

## 3. Tests (`tests/editor/test_undo_redo.cpp`)

- [x] 3.1 63 tests covering execute/undo/redo, max-size cap, group batching, try_merge, edge cases
- [x] 3.2 All tests pass — integrated into `UzEngineTests`

## 4. CMakeLists.txt Integration

- [x] 4.1 `src/engine/editor/undo_redo.cpp` added to `UzEngineTests`
- [x] 4.2 `tests/editor/test_undo_redo.cpp` added to `UzEngineTests`
