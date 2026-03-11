/// Unit tests for UploadContext.
///
/// Tests argument validation and staging buffer tracking.
/// GPU-side upload tests (actual copy) are in the DX12 integration tests
/// since they require a live device.

#include "upload_context.h"

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
