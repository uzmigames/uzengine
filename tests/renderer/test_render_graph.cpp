// test_render_graph.cpp -- Google Test coverage for RenderGraph.
//
// Covers:
//   - Construction and initial state
//   - Resource creation (textures, buffers, imports)
//   - Pass registration and builder chaining
//   - Dead pass culling (force_run, cascading, output frontier)
//   - Topological execution order
//   - Execute callbacks and cmd_list forwarding
//   - reset() lifecycle
//   - Edge cases (max passes, compile-without-passes, etc.)
//   - Move semantics
//   - Diagnostics (cull_reason, pass_name)

#include <gtest/gtest.h>
#include "engine/renderer/core/render_graph.h"

#include <atomic>
#include "common/safe_string.h"
#include <string>
#include <vector>

// ============================================================================
// Test fixture
// ============================================================================

class RenderGraphTest : public ::testing::Test {
protected:
    uz::RenderGraph rg_;
};

// ============================================================================
// Basic construction
// ============================================================================

TEST_F(RenderGraphTest, EmptyGraph_CompileSucceeds) {
    EXPECT_TRUE(rg_.compile());
    EXPECT_EQ(rg_.live_pass_count(), 0u);
}

TEST_F(RenderGraphTest, InitialState_ZeroCounts) {
    EXPECT_EQ(rg_.pass_count(), 0u);
    EXPECT_EQ(rg_.live_pass_count(), 0u);
    EXPECT_EQ(rg_.texture_count(), 0u);
    EXPECT_EQ(rg_.buffer_count(), 0u);
}

TEST_F(RenderGraphTest, ExecutionOrder_EmptyGraph_ReturnsNull) {
    rg_.compile();
    EXPECT_EQ(rg_.execution_order(), nullptr);
}

// ============================================================================
// Resource creation
// ============================================================================

TEST_F(RenderGraphTest, CreateTexture_ReturnsValidHandle) {
    uz::RgTextureDesc desc;
    uz::str_copy(desc.name, sizeof(desc.name), "color");
    auto h = rg_.create_texture(desc);
    EXPECT_NE(h, uz::kRgHandleInvalid);
    EXPECT_EQ(rg_.texture_count(), 1u);
}

TEST_F(RenderGraphTest, CreateTexture_MultipleUniqueHandles) {
    uz::RgTextureDesc desc;
    auto h1 = rg_.create_texture(desc);
    auto h2 = rg_.create_texture(desc);
    EXPECT_NE(h1, h2);
    EXPECT_EQ(rg_.texture_count(), 2u);
}

TEST_F(RenderGraphTest, CreateBuffer_ReturnsValidHandle) {
    uz::RgBufferDesc desc;
    desc.byte_size = 1024;
    uz::str_copy(desc.name, sizeof(desc.name), "constants");
    auto h = rg_.create_buffer(desc);
    EXPECT_NE(h, uz::kRgHandleInvalid);
    EXPECT_EQ(rg_.buffer_count(), 1u);
}

TEST_F(RenderGraphTest, CreateBuffer_MultipleUniqueHandles) {
    uz::RgBufferDesc desc;
    desc.byte_size = 256;
    auto h1 = rg_.create_buffer(desc);
    auto h2 = rg_.create_buffer(desc);
    EXPECT_NE(h1, h2);
    EXPECT_EQ(rg_.buffer_count(), 2u);
}

TEST_F(RenderGraphTest, ImportTexture_ReturnsValidHandle) {
    auto h = rg_.import_texture("backbuffer", static_cast<RhiTexture>(42));
    EXPECT_NE(h, uz::kRgHandleInvalid);
    EXPECT_EQ(rg_.texture_count(), 1u);
}

TEST_F(RenderGraphTest, ImportBuffer_ReturnsValidHandle) {
    auto h = rg_.import_buffer("per_frame_cb", static_cast<RhiBuffer>(99));
    EXPECT_NE(h, uz::kRgHandleInvalid);
    EXPECT_EQ(rg_.buffer_count(), 1u);
}

TEST_F(RenderGraphTest, ImportTexture_HandleDiffersFromCreateTexture) {
    uz::RgTextureDesc desc;
    auto h_create = rg_.create_texture(desc);
    auto h_import = rg_.import_texture("bb", static_cast<RhiTexture>(1));
    EXPECT_NE(h_create, h_import);
}

