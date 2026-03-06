// test_engine_mode.cpp -- Google Test coverage for engine_mode.h and
// ipc_playmode_handlers.h/.cpp.
//
// Targets >=95% line + branch coverage for:
//   - parse_engine_mode()        — mode flag parsing
//   - parse_launch_args()        — full CLI argument parsing
//   - playmode_register_handlers — all 6 IPC handlers
//
// Each test constructs fresh state so tests are independent.

#include <gtest/gtest.h>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/core/engine_mode.h"
#include "engine/editor/ipc_playmode_handlers.h"
#include "common/ipc_registry.h"

using namespace uz;
using json = nlohmann::json;

// ============================================================================
// Helpers
// ============================================================================

/// Helper to build an argv array from a list of strings.
/// The first element is always the program name.
class ArgvBuilder {
public:
    ArgvBuilder() {
        add("render.exe");
    }

    void add(const char* arg) {
        storage_.emplace_back(arg);
    }

    int argc() const {
        return static_cast<int>(storage_.size());
    }

    /// Returns a char** suitable for passing to parse functions.
    /// The returned pointer is valid as long as this ArgvBuilder is alive.
    char** argv() {
        ptrs_.clear();
        for (auto& s : storage_) {
            ptrs_.push_back(s.data());
        }
        return ptrs_.data();
    }

private:
    std::vector<std::string> storage_;
    std::vector<char*>       ptrs_;
};

// ============================================================================
// parse_engine_mode tests
// ============================================================================

TEST(ParseEngineMode, NoArgs_DefaultsToEditor) {
    ArgvBuilder args;
    EXPECT_EQ(parse_engine_mode(args.argc(), args.argv()), EngineMode::Editor);
}

TEST(ParseEngineMode, EditorFlag_ReturnsEditor) {
    ArgvBuilder args;
    args.add("--editor");
    EXPECT_EQ(parse_engine_mode(args.argc(), args.argv()), EngineMode::Editor);
}

TEST(ParseEngineMode, PlayFlag_ReturnsPlay) {
    ArgvBuilder args;
    args.add("--play");
    EXPECT_EQ(parse_engine_mode(args.argc(), args.argv()), EngineMode::Play);
}

TEST(ParseEngineMode, BothFlags_LastOneWins_PlayLast) {
    ArgvBuilder args;
    args.add("--editor");
    args.add("--play");
    EXPECT_EQ(parse_engine_mode(args.argc(), args.argv()), EngineMode::Play);
}

TEST(ParseEngineMode, BothFlags_LastOneWins_EditorLast) {
    ArgvBuilder args;
    args.add("--play");
    args.add("--editor");
    EXPECT_EQ(parse_engine_mode(args.argc(), args.argv()), EngineMode::Editor);
}

TEST(ParseEngineMode, UnknownFlags_Ignored) {
    ArgvBuilder args;
    args.add("--unknown");
    args.add("--foo=bar");
    EXPECT_EQ(parse_engine_mode(args.argc(), args.argv()), EngineMode::Editor);
}

// ============================================================================
// parse_launch_args tests
// ============================================================================

TEST(ParseLaunchArgs, NoArgs_DefaultsToEditorPort9002) {
    ArgvBuilder args;
    auto result = parse_launch_args(args.argc(), args.argv());
    EXPECT_EQ(result.mode, EngineMode::Editor);
    EXPECT_EQ(result.port, 9002);
    EXPECT_TRUE(result.project_path.empty());
    EXPECT_TRUE(result.scene_path.empty());
}

TEST(ParseLaunchArgs, EditorFlag_ModeEditorPort9002) {
    ArgvBuilder args;
    args.add("--editor");
    auto result = parse_launch_args(args.argc(), args.argv());
    EXPECT_EQ(result.mode, EngineMode::Editor);
    EXPECT_EQ(result.port, 9002);
}

TEST(ParseLaunchArgs, PlayFlag_ModePlayPort9003) {
    ArgvBuilder args;
    args.add("--play");
    auto result = parse_launch_args(args.argc(), args.argv());
    EXPECT_EQ(result.mode, EngineMode::Play);
    EXPECT_EQ(result.port, 9003);
}

TEST(ParseLaunchArgs, PlayWithPortOverride) {
    ArgvBuilder args;
    args.add("--play");
    args.add("--port=9004");
    auto result = parse_launch_args(args.argc(), args.argv());
    EXPECT_EQ(result.mode, EngineMode::Play);
    EXPECT_EQ(result.port, 9004);
}

TEST(ParseLaunchArgs, EditorWithPortOverride) {
    ArgvBuilder args;
    args.add("--editor");
    args.add("--port=8080");
    auto result = parse_launch_args(args.argc(), args.argv());
    EXPECT_EQ(result.mode, EngineMode::Editor);
    EXPECT_EQ(result.port, 8080);
}

