/// UzEngine Demo Build Common — shared build logic for all standalone demos.
///
/// Each demo's build.zig imports this module and calls `createDemo()` with its
/// name and source list. This eliminates ~80 lines of duplicated boilerplate
/// (include paths, thirdparty paths, system libs, DLL deployment, icon, run step).
///
/// Usage from a demo's build.zig:
/// ```zig
/// const common = @import("../common/build_common.zig");
///
/// pub fn build(b: *std.Build) void {
///     var demo = common.createDemo(b, .{
///         .name = "triangle_demo",
///         .sources = &.{ "main.cpp", "triangle_demo.cpp" },
///     });
///     // Add demo-specific extras here if needed:
///     // demo.mod.addIncludePath(b.path("../common"));
///     demo.install();
/// }
/// ```
const std = @import("std");

/// Configuration for a demo executable.
pub const DemoConfig = struct {
    /// Executable name (without .exe extension).
    name: []const u8,
    /// C++ source files relative to the demo directory.
    sources: []const []const u8,
    /// Extra C++ source file groups: each entry is { root_path, &.{ files... } }.
    /// Use for shared sources like fps_camera_controller, editor_grid, etc.
    extra_sources: []const ExtraSource = &.{},
    /// Extra include paths beyond the standard engine/thirdparty set.
    extra_includes: []const []const u8 = &.{},
    /// Extra preprocessor defines passed to all source files (e.g., "USE_APPLICATION").
    /// Each entry is a bare name — the value is always "1".
    extra_defines: []const []const u8 = &.{},
    /// Whether to compile demo_application.cpp (the legacy DemoApplication base class).
    /// Set to false for demos using the new uz::Application pattern.
    use_demo_application: bool = true,
    /// Whether to deploy d3dcompiler_47.dll (FXC). Some demos need it, others don't.
    deploy_fxc: bool = false,
    /// Extra files to deploy (legacy — prefer asset_dirs for pak mode).
    /// Each entry is { source_path, install_subpath }.
    extra_deploy: []const DeployFile = &.{},
    /// Asset directories to pack into build/data/game.uzpak via uzpack.
    /// Paths relative to the demo directory. All directories are staged
    /// into a temp folder, then packed. Empty = no packing step.
    asset_dirs: []const []const u8 = &.{},
    /// When true, runs uzcook on asset_dirs before packing to compress
    /// textures (PNG/JPG → BC7/BC5/BC4 .uztexture). Cooked outputs replace
    /// originals in the pak. Requires uzcook.exe built by the runtime.
    cook_textures: bool = false,
};

pub const ExtraSource = struct {
    root: []const u8,
    files: []const []const u8,
};

pub const DeployFile = struct {
    source: []const u8,
    dest: []const u8,
};

