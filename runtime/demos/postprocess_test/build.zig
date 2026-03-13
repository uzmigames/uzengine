const std = @import("std");
const common = @import("uz_demo_common");

/// Standalone build for the post-process test demo.
/// Run `zig build` from runtime/ first, then `zig build run` from here.
pub fn build(b: *std.Build) void {
    var demo = common.createDemo(b, .{
        .name = "postprocess_test_demo",
        .sources = &.{ "main.cpp", "postprocess_test_demo.cpp" },
        .extra_sources = &.{
            .{ .root = "../../editor/grid", .files = &.{"editor_grid.cpp"} },
            .{ .root = "../../editor/font", .files = &.{ "bitmap_font.cpp", "text_renderer.cpp" } },
            .{ .root = "../../editor/stats", .files = &.{"stats_overlay.cpp"} },
        },
        .extra_includes = &.{
            "../../engine/renderer/camera",
            "../../engine/renderer/core",
            "../../engine/renderer/mesh",
            "../../engine/renderer/passes",
            "../../engine/renderer/upload",
            "../../engine/renderer/material",
            "../../engine/renderer/shadows",
            "../../engine/ecs",
            "../../engine/event",
            "../../engine/property",
            "../../engine/job",
            "../../editor/grid",
            "../../editor/font",
            "../../editor/stats",
            "../../build/generated/shaders",
        },
        .deploy_fxc = true,
        .extra_deploy = &.{
            .{ .source = "assets/cube.uzmesh", .dest = "assets/cube.uzmesh" },
            .{ .source = "assets/sphere.uzmesh", .dest = "assets/sphere.uzmesh" },
            .{ .source = "assets/checker.png", .dest = "assets/checker.png" },
        },
    });
    demo.install();
}
