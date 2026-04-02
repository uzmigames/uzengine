/// VFX Showcase — Lua script host entry point.
///
/// This demo consists of a single Lua script (lua_vfx_showcase.lua) that
/// exercises the uz.vfx API. The script must be attached to a scene node
/// within a running engine instance that has the Lua VM initialized.
///
/// Usage:
///   Attach lua_vfx_showcase.lua to any scene node:
///     { "script": "demos/vfx_showcase/lua_vfx_showcase.lua" }
///   Replace asset paths inside the script with your project's .uzvfx /
///   .uzmat files as required.
///
/// This executable exists solely to satisfy the build system requirement
/// that every demo in runtime/demos/ produces an artifact, and to deploy
/// the Lua script alongside it in the build output.

#include "engine_loop.h"
#include "log.h"

#include <cstdio>

namespace {

/// Wraps the vfx_showcase demo as an EngineLoop subsystem.
class VfxShowcaseSubsystem final : public uz::ISubsystem {
public:
    [[nodiscard]] const char* GetName() const override { return "VfxShowcase"; }
    [[nodiscard]] uz::InitPhase GetInitPhase() const override { return uz::InitPhase::Init; }
    [[nodiscard]] uz::TickGroup GetTickGroup() const override { return uz::TickGroup::None; }

    [[nodiscard]] bool Init() override {
std::puts("VFX Showcase: this demo is a Lua script, not a standalone executable.");
    std::puts("Attach runtime/demos/vfx_showcase/lua_vfx_showcase.lua to a scene node");
    std::puts("inside a running UzEngine instance with the Lua VM enabled.");
    std::puts("See lua_vfx_showcase.lua for full API reference and configuration.");
    return 0;
    }

    void Shutdown() override {}
};

} // anonymous namespace

int main(int argc, char* argv[]) {
    uz::LogInit();

    uz::EngineLoop loop;
    loop.RegisterSubsystem(std::make_unique<VfxShowcaseSubsystem>());

    uz::EngineConfig config = uz::ParseCommandLine(argc, argv);
    config.maxFrames = 1;
    int result = loop.Run(config);

    uz::LogShutdown();
    return result;
}
