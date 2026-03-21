# Vulkan Compute Dispatch Without Descriptor Flush

**Category**: rhi
**Tags**: vulkan, compute, dispatch, descriptor-set, bind-point, flush, rhi

## Description

Vulkan compute dispatches (vkCmdDispatch) require descriptors to be bound via vkCmdBindDescriptorSets with VK_PIPELINE_BIND_POINT_COMPUTE before dispatch. If the descriptor flush only happens in the draw path (DrawIndexed/Draw) but not in Dispatch(), compute shaders read from stale/empty descriptor sets. Additionally, FlushDescriptorSet must use the correct bind point — using VK_PIPELINE_BIND_POINT_GRAPHICS for a compute dispatch silently fails.

## Example

// WRONG: Dispatch without flushing descriptors
void VulkanCommandList::Dispatch(uint32_t x, uint32_t y, uint32_t z) {
    vkCmdDispatch(m_cmd, x, y, z); // No descriptors bound!
}

// CORRECT: Flush descriptors with correct bind point before dispatch
void VulkanCommandList::Dispatch(uint32_t x, uint32_t y, uint32_t z) {
    FlushDescriptorSet(); // Must use VK_PIPELINE_BIND_POINT_COMPUTE
    vkCmdDispatch(m_cmd, x, y, z);
}

void VulkanCommandList::FlushDescriptorSet() {
    VkPipelineBindPoint bindPoint = m_computePipelineBound
        ? VK_PIPELINE_BIND_POINT_COMPUTE
        : VK_PIPELINE_BIND_POINT_GRAPHICS;
    vkCmdBindDescriptorSets(m_cmd, bindPoint, ...);
}

## When to Use

Any Vulkan backend that shares command list implementation between graphics and compute pipelines. Track m_computePipelineBound in SetPipeline() and use it in FlushDescriptorSet().

## When NOT to Use

Backends that use separate command lists for compute — the bind point is always COMPUTE.
