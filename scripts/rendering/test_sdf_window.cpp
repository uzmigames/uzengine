#define main ExistingSdfRegressionMain
#include "test_sdf_gpu.cpp"
#undef main
#include "../../runtime/engine/renderer/gi/mesh_sdf_bvh.h"

static std::vector<Triangle> WindowWall() {
    const float rectangles[4][4]={{-2000,-50,-500,500},{50,2000,-500,500},
                                  {-50,50,-500,-100},{-50,50,100,500}};
    std::vector<Triangle> result;
    for (const auto& rectangle:rectangles) {
        const float vertices[4][3]={{rectangle[0],0,rectangle[2]},{rectangle[1],0,rectangle[2]},
                                    {rectangle[1],0,rectangle[3]},{rectangle[0],0,rectangle[3]}};
        for (int corner=1;corner<3;++corner) {
            Triangle triangle{};
            std::memcpy(triangle.a,vertices[0],12);
            std::memcpy(triangle.b,vertices[corner],12);
            std::memcpy(triangle.c,vertices[corner+1],12);
            triangle.normal[1]=-1;
            result.push_back(triangle);
        }
    }
    return result;
}

static bool CenterRayHitsTriangle(const Triangle& triangle) {
    const float bx=triangle.b[0]-triangle.a[0], bz=triangle.b[2]-triangle.a[2];
    const float cx=triangle.c[0]-triangle.a[0], cz=triangle.c[2]-triangle.a[2];
    const float determinant=bx*cz-bz*cx;
    const float u=(-triangle.a[0]*cz+triangle.a[2]*cx)/determinant;
    const float v=(-bx*triangle.a[2]+bz*triangle.a[0])/determinant;
    return u>=0 && v>=0 && u+v<=1;
}

