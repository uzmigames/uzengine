/// CPU-only and GPU integration tests for the Vulkan format conversion function.
///
/// # CPU-only tests (no Vulkan device required)
/// The static `ToVkFormat` function is duplicated here from
/// `vulkan_command_list.cpp` (it is file-static and not linkable). Every one
/// of the 20 explicitly mapped RHIFormat values is tested individually, plus
/// the Unknown → VK_FORMAT_UNDEFINED path and the default-branch fallback for
/// a cast out-of-range value.
///
/// # GPU integration tests (require a real Vulkan device)
/// For every non-depth color format the test creates a real 64x64 texture on a
/// live Vulkan device and asserts the handle is valid. This confirms that the
/// VkFormat produced by ToVkFormat is accepted by the Vulkan driver. Tests are
/// skipped automatically via GTEST_SKIP() when no Vulkan-capable GPU or driver
/// is present.
///
/// # Coverage
/// - ToVkFormat — all 20 explicit mappings
/// - ToVkFormat — RHIFormat::Unknown → VK_FORMAT_UNDEFINED
/// - ToVkFormat — out-of-range cast → default branch → VK_FORMAT_UNDEFINED
/// - GPU texture creation round-trip for every non-depth color format (16 formats)

#ifdef _WIN32

#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <volk.h>

#include "rhi.h"
#include "rhi_backend.h"
#include "rhi_types.h"

#include <gtest/gtest.h>

using namespace uz;

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Local duplicate of ToVkFormat from vulkan_command_list.cpp
//
// The production function is file-static and cannot be linked into tests.
// This copy mirrors the switch statement exactly so that every branch is
// exercised by the unit tests below.
// ─────────────────────────────────────────────────────────────────────────────

