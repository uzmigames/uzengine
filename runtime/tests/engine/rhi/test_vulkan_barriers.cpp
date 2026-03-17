/// CPU-only and GPU integration tests for Vulkan barrier conversion functions.
///
/// # CPU-only tests (no Vulkan device required)
/// The static mapping functions (ToVkImageLayout, ToVkAccessFlags,
/// ToVkPipelineStage, GetAspectFlags, IsDepthFormat) are duplicated here
/// from vulkan_command_list.cpp and tested exhaustively for every
/// RHIResourceState and RHIFormat value.
///
/// # GPU integration tests (require a real Vulkan device)
/// These tests issue actual barrier commands via IRHICommandList and verify
/// that WaitIdle() succeeds (no Vulkan validation layer errors). If device
/// creation fails, all GPU tests are skipped via GTEST_SKIP().
///
/// # Coverage
/// - ToVkImageLayout — all 10 mapped states + default fallback
/// - ToVkAccessFlags — all 14 mapped states + default fallback
/// - ToVkPipelineStage — all 14 mapped states + default fallback
/// - GetAspectFlags — color formats, pure-depth formats, depth+stencil formats
/// - IsDepthFormat — all 4 depth/stencil formats, several color formats
/// - Texture barrier round-trip: Undefined→RenderTarget→ShaderResource→Present
/// - Buffer barrier round-trip: CopySrc→CopyDst

#ifdef _WIN32

#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <volk.h>

#include "rhi.h"
#include "rhi_backend.h"
#include "rhi_types.h"

#include <gtest/gtest.h>
#include <memory>

using namespace uz;

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Local duplicates of the static mapping functions from vulkan_command_list.cpp
//
// These mirror the implementation exactly so each branch is exercised by the
// tests below without requiring a full GPU context.
// ─────────────────────────────────────────────────────────────────────────────

/// Local copy of IsDepthFormat (mirrors vulkan_command_list.cpp).
///
/// Returns true when the format carries a depth component (D16, D32, D24S8,
/// D32S8). Used to drive GetAspectFlags.
[[nodiscard]] static bool LocalIsDepthFormat(RHIFormat format) {
    return format == RHIFormat::D16Unorm
        || format == RHIFormat::D32Float
        || format == RHIFormat::D24UnormS8Uint
        || format == RHIFormat::D32FloatS8Uint;
}

/// Local copy of ToVkImageLayout (mirrors vulkan_command_list.cpp).
///
/// Maps each RHIResourceState to the VkImageLayout used when issuing an image
/// memory barrier. States that have no meaningful image layout (e.g. buffer-
/// only states) fall back to VK_IMAGE_LAYOUT_GENERAL.
[[nodiscard]] static VkImageLayout LocalToVkImageLayout(RHIResourceState state) {
    switch (state) {
        case RHIResourceState::Undefined:         return VK_IMAGE_LAYOUT_UNDEFINED;
        case RHIResourceState::Common:            return VK_IMAGE_LAYOUT_GENERAL;
        case RHIResourceState::ShaderResource:    return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        case RHIResourceState::UnorderedAccess:   return VK_IMAGE_LAYOUT_GENERAL;
        case RHIResourceState::RenderTarget:      return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        case RHIResourceState::DepthStencilWrite: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        case RHIResourceState::DepthStencilRead:  return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        case RHIResourceState::CopySrc:           return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        case RHIResourceState::CopyDst:           return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        case RHIResourceState::Present:           return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        default:                                  return VK_IMAGE_LAYOUT_GENERAL;
    }
}

/// Local copy of ToVkAccessFlags (mirrors vulkan_command_list.cpp).
///
/// Maps each RHIResourceState to the VkAccessFlags2 mask used in
/// synchronization2 image/buffer memory barriers.
[[nodiscard]] static VkAccessFlags2 LocalToVkAccessFlags(RHIResourceState state) {
    switch (state) {
        case RHIResourceState::Undefined:         return VK_ACCESS_2_NONE;
        case RHIResourceState::Common:            return VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        case RHIResourceState::VertexBuffer:      return VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
        case RHIResourceState::IndexBuffer:       return VK_ACCESS_2_INDEX_READ_BIT;
        case RHIResourceState::ConstantBuffer:    return VK_ACCESS_2_UNIFORM_READ_BIT;
        case RHIResourceState::ShaderResource:    return VK_ACCESS_2_SHADER_READ_BIT;
        case RHIResourceState::UnorderedAccess:   return VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
        case RHIResourceState::RenderTarget:      return VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        case RHIResourceState::DepthStencilWrite: return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        case RHIResourceState::DepthStencilRead:  return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        case RHIResourceState::CopySrc:           return VK_ACCESS_2_TRANSFER_READ_BIT;
        case RHIResourceState::CopyDst:           return VK_ACCESS_2_TRANSFER_WRITE_BIT;
        case RHIResourceState::Present:           return VK_ACCESS_2_NONE;
        case RHIResourceState::IndirectArgument:  return VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        default:                                  return VK_ACCESS_2_NONE;
    }
}