int main(int argc,char** argv) try {
    if (argc<5 || argc>7) throw std::runtime_error("Usage: test_sdf_window.exe meshResolution mesh.dxil gdf.dxil reflection.dxil [globalResolution [--bvh]]");
    const UINT meshResolution=static_cast<UINT>(std::stoul(argv[1]));
    if (meshResolution!=32 && meshResolution!=128 && meshResolution!=256) throw std::runtime_error("Mesh resolution must be 32, 128 or 256");
    const UINT globalResolution=argc>=6 ? static_cast<UINT>(std::stoul(argv[5])) : 128u;
    const bool bvh=argc==7 && std::strcmp(argv[6],"--bvh")==0;
    if (globalResolution!=128 && globalResolution!=256) throw std::runtime_error("Global resolution must be 128 or 256");
    const auto triangles=WindowWall();
    for (const auto& triangle:triangles) if (CenterRayHitsTriangle(triangle)) throw std::runtime_error("Analytic center ray unexpectedly hits window geometry");
    std::vector<uz::MeshSdfGpuTriangle> ordered(triangles.size());
    std::memcpy(ordered.data(),triangles.data(),triangles.size()*sizeof(Triangle));
    std::vector<uz::MeshSdfGpuBvhNode> nodes;
    if (bvh && !uz::BuildMeshSdfBvh(ordered,nodes)) throw std::runtime_error("Production BVH builder rejected fixture");
    std::printf("Mesh bake traversal: %s, triangles=%zu, nodes=%zu\n",bvh?"production BVH":"brute force",ordered.size(),nodes.size());
    std::printf("PASS exact geometry: eight triangles, opening100x200cm, center ray Y=-200 to +200 unobstructed; meshResolution=%u\n",meshResolution);
    ComPtr<IDXGIFactory6> factory;
    Check(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)),"Create window factory");
    ComPtr<ID3D12Device> device;
    for (UINT index=0;;++index) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapterByGpuPreference(index,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter))==DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 desc{}; Check(adapter->GetDesc1(&desc),"Get window adapter");
        if (!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE) && SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_ID3D12Device,reinterpret_cast<void**>(device.GetAddressOf())))) break;
    }
    if (!device) throw std::runtime_error("No hardware adapter");
    D3D12_ROOT_PARAMETER parameters[2]{};
    parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[0].Constants.Num32BitValues=9;
    parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[1].Descriptor.ShaderRegister=2;
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD=D3D12_FLOAT32_MAX; sampler.ShaderRegister=2;
    D3D12_ROOT_SIGNATURE_DESC root{};
    root.NumParameters=2; root.pParameters=parameters; root.NumStaticSamplers=1; root.pStaticSamplers=&sampler;
    root.Flags=D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
    ComPtr<ID3DBlob> rootBytes,error;
    Check(D3D12SerializeRootSignature(&root,D3D_ROOT_SIGNATURE_VERSION_1,&rootBytes,&error),"Serialize window root");
    ComPtr<ID3D12RootSignature> signature;
    Check(device->CreateRootSignature(0,rootBytes->GetBufferPointer(),rootBytes->GetBufferSize(),IID_ID3D12RootSignature,reinterpret_cast<void**>(signature.GetAddressOf())),"Create window root");
    const auto pipelineFor=[&](const char* path) {
        std::ifstream file(path,std::ios::binary);
        const std::vector<char> bytes{std::istreambuf_iterator<char>(file),{}};
        if (bytes.empty()) throw std::runtime_error("Missing window shader");
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{}; desc.pRootSignature=signature.Get(); desc.CS={bytes.data(),bytes.size()};
        ComPtr<ID3D12PipelineState> pipeline;
        Check(device->CreateComputePipelineState(&desc,IID_ID3D12PipelineState,reinterpret_cast<void**>(pipeline.GetAddressOf())),"Create window pipeline");
        return pipeline;
    };
    auto bakePipeline=pipelineFor(argv[2]); auto compositePipeline=pipelineFor(argv[3]);
    D3D12_COMMAND_QUEUE_DESC queueDesc{}; queueDesc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue; Check(device->CreateCommandQueue(&queueDesc,IID_ID3D12CommandQueue,reinterpret_cast<void**>(queue.GetAddressOf())),"Create window queue");
    ComPtr<ID3D12CommandAllocator> allocator; Check(device->CreateCommandAllocator(queueDesc.Type,IID_ID3D12CommandAllocator,reinterpret_cast<void**>(allocator.GetAddressOf())),"Create window allocator");
    ComPtr<ID3D12GraphicsCommandList> list; Check(device->CreateCommandList(0,queueDesc.Type,allocator.Get(),bakePipeline.Get(),IID_ID3D12GraphicsCommandList,reinterpret_cast<void**>(list.GetAddressOf())),"Create window list");
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{}; heapDesc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; heapDesc.NumDescriptors=10; heapDesc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> heap; Check(device->CreateDescriptorHeap(&heapDesc,IID_ID3D12DescriptorHeap,reinterpret_cast<void**>(heap.GetAddressOf())),"Create window heap");
    D3D12_CPU_DESCRIPTOR_HANDLE base{}; heap->GetCPUDescriptorHandleForHeapStart(&base);
    const UINT stride=device->GetDescriptorHandleIncrementSize(heapDesc.Type);
    const auto descriptor=[&](UINT index) { return D3D12_CPU_DESCRIPTOR_HANDLE{base.ptr+index*stride}; };
    auto triangleBuffer=Resource(device.Get(),BufferDesc((ordered.size()+nodes.size())*sizeof(Triangle)),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    void* mapped=nullptr; Check(triangleBuffer->Map(0,nullptr,&mapped),"Map window triangles");
    std::memcpy(mapped,ordered.data(),ordered.size()*sizeof(Triangle));
    if (!nodes.empty()) std::memcpy(static_cast<char*>(mapped)+ordered.size()*sizeof(Triangle),nodes.data(),nodes.size()*sizeof(uz::MeshSdfGpuBvhNode));
    triangleBuffer->Unmap(0,nullptr);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{}; srv.ViewDimension=D3D12_SRV_DIMENSION_BUFFER; srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Buffer.NumElements=static_cast<UINT>(ordered.size()+nodes.size()); srv.Buffer.StructureByteStride=sizeof(Triangle);
    device->CreateShaderResourceView(triangleBuffer.Get(),&srv,descriptor(1));
    D3D12_RESOURCE_DESC volume{}; volume.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE3D; volume.Width=meshResolution; volume.Height=meshResolution;
    volume.DepthOrArraySize=static_cast<UINT16>(meshResolution); volume.MipLevels=1; volume.Format=DXGI_FORMAT_R16_FLOAT; volume.SampleDesc.Count=1; volume.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    auto mesh=Resource(device.Get(),volume,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT meshFootprint{}; UINT meshRows{}; UINT64 meshBytes{};
    device->GetCopyableFootprints(&volume,0,1,0,&meshFootprint,&meshRows,nullptr,&meshBytes);
    auto meshReadback=Resource(device.Get(),BufferDesc(meshBytes),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{}; uav.Format=volume.Format; uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE3D; uav.Texture3D.WSize=meshResolution;
    device->CreateUnorderedAccessView(mesh.Get(),nullptr,&uav,descriptor(0));
    srv={}; srv.Format=volume.Format; srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE3D; srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Texture3D.MipLevels=1;
    device->CreateShaderResourceView(mesh.Get(),&srv,descriptor(2));
    volume.Width=volume.Height=volume.DepthOrArraySize=static_cast<UINT16>(globalResolution); volume.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    auto global=Resource(device.Get(),volume,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    uav.Format=volume.Format; uav.Texture3D.WSize=globalResolution;
    device->CreateUnorderedAccessView(global.Get(),nullptr,&uav,descriptor(3));
    volume.Format=DXGI_FORMAT_R16_FLOAT;
    auto rawGlobal=Resource(device.Get(),volume,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    uav.Format=DXGI_FORMAT_R16_FLOAT;
    device->CreateUnorderedAccessView(rawGlobal.Get(),nullptr,&uav,descriptor(8));
    ID3D12DescriptorHeap* heaps[]={heap.Get()}; list->SetDescriptorHeaps(1,heaps); list->SetComputeRootSignature(signature.Get());
    struct Bake { UINT output,triangles,count,resolution; float minimum[3],voxel; } bake{0,1,8,meshResolution|0x80000000u,{-2400,-2400,-2400},4800.0f/meshResolution};
    if (bvh) bake.count|=uz::kMeshSdfBvhFlag;
    list->SetComputeRoot32BitConstants(0,8,&bake,0); list->Dispatch(meshResolution/4,meshResolution/4,meshResolution/4);
    Transition(list.Get(),mesh.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION meshSource{}; meshSource.pResource=mesh.Get(); meshSource.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION meshDestination{}; meshDestination.pResource=meshReadback.Get(); meshDestination.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; meshDestination.PlacedFootprint=meshFootprint;
    list->CopyTextureRegion(&meshDestination,0,0,0,&meshSource,nullptr);
    Transition(list.Get(),mesh.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    struct Mesh { float origin[3],scale,extent[3],bias,inverse[3][4]; UINT srv,resolution,albedo,metal; float tint[3],metalFactor; UINT flags,padding[3]; };
    static_assert(sizeof(Mesh)==128);
    const Mesh metadata{{0,0,0},1,{2400,2400,2400},2400.0f/meshResolution,{{1,0,0,0},{0,1,0,0},{0,0,1,0}},2,meshResolution,0,0,{.8f,.2f,.1f},1,0,{0,0,0}};
    auto constants=Resource(device.Get(),BufferDesc(512*sizeof(Mesh)),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    Check(constants->Map(0,nullptr,&mapped),"Map window GDF constants"); std::memset(mapped,0,512*sizeof(Mesh)); std::memcpy(mapped,&metadata,sizeof(metadata)); constants->Unmap(0,nullptr);
    struct Composite { UINT output,count,accumulate,resolution; float center[3],extent; UINT rawOutput; } composite{3,1,0,globalResolution,{0,0,0},2500,8};
    list->SetPipelineState(compositePipeline.Get()); list->SetComputeRoot32BitConstants(0,9,&composite,0);
    list->SetComputeRootConstantBufferView(1,constants->GetGPUVirtualAddress()); list->Dispatch(globalResolution/4,globalResolution/4,globalResolution/4);
    Transition(list.Get(),global.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(list.Get(),rawGlobal.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Check(list->Close(),"Close window bake list"); ID3D12CommandList* lists[]={list.Get()}; queue->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence> fence; Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,reinterpret_cast<void**>(fence.GetAddressOf())),"Create window fence"); Check(queue->Signal(fence.Get(),1),"Signal window fence");
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr); if (!event) throw std::runtime_error("Create window event failed");
    Check(fence->SetEventOnCompletion(1,event),"Wait window fence"); const DWORD wait=WaitForSingleObject(event,30000); CloseHandle(event);
    if (wait!=WAIT_OBJECT_0) throw std::runtime_error("Window bake timeout"); Check(device->GetDeviceRemovedReason(),"Window bake execution");
    Check(meshReadback->Map(0,nullptr,&mapped),"Map window mesh readback");
    const std::string fieldPath="scripts/test_sdf_dynamic_gpu_artifacts/window_mesh_"+std::to_string(meshResolution)+(bvh?"_bvh.r16":"_brute.r16");
    std::ofstream field(fieldPath,std::ios::binary);
    if (!field) throw std::runtime_error("Cannot open window mesh evidence file");
    for (UINT z=0;z<meshResolution;++z) for (UINT y=0;y<meshResolution;++y) {
        const char* row=static_cast<const char*>(mapped)+meshFootprint.Offset+(z*meshRows+y)*meshFootprint.Footprint.RowPitch;
        field.write(row,meshResolution*sizeof(unsigned short));
    }
    field.close(); meshReadback->Unmap(0,nullptr);
    const bool escaped=RadianceCache(device.Get(),heap.Get(),queue.Get(),allocator.Get(),list.Get(),signature.Get(),global.Get(),rawGlobal.Get(),argv[4],false,true,true,true);
    std::printf("%s narrow opening: mesh=%u GDF=%u mesh_voxel=%.3f combined_bias=%.3f expected_escape=true\n",escaped?"PASS":"FAIL",meshResolution,globalResolution,bake.voxel,metadata.bias+2500.0f/globalResolution);
    return escaped?0:1;
} catch (const std::exception& error) { std::fprintf(stderr,"ERROR: %s\n",error.what()); return 2; }
