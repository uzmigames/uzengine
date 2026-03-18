/// UzEngine GPU Material Baker — Implementation
///
/// Generates a compute shader wrapper around the material graph's HLSL,
/// compiles it, dispatches on the GPU, and returns a ready-to-bind SRV texture.
/// Follows the same command list / fence / dispatch / barrier pattern as
/// IBLBaker::BakeBRDFLut (see ibl_baker.cpp lines 264-369).

#include "material_baker_gpu.h"

#include "log.h"
#include "rhi.h"
#include "shader_pipeline.h"

#include <string>

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

namespace {

/// Thread group size for the bake compute shader (8x8 = 64 threads per group).
static constexpr uint32_t kThreadGroupSize = 8;

/// Generates the full compute shader source by wrapping the material graph's
/// generated HLSL function inside a dispatch kernel.
///
/// The kernel writes one RGBA8 texel per thread. The output pin selects which
/// PBR channel (BaseColor, Metallic, Roughness, Normal, AO) is written.
[[nodiscard]] std::string GenerateBakeShaderSource(const std::string& materialHlsl) {
    // The compute shader structure:
    // 1. Root constants cbuffer with output dimensions and pin selection
    // 2. UAV output texture
    // 3. Injected material HLSL (contains ComputeMaterialOutputs)
    // 4. Dispatch kernel that evaluates the material per-texel

    std::string source;
    source.reserve(materialHlsl.size() + 2048);

    source +=
        "// GPU Material Baker — Auto-generated compute shader\n"
        "// Wraps ComputeMaterialOutputs() in a per-texel dispatch kernel.\n"
        "\n"
        "cbuffer BakeParams : register(b0) {\n"
        "    uint outputWidth;\n"
        "    uint outputHeight;\n"
        "    uint outputPin;\n"
        "    uint _pad;\n"
        "};\n"
        "\n"
        "RWTexture2D<float4> outputTex : register(u0);\n"
        "\n";

    // Inject the material graph's generated HLSL
    source += "// ── Injected material HLSL ──────────────────────────────────────\n";
    source += materialHlsl;
    source += "\n// ── End injected material HLSL ──────────────────────────────────\n\n";

    source +=
        "[numthreads(8, 8, 1)]\n"
        "void main(uint3 dtid : SV_DispatchThreadID) {\n"
        "    if (dtid.x >= outputWidth || dtid.y >= outputHeight) return;\n"
        "\n"
        "    float2 uv = (float2(dtid.xy) + 0.5) / float2(outputWidth, outputHeight);\n"
        "\n"
        "    float3 outBaseColor, outNormal, outEmissive;\n"
        "    float outMetallic, outRoughness, outOpacity, outAO;\n"
        "\n"
        "    // Initialize materialParams to zero (no dynamic params during bake)\n"
        "    float4 materialParams[16];\n"
        "    [unroll] for (uint i = 0; i < 16; i++) materialParams[i] = float4(0,0,0,0);\n"
        "\n"
        "    ComputeMaterialOutputs(\n"
        "        uv,                    // texCoord\n"
        "        float3(0, 0, 0),       // worldPos\n"
        "        float3(0, 0, 1),       // worldNormal\n"
        "        float4(1, 1, 1, 1),    // vertexColor\n"
        "        float3(0, 0, 0),       // cameraPos\n"
        "        float3(0, 0, 0),       // lightDir\n"
        "        uv,                    // screenUV\n"
        "        0.0,                   // time\n"
        "        materialParams,        // float4[16]\n"
        "        outBaseColor,\n"
        "        outMetallic,\n"
        "        outRoughness,\n"
        "        outNormal,\n"
        "        outEmissive,\n"
        "        outOpacity,\n"
        "        outAO);\n"
        "\n"
        "    float4 result;\n"
        "    if (outputPin == 0)      result = float4(outBaseColor, 1.0);\n"
        "    else if (outputPin == 1) result = float4(outMetallic.xxx, 1.0);\n"
        "    else if (outputPin == 2) result = float4(outRoughness.xxx, 1.0);\n"
        "    else if (outputPin == 3) result = float4(outNormal * 0.5 + 0.5, 1.0);\n"
        "    else if (outputPin == 4) result = float4(outAO.xxx, 1.0);\n"
        "    else                     result = float4(1.0, 0.0, 1.0, 1.0); // magenta = error\n"
        "\n"
        "    outputTex[dtid.xy] = result;\n"
        "}\n";

    return source;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// MaterialBakerGpu::Bake
// ─────────────────────────────────────────────────────────────────────────────

MaterialBakerGpu::BakeResult MaterialBakerGpu::Bake(
    IRHIDevice* device,
    ShaderPipeline& pipeline,
    const std::string& materialHlsl,
    OutputPin outputPin,
    uint32_t width, uint32_t height)
{
    BakeResult result;

    if (!device) {
        UZ_LOG(Renderer, Error, "MaterialBakerGpu::Bake: device is null");
        return result;
    }

    if (materialHlsl.empty()) {
        UZ_LOG(Renderer, Error, "MaterialBakerGpu::Bake: materialHlsl is empty");
        return result;
    }

    if (width == 0 || height == 0) {
        UZ_LOG(Renderer, Error, "MaterialBakerGpu::Bake: invalid dimensions ({}x{})",
               width, height);
        return result;
    }

    // ── Step 1: Generate and compile the compute shader ──────────────────

    const std::string shaderSource = GenerateBakeShaderSource(materialHlsl);

    std::vector<uint8_t> bytecode = pipeline.LoadBytecodeFromSource(
        "bake_material_cs.hlsl", shaderSource);

    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "MaterialBakerGpu::Bake: failed to compile bake compute shader");
        return result;
    }