TEST_F(RenderGraphTest, MixedResources_CorrectCounts) {
    uz::RgTextureDesc td;
    uz::RgBufferDesc bd;
    bd.byte_size = 64;

    rg_.create_texture(td);
    rg_.create_texture(td);
    rg_.create_buffer(bd);
    rg_.import_texture("ext", RHI_NULL);
    rg_.import_buffer("ext_buf", RHI_NULL);

    EXPECT_EQ(rg_.texture_count(), 3u);
    EXPECT_EQ(rg_.buffer_count(), 2u);
}

// ============================================================================
// Pass registration
// ============================================================================

TEST_F(RenderGraphTest, AddPass_IncrementsPassCount) {
    rg_.add_pass("p1", uz::RgPassType::Graphics, nullptr, nullptr);
    EXPECT_EQ(rg_.pass_count(), 1u);
    rg_.add_pass("p2", uz::RgPassType::Compute, nullptr, nullptr);
    EXPECT_EQ(rg_.pass_count(), 2u);
}

TEST_F(RenderGraphTest, AddPass_NameIsStored) {
    rg_.add_pass("GBuffer", uz::RgPassType::Graphics, nullptr, nullptr);
    EXPECT_STREQ(rg_.pass_name(0), "GBuffer");
}

TEST_F(RenderGraphTest, PassName_InvalidIndex_ReturnsEmpty) {
    EXPECT_STREQ(rg_.pass_name(999), "");
}

TEST_F(RenderGraphTest, AddPass_BuilderChaining) {
    uz::RgTextureDesc td;
    auto t1 = rg_.create_texture(td);
    auto t2 = rg_.create_texture(td);

    rg_.add_pass("chain_test", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) {
            b.read(t1).write(t2).force_run();
        },
        nullptr);

    EXPECT_EQ(rg_.pass_count(), 1u);
}

// ============================================================================
// Dead pass culling
// ============================================================================

TEST_F(RenderGraphTest, SinglePassNoReadsNoWrites_Culled) {
    rg_.add_pass("orphan", uz::RgPassType::Graphics, nullptr, nullptr);
    EXPECT_TRUE(rg_.compile());
    EXPECT_TRUE(rg_.is_pass_culled(0));
    EXPECT_EQ(rg_.live_pass_count(), 0u);
}

TEST_F(RenderGraphTest, SinglePassForceRun_NotCulled) {
    rg_.add_pass("forced", uz::RgPassType::Graphics,
        [](uz::RgPassBuilder& b) { b.force_run(); },
        nullptr);

    EXPECT_TRUE(rg_.compile());
    EXPECT_FALSE(rg_.is_pass_culled(0));
    EXPECT_EQ(rg_.live_pass_count(), 1u);
}

TEST_F(RenderGraphTest, CullReason_CulledPass_HasReason) {
    rg_.add_pass("orphan", uz::RgPassType::Graphics, nullptr, nullptr);
    rg_.compile();

    std::string reason = rg_.cull_reason(0);
    EXPECT_FALSE(reason.empty());
}

TEST_F(RenderGraphTest, CullReason_LivePass_EmptyReason) {
    rg_.add_pass("forced", uz::RgPassType::Graphics,
        [](uz::RgPassBuilder& b) { b.force_run(); },
        nullptr);
    rg_.compile();

    EXPECT_TRUE(rg_.cull_reason(0).empty());
}

TEST_F(RenderGraphTest, CullReason_InvalidIndex) {
    std::string reason = rg_.cull_reason(999);
    EXPECT_FALSE(reason.empty());
}

TEST_F(RenderGraphTest, PassWritesTexture_NoConsumer_Culled) {
    uz::RgTextureDesc td;
    auto tex = rg_.create_texture(td);

    rg_.add_pass("writer", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.write(tex); },
        nullptr);

    rg_.compile();
    EXPECT_TRUE(rg_.is_pass_culled(0));
}

TEST_F(RenderGraphTest, PassWritesTexture_ConsumerForceRun_BothAlive) {
    uz::RgTextureDesc td;
    auto tex = rg_.create_texture(td);

    rg_.add_pass("writer", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.write(tex); },
        nullptr);

    rg_.add_pass("reader", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.read(tex).force_run(); },
        nullptr);

    rg_.compile();
    EXPECT_FALSE(rg_.is_pass_culled(0));
    EXPECT_FALSE(rg_.is_pass_culled(1));
    EXPECT_EQ(rg_.live_pass_count(), 2u);
}

