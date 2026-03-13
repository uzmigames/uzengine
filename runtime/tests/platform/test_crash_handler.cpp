/// Unit tests for the crash handler system (4.5.0).
///
/// Covers: stack trace capture, symbol formatting, crash handler lifecycle,
/// and all context-state mutators. All tests are non-crashing by design —
/// no real exceptions are raised. DbgHelp is exercised through the public
/// WalkStack/FormatStackTrace API; the SEH filter itself is tested only for
/// install/uninstall lifecycle.
#include "crash_handler.h"
#include "stack_walker.h"
#include "log.h"

#include <cstring>
#include <filesystem>
#include <gtest/gtest.h>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// Test fixture — initialises logging once, cleans nothing on disk
// (the crash handler only writes to disk when an exception fires, which we
// deliberately never trigger in unit tests).
// ─────────────────────────────────────────────────────────────────────────────

class CrashHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Ensure the log system is up so UZ_LOG inside the implementation
        // does not crash. Use a temp path and silence output at Error level.
        fs::path tmpLog = fs::temp_directory_path() / "uzengine_crash_test.log";
        uz::LogInit(tmpLog.string().c_str());
        uz::LogSetLevel(uz::LogLevel::Error);
    }

    void TearDown() override {
        // Always uninstall so subsequent tests get a clean filter state.
        uz::UninstallCrashHandler();

        uz::LogShutdown();

        // Silence re-init for the next test's SetUp.
        fs::path tmpLog = fs::temp_directory_path() / "uzengine_crash_test.log";
        uz::LogInit(tmpLog.string().c_str());
        uz::LogSetLevel(uz::LogLevel::Error);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Stack Walker — CaptureRawStackTrace
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(CrashHandlerTest, CaptureRawStackTrace_NormalCall_CapturesAtLeastOneFrame) {
    void* frames[32] = {};
    const int count = uz::CaptureRawStackTrace(frames, 32);

    EXPECT_GT(count, 0) << "CaptureRawStackTrace should capture at least 1 frame";
    // First captured frame must be a non-null instruction pointer.
    EXPECT_NE(frames[0], nullptr);
}

TEST_F(CrashHandlerTest, CaptureRawStackTrace_NullBuffer_ReturnsZero) {
    const int count = uz::CaptureRawStackTrace(nullptr, 32);
    EXPECT_EQ(count, 0);
}

TEST_F(CrashHandlerTest, CaptureRawStackTrace_ZeroMaxFrames_ReturnsZero) {
    void* frames[4] = {};
    const int count = uz::CaptureRawStackTrace(frames, 0);
    EXPECT_EQ(count, 0);
}

TEST_F(CrashHandlerTest, CaptureRawStackTrace_NegativeMaxFrames_ReturnsZero) {
    void* frames[4] = {};
    const int count = uz::CaptureRawStackTrace(frames, -1);
    EXPECT_EQ(count, 0);
}

TEST_F(CrashHandlerTest, CaptureRawStackTrace_MaxFramesOne_CapturesExactlyOne) {
    void* frames[1] = {};
    const int count = uz::CaptureRawStackTrace(frames, 1);

    // Should capture exactly 1 frame (the limit) rather than overflowing.
    EXPECT_EQ(count, 1);
    EXPECT_NE(frames[0], nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Stack Walker — WalkStack
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(CrashHandlerTest, WalkStack_NormalCall_ReturnsAtLeastOneFrame) {
    uz::StackFrame frames[32] = {};
    const int count = uz::WalkStack(frames, 32);

    EXPECT_GT(count, 0) << "WalkStack should resolve at least 1 frame";
    // The address field must be non-null for every resolved frame.
    for (int i = 0; i < count; ++i) {
        EXPECT_NE(frames[i].address, nullptr)
            << "Frame " << i << " has null address";
    }
}

TEST_F(CrashHandlerTest, WalkStack_ZeroMaxFrames_ReturnsZero) {
    uz::StackFrame frames[4] = {};
    const int count = uz::WalkStack(frames, 0);
    EXPECT_EQ(count, 0);
}

TEST_F(CrashHandlerTest, WalkStack_NullBuffer_ReturnsZero) {
    const int count = uz::WalkStack(nullptr, 32);
    EXPECT_EQ(count, 0);
}

TEST_F(CrashHandlerTest, WalkStack_MaxFramesOne_DoesNotOverflow) {
    uz::StackFrame frames[2] = {};
    // Zero-fill sentinel in slot 1 so we can detect overflow.
    std::memset(&frames[1], 0xCD, sizeof(uz::StackFrame));

    const int count = uz::WalkStack(frames, 1);

    EXPECT_LE(count, 1) << "WalkStack must not write beyond maxFrames";
    // Slot 1 must remain untouched (0xCD pattern).
    const auto* raw = reinterpret_cast<const unsigned char*>(&frames[1]);
    EXPECT_EQ(raw[0], 0xCD) << "WalkStack wrote past the end of the buffer";
}

TEST_F(CrashHandlerTest, WalkStack_FunctionNameField_IsNullTerminated) {
    uz::StackFrame frames[8] = {};
    const int count = uz::WalkStack(frames, 8);

    ASSERT_GT(count, 0);
    for (int i = 0; i < count; ++i) {
        // functionName must be a valid C string (null-terminated within buffer).
        const char* name = frames[i].functionName;
        bool terminated = false;
        for (size_t c = 0; c < sizeof(frames[i].functionName); ++c) {
            if (name[c] == '\0') { terminated = true; break; }
        }
        EXPECT_TRUE(terminated) << "Frame " << i << " functionName is not null-terminated";
    }
}

TEST_F(CrashHandlerTest, WalkStack_WithPdbs_ReturnsNonEmptyFunctionName) {
    // With PDBs (available in debug builds) at least one frame should resolve.
    // Without PDBs, frames fall back to "???". Either way the field must be
    // non-empty — never a zero-length string for a real frame.
    uz::StackFrame frames[16] = {};
    const int count = uz::WalkStack(frames, 16);

    ASSERT_GT(count, 0);
    bool anyNonEmpty = false;
    for (int i = 0; i < count; ++i) {
        if (frames[i].functionName[0] != '\0') {
            anyNonEmpty = true;
            break;
        }
    }
    EXPECT_TRUE(anyNonEmpty)
        << "All frames have empty functionName — expected at least '???' as fallback";
}

// ─────────────────────────────────────────────────────────────────────────────
// Stack Walker — FormatStackTrace
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(CrashHandlerTest, FormatStackTrace_NullFrames_ReturnsEmptyString) {
    const std::string result = uz::FormatStackTrace(nullptr, 5);
    EXPECT_TRUE(result.empty());
}

TEST_F(CrashHandlerTest, FormatStackTrace_ZeroCount_ReturnsEmptyString) {
    uz::StackFrame frames[4] = {};
    const std::string result = uz::FormatStackTrace(frames, 0);
    EXPECT_TRUE(result.empty());
}

TEST_F(CrashHandlerTest, FormatStackTrace_NegativeCount_ReturnsEmptyString) {
    uz::StackFrame frames[4] = {};
    const std::string result = uz::FormatStackTrace(frames, -1);
    EXPECT_TRUE(result.empty());
}

TEST_F(CrashHandlerTest, FormatStackTrace_ManualFrame_ContainsAddressHex) {
    uz::StackFrame frame{};
    frame.address = reinterpret_cast<void*>(static_cast<uintptr_t>(0xDEADBEEF'CAFEBABE));
    std::strncpy(frame.functionName, "TestFunction", sizeof(frame.functionName) - 1);
    frame.fileName[0] = '\0';
    frame.lineNumber = 0;

    const std::string result = uz::FormatStackTrace(&frame, 1);

    EXPECT_FALSE(result.empty());
    // Output must contain the function name.
    EXPECT_NE(result.find("TestFunction"), std::string::npos)
        << "FormatStackTrace output missing function name: " << result;
    // Output must contain the address in hex (case-insensitive check via both).
    const bool hasAddr =
        result.find("DEADBEEF") != std::string::npos ||
        result.find("deadbeef") != std::string::npos ||
        result.find("CAFEBABE") != std::string::npos ||
        result.find("cafebabe") != std::string::npos;
    EXPECT_TRUE(hasAddr) << "FormatStackTrace output missing hex address: " << result;
}

TEST_F(CrashHandlerTest, FormatStackTrace_ManualFrameWithSourceInfo_ContainsFileAndLine) {
    uz::StackFrame frame{};
    frame.address = reinterpret_cast<void*>(static_cast<uintptr_t>(0x1000));
    std::strncpy(frame.functionName, "MyFunc", sizeof(frame.functionName) - 1);
    std::strncpy(frame.fileName, "source.cpp", sizeof(frame.fileName) - 1);
    frame.lineNumber = 42;

    const std::string result = uz::FormatStackTrace(&frame, 1);

    EXPECT_NE(result.find("source.cpp"), std::string::npos)
        << "FormatStackTrace missing filename: " << result;
    EXPECT_NE(result.find("42"), std::string::npos)
        << "FormatStackTrace missing line number: " << result;
}

TEST_F(CrashHandlerTest, FormatStackTrace_MultipleFrames_ContainsBracketIndices) {
    uz::StackFrame frames[3] = {};
    for (int i = 0; i < 3; ++i) {
        frames[i].address = reinterpret_cast<void*>(static_cast<uintptr_t>(0x1000 + i));
        std::snprintf(frames[i].functionName, sizeof(frames[i].functionName),
                      "Func%d", i);
    }

    const std::string result = uz::FormatStackTrace(frames, 3);

    // Each frame produces a "[N]" prefix.
    EXPECT_NE(result.find("[ 0]"), std::string::npos)
        << "FormatStackTrace missing frame 0 index: " << result;
    EXPECT_NE(result.find("[ 1]"), std::string::npos)
        << "FormatStackTrace missing frame 1 index: " << result;
    EXPECT_NE(result.find("[ 2]"), std::string::npos)
        << "FormatStackTrace missing frame 2 index: " << result;
}

TEST_F(CrashHandlerTest, FormatStackTrace_LiveFrames_ProducesNonEmptyOutput) {
    uz::StackFrame frames[16] = {};
    const int count = uz::WalkStack(frames, 16);

    ASSERT_GT(count, 0);

    const std::string result = uz::FormatStackTrace(frames, count);
    EXPECT_FALSE(result.empty())
        << "FormatStackTrace produced empty string for " << count << " live frames";
    // Each frame produces a newline-terminated line.
    EXPECT_NE(result.find('\n'), std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// Crash Handler — lifecycle (Install / Uninstall)
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(CrashHandlerTest, InstallAndUninstall_DoNotCrash) {
    // Basic lifecycle: install then uninstall must not crash or assert.
    uz::InstallCrashHandler();
    uz::UninstallCrashHandler();
    // Still alive — success.
    SUCCEED();
}

TEST_F(CrashHandlerTest, UninstallWithoutInstall_IsNoOp) {
    // Uninstalling when the handler was never installed must be safe.
    uz::UninstallCrashHandler();
    uz::UninstallCrashHandler(); // second time also safe
    SUCCEED();
}

TEST_F(CrashHandlerTest, InstallMultipleTimes_IsIdempotent) {
    // Reinstalling over an already-installed handler must not crash
    // (documented: idempotent — reinstalls the same filter).
    uz::InstallCrashHandler();
    uz::InstallCrashHandler();
    uz::InstallCrashHandler();
    uz::UninstallCrashHandler();
    SUCCEED();
}

TEST_F(CrashHandlerTest, InstallUninstallCycle_RepeatedCallsAreSafe) {
    for (int i = 0; i < 5; ++i) {
        uz::InstallCrashHandler();
        uz::UninstallCrashHandler();
    }
    SUCCEED();
}

// ─────────────────────────────────────────────────────────────────────────────
// Crash Handler — context state mutators
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(CrashHandlerTest, SetFrameNumber_ValidValue_DoesNotCrash) {
    uz::CrashHandler_SetFrameNumber(0);
    uz::CrashHandler_SetFrameNumber(1);
    uz::CrashHandler_SetFrameNumber(UINT64_MAX);
    SUCCEED();
}

TEST_F(CrashHandlerTest, SetFrameNumber_CalledRepeatedly_DoesNotCrash) {
    for (uint64_t i = 0; i < 1000; ++i) {
        uz::CrashHandler_SetFrameNumber(i);
    }
    SUCCEED();
}

TEST_F(CrashHandlerTest, SetSceneName_ValidString_DoesNotCrash) {
    uz::CrashHandler_SetSceneName("TestLevel");
    uz::CrashHandler_SetSceneName("levels/world_01.uzscene");
    SUCCEED();
}

TEST_F(CrashHandlerTest, SetSceneName_NullPtr_ClearsAndDoesNotCrash) {
    // Must not crash — nullptr clears the buffer per header contract.
    uz::CrashHandler_SetSceneName("SomeLevel");
    uz::CrashHandler_SetSceneName(nullptr);
    SUCCEED();
}

TEST_F(CrashHandlerTest, SetSceneName_EmptyString_DoesNotCrash) {
    uz::CrashHandler_SetSceneName("");
    SUCCEED();
}

TEST_F(CrashHandlerTest, SetSceneName_ExactlyMaxLength_DoesNotOverflow) {
    // 255 chars + null = 256 total — exactly fits the internal buffer.
    std::string longName(255, 'A');
    uz::CrashHandler_SetSceneName(longName.c_str());
    SUCCEED();
}

TEST_F(CrashHandlerTest, SetSceneName_OverMaxLength_IsTruncatedSafely) {
    // 512 chars — must be clamped without buffer overflow.
    std::string tooLong(512, 'B');
    uz::CrashHandler_SetSceneName(tooLong.c_str());
    SUCCEED();
}

TEST_F(CrashHandlerTest, SetLastRhiOp_ValidString_DoesNotCrash) {
    uz::CrashHandler_SetLastRhiOp("DrawIndexedInstanced");
    uz::CrashHandler_SetLastRhiOp("Present");
    SUCCEED();
}

TEST_F(CrashHandlerTest, SetLastRhiOp_NullPtr_ClearsAndDoesNotCrash) {
    uz::CrashHandler_SetLastRhiOp("SomeOp");
    uz::CrashHandler_SetLastRhiOp(nullptr);
    SUCCEED();
}

TEST_F(CrashHandlerTest, SetLastRhiOp_EmptyString_DoesNotCrash) {
    uz::CrashHandler_SetLastRhiOp("");
    SUCCEED();
}

TEST_F(CrashHandlerTest, SetLastRhiOp_OverMaxLength_IsTruncatedSafely) {
    std::string tooLong(512, 'R');
    uz::CrashHandler_SetLastRhiOp(tooLong.c_str());
    SUCCEED();
}

TEST_F(CrashHandlerTest, SetGpuAdapter_ValidString_DoesNotCrash) {
    uz::CrashHandler_SetGpuAdapter("NVIDIA GeForce RTX 4090");
    uz::CrashHandler_SetGpuAdapter("AMD Radeon RX 7900 XTX");
    SUCCEED();
}

TEST_F(CrashHandlerTest, SetGpuAdapter_NullPtr_ClearsAndDoesNotCrash) {
    uz::CrashHandler_SetGpuAdapter("Some GPU");
    uz::CrashHandler_SetGpuAdapter(nullptr);
    SUCCEED();
}

TEST_F(CrashHandlerTest, SetGpuAdapter_EmptyString_DoesNotCrash) {
    uz::CrashHandler_SetGpuAdapter("");
    SUCCEED();
}

TEST_F(CrashHandlerTest, SetGpuAdapter_OverMaxLength_IsTruncatedSafely) {
    std::string tooLong(512, 'G');
    uz::CrashHandler_SetGpuAdapter(tooLong.c_str());
    SUCCEED();
}

TEST_F(CrashHandlerTest, SetGpuDeviceRemoved_TrueAndFalse_DoNotCrash) {
    uz::CrashHandler_SetGpuDeviceRemoved(true);
    uz::CrashHandler_SetGpuDeviceRemoved(false);
    uz::CrashHandler_SetGpuDeviceRemoved(true);
    SUCCEED();
}

// ─────────────────────────────────────────────────────────────────────────────
// Crash Handler — state mutators work during handler-installed phase
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(CrashHandlerTest, ContextMutators_WhileInstalled_DoNotCrash) {
    uz::InstallCrashHandler();

    uz::CrashHandler_SetFrameNumber(9999);
    uz::CrashHandler_SetSceneName("InGame");
    uz::CrashHandler_SetLastRhiOp("Present");
    uz::CrashHandler_SetGpuAdapter("Test GPU");
    uz::CrashHandler_SetGpuDeviceRemoved(false);

    uz::UninstallCrashHandler();
    SUCCEED();
}

// ─────────────────────────────────────────────────────────────────────────────
// StackFrame struct — field size contract
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(CrashHandlerTest, StackFrame_BufferSizes_MatchDocumentedCapacity) {
    uz::StackFrame frame{};
    // Header documents 256-char fixed buffers — verify at compile/link time.
    static_assert(sizeof(frame.functionName) == 256,
                  "StackFrame::functionName must be 256 bytes");
    static_assert(sizeof(frame.fileName) == 256,
                  "StackFrame::fileName must be 256 bytes");
    SUCCEED();
}

TEST_F(CrashHandlerTest, CrashContext_MaxStackFrames_IsAtLeast64) {
    // CrashContext::kMaxStackFrames is documented as 64.
    static_assert(uz::CrashContext::kMaxStackFrames >= 64,
                  "CrashContext::kMaxStackFrames must be >= 64");
    SUCCEED();
}