    // Create the shader module from compiled bytecode
    RHIShaderDesc shaderDesc;
    shaderDesc.bytecode = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage = RHIShaderStage::Compute;
    shaderDesc.entryPoint = "main";
    shaderDesc.debugName = "bake_material_cs";

    RHIShaderHandle csHandle = device->CreateShader(shaderDesc);
    if (!csHandle.IsValid()) {
        UZ_LOG(Renderer, Error, "MaterialBakerGpu::Bake: failed to create compute shader module");
        return result;
    }

    // Create the compute pipeline
    RHIComputePipelineDesc pipelineDesc;
    pipelineDesc.computeShader = csHandle;
    pipelineDesc.debugName = "GpuMaterialBake_PSO";

    RHIPipelineHandle pso = device->CreateComputePipeline(pipelineDesc);
    if (!pso.IsValid()) {
        UZ_LOG(Renderer, Error, "MaterialBakerGpu::Bake: failed to create compute pipeline");
        device->DestroyShader(csHandle);
        return result;
    }

    // ── Step 2: Create the output texture (RGBA8Unorm, UAV + SRV) ───────

    RHITextureDesc texDesc;
    texDesc.width = width;
    texDesc.height = height;
    texDesc.format = RHIFormat::RGBA8Unorm;
    texDesc.usage = RHITextureUsage::UnorderedAccess | RHITextureUsage::ShaderResource;
    texDesc.debugName = "GpuBakedMaterial";

    result.texture = device->CreateTexture(texDesc);
    if (!result.texture.IsValid()) {
        UZ_LOG(Renderer, Error, "MaterialBakerGpu::Bake: failed to create output texture ({}x{})",
               width, height);
        device->DestroyPipeline(pso);
        device->DestroyShader(csHandle);
        return result;
    }

    // ── Step 3: Create command list and fence ────────────────────────────

    auto* cmdList = device->CreateCommandList(RHIQueueType::Graphics);
    if (!cmdList) {
        UZ_LOG(Renderer, Error, "MaterialBakerGpu::Bake: failed to create command list");
        device->DestroyTexture(result.texture);
        result.texture = RHITextureHandle::Invalid();
        device->DestroyPipeline(pso);
        device->DestroyShader(csHandle);
        return result;
    }

