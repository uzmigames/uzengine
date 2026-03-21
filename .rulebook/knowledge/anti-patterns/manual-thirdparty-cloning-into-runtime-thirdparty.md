# Manual Thirdparty Cloning into runtime/thirdparty/

**Category**: architecture
**Tags**: build, thirdparty, dependencies, setup, forbidden

## Description

Running git clone, curl, or wget to manually place thirdparty libraries into runtime/thirdparty/. This bypasses setup.ps1 which handles version pinning, .git cleanup, junk removal, patch application, and verification. Manual clones create inconsistent state, wrong versions, and missing patches.

## Example

// WRONG:\ngit clone https://github.com/lib/lib.git runtime/thirdparty/lib\ncurl -L https://lib.org/lib.zip -o runtime/thirdparty/lib.zip\n\n// CORRECT:\n// 1. Edit scripts/setup.ps1\n// 2. rm -rf runtime/thirdparty/<lib>\n// 3. powershell -ExecutionPolicy Bypass -File scripts/setup.ps1

## When to Use

When reviewing any PR or agent action that touches runtime/thirdparty/. When adding new dependencies.
