// editor_component.cpp — Editor-only rendering: ground grid + gizmos.
//
// Gizmo drawing is fully delegated to the GizmoRegistry. To add a new
// gizmo type, create a class implementing GizmoRenderer and register it
// in init() below. No other changes needed.

#include "editor_component.h"

#include "renderer/renderer.h"  // For Vertex struct and HLSL shader sources

// Gizmo types — each is a self-contained GizmoRenderer implementation.
#include "renderer/gizmos/gizmo_cube.h"
#include "renderer/gizmos/gizmo_camera.h"
#include "renderer/gizmos/gizmo_directional_light.h"
#include "renderer/gizmos/gizmo_point_light.h"

#include <cstring>
#include <stdexcept>
#include <string>

#include <d3dcompiler.h>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace {

inline void ThrowIfFailed(HRESULT hr, const char* context = nullptr) {
    if (SUCCEEDED(hr)) return;
    std::string msg = "D3D12 call failed with HRESULT 0x";
    char hex[16];
    snprintf(hex, sizeof(hex), "%08lX", static_cast<unsigned long>(hr));
    msg += hex;
    if (context) { msg += " ("; msg += context; msg += ")"; }
    throw std::runtime_error(msg);
}

} // anonymous namespace

namespace uz {

// ============================================================================
// Initialization
// ============================================================================

void EditorComponent::init(ID3D12Device* device, ID3D12RootSignature* rootSig) {
    createLinePipelineState(device, rootSig);
    createGridVertexBuffer(device);

    // -- Register gizmos by NodeType. ----------------------------------------
    // NodeType enum values (from scene_node.h):
    //   DirectionalLight = 3, PointLight = 4, Camera = 6
    gizmoRegistry_.setDefaultGizmo(std::make_unique<GizmoCube>());
    gizmoRegistry_.registerGizmo(3, std::make_unique<GizmoDirectionalLight>());
    gizmoRegistry_.registerGizmo(4, std::make_unique<GizmoPointLight>());
    gizmoRegistry_.registerGizmo(6, std::make_unique<GizmoCamera>());

    gizmoRegistry_.initAll(device);
    transformGizmo_.init(device);

    createSolidPipelineState(device, rootSig);
}

// ============================================================================
// Draw
// ============================================================================

void EditorComponent::draw(ID3D12GraphicsCommandList* cmdList,
                           const XMFLOAT4X4& viewProj,
                           uint32_t selectedNodeId,
                           int gizmoMode,
                           int gizmoActiveAxis) {
    // -- Ground grid --
    cmdList->SetPipelineState(linePipelineState_.Get());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);

    cmdList->SetGraphicsRoot32BitConstants(0, 16, &viewProj, 0);

    const float white[4] = {1.0f, 1.0f, 1.0f, 0.0f};
    cmdList->SetGraphicsRoot32BitConstants(1, 4, white, 0);

    cmdList->IASetVertexBuffers(0, 1, &gridVertexBufferView_);
    cmdList->DrawInstanced(gridVertexCount_, 1, 0, 0);

    // -- Per-node gizmos (dispatched via registry) --
    XMFLOAT3 selectedPos{0.0f, 0.0f, 0.0f};
    bool foundSelected = false;
    {
        std::lock_guard<std::mutex> lk(editorNodesMutex_);
        const XMMATRIX vp = XMLoadFloat4x4(&viewProj);

        for (const auto& node : editorNodes_) {
            const XMMATRIX nodeWorld =
                XMMatrixScaling(node.scale.x, node.scale.y, node.scale.z)
                * XMMatrixRotationRollPitchYaw(node.rotation.x, node.rotation.y, node.rotation.z)
                * XMMatrixTranslation(node.position.x, node.position.y, node.position.z);

            XMFLOAT4X4 mvp;
            XMStoreFloat4x4(&mvp, nodeWorld * vp);
            cmdList->SetGraphicsRoot32BitConstants(0, 16, &mvp, 0);

            const float col[4] = {node.color.x, node.color.y, node.color.z, 0.0f};
            cmdList->SetGraphicsRoot32BitConstants(1, 4, col, 0);

            // Lookup and draw the correct gizmo for this node type.
            GizmoRenderer* gizmo = gizmoRegistry_.get(node.nodeType);
            if (gizmo) {
                gizmo->draw(cmdList);
            }

            // Track selected node position
            if (selectedNodeId > 0 && node.nodeId == selectedNodeId) {
                selectedPos = node.position;
                foundSelected = true;
            }
        }
    }

