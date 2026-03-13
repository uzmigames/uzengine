# Clipboard & Drag-Drop — Spec Delta

## ADDED Requirements

### Requirement: Clipboard Text Operations
The platform layer MUST support reading and writing UTF-8 text to the system clipboard.

#### Scenario: Clipboard Round-Trip
Given the clipboard is empty
When `SetClipboardText("Hello")` is called
Then `GetClipboardText()` returns `"Hello"`

#### Scenario: Empty Clipboard
Given the clipboard contains an image (no text)
When `GetClipboardText()` is called
Then an empty string is returned

### Requirement: OLE Drag-Drop
The window MUST register as an OLE drop target and fire callbacks when files are dragged over or dropped.

#### Scenario: File Drop From Explorer
Given a user drags 3 PNG files from Windows Explorer onto the engine window
When the files are released over the window
Then `OnFilesDropped` callback fires with 3 file paths and the mouse position in client coords

#### Scenario: Drag Visual Feedback
Given a user drags a file over the engine window
When `DragEnter` fires
Then the engine receives `OnDragEnter(fileTypes, position)` to display a drop highlight
And `OnDragLeave` fires when the cursor exits the window
