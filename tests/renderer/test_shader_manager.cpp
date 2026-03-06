// test_shader_manager.cpp -- Google Test coverage for ShaderManager and
// ipc_shader_handlers.
//
// Covers:
//   - ShaderManager: register, list, get_info, reload, get_errors, callback
//   - Thread safety: concurrent reload from multiple threads
//   - IPC handlers: shader.reload, shader.getErrors, shader.list,
//                   shader.getSource, shader.setSource

#include <gtest/gtest.h>
#include "engine/renderer/resources/shader_manager.h"
#include "engine/core/platform.h"
#include "common/ipc_shader_handlers.h"
#include "common/ipc_registry.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#endif

using json = nlohmann::json;

// ============================================================================
// RAII helpers
// ============================================================================

namespace {

struct TempDir {
    std::string path;
    TempDir() {
#ifdef _WIN32
        char tmp[MAX_PATH];
        GetTempPathA(MAX_PATH, tmp);
        std::string tmpl = std::string(tmp) + "uz_sm_test_XXXXXX";
        if (_mktemp_s(tmpl.data(), tmpl.size() + 1) == 0) {
            if (CreateDirectoryA(tmpl.c_str(), nullptr)) {
                path = tmpl;
            }
        }
#else
        std::string tmpl = "/tmp/uz_sm_test_XXXXXX";
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        char* result = mkdtemp(buf.data());
        if (result) {
            path = result;
        }
#endif
    }
    ~TempDir() {
        if (!path.empty()) {
#ifdef _WIN32
            std::string cmd = "rmdir /s /q \"" + path + "\"";
#else
            std::string cmd = "rm -rf \"" + path + "\"";
#endif
            (void)system(cmd.c_str());
        }
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

void write_file(const std::string& path, const std::string& content) {
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
}

} // anonymous namespace

// ============================================================================
// ShaderManager unit tests
// ============================================================================

class ShaderManagerTest : public ::testing::Test {
protected:
    uz::ShaderManager mgr_;
};

TEST_F(ShaderManagerTest, Constructor_ListEmpty) {
    auto names = mgr_.list();
    EXPECT_TRUE(names.empty());
}

TEST_F(ShaderManagerTest, RegisterShader_AppearsInList) {
    mgr_.register_shader("basic", "shaders/basic.vs.hlsl", "shaders/basic.ps.hlsl");
    auto names = mgr_.list();
    ASSERT_EQ(names.size(), 1u);
    EXPECT_EQ(names[0], "basic");
}

TEST_F(ShaderManagerTest, RegisterMultiple_AllAppear) {
    mgr_.register_shader("basic", "basic.vs", "basic.ps");
    mgr_.register_shader("pbr", "pbr.vs", "pbr.ps");
    mgr_.register_shader("shadow", "shadow.vs", "shadow.ps");
    auto names = mgr_.list();
    ASSERT_EQ(names.size(), 3u);
    // list() is sorted
    EXPECT_EQ(names[0], "basic");
    EXPECT_EQ(names[1], "pbr");
    EXPECT_EQ(names[2], "shadow");
}

TEST_F(ShaderManagerTest, RegisterDuplicate_OverwritesPrevious) {
    mgr_.register_shader("basic", "old.vs", "old.ps");
    mgr_.register_shader("basic", "new.vs", "new.ps");
    auto names = mgr_.list();
    ASSERT_EQ(names.size(), 1u);

    const auto* info = mgr_.get_info("basic");
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->vs_path, "new.vs");
    EXPECT_EQ(info->ps_path, "new.ps");
}

TEST_F(ShaderManagerTest, GetInfo_Registered_ReturnsPaths) {
    mgr_.register_shader("test", "vs/test.hlsl", "ps/test.hlsl");
    const auto* info = mgr_.get_info("test");
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->name, "test");
    EXPECT_EQ(info->vs_path, "vs/test.hlsl");
    EXPECT_EQ(info->ps_path, "ps/test.hlsl");
    EXPECT_FALSE(info->is_valid);
    EXPECT_EQ(info->reload_count, 0u);
    EXPECT_TRUE(info->last_errors.empty());
}

