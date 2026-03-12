/// Unit tests for the ProfileCapture system.
///
/// Covers start/stop state transitions, scope event recording, `.cpuprofile`
/// JSON export format (Chrome DevTools Protocol), edge cases (double-start,
/// stop-without-start, buffer overflow), and no-op behaviour when not
/// capturing.
///
/// Temp files written during export tests are cleaned up in TearDown.
/// Tests are independent of Tracy — ProfileCapture is always available.

#include "profile_capture.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <json.hpp>
#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <vector>

using json = nlohmann::json;

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Returns a unique temp-file path for each call (within one test run).
/// Files are created in the system temp directory.
std::string MakeTempPath(const std::string& tag) {
    // Use a counter to avoid clashes between parallel tests.
    static std::atomic<int> counter{0};
    int id = counter.fetch_add(1, std::memory_order_relaxed);
    return std::string("test_capture_") + tag + "_" + std::to_string(id) + ".cpuprofile";
}

/// Reads an entire file into a string. Returns empty string on failure.
std::string ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

/// Ensures the singleton is not capturing before a test begins.
/// If a previous (failed) test left capture active, stop it into /dev/null.
void EnsureNotCapturing() {
    auto& cap = uz::ProfileCapture::Get();
    if (cap.IsCapturing()) {
        cap.StopAndExport("test_leftover_cleanup.cpuprofile");
        std::remove("test_leftover_cleanup.cpuprofile");
    }
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Fixture — manages temp file lifecycle for export tests
// ─────────────────────────────────────────────────────────────────────────────

/// Test fixture that tracks temp files created during a test and removes them
/// all in TearDown, even if the test fails mid-way.
class ProfileCaptureTest : public ::testing::Test {
protected:
    void SetUp() override {
        EnsureNotCapturing();
        m_tempFiles.clear();
    }

    void TearDown() override {
        // Best-effort stop if a test crashed before StopAndExport.
        EnsureNotCapturing();
        for (const auto& path : m_tempFiles) {
            std::remove(path.c_str());
        }
    }

    /// Registers a path for cleanup and returns it (for chaining).
    std::string Track(std::string path) {
        m_tempFiles.push_back(path);
        return path;
    }

private:
    std::vector<std::string> m_tempFiles;
};

// ─────────────────────────────────────────────────────────────────────────────
// State transition tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ProfileCaptureTest, IsCapturing_InitialState_IsFalse) {
    EXPECT_FALSE(uz::ProfileCapture::Get().IsCapturing());
}

TEST_F(ProfileCaptureTest, StartCapture_TransitionsToCapturing) {
    auto& cap = uz::ProfileCapture::Get();

    cap.StartCapture();
    EXPECT_TRUE(cap.IsCapturing());

    // Clean up — stop into a tracked temp file.
    const std::string path = Track(MakeTempPath("start_transitions"));
    cap.StopAndExport(path);
    EXPECT_FALSE(cap.IsCapturing());
}

TEST_F(ProfileCaptureTest, StopAndExport_AfterStart_TransitionsToNotCapturing) {
    auto& cap = uz::ProfileCapture::Get();
    const std::string path = Track(MakeTempPath("stop_transitions"));

    cap.StartCapture();
    ASSERT_TRUE(cap.IsCapturing());

    // Record at least one event so StopAndExport doesn't return false for empty buffer.
    { UZ_CAPTURE_SCOPE(StopTransitionWork); }

    bool ok = cap.StopAndExport(path);

    EXPECT_TRUE(ok);
    EXPECT_FALSE(cap.IsCapturing());
}

TEST_F(ProfileCaptureTest, StopAndExport_WithoutStart_ReturnsFalse) {
    auto& cap = uz::ProfileCapture::Get();
    ASSERT_FALSE(cap.IsCapturing());

    const std::string path = Track(MakeTempPath("stop_no_start"));
    bool ok = cap.StopAndExport(path);

    EXPECT_FALSE(ok);
    // The singleton must remain in not-capturing state.
    EXPECT_FALSE(cap.IsCapturing());
}

