const std = @import("std");
const common = @import("uz_demo_common");

/// Standalone build for the PBR test demo.
/// Run `zig build` from runtime/ first, then `zig build` from here.
pub fn build(b: *std.Build) void {
    var demo = common.createDemo(b, .{
        .name = "pbr_test",
        .sources = &.{ "main.cpp", "pbr_test.cpp" },
        .use_demo_application = false,
        .deploy_fxc = true,
        .extra_deploy = &.{
            // Mesh
            .{ .source = "assets/cube.uzmesh",              .dest = "assets/cube.uzmesh"              },
            .{ .source = "assets/checker.png",              .dest = "assets/checker.png"               },
            // Material graph
            .{ .source = "assets/mi_procgrid.uzmat",        .dest = "assets/mi_procgrid.uzmat"         },
            // Stonebricks
            .{ .source = "assets/Stonebricks_BaseColor.jpg",.dest = "assets/Stonebricks_BaseColor.jpg" },
            .{ .source = "assets/Stonebricks_Normal.jpg",   .dest = "assets/Stonebricks_Normal.jpg"    },
            .{ .source = "assets/Stonebricks_Roughness.jpg",.dest = "assets/Stonebricks_Roughness.jpg" },
            .{ .source = "assets/Stonebricks_Metallic.jpg", .dest = "assets/Stonebricks_Metallic.jpg"  },
            .{ .source = "assets/Stonebricks_AO.jpg",       .dest = "assets/Stonebricks_AO.jpg"        },
            // Gold
            .{ .source = "assets/Poliigon_MetalGoldPaint_7253_BaseColor.jpg",        .dest = "assets/Poliigon_MetalGoldPaint_7253_BaseColor.jpg"        },
            .{ .source = "assets/Poliigon_MetalGoldPaint_7253_Normal.png",           .dest = "assets/Poliigon_MetalGoldPaint_7253_Normal.png"           },
            .{ .source = "assets/Poliigon_MetalGoldPaint_7253_Roughness.jpg",        .dest = "assets/Poliigon_MetalGoldPaint_7253_Roughness.jpg"        },
            .{ .source = "assets/Poliigon_MetalGoldPaint_7253_Metallic.jpg",         .dest = "assets/Poliigon_MetalGoldPaint_7253_Metallic.jpg"         },
            .{ .source = "assets/Poliigon_MetalGoldPaint_7253_AmbientOcclusion.jpg", .dest = "assets/Poliigon_MetalGoldPaint_7253_AmbientOcclusion.jpg" },
            // Metal
            .{ .source = "assets/MetalGalvanizedSteelWorn001_COL_2K_METALNESS.jpg",       .dest = "assets/MetalGalvanizedSteelWorn001_COL_2K_METALNESS.jpg"       },
            .{ .source = "assets/MetalGalvanizedSteelWorn001_NRM_2K_METALNESS.jpg",       .dest = "assets/MetalGalvanizedSteelWorn001_NRM_2K_METALNESS.jpg"       },
            .{ .source = "assets/MetalGalvanizedSteelWorn001_ROUGHNESS_2K_METALNESS.jpg", .dest = "assets/MetalGalvanizedSteelWorn001_ROUGHNESS_2K_METALNESS.jpg" },
            .{ .source = "assets/MetalGalvanizedSteelWorn001_METALNESS_2K_METALNESS.jpg", .dest = "assets/MetalGalvanizedSteelWorn001_METALNESS_2K_METALNESS.jpg" },
            // RattanWeave
            .{ .source = "assets/Poliigon_RattanWeave_6945_BaseColor.jpg",        .dest = "assets/Poliigon_RattanWeave_6945_BaseColor.jpg"        },
            .{ .source = "assets/Poliigon_RattanWeave_6945_Normal.png",           .dest = "assets/Poliigon_RattanWeave_6945_Normal.png"           },
            .{ .source = "assets/Poliigon_RattanWeave_6945_Roughness.jpg",        .dest = "assets/Poliigon_RattanWeave_6945_Roughness.jpg"        },
            .{ .source = "assets/Poliigon_RattanWeave_6945_Metallic.jpg",         .dest = "assets/Poliigon_RattanWeave_6945_Metallic.jpg"         },
            .{ .source = "assets/Poliigon_RattanWeave_6945_AmbientOcclusion.jpg", .dest = "assets/Poliigon_RattanWeave_6945_AmbientOcclusion.jpg" },
            // StoneQuartzite
            .{ .source = "assets/Poliigon_StoneQuartzite_8060_BaseColor.jpg",        .dest = "assets/Poliigon_StoneQuartzite_8060_BaseColor.jpg"        },
            .{ .source = "assets/Poliigon_StoneQuartzite_8060_Normal.png",           .dest = "assets/Poliigon_StoneQuartzite_8060_Normal.png"           },
            .{ .source = "assets/Poliigon_StoneQuartzite_8060_Roughness.jpg",        .dest = "assets/Poliigon_StoneQuartzite_8060_Roughness.jpg"        },
            .{ .source = "assets/Poliigon_StoneQuartzite_8060_Metallic.jpg",         .dest = "assets/Poliigon_StoneQuartzite_8060_Metallic.jpg"         },
            .{ .source = "assets/Poliigon_StoneQuartzite_8060_AmbientOcclusion.jpg", .dest = "assets/Poliigon_StoneQuartzite_8060_AmbientOcclusion.jpg" },
            // Concrete
            .{ .source = "assets/Poliigon_ConcreteWorn_8690_BaseColor.jpg",        .dest = "assets/Poliigon_ConcreteWorn_8690_BaseColor.jpg"        },
            .{ .source = "assets/Poliigon_ConcreteWorn_8690_Normal.png",           .dest = "assets/Poliigon_ConcreteWorn_8690_Normal.png"           },
            .{ .source = "assets/Poliigon_ConcreteWorn_8690_Roughness.jpg",        .dest = "assets/Poliigon_ConcreteWorn_8690_Roughness.jpg"        },
            .{ .source = "assets/Poliigon_ConcreteWorn_8690_Metallic.jpg",         .dest = "assets/Poliigon_ConcreteWorn_8690_Metallic.jpg"         },
            .{ .source = "assets/Poliigon_ConcreteWorn_8690_AmbientOcclusion.jpg", .dest = "assets/Poliigon_ConcreteWorn_8690_AmbientOcclusion.jpg" },
            // MetalCorroded
            .{ .source = "assets/MetalCorrodedHeavy001_COL_2K_METALNESS.jpg",       .dest = "assets/MetalCorrodedHeavy001_COL_2K_METALNESS.jpg"       },
            .{ .source = "assets/MetalCorrodedHeavy001_NRM_2K_METALNESS.jpg",       .dest = "assets/MetalCorrodedHeavy001_NRM_2K_METALNESS.jpg"       },
            .{ .source = "assets/MetalCorrodedHeavy001_ROUGHNESS_2K_METALNESS.jpg", .dest = "assets/MetalCorrodedHeavy001_ROUGHNESS_2K_METALNESS.jpg" },
            .{ .source = "assets/MetalCorrodedHeavy001_METALNESS_2K_METALNESS.jpg", .dest = "assets/MetalCorrodedHeavy001_METALNESS_2K_METALNESS.jpg" },
            // Grass
            .{ .source = "assets/Poliigon_GrassPatchyGround_4585_BaseColor.jpg",        .dest = "assets/Poliigon_GrassPatchyGround_4585_BaseColor.jpg"        },
            .{ .source = "assets/Poliigon_GrassPatchyGround_4585_Normal.png",           .dest = "assets/Poliigon_GrassPatchyGround_4585_Normal.png"           },
            .{ .source = "assets/Poliigon_GrassPatchyGround_4585_Roughness.jpg",        .dest = "assets/Poliigon_GrassPatchyGround_4585_Roughness.jpg"        },
            .{ .source = "assets/Poliigon_GrassPatchyGround_4585_Metallic.jpg",         .dest = "assets/Poliigon_GrassPatchyGround_4585_Metallic.jpg"         },
            .{ .source = "assets/Poliigon_GrassPatchyGround_4585_AmbientOcclusion.jpg", .dest = "assets/Poliigon_GrassPatchyGround_4585_AmbientOcclusion.jpg" },
            // Sand
            .{ .source = "assets/GroundSand005_COL_2K.jpg", .dest = "assets/GroundSand005_COL_2K.jpg" },
            .{ .source = "assets/GroundSand005_NRM_2K.jpg", .dest = "assets/GroundSand005_NRM_2K.jpg" },
            .{ .source = "assets/GroundSand005_AO_2K.jpg",  .dest = "assets/GroundSand005_AO_2K.jpg"  },
            // Cloud noise textures (precomputed 3D Perlin-Worley volumes)
            .{ .source = "assets/cloud_shape.bin",        .dest = "assets/cloud_shape.bin"        },
            .{ .source = "assets/cloud_shape_detail.bin", .dest = "assets/cloud_shape_detail.bin" },
        },
    });
    demo.install();
}
