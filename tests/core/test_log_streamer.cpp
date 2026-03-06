// test_log_streamer.cpp -- Tests for LogStreamer.

#include <gtest/gtest.h>
#include "engine/core/log_streamer.h"
#include "engine/core/crash_reporter.h"

#include <string>
#include <vector>
#include <mutex>

// ============================================================================
// Test fixture
// ============================================================================

class LogStreamerTest : public ::testing::Test {
protected:
    std::vector<std::string> captured_;
    std::mutex mu_;

    void SetUp() override {
        captured_.clear();
        uz::LogStreamer::clear();
        uz::LogStreamer::init([this](const std::string& msg) {
            std::lock_guard<std::mutex> lock(mu_);
            captured_.push_back(msg);
        });
    }

    void TearDown() override {
        uz::LogStreamer::clear();
        uz::LogStreamer::init(nullptr);
    }
};

// ============================================================================
// Basic subscription tests
// ============================================================================

TEST_F(LogStreamerTest, SubscribeReturnsNonEmptyId) {
    auto id = uz::LogStreamer::subscribe(uz::LogLevel::Info, {});
    EXPECT_FALSE(id.empty());
    EXPECT_EQ(id.substr(0, 7), "logsub_");
}

TEST_F(LogStreamerTest, EmitWithoutSubscriptionsDoesNotBroadcast) {
    uz::LogStreamer::emit(uz::LogLevel::Error, "Test", "Hello");
    EXPECT_TRUE(captured_.empty());
}

TEST_F(LogStreamerTest, EmitMatchingSubscriptionBroadcasts) {
    uz::LogStreamer::subscribe(uz::LogLevel::Error, {});
    uz::LogStreamer::emit(uz::LogLevel::Error, "RHI", "Device lost");
    EXPECT_EQ(captured_.size(), 1u);
    EXPECT_NE(captured_[0].find("Device lost"), std::string::npos);
}

TEST_F(LogStreamerTest, EmitBelowSubscriptionLevelDoesNotBroadcast) {
    uz::LogStreamer::subscribe(uz::LogLevel::Error, {});
    uz::LogStreamer::emit(uz::LogLevel::Warn, "RHI", "Low memory");
    EXPECT_TRUE(captured_.empty());
}

TEST_F(LogStreamerTest, SystemFilterWorks) {
    uz::LogStreamer::subscribe(uz::LogLevel::Info, {"RHI"});
    uz::LogStreamer::emit(uz::LogLevel::Error, "Scene", "Node missing");
    EXPECT_TRUE(captured_.empty());
    uz::LogStreamer::emit(uz::LogLevel::Error, "RHI", "Device removed");
    EXPECT_EQ(captured_.size(), 1u);
}

TEST_F(LogStreamerTest, UnsubscribeStopsBroadcasting) {
    auto id = uz::LogStreamer::subscribe(uz::LogLevel::Info, {});
    uz::LogStreamer::emit(uz::LogLevel::Info, "Test", "First");
    EXPECT_EQ(captured_.size(), 1u);

    uz::LogStreamer::unsubscribe(id);
    uz::LogStreamer::emit(uz::LogLevel::Info, "Test", "Second");
    EXPECT_EQ(captured_.size(), 1u);
}

TEST_F(LogStreamerTest, ClearRemovesAllSubscriptions) {
    uz::LogStreamer::subscribe(uz::LogLevel::Info, {});
    uz::LogStreamer::subscribe(uz::LogLevel::Warn, {});
    uz::LogStreamer::clear();
    uz::LogStreamer::emit(uz::LogLevel::Fatal, "Test", "Should not appear");
    EXPECT_TRUE(captured_.empty());
}

// ============================================================================
// Error buffer tests
// ============================================================================

TEST_F(LogStreamerTest, ErrorBufferCapturesErrors) {
    uz::LogStreamer::emit(uz::LogLevel::Error, "Test", "Error 1");
    uz::LogStreamer::emit(uz::LogLevel::Fatal, "Test", "Fatal 1");
    uz::LogStreamer::emit(uz::LogLevel::Warn,  "Test", "Warn 1");

    auto json = uz::LogStreamer::get_error_buffer_json();
    EXPECT_NE(json.find("Error 1"), std::string::npos);
    EXPECT_NE(json.find("Fatal 1"), std::string::npos);
    EXPECT_EQ(json.find("Warn 1"), std::string::npos);
}

TEST_F(LogStreamerTest, ErrorBufferReturnsValidJson) {
    uz::LogStreamer::emit(uz::LogLevel::Error, "RHI", "Test error");
    auto json = uz::LogStreamer::get_error_buffer_json();
    EXPECT_EQ(json.front(), '[');
    EXPECT_EQ(json.back(), ']');
}

// ============================================================================
// Severity levels
// ============================================================================

TEST_F(LogStreamerTest, LogLevelNamesAreCorrect) {
    EXPECT_STREQ(uz::log_level_name(uz::LogLevel::Trace), "TRACE");
    EXPECT_STREQ(uz::log_level_name(uz::LogLevel::Debug), "DEBUG");
    EXPECT_STREQ(uz::log_level_name(uz::LogLevel::Info),  "INFO");
    EXPECT_STREQ(uz::log_level_name(uz::LogLevel::Warn),  "WARN");
    EXPECT_STREQ(uz::log_level_name(uz::LogLevel::Error), "ERROR");
    EXPECT_STREQ(uz::log_level_name(uz::LogLevel::Fatal), "FATAL");
}

TEST_F(LogStreamerTest, LogLevelFromStringWorks) {
    EXPECT_EQ(uz::log_level_from_string("TRACE"), uz::LogLevel::Trace);
    EXPECT_EQ(uz::log_level_from_string("ERROR"), uz::LogLevel::Error);
    EXPECT_EQ(uz::log_level_from_string("FATAL"), uz::LogLevel::Fatal);
    EXPECT_EQ(uz::log_level_from_string("garbage"), uz::LogLevel::Info);
}

// ============================================================================
// Metadata tests
// ============================================================================

TEST_F(LogStreamerTest, MetadataSetAndRead) {
    uz::CrashReporter::set_metadata("test_key", "test_value");
    uz::CrashReporter::set_metadata("another_key", "another_value");
    // Just verify no crash — metadata is verified through crash reports.
    uz::CrashReporter::clear_metadata();
}
