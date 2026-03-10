# Milestone 1.2 — RHI Implementation Tasks

## Phase 1: RHI Interface Design
- [ ] 1.1 Design RHI vtable (device, command list, resource operations)
- [ ] 1.2 Implement `rhi.h` pure virtual interface
- [ ] 1.3 Create `rhi_backend.h` for backend registration
- [ ] 1.4 Add unit tests for interface validation

## Phase 2: DX12 SM6.6 Backend
- [ ] 2.1 Implement DX12Device initialization (IDXGIDevice6, ID3D12Device6)
- [ ] 2.2 Implement DX12CommandList recording and execution
- [ ] 2.3 Implement resource creation (buffers, textures)
- [ ] 2.4 Integrate D3D12MemoryAllocator for GPU heap management
- [ ] 2.5 Implement descriptor heap management (bindless support)
- [ ] 2.6 Add PSO (Pipeline State Object) caching

## Phase 3: Swapchain & Frame Pacing
- [ ] 3.1 Implement double-buffered swapchain
- [ ] 3.2 Implement fence synchronization for frame pacing
- [ ] 3.3 Add GPU timestamp queries for profiling
- [ ] 3.4 Handle swapchain resize and DXGI_ERROR_DEVICE_RESET

## Phase 4: Validation & Clear Screen
- [ ] 4.1 Enable D3D12 debug layer and validation
- [ ] 4.2 Implement clear color command
- [ ] 4.3 Implement animated color render loop (sine wave test)
- [ ] 4.4 Validate GPU command execution
- [ ] 4.5 Add frame time profiling (target 60fps)

## Phase 5: Testing & Documentation
- [ ] 5.1 Write RHI backend tests
- [ ] 5.2 Write swapchain tests
- [ ] 5.3 Document RHI interface and backend pattern
- [ ] 5.4 Add usage examples (create device, record commands, present)

## Success Criteria
- [ ] All RHI methods compile and link
- [ ] DX12 backend initializes without errors
- [ ] Swapchain presents animated color at 60fps
- [ ] No GPU validation errors or memory leaks
- [ ] Tests pass on MSVC
- [ ] Documentation complete

<!-- Phase: In Planning -->