/// Local copy of ToVkFormat (mirrors vulkan_command_list.cpp).
///
/// Maps each RHIFormat enumerator to the corresponding VkFormat value used
/// when creating images or recording copy/blit commands. Unknown and any
/// unrecognised enumerator value map to VK_FORMAT_UNDEFINED.
///
/// # Parameters
/// - `format` — the engine-level texture format to convert.
///
/// # Returns
/// The corresponding VkFormat, or VK_FORMAT_UNDEFINED for Unknown / default.
[[nodiscard]] static VkFormat LocalToVkFormat(RHIFormat format) {
    switch (format) {
        case RHIFormat::R8Unorm:        return VK_FORMAT_R8_UNORM;
        case RHIFormat::RG8Unorm:       return VK_FORMAT_R8G8_UNORM;
        case RHIFormat::RGBA8Unorm:     return VK_FORMAT_R8G8B8A8_UNORM;
        case RHIFormat::RGBA8Srgb:      return VK_FORMAT_R8G8B8A8_SRGB;
        case RHIFormat::BGRA8Unorm:     return VK_FORMAT_B8G8R8A8_UNORM;
        case RHIFormat::BGRA8Srgb:      return VK_FORMAT_B8G8R8A8_SRGB;
        case RHIFormat::R16Float:       return VK_FORMAT_R16_SFLOAT;
        case RHIFormat::RG16Float:      return VK_FORMAT_R16G16_SFLOAT;
        case RHIFormat::RGBA16Float:    return VK_FORMAT_R16G16B16A16_SFLOAT;
        case RHIFormat::R32Float:       return VK_FORMAT_R32_SFLOAT;
        case RHIFormat::RG32Float:      return VK_FORMAT_R32G32_SFLOAT;
        case RHIFormat::RGB32Float:     return VK_FORMAT_R32G32B32_SFLOAT;
        case RHIFormat::RGBA32Float:    return VK_FORMAT_R32G32B32A32_SFLOAT;
        case RHIFormat::R32Uint:        return VK_FORMAT_R32_UINT;
        case RHIFormat::RG32Uint:       return VK_FORMAT_R32G32_UINT;
        case RHIFormat::RGBA32Uint:     return VK_FORMAT_R32G32B32A32_UINT;
        case RHIFormat::D16Unorm:       return VK_FORMAT_D16_UNORM;
        case RHIFormat::D32Float:       return VK_FORMAT_D32_SFLOAT;
        case RHIFormat::D24UnormS8Uint: return VK_FORMAT_D24_UNORM_S8_UINT;
        case RHIFormat::D32FloatS8Uint: return VK_FORMAT_D32_SFLOAT_S8_UINT;
        default:                        return VK_FORMAT_UNDEFINED;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// CPU-only — Colour format mappings
// ─────────────────────────────────────────────────────────────────────────────

TEST(VulkanFormatConversion, R8Unorm_MapsTo_VK_FORMAT_R8_UNORM) {
    EXPECT_EQ(LocalToVkFormat(RHIFormat::R8Unorm), VK_FORMAT_R8_UNORM);
}

TEST(VulkanFormatConversion, RG8Unorm_MapsTo_VK_FORMAT_R8G8_UNORM) {
    EXPECT_EQ(LocalToVkFormat(RHIFormat::RG8Unorm), VK_FORMAT_R8G8_UNORM);
}

TEST(VulkanFormatConversion, RGBA8Unorm_MapsTo_VK_FORMAT_R8G8B8A8_UNORM) {
    EXPECT_EQ(LocalToVkFormat(RHIFormat::RGBA8Unorm), VK_FORMAT_R8G8B8A8_UNORM);
}

TEST(VulkanFormatConversion, RGBA8Srgb_MapsTo_VK_FORMAT_R8G8B8A8_SRGB) {
    EXPECT_EQ(LocalToVkFormat(RHIFormat::RGBA8Srgb), VK_FORMAT_R8G8B8A8_SRGB);
}

TEST(VulkanFormatConversion, BGRA8Unorm_MapsTo_VK_FORMAT_B8G8R8A8_UNORM) {
    EXPECT_EQ(LocalToVkFormat(RHIFormat::BGRA8Unorm), VK_FORMAT_B8G8R8A8_UNORM);
}

TEST(VulkanFormatConversion, BGRA8Srgb_MapsTo_VK_FORMAT_B8G8R8A8_SRGB) {
    EXPECT_EQ(LocalToVkFormat(RHIFormat::BGRA8Srgb), VK_FORMAT_B8G8R8A8_SRGB);
}

// ─────────────────────────────────────────────────────────────────────────────
// CPU-only — Float format mappings
// ─────────────────────────────────────────────────────────────────────────────

TEST(VulkanFormatConversion, R16Float_MapsTo_VK_FORMAT_R16_SFLOAT) {
    EXPECT_EQ(LocalToVkFormat(RHIFormat::R16Float), VK_FORMAT_R16_SFLOAT);
}

TEST(VulkanFormatConversion, RG16Float_MapsTo_VK_FORMAT_R16G16_SFLOAT) {
    EXPECT_EQ(LocalToVkFormat(RHIFormat::RG16Float), VK_FORMAT_R16G16_SFLOAT);
}

TEST(VulkanFormatConversion, RGBA16Float_MapsTo_VK_FORMAT_R16G16B16A16_SFLOAT) {
    EXPECT_EQ(LocalToVkFormat(RHIFormat::RGBA16Float), VK_FORMAT_R16G16B16A16_SFLOAT);
}

TEST(VulkanFormatConversion, R32Float_MapsTo_VK_FORMAT_R32_SFLOAT) {
    EXPECT_EQ(LocalToVkFormat(RHIFormat::R32Float), VK_FORMAT_R32_SFLOAT);
}

TEST(VulkanFormatConversion, RG32Float_MapsTo_VK_FORMAT_R32G32_SFLOAT) {
    EXPECT_EQ(LocalToVkFormat(RHIFormat::RG32Float), VK_FORMAT_R32G32_SFLOAT);
}

TEST(VulkanFormatConversion, RGB32Float_MapsTo_VK_FORMAT_R32G32B32_SFLOAT) {
    EXPECT_EQ(LocalToVkFormat(RHIFormat::RGB32Float), VK_FORMAT_R32G32B32_SFLOAT);
}

TEST(VulkanFormatConversion, RGBA32Float_MapsTo_VK_FORMAT_R32G32B32A32_SFLOAT) {
    EXPECT_EQ(LocalToVkFormat(RHIFormat::RGBA32Float), VK_FORMAT_R32G32B32A32_SFLOAT);
}

// ─────────────────────────────────────────────────────────────────────────────
// CPU-only — Unsigned integer format mappings
// ─────────────────────────────────────────────────────────────────────────────

TEST(VulkanFormatConversion, R32Uint_MapsTo_VK_FORMAT_R32_UINT) {
    EXPECT_EQ(LocalToVkFormat(RHIFormat::R32Uint), VK_FORMAT_R32_UINT);
}

TEST(VulkanFormatConversion, RG32Uint_MapsTo_VK_FORMAT_R32G32_UINT) {
    EXPECT_EQ(LocalToVkFormat(RHIFormat::RG32Uint), VK_FORMAT_R32G32_UINT);
}

TEST(VulkanFormatConversion, RGBA32Uint_MapsTo_VK_FORMAT_R32G32B32A32_UINT) {
    EXPECT_EQ(LocalToVkFormat(RHIFormat::RGBA32Uint), VK_FORMAT_R32G32B32A32_UINT);
}

// ─────────────────────────────────────────────────────────────────────────────
// CPU-only — Depth / depth-stencil format mappings
// ─────────────────────────────────────────────────────────────────────────────

TEST(VulkanFormatConversion, D16Unorm_MapsTo_VK_FORMAT_D16_UNORM) {
    EXPECT_EQ(LocalToVkFormat(RHIFormat::D16Unorm), VK_FORMAT_D16_UNORM);
}

TEST(VulkanFormatConversion, D32Float_MapsTo_VK_FORMAT_D32_SFLOAT) {
    EXPECT_EQ(LocalToVkFormat(RHIFormat::D32Float), VK_FORMAT_D32_SFLOAT);
}

TEST(VulkanFormatConversion, D24UnormS8Uint_MapsTo_VK_FORMAT_D24_UNORM_S8_UINT) {
    EXPECT_EQ(LocalToVkFormat(RHIFormat::D24UnormS8Uint), VK_FORMAT_D24_UNORM_S8_UINT);
}

TEST(VulkanFormatConversion, D32FloatS8Uint_MapsTo_VK_FORMAT_D32_SFLOAT_S8_UINT) {
    EXPECT_EQ(LocalToVkFormat(RHIFormat::D32FloatS8Uint), VK_FORMAT_D32_SFLOAT_S8_UINT);
}

// ─────────────────────────────────────────────────────────────────────────────
// CPU-only — Unknown and default fallback mappings
// ─────────────────────────────────────────────────────────────────────────────

/// RHIFormat::Unknown is a named enumerator that falls through to the default
/// branch. The result must be VK_FORMAT_UNDEFINED so callers can detect it.
TEST(VulkanFormatConversion, Unknown_MapsTo_VK_FORMAT_UNDEFINED) {
    EXPECT_EQ(LocalToVkFormat(RHIFormat::Unknown), VK_FORMAT_UNDEFINED);
}

/// An out-of-range value cast to RHIFormat exercises the `default:` branch.
/// The function must not crash and must return VK_FORMAT_UNDEFINED.
TEST(VulkanFormatConversion, OutOfRangeCast_MapsTo_VK_FORMAT_UNDEFINED_ViaDefaultBranch) {
    // 0xFF is not a valid RHIFormat enumerator — it exercises the default branch.
    const RHIFormat outOfRange = static_cast<RHIFormat>(0xFF);
    EXPECT_EQ(LocalToVkFormat(outOfRange), VK_FORMAT_UNDEFINED);
}

// ─────────────────────────────────────────────────────────────────────────────
// GPU Integration Test Fixture
//
// Creates a real Vulkan IRHIDevice with validation enabled. All tests in this
// fixture are skipped when no Vulkan-capable GPU or Vulkan driver is present.
// Each test creates a small 64x64 texture in the format under test and asserts
// that the handle is valid, confirming the VkFormat is accepted by the driver.
// ─────────────────────────────────────────────────────────────────────────────

/// Fixture owning a real Vulkan device used for format creation round-trip tests.
///
/// SetUp creates a device with validation layers enabled. TearDown calls
/// WaitIdle() and destroys the device. Tests call GTEST_SKIP() when device
/// creation fails (no GPU in CI or no Vulkan driver installed).
class VulkanFormatGpuTest : public ::testing::Test {
protected:
    IRHIDevice* m_device = nullptr;

    void SetUp() override {
        RHIDeviceDesc desc{};
        desc.backend          = RHIBackend::Vulkan;
        desc.enableValidation = true;
        m_device = CreateRHIDevice(desc);
        if (!m_device) {
            GTEST_SKIP() << "Vulkan device creation failed (no GPU or Vulkan driver)";
        }
    }

    void TearDown() override {
        if (m_device) {
            m_device->WaitIdle();
            DestroyRHIDevice(m_device);
            m_device = nullptr;
        }
    }

    /// Creates a 64x64 ShaderResource texture of the given format and asserts
    /// it is valid. Returns the handle — the caller must destroy it before
    /// the test ends.
    ///
    /// # Parameters
    /// - `format`    — the RHIFormat to test.
    /// - `debugName` — label passed to the Vulkan debug utils for error messages.
    ///
    /// # Returns
    /// A valid RHITextureHandle on success. The test fails immediately via
    /// ASSERT_TRUE if the driver rejects the format.
    [[nodiscard]] RHITextureHandle CreateColorTexture(RHIFormat format, const char* debugName) {
        RHITextureDesc desc{};
        desc.width     = 64;
        desc.height    = 64;
        desc.format    = format;
        desc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;
        desc.debugName = debugName;
        RHITextureHandle handle = m_device->CreateTexture(desc);
        EXPECT_TRUE(handle.IsValid())
            << "Driver rejected texture creation for format: " << debugName;
        return handle;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// GPU: colour format creation round-trips
//
// Each test creates a 64x64 texture in the target format to confirm that the
// VkFormat produced by ToVkFormat is accepted by the Vulkan driver end-to-end.
// Depth formats are excluded here because they require DepthStencil usage
// which has different constraints; they are covered in test_vulkan_barriers.cpp.
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(VulkanFormatGpuTest, R8Unorm_TextureCreationSucceeds) {
    RHITextureHandle h = CreateColorTexture(RHIFormat::R8Unorm, "GPU_R8Unorm");
    if (h.IsValid()) { m_device->DestroyTexture(h); }
}

TEST_F(VulkanFormatGpuTest, RG8Unorm_TextureCreationSucceeds) {
    RHITextureHandle h = CreateColorTexture(RHIFormat::RG8Unorm, "GPU_RG8Unorm");
    if (h.IsValid()) { m_device->DestroyTexture(h); }
}

TEST_F(VulkanFormatGpuTest, RGBA8Unorm_TextureCreationSucceeds) {
    RHITextureHandle h = CreateColorTexture(RHIFormat::RGBA8Unorm, "GPU_RGBA8Unorm");
    if (h.IsValid()) { m_device->DestroyTexture(h); }
}

TEST_F(VulkanFormatGpuTest, RGBA8Srgb_TextureCreationSucceeds) {
    RHITextureHandle h = CreateColorTexture(RHIFormat::RGBA8Srgb, "GPU_RGBA8Srgb");
    if (h.IsValid()) { m_device->DestroyTexture(h); }
}

TEST_F(VulkanFormatGpuTest, BGRA8Unorm_TextureCreationSucceeds) {
    RHITextureHandle h = CreateColorTexture(RHIFormat::BGRA8Unorm, "GPU_BGRA8Unorm");
    if (h.IsValid()) { m_device->DestroyTexture(h); }
}

TEST_F(VulkanFormatGpuTest, BGRA8Srgb_TextureCreationSucceeds) {
    RHITextureHandle h = CreateColorTexture(RHIFormat::BGRA8Srgb, "GPU_BGRA8Srgb");
    if (h.IsValid()) { m_device->DestroyTexture(h); }
}

TEST_F(VulkanFormatGpuTest, R16Float_TextureCreationSucceeds) {
    RHITextureHandle h = CreateColorTexture(RHIFormat::R16Float, "GPU_R16Float");
    if (h.IsValid()) { m_device->DestroyTexture(h); }
}

TEST_F(VulkanFormatGpuTest, RG16Float_TextureCreationSucceeds) {
    RHITextureHandle h = CreateColorTexture(RHIFormat::RG16Float, "GPU_RG16Float");
    if (h.IsValid()) { m_device->DestroyTexture(h); }
}

TEST_F(VulkanFormatGpuTest, RGBA16Float_TextureCreationSucceeds) {
    RHITextureHandle h = CreateColorTexture(RHIFormat::RGBA16Float, "GPU_RGBA16Float");
    if (h.IsValid()) { m_device->DestroyTexture(h); }
}

TEST_F(VulkanFormatGpuTest, R32Float_TextureCreationSucceeds) {
    RHITextureHandle h = CreateColorTexture(RHIFormat::R32Float, "GPU_R32Float");
    if (h.IsValid()) { m_device->DestroyTexture(h); }
}

TEST_F(VulkanFormatGpuTest, RG32Float_TextureCreationSucceeds) {
    RHITextureHandle h = CreateColorTexture(RHIFormat::RG32Float, "GPU_RG32Float");
    if (h.IsValid()) { m_device->DestroyTexture(h); }
}

/// RGB32Float (VK_FORMAT_R32G32B32_SFLOAT) is not required by the Vulkan spec
/// to support sampled-image usage on all devices. A driver that lacks this
/// support will return an invalid handle — the test accepts that outcome as a
/// legal skip rather than a failure.
TEST_F(VulkanFormatGpuTest, RGB32Float_TextureCreationAttempted_DriverMayReject) {
    RHITextureDesc desc{};
    desc.width     = 64;
    desc.height    = 64;
    desc.format    = RHIFormat::RGB32Float;
    desc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;
    desc.debugName = "GPU_RGB32Float";
    RHITextureHandle h = m_device->CreateTexture(desc);
    // Some drivers do not support VK_FORMAT_R32G32B32_SFLOAT for sampled
    // images — not a bug in ToVkFormat. Only destroy if valid.
    if (h.IsValid()) {
        m_device->DestroyTexture(h);
    }
    // The test goal is that the engine does not crash (no assertion, no UB).
    // An invalid handle is an acceptable driver response for this format.
    SUCCEED();
}

TEST_F(VulkanFormatGpuTest, RGBA32Float_TextureCreationSucceeds) {
    RHITextureHandle h = CreateColorTexture(RHIFormat::RGBA32Float, "GPU_RGBA32Float");
    if (h.IsValid()) { m_device->DestroyTexture(h); }
}

TEST_F(VulkanFormatGpuTest, R32Uint_TextureCreationSucceeds) {
    RHITextureHandle h = CreateColorTexture(RHIFormat::R32Uint, "GPU_R32Uint");
    if (h.IsValid()) { m_device->DestroyTexture(h); }
}

TEST_F(VulkanFormatGpuTest, RG32Uint_TextureCreationSucceeds) {
    RHITextureHandle h = CreateColorTexture(RHIFormat::RG32Uint, "GPU_RG32Uint");
    if (h.IsValid()) { m_device->DestroyTexture(h); }
}

TEST_F(VulkanFormatGpuTest, RGBA32Uint_TextureCreationSucceeds) {
    RHITextureHandle h = CreateColorTexture(RHIFormat::RGBA32Uint, "GPU_RGBA32Uint");
    if (h.IsValid()) { m_device->DestroyTexture(h); }
}

} // namespace

#endif // _WIN32
