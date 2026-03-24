const std = @import("std");
const common = @import("uz_demo_common");

/// Standalone build for the material test demo.
/// Run `zig build` from runtime/ first, then `zig build` from here.
/// All meshes use builtin: primitives — no external assets needed.
pub fn build(b: *std.Build) void {
    var demo = common.createDemo(b, .{
        .name = "material_test",
        .sources = &.{ "main.cpp", "material_test.cpp" },
        .use_demo_application = false,
        .deploy_fxc = true,
    });
    demo.install();
}