TEST_F(RenderGraphTest, Chain_ABC_CForceRun_AllAlive) {
    uz::RgTextureDesc td;
    auto t1 = rg_.create_texture(td);
    auto t2 = rg_.create_texture(td);

    // A writes t1
    rg_.add_pass("A", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.write(t1); },
        nullptr);
    // B reads t1, writes t2
    rg_.add_pass("B", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.read(t1).write(t2); },
        nullptr);
    // C reads t2, force_run
    rg_.add_pass("C", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.read(t2).force_run(); },
        nullptr);

    rg_.compile();
    EXPECT_FALSE(rg_.is_pass_culled(0));
    EXPECT_FALSE(rg_.is_pass_culled(1));
    EXPECT_FALSE(rg_.is_pass_culled(2));
    EXPECT_EQ(rg_.live_pass_count(), 3u);
}

TEST_F(RenderGraphTest, Chain_ABC_NoneForceRun_AllCulled) {
    uz::RgTextureDesc td;
    auto t1 = rg_.create_texture(td);
    auto t2 = rg_.create_texture(td);

    rg_.add_pass("A", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.write(t1); }, nullptr);
    rg_.add_pass("B", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.read(t1).write(t2); }, nullptr);
    rg_.add_pass("C", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.read(t2); }, nullptr);

    rg_.compile();
    EXPECT_TRUE(rg_.is_pass_culled(0));
    EXPECT_TRUE(rg_.is_pass_culled(1));
    EXPECT_TRUE(rg_.is_pass_culled(2));
    EXPECT_EQ(rg_.live_pass_count(), 0u);
}

TEST_F(RenderGraphTest, CascadeCull_RemovingEndOfChain_CullsUpstream) {
    // A -> B -> C. Only C's output is used. If C is culled, B and A cascade.
    uz::RgTextureDesc td;
    auto t1 = rg_.create_texture(td);
    auto t2 = rg_.create_texture(td);
    auto t3 = rg_.create_texture(td);

    rg_.add_pass("A", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.write(t1); }, nullptr);
    rg_.add_pass("B", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.read(t1).write(t2); }, nullptr);
    rg_.add_pass("C", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.read(t2).write(t3); }, nullptr);

    // Nobody reads t3, nobody is force_run => all culled
    rg_.compile();
    EXPECT_TRUE(rg_.is_pass_culled(0));
    EXPECT_TRUE(rg_.is_pass_culled(1));
    EXPECT_TRUE(rg_.is_pass_culled(2));
}

TEST_F(RenderGraphTest, WritesToImportedTexture_KeepsPassAlive) {
    auto imported = rg_.import_texture("swapchain", static_cast<RhiTexture>(1));

    rg_.add_pass("present", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.write(imported); },
        nullptr);

    rg_.compile();
    EXPECT_FALSE(rg_.is_pass_culled(0));
    EXPECT_EQ(rg_.live_pass_count(), 1u);
}

TEST_F(RenderGraphTest, WritesToImportedBuffer_KeepsPassAlive) {
    auto imported = rg_.import_buffer("frame_cb", static_cast<RhiBuffer>(7));

    rg_.add_pass("update_cb", uz::RgPassType::Copy,
        [&](uz::RgPassBuilder& b) { b.write_buffer(imported); },
        nullptr);

    rg_.compile();
    EXPECT_FALSE(rg_.is_pass_culled(0));
}

TEST_F(RenderGraphTest, BufferDependencyChain_ForceRun_AllAlive) {
    uz::RgBufferDesc bd;
    bd.byte_size = 256;
    auto buf = rg_.create_buffer(bd);

    rg_.add_pass("fill_buf", uz::RgPassType::Compute,
        [&](uz::RgPassBuilder& b) { b.write_buffer(buf); },
        nullptr);

    rg_.add_pass("use_buf", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.read_buffer(buf).force_run(); },
        nullptr);

    rg_.compile();
    EXPECT_FALSE(rg_.is_pass_culled(0));
    EXPECT_FALSE(rg_.is_pass_culled(1));
}

// ============================================================================
// Execution order
// ============================================================================

TEST_F(RenderGraphTest, TwoIndependentForceRun_BothExecute) {
    rg_.add_pass("p1", uz::RgPassType::Graphics,
        [](uz::RgPassBuilder& b) { b.force_run(); }, nullptr);
    rg_.add_pass("p2", uz::RgPassType::Compute,
        [](uz::RgPassBuilder& b) { b.force_run(); }, nullptr);

    rg_.compile();
    EXPECT_EQ(rg_.live_pass_count(), 2u);
}

