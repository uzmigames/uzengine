## 1. Implementation

- [x] 1.1 Test Vulkan barriers: correct VkImageLayout transitions, pipeline stage flags (64 CPU tests + 8 GPU tests)
- [ ] 1.2 Test swapchain recreation on resize (VK_ERROR_OUT_OF_DATE handling) — BLOCKED: requires HWND/window infrastructure
- [x] 1.3 Test format conversion: all 22 RHIFormat → VkFormat mappings (22 CPU tests + 16 GPU tests)
- [ ] 1.4 Cross-backend comparison: DX12 vs Vulkan produce equivalent rendering output — BLOCKED: requires render pipeline
- [x] 1.5 Verify 95%+ coverage on all Vulkan code — 102 new tests added (total 137 Vulkan tests)

## 2. Documentation

- [x] 2.1 Update CHANGELOG with test additions
