const std = @import("std");
const common = @import("uz_demo_common");

/// Standalone build for the PBR test demo.
/// Run `zig build` from runtime/ first, then `zig build run` from here.
///
/// The `new` option (default: false) selects between:
///   false → pbr_test_demo  (legacy DemoApplication)
///   true  → pbr_test_app   (new uz::Application)
pub fn build(b: *std.Build) void {
    const use_new = b.option(bool, "new", "Build the Application-based demo instead of legacy") orelse false;

    const assets = &.{
        common.DeployFile{ .source = "assets/cube.uzmesh", .dest = "assets/cube.uzmesh" },
        common.DeployFile{ .source = "assets/Stonebricks_BaseColor.jpg", .dest = "assets/Stonebricks_BaseColor.jpg" },
        common.DeployFile{ .source = "assets/Stonebricks_Normal.jpg", .dest = "assets/Stonebricks_Normal.jpg" },
        common.DeployFile{ .source = "assets/Stonebricks_Roughness.jpg", .dest = "assets/Stonebricks_Roughness.jpg" },
        common.DeployFile{ .source = "assets/Stonebricks_Metallic.jpg", .dest = "assets/Stonebricks_Metallic.jpg" },
        common.DeployFile{ .source = "assets/Stonebricks_AO.jpg", .dest = "assets/Stonebricks_AO.jpg" },
        common.DeployFile{ .source = "assets/checker.png", .dest = "assets/checker.png" },
    };

    if (use_new) {
        // New uz::Application-based demo.
        // -DUSE_APPLICATION switches main.cpp to UZ_APPLICATION_MAIN(PBRTestApp).
        var demo = common.createDemo(b, .{
            .name = "pbr_test_app",
            .sources = &.{ "main.cpp", "pbr_test_app.cpp" },
            .extra_defines = &.{"USE_APPLICATION"},
            .deploy_fxc = true,
            .extra_deploy = assets,
        });
        demo.install();
    } else {
        // Legacy DemoApplication-based demo.
        var demo = common.createDemo(b, .{
            .name = "pbr_test_demo",
            .sources = &.{ "main.cpp", "pbr_test_demo.cpp" },
            .deploy_fxc = true,
            .extra_deploy = assets,
        });
        demo.install();
    }
}