TEST(ParseLaunchArgs, ProjectPath) {
    ArgvBuilder args;
    args.add("--project=/foo/bar");
    auto result = parse_launch_args(args.argc(), args.argv());
    EXPECT_EQ(result.project_path, "/foo/bar");
}

TEST(ParseLaunchArgs, ScenePath) {
    ArgvBuilder args;
    args.add("--scene=bar.uzscene");
    auto result = parse_launch_args(args.argc(), args.argv());
    EXPECT_EQ(result.scene_path, "bar.uzscene");
}

TEST(ParseLaunchArgs, ProjectAndScene) {
    ArgvBuilder args;
    args.add("--project=/foo");
    args.add("--scene=bar.uzscene");
    auto result = parse_launch_args(args.argc(), args.argv());
    EXPECT_EQ(result.project_path, "/foo");
    EXPECT_EQ(result.scene_path, "bar.uzscene");
}

TEST(ParseLaunchArgs, AllArgsAtOnce) {
    ArgvBuilder args;
    args.add("--play");
    args.add("--project=/my/project");
    args.add("--scene=level1.uzscene");
    args.add("--port=9999");
    auto result = parse_launch_args(args.argc(), args.argv());
    EXPECT_EQ(result.mode, EngineMode::Play);
    EXPECT_EQ(result.project_path, "/my/project");
    EXPECT_EQ(result.scene_path, "level1.uzscene");
    EXPECT_EQ(result.port, 9999);
}

TEST(ParseLaunchArgs, UnknownArgsIgnored) {
    ArgvBuilder args;
    args.add("--verbose");
    args.add("--debug=true");
    auto result = parse_launch_args(args.argc(), args.argv());
    EXPECT_EQ(result.mode, EngineMode::Editor);
    EXPECT_EQ(result.port, 9002);
}

TEST(ParseLaunchArgs, EmptyProjectPath) {
    ArgvBuilder args;
    args.add("--project=");
    auto result = parse_launch_args(args.argc(), args.argv());
    EXPECT_EQ(result.project_path, "");
}

TEST(ParseLaunchArgs, EmptyScenePath) {
    ArgvBuilder args;
    args.add("--scene=");
    auto result = parse_launch_args(args.argc(), args.argv());
    EXPECT_EQ(result.scene_path, "");
}

TEST(ParseLaunchArgs, PortZero) {
    ArgvBuilder args;
    args.add("--port=0");
    auto result = parse_launch_args(args.argc(), args.argv());
    EXPECT_EQ(result.port, 0);
}

TEST(ParseLaunchArgs, BothModeFlags_LastWins) {
    ArgvBuilder args;
    args.add("--editor");
    args.add("--play");
    auto result = parse_launch_args(args.argc(), args.argv());
    EXPECT_EQ(result.mode, EngineMode::Play);
    EXPECT_EQ(result.port, 9003);  // play default since port not explicit
}

TEST(ParseLaunchArgs, BothModeFlags_EditorLast_NoExplicitPort) {
    ArgvBuilder args;
    args.add("--play");
    args.add("--editor");
    auto result = parse_launch_args(args.argc(), args.argv());
    EXPECT_EQ(result.mode, EngineMode::Editor);
    EXPECT_EQ(result.port, 9002);  // editor default since port not explicit
}

// ============================================================================
// PlayMode IPC handler test fixture
// ============================================================================

class PlayModeHandlersTest : public ::testing::Test {
protected:
    void SetUp() override {
        state_ = std::make_unique<PlayModeState>();
        registry_ = std::make_unique<IpcRegistry>();
    }

    /// Register handlers for the given mode with an optional stop callback.
    void registerHandlers(EngineMode mode, StopCallback on_stop = nullptr) {
        playmode_register_handlers(*registry_, mode, state_.get(),
                                    std::move(on_stop));
    }

    json call(const std::string& name, const json& args = json::object()) {
        return registry_->call(name, args);
    }

    std::unique_ptr<PlayModeState> state_;
    std::unique_ptr<IpcRegistry>   registry_;
};

// ============================================================================
// Handler registration
// ============================================================================

TEST_F(PlayModeHandlersTest, AllHandlersRegistered_EditorMode) {
    registerHandlers(EngineMode::Editor);
    EXPECT_TRUE(registry_->has("playmode.status"));
    EXPECT_TRUE(registry_->has("playmode.start"));
    EXPECT_TRUE(registry_->has("playmode.stop"));
    EXPECT_TRUE(registry_->has("engine.pause"));
    EXPECT_TRUE(registry_->has("engine.resume"));
    EXPECT_TRUE(registry_->has("engine.setPaused"));
}

