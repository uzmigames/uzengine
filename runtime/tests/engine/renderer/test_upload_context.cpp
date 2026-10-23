/// Unit tests for UploadContext.
///
/// Tests argument validation and staging buffer tracking.
/// GPU-side upload tests (actual copy) are in the DX12 integration tests
/// since they require a live device.

#include "upload_context.h"
#include "../vfx/mock_rhi_device.h"

#include <gtest/gtest.h>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Argument validation (no device needed)
// ─────────────────────────────────────────────────────────────────────────────

TEST(UploadContext, RejectsNullDevice) {
    UploadContext ctx(nullptr);
    uint8_t data = 42;
    EXPECT_FALSE(ctx.UploadBuffer(nullptr, RHIBufferHandle{}, &data, 1));
}

TEST(UploadContext, RejectsNullData) {
    // Device pointer is non-null but fake — we never dereference it because
    // the null-data check fires first.
    auto* fakeDevice = reinterpret_cast<IRHIDevice*>(0x1);
    auto* fakeCmdList = reinterpret_cast<IRHICommandList*>(0x1);
    UploadContext ctx(fakeDevice);
    EXPECT_FALSE(ctx.UploadBuffer(fakeCmdList, RHIBufferHandle{}, nullptr, 100));
}

TEST(UploadContext, RejectsZeroSize) {
    auto* fakeDevice = reinterpret_cast<IRHIDevice*>(0x1);
    auto* fakeCmdList = reinterpret_cast<IRHICommandList*>(0x1);
    UploadContext ctx(fakeDevice);
    uint8_t data = 42;
    EXPECT_FALSE(ctx.UploadBuffer(fakeCmdList, RHIBufferHandle{}, &data, 0));
}

TEST(UploadContext, RejectsNullCmdList) {
    auto* fakeDevice = reinterpret_cast<IRHIDevice*>(0x1);
    UploadContext ctx(fakeDevice);
    uint8_t data = 42;
    EXPECT_FALSE(ctx.UploadBuffer(nullptr, RHIBufferHandle{}, &data, 1));
}

TEST(UploadContext, StagingCountStartsAtZero) {
    UploadContext ctx(nullptr);
    EXPECT_EQ(ctx.GetStagingCount(), 0u);
}

TEST(UploadContext, ReleaseOnEmptyIsNoOp) {
    UploadContext ctx(nullptr);
    ctx.ReleaseStaging(); // Should not crash
    EXPECT_EQ(ctx.GetStagingCount(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// UploadTexture — Argument validation (no device needed)
// ─────────────────────────────────────────────────────────────────────────────

TEST(UploadContext, UploadTextureRejectsNullDevice) {
    UploadContext ctx(nullptr);
    uint8_t data = 42;
    EXPECT_FALSE(ctx.UploadTexture(nullptr, RHITextureHandle{}, &data, 1));
}

TEST(UploadContext, UploadTextureRejectsNullData) {
    auto* fakeDevice = reinterpret_cast<IRHIDevice*>(0x1);
    auto* fakeCmdList = reinterpret_cast<IRHICommandList*>(0x1);
    UploadContext ctx(fakeDevice);
    EXPECT_FALSE(ctx.UploadTexture(fakeCmdList, RHITextureHandle{}, nullptr, 100));
}

TEST(UploadContext, UploadTextureRejectsZeroSize) {
    auto* fakeDevice = reinterpret_cast<IRHIDevice*>(0x1);
    auto* fakeCmdList = reinterpret_cast<IRHICommandList*>(0x1);
    UploadContext ctx(fakeDevice);
    uint8_t data = 42;
    EXPECT_FALSE(ctx.UploadTexture(fakeCmdList, RHITextureHandle{}, &data, 0));
}

TEST(UploadContext, UploadTextureRejectsNullCmdList) {
    auto* fakeDevice = reinterpret_cast<IRHIDevice*>(0x1);
    UploadContext ctx(fakeDevice);
    uint8_t data = 42;
    EXPECT_FALSE(ctx.UploadTexture(nullptr, RHITextureHandle{}, &data, 1));
}

TEST(UploadContext, UploadTextureRejectsInvalidHandle) {
    auto* fakeDevice = reinterpret_cast<IRHIDevice*>(0x1);
    auto* fakeCmdList = reinterpret_cast<IRHICommandList*>(0x1);
    UploadContext ctx(fakeDevice);
    uint8_t data = 42;
    RHITextureHandle invalid = RHITextureHandle::Invalid();
    EXPECT_FALSE(ctx.UploadTexture(fakeCmdList, invalid, &data, 4));
}

// ─────────────────────────────────────────────────────────────────────────────
// UploadTexture — source size vs. mip layout (defense in depth at the sink)
//
// MockRHIDevice::GetTextureDesc returns a default RHITextureDesc: 1x1 RGBA8,
// one mip, so a full mip needs 4 source bytes. The rejected calls must return
// before creating a staging buffer and before touching the command list.
// ─────────────────────────────────────────────────────────────────────────────

TEST(UploadContext, UploadTextureRejectsSourceSmallerThanMip) {
    uz::test::MockRHIDevice device;
    auto* fakeCmdList = reinterpret_cast<IRHICommandList*>(0x1);
    UploadContext ctx(&device);
    const uint8_t data[3] = {1, 2, 3};

    EXPECT_FALSE(ctx.UploadTexture(fakeCmdList, RHITextureHandle{1, 1}, data, sizeof(data)));
    EXPECT_EQ(device.buffersCreated.load(), 0u);
    EXPECT_EQ(ctx.GetStagingCount(), 0u);
}

TEST(UploadContext, UploadTextureRejectsMipLevelOutOfRange) {
    uz::test::MockRHIDevice device;
    auto* fakeCmdList = reinterpret_cast<IRHICommandList*>(0x1);
    UploadContext ctx(&device);
    const uint8_t data[4] = {1, 2, 3, 4};

    EXPECT_FALSE(ctx.UploadTexture(fakeCmdList, RHITextureHandle{1, 1}, data, sizeof(data), 1));
    EXPECT_FALSE(ctx.UploadTexture(fakeCmdList, RHITextureHandle{1, 1}, data, sizeof(data), 40));
    EXPECT_EQ(device.buffersCreated.load(), 0u);
}
