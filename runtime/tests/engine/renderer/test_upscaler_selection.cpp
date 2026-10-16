// Compile the actual SDK-less branches under distinct class names. The runtime
// library can have SDKs enabled independently of the test module's macros.
#define UZ_FSR2_ENABLED 0
#define FSR2Upscaler Fsr2StubForSelectionTest
#include "engine/renderer/passes/postprocess/fsr2_upscaler.cpp"
#undef FSR2Upscaler
#undef UZ_FSR2_ENABLED
#define UZ_DLSS_ENABLED 0
#define DlssUpscaler DlssStubForSelectionTest
#include "engine/renderer/passes/postprocess/dlss_upscaler.cpp"
#undef DlssUpscaler
#undef UZ_DLSS_ENABLED

#include "engine/renderer/scene/scene_renderer.h"
#include "engine/renderer/passes/postprocess/tsr_upscaler.h"
#include "engine/renderer/passes/postprocess/taa_upscaler.h"
#include "cvar.h"

#include <gtest/gtest.h>

#include <vector>

namespace {
using namespace uz;

struct Probe {
    UpscalerType unavailable = UpscalerType::None;
    UpscalerType initFailed = UpscalerType::None;
    std::vector<UpscalerType> availableCalls, initCalls;
    UpscalerProbe Get() {
        return {
            [this](UpscalerType type) -> const char* {
                availableCalls.push_back(type);
                if (type == UpscalerType::XeSS) return "not implemented";
                if (type == unavailable)
                    return type == UpscalerType::TSR ? "TSR shader not implemented" : "not built with SDK";
                return nullptr;
            },
            [this](UpscalerType type) {
                initCalls.push_back(type);
                return type != initFailed;
            }};
    }
};

TEST(UpscalerSelection, Fsr2Unavailable_FallsToTaa) {
    Probe probe;
    probe.unavailable = UpscalerType::FSR2;
    const auto selection = ResolveUpscalerSelection(UpscalerType::FSR2, probe.Get());
    EXPECT_EQ(selection.effective, UpscalerType::TAA);
    EXPECT_EQ(probe.initCalls, std::vector{UpscalerType::TAA});
}

TEST(UpscalerSelection, DlssUnavailable_FallsToTaa) {
    Probe probe;
    probe.unavailable = UpscalerType::DLSS;
    EXPECT_EQ(ResolveUpscalerSelection(UpscalerType::DLSS, probe.Get()).effective, UpscalerType::TAA);
    EXPECT_EQ(probe.initCalls, std::vector{UpscalerType::TAA});
}

TEST(UpscalerSelection, Xess_NotImplemented_FallsToTaa) {
    Probe probe;
    const auto selection = ResolveUpscalerSelection(UpscalerType::XeSS, probe.Get());
    EXPECT_EQ(selection.effective, UpscalerType::TAA);
    EXPECT_STREQ(selection.reason, "not implemented");
    EXPECT_EQ(CreateTemporalUpscaler(UpscalerType::XeSS), nullptr);
}

TEST(UpscalerSelection, Tsr_Unavailable_FallsToTaa) {
    Probe probe;
    probe.unavailable = UpscalerType::TSR;
    const auto selection = ResolveUpscalerSelection(UpscalerType::TSR, probe.Get());
    EXPECT_EQ(selection.effective, UpscalerType::TAA);
    EXPECT_STREQ(selection.reason, "TSR shader not implemented");
    TsrUpscaler tsr;
    EXPECT_FALSE(tsr.IsAvailable());
    EXPECT_FALSE(tsr.Init(nullptr, nullptr, nullptr));
    EXPECT_FALSE(tsr.Execute(nullptr, {}));
}

TEST(UpscalerSelection, TaaInitFails_FallsToNone) {
    Probe probe;
    probe.initFailed = UpscalerType::TAA;
    const auto selection = ResolveUpscalerSelection(UpscalerType::TAA, probe.Get());
    EXPECT_EQ(selection.effective, UpscalerType::None);
    EXPECT_STREQ(selection.reason, "init failed");
    EXPECT_EQ(probe.initCalls, std::vector{UpscalerType::TAA});
}

TEST(UpscalerSelection, None_IsNone_NoProbe) {
    Probe probe;
    const auto selection = ResolveUpscalerSelection(UpscalerType::None, probe.Get());
    EXPECT_EQ(selection.requested, UpscalerType::None);
    EXPECT_EQ(selection.effective, UpscalerType::None);
    EXPECT_TRUE(probe.availableCalls.empty());
    EXPECT_TRUE(probe.initCalls.empty());
}

TEST(UpscalerSelection, RequestedAndReason_Recorded) {
    Probe probe;
    probe.unavailable = UpscalerType::FSR2;
    const auto selection = ResolveUpscalerSelection(UpscalerType::FSR2, probe.Get());
    EXPECT_EQ(selection.requested, UpscalerType::FSR2);
    EXPECT_STREQ(selection.reason, "not built with SDK");
}

TEST(UpscalerSelection, SameRequest_NoReselect) {
    // No device: even the fallback TAA cannot initialize. A repeated request
    // must still retain the already-resolved selection rather than retry it.
    SceneRenderer renderer;
    auto& cvars = CVarRegistry::Get();
    cvars.Register("r.AntiAliasingMethod", 1.f, "Anti-aliasing method", 0.f, 5.f);
    const float oldRequest = cvars.GetFloat("r.AntiAliasingMethod", 1.f);
    const auto& selection = renderer.SetUpscalerType(UpscalerType::FSR2);
    EXPECT_EQ(selection.requested, UpscalerType::FSR2);
    EXPECT_EQ(selection.effective, UpscalerType::None);
    EXPECT_EQ(cvars.GetInt("r.AntiAliasingMethod"), 3);
    // Selection synchronizes this CVar. An unchanged value below proves the
    // second call did not run selection (including its availability/init probe).
    cvars.Set("r.AntiAliasingMethod", 4.f);
    const auto& same = renderer.SetUpscalerType(UpscalerType::FSR2);
    EXPECT_EQ(&same, &selection);
    EXPECT_EQ(renderer.GetUpscalerType(), UpscalerType::None);
    EXPECT_EQ(renderer.GetUpscaler(), nullptr);
    EXPECT_EQ(cvars.GetInt("r.AntiAliasingMethod"), 4);
    cvars.Set("r.AntiAliasingMethod", oldRequest);
}

TEST(UpscalerSelection, Failover_SkipsFailedEntry) {
    Probe probe;
    auto selection = ResolveUpscalerSelection(UpscalerType::FSR2, probe.Get(), UpscalerType::FSR2);
    EXPECT_EQ(selection.requested, UpscalerType::FSR2);
    EXPECT_EQ(selection.effective, UpscalerType::TAA);
    EXPECT_EQ(probe.availableCalls, std::vector{UpscalerType::TAA});
    probe.availableCalls.clear();
    selection = ResolveUpscalerSelection(UpscalerType::TAA, probe.Get(), UpscalerType::TAA);
    EXPECT_EQ(selection.effective, UpscalerType::None);
    EXPECT_TRUE(probe.availableCalls.empty());
    selection = ResolveUpscalerSelection(UpscalerType::FSR2, probe.Get(), UpscalerType::TAA);
    EXPECT_EQ(selection.effective, UpscalerType::None);
    EXPECT_TRUE(probe.availableCalls.empty());
}

TEST(UpscalerSelection, AvailableRequest_IsUsed) {
    Probe probe;
    const auto selection = ResolveUpscalerSelection(UpscalerType::TAA, probe.Get());
    EXPECT_EQ(selection.effective, UpscalerType::TAA);
    EXPECT_STREQ(selection.reason, "ok");
    EXPECT_EQ(probe.initCalls, std::vector{UpscalerType::TAA});
    TaaUpscaler taa;
    EXPECT_TRUE(taa.IsAvailable());
    EXPECT_FALSE(taa.Init(nullptr, nullptr, nullptr));
}

TEST(DisplaySource, Temporal_WriteSlotAtOutputSize) {
    const RHITextureHandle texture{7, 1};
    const auto source = FrameContext::DisplaySource::Temporal(11, texture, 12, 1920.f, 1080.f);
    EXPECT_EQ(source.srv, 11u);
    EXPECT_EQ(source.texture, texture);
    EXPECT_EQ(source.uav, 12u);
    EXPECT_FLOAT_EQ(source.width, 1920.f);
    EXPECT_FLOAT_EQ(source.height, 1080.f);
    EXPECT_TRUE(source.temporal);
}

TEST(DisplaySource, Hdr_RenderSize_NoUav) {
    const RHITextureHandle texture{8, 1};
    const auto source = FrameContext::DisplaySource::Hdr(13, texture, 1280.f, 720.f);
    EXPECT_EQ(source.srv, 13u);
    EXPECT_EQ(source.texture, texture);
    EXPECT_EQ(source.uav, UINT32_MAX);
    EXPECT_FLOAT_EQ(source.width, 1280.f);
    EXPECT_FLOAT_EQ(source.height, 720.f);
    EXPECT_FALSE(source.temporal);
}

TEST(DisplaySource, ExecuteFailure_CurrentHdrAndThreeFrameFailover) {
    FrameContext ctx;
    uint32_t streak = 0;
    const auto hdr = FrameContext::DisplaySource::Hdr(13, {8, 1}, 1280.f, 720.f);
    Probe probe;
    TemporalHistoryState history;
    TemporalFrameKey key{};
    key.upscalerType = static_cast<uint32_t>(UpscalerType::TAA);
    key.outputW = 1920;
    key.outputH = 1080;
    for (uint32_t frame = 0; frame < 3; ++frame) {
        key.frameIndex = frame;
        const uint32_t write = frame % 2;
        const auto decision = history.Advance(key, 1 - write, {});
        EXPECT_TRUE(decision.firstFrame());
        ctx.display = FrameContext::DisplaySource::Temporal(11, {7, 1}, 12, 1920.f, 1080.f);
        // Mock producer reports exactly the runtime contract: no write recorded.
        ctx.ResolveDisplaySource(false, hdr, streak);
        history.MarkFailed(write);
        EXPECT_FALSE(history.SlotWritten(write));
        EXPECT_EQ(ctx.display.texture, hdr.texture);
        EXPECT_EQ(ctx.display.srv, hdr.srv);
        EXPECT_EQ(ctx.display.uav, UINT32_MAX);
        EXPECT_FLOAT_EQ(ctx.display.width, 1280.f);
        EXPECT_FLOAT_EQ(ctx.display.height, 720.f);
        EXPECT_FALSE(ctx.display.temporal);
        EXPECT_EQ(streak, frame + 1);
    }
    ASSERT_EQ(streak, 3u);
    EXPECT_EQ(ResolveUpscalerSelection(UpscalerType::TAA, probe.Get(), UpscalerType::TAA).effective,
              UpscalerType::None);
}

TEST(DisplaySource, ExecuteSuccess_KeepsHistoryAndResetsStreak) {
    FrameContext ctx;
    uint32_t streak = 2;
    ctx.display = FrameContext::DisplaySource::Temporal(11, {7, 1}, 12, 1920.f, 1080.f);
    ctx.ResolveDisplaySource(true, FrameContext::DisplaySource::Hdr(13, {8, 1}, 1280.f, 720.f), streak);
    EXPECT_EQ(streak, 0u);
    EXPECT_EQ(ctx.display.srv, 11u);
    EXPECT_EQ(ctx.display.texture, (RHITextureHandle{7, 1}));
    EXPECT_TRUE(ctx.display.temporal);
    EXPECT_FLOAT_EQ(ctx.display.width, 1920.f);
}

TEST(FSR2UpscalerGuard, StubBuild_IsAvailable_False) {
    Fsr2StubForSelectionTest upscaler;
    EXPECT_FALSE(upscaler.IsAvailable());
}
TEST(FSR2UpscalerGuard, StubBuild_Init_ReturnsFalse) {
    Fsr2StubForSelectionTest upscaler;
    EXPECT_FALSE(upscaler.Init(nullptr, nullptr, nullptr));
}
TEST(FSR2UpscalerGuard, StubBuild_Execute_ReturnsFalse) {
    Fsr2StubForSelectionTest upscaler;
    EXPECT_FALSE(upscaler.Execute(nullptr, {}));
}
TEST(DlssUpscalerGuard, StubBuild_IsAvailable_False) {
    DlssStubForSelectionTest upscaler;
    EXPECT_FALSE(upscaler.IsAvailable());
}
TEST(DlssUpscalerGuard, StubBuild_Init_ReturnsFalse) {
    DlssStubForSelectionTest upscaler;
    EXPECT_FALSE(upscaler.Init(nullptr, nullptr, nullptr));
}
TEST(DlssUpscalerGuard, StubBuild_Execute_ReturnsFalse) {
    DlssStubForSelectionTest upscaler;
    EXPECT_FALSE(upscaler.Execute(nullptr, {}));
}
} // namespace