TEST_F(ProfileCaptureTest, DoubleStart_IsNoOp_NoStateCorruption) {
    auto& cap = uz::ProfileCapture::Get();

    cap.StartCapture();
    EXPECT_TRUE(cap.IsCapturing());

    // Second start must not crash and must leave capture active.
    cap.StartCapture();
    EXPECT_TRUE(cap.IsCapturing());

    // Record at least one event so StopAndExport doesn't return false for empty buffer.
    { UZ_CAPTURE_SCOPE(DoubleStartWork); }

    const std::string path = Track(MakeTempPath("double_start"));
    bool ok = cap.StopAndExport(path);

    EXPECT_TRUE(ok);
    EXPECT_FALSE(cap.IsCapturing());
}

// ─────────────────────────────────────────────────────────────────────────────
// No-op behaviour when not capturing
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ProfileCaptureTest, RecordEnterExit_WhenNotCapturing_DoNotCrash) {
    auto& cap = uz::ProfileCapture::Get();
    ASSERT_FALSE(cap.IsCapturing());

    // Direct API calls — must be no-ops, no crash.
    cap.RecordEnter("SomeName", __FILE__, __LINE__);
    cap.RecordExit("SomeName");

    SUCCEED();
}

TEST_F(ProfileCaptureTest, CaptureScopeMacro_WhenNotCapturing_DoNotCrash) {
    ASSERT_FALSE(uz::ProfileCapture::Get().IsCapturing());

    { UZ_CAPTURE_SCOPE(IdleScopeA); }
    { UZ_CAPTURE_FUNCTION(); }

    SUCCEED();
}

// ─────────────────────────────────────────────────────────────────────────────
// Scope event recording
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ProfileCaptureTest, ScopeEvents_Recorded_ExportedFileIsNonEmpty) {
    auto& cap = uz::ProfileCapture::Get();
    const std::string path = Track(MakeTempPath("nonempty"));

    cap.StartCapture();
    {
        UZ_CAPTURE_SCOPE(TestWork);
        volatile int x = 42;
        (void)x;
    }
    bool ok = cap.StopAndExport(path);

    ASSERT_TRUE(ok);

    const std::string content = ReadFile(path);
    EXPECT_FALSE(content.empty())
        << "Exported .cpuprofile must not be empty after recording scope events";
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON structure validation
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ProfileCaptureTest, Export_ProducesValidJson) {
    auto& cap = uz::ProfileCapture::Get();
    const std::string path = Track(MakeTempPath("valid_json"));

    cap.StartCapture();
    { UZ_CAPTURE_SCOPE(JsonStructureTest); }
    ASSERT_TRUE(cap.StopAndExport(path));

    const std::string content = ReadFile(path);
    ASSERT_FALSE(content.empty());

    // Must parse without throwing.
    ASSERT_NO_THROW({
        json j = json::parse(content);
        (void)j;
    }) << "Exported file is not valid JSON:\n"
       << content.substr(0, 512);
}

TEST_F(ProfileCaptureTest, Export_Json_HasRequiredTopLevelFields) {
    auto& cap = uz::ProfileCapture::Get();
    const std::string path = Track(MakeTempPath("toplevel_fields"));

    cap.StartCapture();
    { UZ_CAPTURE_SCOPE(TopLevelFieldTest); }
    ASSERT_TRUE(cap.StopAndExport(path));

    const json j = json::parse(ReadFile(path));

    EXPECT_TRUE(j.contains("nodes"))
        << "Missing required field 'nodes' in .cpuprofile";
    EXPECT_TRUE(j.contains("startTime"))
        << "Missing required field 'startTime' in .cpuprofile";
    EXPECT_TRUE(j.contains("endTime"))
        << "Missing required field 'endTime' in .cpuprofile";
    EXPECT_TRUE(j.contains("samples"))
        << "Missing required field 'samples' in .cpuprofile";
    EXPECT_TRUE(j.contains("timeDeltas"))
        << "Missing required field 'timeDeltas' in .cpuprofile";
}

TEST_F(ProfileCaptureTest, Export_Json_StartTimeLessThanEndTime) {
    auto& cap = uz::ProfileCapture::Get();
    const std::string path = Track(MakeTempPath("time_ordering"));

    cap.StartCapture();
    { UZ_CAPTURE_SCOPE(TimeOrderTest); }
    ASSERT_TRUE(cap.StopAndExport(path));

    const json j = json::parse(ReadFile(path));
    const auto startTime = j["startTime"].get<int64_t>();
    const auto endTime   = j["endTime"].get<int64_t>();

    EXPECT_LE(startTime, endTime)
        << "startTime (" << startTime << ") must be <= endTime (" << endTime << ")";
}

