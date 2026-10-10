const std = @import("std");
const common = @import("uz_demo_common");

pub fn build(b: *std.Build) void {
    var demo = common.createDemo(b, .{
        .name = "vfx_viewport_app",
        .sources = &.{
            "main.cpp",
            "vfx_viewport_app.cpp",
        },
        .use_demo_application = false,
        // Phase 0 (demo launch pipeline fix) — the VFX viewport uses
        // the engine's ShaderCompilerFXC path (for SM5 fallback) which
        // dynamically loads d3dcompiler_47.dll. Without this flag the
        // DLL never lands in the demo's bin/ and startup fails with
        // Win32 error 126.
        .deploy_fxc = true,
    });
    demo.install();
}
