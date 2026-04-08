# Proposal: phase1_vulkan-backend-deferred-tests

## Why
The Vulkan backend (phase0_0.2.0_vulkan-backend) shipped with 35 passing tests but deferred 5 test items that require more infrastructure or cross-backend tooling.

## What Changes
- Add Vulkan barrier transition tests (VkImageLayout correctness, pipeline stage flags)
- Add swapchain resize/recreation tests (VK_ERROR_OUT_OF_DATE handling)
- Add format conversion tests (all 22 RHIFormat → VkFormat mappings)
- Add cross-backend rendering comparison (DX12 vs Vulkan screenshot diff)
- Achieve 95%+ coverage on all Vulkan code

## Impact
- Affected specs: rhi-vulkan-backend
- Affected code: runtime/tests/rhi/vulkan/
- Breaking change: NO
- User benefit: Higher confidence in Vulkan backend correctness
