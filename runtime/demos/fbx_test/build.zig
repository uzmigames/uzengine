const std = @import("std");
const common = @import("uz_demo_common");

/// Standalone build for the FBX mesh test demo.
/// Run `zig build` from runtime/ first, then `zig build` from here.
pub fn build(b: *std.Build) void {
    var demo = common.createDemo(b, .{
        .name = "fbx_test",
        .sources = &.{ "main.cpp", "fbx_test.cpp" },
        .use_demo_application = false,
        .deploy_fxc = true,
        .extra_deploy = &.{
            // FBX mesh
            .{ .source = "assets/MushroomMonster_Low.fbx", .dest = "assets/MushroomMonster_Low.fbx" },
            // Textures
            .{ .source = "assets/textures/MushroomMonster_Low_MushroomMesh2_1_BaseColor.1001.png", .dest = "assets/textures/MushroomMonster_Low_MushroomMesh2_1_BaseColor.1001.png" },
            .{ .source = "assets/textures/MushroomMonster_Low_MushroomMesh2_1_Normal.1001.png", .dest = "assets/textures/MushroomMonster_Low_MushroomMesh2_1_Normal.1001.png" },
            .{ .source = "assets/textures/MushroomMonster_Low_MushroomMesh2_1_OcclusionRoughnessMetallic.1001.png", .dest = "assets/textures/MushroomMonster_Low_MushroomMesh2_1_OcclusionRoughnessMetallic.1001.png" },
            // Cloud noise (needed by atmosphere)
            .{ .source = "../../content/atmosphere/shape.bin", .dest = "content/atmosphere/shape.bin" },
            .{ .source = "../../content/atmosphere/shape_detail.bin", .dest = "content/atmosphere/shape_detail.bin" },
        },
    });
    demo.install();
}
