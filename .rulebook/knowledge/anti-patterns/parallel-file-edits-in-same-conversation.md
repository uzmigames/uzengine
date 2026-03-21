# Parallel File Edits in Same Conversation

**Category**: code
**Tags**: editing, workflow, sequential, reliability, tooling

## Description

Editing multiple files in parallel using the Edit tool. The Edit tool uses exact string matching for replacements. When multiple files are edited simultaneously, the tool may fail to find exact strings, race conditions can cause partial edits, and error recovery becomes impossible. Files must be edited sequentially — edit file A, wait for confirmation, then edit file B.

## Example

// WRONG (parallel edits):\nEdit file_a.cpp  // launched simultaneously\nEdit file_b.cpp  // launched simultaneously\nEdit file_c.cpp  // → failures likely\n\n// CORRECT (sequential edits):\nEdit file_a.cpp → wait for confirmation\nEdit file_b.cpp → wait for confirmation\nEdit file_c.cpp → wait for confirmation

## When to Use

When reviewing any multi-file edit operation. When planning implementation that spans multiple files.
