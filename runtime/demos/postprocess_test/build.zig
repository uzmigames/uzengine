const std = @import("std");
const common = @import("uz_demo_common");

/// Standalone build for the post-process test demo.
/// Run `zig build` from runtime/ first, then `zig build run` from here.
pub fn build(b: *std.Build) void {
    var demo = common.createDemo(b, .{
        .name = "postprocess_test_demo",
        .sources = &.{ "main.cpp", "postprocess_test_demo.cpp" },
        .extra_includes = &.{},
        .deploy_fxc = true,
        .extra_deploy = &.{
            .{ .source = "assets/cube.uzmesh", .dest = "assets/cube.uzmesh" },
            .{ .source = "assets/sphere.uzmesh", .dest = "assets/sphere.uzmesh" },
            .{ .source = "assets/checker.png", .dest = "assets/checker.png" },
        },
    });
    demo.install();
}