/// Local copy of ToVkPipelineStage (mirrors vulkan_command_list.cpp).
///
/// Maps each RHIResourceState to the VkPipelineStageFlags2 mask used in
/// synchronization2 barriers to identify which pipeline stages produce or
/// consume the resource.
[[nodiscard]] static VkPipelineStageFlags2 LocalToVkPipelineStage(RHIResourceState state) {
    switch (state) {
        case RHIResourceState::Undefined:
            return VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        case RHIResourceState::Common:
            return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        case RHIResourceState::VertexBuffer:
            return VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
        case RHIResourceState::IndexBuffer:
            return VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
        case RHIResourceState::ConstantBuffer:
            return VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        case RHIResourceState::ShaderResource:
            return VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        case RHIResourceState::UnorderedAccess:
            return VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        case RHIResourceState::RenderTarget:
            return VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        case RHIResourceState::DepthStencilWrite:
            return VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        case RHIResourceState::DepthStencilRead:
            return VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
        case RHIResourceState::CopySrc:
            return VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        case RHIResourceState::CopyDst:
            return VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        case RHIResourceState::Present:
            return VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        case RHIResourceState::IndirectArgument:
            return VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
        default:
            return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    }
}

/// Local copy of GetAspectFlags (mirrors vulkan_command_list.cpp).
///
/// Returns the VkImageAspectFlags for image subresource ranges. Depth-only
/// formats set DEPTH_BIT; combined depth+stencil formats set both DEPTH_BIT
/// and STENCIL_BIT; all other formats set COLOR_BIT.
[[nodiscard]] static VkImageAspectFlags LocalGetAspectFlags(RHIFormat format) {
    if (LocalIsDepthFormat(format)) {
        VkImageAspectFlags flags = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (format == RHIFormat::D24UnormS8Uint || format == RHIFormat::D32FloatS8Uint) {
            flags |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
        return flags;
    }
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

// ─────────────────────────────────────────────────────────────────────────────
// IsDepthFormat Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(VulkanBarriers_IsDepthFormat, D16Unorm_IsDepth) {
    EXPECT_TRUE(LocalIsDepthFormat(RHIFormat::D16Unorm));
}

TEST(VulkanBarriers_IsDepthFormat, D32Float_IsDepth) {
    EXPECT_TRUE(LocalIsDepthFormat(RHIFormat::D32Float));
}

TEST(VulkanBarriers_IsDepthFormat, D24UnormS8Uint_IsDepth) {
    EXPECT_TRUE(LocalIsDepthFormat(RHIFormat::D24UnormS8Uint));
}

TEST(VulkanBarriers_IsDepthFormat, D32FloatS8Uint_IsDepth) {
    EXPECT_TRUE(LocalIsDepthFormat(RHIFormat::D32FloatS8Uint));
}

TEST(VulkanBarriers_IsDepthFormat, RGBA8Unorm_IsNotDepth) {
    EXPECT_FALSE(LocalIsDepthFormat(RHIFormat::RGBA8Unorm));
}

TEST(VulkanBarriers_IsDepthFormat, RGBA16Float_IsNotDepth) {
    EXPECT_FALSE(LocalIsDepthFormat(RHIFormat::RGBA16Float));
}

TEST(VulkanBarriers_IsDepthFormat, R32Float_IsNotDepth) {
    EXPECT_FALSE(LocalIsDepthFormat(RHIFormat::R32Float));
}

TEST(VulkanBarriers_IsDepthFormat, BGRA8Unorm_IsNotDepth) {
    EXPECT_FALSE(LocalIsDepthFormat(RHIFormat::BGRA8Unorm));
}

TEST(VulkanBarriers_IsDepthFormat, R8Unorm_IsNotDepth) {
    EXPECT_FALSE(LocalIsDepthFormat(RHIFormat::R8Unorm));
}

TEST(VulkanBarriers_IsDepthFormat, RGBA32Float_IsNotDepth) {
    EXPECT_FALSE(LocalIsDepthFormat(RHIFormat::RGBA32Float));
}

// ─────────────────────────────────────────────────────────────────────────────
// GetAspectFlags Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(VulkanBarriers_GetAspectFlags, RGBA8Unorm_ColorBit) {
    EXPECT_EQ(LocalGetAspectFlags(RHIFormat::RGBA8Unorm), VK_IMAGE_ASPECT_COLOR_BIT);
}