TEST_F(PlayModeHandlersTest, AllHandlersRegistered_PlayMode) {
    registerHandlers(EngineMode::Play);
    EXPECT_TRUE(registry_->has("playmode.status"));
    EXPECT_TRUE(registry_->has("playmode.start"));
    EXPECT_TRUE(registry_->has("playmode.stop"));
    EXPECT_TRUE(registry_->has("engine.pause"));
    EXPECT_TRUE(registry_->has("engine.resume"));
    EXPECT_TRUE(registry_->has("engine.setPaused"));
}

// ============================================================================
// playmode.status
// ============================================================================

TEST_F(PlayModeHandlersTest, Status_EditorMode_ReturnsEditorString) {
    registerHandlers(EngineMode::Editor);
    auto result = call("playmode.status");
    EXPECT_EQ(result["ok"], true);
    EXPECT_EQ(result["mode"], "editor");
    EXPECT_EQ(result["paused"], false);
    EXPECT_EQ(result["scene"], "");
}

TEST_F(PlayModeHandlersTest, Status_PlayMode_ReturnsPlayString) {
    state_->scene_path = "level1.uzscene";
    registerHandlers(EngineMode::Play);
    auto result = call("playmode.status");
    EXPECT_EQ(result["ok"], true);
    EXPECT_EQ(result["mode"], "play");
    EXPECT_EQ(result["scene"], "level1.uzscene");
    EXPECT_EQ(result["paused"], false);
}

TEST_F(PlayModeHandlersTest, Status_ReflectsPausedState) {
    registerHandlers(EngineMode::Play);
    state_->paused.store(true);
    auto result = call("playmode.status");
    EXPECT_EQ(result["paused"], true);
}

TEST_F(PlayModeHandlersTest, Status_NullState_Error) {
    IpcRegistry reg;
    playmode_register_handlers(reg, EngineMode::Editor, nullptr);
    auto result = reg.call("playmode.status", json::object());
    EXPECT_EQ(result["ok"], false);
    EXPECT_EQ(result["error"], "no state");
}

// ============================================================================
// playmode.start
// ============================================================================

TEST_F(PlayModeHandlersTest, Start_EditorMode_ReturnsPort9003) {
    registerHandlers(EngineMode::Editor);
    auto result = call("playmode.start");
    EXPECT_EQ(result["ok"], true);
    EXPECT_EQ(result["port"], 9003);
}

TEST_F(PlayModeHandlersTest, Start_PlayMode_ReturnsError) {
    registerHandlers(EngineMode::Play);
    auto result = call("playmode.start");
    EXPECT_EQ(result["ok"], false);
    EXPECT_EQ(result["error"], "already in play mode");
}

// ============================================================================
// playmode.stop
// ============================================================================

TEST_F(PlayModeHandlersTest, Stop_EditorMode_ReturnsError) {
    registerHandlers(EngineMode::Editor);
    auto result = call("playmode.stop");
    EXPECT_EQ(result["ok"], false);
    EXPECT_EQ(result["error"], "not in play mode");
}

TEST_F(PlayModeHandlersTest, Stop_PlayMode_CallsStopCallback) {
    bool stop_called = false;
    registerHandlers(EngineMode::Play, [&stop_called]() {
        stop_called = true;
    });
    auto result = call("playmode.stop");
    EXPECT_EQ(result["ok"], true);
    EXPECT_TRUE(stop_called);
}

TEST_F(PlayModeHandlersTest, Stop_PlayMode_NoCallback_DefaultBehavior) {
    // We cannot test exit(0) directly, but we test that registering with
    // nullptr on_stop does not crash when the handler is registered.
    // The actual exit(0) call would terminate the test process.
    // We only verify the handler is registered and callable in editor mode.
    registerHandlers(EngineMode::Editor, nullptr);
    // In editor mode, stop returns an error (does not call exit).
    auto result = call("playmode.stop");
    EXPECT_EQ(result["ok"], false);
}

// ============================================================================
// engine.pause
// ============================================================================

TEST_F(PlayModeHandlersTest, Pause_SetsPausedTrue) {
    registerHandlers(EngineMode::Play);
    EXPECT_FALSE(state_->paused.load());

    auto result = call("engine.pause");
    EXPECT_EQ(result["ok"], true);
    EXPECT_TRUE(state_->paused.load());
}

TEST_F(PlayModeHandlersTest, Pause_AlreadyPaused_StillOk) {
    registerHandlers(EngineMode::Play);
    state_->paused.store(true);

    auto result = call("engine.pause");
    EXPECT_EQ(result["ok"], true);
    EXPECT_TRUE(state_->paused.load());
}

