/// Headless integration tests for the shipped path tracer Lua script and the
/// renderer registry seam. No device or render frame is created.

#include "lua_vm.h"
#include "lua_renderer_api.h"
#include "scene_renderer.h"
#include "pathtracing/path_tracer_pass.h"
#include "cvar.h"

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

class LuaPathTracerAPITest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(uz::LuaVM::Init());
        auto& cvars = uz::CVarRegistry::Get();
        m_enabled = cvars.GetFloat("r.PathTracer.Enable");
        m_maxBounces = cvars.GetFloat("r.PathTracer.MaxBounces");
        m_samplesPerFrame = cvars.GetFloat("r.PathTracer.SamplesPerFrame");
        m_envIntensity = cvars.GetFloat("r.PathTracer.EnvIntensity");
    }

    void TearDown() override {
        auto& cvars = uz::CVarRegistry::Get();
        cvars.Set("r.PathTracer.Enable", m_enabled);
        cvars.Set("r.PathTracer.MaxBounces", m_maxBounces);
        cvars.Set("r.PathTracer.SamplesPerFrame", m_samplesPerFrame);
        cvars.Set("r.PathTracer.EnvIntensity", m_envIntensity);
        uz::LuaVM::Shutdown();
    }

    float m_enabled = 0.f;
    float m_maxBounces = 0.f;
    float m_samplesPerFrame = 0.f;
    float m_envIntensity = 0.f;
};

TEST_F(LuaPathTracerAPITest, ScriptBindings) {
    const auto path = std::filesystem::path(__FILE__).parent_path()
        .parent_path().parent_path() / "lua" / "test_pathtracer_api.lua";
    std::ifstream file(path);
    ASSERT_TRUE(file.is_open()) << path;
    const std::string script((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
    const uint32_t resetGeneration = uz::GetPathTracerResetGeneration();
    lua_State* L = uz::LuaVM::GetState();
    const int rc = luaL_dostring(L, script.c_str());
    ASSERT_EQ(rc, 0) << (lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown Lua error");
    EXPECT_EQ(uz::GetPathTracerResetGeneration(), resetGeneration + 1u);
}

TEST_F(LuaPathTracerAPITest, SamplesUseInjectedRenderer) {
    uz::SceneRenderer renderer;
    lua_State* L = uz::LuaVM::GetState();
    uz::LuaSetSceneRenderer(L, &renderer);
    ASSERT_EQ(luaL_dostring(L, "return uz.pathtracer.samples_accumulated()"), 0);
    EXPECT_EQ(lua_tointeger(L, -1), renderer.GetPathTracerSamplesAccumulated());
    lua_pop(L, 1);

    uz::LuaSetSceneRenderer(L, nullptr);
    ASSERT_EQ(luaL_dostring(L, "return uz.pathtracer.samples_accumulated()"), 0);
    EXPECT_EQ(lua_tointeger(L, -1), 0);
    lua_pop(L, 1);
}
