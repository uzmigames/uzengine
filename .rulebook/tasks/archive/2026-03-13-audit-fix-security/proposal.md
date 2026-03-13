# Proposal: Security & Memory Safety Fixes

## Why
Internal audit (2026-03-13) found 3 CRITICAL and 5 HIGH security/memory safety issues. Descriptor heap
index without bounds checking can corrupt GPU memory, raw pointers without RAII risk leaks and
use-after-free, and missing null checks after COM QueryInterface can cause crashes.

## What Changes
- Add bounds checking to descriptor heap index allocation (S-03)
- Wrap raw pointers in RAII (ComPtr, unique_ptr) throughout RHI backend (S-01)
- Add null checks after all COM QueryInterface calls (S-02)
- Validate shader bytecode size before GPU upload (S-05)
- Replace manual COM Release calls with ComPtr (S-06)
- Add fence value overflow protection (S-07)
- Sanitize shader include file paths (S-08)
- Add integer overflow guards for buffer size calculations (S-09)
- Fix resource cleanup on error paths (S-10)

## Impact
- Affected specs: RHI interface
- Affected code: runtime/engine/rhi/dx12/*.cpp, runtime/engine/renderer/passes/*.cpp
- Breaking change: NO
- User benefit: No GPU memory corruption, no crashes from null COM pointers, no resource leaks