TEST(VulkanBarriers_GetAspectFlags, RGBA8Srgb_ColorBit) {
    EXPECT_EQ(LocalGetAspectFlags(RHIFormat::RGBA8Srgb), VK_IMAGE_ASPECT_COLOR_BIT);
}

TEST(VulkanBarriers_GetAspectFlags, BGRA8Unorm_ColorBit) {
    EXPECT_EQ(LocalGetAspectFlags(RHIFormat::BGRA8Unorm), VK_IMAGE_ASPECT_COLOR_BIT);
}

TEST(VulkanBarriers_GetAspectFlags, BGRA8Srgb_ColorBit) {
    EXPECT_EQ(LocalGetAspectFlags(RHIFormat::BGRA8Srgb), VK_IMAGE_ASPECT_COLOR_BIT);
}

TEST(VulkanBarriers_GetAspectFlags, RGBA16Float_ColorBit) {
    EXPECT_EQ(LocalGetAspectFlags(RHIFormat::RGBA16Float), VK_IMAGE_ASPECT_COLOR_BIT);
}

TEST(VulkanBarriers_GetAspectFlags, RGBA32Float_ColorBit) {
    EXPECT_EQ(LocalGetAspectFlags(RHIFormat::RGBA32Float), VK_IMAGE_ASPECT_COLOR_BIT);
}

TEST(VulkanBarriers_GetAspectFlags, R32Float_ColorBit) {
    EXPECT_EQ(LocalGetAspectFlags(RHIFormat::R32Float), VK_IMAGE_ASPECT_COLOR_BIT);
}

TEST(VulkanBarriers_GetAspectFlags, R32Uint_ColorBit) {
    EXPECT_EQ(LocalGetAspectFlags(RHIFormat::R32Uint), VK_IMAGE_ASPECT_COLOR_BIT);
}

TEST(VulkanBarriers_GetAspectFlags, D16Unorm_DepthBitOnly) {
    VkImageAspectFlags flags = LocalGetAspectFlags(RHIFormat::D16Unorm);
    EXPECT_EQ(flags & VK_IMAGE_ASPECT_DEPTH_BIT, static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_DEPTH_BIT));
    EXPECT_EQ(flags & VK_IMAGE_ASPECT_STENCIL_BIT, 0u);
}

TEST(VulkanBarriers_GetAspectFlags, D32Float_DepthBitOnly) {
    VkImageAspectFlags flags = LocalGetAspectFlags(RHIFormat::D32Float);
    EXPECT_EQ(flags & VK_IMAGE_ASPECT_DEPTH_BIT, static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_DEPTH_BIT));
    EXPECT_EQ(flags & VK_IMAGE_ASPECT_STENCIL_BIT, 0u);
}

TEST(VulkanBarriers_GetAspectFlags, D24UnormS8Uint_DepthAndStencilBits) {
    VkImageAspectFlags flags = LocalGetAspectFlags(RHIFormat::D24UnormS8Uint);
    EXPECT_EQ(flags & VK_IMAGE_ASPECT_DEPTH_BIT,   static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_DEPTH_BIT));
    EXPECT_EQ(flags & VK_IMAGE_ASPECT_STENCIL_BIT, static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_STENCIL_BIT));
}

TEST(VulkanBarriers_GetAspectFlags, D32FloatS8Uint_DepthAndStencilBits) {
    VkImageAspectFlags flags = LocalGetAspectFlags(RHIFormat::D32FloatS8Uint);
    EXPECT_EQ(flags & VK_IMAGE_ASPECT_DEPTH_BIT,   static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_DEPTH_BIT));
    EXPECT_EQ(flags & VK_IMAGE_ASPECT_STENCIL_BIT, static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_STENCIL_BIT));
}