    // -- Transform gizmo for selected node (using SOLID PSO) --
    if (foundSelected && selectedNodeId > 0) {
        cmdList->SetPipelineState(solidPipelineState_.Get());
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        const XMMATRIX vp = XMLoadFloat4x4(&viewProj);
        // Position gizmo at selected node, no rotation (world space gizmo)
        const XMMATRIX gizmoWorld = XMMatrixTranslation(
            selectedPos.x, selectedPos.y, selectedPos.z);

        XMFLOAT4X4 mvp;
        XMStoreFloat4x4(&mvp, gizmoWorld * vp);
        cmdList->SetGraphicsRoot32BitConstants(0, 16, &mvp, 0);

        // The vertex colors encode axis colors (R=X, G=Y, B=Z)
        const float gizmoCol[4] = {1.0f, 1.0f, 1.0f, 0.0f};
        cmdList->SetGraphicsRoot32BitConstants(1, 4, gizmoCol, 0);

        transformGizmo_.drawMode(cmdList, gizmoMode, gizmoActiveAxis);
    }
}

// ============================================================================
// Load editor nodes
// ============================================================================

size_t EditorComponent::loadNodes(std::vector<EditorNode> nodes) {
    const size_t count = nodes.size();
    {
        std::lock_guard<std::mutex> lk(editorNodesMutex_);
        editorNodes_ = std::move(nodes);
    }
    return count;
}

std::vector<EditorComponent::EditorNode> EditorComponent::getNodes() const {
    std::lock_guard<std::mutex> lk(editorNodesMutex_);
    return editorNodes_;
}

// ============================================================================
// Shutdown
// ============================================================================

void EditorComponent::shutdown() {
    gizmoRegistry_.shutdownAll();
    gridVertexBuffer_.Reset();
    linePipelineState_.Reset();
    solidPipelineState_.Reset();
}

// ============================================================================
// Private — Line PSO creation
// ============================================================================