TEST_F(ShaderManagerTest, GetInfo_Unknown_ReturnsNullptr) {
    EXPECT_EQ(mgr_.get_info("nonexistent"), nullptr);
}

TEST_F(ShaderManagerTest, Reload_MatchingVSPath_ReturnsTrue) {
    mgr_.register_shader("basic", "shaders/basic.vs.hlsl", "shaders/basic.ps.hlsl");
    bool result = mgr_.reload("shaders/basic.vs.hlsl");
    EXPECT_TRUE(result);
}

TEST_F(ShaderManagerTest, Reload_MatchingPSPath_ReturnsTrue) {
    mgr_.register_shader("basic", "shaders/basic.vs.hlsl", "shaders/basic.ps.hlsl");
    bool result = mgr_.reload("shaders/basic.ps.hlsl");
    EXPECT_TRUE(result);
}

TEST_F(ShaderManagerTest, Reload_NoMatch_ReturnsFalse) {
    mgr_.register_shader("basic", "basic.vs", "basic.ps");
    bool result = mgr_.reload("unknown/path.hlsl");
    EXPECT_FALSE(result);
}

TEST_F(ShaderManagerTest, Reload_IncrementsReloadCount) {
    mgr_.register_shader("basic", "basic.vs", "basic.ps");
    mgr_.reload("basic.vs");
    mgr_.reload("basic.vs");
    mgr_.reload("basic.ps");

    const auto* info = mgr_.get_info("basic");
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->reload_count, 3u);
}

TEST_F(ShaderManagerTest, Reload_SetsIsValid) {
    mgr_.register_shader("basic", "basic.vs", "basic.ps");
    const auto* info_before = mgr_.get_info("basic");
    ASSERT_NE(info_before, nullptr);
    EXPECT_FALSE(info_before->is_valid);

    mgr_.reload("basic.vs");

    const auto* info_after = mgr_.get_info("basic");
    ASSERT_NE(info_after, nullptr);
    EXPECT_TRUE(info_after->is_valid);
}

TEST_F(ShaderManagerTest, Reload_AffectsMultipleShadersSharingPath) {
    // Two shaders share the same VS path
    mgr_.register_shader("a", "shared.vs", "a.ps");
    mgr_.register_shader("b", "shared.vs", "b.ps");

    bool result = mgr_.reload("shared.vs");
    EXPECT_TRUE(result);

    const auto* info_a = mgr_.get_info("a");
    const auto* info_b = mgr_.get_info("b");
    ASSERT_NE(info_a, nullptr);
    ASSERT_NE(info_b, nullptr);
    EXPECT_EQ(info_a->reload_count, 1u);
    EXPECT_EQ(info_b->reload_count, 1u);
}

TEST_F(ShaderManagerTest, GetErrors_AfterReload_ReturnsEmpty) {
    mgr_.register_shader("basic", "basic.vs", "basic.ps");
    mgr_.reload("basic.vs");
    auto errors = mgr_.get_errors("basic.vs");
    EXPECT_TRUE(errors.empty());
}

TEST_F(ShaderManagerTest, GetErrors_UnknownPath_ReturnsEmpty) {
    auto errors = mgr_.get_errors("no/such/path.hlsl");
    EXPECT_TRUE(errors.empty());
}

TEST_F(ShaderManagerTest, Callback_InvokedOnReload) {
    mgr_.register_shader("basic", "basic.vs", "basic.ps");

    std::string callback_path;
    bool callback_success = false;
    int callback_count = 0;

    mgr_.set_reload_callback([&](const std::string& path, bool success,
                                  const std::vector<uz::ShaderError>& /*errors*/) {
        callback_path = path;
        callback_success = success;
        callback_count++;
    });

    mgr_.reload("basic.vs");

    EXPECT_EQ(callback_count, 1);
    EXPECT_EQ(callback_path, "basic.vs");
    EXPECT_TRUE(callback_success);
}

TEST_F(ShaderManagerTest, Callback_NotInvokedWhenNoMatch) {
    mgr_.register_shader("basic", "basic.vs", "basic.ps");

    int callback_count = 0;
    mgr_.set_reload_callback([&](const std::string&, bool,
                                  const std::vector<uz::ShaderError>&) {
        callback_count++;
    });

    mgr_.reload("unknown.hlsl");
    EXPECT_EQ(callback_count, 0);
}