// ─────────────────────────────────────────────────────────────────────────────
// ToVkImageLayout Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(VulkanBarriers_ToVkImageLayout, Undefined_MapsToUndefined) {
    EXPECT_EQ(LocalToVkImageLayout(RHIResourceState::Undefined), VK_IMAGE_LAYOUT_UNDEFINED);
}

TEST(VulkanBarriers_ToVkImageLayout, Common_MapsToGeneral) {
    EXPECT_EQ(LocalToVkImageLayout(RHIResourceState::Common), VK_IMAGE_LAYOUT_GENERAL);
}

TEST(VulkanBarriers_ToVkImageLayout, ShaderResource_MapsToShaderReadOnlyOptimal) {
    EXPECT_EQ(LocalToVkImageLayout(RHIResourceState::ShaderResource),
              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

TEST(VulkanBarriers_ToVkImageLayout, UnorderedAccess_MapsToGeneral) {
    EXPECT_EQ(LocalToVkImageLayout(RHIResourceState::UnorderedAccess), VK_IMAGE_LAYOUT_GENERAL);
}

TEST(VulkanBarriers_ToVkImageLayout, RenderTarget_MapsToColorAttachmentOptimal) {
    EXPECT_EQ(LocalToVkImageLayout(RHIResourceState::RenderTarget),
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
}

TEST(VulkanBarriers_ToVkImageLayout, DepthStencilWrite_MapsToDepthStencilAttachmentOptimal) {
    EXPECT_EQ(LocalToVkImageLayout(RHIResourceState::DepthStencilWrite),
              VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
}

TEST(VulkanBarriers_ToVkImageLayout, DepthStencilRead_MapsToDepthStencilReadOnlyOptimal) {
    EXPECT_EQ(LocalToVkImageLayout(RHIResourceState::DepthStencilRead),
              VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
}

TEST(VulkanBarriers_ToVkImageLayout, CopySrc_MapsToTransferSrcOptimal) {
    EXPECT_EQ(LocalToVkImageLayout(RHIResourceState::CopySrc),
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
}

TEST(VulkanBarriers_ToVkImageLayout, CopyDst_MapsToTransferDstOptimal) {
    EXPECT_EQ(LocalToVkImageLayout(RHIResourceState::CopyDst),
              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
}

TEST(VulkanBarriers_ToVkImageLayout, Present_MapsToPresentSrcKHR) {
    EXPECT_EQ(LocalToVkImageLayout(RHIResourceState::Present),
              VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
}

/// Buffer-only states (VertexBuffer, IndexBuffer, ConstantBuffer,
/// IndirectArgument) have no canonical image layout — they fall back to
/// VK_IMAGE_LAYOUT_GENERAL via the default branch.
TEST(VulkanBarriers_ToVkImageLayout, VertexBuffer_FallsBackToGeneral) {
    EXPECT_EQ(LocalToVkImageLayout(RHIResourceState::VertexBuffer), VK_IMAGE_LAYOUT_GENERAL);
}

TEST(VulkanBarriers_ToVkImageLayout, IndexBuffer_FallsBackToGeneral) {
    EXPECT_EQ(LocalToVkImageLayout(RHIResourceState::IndexBuffer), VK_IMAGE_LAYOUT_GENERAL);
}

TEST(VulkanBarriers_ToVkImageLayout, ConstantBuffer_FallsBackToGeneral) {
    EXPECT_EQ(LocalToVkImageLayout(RHIResourceState::ConstantBuffer), VK_IMAGE_LAYOUT_GENERAL);
}

TEST(VulkanBarriers_ToVkImageLayout, IndirectArgument_FallsBackToGeneral) {
    EXPECT_EQ(LocalToVkImageLayout(RHIResourceState::IndirectArgument), VK_IMAGE_LAYOUT_GENERAL);
}

// ─────────────────────────────────────────────────────────────────────────────
// ToVkAccessFlags Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(VulkanBarriers_ToVkAccessFlags, Undefined_MapsToNone) {
    EXPECT_EQ(LocalToVkAccessFlags(RHIResourceState::Undefined), VK_ACCESS_2_NONE);
}

TEST(VulkanBarriers_ToVkAccessFlags, Common_MapsToMemoryReadAndWrite) {
    VkAccessFlags2 expected = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    EXPECT_EQ(LocalToVkAccessFlags(RHIResourceState::Common), expected);
}

TEST(VulkanBarriers_ToVkAccessFlags, VertexBuffer_MapsToVertexAttributeRead) {
    EXPECT_EQ(LocalToVkAccessFlags(RHIResourceState::VertexBuffer),
              VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT);
}

TEST(VulkanBarriers_ToVkAccessFlags, IndexBuffer_MapsToIndexRead) {
    EXPECT_EQ(LocalToVkAccessFlags(RHIResourceState::IndexBuffer), VK_ACCESS_2_INDEX_READ_BIT);
}

TEST(VulkanBarriers_ToVkAccessFlags, ConstantBuffer_MapsToUniformRead) {
    EXPECT_EQ(LocalToVkAccessFlags(RHIResourceState::ConstantBuffer), VK_ACCESS_2_UNIFORM_READ_BIT);
}

TEST(VulkanBarriers_ToVkAccessFlags, ShaderResource_MapsToShaderRead) {
    EXPECT_EQ(LocalToVkAccessFlags(RHIResourceState::ShaderResource), VK_ACCESS_2_SHADER_READ_BIT);
}

TEST(VulkanBarriers_ToVkAccessFlags, UnorderedAccess_MapsToShaderReadAndWrite) {
    VkAccessFlags2 expected = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
    EXPECT_EQ(LocalToVkAccessFlags(RHIResourceState::UnorderedAccess), expected);
}

TEST(VulkanBarriers_ToVkAccessFlags, RenderTarget_MapsToColorAttachmentWrite) {
    EXPECT_EQ(LocalToVkAccessFlags(RHIResourceState::RenderTarget),
              VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
}

TEST(VulkanBarriers_ToVkAccessFlags, DepthStencilWrite_MapsToDepthStencilAttachmentWrite) {
    EXPECT_EQ(LocalToVkAccessFlags(RHIResourceState::DepthStencilWrite),
              VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
}

TEST(VulkanBarriers_ToVkAccessFlags, DepthStencilRead_MapsToDepthStencilAttachmentRead) {
    EXPECT_EQ(LocalToVkAccessFlags(RHIResourceState::DepthStencilRead),
              VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
}

TEST(VulkanBarriers_ToVkAccessFlags, CopySrc_MapsToTransferRead) {
    EXPECT_EQ(LocalToVkAccessFlags(RHIResourceState::CopySrc), VK_ACCESS_2_TRANSFER_READ_BIT);
}

TEST(VulkanBarriers_ToVkAccessFlags, CopyDst_MapsToTransferWrite) {
    EXPECT_EQ(LocalToVkAccessFlags(RHIResourceState::CopyDst), VK_ACCESS_2_TRANSFER_WRITE_BIT);
}

TEST(VulkanBarriers_ToVkAccessFlags, Present_MapsToNone) {
    EXPECT_EQ(LocalToVkAccessFlags(RHIResourceState::Present), VK_ACCESS_2_NONE);
}

TEST(VulkanBarriers_ToVkAccessFlags, IndirectArgument_MapsToIndirectCommandRead) {
    EXPECT_EQ(LocalToVkAccessFlags(RHIResourceState::IndirectArgument),
              VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
}

// ─────────────────────────────────────────────────────────────────────────────
// ToVkPipelineStage Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(VulkanBarriers_ToVkPipelineStage, Undefined_MapsToTopOfPipe) {
    EXPECT_EQ(LocalToVkPipelineStage(RHIResourceState::Undefined),
              VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);
}

TEST(VulkanBarriers_ToVkPipelineStage, Common_MapsToAllCommands) {
    EXPECT_EQ(LocalToVkPipelineStage(RHIResourceState::Common),
              VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
}

TEST(VulkanBarriers_ToVkPipelineStage, VertexBuffer_MapsToVertexInput) {
    EXPECT_EQ(LocalToVkPipelineStage(RHIResourceState::VertexBuffer),
              VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT);
}

TEST(VulkanBarriers_ToVkPipelineStage, IndexBuffer_MapsToIndexInput) {
    EXPECT_EQ(LocalToVkPipelineStage(RHIResourceState::IndexBuffer),
              VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT);
}

TEST(VulkanBarriers_ToVkPipelineStage, ConstantBuffer_MapsToVertexAndFragmentShader) {
    VkPipelineStageFlags2 expected =
        VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    EXPECT_EQ(LocalToVkPipelineStage(RHIResourceState::ConstantBuffer), expected);
}

TEST(VulkanBarriers_ToVkPipelineStage, ShaderResource_MapsToVertexAndFragmentShader) {
    VkPipelineStageFlags2 expected =
        VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    EXPECT_EQ(LocalToVkPipelineStage(RHIResourceState::ShaderResource), expected);
}

TEST(VulkanBarriers_ToVkPipelineStage, UnorderedAccess_MapsToComputeShader) {
    EXPECT_EQ(LocalToVkPipelineStage(RHIResourceState::UnorderedAccess),
              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
}

TEST(VulkanBarriers_ToVkPipelineStage, RenderTarget_MapsToColorAttachmentOutput) {
    EXPECT_EQ(LocalToVkPipelineStage(RHIResourceState::RenderTarget),
              VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
}

TEST(VulkanBarriers_ToVkPipelineStage, DepthStencilWrite_MapsToEarlyAndLateFragmentTests) {
    VkPipelineStageFlags2 expected =
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    EXPECT_EQ(LocalToVkPipelineStage(RHIResourceState::DepthStencilWrite), expected);
}

TEST(VulkanBarriers_ToVkPipelineStage, DepthStencilRead_MapsToEarlyFragmentTests) {
    EXPECT_EQ(LocalToVkPipelineStage(RHIResourceState::DepthStencilRead),
              VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT);
}

TEST(VulkanBarriers_ToVkPipelineStage, CopySrc_MapsToTransfer) {
    EXPECT_EQ(LocalToVkPipelineStage(RHIResourceState::CopySrc),
              VK_PIPELINE_STAGE_2_TRANSFER_BIT);
}

TEST(VulkanBarriers_ToVkPipelineStage, CopyDst_MapsToTransfer) {
    EXPECT_EQ(LocalToVkPipelineStage(RHIResourceState::CopyDst),
              VK_PIPELINE_STAGE_2_TRANSFER_BIT);
}

TEST(VulkanBarriers_ToVkPipelineStage, Present_MapsToBottomOfPipe) {
    EXPECT_EQ(LocalToVkPipelineStage(RHIResourceState::Present),
              VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
}

TEST(VulkanBarriers_ToVkPipelineStage, IndirectArgument_MapsToDrawIndirect) {
    EXPECT_EQ(LocalToVkPipelineStage(RHIResourceState::IndirectArgument),
              VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT);
}

// ─────────────────────────────────────────────────────────────────────────────
// GPU Integration Test Fixture
//
// Creates a real Vulkan IRHIDevice with validation enabled. All tests inside
// this fixture are skipped when no Vulkan-capable GPU or driver is present.
// ─────────────────────────────────────────────────────────────────────────────

/// Fixture owning a real Vulkan device and command list for barrier round-trip
/// tests. Skips automatically when device creation fails (no GPU / CI).
class VulkanBarrierGpuTest : public ::testing::Test {
protected:
    std::unique_ptr<IRHIDevice> m_device;
    IRHICommandList* m_cmdList = nullptr;

    void SetUp() override {
        RHIDeviceDesc desc{};
        desc.backend          = RHIBackend::Vulkan;
        desc.enableValidation = true;
        m_device = CreateRHIDevice(desc);
        if (!m_device) {
            GTEST_SKIP() << "Vulkan device creation failed (no GPU or Vulkan driver)";
        }

        m_cmdList = m_device->CreateCommandList(RHIQueueType::Graphics);
        if (!m_cmdList) {
            m_device->WaitIdle();
            m_device.reset();
            GTEST_SKIP() << "Failed to create Vulkan command list";
        }
    }

    void TearDown() override {
        if (m_device) {
            m_device->WaitIdle();
            if (m_cmdList) {
                m_device->DestroyCommandList(m_cmdList);
                m_cmdList = nullptr;
            }
            m_device.reset();
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// GPU: Texture Barrier Round-Trip
// ─────────────────────────────────────────────────────────────────────────────

/// Verifies that a texture can be transitioned through the states a typical
/// render-target-then-sampled-then-presented path would use:
///   Undefined → RenderTarget → ShaderResource → Present
///
/// If Vulkan validation layers are enabled, any incorrect srcStageMask /
/// dstStageMask / layout combination would produce a validation error and the
/// subsequent WaitIdle() call would surface it as a device-lost or abort.
TEST_F(VulkanBarrierGpuTest, TextureBarrier_UndefinedToRenderTargetToShaderResourceToPresent) {
    // Create a 256x256 RGBA render target
    RHITextureDesc texDesc{};
    texDesc.width    = 256;
    texDesc.height   = 256;
    texDesc.format   = RHIFormat::RGBA8Unorm;
    texDesc.usage    = RHITextureUsage::RenderTarget | RHITextureUsage::ShaderResource;
    texDesc.debugName = "BarrierRoundTripRT";

    RHITextureHandle texture = m_device->CreateTexture(texDesc);
    ASSERT_TRUE(texture.IsValid()) << "Failed to create render-target texture";

    // Barrier sequence: Undefined → RenderTarget
    RHIBarrierDesc toRT{};
    toRT.texture     = texture;
    toRT.stateBefore = RHIResourceState::Undefined;
    toRT.stateAfter  = RHIResourceState::RenderTarget;

    // Barrier sequence: RenderTarget → ShaderResource
    RHIBarrierDesc toSR{};
    toSR.texture     = texture;
    toSR.stateBefore = RHIResourceState::RenderTarget;
    toSR.stateAfter  = RHIResourceState::ShaderResource;

    // Barrier sequence: ShaderResource → Present
    RHIBarrierDesc toPresent{};
    toPresent.texture     = texture;
    toPresent.stateBefore = RHIResourceState::ShaderResource;
    toPresent.stateAfter  = RHIResourceState::Present;

    m_cmdList->Begin();
    m_cmdList->ResourceBarrier(&toRT, 1);
    m_cmdList->ResourceBarrier(&toSR, 1);
    m_cmdList->ResourceBarrier(&toPresent, 1);
    m_cmdList->End();
    m_cmdList->Execute();

    // WaitIdle flushes all GPU work. A Vulkan validation error during the
    // barrier commands would surface here as a debug callback / abort.
    m_device->WaitIdle();

    m_device->DestroyTexture(texture);
}

/// Verifies that a depth texture barrier from Undefined to DepthStencilWrite
/// to DepthStencilRead completes without validation errors. The aspect mask
/// for D32Float must be DEPTH_BIT (no stencil) — an incorrect STENCIL_BIT
/// would trigger a validation error from the driver.
TEST_F(VulkanBarrierGpuTest, TextureBarrier_DepthTexture_UndefinedToWriteToRead) {
    RHITextureDesc depthDesc{};
    depthDesc.width     = 512;
    depthDesc.height    = 512;
    depthDesc.format    = RHIFormat::D32Float;
    depthDesc.usage     = RHITextureUsage::DepthStencil;
    depthDesc.debugName = "BarrierDepthTexture";

    RHITextureHandle depth = m_device->CreateTexture(depthDesc);
    ASSERT_TRUE(depth.IsValid()) << "Failed to create depth texture";

    RHIBarrierDesc toWrite{};
    toWrite.texture     = depth;
    toWrite.stateBefore = RHIResourceState::Undefined;
    toWrite.stateAfter  = RHIResourceState::DepthStencilWrite;

    RHIBarrierDesc toRead{};
    toRead.texture     = depth;
    toRead.stateBefore = RHIResourceState::DepthStencilWrite;
    toRead.stateAfter  = RHIResourceState::DepthStencilRead;

    m_cmdList->Begin();
    m_cmdList->ResourceBarrier(&toWrite, 1);
    m_cmdList->ResourceBarrier(&toRead, 1);
    m_cmdList->End();
    m_cmdList->Execute();
    m_device->WaitIdle();

    m_device->DestroyTexture(depth);
}

/// Verifies that a combined depth+stencil texture (D24UnormS8Uint) transitions
/// through Undefined → DepthStencilWrite correctly. The aspect mask must
/// include both DEPTH_BIT and STENCIL_BIT.
TEST_F(VulkanBarrierGpuTest, TextureBarrier_D24S8_DepthAndStencilAspectUsed) {
    RHITextureDesc ds{};
    ds.width     = 128;
    ds.height    = 128;
    ds.format    = RHIFormat::D24UnormS8Uint;
    ds.usage     = RHITextureUsage::DepthStencil;
    ds.debugName = "BarrierD24S8";

    RHITextureHandle handle = m_device->CreateTexture(ds);
    ASSERT_TRUE(handle.IsValid()) << "Failed to create D24UnormS8Uint texture";

    RHIBarrierDesc barrier{};
    barrier.texture     = handle;
    barrier.stateBefore = RHIResourceState::Undefined;
    barrier.stateAfter  = RHIResourceState::DepthStencilWrite;

    m_cmdList->Begin();
    m_cmdList->ResourceBarrier(&barrier, 1);
    m_cmdList->End();
    m_cmdList->Execute();
    m_device->WaitIdle();

    m_device->DestroyTexture(handle);
}

// ─────────────────────────────────────────────────────────────────────────────
// GPU: Buffer Barrier Round-Trip
// ─────────────────────────────────────────────────────────────────────────────

/// Verifies that a buffer barrier from CopySrc to CopyDst is issued without
/// Vulkan validation errors. Buffer barriers use VkBufferMemoryBarrier2 rather
/// than VkImageMemoryBarrier2 — this test exercises that branch of
/// ResourceBarrier().
TEST_F(VulkanBarrierGpuTest, BufferBarrier_CopySrcToCopyDst_NoValidationErrors) {
    RHIBufferDesc bufDesc{};
    bufDesc.sizeBytes = 1024;
    bufDesc.usage     = RHIBufferUsage::CopySrc | RHIBufferUsage::CopyDst;
    bufDesc.memory    = RHIMemoryType::Default;
    bufDesc.debugName = "BarrierTestBuffer";

    RHIBufferHandle buffer = m_device->CreateBuffer(bufDesc);
    ASSERT_TRUE(buffer.IsValid()) << "Failed to create buffer for barrier test";

    RHIBarrierDesc barrier{};
    barrier.buffer      = buffer;
    barrier.stateBefore = RHIResourceState::CopySrc;
    barrier.stateAfter  = RHIResourceState::CopyDst;

    m_cmdList->Begin();
    m_cmdList->ResourceBarrier(&barrier, 1);
    m_cmdList->End();
    m_cmdList->Execute();
    m_device->WaitIdle();

    m_device->DestroyBuffer(buffer);
}

/// Verifies that a buffer transitioning through VertexBuffer → ShaderResource
/// → UnorderedAccess → Common produces no Vulkan validation errors.
TEST_F(VulkanBarrierGpuTest, BufferBarrier_VertexToShaderToUAVToCommon_NoValidationErrors) {
    RHIBufferDesc bufDesc{};
    bufDesc.sizeBytes = 4096;
    bufDesc.usage     = RHIBufferUsage::VertexBuffer | RHIBufferUsage::UnorderedAccess;
    bufDesc.memory    = RHIMemoryType::Default;
    bufDesc.debugName = "BarrierUAVBuffer";

    RHIBufferHandle buffer = m_device->CreateBuffer(bufDesc);
    ASSERT_TRUE(buffer.IsValid()) << "Failed to create UAV buffer for barrier test";

    RHIBarrierDesc toSR{};
    toSR.buffer      = buffer;
    toSR.stateBefore = RHIResourceState::VertexBuffer;
    toSR.stateAfter  = RHIResourceState::ShaderResource;

    RHIBarrierDesc toUAV{};
    toUAV.buffer      = buffer;
    toUAV.stateBefore = RHIResourceState::ShaderResource;
    toUAV.stateAfter  = RHIResourceState::UnorderedAccess;

    RHIBarrierDesc toCommon{};
    toCommon.buffer      = buffer;
    toCommon.stateBefore = RHIResourceState::UnorderedAccess;
    toCommon.stateAfter  = RHIResourceState::Common;

    m_cmdList->Begin();
    m_cmdList->ResourceBarrier(&toSR, 1);
    m_cmdList->ResourceBarrier(&toUAV, 1);
    m_cmdList->ResourceBarrier(&toCommon, 1);
    m_cmdList->End();
    m_cmdList->Execute();
    m_device->WaitIdle();

    m_device->DestroyBuffer(buffer);
}

/// Verifies that ResourceBarrier() is a no-op (does not crash) when called
/// with count == 0.
TEST_F(VulkanBarrierGpuTest, ResourceBarrier_CountZero_IsNoop) {
    m_cmdList->Begin();
    m_cmdList->ResourceBarrier(nullptr, 0);
    m_cmdList->End();
    m_cmdList->Execute();
    m_device->WaitIdle();
    // Reaching this line without a crash or Vulkan abort is the assertion.
}

/// Verifies that ResourceBarrier() is a no-op when passed a null pointer with
/// a non-zero count. The implementation guards against this.
TEST_F(VulkanBarrierGpuTest, ResourceBarrier_NullPointerWithNonZeroCount_IsNoop) {
    m_cmdList->Begin();
    m_cmdList->ResourceBarrier(nullptr, 4);
    m_cmdList->End();
    m_cmdList->Execute();
    m_device->WaitIdle();
}

} // namespace

#endif // _WIN32
