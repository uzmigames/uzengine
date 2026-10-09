/// Unit tests for the render-graph resource-state registry (phase0_render-graph-state-registry).
///
/// `GenerationalMockDevice` reuses a destroyed slot with a new generation, as the real resource pools do,
/// and returns each texture's descriptor. `D3D12TruthCommandList` keeps the state every watched resource
/// is really in (creation state first, then each barrier's stateAfter; buffers, and textures implicitly
/// promoted to a read-only state, back to Common at every submit: D3D12's decay) and records the errors
/// the D3D12 debug layer would report: a barrier whose stateBefore disagrees with the actual state, and a
/// pass that accesses a resource in a state it is not in and cannot be implicitly promoted to (every pass
/// of these tests reports its declared accesses). No GPU is needed.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "render_graph_builder.h"
#include "render_graph_resources.h"
#include "render_graph_states.h"
#include "../../rhi/rhi.h"

namespace {

using uz::RHIResourceState;
using uz::rg::RgStateRegistry;

// ─────────────────────────────────────────────────────────────────────────────
// GenerationalMockDevice
// ─────────────────────────────────────────────────────────────────────────────

class GenerationalMockDevice final : public uz::IRHIDevice {
public:
    uz::RHITextureHandle CreateTexture(const uz::RHITextureDesc& d) override {
        uint32_t index;
        if (!freeTextures_.empty()) {
            index = freeTextures_.back();
            freeTextures_.pop_back();
        } else {
            index = static_cast<uint32_t>(textures_.size());
            textures_.push_back({});
        }
        auto& slot = textures_[index];
        slot.generation += 1;
        slot.alive = true;
        slot.desc = d;
        return {index, slot.generation};
    }

    void DestroyTexture(uz::RHITextureHandle h) override {
        if (h.index < textures_.size() && textures_[h.index].generation == h.generation) {
            textures_[h.index].alive = false;
            freeTextures_.push_back(h.index);
        }
    }

    uz::RHIBufferHandle CreateBuffer(const uz::RHIBufferDesc&) override {
        uint32_t index;
        if (!freeBuffers_.empty()) {
            index = freeBuffers_.back();
            freeBuffers_.pop_back();
        } else {
            index = static_cast<uint32_t>(bufferGenerations_.size());
            bufferGenerations_.push_back(0);
        }
        bufferGenerations_[index] += 1;
        return {index, bufferGenerations_[index]};
    }

    void DestroyBuffer(uz::RHIBufferHandle h) override {
        if (h.index < bufferGenerations_.size() && bufferGenerations_[h.index] == h.generation) {
            freeBuffers_.push_back(h.index);
        }
    }

    uz::RHITextureDesc GetTextureDesc(uz::RHITextureHandle h) const override {
        if (h.index < textures_.size() && textures_[h.index].generation == h.generation && textures_[h.index].alive) {
            return textures_[h.index].desc;
        }
        return {};
    }

    // ── Unused by these tests ────────────────────────────────────────────
    void* MapBuffer(uz::RHIBufferHandle) override { return nullptr; }
    void UnmapBuffer(uz::RHIBufferHandle) override {}
    uint32_t GetBufferSRVIndex(uz::RHIBufferHandle) const override { return UINT32_MAX; }
    uint32_t GetBufferUAVIndex(uz::RHIBufferHandle) const override { return UINT32_MAX; }
    uint64_t GetBufferGPUAddress(uz::RHIBufferHandle) const override { return 0; }
    uint32_t GetTextureSRVIndex(uz::RHITextureHandle) const override { return UINT32_MAX; }
    uint32_t GetTextureUAVIndex(uz::RHITextureHandle) const override { return UINT32_MAX; }
    uz::RHIShaderHandle CreateShader(const uz::RHIShaderDesc&) override { return {0, 0}; }
    void DestroyShader(uz::RHIShaderHandle) override {}
    uz::RHIPipelineHandle CreateGraphicsPipeline(const uz::RHIGraphicsPipelineDesc&) override { return {0, 0}; }
    uz::RHIPipelineHandle CreateComputePipeline(const uz::RHIComputePipelineDesc&) override { return {0, 0}; }
    void DestroyPipeline(uz::RHIPipelineHandle) override {}
    std::vector<uint8_t> GetPipelineCachedBlob(uz::RHIPipelineHandle) override { return {}; }
    uz::RHISamplerHandle CreateSampler(const uz::RHISamplerDesc&) override { return {0, 0}; }
    void DestroySampler(uz::RHISamplerHandle) override {}
    uint32_t GetSamplerIndex(uz::RHISamplerHandle) const override { return UINT32_MAX; }
    uz::IRHISwapChain* CreateSwapChain(const uz::RHISwapChainDesc&) override { return nullptr; }
    void DestroySwapChain(uz::IRHISwapChain*) override {}
    uz::IRHIFence* CreateFence() override { return nullptr; }
    void DestroyFence(uz::IRHIFence*) override {}
    uz::IRHICommandList* CreateCommandList(uz::RHIQueueType) override { return nullptr; }
    void DestroyCommandList(uz::IRHICommandList*) override {}
    void WaitIdle() override {}
    const char* GetAdapterName() const override { return "GenerationalMockDevice"; }
    const char* GetFeatureLevel() const override { return "Mock"; }
    uz::RHIMemoryBudget GetMemoryBudget() const override { return {}; }
    uz::ShaderModel GetShaderModel() const override { return static_cast<uz::ShaderModel>(0); }
    uz::RHIBackend GetBackend() const override { return static_cast<uz::RHIBackend>(0); }

private:
    struct TextureSlot {
        uint32_t generation = 0;
        bool alive = false;
        uz::RHITextureDesc desc{};
    };
    std::vector<TextureSlot> textures_;
    std::vector<uint32_t> freeTextures_;
    std::vector<uint32_t> bufferGenerations_;
    std::vector<uint32_t> freeBuffers_;
};

// ─────────────────────────────────────────────────────────────────────────────
// D3D12TruthCommandList
// ─────────────────────────────────────────────────────────────────────────────

class D3D12TruthCommandList final : public uz::IRHICommandList {
public:
    struct Mismatch {
        std::string name;
        RHIResourceState claimedBefore;
        RHIResourceState actual;
        RHIResourceState after;
    };