/// Handle returned by createDemo — call install() to finalize.
pub const DemoHandle = struct {
    b: *std.Build,
    mod: *std.Build.Module,
    exe: *std.Build.Step.Compile,
    config: DemoConfig,

    /// Finalize: install artifact, deploy DLLs, cook textures, pack assets, create run step.
    pub fn install(self: *DemoHandle) void {
        self.b.installArtifact(self.exe);
        self.deployDlls();
        if (self.config.cook_textures) self.cookTextures();
        self.packAssets();
        self.createRunStep();
    }

    fn deployDlls(self: *DemoHandle) void {
        const dlls = [_]struct { src: []const u8, dst: []const u8 }{
            .{ .src = "../../thirdparty/agility/bin/x64/D3D12Core.dll", .dst = "D3D12/D3D12Core.dll" },
            .{ .src = "../../thirdparty/agility/bin/x64/d3d12SDKLayers.dll", .dst = "D3D12/d3d12SDKLayers.dll" },
            .{ .src = "../../thirdparty/dxc/bin/x64/dxcompiler.dll", .dst = "dxcompiler.dll" },
            .{ .src = "../../thirdparty/dxc/bin/x64/dxil.dll", .dst = "dxil.dll" },
            .{ .src = "../../thirdparty/luajit/src/lua51.dll", .dst = "lua51.dll" },
        };
        for (dlls) |dll| {
            self.b.getInstallStep().dependOn(&self.b.addInstallFileWithDir(
                self.b.path(dll.src),
                .{ .custom = "lib" },
                dll.dst,
            ).step);
        }

        if (self.config.deploy_fxc) {
            self.b.getInstallStep().dependOn(&self.b.addInstallFileWithDir(
                self.b.path("../../thirdparty/fxc/bin/x64/d3dcompiler_47.dll"),
                .{ .custom = "lib" },
                "d3dcompiler_47.dll",
            ).step);
        }

        for (self.config.extra_deploy) |deploy| {
            self.b.getInstallStep().dependOn(&self.b.addInstallFileWithDir(
                self.b.path(deploy.source),
                .bin,
                deploy.dest,
            ).step);
        }
    }

    fn cookTextures(self: *DemoHandle) void {
        if (self.config.asset_dirs.len == 0) return;

        // uzcook is built by the runtime at ../../build/bin/uzcook.exe
        const uzcook_path = self.b.path("../../build/bin/uzcook.exe");

        // Cook each asset directory: PNG/JPG → BC7/BC5/BC4 .uztexture
        // uzcook reads source textures and writes .uztexture files alongside
        // the originals (in the cache dir), which uzpack then picks up.
        for (self.config.asset_dirs) |dir| {
            const cook_cmd = self.b.addSystemCommand(&.{
                uzcook_path.getPath(self.b),
                "--source",
                self.b.path(dir).getPath(self.b),
                "--output",
                self.b.path(dir).getPath(self.b),
                "--textures-only",
            });
            cook_cmd.step.dependOn(&self.exe.step);
            self.b.getInstallStep().dependOn(&cook_cmd.step);
        }
    }

    fn packAssets(self: *DemoHandle) void {
        if (self.config.asset_dirs.len == 0) return;

        // uzpack is built by the runtime at ../../build/bin/uzpack.exe
        const uzpack_path = self.b.path("../../build/bin/uzpack.exe");
        const output_dir = self.b.getInstallPath(.{ .custom = "data" }, "");

        // Pack all asset directories into a single .uzpak. uzpack supports
        // multiple --input flags — each directory is scanned and merged.
        const pack_cmd = self.b.addSystemCommand(&.{uzpack_path.getPath(self.b)});
        for (self.config.asset_dirs) |dir| {
            pack_cmd.addArgs(&.{ "--input", self.b.path(dir).getPath(self.b) });
        }
        pack_cmd.addArgs(&.{ "--output", output_dir, "--name", "game" });

        // Pack runs after the exe is compiled (no circular dependency).
        pack_cmd.step.dependOn(&self.exe.step);

        // Default build includes packing — no separate "zig build pack" needed.
        self.b.getInstallStep().dependOn(&pack_cmd.step);

        // Also expose as a standalone "pack" step for manual re-packing.
        const pack_step = self.b.step("pack", "Pack assets into .uzpak");
        pack_step.dependOn(&pack_cmd.step);
    }

    fn createRunStep(self: *DemoHandle) void {
        const exe_path = self.b.getInstallPath(.bin, self.b.fmt("{s}.exe", .{ self.config.name }));
        const run = self.b.addSystemCommand(&.{exe_path});
        run.step.dependOn(self.b.getInstallStep());
        const step = self.b.step("run", self.b.fmt("Run the {s}", .{self.config.name}));
        step.dependOn(&run.step);
    }
};