TEST_F(ProfileCaptureTest, Export_Json_NodesIsNonEmptyArray) {
    auto& cap = uz::ProfileCapture::Get();
    const std::string path = Track(MakeTempPath("nodes_nonempty"));

    cap.StartCapture();
    { UZ_CAPTURE_SCOPE(NodeArrayTest); }
    ASSERT_TRUE(cap.StopAndExport(path));

    const json j = json::parse(ReadFile(path));
    ASSERT_TRUE(j["nodes"].is_array());
    EXPECT_GT(j["nodes"].size(), 0u)
        << "'nodes' array must contain at least the root node";
}

TEST_F(ProfileCaptureTest, Export_Json_EachNodeHasRequiredFields) {
    auto& cap = uz::ProfileCapture::Get();
    const std::string path = Track(MakeTempPath("node_fields"));

    cap.StartCapture();
    { UZ_CAPTURE_SCOPE(NodeFieldTest); }
    ASSERT_TRUE(cap.StopAndExport(path));

    const json j = json::parse(ReadFile(path));
    const auto& nodes = j["nodes"];
    ASSERT_TRUE(nodes.is_array());
    ASSERT_GT(nodes.size(), 0u);

    for (size_t i = 0; i < nodes.size(); ++i) {
        const auto& node = nodes[i];
        EXPECT_TRUE(node.contains("id"))
            << "nodes[" << i << "] missing 'id'";
        ASSERT_TRUE(node.contains("callFrame"))
            << "nodes[" << i << "] missing 'callFrame'";

        const auto& cf = node["callFrame"];
        EXPECT_TRUE(cf.contains("functionName"))
            << "nodes[" << i << "].callFrame missing 'functionName'";
        EXPECT_TRUE(cf.contains("scriptId"))
            << "nodes[" << i << "].callFrame missing 'scriptId'";
        EXPECT_TRUE(cf.contains("url"))
            << "nodes[" << i << "].callFrame missing 'url'";
        EXPECT_TRUE(cf.contains("lineNumber"))
            << "nodes[" << i << "].callFrame missing 'lineNumber'";
        EXPECT_TRUE(cf.contains("columnNumber"))
            << "nodes[" << i << "].callFrame missing 'columnNumber'";
    }
}

TEST_F(ProfileCaptureTest, Export_Json_RootNodeIsFirstWithCorrectName) {
    auto& cap = uz::ProfileCapture::Get();
    const std::string path = Track(MakeTempPath("root_node"));

    cap.StartCapture();
    { UZ_CAPTURE_SCOPE(RootNodeTest); }
    ASSERT_TRUE(cap.StopAndExport(path));

    const json j = json::parse(ReadFile(path));
    const auto& nodes = j["nodes"];
    ASSERT_TRUE(nodes.is_array());
    ASSERT_GE(nodes.size(), 1u);

    const std::string rootName = nodes[0]["callFrame"]["functionName"].get<std::string>();
    EXPECT_EQ(rootName, "(root)")
        << "First node must have functionName == '(root)', got '" << rootName << "'";
}

TEST_F(ProfileCaptureTest, Export_Json_SamplesAndTimeDeltasSameLength) {
    auto& cap = uz::ProfileCapture::Get();
    const std::string path = Track(MakeTempPath("samples_length"));

    cap.StartCapture();
    {
        UZ_CAPTURE_SCOPE(SamplesTest);
        // Burn a small amount of time so at least one 10-µs sample fires.
        volatile uint32_t sum = 0;
        for (uint32_t i = 0; i < 1000; ++i) { sum += i; }
        (void)sum;
    }
    ASSERT_TRUE(cap.StopAndExport(path));

    const json j = json::parse(ReadFile(path));
    EXPECT_EQ(j["samples"].size(), j["timeDeltas"].size())
        << "'samples' and 'timeDeltas' arrays must have the same length";
}