    explicit D3D12TruthCommandList(const GenerationalMockDevice* device, bool countsSubmits = true)
        : device_(device), countsSubmits_(countsSubmits) {}

    /// Only watched resources are checked (graph transients start without a barrier by design).
    void Watch(uz::RHITextureHandle h, std::string name) { textureNames_[RgStateRegistry::Key(h)] = std::move(name); }
    void Watch(uz::RHIBufferHandle h, std::string name) { bufferNames_[RgStateRegistry::Key(h)] = std::move(name); }

    RHIResourceState Actual(uz::RHITextureHandle h) const {
        const auto it = textureStates_.find(RgStateRegistry::Key(h));
        return it != textureStates_.end() ? it->second : RgStateRegistry::CreationState(device_->GetTextureDesc(h));
    }
    RHIResourceState Actual(uz::RHIBufferHandle h) const {
        const auto it = bufferStates_.find(RgStateRegistry::Key(h));
        return it != bufferStates_.end() ? it->second : RHIResourceState::Common;
    }

    void ResourceBarrier(const uz::RHIBarrierDesc* descs, uint32_t count) override {
        for (uint32_t i = 0; i < count; ++i) {
            const auto& d = descs[i];
            barriers.push_back(d);
            const bool uav = d.stateBefore == RHIResourceState::UnorderedAccess
                          && d.stateAfter == RHIResourceState::UnorderedAccess;
            if (d.texture.IsValid()) {
                const uint64_t key = RgStateRegistry::Key(d.texture);
                const auto name = textureNames_.find(key);
                if (name == textureNames_.end()) continue;
                const RHIResourceState actual = Actual(d.texture);
                if (!uav && !RgStateRegistry::SameState(actual, d.stateBefore)) mismatches.push_back({name->second, d.stateBefore, actual, d.stateAfter});
                if (!uav) {
                    textureStates_[key] = d.stateAfter;
                    promoted_.erase(key);   // an explicit state now: no decay
                }
            } else if (d.buffer.IsValid()) {
                const uint64_t key = RgStateRegistry::Key(d.buffer);
                const auto name = bufferNames_.find(key);
                if (name == bufferNames_.end()) continue;
                const RHIResourceState actual = Actual(d.buffer);
                if (!uav && !RgStateRegistry::SameState(actual, d.stateBefore)) mismatches.push_back({name->second, d.stateBefore, actual, d.stateAfter});
                if (!uav) bufferStates_[key] = d.stateAfter;
            }
        }
    }

    /// A pass accesses `h` in `required`. A texture in Common is promoted implicitly to a read-only or
    /// copy state (not to RenderTarget, UnorderedAccess or depth), a buffer in Common to any state;
    /// anything else in the wrong state is an error.
    void Access(uz::RHITextureHandle h, RHIResourceState required) {
        const uint64_t key = RgStateRegistry::Key(h);
        const auto name = textureNames_.find(key);
        if (name == textureNames_.end()) return;
        const RHIResourceState actual = Actual(h);
        if (actual == required) return;
        const bool promotable = actual == RHIResourceState::Common
            && (required == RHIResourceState::ShaderResource || required == RHIResourceState::CopySrc
                || required == RHIResourceState::CopyDst);
        if (promotable) {
            textureStates_[key] = required;
            promoted_.insert(key);
            return;
        }
        mismatches.push_back({name->second, required, actual, required});
    }
    void Access(uz::RHIBufferHandle h, RHIResourceState required) {
        const uint64_t key = RgStateRegistry::Key(h);
        const auto name = bufferNames_.find(key);
        if (name == bufferNames_.end()) return;
        const RHIResourceState actual = Actual(h);
        if (actual == required) return;
        if (actual == RHIResourceState::Common) {
            bufferStates_[key] = required;
            return;
        }
        mismatches.push_back({name->second, required, actual, required});
    }

    /// A submit: every buffer, and every texture promoted implicitly since the last one, decays to Common.
    void Execute() override {
        ++submits_;
        bufferStates_.clear();
        for (uint64_t key : promoted_) textureStates_.erase(key);
        promoted_.clear();
    }
    uint64_t GetSubmitCount() const override { return countsSubmits_ ? submits_ : 0; }

    size_t MismatchesFor(const std::string& name) const {
        size_t n = 0;
        for (const auto& m : mismatches) n += m.name == name ? 1u : 0u;
        return n;
    }

    std::vector<uz::RHIBarrierDesc> barriers;
    std::vector<Mismatch> mismatches;

