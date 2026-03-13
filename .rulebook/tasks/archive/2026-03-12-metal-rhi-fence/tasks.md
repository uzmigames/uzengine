## 1. Implementation

- [ ] 1.1 Create `metal_fence.h` — `MetalFence : public IRHIFence` class declaration
- [ ] 1.2 Store `MTL::SharedEvent*` as primary sync primitive
- [ ] 1.3 Create `MTL::SharedEventListener*` for async notification dispatch
- [ ] 1.4 Implement `Signal(uint64_t value)` — encode signal into command buffer
- [ ] 1.5 Implement `WaitCPU(uint64_t value)` — listener + dispatch_semaphore blocking wait
- [ ] 1.6 Implement `IsComplete(uint64_t value)` — compare `signaledValue >= value`
- [ ] 1.7 Implement `GetCompletedValue()` — return `event->signaledValue()`
- [ ] 1.8 Implement destructor — release SharedEvent and SharedEventListener

## 2. Testing

- [ ] 2.1 Test fence creation and initial signaled value is 0
- [ ] 2.2 Test `IsComplete(0)` returns true immediately (initial state)
- [ ] 2.3 Test `GetCompletedValue()` returns 0 before any GPU work
- [ ] 2.4 Test `WaitCPU` with already-completed value returns immediately
- [ ] 2.5 Test `Signal` + `WaitCPU` round-trip with actual command buffer submission
- [ ] 2.6 Test monotonic value ordering (signal 1, 2, 3 — verify each completes in order)
- [ ] 2.7 Verify 95%+ coverage

## 3. Documentation

- [ ] 3.1 Document MTLSharedEvent usage in `docs/engine/03-rhi/15-metal-backend.md`
- [ ] 3.2 Document why MTLSharedEvent was chosen over MTLFence
- [ ] 3.3 Document dispatch_semaphore blocking pattern for WaitCPU