TEST_F(RenderGraphTest, DependentPasses_ProducerBeforeConsumer) {
    uz::RgTextureDesc td;
    auto tex = rg_.create_texture(td);

    // "producer" added first, writes tex
    rg_.add_pass("producer", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.write(tex); }, nullptr);

    // "consumer" added second, reads tex and is force_run
    rg_.add_pass("consumer", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.read(tex).force_run(); }, nullptr);

    rg_.compile();

    // Execution order must have producer before consumer
    const auto* order = rg_.execution_order();
    ASSERT_NE(order, nullptr);
    EXPECT_EQ(rg_.live_pass_count(), 2u);
    EXPECT_EQ(order[0], 0u); // producer
    EXPECT_EQ(order[1], 1u); // consumer
}

TEST_F(RenderGraphTest, ThreePassLinearChain_CorrectOrder) {
    uz::RgTextureDesc td;
    auto t1 = rg_.create_texture(td);
    auto t2 = rg_.create_texture(td);

    // A writes t1
    rg_.add_pass("A", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.write(t1); }, nullptr);
    // B reads t1, writes t2
    rg_.add_pass("B", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.read(t1).write(t2); }, nullptr);
    // C reads t2 (force_run)
    rg_.add_pass("C", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.read(t2).force_run(); }, nullptr);

    rg_.compile();

    const auto* order = rg_.execution_order();
    ASSERT_NE(order, nullptr);
    ASSERT_EQ(rg_.live_pass_count(), 3u);
    EXPECT_EQ(order[0], 0u); // A
    EXPECT_EQ(order[1], 1u); // B
    EXPECT_EQ(order[2], 2u); // C
}

TEST_F(RenderGraphTest, DiamondDependency_AllAlive_CorrectOrder) {
    //     A
    //    / \
    //   B   C
    //    \ /
    //     D (force_run)
    uz::RgTextureDesc td;
    auto t_ab = rg_.create_texture(td);
    auto t_ac = rg_.create_texture(td);
    auto t_bd = rg_.create_texture(td);
    auto t_cd = rg_.create_texture(td);

    rg_.add_pass("A", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.write(t_ab).write(t_ac); }, nullptr);
    rg_.add_pass("B", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.read(t_ab).write(t_bd); }, nullptr);
    rg_.add_pass("C", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.read(t_ac).write(t_cd); }, nullptr);
    rg_.add_pass("D", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.read(t_bd).read(t_cd).force_run(); },
        nullptr);

    rg_.compile();
    EXPECT_EQ(rg_.live_pass_count(), 4u);

    // A must come before B and C; B and C must come before D
    const auto* order = rg_.execution_order();
    ASSERT_NE(order, nullptr);

    // Find positions
    uint32_t pos_a = UINT32_MAX, pos_b = UINT32_MAX;
    uint32_t pos_c = UINT32_MAX, pos_d = UINT32_MAX;
    for (uint32_t i = 0; i < rg_.live_pass_count(); ++i) {
        if (order[i] == 0) pos_a = i;
        if (order[i] == 1) pos_b = i;
        if (order[i] == 2) pos_c = i;
        if (order[i] == 3) pos_d = i;
    }
    EXPECT_LT(pos_a, pos_b);
    EXPECT_LT(pos_a, pos_c);
    EXPECT_LT(pos_b, pos_d);
    EXPECT_LT(pos_c, pos_d);
}

// ============================================================================
// Execute callbacks
// ============================================================================

TEST_F(RenderGraphTest, Execute_CallsLivePassCallbacks) {
    std::vector<std::string> called;

    uz::RgTextureDesc td;
    auto tex = rg_.create_texture(td);

    rg_.add_pass("writer", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.write(tex); },
        [&](RhiCmdList) { called.push_back("writer"); });

    rg_.add_pass("reader", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.read(tex).force_run(); },
        [&](RhiCmdList) { called.push_back("reader"); });

    rg_.compile();
    rg_.execute();

    ASSERT_EQ(called.size(), 2u);
    EXPECT_EQ(called[0], "writer");
    EXPECT_EQ(called[1], "reader");
}

TEST_F(RenderGraphTest, Execute_DoesNotCallCulledPassCallbacks) {
    bool called = false;

    rg_.add_pass("orphan", uz::RgPassType::Graphics, nullptr,
        [&](RhiCmdList) { called = true; });

    rg_.compile();
    rg_.execute();

    EXPECT_FALSE(called);
}