    // ── No-op stubs ──────────────────────────────────────────────────────
    void Begin() override {}
    void End() override {}
    void SetPipeline(uz::RHIPipelineHandle) override {}
    void SetVertexBuffer(uint32_t, uz::RHIBufferHandle, uint32_t) override {}
    void SetIndexBuffer(uz::RHIBufferHandle, uz::RHIIndexFormat, uint32_t) override {}
    void SetViewport(const uz::RHIViewport&) override {}
    void SetScissor(const uz::RHIScissorRect&) override {}
    void SetDescriptorIndex(uint32_t, uint32_t) override {}
    void SetRootConstants(uint32_t, const uint32_t*, uint32_t, uint32_t) override {}
    void SetRootCBV(uint32_t, uint64_t) override {}
    void SetRootCBV(uint32_t, uz::RHIBufferHandle, uint64_t, const void*, uint32_t) override {}
    void SetDescriptorTable(uint32_t, uint32_t) override {}
    void DrawIndexed(uint32_t, uint32_t, uint32_t, int32_t, uint32_t) override {}
    void Draw(uint32_t, uint32_t, uint32_t, uint32_t) override {}
    void Dispatch(uint32_t, uint32_t, uint32_t) override {}
    void DrawIndexedIndirect(uz::RHIBufferHandle, uint64_t, uint32_t, uint32_t) override {}
    void DispatchIndirect(uz::RHIBufferHandle, uint64_t) override {}
    void ClearRenderTarget(uz::RHITextureHandle, const float[4]) override {}
    void ClearDepthStencil(uz::RHITextureHandle, float, uint8_t) override {}
    void CopyBuffer(uz::RHIBufferHandle, uint64_t, uz::RHIBufferHandle, uint64_t, uint64_t) override {}
    void CopyTexture(uz::RHITextureHandle, uz::RHITextureHandle) override {}
    void CopyBufferToTexture(uz::RHIBufferHandle, uint64_t, uint32_t,
                             uz::RHITextureHandle, uint32_t, uint32_t) override {}
    void CopyTextureToBuffer(uz::RHITextureHandle, uint32_t,
                             uz::RHIBufferHandle, uint64_t, uint32_t, uint32_t) override {}
    void BeginRenderPass(const uz::RHIRenderPassDesc&) override {}
    void EndRenderPass() override {}

private:
    const GenerationalMockDevice* device_;
    bool countsSubmits_;
    uint64_t submits_ = 0;
    std::unordered_map<uint64_t, std::string> textureNames_, bufferNames_;
    std::unordered_map<uint64_t, RHIResourceState> textureStates_, bufferStates_;
    std::unordered_set<uint64_t> promoted_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

struct Acc {
    uz::rg::RgResourceId id;
    RHIResourceState state;
    bool write;
    bool buffer = false;
};

Acc R(uz::rg::RgResourceId id, RHIResourceState s) { return {id, s, false}; }
Acc W(uz::rg::RgResourceId id, RHIResourceState s) { return {id, s, true}; }
Acc RB(uz::rg::RgResourceId id, RHIResourceState s) { return {id, s, false, true}; }
Acc WB(uz::rg::RgResourceId id, RHIResourceState s) { return {id, s, true, true}; }

/// A pass declaring `accesses`; when it runs on a D3D12TruthCommandList it reports them to it.
void Pass(uz::rg::RgBuilder& rg, const char* name, std::vector<Acc> accesses) {
    rg.AddPass(name, [accesses](uz::rg::RgPassBuilder& pb) {
        for (const auto& a : accesses) {
            if (a.write) (void)pb.Write(a.id, a.state);
            else         (void)pb.Read(a.id, a.state);
        }
        pb.SetExecutionCallback([accesses](uz::IRHICommandList* c, const uz::rg::RgPassResources& res) {
            auto* truth = dynamic_cast<D3D12TruthCommandList*>(c);
            if (!truth) return;
            for (const auto& a : accesses) {
                if (a.buffer) truth->Access(res.GetBuffer(a.id), a.state);
                else          truth->Access(res.GetTexture(a.id), a.state);
            }
        });
    });
}

uz::RHITextureDesc ColorDesc() {
    uz::RHITextureDesc d{};
    d.width = 64; d.height = 64;
    d.format = uz::RHIFormat::RGBA16Float;
    d.usage = uz::RHITextureUsage::RenderTarget | uz::RHITextureUsage::ShaderResource;
    return d;
}

uz::RHITextureDesc DepthDesc() {
    uz::RHITextureDesc d{};
    d.width = 64; d.height = 64;
    d.format = uz::RHIFormat::D32Float;
    d.usage = uz::RHITextureUsage::DepthStencil | uz::RHITextureUsage::ShaderResource;
    return d;
}

uz::RHITextureDesc VolumeDesc() {
    uz::RHITextureDesc d{};
    d.width = 16; d.height = 16; d.depthOrArraySize = 16; d.is3D = true;
    d.format = uz::RHIFormat::RGBA16Float;
    d.usage = uz::RHITextureUsage::UnorderedAccess | uz::RHITextureUsage::ShaderResource;
    return d;
}

bool Has(const std::vector<uz::RHIBarrierDesc>& barriers, uz::RHITextureHandle t, RHIResourceState before,
         RHIResourceState after) {
    for (const auto& b : barriers) {
        if (b.texture == t && b.stateBefore == before && b.stateAfter == after) return true;
    }
    return false;
}

bool Has(const std::vector<uz::RHIBarrierDesc>& barriers, uz::RHIBufferHandle t, RHIResourceState before,
         RHIResourceState after) {
    for (const auto& b : barriers) {
        if (b.buffer == t && b.stateBefore == before && b.stateAfter == after) return true;
    }
    return false;
}

class RenderGraphStates : public ::testing::Test {
protected:
    void SetUp() override {
        device = std::make_unique<GenerationalMockDevice>();
        pool = std::make_unique<uz::rg::RgResourcePool>(device.get());
        states = std::make_unique<RgStateRegistry>(device.get());
        cmd = std::make_unique<D3D12TruthCommandList>(device.get());
        states->BindCommandList(cmd.get());
    }
    void TearDown() override { pool->Trim(); }