void EditorComponent::createLinePipelineState(ID3D12Device* device,
                                              ID3D12RootSignature* rootSig) {
    UINT compileFlags = 0;
#ifdef UZENGINE_DEBUG
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> vertexShader;
    ComPtr<ID3DBlob> pixelShader;
    ComPtr<ID3DBlob> errors;

    HRESULT hr = D3DCompile(
        kVertexShaderSrc,
        strlen(kVertexShaderSrc),
        "VertexShader",
        nullptr, nullptr,
        "VSMain", "vs_5_0",
        compileFlags, 0,
        &vertexShader, &errors);

    if (FAILED(hr)) {
        std::string msg = "Line VS compilation failed";
        if (errors) {
            msg += ": ";
            msg += static_cast<const char*>(errors->GetBufferPointer());
        }
        throw std::runtime_error(msg);
    }

    errors.Reset();

    hr = D3DCompile(
        kPixelShaderSrc,
        strlen(kPixelShaderSrc),
        "PixelShader",
        nullptr, nullptr,
        "PSMain", "ps_5_0",
        compileFlags, 0,
        &pixelShader, &errors);

    if (FAILED(hr)) {
        std::string msg = "Line PS compilation failed";
        if (errors) {
            msg += ": ";
            msg += static_cast<const char*>(errors->GetBufferPointer());
        }
        throw std::runtime_error(msg);
    }

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, sizeof(XMFLOAT3),
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.InputLayout    = { inputLayout, _countof(inputLayout) };
    psoDesc.pRootSignature = rootSig;
    psoDesc.VS             = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
    psoDesc.PS             = { pixelShader->GetBufferPointer(),  pixelShader->GetBufferSize()  };

    psoDesc.RasterizerState.FillMode              = D3D12_FILL_MODE_WIREFRAME;
    psoDesc.RasterizerState.CullMode              = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthBias             = D3D12_DEFAULT_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthBiasClamp        = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    psoDesc.RasterizerState.SlopeScaledDepthBias  = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthClipEnable       = TRUE;
    psoDesc.RasterizerState.MultisampleEnable     = FALSE;
    psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
    psoDesc.RasterizerState.ForcedSampleCount     = 0;
    psoDesc.RasterizerState.ConservativeRaster    = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    psoDesc.BlendState.AlphaToCoverageEnable  = FALSE;
    psoDesc.BlendState.IndependentBlendEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].BlendEnable           = FALSE;
    psoDesc.BlendState.RenderTarget[0].LogicOpEnable         = FALSE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend              = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlend             = D3D12_BLEND_ZERO;
    psoDesc.BlendState.RenderTarget[0].BlendOp               = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha         = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha        = D3D12_BLEND_ZERO;
    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha          = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].LogicOp               = D3D12_LOGIC_OP_NOOP;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    psoDesc.DepthStencilState.DepthEnable    = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS;
    psoDesc.DepthStencilState.StencilEnable  = FALSE;
    psoDesc.DSVFormat                        = DXGI_FORMAT_D32_FLOAT;

    psoDesc.SampleMask            = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    psoDesc.NumRenderTargets      = 1;
    psoDesc.RTVFormats[0]         = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc            = { 1, 0 };

    ThrowIfFailed(
        device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&linePipelineState_)),
        "CreateGraphicsPipelineState (editor line)");
}

// ============================================================================
// Private — Solid triangle PSO for transform gizmo handles
// ============================================================================

void EditorComponent::createSolidPipelineState(ID3D12Device* device,
                                                ID3D12RootSignature* rootSig) {
    UINT compileFlags = 0;
#ifdef UZENGINE_DEBUG
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> vertexShader;
    ComPtr<ID3DBlob> pixelShader;
    ComPtr<ID3DBlob> errors;

    HRESULT hr = D3DCompile(
        kVertexShaderSrc, strlen(kVertexShaderSrc),
        "GizmoVS", nullptr, nullptr,
        "VSMain", "vs_5_0", compileFlags, 0,
        &vertexShader, &errors);
    if (FAILED(hr)) {
        std::string msg = "Solid VS compilation failed";
        if (errors) { msg += ": "; msg += static_cast<const char*>(errors->GetBufferPointer()); }
        throw std::runtime_error(msg);
    }
    errors.Reset();

    hr = D3DCompile(
        kPixelShaderSrc, strlen(kPixelShaderSrc),
        "GizmoPS", nullptr, nullptr,
        "PSMain", "ps_5_0", compileFlags, 0,
        &pixelShader, &errors);
    if (FAILED(hr)) {
        std::string msg = "Solid PS compilation failed";
        if (errors) { msg += ": "; msg += static_cast<const char*>(errors->GetBufferPointer()); }
        throw std::runtime_error(msg);
    }

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, sizeof(XMFLOAT3),
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.InputLayout    = { inputLayout, _countof(inputLayout) };
    psoDesc.pRootSignature = rootSig;
    psoDesc.VS             = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
    psoDesc.PS             = { pixelShader->GetBufferPointer(),  pixelShader->GetBufferSize()  };

    psoDesc.RasterizerState.FillMode              = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode              = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthBias             = D3D12_DEFAULT_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthBiasClamp        = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    psoDesc.RasterizerState.SlopeScaledDepthBias  = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthClipEnable       = TRUE;
    psoDesc.RasterizerState.MultisampleEnable     = FALSE;
    psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
    psoDesc.RasterizerState.ForcedSampleCount     = 0;
    psoDesc.RasterizerState.ConservativeRaster    = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    psoDesc.BlendState.AlphaToCoverageEnable  = FALSE;
    psoDesc.BlendState.IndependentBlendEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].BlendEnable           = FALSE;
    psoDesc.BlendState.RenderTarget[0].LogicOpEnable         = FALSE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend              = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlend             = D3D12_BLEND_ZERO;
    psoDesc.BlendState.RenderTarget[0].BlendOp               = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha         = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha        = D3D12_BLEND_ZERO;
    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha          = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].LogicOp               = D3D12_LOGIC_OP_NOOP;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    psoDesc.DepthStencilState.DepthEnable    = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    psoDesc.DepthStencilState.StencilEnable  = FALSE;
    psoDesc.DSVFormat                        = DXGI_FORMAT_D32_FLOAT;

    psoDesc.SampleMask            = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets      = 1;
    psoDesc.RTVFormats[0]         = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc            = { 1, 0 };

    ThrowIfFailed(
        device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&solidPipelineState_)),
        "CreateGraphicsPipelineState (editor solid gizmo)");
}
// ============================================================================
// Private — Grid vertex buffer
// ============================================================================

