# Thirdparty Dependencies via Setup Script Only

**Category**: architecture
**Tags**: build, thirdparty, dependencies, setup, automation

## Description

All 35 thirdparty dependencies are managed exclusively by scripts/setup.ps1. The script handles: git clone --depth 1, .git directory removal, junk cleanup (docs/tests/examples), patch application, prebuilt binary downloads (DXC, Agility SDK, Slang), and verification. This ensures consistent versions, applied patches, and clean state across all developers.

## Example

# To add a new library:\n# 1. Edit scripts/setup.ps1 — add clone/download section\n# 2. Delete runtime/thirdparty/<lib> if exists\n# 3. Run: powershell -ExecutionPolicy Bypass -File scripts/setup.ps1\n# 4. Verify: cd runtime && zig build

## When to Use

When adding a new thirdparty library. When updating a library version. When fixing a bug in a thirdparty lib (add a patch file).

## When NOT to Use

Never bypass this — no manual git clone, curl, or wget into runtime/thirdparty/.