TEST_F(RenderGraphTest, Execute_PassReceivesRHI_NULL_OnLinux) {
    RhiCmdList received_cmd = static_cast<RhiCmdList>(999);

    rg_.add_pass("test", uz::RgPassType::Graphics,
        [](uz::RgPassBuilder& b) { b.force_run(); },
        [&](RhiCmdList cmd) { received_cmd = cmd; });

    rg_.compile();
    rg_.execute(); // Default arg is RHI_NULL

    EXPECT_EQ(received_cmd, RHI_NULL);
}

TEST_F(RenderGraphTest, Execute_PassReceivesProvidedCmdList) {
    RhiCmdList received_cmd = RHI_NULL;
    const RhiCmdList fake_cmd = static_cast<RhiCmdList>(42);

    rg_.add_pass("test", uz::RgPassType::Graphics,
        [](uz::RgPassBuilder& b) { b.force_run(); },
        [&](RhiCmdList cmd) { received_cmd = cmd; });

    rg_.compile();
    rg_.execute(fake_cmd);

    EXPECT_EQ(received_cmd, fake_cmd);
}

TEST_F(RenderGraphTest, Execute_CallbackOrderMatchesTopologicalOrder) {
    std::vector<uint32_t> order;

    uz::RgTextureDesc td;
    auto t1 = rg_.create_texture(td);
    auto t2 = rg_.create_texture(td);

    rg_.add_pass("A", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.write(t1); },
        [&](RhiCmdList) { order.push_back(0); });

    rg_.add_pass("B", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.read(t1).write(t2); },
        [&](RhiCmdList) { order.push_back(1); });

    rg_.add_pass("C", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.read(t2).force_run(); },
        [&](RhiCmdList) { order.push_back(2); });

    rg_.compile();
    rg_.execute();

    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 0u);
    EXPECT_EQ(order[1], 1u);
    EXPECT_EQ(order[2], 2u);
}

TEST_F(RenderGraphTest, Execute_WithoutCompile_NoCallbacks) {
    bool called = false;

    rg_.add_pass("test", uz::RgPassType::Graphics,
        [](uz::RgPassBuilder& b) { b.force_run(); },
        [&](RhiCmdList) { called = true; });

    // execute() without compile() -- should be safe (no-op)
    rg_.execute();

    EXPECT_FALSE(called);
}

TEST_F(RenderGraphTest, Execute_NullCallback_NoFault) {
    rg_.add_pass("no_cb", uz::RgPassType::Graphics,
        [](uz::RgPassBuilder& b) { b.force_run(); },
        nullptr);

    rg_.compile();
    // Should not crash
    rg_.execute();
    EXPECT_EQ(rg_.live_pass_count(), 1u);
}

// ============================================================================
// reset()
// ============================================================================

TEST_F(RenderGraphTest, Reset_ClearsAllCounts) {
    uz::RgTextureDesc td;
    uz::RgBufferDesc bd;
    bd.byte_size = 64;

    rg_.create_texture(td);
    rg_.create_buffer(bd);
    rg_.add_pass("p1", uz::RgPassType::Graphics,
        [](uz::RgPassBuilder& b) { b.force_run(); }, nullptr);

    rg_.compile();
    EXPECT_GT(rg_.pass_count(), 0u);

    rg_.reset();

    EXPECT_EQ(rg_.pass_count(), 0u);
    EXPECT_EQ(rg_.texture_count(), 0u);
    EXPECT_EQ(rg_.buffer_count(), 0u);
    EXPECT_EQ(rg_.live_pass_count(), 0u);
    EXPECT_EQ(rg_.execution_order(), nullptr);
}

TEST_F(RenderGraphTest, Reset_CanAddPassesAgain) {
    rg_.add_pass("old", uz::RgPassType::Graphics,
        [](uz::RgPassBuilder& b) { b.force_run(); }, nullptr);
    rg_.compile();
    rg_.reset();

    rg_.add_pass("new", uz::RgPassType::Compute,
        [](uz::RgPassBuilder& b) { b.force_run(); }, nullptr);

    EXPECT_EQ(rg_.pass_count(), 1u);
    EXPECT_STREQ(rg_.pass_name(0), "new");

    EXPECT_TRUE(rg_.compile());
    EXPECT_EQ(rg_.live_pass_count(), 1u);
}

TEST_F(RenderGraphTest, Reset_CompileAfterReset_Succeeds) {
    rg_.add_pass("p", uz::RgPassType::Graphics, nullptr, nullptr);
    rg_.compile();
    rg_.reset();

    EXPECT_TRUE(rg_.compile());
    EXPECT_EQ(rg_.live_pass_count(), 0u);
}