TEST_F(PlayModeHandlersTest, Pause_NullState_Error) {
    IpcRegistry reg;
    playmode_register_handlers(reg, EngineMode::Play, nullptr);
    auto result = reg.call("engine.pause", json::object());
    EXPECT_EQ(result["ok"], false);
    EXPECT_EQ(result["error"], "no state");
}

// ============================================================================
// engine.resume
// ============================================================================

TEST_F(PlayModeHandlersTest, Resume_SetsPausedFalse) {
    registerHandlers(EngineMode::Play);
    state_->paused.store(true);

    auto result = call("engine.resume");
    EXPECT_EQ(result["ok"], true);
    EXPECT_FALSE(state_->paused.load());
}

TEST_F(PlayModeHandlersTest, Resume_AlreadyResumed_StillOk) {
    registerHandlers(EngineMode::Play);
    EXPECT_FALSE(state_->paused.load());

    auto result = call("engine.resume");
    EXPECT_EQ(result["ok"], true);
    EXPECT_FALSE(state_->paused.load());
}

TEST_F(PlayModeHandlersTest, Resume_NullState_Error) {
    IpcRegistry reg;
    playmode_register_handlers(reg, EngineMode::Play, nullptr);
    auto result = reg.call("engine.resume", json::object());
    EXPECT_EQ(result["ok"], false);
    EXPECT_EQ(result["error"], "no state");
}

// ============================================================================
// engine.setPaused
// ============================================================================

TEST_F(PlayModeHandlersTest, SetPaused_True) {
    registerHandlers(EngineMode::Play);
    auto result = call("engine.setPaused", {{"paused", true}});
    EXPECT_EQ(result["ok"], true);
    EXPECT_TRUE(state_->paused.load());
}

TEST_F(PlayModeHandlersTest, SetPaused_False) {
    registerHandlers(EngineMode::Play);
    state_->paused.store(true);

    auto result = call("engine.setPaused", {{"paused", false}});
    EXPECT_EQ(result["ok"], true);
    EXPECT_FALSE(state_->paused.load());
}

TEST_F(PlayModeHandlersTest, SetPaused_MissingArg_Error) {
    registerHandlers(EngineMode::Play);
    auto result = call("engine.setPaused", json::object());
    EXPECT_EQ(result["ok"], false);
    EXPECT_TRUE(result["error"].get<std::string>().find("paused") != std::string::npos);
}

TEST_F(PlayModeHandlersTest, SetPaused_WrongType_Error) {
    registerHandlers(EngineMode::Play);
    auto result = call("engine.setPaused", {{"paused", 1}});
    EXPECT_EQ(result["ok"], false);
    EXPECT_TRUE(result["error"].get<std::string>().find("boolean") != std::string::npos);
}

TEST_F(PlayModeHandlersTest, SetPaused_StringArg_Error) {
    registerHandlers(EngineMode::Play);
    auto result = call("engine.setPaused", {{"paused", "yes"}});
    EXPECT_EQ(result["ok"], false);
    EXPECT_TRUE(result["error"].get<std::string>().find("boolean") != std::string::npos);
}

TEST_F(PlayModeHandlersTest, SetPaused_NullState_Error) {
    IpcRegistry reg;
    playmode_register_handlers(reg, EngineMode::Play, nullptr);
    auto result = reg.call("engine.setPaused", {{"paused", true}});
    EXPECT_EQ(result["ok"], false);
    EXPECT_EQ(result["error"], "no state");
}

// ============================================================================
// Integration: pause -> status -> resume -> status
// ============================================================================

TEST_F(PlayModeHandlersTest, RoundTrip_PauseResumeStatus) {
    registerHandlers(EngineMode::Play);

    // Initial: not paused
    auto s1 = call("playmode.status");
    EXPECT_EQ(s1["paused"], false);

    // Pause
    call("engine.pause");
    auto s2 = call("playmode.status");
    EXPECT_EQ(s2["paused"], true);

    // Resume
    call("engine.resume");
    auto s3 = call("playmode.status");
    EXPECT_EQ(s3["paused"], false);
}

TEST_F(PlayModeHandlersTest, RoundTrip_SetPausedAndStatus) {
    registerHandlers(EngineMode::Play);

    call("engine.setPaused", {{"paused", true}});
    auto s1 = call("playmode.status");
    EXPECT_EQ(s1["paused"], true);

    call("engine.setPaused", {{"paused", false}});
    auto s2 = call("playmode.status");
    EXPECT_EQ(s2["paused"], false);
}

// ============================================================================
// Unknown handler
// ============================================================================

TEST_F(PlayModeHandlersTest, UnknownHandler_Error) {
    registerHandlers(EngineMode::Editor);
    auto result = call("playmode.nonexistent");
    EXPECT_EQ(result["ok"], false);
    EXPECT_TRUE(result["error"].get<std::string>().find("not found") != std::string::npos);
}