    auto* fence = device->CreateFence();
    if (!fence) {
        UZ_LOG(Renderer, Error, "MaterialBakerGpu::Bake: failed to create fence");
        device->DestroyCommandList(cmdList);
        device->DestroyTexture(result.texture);
        result.texture = RHITextureHandle::Invalid();
        device->DestroyPipeline(pso);
        device->DestroyShader(csHandle);
        return result;
    }

    // ── Step 4: Record and dispatch ──────────────────────────────────────

    cmdList->Begin();

    // Bind compute pipeline
    cmdList->SetPipeline(pso);

    // Root constants at slot 0: outputWidth, outputHeight, outputPin, padding
    const uint32_t rootConstants[4] = {
        width,
        height,
        static_cast<uint32_t>(outputPin),
        0  // padding
    };
    cmdList->SetRootConstants(0, rootConstants, 4);

    // Bind UAV descriptor table at root param 4 (u0 range)
    uint32_t uavIndex = device->GetTextureUAVIndex(result.texture);
    if (uavIndex == UINT32_MAX) {
        UZ_LOG(Renderer, Error, "MaterialBakerGpu::Bake: output texture has no UAV descriptor");
        cmdList->End();
        device->DestroyFence(fence);
        device->DestroyCommandList(cmdList);
        device->DestroyTexture(result.texture);
        result.texture = RHITextureHandle::Invalid();
        device->DestroyPipeline(pso);
        device->DestroyShader(csHandle);
        return result;
    }
    cmdList->SetDescriptorTable(4, uavIndex);

    // Dispatch: ceil(width/8) x ceil(height/8) x 1
    const uint32_t groupsX = (width  + kThreadGroupSize - 1) / kThreadGroupSize;
    const uint32_t groupsY = (height + kThreadGroupSize - 1) / kThreadGroupSize;
    cmdList->Dispatch(groupsX, groupsY, 1);

    UZ_LOG(Renderer, Info,
           "MaterialBakerGpu: dispatch — groups=({}, {}, 1), UAV={}, size={}x{}, pin={}",
           groupsX, groupsY, uavIndex, width, height,
           static_cast<uint32_t>(outputPin));

    // Barrier: UnorderedAccess -> ShaderResource
    RHIBarrierDesc barrier;
    barrier.texture = result.texture;
    barrier.buffer = RHIBufferHandle::Invalid();
    barrier.stateBefore = RHIResourceState::UnorderedAccess;
    barrier.stateAfter = RHIResourceState::ShaderResource;
    cmdList->ResourceBarrier(&barrier, 1);

    cmdList->End();
    cmdList->Execute();

    // ── Step 5: Wait for GPU completion ──────────────────────────────────

    fence->Signal(1);
    fence->WaitCPU(1);

    // ── Step 6: Cleanup transient objects, keep the texture ──────────────

    device->DestroyFence(fence);
    device->DestroyCommandList(cmdList);
    device->DestroyPipeline(pso);
    device->DestroyShader(csHandle);

    // ── Step 7: Get SRV index for binding ────────────────────────────────

    result.srvIndex = device->GetTextureSRVIndex(result.texture);
    if (result.srvIndex == UINT32_MAX) {
        UZ_LOG(Renderer, Error,
               "MaterialBakerGpu::Bake: texture has no SRV — this should not happen");
        device->DestroyTexture(result.texture);
        result.texture = RHITextureHandle::Invalid();
        return result;
    }

    result.success = true;

    UZ_LOG(Renderer, Info,
           "MaterialBakerGpu: baked material ({}x{}, RGBA8, pin={}, SRV={})",
           width, height, static_cast<uint32_t>(outputPin), result.srvIndex);

    return result;
}

} // namespace uz