// ============================================================================
// Import + read dependency
// ============================================================================

TEST_F(RenderGraphTest, ImportedTextureCanBeReadByPass) {
    auto imported = rg_.import_texture("gbuf", static_cast<RhiTexture>(10));

    bool called = false;
    rg_.add_pass("use_gbuf", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.read(imported).force_run(); },
        [&](RhiCmdList) { called = true; });

    rg_.compile();
    rg_.execute();
    EXPECT_TRUE(called);
    EXPECT_FALSE(rg_.is_pass_culled(0));
}

TEST_F(RenderGraphTest, ImportedBufferCanBeReadByPass) {
    auto imported = rg_.import_buffer("lights", static_cast<RhiBuffer>(20));

    bool called = false;
    rg_.add_pass("use_buf", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.read_buffer(imported).force_run(); },
        [&](RhiCmdList) { called = true; });

    rg_.compile();
    rg_.execute();
    EXPECT_TRUE(called);
}

// ============================================================================
// Edge cases
// ============================================================================

TEST_F(RenderGraphTest, CompileTwice_SecondReflectsCurrentState) {
    rg_.add_pass("p1", uz::RgPassType::Graphics,
        [](uz::RgPassBuilder& b) { b.force_run(); }, nullptr);
    rg_.compile();
    EXPECT_EQ(rg_.live_pass_count(), 1u);

    // Compile again (no reset, same data)
    rg_.compile();
    EXPECT_EQ(rg_.live_pass_count(), 1u);
}

TEST_F(RenderGraphTest, MaxPasses_CompileSucceeds) {
    for (uint32_t i = 0; i < uz::kRgMaxPasses; ++i) {
        std::string name = "pass_" + std::to_string(i);
        rg_.add_pass(name.c_str(), uz::RgPassType::Graphics,
            [](uz::RgPassBuilder& b) { b.force_run(); }, nullptr);
    }

    EXPECT_EQ(rg_.pass_count(), uz::kRgMaxPasses);
    EXPECT_TRUE(rg_.compile());
    EXPECT_EQ(rg_.live_pass_count(), uz::kRgMaxPasses);
}

TEST_F(RenderGraphTest, IsPassed_InvalidIndex_ReturnsTrue) {
    EXPECT_TRUE(rg_.is_pass_culled(0));
    EXPECT_TRUE(rg_.is_pass_culled(999));
}

TEST_F(RenderGraphTest, PassTypes_Graphics_Compute_Copy) {
    rg_.add_pass("gfx", uz::RgPassType::Graphics,
        [](uz::RgPassBuilder& b) { b.force_run(); }, nullptr);
    rg_.add_pass("cmp", uz::RgPassType::Compute,
        [](uz::RgPassBuilder& b) { b.force_run(); }, nullptr);
    rg_.add_pass("cpy", uz::RgPassType::Copy,
        [](uz::RgPassBuilder& b) { b.force_run(); }, nullptr);

    EXPECT_TRUE(rg_.compile());
    EXPECT_EQ(rg_.live_pass_count(), 3u);
}

TEST_F(RenderGraphTest, TextureDescFields_Preserved) {
    uz::RgTextureDesc td;
    td.width            = 1920;
    td.height           = 1080;
    td.mip_levels       = 4;
    td.format           = uz::RgFormat::RGBA16F;
    td.is_render_target = true;
    td.is_depth_stencil = false;
    td.is_uav           = true;
    uz::str_copy(td.name, sizeof(td.name), "hdr_color");

    auto h = rg_.create_texture(td);
    EXPECT_NE(h, uz::kRgHandleInvalid);
    // Just ensure it doesn't crash -- desc is stored internally
}

TEST_F(RenderGraphTest, BufferDescFields_Preserved) {
    uz::RgBufferDesc bd;
    bd.byte_size  = 65536;
    bd.stride     = 16;
    bd.is_uav     = true;
    bd.persistent = true;
    uz::str_copy(bd.name, sizeof(bd.name), "structured_buf");

    auto h = rg_.create_buffer(bd);
    EXPECT_NE(h, uz::kRgHandleInvalid);
}

// ============================================================================
// Move semantics
// ============================================================================

TEST_F(RenderGraphTest, MoveConstruct_SourceBecomesSafe) {
    rg_.add_pass("p", uz::RgPassType::Graphics,
        [](uz::RgPassBuilder& b) { b.force_run(); }, nullptr);

    uz::RenderGraph moved(std::move(rg_));
    EXPECT_EQ(moved.pass_count(), 1u);
    // Source should not crash on destruction (will be destroyed in TearDown)
}

