const std = @import("std");
const common = @import("uz_demo_common");

/// Standalone build for the deferred rendering test demo.
/// Run `zig build` from runtime/ first, then `zig build run` from here.
pub fn build(b: *std.Build) void {
    var demo = common.createDemo(b, .{
        .name = "deferred_test",
        .sources = &.{ "main.cpp", "deferred_test_demo.cpp" },
        .extra_sources = &.{
            .{ .root = "../../editor/grid",  .files = &.{"editor_grid.cpp"} },
            .{ .root = "../../editor/font",  .files = &.{ "bitmap_font.cpp", "text_renderer.cpp" } },
            .{ .root = "../../editor/stats", .files = &.{"stats_overlay.cpp"} },
        },
        .extra_includes = &.{
            "../../engine/renderer/camera",
            "../../engine/renderer/core",
            "../../engine/renderer/mesh",
            "../../engine/renderer/shadows",
            "../../engine/renderer/upload",
            "../../editor/grid",
            "../../editor/font",
            "../../editor/stats",
            "../../build/generated/shaders",
        },
        .deploy_fxc = true,
        .extra_deploy = &.{
            .{ .source = "assets/cube.uzmesh",                .dest = "assets/cube.uzmesh" },
            .{ .source = "assets/Stonebricks_BaseColor.jpg",  .dest = "assets/Stonebricks_BaseColor.jpg" },
            .{ .source = "assets/Stonebricks_Normal.jpg",     .dest = "assets/Stonebricks_Normal.jpg" },
            .{ .source = "assets/Stonebricks_Roughness.jpg",  .dest = "assets/Stonebricks_Roughness.jpg" },
            .{ .source = "assets/Stonebricks_Metallic.jpg",   .dest = "assets/Stonebricks_Metallic.jpg" },
            .{ .source = "assets/Stonebricks_AO.jpg",         .dest = "assets/Stonebricks_AO.jpg" },
            .{ .source = "assets/checker.png",                .dest = "assets/checker.png" },
        },
    });
    demo.install();
}