// ─────────────────────────────────────────────────────────────────────────────
// Custom scope names appear in the exported tree
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ProfileCaptureTest, Export_Json_CustomScopeNamesAppearInNodes) {
    auto& cap = uz::ProfileCapture::Get();
    const std::string path = Track(MakeTempPath("custom_names"));

    cap.StartCapture();
    {
        UZ_CAPTURE_SCOPE(MyCustomScopeName);
        {
            UZ_CAPTURE_SCOPE(InnerCustomScope);
        }
    }
    ASSERT_TRUE(cap.StopAndExport(path));

    const json j   = json::parse(ReadFile(path));
    const auto& nodes = j["nodes"];

    bool foundOuter = false;
    bool foundInner = false;
    for (const auto& node : nodes) {
        const std::string fn = node["callFrame"]["functionName"].get<std::string>();
        if (fn == "MyCustomScopeName") foundOuter = true;
        if (fn == "InnerCustomScope")  foundInner = true;
    }

    EXPECT_TRUE(foundOuter)
        << "Expected node with functionName 'MyCustomScopeName' in exported JSON";
    EXPECT_TRUE(foundInner)
        << "Expected node with functionName 'InnerCustomScope' in exported JSON";
}

TEST_F(ProfileCaptureTest, Export_Json_CaptureFunctionNameAppearsInNodes) {
    auto& cap = uz::ProfileCapture::Get();
    const std::string path = Track(MakeTempPath("capture_function"));

    // A lambda wrapping UZ_CAPTURE_FUNCTION so __FUNCTION__ has a known prefix.
    auto work = [&] {
        UZ_CAPTURE_FUNCTION();
        volatile int x = 1; (void)x;
    };

    cap.StartCapture();
    work();
    ASSERT_TRUE(cap.StopAndExport(path));

    const json j = json::parse(ReadFile(path));
    const auto& nodes = j["nodes"];

    // We just verify at least one non-root node exists and has a non-empty name.
    bool foundNonRoot = false;
    for (const auto& node : nodes) {
        const std::string fn = node["callFrame"]["functionName"].get<std::string>();
        if (fn != "(root)") {
            foundNonRoot = true;
            EXPECT_FALSE(fn.empty())
                << "Non-root node must have a non-empty functionName";
            break;
        }
    }
    EXPECT_TRUE(foundNonRoot)
        << "Expected at least one non-root node from UZ_CAPTURE_FUNCTION()";
}

// ─────────────────────────────────────────────────────────────────────────────
// Buffer overflow — first-N strategy
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ProfileCaptureTest, BufferOverflow_RecordMoreThanCapacity_NocrashExportSucceeds) {
    // kMaxEvents = 1024*1024. We record pairs (enter+exit) in a tight loop.
    // Recording 600 000 pairs = 1.2M events exceeds the buffer; events past
    // the limit are silently dropped (first-N strategy).
    constexpr int kPairs = 600000;

    auto& cap = uz::ProfileCapture::Get();
    const std::string path = Track(MakeTempPath("overflow"));

    cap.StartCapture();
    for (int i = 0; i < kPairs; ++i) {
        cap.RecordEnter("OverflowScope", __FILE__, __LINE__);
        cap.RecordExit("OverflowScope");
    }
    bool ok = cap.StopAndExport(path);

    EXPECT_TRUE(ok) << "StopAndExport must succeed even after buffer overflow";
    EXPECT_FALSE(cap.IsCapturing());

    // The file must exist and be parseable.
    const std::string content = ReadFile(path);
    ASSERT_FALSE(content.empty()) << "Exported file must not be empty after overflow";
    ASSERT_NO_THROW({
        json j = json::parse(content);
        EXPECT_TRUE(j.contains("nodes"));
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// Repeated start/stop cycles
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ProfileCaptureTest, MultipleStartStopCycles_EachProducesValidExport) {
    auto& cap = uz::ProfileCapture::Get();

    for (int cycle = 0; cycle < 3; ++cycle) {
        const std::string path = Track(MakeTempPath("cycle_" + std::to_string(cycle)));

        cap.StartCapture();
        ASSERT_TRUE(cap.IsCapturing())
            << "Cycle " << cycle << ": IsCapturing must be true after StartCapture";

        { UZ_CAPTURE_SCOPE(CycleScope); }

        bool ok = cap.StopAndExport(path);
        EXPECT_TRUE(ok)            << "Cycle " << cycle << ": StopAndExport must return true";
        EXPECT_FALSE(cap.IsCapturing()) << "Cycle " << cycle << ": must not be capturing after stop";

        const std::string content = ReadFile(path);
        ASSERT_FALSE(content.empty()) << "Cycle " << cycle << ": exported file must not be empty";
        const json j = json::parse(content);
        EXPECT_TRUE(j.contains("nodes")) << "Cycle " << cycle << ": JSON missing 'nodes'";
    }
}