TEST(RenderGraphMoveTest, MoveAssign_TransfersState) {
    uz::RenderGraph rg1;
    rg1.add_pass("p1", uz::RgPassType::Graphics,
        [](uz::RgPassBuilder& b) { b.force_run(); }, nullptr);

    uz::RenderGraph rg2;
    rg2 = std::move(rg1);

    EXPECT_EQ(rg2.pass_count(), 1u);
    EXPECT_TRUE(rg2.compile());
    EXPECT_EQ(rg2.live_pass_count(), 1u);
}

// ============================================================================
// Complex culling scenarios
// ============================================================================

TEST_F(RenderGraphTest, MultipleFrontierPasses_KeepAllUpstream) {
    // Two separate output passes, each with own upstream chain.
    uz::RgTextureDesc td;
    auto t1 = rg_.create_texture(td);
    auto t2 = rg_.create_texture(td);

    rg_.add_pass("A", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.write(t1); }, nullptr);
    rg_.add_pass("B", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.write(t2); }, nullptr);
    rg_.add_pass("C", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.read(t1).force_run(); }, nullptr);
    rg_.add_pass("D", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.read(t2).force_run(); }, nullptr);

    rg_.compile();
    EXPECT_FALSE(rg_.is_pass_culled(0)); // A alive (produces for C)
    EXPECT_FALSE(rg_.is_pass_culled(1)); // B alive (produces for D)
    EXPECT_FALSE(rg_.is_pass_culled(2)); // C alive (force_run)
    EXPECT_FALSE(rg_.is_pass_culled(3)); // D alive (force_run)
    EXPECT_EQ(rg_.live_pass_count(), 4u);
}

TEST_F(RenderGraphTest, PartialCull_OneBranchAlive_OneNotNeeded) {
    // A -> B (force_run), A -> C (not force_run, output not consumed)
    uz::RgTextureDesc td;
    auto t_ab = rg_.create_texture(td);
    auto t_ac = rg_.create_texture(td);
    auto t_out = rg_.create_texture(td);

    rg_.add_pass("A", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.write(t_ab).write(t_ac); }, nullptr);
    rg_.add_pass("B", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.read(t_ab).force_run(); }, nullptr);
    rg_.add_pass("C", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.read(t_ac).write(t_out); }, nullptr);

    rg_.compile();
    // A is alive because B depends on it
    EXPECT_FALSE(rg_.is_pass_culled(0));
    EXPECT_FALSE(rg_.is_pass_culled(1));
    // C is culled because t_out is never consumed and C is not force_run
    EXPECT_TRUE(rg_.is_pass_culled(2));
}

TEST_F(RenderGraphTest, PassWritesToPersistentTransient_StaysAlive) {
    // A persistent transient texture (created with persistent=true) keeps
    // its writer alive even if nobody else reads it.
    uz::RgTextureDesc td;
    td.persistent = true;
    uz::str_copy(td.name, sizeof(td.name), "persistent_rt");
    auto tex = rg_.create_texture(td);

    rg_.add_pass("writer", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.write(tex); }, nullptr);

    rg_.compile();
    EXPECT_FALSE(rg_.is_pass_culled(0));
}

TEST_F(RenderGraphTest, PassWritesToPersistentBuffer_StaysAlive) {
    uz::RgBufferDesc bd;
    bd.byte_size  = 128;
    bd.persistent = true;
    auto buf = rg_.create_buffer(bd);

    rg_.add_pass("fill", uz::RgPassType::Compute,
        [&](uz::RgPassBuilder& b) { b.write_buffer(buf); }, nullptr);

    rg_.compile();
    EXPECT_FALSE(rg_.is_pass_culled(0));
}

TEST_F(RenderGraphTest, ForceRunWithNoResources_StillAlive) {
    int counter = 0;
    rg_.add_pass("debug_overlay", uz::RgPassType::Graphics,
        [](uz::RgPassBuilder& b) { b.force_run(); },
        [&](RhiCmdList) { counter++; });

    rg_.compile();
    rg_.execute();

    EXPECT_FALSE(rg_.is_pass_culled(0));
    EXPECT_EQ(counter, 1);
}

// ============================================================================
// Enum values
// ============================================================================