    uz::RHITextureHandle Texture(const uz::RHITextureDesc& d, const char* name) {
        auto h = device->CreateTexture(d);
        cmd->Watch(h, name);
        return h;
    }

    uz::RHIBufferHandle Buffer(const char* name) {
        uz::RHIBufferDesc d{};
        d.sizeBytes = 256;
        d.usage = uz::RHIBufferUsage::UnorderedAccess;
        auto h = device->CreateBuffer(d);
        cmd->Watch(h, name);
        return h;
    }

    uz::rg::RgBuilder Graph() { return uz::rg::RgBuilder(device.get(), pool.get(), states.get()); }

    std::unique_ptr<GenerationalMockDevice> device;
    std::unique_ptr<uz::rg::RgResourcePool> pool;
    std::unique_ptr<RgStateRegistry> states;
    std::unique_ptr<D3D12TruthCommandList> cmd;
};

// ─────────────────────────────────────────────────────────────────────────────
// 1-10: the registry's rules
// ─────────────────────────────────────────────────────────────────────────────

/// 1. The second frame's import starts from the first frame's end state.
TEST_F(RenderGraphStates, SecondFrameStartsFromFirstFrameEndState) {
    const auto tex = Texture(ColorDesc(), "hdr");
    {
        auto rg = Graph();
        const auto id = rg.ImportTrackedTexture("HDR", tex);
        Pass(rg, "Write", {W(id, RHIResourceState::RenderTarget)});
        Pass(rg, "Read", {R(id, RHIResourceState::ShaderResource)});
        rg.Compile();
        rg.Execute(cmd.get());
    }
    EXPECT_EQ(states->Get(tex), RHIResourceState::ShaderResource);
    cmd->barriers.clear();
    {
        auto rg = Graph();
        const auto id = rg.ImportTrackedTexture("HDR", tex);
        Pass(rg, "Write", {W(id, RHIResourceState::RenderTarget)});
        rg.Compile();
        rg.Execute(cmd.get());
    }
    EXPECT_TRUE(Has(cmd->barriers, tex, RHIResourceState::ShaderResource, RHIResourceState::RenderTarget));
    EXPECT_TRUE(cmd->mismatches.empty());
}

/// 2. Fresh textures start in their creation state: Common for colour, DepthStencilWrite for depth.
TEST_F(RenderGraphStates, FreshTexturesStartInTheirCreationState) {
    const auto color = Texture(ColorDesc(), "color");
    const auto depth = Texture(DepthDesc(), "depth");
    auto rg = Graph();
    const auto c = rg.ImportTrackedTexture("Color", color);
    const auto d = rg.ImportTrackedTexture("Depth", depth);
    Pass(rg, "GBuffer", {W(c, RHIResourceState::RenderTarget), W(d, RHIResourceState::DepthStencilWrite)});
    Pass(rg, "Lighting", {R(c, RHIResourceState::ShaderResource), R(d, RHIResourceState::ShaderResource)});
    rg.Compile();
    rg.Execute(cmd.get());
    EXPECT_TRUE(Has(cmd->barriers, color, RHIResourceState::Common, RHIResourceState::RenderTarget));
    EXPECT_FALSE(Has(cmd->barriers, depth, RHIResourceState::Common, RHIResourceState::DepthStencilWrite));
    EXPECT_TRUE(Has(cmd->barriers, depth, RHIResourceState::DepthStencilWrite, RHIResourceState::ShaderResource));
    EXPECT_TRUE(cmd->mismatches.empty());
}

/// 3. A recreated handle (same slot, new generation) starts in its creation state, with no reset hook.
TEST_F(RenderGraphStates, RecreatedHandleStartsInItsCreationState) {
    const auto first = Texture(ColorDesc(), "hdr-v1");
    {
        auto rg = Graph();
        const auto id = rg.ImportTrackedTexture("HDR", first);
        Pass(rg, "Write", {W(id, RHIResourceState::RenderTarget)});
        Pass(rg, "Read", {R(id, RHIResourceState::ShaderResource)});
        rg.Compile();
        rg.Execute(cmd.get());
    }
    device->DestroyTexture(first);
    const auto second = Texture(ColorDesc(), "hdr-v2");
    ASSERT_EQ(second.index, first.index);
    ASSERT_NE(second.generation, first.generation);
    cmd->barriers.clear();
    {
        auto rg = Graph();
        const auto id = rg.ImportTrackedTexture("HDR", second);
        Pass(rg, "Write", {W(id, RHIResourceState::RenderTarget)});
        rg.Compile();
        rg.Execute(cmd.get());
    }
    EXPECT_TRUE(Has(cmd->barriers, second, RHIResourceState::Common, RHIResourceState::RenderTarget));
    EXPECT_TRUE(cmd->mismatches.empty());
}

/// 4. The same handle imported twice yields one id and one barrier chain.
TEST_F(RenderGraphStates, DoubleTrackedImportYieldsOneId) {
    const auto tex = Texture(VolumeDesc(), "ap-volume");
    auto rg = Graph();
    const auto a = rg.ImportTrackedTexture("APVolume", tex);
    const auto b = rg.ImportTrackedTexture("APVolume (sky)", tex);
    EXPECT_EQ(a.index, b.index);
    Pass(rg, "Compute", {W(a, RHIResourceState::UnorderedAccess)});
    Pass(rg, "Apply", {R(b, RHIResourceState::ShaderResource)});
    rg.Compile();
    rg.Execute(cmd.get());
    EXPECT_TRUE(Has(cmd->barriers, tex, RHIResourceState::Common, RHIResourceState::UnorderedAccess));
    EXPECT_TRUE(Has(cmd->barriers, tex, RHIResourceState::UnorderedAccess, RHIResourceState::ShaderResource));
    EXPECT_TRUE(cmd->mismatches.empty());
}

/// 5. Buffers import as Common after every submit (implicit decay), counted by the list or announced.
TEST_F(RenderGraphStates, BuffersImportAsCommonAfterEverySubmit) {
    const auto buf = Buffer("mesh-args");
    for (int frame = 0; frame < 3; ++frame) {
        cmd->barriers.clear();
        auto rg = Graph();
        const auto id = rg.ImportTrackedBuffer("MeshArgs", buf);
        Pass(rg, "Simulate", {WB(id, RHIResourceState::UnorderedAccess)});
        Pass(rg, "Draw", {RB(id, RHIResourceState::IndirectArgument)});
        rg.Compile();
        rg.Execute(cmd.get());
        EXPECT_TRUE(Has(cmd->barriers, buf, RHIResourceState::Common, RHIResourceState::UnorderedAccess)) << frame;
        EXPECT_EQ(states->Get(buf), RHIResourceState::IndirectArgument);
        cmd->Execute();   // the frame's submit
        EXPECT_EQ(states->Get(buf), RHIResourceState::Common);
    }
    EXPECT_TRUE(cmd->mismatches.empty());

    // A list that does not count its submits: the registry relies on OnQueueSubmit.
    D3D12TruthCommandList plain(device.get(), /*countsSubmits=*/false);
    plain.Watch(buf, "mesh-args");
    states->BindCommandList(&plain);
    states->Transition(&plain, buf, RHIResourceState::UnorderedAccess);
    plain.Execute();
    EXPECT_EQ(states->Get(buf), RHIResourceState::UnorderedAccess);   // not told yet
    states->OnQueueSubmit();
    EXPECT_EQ(states->Get(buf), RHIResourceState::Common);
    EXPECT_TRUE(plain.mismatches.empty());
}

/// 6. A registry transition recorded after Import and before Compile is honoured.
TEST_F(RenderGraphStates, TransitionBetweenImportAndCompileIsHonoured) {
    const auto tex = Texture(ColorDesc(), "sky-cube");
    auto rg = Graph();
    const auto id = rg.ImportTrackedTexture("SkyCubemap", tex);
    states->Transition(cmd.get(), tex, RHIResourceState::ShaderResource);   // e.g. a pre-graph bake
    Pass(rg, "Capture", {W(id, RHIResourceState::RenderTarget)});
    rg.Compile();
    rg.Execute(cmd.get());
    EXPECT_TRUE(Has(cmd->barriers, tex, RHIResourceState::Common, RHIResourceState::ShaderResource));
    EXPECT_TRUE(Has(cmd->barriers, tex, RHIResourceState::ShaderResource, RHIResourceState::RenderTarget));
    EXPECT_TRUE(cmd->mismatches.empty());
}

/// 7. A frame that does not import a resource keeps its state.
TEST_F(RenderGraphStates, FrameWithoutTheImportKeepsTheState) {
    const auto tex = Texture(ColorDesc(), "light-direct");
    const auto other = Texture(ColorDesc(), "other");
    {
        auto rg = Graph();
        const auto id = rg.ImportTrackedTexture("Light_Direct", tex);
        Pass(rg, "Write", {W(id, RHIResourceState::RenderTarget)});
        Pass(rg, "DebugView", {R(id, RHIResourceState::ShaderResource)});
        rg.Compile();
        rg.Execute(cmd.get());
    }
    {
        auto rg = Graph();
        const auto id = rg.ImportTrackedTexture("Other", other);
        Pass(rg, "Write", {W(id, RHIResourceState::RenderTarget)});
        rg.Compile();
        rg.Execute(cmd.get());
    }
    cmd->barriers.clear();
    {
        auto rg = Graph();
        const auto id = rg.ImportTrackedTexture("Light_Direct", tex);
        Pass(rg, "Write", {W(id, RHIResourceState::RenderTarget)});
        rg.Compile();
        rg.Execute(cmd.get());
    }
    EXPECT_TRUE(Has(cmd->barriers, tex, RHIResourceState::ShaderResource, RHIResourceState::RenderTarget));
    EXPECT_TRUE(cmd->mismatches.empty());
}

/// 8. A culled pass does not change the committed state.
TEST_F(RenderGraphStates, CulledPassDoesNotChangeTheCommittedState) {
    const auto tex = Texture(ColorDesc(), "albedo");
    {
        auto rg = Graph();
        const auto id = rg.ImportTrackedTexture("Albedo", tex);
        uz::rg::RgTextureDesc td{};
        td.width = 64; td.height = 64; td.format = uz::RHIFormat::RGBA8Unorm;
        td.usage = uz::RHITextureUsage::RenderTarget;
        const auto unused = rg.CreateTexture("Unused", td);
        Pass(rg, "GBuffer", {W(id, RHIResourceState::RenderTarget)});
        Pass(rg, "DeadBranch", {R(id, RHIResourceState::ShaderResource), W(unused, RHIResourceState::RenderTarget)});
        rg.Compile();
        EXPECT_EQ(rg.GetCulledPassCount(), 1u);
        rg.Execute(cmd.get());
    }
    EXPECT_EQ(states->Get(tex), RHIResourceState::RenderTarget);
    EXPECT_EQ(cmd->Actual(tex), RHIResourceState::RenderTarget);
    cmd->barriers.clear();
    {
        auto rg = Graph();
        const auto id = rg.ImportTrackedTexture("Albedo", tex);
        Pass(rg, "GBuffer", {W(id, RHIResourceState::RenderTarget)});
        rg.Compile();
        rg.Execute(cmd.get());
    }
    EXPECT_TRUE(cmd->barriers.empty());
    EXPECT_TRUE(cmd->mismatches.empty());
}

/// 9. Transition to the recorded state is a no-op; UavBarrier emits {UAV, UAV}.
TEST_F(RenderGraphStates, TransitionToTheSameStateIsANoOpAndUavBarrierEmitsUavUav) {
    const auto tex = Texture(VolumeDesc(), "froxels");
    states->Transition(cmd.get(), tex, RHIResourceState::UnorderedAccess);
    EXPECT_EQ(cmd->barriers.size(), 1u);
    states->Transition(cmd.get(), tex, RHIResourceState::UnorderedAccess);
    EXPECT_EQ(cmd->barriers.size(), 1u);
    states->UavBarrier(cmd.get(), tex);
    ASSERT_EQ(cmd->barriers.size(), 2u);
    EXPECT_EQ(cmd->barriers[1].stateBefore, RHIResourceState::UnorderedAccess);
    EXPECT_EQ(cmd->barriers[1].stateAfter, RHIResourceState::UnorderedAccess);
    EXPECT_EQ(states->GetOrigin(tex), uz::rg::RgStateOrigin::External);
    EXPECT_TRUE(cmd->mismatches.empty());
}

/// 10. An explicit claim that disagrees with the registry is reported (observe mode); one that agrees is not.
TEST_F(RenderGraphStates, ExplicitClaimThatDisagreesIsReported) {
    const auto tex = Texture(VolumeDesc(), "froxel-integrated");
    {
        auto rg = Graph();
        const auto id = rg.ImportTexture("FroxelFog_Integrated", tex, RHIResourceState::Common);
        Pass(rg, "Integrate", {W(id, RHIResourceState::UnorderedAccess)});
        Pass(rg, "Apply", {R(id, RHIResourceState::ShaderResource)});
        rg.Compile();
        EXPECT_EQ(rg.GetImportClaimMismatchCount(), 0u);   // Common is its creation state: the claim holds
        rg.Execute(cmd.get());
    }
    {
        auto rg = Graph();
        const auto id = rg.ImportTexture("FroxelFog_Integrated", tex, RHIResourceState::UnorderedAccess);
        Pass(rg, "Integrate", {W(id, RHIResourceState::UnorderedAccess)});
        Pass(rg, "Apply", {R(id, RHIResourceState::ShaderResource)});
        rg.Compile();
        EXPECT_EQ(rg.GetImportClaimMismatchCount(), 1u);
        rg.Execute(cmd.get());
    }
    ASSERT_EQ(states->GetClaimMismatches().size(), 1u);
    const auto& m = states->GetClaimMismatches()[0];
    EXPECT_EQ(m.name, "FroxelFog_Integrated");
    EXPECT_EQ(m.claimed, RHIResourceState::UnorderedAccess);
    EXPECT_EQ(m.tracked, RHIResourceState::ShaderResource);
    EXPECT_EQ(m.origin, uz::rg::RgStateOrigin::Graph);
    // The claim really was wrong, which is what the observe mode is there to show: Integrate writes the
    // volume while it is still in ShaderResource (no barrier: the claim said UnorderedAccess), then the
    // UnorderedAccess -> ShaderResource barrier names a state it is not in.
    EXPECT_EQ(cmd->MismatchesFor("froxel-integrated"), 2u);
    // Two explicit imports of one handle keep two chains and are reported.
    {
        auto rg = Graph();
        const auto a = rg.ImportTexture("APVolume", tex, RHIResourceState::ShaderResource);
        const auto b = rg.ImportTexture("APVolume (sky)", tex, RHIResourceState::ShaderResource);
        EXPECT_NE(a.index, b.index);
        Pass(rg, "Read", {R(a, RHIResourceState::ShaderResource), R(b, RHIResourceState::ShaderResource)});
        rg.Compile();
        rg.Execute(cmd.get());
    }
    EXPECT_EQ(states->GetDoubleImportTotal(), 1u);
}

/// Present and Common are one D3D12 state (both 0): a backbuffer claimed Present on a texture in Common is not
/// a mismatch, and a transition between them emits nothing.
TEST_F(RenderGraphStates, PresentIsCommon) {
    const auto backbuffer = Texture(ColorDesc(), "backbuffer");
    auto rg = Graph();
    const auto id = rg.ImportTexture("Backbuffer", backbuffer, RHIResourceState::Present);
    Pass(rg, "Composite", {W(id, RHIResourceState::RenderTarget)});
    rg.Compile();
    EXPECT_EQ(rg.GetImportClaimMismatchCount(), 0u);
    rg.Execute(cmd.get());
    EXPECT_EQ(states->Get(backbuffer), RHIResourceState::RenderTarget);
    cmd->barriers.clear();
    states->Transition(cmd.get(), backbuffer, RHIResourceState::Present);
    EXPECT_EQ(cmd->barriers.size(), 1u);   // RenderTarget -> Present
    states->Transition(cmd.get(), backbuffer, RHIResourceState::Common);
    EXPECT_EQ(cmd->barriers.size(), 1u);   // Present -> Common: the same state, nothing emitted
    EXPECT_TRUE(RgStateRegistry::SameState(RHIResourceState::Present, RHIResourceState::Common));
}

/// The builder without a registry behaves as before: explicit claims, no commit, no report.
TEST_F(RenderGraphStates, WithoutARegistryNothingChanges) {
    const auto tex = Texture(ColorDesc(), "plain");
    uz::rg::RgBuilder rg(device.get(), pool.get());
    const auto id = rg.ImportTexture("Plain", tex, RHIResourceState::ShaderResource);
    Pass(rg, "Write", {W(id, RHIResourceState::RenderTarget)});
    rg.Compile();
    rg.Execute(cmd.get());
    EXPECT_TRUE(Has(cmd->barriers, tex, RHIResourceState::ShaderResource, RHIResourceState::RenderTarget));
    EXPECT_EQ(rg.GetImportClaimMismatchCount(), 0u);
    EXPECT_FALSE(states->Find(tex).has_value());
}

// ─────────────────────────────────────────────────────────────────────────────
// 11-12: an eight-frame mode matrix, with tracked imports and with today's claims
// ─────────────────────────────────────────────────────────────────────────────

/// The resources of the frame loop below, recreated at "Resize".
struct FrameResources {
    uz::RHITextureHandle hdr, depth, albedo, velocity, lightDirect, ptAccum[2], froxel;
    uz::RHIBufferHandle meshArgs;
};

/// Today's explicit claims (the audit's patterns) or tracked imports for one frame of a GBuffer → lighting
/// loop: a debug view on frames 3-4 (Light_* read), a path-traced frame 5 (accumulation pair), a froxel
/// volume written then read every frame, mesh arguments written then drawn every frame, Resize at frame 6.
void RunFrame(uz::rg::RgBuilder& rg, const FrameResources& r, int frame, bool tracked,
              RHIResourceState& hdrEnd, RHIResourceState& albedoEnd, bool& firstCycle) {
    using S = RHIResourceState;
    auto tex = [&](const char* name, uz::RHITextureHandle h, S claim) {
        return tracked ? rg.ImportTrackedTexture(name, h) : rg.ImportTexture(name, h, claim);
    };
    const auto hdr = tex("HDR_SceneColor", r.hdr, hdrEnd);
    const auto depth = tex("GBuf_Depth", r.depth, S::DepthStencilWrite);
    const auto albedo = tex("GBuf_Albedo", r.albedo, albedoEnd);
    const auto velocity = tex("GBuf_Velocity", r.velocity, albedoEnd);         // today: albedo's state
    const auto light = tex("Light_Direct", r.lightDirect, S::RenderTarget);    // today: always RT
    const auto froxel = tex("FroxelFog_Integrated", r.froxel, S::UnorderedAccess);   // today: UAV every frame
    const auto args = tracked ? rg.ImportTrackedBuffer("VFX_MeshArgs", r.meshArgs)
                              : rg.ImportBuffer("VFX_MeshArgs", r.meshArgs,
                                                firstCycle ? S::Common : S::IndirectArgument);  // today: ternary
    firstCycle = false;

    Pass(rg, "GBuffer", {W(albedo, S::RenderTarget), W(depth, S::DepthStencilWrite)});
    if (frame != 0) Pass(rg, "Velocity", {W(velocity, S::RenderTarget)});
    Pass(rg, "Froxels", {W(froxel, S::UnorderedAccess)});
    Pass(rg, "MeshSim", {WB(args, S::UnorderedAccess)});
    Pass(rg, "Lighting", {R(albedo, S::ShaderResource), R(depth, S::ShaderResource), R(froxel, S::ShaderResource),
                          W(hdr, S::RenderTarget), W(light, S::RenderTarget)});
    Pass(rg, "MeshDraw", {RB(args, S::IndirectArgument), W(hdr, S::RenderTarget)});
    if (frame == 3 || frame == 4) Pass(rg, "DebugView", {R(light, S::ShaderResource), W(hdr, S::RenderTarget)});
    if (frame == 5) {
        const auto curr = tex("PT_AccumCurr", r.ptAccum[1], S::RenderTarget);
        const auto prev = tex("PT_AccumPrev", r.ptAccum[0], S::ShaderResource);
        Pass(rg, "PathTrace", {R(prev, S::ShaderResource), W(curr, S::RenderTarget), W(hdr, S::RenderTarget)});
    }
    Pass(rg, "TAA", {R(hdr, S::ShaderResource), R(velocity, S::ShaderResource)});
    rg.Compile();
    hdrEnd = rg.GetFinalState(hdr);       // today's end-state export
    albedoEnd = rg.GetFinalState(albedo);
}

FrameResources CreateResources(GenerationalMockDevice& device, D3D12TruthCommandList& cmd) {
    FrameResources r{};
    auto make = [&](const uz::RHITextureDesc& d, const char* name) {
        auto h = device.CreateTexture(d);
        cmd.Watch(h, name);
        return h;
    };
    r.hdr = make(ColorDesc(), "HDR_SceneColor");
    r.depth = make(DepthDesc(), "GBuf_Depth");
    r.albedo = make(ColorDesc(), "GBuf_Albedo");
    r.velocity = make(ColorDesc(), "GBuf_Velocity");
    r.lightDirect = make(ColorDesc(), "Light_Direct");
    r.ptAccum[0] = make(ColorDesc(), "PT_AccumPrev");
    r.ptAccum[1] = make(ColorDesc(), "PT_AccumCurr");
    r.froxel = make(VolumeDesc(), "FroxelFog_Integrated");
    uz::RHIBufferDesc bd{};
    bd.sizeBytes = 256;
    bd.usage = uz::RHIBufferUsage::UnorderedAccess;
    r.meshArgs = device.CreateBuffer(bd);
    cmd.Watch(r.meshArgs, "VFX_MeshArgs");
    return r;
}

void RunMatrix(RenderGraphStates* t, GenerationalMockDevice& device, uz::rg::RgResourcePool& pool,
               RgStateRegistry& states, D3D12TruthCommandList& cmd, bool tracked) {
    FrameResources r = CreateResources(device, cmd);
    // Today's frame-0 warmup: the colour targets go to RT before the first graph.
    std::vector<uz::RHIBarrierDesc> warm;
    for (auto h : {r.hdr, r.albedo, r.velocity}) {
        uz::RHIBarrierDesc b{};
        b.texture = h;
        b.stateBefore = RHIResourceState::Common;
        b.stateAfter = RHIResourceState::RenderTarget;
        warm.push_back(b);
    }
    cmd.ResourceBarrier(warm.data(), static_cast<uint32_t>(warm.size()));
    for (const auto& b : warm) states.Set(b.texture, b.stateAfter);

    RHIResourceState hdrEnd = RHIResourceState::RenderTarget, albedoEnd = RHIResourceState::RenderTarget;
    bool firstCycle = true;
    for (int frame = 0; frame < 8; ++frame) {
        if (frame == 6) {   // Resize: the screen-size targets are recreated (new generations, Common)
            for (auto* h : {&r.hdr, &r.albedo, &r.velocity, &r.lightDirect}) {
                device.DestroyTexture(*h);
                *h = device.CreateTexture(ColorDesc());
            }
            cmd.Watch(r.hdr, "HDR_SceneColor");
            cmd.Watch(r.albedo, "GBuf_Albedo");
            cmd.Watch(r.velocity, "GBuf_Velocity");
            cmd.Watch(r.lightDirect, "Light_Direct");
            // today: the end-state members are not reset at Resize
        }
        uz::rg::RgBuilder rg(&device, &pool, &states);
        RunFrame(rg, r, frame, tracked, hdrEnd, albedoEnd, firstCycle);
        rg.Execute(&cmd);
        cmd.Execute();   // the frame's submit
    }
    (void)t;
}

/// 11. Eight frames with tracked imports: no barrier names a wrong state.
TEST_F(RenderGraphStates, EightFrameModeMatrixWithTrackedImportsHasNoMismatch) {
    RunMatrix(this, *device, *pool, *states, *cmd, /*tracked=*/true);
    for (const auto& m : cmd->mismatches) {
        ADD_FAILURE() << m.name << ": barrier claimed " << RgStateRegistry::StateName(m.claimedBefore)
                      << " but the resource was in " << RgStateRegistry::StateName(m.actual);
    }
    EXPECT_TRUE(cmd->mismatches.empty());
}

/// 12. Negative control: the same frames with today's claims fail on every audited pattern, and the
/// observe mode names them.
TEST_F(RenderGraphStates, NegativeControlTodaysClaimsFailOnEveryPattern) {
    RunMatrix(this, *device, *pool, *states, *cmd, /*tracked=*/false);
    EXPECT_GE(cmd->MismatchesFor("FroxelFog_Integrated"), 1u);   // UAV claimed, SRV from the last frame
    EXPECT_GE(cmd->MismatchesFor("Light_Direct"), 1u);           // RT claimed, never transitioned / SRV
    EXPECT_GE(cmd->MismatchesFor("GBuf_Velocity"), 1u);          // albedo's end state
    EXPECT_GE(cmd->MismatchesFor("HDR_SceneColor"), 1u);         // the old end state after Resize
    EXPECT_GE(cmd->MismatchesFor("PT_AccumCurr"), 1u);           // RT claimed: written while still Common
    std::unordered_set<std::string> reported;
    for (const auto& m : states->GetClaimMismatches()) reported.insert(m.name);
    for (const char* name : {"FroxelFog_Integrated", "Light_Direct", "GBuf_Velocity", "HDR_SceneColor", "PT_AccumCurr"}) {
        EXPECT_TRUE(reported.count(name)) << name << " was not reported by the observe mode";
    }
}

} // namespace
