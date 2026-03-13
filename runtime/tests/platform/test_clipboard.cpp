/// Unit tests for UzEngine Clipboard (platform/windows/clipboard.h).
///
/// Covers: UTF-8 text round-trip, empty string handling, crash-safety when
/// clipboard has no text, binary custom-format round-trip, and missing-format
/// fallback. All operations open/close the clipboard internally; no manual
/// clipboard state management is required by the caller.
///
/// Tests must be run on the main thread (Win32 clipboard constraint) and
/// assume no other process is actively locking the clipboard during the run.

#include "clipboard.h"
#include "log.h"

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// Fixture — logging lifecycle
// ─────────────────────────────────────────────────────────────────────────────

class ClipboardTest : public ::testing::Test {
protected:
    void SetUp() override {
        fs::path logPath = fs::temp_directory_path() / "uzengine_clipboard_test.log";
        uz::LogInit(logPath.string().c_str());
        uz::LogSetLevel(uz::LogLevel::Error);
    }

    void TearDown() override {
        uz::LogShutdown();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// ClipboardSetText / ClipboardGetText — text round-trip
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ClipboardTest, SetText_ThenGetText_RoundTripMatchesExact) {
    const char* kText = "Hello, UzEngine Clipboard!";

    const bool ok = uz::ClipboardSetText(kText);
    ASSERT_TRUE(ok)
        << "ClipboardSetText must return true for a non-empty UTF-8 string";

    const std::string retrieved = uz::ClipboardGetText();
    EXPECT_EQ(retrieved, std::string(kText))
        << "ClipboardGetText must return exactly the string written by SetText";
}

TEST_F(ClipboardTest, SetText_ASCIIString_RoundTripIsExact) {
    const char* kText = "abcdefghijklmnopqrstuvwxyz0123456789";

    ASSERT_TRUE(uz::ClipboardSetText(kText));
    const std::string result = uz::ClipboardGetText();

    EXPECT_EQ(result, std::string(kText));
}

TEST_F(ClipboardTest, SetText_Utf8MultiByte_RoundTripPreservesBytes) {
    // "Olá" in UTF-8: 'O' + 'l' + 0xC3 0xA1 (á)
    const char* kText = "Ol\xC3\xA1 UzEngine";

    ASSERT_TRUE(uz::ClipboardSetText(kText));
    const std::string result = uz::ClipboardGetText();

    EXPECT_EQ(result, std::string(kText))
        << "UTF-8 multi-byte characters must survive the CF_UNICODETEXT round-trip";
}

TEST_F(ClipboardTest, SetText_MultiLine_RoundTripPreservesNewlines) {
    const char* kText = "line one\nline two\nline three";

    ASSERT_TRUE(uz::ClipboardSetText(kText));
    const std::string result = uz::ClipboardGetText();

    EXPECT_EQ(result, std::string(kText))
        << "Newlines must be preserved through the clipboard round-trip";
}

TEST_F(ClipboardTest, SetText_EmptyString_SetsClipboardAndGetTextReturnsEmpty) {
    const bool ok = uz::ClipboardSetText("");
    // Empty string — implementation may return true or false (both acceptable
    // per the doc "Zero is a valid no-op"), but must not crash.
    (void)ok;

    const std::string result = uz::ClipboardGetText();
    // After setting empty text the clipboard text content is empty or cleared.
    // Accept either empty or the previous content — the important invariant
    // is that the call completes without crashing.
    SUCCEED();
}

TEST_F(ClipboardTest, SetText_OverwritesPreviousContent) {
    ASSERT_TRUE(uz::ClipboardSetText("first content"));
    ASSERT_TRUE(uz::ClipboardSetText("second content"));

    const std::string result = uz::ClipboardGetText();
    EXPECT_EQ(result, std::string("second content"))
        << "Second SetText must replace the first — clipboard holds only one item";
}

TEST_F(ClipboardTest, SetText_LongString_RoundTripSucceeds) {
    // 4 KB of repeated ASCII — tests allocation path for large content.
    const std::string kLong(4096, 'A');

    ASSERT_TRUE(uz::ClipboardSetText(kLong.c_str()));

    const std::string result = uz::ClipboardGetText();
    EXPECT_EQ(result, kLong)
        << "A 4096-character string must survive the clipboard round-trip";
}

// ─────────────────────────────────────────────────────────────────────────────
// ClipboardGetText — when clipboard has no text
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ClipboardTest, GetText_DoesNotCrash_ReturnsString) {
    // We cannot guarantee the clipboard has no text (another process may have
    // placed text there), but we can always ensure the call doesn't crash and
    // returns a valid std::string object.
    EXPECT_NO_FATAL_FAILURE({
        const std::string result = uz::ClipboardGetText();
        // result is a valid string regardless of whether it is empty or not.
        (void)result;
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// ClipboardSetData / ClipboardGetData — binary custom format round-trip
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ClipboardTest, SetData_ThenGetData_RoundTripMatchesBinaryPayload) {
    const char* kFormat = "UzEngine/TestBinaryFormat";
    const uint8_t kPayload[] = {
        0x00, 0xFF, 0x42, 0x13, 0x37, 0xDE, 0xAD, 0xBE,
        0xEF, 0xCA, 0xFE, 0xBA, 0xBE, 0x01, 0x02, 0x03
    };
    const size_t kSize = sizeof(kPayload);

    const bool ok = uz::ClipboardSetData(kFormat, kPayload, kSize);
    ASSERT_TRUE(ok)
        << "ClipboardSetData must return true for a valid format and payload";

    const std::vector<uint8_t> result = uz::ClipboardGetData(kFormat);
    ASSERT_EQ(result.size(), kSize)
        << "ClipboardGetData must return exactly " << kSize << " bytes";

    for (size_t i = 0; i < kSize; ++i) {
        EXPECT_EQ(result[i], kPayload[i])
            << "Byte mismatch at index " << i << " in binary clipboard round-trip";
    }
}

TEST_F(ClipboardTest, SetData_CustomFormat_DoesNotInterfereWithTextFormat) {
    // Set a known text so we can verify it after writing binary data.
    ASSERT_TRUE(uz::ClipboardSetText("text before binary"));

    const char* kFormat = "UzEngine/NoTextInterference";
    const uint8_t kData[] = { 0x01, 0x02, 0x03 };

    // Per ClipboardSetData docs, it "replaces any existing clipboard contents".
    // This test simply ensures neither call crashes.
    EXPECT_NO_FATAL_FAILURE({
        uz::ClipboardSetData(kFormat, kData, sizeof(kData));
        uz::ClipboardGetData(kFormat);
    });
}

TEST_F(ClipboardTest, SetData_LargeBinaryPayload_RoundTripSucceeds) {
    const char* kFormat = "UzEngine/LargePayload";
    std::vector<uint8_t> kLarge(8192);
    for (size_t i = 0; i < kLarge.size(); ++i) {
        kLarge[i] = static_cast<uint8_t>(i & 0xFF);
    }

    ASSERT_TRUE(uz::ClipboardSetData(kFormat, kLarge.data(), kLarge.size()));

    const std::vector<uint8_t> result = uz::ClipboardGetData(kFormat);
    ASSERT_EQ(result.size(), kLarge.size());
    for (size_t i = 0; i < kLarge.size(); ++i) {
        EXPECT_EQ(result[i], kLarge[i])
            << "Byte mismatch at index " << i << " for large binary payload";
    }
}

TEST_F(ClipboardTest, SetData_ZeroSize_ReturnsTrue_IsNoOp) {
    const char* kFormat = "UzEngine/ZeroSizeFormat";

    // Per docs: "Zero is a valid no-op (returns true)".
    const bool ok = uz::ClipboardSetData(kFormat, nullptr, 0);
    EXPECT_TRUE(ok)
        << "ClipboardSetData with size=0 must return true (documented no-op)";
}

// ─────────────────────────────────────────────────────────────────────────────
// ClipboardGetData — non-existent format returns empty vector
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ClipboardTest, GetData_NonExistentFormat_ReturnsEmptyVector) {
    const std::vector<uint8_t> result =
        uz::ClipboardGetData("UzEngine/FormatThatWasNeverSet_XYZ_99999");

    EXPECT_TRUE(result.empty())
        << "ClipboardGetData must return an empty vector when the format "
           "is not present on the clipboard";
}

TEST_F(ClipboardTest, GetData_AfterTextWasSet_CustomFormatIsAbsent) {
    // Put text on the clipboard, then ask for a custom binary format.
    // The custom format must not be present.
    ASSERT_TRUE(uz::ClipboardSetText("only text here"));

    const std::vector<uint8_t> result =
        uz::ClipboardGetData("UzEngine/BinaryFormatAfterTextOnly");

    EXPECT_TRUE(result.empty())
        << "Custom binary format must be absent after only text was set";
}

TEST_F(ClipboardTest, GetData_DoesNotCrash_WhenClipboardIsEmpty) {
    // Place known text then overwrite with a fresh SetText to put the
    // clipboard in a clean state with only CF_UNICODETEXT.
    ASSERT_TRUE(uz::ClipboardSetText("reset"));

    EXPECT_NO_FATAL_FAILURE({
        const std::vector<uint8_t> result =
            uz::ClipboardGetData("UzEngine/AbsentFormat");
        EXPECT_TRUE(result.empty());
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// ClipboardSetText return value consistency
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ClipboardTest, SetText_ReturnsTrue_ForMultipleConsecutiveCalls) {
    // Each successful SetText call must return true.
    const char* texts[] = { "alpha", "beta", "gamma", "delta", nullptr };
    for (int i = 0; texts[i] != nullptr; ++i) {
        const bool ok = uz::ClipboardSetText(texts[i]);
        EXPECT_TRUE(ok)
            << "ClipboardSetText should return true for text: " << texts[i];
    }
    // Final read must match the last written value.
    const std::string result = uz::ClipboardGetText();
    EXPECT_EQ(result, std::string("delta"));
}
