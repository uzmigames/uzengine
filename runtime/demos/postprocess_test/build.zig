const std = @import("std");
const common = @import("uz_demo_common");

/// Standalone build for the post-process test demo.
/// Run `zig build` from runtime/ first, then `zig build run` from here.
pub fn build(b: *std.Build) void {
    var demo = common.createDemo(b, .{
        .name = "postprocess_test_demo",
        .sources = &.{ "main.cpp", "postprocess_test_demo.cpp" },
        .extra_includes = &.{
            "../../engine/renderer/camera",
            "../../engine/renderer/core",
            "../../engine/renderer/mesh",
            "../../engine/renderer/passes",
            "../../engine/renderer/upload",
            "../../build/generated/shaders",
        },
        .deploy_fxc = true,
    });
    demo.install();
}