void EditorComponent::createGridVertexBuffer(ID3D12Device* device) {
    constexpr int   kHalfExtent = 100;
    constexpr int   kLineCount  = 2 * kHalfExtent + 1;
    constexpr int   kTotalVerts = kLineCount * 2 * 2;

    Vertex gridVerts[kTotalVerts];
    uint32_t idx = 0;

    const float extent = static_cast<float>(kHalfExtent);

    const XMFLOAT3 kMinorColor = {0.12f, 0.12f, 0.12f};
    const XMFLOAT3 kMajorColor = {0.25f, 0.25f, 0.25f};
    const XMFLOAT3 kAxisX      = {0.55f, 0.10f, 0.10f};
    const XMFLOAT3 kAxisZ      = {0.10f, 0.55f, 0.10f};

    for (int i = -kHalfExtent; i <= kHalfExtent; ++i) {
        const float x = static_cast<float>(i);
        XMFLOAT3 col;
        if (i == 0)          col = kAxisX;
        else if (i % 10 == 0) col = kMajorColor;
        else                  col = kMinorColor;
        gridVerts[idx++] = { {x, 0.0f, -extent}, col };
        gridVerts[idx++] = { {x, 0.0f,  extent}, col };
    }

    for (int i = -kHalfExtent; i <= kHalfExtent; ++i) {
        const float z = static_cast<float>(i);
        XMFLOAT3 col;
        if (i == 0)          col = kAxisZ;
        else if (i % 10 == 0) col = kMajorColor;
        else                  col = kMinorColor;
        gridVerts[idx++] = { {-extent, 0.0f, z}, col };
        gridVerts[idx++] = { { extent, 0.0f, z}, col };
    }

    gridVertexCount_ = idx;
    const UINT bufferSize = sizeof(Vertex) * gridVertexCount_;

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type                 = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask     = 1;
    heapProps.VisibleNodeMask      = 1;

    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width              = bufferSize;
    bufferDesc.Height             = 1;
    bufferDesc.DepthOrArraySize   = 1;
    bufferDesc.MipLevels          = 1;
    bufferDesc.Format             = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc         = { 1, 0 };
    bufferDesc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags              = D3D12_RESOURCE_FLAG_NONE;

    ThrowIfFailed(
        device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&gridVertexBuffer_)),
        "CreateCommittedResource (editor grid VB)");

    void* mappedData = nullptr;
    D3D12_RANGE readRange{ 0, 0 };
    ThrowIfFailed(gridVertexBuffer_->Map(0, &readRange, &mappedData),
                  "EditorGridVB::Map");
    std::memcpy(mappedData, gridVerts, bufferSize);
    gridVertexBuffer_->Unmap(0, nullptr);

    gridVertexBufferView_.BufferLocation = gridVertexBuffer_->GetGPUVirtualAddress();
    gridVertexBufferView_.StrideInBytes  = sizeof(Vertex);
    gridVertexBufferView_.SizeInBytes    = bufferSize;
}

} // namespace uz