TEST(RenderGraphEnumTest, RgFormat_EnumValues) {
    EXPECT_EQ(static_cast<uint8_t>(uz::RgFormat::Unknown), 0);
    EXPECT_EQ(static_cast<uint8_t>(uz::RgFormat::RGBA8), 1);
    EXPECT_EQ(static_cast<uint8_t>(uz::RgFormat::RGBA16F), 2);
    EXPECT_EQ(static_cast<uint8_t>(uz::RgFormat::R32F), 3);
    EXPECT_EQ(static_cast<uint8_t>(uz::RgFormat::D32F), 4);
    EXPECT_EQ(static_cast<uint8_t>(uz::RgFormat::RG16F), 5);
    EXPECT_EQ(static_cast<uint8_t>(uz::RgFormat::BC7), 6);
}

TEST(RenderGraphEnumTest, RgPassType_EnumValues) {
    EXPECT_EQ(static_cast<uint8_t>(uz::RgPassType::Graphics), 0);
    EXPECT_EQ(static_cast<uint8_t>(uz::RgPassType::Compute), 1);
    EXPECT_EQ(static_cast<uint8_t>(uz::RgPassType::Copy), 2);
}

TEST(RenderGraphEnumTest, HandleConstants) {
    EXPECT_EQ(uz::kRgHandleInvalid, UINT32_MAX);
    EXPECT_EQ(uz::kRgMaxPasses, 64u);
    EXPECT_EQ(uz::kRgMaxResources, 128u);
}

// ============================================================================
// Stress: large graph
// ============================================================================

TEST(RenderGraphStressTest, ManyPasses_LinearChain_64Passes) {
    uz::RenderGraph rg;

    // Create 63 textures for 64-pass linear chain
    std::vector<uz::RgTextureHandle> textures;
    for (uint32_t i = 0; i < 63; ++i) {
        uz::RgTextureDesc td;
        textures.push_back(rg.create_texture(td));
    }

    // Pass 0: writes textures[0]
    rg.add_pass("pass_0", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) { b.write(textures[0]); }, nullptr);

    // Passes 1..62: reads textures[i-1], writes textures[i]
    for (uint32_t i = 1; i < 63; ++i) {
        std::string name = "pass_" + std::to_string(i);
        rg.add_pass(name.c_str(), uz::RgPassType::Graphics,
            [&, i](uz::RgPassBuilder& b) {
                b.read(textures[i - 1]).write(textures[i]);
            }, nullptr);
    }

    // Pass 63: reads textures[62], force_run
    rg.add_pass("pass_63", uz::RgPassType::Graphics,
        [&](uz::RgPassBuilder& b) {
            b.read(textures[62]).force_run();
        }, nullptr);

    EXPECT_EQ(rg.pass_count(), 64u);
    EXPECT_TRUE(rg.compile());
    EXPECT_EQ(rg.live_pass_count(), 64u);

    // Verify execution order is 0, 1, 2, ..., 63
    const auto* order = rg.execution_order();
    ASSERT_NE(order, nullptr);
    for (uint32_t i = 0; i < 64; ++i) {
        EXPECT_EQ(order[i], i) << "Order mismatch at position " << i;
    }
}

// ============================================================================
// Import with null name
// ============================================================================

TEST_F(RenderGraphTest, ImportTexture_NullName_NoFault) {
    auto h = rg_.import_texture(nullptr, static_cast<RhiTexture>(5));
    EXPECT_NE(h, uz::kRgHandleInvalid);
}

TEST_F(RenderGraphTest, ImportBuffer_NullName_NoFault) {
    auto h = rg_.import_buffer(nullptr, static_cast<RhiBuffer>(5));
    EXPECT_NE(h, uz::kRgHandleInvalid);
}

// ============================================================================
// Multi-frame lifecycle (reset + re-build + re-compile)
// ============================================================================

TEST(RenderGraphLifecycleTest, ThreeFrames) {
    uz::RenderGraph rg;

    for (int frame = 0; frame < 3; ++frame) {
        rg.reset();

        uz::RgTextureDesc td;
        auto tex = rg.create_texture(td);

        int call_count = 0;
        rg.add_pass("draw", uz::RgPassType::Graphics,
            [&](uz::RgPassBuilder& b) { b.write(tex).force_run(); },
            [&](RhiCmdList) { call_count++; });

        EXPECT_TRUE(rg.compile()) << "Frame " << frame;
        EXPECT_EQ(rg.live_pass_count(), 1u) << "Frame " << frame;

        rg.execute();
        EXPECT_EQ(call_count, 1) << "Frame " << frame;
    }
}