/// Create a demo executable with all standard UzEngine build configuration.
pub fn createDemo(b: *std.Build, config: DemoConfig) DemoHandle {
    // Default output to build/ — matches runtime convention. Override with -p <path>.
    if (std.mem.endsWith(u8, b.install_prefix, "zig-out")) {
        b.resolveInstallPrefix("build", .{});
    }

    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const cpp_flags = &.{ "-std=c++17", "-DSPDLOG_COMPILED_LIB" };

    const mod = b.createModule(.{ .target = target, .optimize = optimize });
    mod.link_libc = true;
    mod.link_libcpp = true;

    // Demo's own sources
    mod.addCSourceFiles(.{
        .root = b.path("."),
        .files = config.sources,
        .flags = cpp_flags,
    });

    // DX12 Agility SDK export (required by all demos)
    mod.addCSourceFiles(.{
        .root = b.path("../../engine/rhi/dx12"),
        .files = &.{"dx12_agility.cpp"},
        .flags = cpp_flags,
    });

    // DemoApplication base class — only needed for legacy DemoApplication-based demos.
    if (config.use_demo_application) {
        mod.addCSourceFiles(.{
            .root = b.path("../common"),
            .files = &.{"demo_application.cpp"},
            .flags = cpp_flags,
        });
    }

    // Extra preprocessor defines (applied module-wide)
    for (config.extra_defines) |define| mod.addCMacro(define, "1");

    // Extra source groups
    for (config.extra_sources) |src| {
        mod.addCSourceFiles(.{
            .root = b.path(src.root),
            .files = src.files,
            .flags = cpp_flags,
        });
    }

    // Engine include paths
    const engine_dirs = [_][]const u8{
        "../..",
        "../common",
        "../../engine",
        "../../engine/app",
        "../../engine/logging",
        "../../engine/assertions",
        "../../engine/profiling",
        "../../engine/memory",
        "../../engine/config",
        "../../engine/math",
        "../../engine/containers",
        "../../engine/random",
        "../../engine/rhi",
        "../../engine/shader",
        "../../engine/paths",
        "../../engine/renderer",
        "../../engine/renderer/render_graph",
        "../../engine/renderer/camera",
        "../../engine/renderer/core",
        "../../engine/renderer/core/params",
        "../../engine/renderer/core/gbuffer",
        "../../engine/renderer/core/shader",
        "../../engine/renderer/core/pipeline",
        "../../engine/renderer/mesh",
        "../../engine/renderer/shadows",
        "../../engine/renderer/upload",
        "../../engine/renderer/material",
        "../../engine/material",
        "../../engine/material/compiler",
        "../../engine/renderer/ibl",
        "../../engine/renderer/lighting",
        "../../engine/renderer/atmosphere",
        "../../engine/renderer/fog",
        "../../engine/renderer/scene",
        "../../engine/renderer/frame",
        "../../engine/renderer/passes",
        "../../engine/renderer/passes/geometry",
        "../../engine/renderer/passes/lighting",
        "../../engine/renderer/passes/atmosphere",
        "../../engine/renderer/passes/postprocess",
        "../../engine/renderer/passes/debug",
        "../../engine/renderer/passes/ibl",
        "../../engine/asset",
        "../../engine/asset/formats",
        "../../engine/asset/importers",
        "../../engine/ecs",
        "../../engine/ecs/core",
        "../../engine/ecs/scene",
        "../../engine/ecs/lighting",
        "../../engine/ecs/mesh",
        "../../engine/ecs/environment",
        "../../engine/ecs/metadata",
        "../../engine/ecs/update",
        "../../engine/ecs/loaders",
        "../../engine/scripting",
        "../../engine/event",
        "../../engine/property",
        "../../engine/job",
        "../../platform/windows",
        "../../engine/rhi/dx12",
        "../../engine/rhi/dx11",
        "../../engine/rhi/vulkan",
        "../../engine/rhi/optimization",
        "../../engine/renderer/memory",
        "../../editor",
        "../../editor/screenshot",
        "../../editor/grid",
        "../../editor/font",
        "../../editor/stats",
        "../../build/generated/shaders",
    };
    for (engine_dirs) |dir| mod.addIncludePath(b.path(dir));

    // Thirdparty include paths
    const tp_dirs = [_][]const u8{
        "../../thirdparty/spdlog/include",
        "../../thirdparty/tracy/public",
        "../../thirdparty/mimalloc/include",
        "../../thirdparty/directxmath/Inc",
        "../../thirdparty/glm",
        "../../thirdparty/nlohmann",
        "../../thirdparty/atomic_queue/include",
        "../../thirdparty/spirv-reflect",
        "../../thirdparty/spirv-reflect/include",
        "../../thirdparty/xxhash",
        "../../thirdparty/lz4/lib",
        "../../thirdparty/stb",
        "../../thirdparty/tinyexr",
        "../../thirdparty/libjpeg-turbo",
        "../../thirdparty/basis_universal/transcoder",
        "../../thirdparty/astcenc/Source",
        "../../thirdparty/d3d12ma/include",
        "../../thirdparty/directx_headers/include",
        "../../thirdparty/directx_headers/include/directx",
        "../../thirdparty/agility/include",
        "../../thirdparty/dxc/inc",
        "../../thirdparty/vulkan_headers/include",
        "../../thirdparty/volk",
        "../../thirdparty/vma/include",
        "../../thirdparty/luajit/src",
        "../../thirdparty/entt/single_include",
    };
    for (tp_dirs) |dir| mod.addIncludePath(b.path(dir));

    // Demo's own directory
    mod.addIncludePath(b.path("."));

    // Extra include paths
    for (config.extra_includes) |dir| mod.addIncludePath(b.path(dir));

    // Link pre-compiled runtime + editor libs
    mod.addObjectFile(b.path("../../build/lib/runtime.lib"));
    mod.addObjectFile(b.path("../../build/lib/editor.lib"));

    // Application icon
    mod.addWin32ResourceFile(.{ .file = b.path("../common/app.rc") });

    const exe = b.addExecutable(.{ .name = config.name, .root_module = mod });

    // System libraries
    const sys_libs = [_][]const u8{ "d3d12", "d3d11", "dxgi", "dxguid", "gdi32", "user32", "ws2_32", "dbghelp", "winmm", "shcore", "xinput1_4" };
    for (sys_libs) |l| exe.linkSystemLibrary(l);

    return .{ .b = b, .mod = mod, .exe = exe, .config = config };
}