TEST_F(ShaderManagerTest, Callback_CalledPerAffectedShader) {
    mgr_.register_shader("a", "shared.vs", "a.ps");
    mgr_.register_shader("b", "shared.vs", "b.ps");

    int callback_count = 0;
    mgr_.set_reload_callback([&](const std::string&, bool,
                                  const std::vector<uz::ShaderError>&) {
        callback_count++;
    });

    mgr_.reload("shared.vs");
    EXPECT_EQ(callback_count, 2);
}

TEST_F(ShaderManagerTest, SetReloadCallback_ReplacesOld) {
    mgr_.register_shader("basic", "basic.vs", "basic.ps");

    int count1 = 0;
    int count2 = 0;

    mgr_.set_reload_callback([&](const std::string&, bool,
                                  const std::vector<uz::ShaderError>&) { count1++; });
    mgr_.reload("basic.vs");

    mgr_.set_reload_callback([&](const std::string&, bool,
                                  const std::vector<uz::ShaderError>&) { count2++; });
    mgr_.reload("basic.vs");

    EXPECT_EQ(count1, 1);
    EXPECT_EQ(count2, 1);
}

// -- Thread safety --

TEST_F(ShaderManagerTest, ConcurrentReload_NoDataRace) {
    mgr_.register_shader("s1", "s1.vs", "s1.ps");
    mgr_.register_shader("s2", "s2.vs", "s2.ps");

    std::atomic<int> total_count{0};
    mgr_.set_reload_callback([&](const std::string&, bool,
                                  const std::vector<uz::ShaderError>&) {
        total_count.fetch_add(1, std::memory_order_relaxed);
    });

    constexpr int kThreads = 4;
    constexpr int kIterations = 50;
    std::vector<std::thread> threads;

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([this, t]() {
            for (int i = 0; i < kIterations; ++i) {
                if (t % 2 == 0) {
                    mgr_.reload("s1.vs");
                } else {
                    mgr_.reload("s2.ps");
                }
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    // Each thread does kIterations reloads, each hitting exactly 1 shader
    EXPECT_EQ(total_count.load(), kThreads * kIterations);

    // Verify reload counts are consistent
    const auto* info1 = mgr_.get_info("s1");
    const auto* info2 = mgr_.get_info("s2");
    ASSERT_NE(info1, nullptr);
    ASSERT_NE(info2, nullptr);
    EXPECT_EQ(info1->reload_count + info2->reload_count,
              static_cast<uint64_t>(kThreads * kIterations));
}

// -- ShaderError struct --

TEST(ShaderErrorTest, DefaultValues) {
    uz::ShaderError err;
    EXPECT_TRUE(err.file.empty());
    EXPECT_EQ(err.line, 0);
    EXPECT_EQ(err.column, 0);
    EXPECT_TRUE(err.message.empty());
}

TEST(ShaderErrorTest, AssignValues) {
    uz::ShaderError err;
    err.file    = "test.hlsl";
    err.line    = 42;
    err.column  = 7;
    err.message = "syntax error";
    EXPECT_EQ(err.file, "test.hlsl");
    EXPECT_EQ(err.line, 42);
    EXPECT_EQ(err.column, 7);
    EXPECT_EQ(err.message, "syntax error");
}

// -- ShaderInfo struct --

TEST(ShaderInfoTest, DefaultValues) {
    uz::ShaderInfo info;
    EXPECT_TRUE(info.name.empty());
    EXPECT_TRUE(info.vs_path.empty());
    EXPECT_TRUE(info.ps_path.empty());
    EXPECT_TRUE(info.last_errors.empty());
    EXPECT_FALSE(info.is_valid);
    EXPECT_EQ(info.reload_count, 0u);
}

// ============================================================================
// IPC shader handlers tests
// ============================================================================

class ShaderHandlersTest : public ::testing::Test {
protected:
    void SetUp() override {
        mgr_      = std::make_unique<uz::ShaderManager>();
        registry_ = std::make_unique<uz::IpcRegistry>();
        uz::shader_register_handlers(*registry_, mgr_.get());
    }

    json call(const std::string& name, const json& args = json::object()) {
        return registry_->call(name, args);
    }

    std::unique_ptr<uz::ShaderManager> mgr_;
    std::unique_ptr<uz::IpcRegistry>   registry_;
};

// -- shader.list --

TEST_F(ShaderHandlersTest, List_Empty_ReturnsEmptyArray) {
    auto result = call("shader.list");
    EXPECT_TRUE(result["ok"].get<bool>());
    EXPECT_TRUE(result["shaders"].is_array());
    EXPECT_EQ(result["shaders"].size(), 0u);
}

TEST_F(ShaderHandlersTest, List_WithShaders_ReturnsSorted) {
    mgr_->register_shader("pbr", "pbr.vs", "pbr.ps");
    mgr_->register_shader("basic", "basic.vs", "basic.ps");

    auto result = call("shader.list");
    EXPECT_TRUE(result["ok"].get<bool>());
    ASSERT_EQ(result["shaders"].size(), 2u);
    EXPECT_EQ(result["shaders"][0]["name"], "basic");
    EXPECT_EQ(result["shaders"][1]["name"], "pbr");
    EXPECT_EQ(result["shaders"][0]["vs_path"], "basic.vs");
    EXPECT_EQ(result["shaders"][0]["ps_path"], "basic.ps");
    EXPECT_FALSE(result["shaders"][0]["is_valid"].get<bool>());
}

// -- shader.reload --

TEST_F(ShaderHandlersTest, Reload_MissingPath_Error) {
    auto result = call("shader.reload");
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_TRUE(result["error"].is_string());
}

TEST_F(ShaderHandlersTest, Reload_InvalidPathType_Error) {
    auto result = call("shader.reload", {{"path", 123}});
    EXPECT_FALSE(result["ok"].get<bool>());
}

TEST_F(ShaderHandlersTest, Reload_NoMatchingShader_NotAffected) {
    mgr_->register_shader("basic", "basic.vs", "basic.ps");
    auto result = call("shader.reload", {{"path", "unknown.hlsl"}});
    EXPECT_TRUE(result["ok"].get<bool>());
    EXPECT_FALSE(result["affected"].get<bool>());
    EXPECT_TRUE(result["errors"].is_array());
    EXPECT_EQ(result["errors"].size(), 0u);
    EXPECT_TRUE(result.contains("elapsed_ms"));
}

TEST_F(ShaderHandlersTest, Reload_MatchingShader_Affected) {
    mgr_->register_shader("basic", "basic.vs", "basic.ps");
    auto result = call("shader.reload", {{"path", "basic.vs"}});
    EXPECT_TRUE(result["ok"].get<bool>());
    EXPECT_TRUE(result["affected"].get<bool>());
    EXPECT_TRUE(result["errors"].is_array());
    EXPECT_EQ(result["errors"].size(), 0u);
    EXPECT_GE(result["elapsed_ms"].get<double>(), 0.0);
}

// -- shader.getErrors --

TEST_F(ShaderHandlersTest, GetErrors_MissingPath_Error) {
    auto result = call("shader.getErrors");
    EXPECT_FALSE(result["ok"].get<bool>());
}

TEST_F(ShaderHandlersTest, GetErrors_ValidPath_EmptyErrors) {
    mgr_->register_shader("basic", "basic.vs", "basic.ps");
    mgr_->reload("basic.vs");
    auto result = call("shader.getErrors", {{"path", "basic.vs"}});
    EXPECT_TRUE(result["ok"].get<bool>());
    EXPECT_TRUE(result["errors"].is_array());
    EXPECT_EQ(result["errors"].size(), 0u);
}

TEST_F(ShaderHandlersTest, GetErrors_UnknownPath_EmptyErrors) {
    auto result = call("shader.getErrors", {{"path", "no/such.hlsl"}});
    EXPECT_TRUE(result["ok"].get<bool>());
    EXPECT_TRUE(result["errors"].is_array());
    EXPECT_EQ(result["errors"].size(), 0u);
}

// -- shader.getSource --

TEST_F(ShaderHandlersTest, GetSource_MissingPath_Error) {
    auto result = call("shader.getSource");
    EXPECT_FALSE(result["ok"].get<bool>());
}

TEST_F(ShaderHandlersTest, GetSource_NonexistentFile_Error) {
    auto result = call("shader.getSource", {{"path", "/tmp/no_such_file_12345.hlsl"}});
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_TRUE(result["error"].is_string());
}

TEST_F(ShaderHandlersTest, GetSource_ExistingFile_ReturnsContent) {
    TempDir tmp;
    ASSERT_FALSE(tmp.path.empty());
    std::string file_path = tmp.path + "/test.hlsl";
    write_file(file_path, "float4 main() : SV_TARGET { return 1; }");

    auto result = call("shader.getSource", {{"path", file_path}});
    EXPECT_TRUE(result["ok"].get<bool>());
    EXPECT_EQ(result["source"].get<std::string>(), "float4 main() : SV_TARGET { return 1; }");
}

// -- shader.setSource --

TEST_F(ShaderHandlersTest, SetSource_MissingPath_Error) {
    auto result = call("shader.setSource", {{"source", "code"}});
    EXPECT_FALSE(result["ok"].get<bool>());
}

TEST_F(ShaderHandlersTest, SetSource_MissingSource_Error) {
    auto result = call("shader.setSource", {{"path", "/tmp/test.hlsl"}});
    EXPECT_FALSE(result["ok"].get<bool>());
}

TEST_F(ShaderHandlersTest, SetSource_InvalidPathType_Error) {
    auto result = call("shader.setSource", {{"path", 42}, {"source", "code"}});
    EXPECT_FALSE(result["ok"].get<bool>());
}

TEST_F(ShaderHandlersTest, SetSource_InvalidSourceType_Error) {
    auto result = call("shader.setSource", {{"path", "/tmp/x.hlsl"}, {"source", 42}});
    EXPECT_FALSE(result["ok"].get<bool>());
}

TEST_F(ShaderHandlersTest, SetSource_WritesFile) {
    TempDir tmp;
    ASSERT_FALSE(tmp.path.empty());
    std::string file_path = tmp.path + "/write_test.hlsl";

    auto result = call("shader.setSource", {{"path", file_path}, {"source", "new shader code"}});
    EXPECT_TRUE(result["ok"].get<bool>());

    // Verify the file was written
    size_t size = 0;
    char* data = uz::platform::file_read_all(file_path.c_str(), &size);
    ASSERT_NE(data, nullptr);
    std::string content(data, size);
    free(data);
    EXPECT_EQ(content, "new shader code");
}

TEST_F(ShaderHandlersTest, SetSource_InvalidDirectory_Error) {
    auto result = call("shader.setSource",
                       {{"path", "/no/such/directory/file.hlsl"}, {"source", "code"}});
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_TRUE(result["error"].is_string());
}

// -- Null manager --

TEST(ShaderHandlersNullTest, NullManager_ReloadError) {
    uz::IpcRegistry registry;
    uz::shader_register_handlers(registry, nullptr);

    auto result = registry.call("shader.reload", {{"path", "test.hlsl"}});
    EXPECT_FALSE(result["ok"].get<bool>());
}

TEST(ShaderHandlersNullTest, NullManager_ListError) {
    uz::IpcRegistry registry;
    uz::shader_register_handlers(registry, nullptr);

    auto result = registry.call("shader.list", json::object());
    EXPECT_FALSE(result["ok"].get<bool>());
}

TEST(ShaderHandlersNullTest, NullManager_GetErrorsError) {
    uz::IpcRegistry registry;
    uz::shader_register_handlers(registry, nullptr);

    auto result = registry.call("shader.getErrors", {{"path", "test.hlsl"}});
    EXPECT_FALSE(result["ok"].get<bool>());
}

// -- All handlers registered --

TEST_F(ShaderHandlersTest, AllHandlersRegistered) {
    EXPECT_TRUE(registry_->has("shader.reload"));
    EXPECT_TRUE(registry_->has("shader.getErrors"));
    EXPECT_TRUE(registry_->has("shader.list"));
    EXPECT_TRUE(registry_->has("shader.getSource"));
    EXPECT_TRUE(registry_->has("shader.setSource"));
}
