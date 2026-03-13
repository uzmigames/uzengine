# Tasks: Security & Memory Safety Fixes

## 1. Critical — Memory Safety
- [ ] 1.1 Add bounds checking to descriptor heap index allocation — assert/clamp before use (S-03)
- [ ] 1.2 Audit and wrap ALL raw pointers in RHI backend with RAII (ComPtr, unique_ptr) (S-01)
- [ ] 1.3 Add null checks after every COM QueryInterface call — log and return error on failure (S-02)

## 2. High — Resource Safety
- [ ] 2.1 Validate shader bytecode size (> 0, < reasonable max) before GPU upload (S-05)
- [ ] 2.2 Replace all manual `->Release()` calls with ComPtr throughout rhi/dx12/ (S-06)
- [ ] 2.3 Add fence value overflow protection — wrap or reset before UINT64_MAX (S-07)
- [ ] 2.4 Sanitize shader `#include` paths — reject `..`, absolute paths, symlinks (S-08)

## 3. Medium — Robustness
- [ ] 3.1 Add integer overflow checks for vertex/index buffer size calculations using safe math (S-09)
- [ ] 3.2 Audit error paths in resource creation — ensure all allocated resources are freed on failure (S-10)
- [ ] 3.3 Add rate limiting to debug layer message output (S-11)
- [ ] 3.4 Remove hardcoded viewport dimensions from demo code — use window size (S-12)

## 4. Low — Path Safety
- [ ] 4.1 Strip internal filesystem paths from shader compilation error messages before logging (S-04)

## 5. Testing
- [ ] 5.1 Add tests for descriptor heap overflow behavior
- [ ] 5.2 Add tests for invalid shader bytecode rejection
- [ ] 5.3 Add tests for fence value overflow handling
- [ ] 5.4 Add tests for path sanitization in shader includes

## 6. Documentation
- [ ] 6.1 Document RAII ownership conventions in AGENTS.md
- [ ] 6.2 Document shader include path security rules
