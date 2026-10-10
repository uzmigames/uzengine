#define NOMINMAX
#include <windows.h>
#include <initguid.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>
using Microsoft::WRL::ComPtr;
#include "test_sdf_dynamic_gpu_artifacts/mesh_normal.generated.h"

static void Check(HRESULT result, const char* action) {
    if (FAILED(result)) {
        char message[192];
        std::snprintf(message, sizeof(message), "%s failed: HRESULT 0x%08lx", action, static_cast<unsigned long>(result));
        throw std::runtime_error(message);
    }
}
static float Half(unsigned short bits) {
    float value = std::ldexp(static_cast<float>(bits & 1023), -24);
    const unsigned exponent = (bits >> 10) & 31;
    if (exponent == 31) return INFINITY;
    if (exponent) value = std::ldexp(1.0f + (bits & 1023) / 1024.0f, static_cast<int>(exponent) - 15);
    return (bits & 32768) ? -value : value;
}
struct Triangle { float a[4], b[4], c[4], normal[4]; };
static_assert(sizeof(Triangle) == 64);
static std::vector<Triangle> Cube(bool reversed, bool provideNormals) {
    std::vector<Triangle> result;
    for (int axis = 0; axis < 3; ++axis) for (int sign : {-1, 1}) {
        const int u = (axis + 1) % 3, v = (axis + 2) % 3;
        float p[4][6]{};
        const float corners[4][2] = {{-1,-1},{1,-1},{1,1},{-1,1}};
        for (int i = 0; i < 4; ++i) { p[i][axis] = static_cast<float>(sign); p[i][u] = corners[i][0]; p[i][v] = corners[i][1]; p[i][3+axis] = static_cast<float>(sign); }
        for (int i = 1; i < 3; ++i) {
            Triangle tri{};
            const bool forward = (sign > 0) != reversed;
            const float* p0 = p[0]; const float* p1 = p[forward ? i : i+1]; const float* p2 = p[forward ? i+1 : i];
            std::memcpy(tri.a, p0, 12); std::memcpy(tri.b, p1, 12); std::memcpy(tri.c, p2, 12);
            const auto normal = ProductionTriangleNormal(p0, p1, p2, provideNormals ? 12u : UINT32_MAX);
            std::memcpy(tri.normal, normal.data(), 12);
            result.push_back(tri);
        }
    }
    return result;
}
static D3D12_RESOURCE_DESC BufferDesc(UINT64 bytes) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; desc.Width = bytes;
    desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
    desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return desc;
}
static ComPtr<ID3D12Resource> Resource(ID3D12Device* device, D3D12_RESOURCE_DESC desc, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES props{}; props.Type = heap;
    ComPtr<ID3D12Resource> result;
    Check(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_ID3D12Resource, reinterpret_cast<void**>(result.GetAddressOf())), "CreateCommittedResource");
    return result;
}
static void Transition(ID3D12GraphicsCommandList* list, ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
    list->ResourceBarrier(1, &barrier);
}
static bool RadianceCache(ID3D12Device* device, ID3D12DescriptorHeap* heap, ID3D12CommandQueue* queue,
        ID3D12CommandAllocator* allocator, ID3D12GraphicsCommandList* list, ID3D12RootSignature* signature,
        ID3D12Resource* global, ID3D12Resource* rawGlobal, const char* shaderPath, bool room, bool firstWrite, bool noHistory,
        bool reflections = false, bool transformed = false, bool mixedValidity = false, bool energy = false, bool energyUniform = false) {
    std::ifstream file(shaderPath, std::ios::binary);
    const std::vector<char> shader{std::istreambuf_iterator<char>(file), {}};
    if (shader.empty()) throw std::runtime_error("Radiance trace bytecode is missing");
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc{}; pipelineDesc.pRootSignature=signature; pipelineDesc.CS={shader.data(),shader.size()};
    ComPtr<ID3D12PipelineState> pipeline;
    Check(device->CreateComputePipelineState(&pipelineDesc,IID_ID3D12PipelineState,reinterpret_cast<void**>(pipeline.GetAddressOf())),"Create RC pipeline");
    D3D12_RESOURCE_DESC cubeDesc{}; cubeDesc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; cubeDesc.Width=1; cubeDesc.Height=1;
    cubeDesc.DepthOrArraySize=6; cubeDesc.MipLevels=1; cubeDesc.SampleDesc.Count=1; cubeDesc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
    auto cube=Resource(device,cubeDesc,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT faces[6]{}; UINT64 bytes{};
    device->GetCopyableFootprints(&cubeDesc,0,6,0,faces,nullptr,nullptr,&bytes);
    auto cubeUpload=Resource(device,BufferDesc(bytes),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    void* mapped=nullptr; Check(cubeUpload->Map(0,nullptr,&mapped),"Map sky upload");
    const float sky[4]={1,0.5f,0.25f,1};
    for (const auto& face:faces) std::memcpy(static_cast<char*>(mapped)+face.Offset,sky,sizeof(sky));
    cubeUpload->Unmap(0,nullptr);
    const UINT totalProbes = room ? 27 : 1, probeIndex = room ? 13 : 0;
    auto shDesc=BufferDesc(totalProbes*7*4*sizeof(float)); shDesc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    auto sh=Resource(device,shDesc,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
    auto shUpload=Resource(device,BufferDesc(shDesc.Width),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    auto shReadback=Resource(device,BufferDesc(shDesc.Width),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    Check(shUpload->Map(0,nullptr,&mapped),"Map NaN SH sentinel"); std::fill_n(static_cast<float*>(mapped),totalProbes*28,NAN); shUpload->Unmap(0,nullptr);
    auto previous=Resource(device,BufferDesc(shDesc.Width),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    Check(previous->Map(0,nullptr,&mapped),"Map previous SH"); auto* previousData=static_cast<float*>(mapped);
    std::fill_n(previousData,totalProbes*28,firstWrite ? NAN : 0.0f);
    if (!firstWrite && !noHistory) for (UINT probe=0;probe<totalProbes;++probe) {
        previousData[probe*28+3]=previousData[probe*28+7]=previousData[probe*28+11]=1;
        previousData[probe*28+27]=1;
    }
    if (mixedValidity) for (UINT probe=0;probe<totalProbes;++probe) {
        if (probe==probeIndex) continue;
        previousData[probe*28+3]=previousData[probe*28+7]=previousData[probe*28+11]=100;
        previousData[probe*28+27]=0;
    }
    // Reflection energy fixture: a non-uniform environment. Probe layers along z carry
    // DC irradiance 0.05 / 1 / 20, so GGX rays that hit the floor, the walls and the
    // ceiling return radiance two orders of magnitude apart — the regime where the
    // Karis 1/(1+luma) weight departs from the plain mean. The uniform variant keeps
    // the constant-1 history, where both estimators must agree exactly.
    if (energy && !energyUniform) for (UINT probe=0;probe<totalProbes;++probe) {
        const float layer[3]={0.05f,1.0f,20.0f};
        const float value=layer[(probe/9)%3];
        previousData[probe*28+3]=previousData[probe*28+7]=previousData[probe*28+11]=value;
        previousData[probe*28+27]=1;
    }
    previous->Unmap(0,nullptr);
    struct Trace { float origin[3],spacing; UINT axis,total,rays,frame; float maximum,alpha; UINT uav,first;
        float clips[4][4]; UINT srvs[4]; UINT count; float sky[3],lightDirection[3],intensity,lightColor[3],albedo; };
    static_assert(sizeof(Trace)==176);
    Trace trace{}; trace.spacing=100; trace.axis=1; trace.total=1; trace.rays=32; trace.maximum=100; trace.alpha=0.1f;
    trace.uav=4; trace.first=firstWrite ? 1 : 0; trace.count=4; trace.lightDirection[2]=1;
    if (room) { trace.axis=3; trace.total=27; trace.spacing=200; trace.maximum=400; trace.alpha=1; for (float& coordinate:trace.origin) coordinate=-200; }
    for (int i=0;i<4;++i) { trace.clips[i][3]=room ? 200 : 1000; trace.srvs[i]=6; }
    if (transformed) {
        const float center[3]={50,-25,75};
        for (int axis=0;axis<3;++axis) {
            trace.origin[axis]+=center[axis];
            for (auto& clip:trace.clips) clip[axis]=center[axis];
        }
    }
    for (float& value:trace.sky) value=1;
    auto constants=Resource(device,BufferDesc(256),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    Check(constants->Map(0,nullptr,&mapped),"Map RC constants"); std::memcpy(mapped,&trace,sizeof(trace)); constants->Unmap(0,nullptr);
    D3D12_CPU_DESCRIPTOR_HANDLE handle{}; heap->GetCPUDescriptorHandleForHeapStart(&handle);
    const UINT stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); handle.ptr+=4*stride;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{}; uav.ViewDimension=D3D12_UAV_DIMENSION_BUFFER; uav.Buffer.NumElements=totalProbes*7; uav.Buffer.StructureByteStride=16;
    device->CreateUnorderedAccessView(sh.Get(),nullptr,&uav,handle); handle.ptr+=stride;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format=cubeDesc.Format; srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURECUBE;
    srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.TextureCube.MipLevels=1;
    device->CreateShaderResourceView(cube.Get(),&srv,handle); handle.ptr+=stride;
    srv.Format=DXGI_FORMAT_R16G16B16A16_FLOAT; srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE3D; srv.Texture3D.MipLevels=1;
    device->CreateShaderResourceView(global,&srv,handle); handle.ptr+=stride;
    srv={}; srv.ViewDimension=D3D12_SRV_DIMENSION_BUFFER; srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Buffer.NumElements=totalProbes*7; srv.Buffer.StructureByteStride=16;
    device->CreateShaderResourceView(previous.Get(),&srv,handle);
    heap->GetCPUDescriptorHandleForHeapStart(&handle); handle.ptr += 9*stride;
    srv={}; srv.Format=DXGI_FORMAT_R16_FLOAT; srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE3D;
    srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Texture3D.MipLevels=1;
    device->CreateShaderResourceView(rawGlobal,&srv,handle);
    Check(allocator->Reset(),"Reset RC allocator"); Check(list->Reset(allocator,pipeline.Get()),"Reset RC list");
    for (UINT i=0;i<6;++i) {
        D3D12_TEXTURE_COPY_LOCATION destination{}; destination.pResource=cube.Get(); destination.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; destination.SubresourceIndex=i;
        D3D12_TEXTURE_COPY_LOCATION source{}; source.pResource=cubeUpload.Get(); source.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; source.PlacedFootprint=faces[i];
        list->CopyTextureRegion(&destination,0,0,0,&source,nullptr);
    }
    list->CopyBufferRegion(sh.Get(),0,shUpload.Get(),0,shDesc.Width);
    Transition(list,cube.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(list,global,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(list,sh.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12DescriptorHeap* heaps[]={heap}; list->SetDescriptorHeaps(1,heaps); list->SetComputeRootSignature(signature);
    const UINT roots[8]={4,7,0,probeIndex,1,room && !reflections ? UINT32_MAX : 5u,0,0}; list->SetComputeRoot32BitConstants(0,8,roots,0);
    list->SetComputeRootConstantBufferView(1,constants->GetGPUVirtualAddress()); list->Dispatch(1,1,1);
    Transition(list,sh.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->CopyBufferRegion(shReadback.Get(),0,sh.Get(),0,shDesc.Width); Check(list->Close(),"Close RC list");
    ID3D12CommandList* lists[]={list}; queue->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence> fence; Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,reinterpret_cast<void**>(fence.GetAddressOf())),"Create RC fence");
    Check(queue->Signal(fence.Get(),1),"Signal RC fence"); HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if (!event) throw std::runtime_error("Create RC event failed");
    Check(fence->SetEventOnCompletion(1,event),"Set RC fence event"); const DWORD wait=WaitForSingleObject(event,30000); CloseHandle(event);
    if (wait!=WAIT_OBJECT_0) throw std::runtime_error("RC GPU fence timed out");
    Check(device->GetDeviceRemovedReason(),"RC GPU execution"); Check(shReadback->Map(0,nullptr,&mapped),"Map RC readback");
    const auto* packed=static_cast<const float*>(mapped)+probeIndex*28; unsigned invalid=0; float maximumError=0;
    const float reflectance[3]={0.8f,0.2f,0.1f};
    for (int i=0;i<28;++i) invalid+=!std::isfinite(packed[i]);
    if (energy) {
        // Wrapper layout: output[k] = sum of 8 pixels x `rays` GGX rays with the Karis weight,
        // output[8+k] = the same pixels/rays with the plain mean (k = thread 0..7).
        const float* out=static_cast<const float*>(mapped);
        double karis[3]{}, plain[3]{}; float pixels=0;
        for (int k=0;k<8;++k) for (int c=0;c<3;++c) { karis[c]+=out[4*k+c]; plain[c]+=out[4*(8+k)+c]; }
        for (int k=0;k<8;++k) pixels+=out[4*k+3];
        unsigned bad=0; double ratio[3]{};
        for (int c=0;c<3;++c) {
            bad+=!std::isfinite(karis[c])||!std::isfinite(plain[c])||plain[c]<=0;
            ratio[c]=plain[c]>0 ? karis[c]/plain[c] : 0.0;
        }
        bool passed=bad==0;
        for (int c=0;c<3;++c) passed=passed && (energyUniform ? std::abs(ratio[c]-1.0)<0.01 : (ratio[c]>0.0 && ratio[c]<=1.01));
        shReadback->Unmap(0,nullptr);
        std::printf("%s reflection energy: history=%s pixels=%g rays_per_pixel=8 karis_over_plain=(%.4f,%.4f,%.4f) plain_mean=(%.4f,%.4f,%.4f) karis_mean=(%.4f,%.4f,%.4f) criterion=%s\n",
            passed?"PASS":"FAIL", energyUniform?"uniform1":"layers0.05/1/20", pixels, ratio[0],ratio[1],ratio[2],
            plain[0]/std::max(pixels,1.0f),plain[1]/std::max(pixels,1.0f),plain[2]/std::max(pixels,1.0f),
            karis[0]/std::max(pixels,1.0f),karis[1]/std::max(pixels,1.0f),karis[2]/std::max(pixels,1.0f),
            energyUniform?"|ratio-1|<0.01":"0<ratio<=1.01 (measurement; the loss is the result)");
        return passed;
    }
    if (reflections) {
        unsigned traceFailures=0;
        for (int ray=0;ray<6;++ray) {
            const float t=packed[4*ray+3];
            const float halfSizes[3]={100,50,150};
            const float faceDistance=transformed ? halfSizes[ray/2] : 100;
            const float conservativeScaleRatios[3]={2,1,3};
            const float tolerance=transformed ? 12.5f*conservativeScaleRatios[ray/2] : 25.0f;
            const bool wrongDistance=room ? !(t>faceDistance-tolerance && t<=faceDistance) : t!=-1;
            traceFailures+=wrongDistance;
            if (wrongDistance) std::printf("Reflection distance mismatch: ray=%d t=%.6f geometric_face=%.6f\n",ray,t,faceDistance);
            for (int channel=0;channel<3;++channel) {
                const float expected=room ? (noHistory ? 0.0f : reflectance[channel]) : sky[channel];
                maximumError=std::max(maximumError,std::abs(packed[4*ray+channel]-expected));
            }
        }
        if (maximumError>=0.002f) for (int ray=0;ray<6;++ray)
            std::printf("GPU sample %d: (%g,%g,%g) t=%g\n",ray,packed[4*ray],packed[4*ray+1],packed[4*ray+2],packed[4*ray+3]);
        for (int channel=0;channel<3;++channel) traceFailures+=packed[24+channel]!=0;
        traceFailures+=packed[27]!=-2;
        shReadback->Unmap(0,nullptr);
        const bool passed=invalid==0 && traceFailures==0 && maximumError<0.002f;
        std::printf("%s world reflections: scene=%s history=%s sky=(1,.5,.25) rays=6 invalid=%u trace_failures=%u max_abs_error=%.6f budget_exhaustion_black=true\n",
            passed?"PASS":"FAIL",room?(transformed?"transformed enclosed100cm":"enclosed100cm"):"empty",firstWrite?"disabled":noHistory?"zero":mixedValidity?"center1_invalid100":"constant1",invalid,traceFailures,maximumError);
        return passed;
    }
    for (int axis=0;axis<3;++axis) for (int sign:{-1,1}) for (int channel=0;channel<3;++channel) {
        float n[3]{}; n[axis]=static_cast<float>(sign); const float* a=packed+4*channel; const float* b=packed+4*(channel+3);
        const float value=a[0]*n[0]+a[1]*n[1]+a[2]*n[2]+a[3]+b[0]*n[0]*n[1]+b[1]*n[1]*n[2]+b[2]*n[2]*n[2]+b[3]*n[2]*n[0]+packed[24+channel]*(n[0]*n[0]-n[1]*n[1]);
        const float expected=room ? (firstWrite ? 0.0f : reflectance[channel]) : sky[channel];
        maximumError=std::max(maximumError,std::abs(value-expected)/std::max(expected,0.01f));
    }
    shReadback->Unmap(0,nullptr); const bool passed=invalid==0 && maximumError<0.1f;
    std::printf("%s radiance cache: scene=%s firstWrite=%u previous=%s NaN_output_sentinel=28 rays=32 invalid=%u six_axis_max_relative_error=%.6f tolerance=0.100000\n",passed?"PASS":"FAIL",room?"enclosed100cm":"empty",trace.first,firstWrite?"NaN":noHistory?"zero":mixedValidity?"center1_invalid100":"constant1",invalid,maximumError);
    return passed;
}
int main(int argc, char** argv) try {
    const bool reflections=argc>1 && std::strncmp(argv[1],"--reflection-",13)==0;
    const bool mixedValidity=argc>1 && std::strstr(argv[1],"mixed-validity")!=nullptr;
    const bool transformed=reflections && std::strcmp(argv[1],"--reflection-transformed-room")==0;
    const bool energy=reflections && std::strncmp(argv[1],"--reflection-energy",19)==0;
    const bool energyUniform=energy && std::strcmp(argv[1],"--reflection-energy-uniform")==0;
    const bool reversed = argc > 1 && (std::strcmp(argv[1], "--reversed-winding") == 0 || std::strcmp(argv[1], "--reversed-winding-without-normals") == 0);
    const bool provideNormals = reversed && std::strcmp(argv[1], "--reversed-winding") == 0;
    const bool plane = argc > 1 && (std::strcmp(argv[1], "--two-sided-plane") == 0 || std::strcmp(argv[1], "--signed-plane") == 0);
    const bool room = argc > 1 && (std::strncmp(argv[1], "--feedback-room", 15) == 0 || (reflections && std::strcmp(argv[1],"--reflection-empty")!=0));
    const bool roomFirstWrite = room && std::strcmp(argv[1], "--feedback-room-first-write") == 0;
    const bool noHistory = room && (std::strcmp(argv[1], "--feedback-room-without-history") == 0 || std::strcmp(argv[1],"--reflection-dark-room")==0);
    const bool twoSided = room || (plane && std::strcmp(argv[1], "--two-sided-plane") == 0);
    if (reversed || plane || room || reflections) { --argc; ++argv; }
    if (argc < 2 || argc > 4) throw std::runtime_error("Usage: test_sdf_gpu.exe mesh_bake.dxil [global_composite.dxil [radiance_trace.dxil]]");
    std::ifstream shaderFile(argv[1], std::ios::binary);
    const std::vector<char> shader{std::istreambuf_iterator<char>(shaderFile), {}};
    if (shader.empty()) throw std::runtime_error("Shader bytecode is missing or empty");
    ComPtr<IDXGIFactory6> factory;
    Check(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2");
    ComPtr<ID3D12Device> device;
    DXGI_ADAPTER_DESC1 adapterDesc{};
    for (UINT i = 0; ; ++i) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) == DXGI_ERROR_NOT_FOUND) break;
        Check(adapter->GetDesc1(&adapterDesc), "GetDesc1");
        if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_ID3D12Device, reinterpret_cast<void**>(device.GetAddressOf())))) break;
    }
    if (!device) throw std::runtime_error("No hardware D3D12 adapter found");
    char adapterName[256]{};
    WideCharToMultiByte(CP_UTF8, 0, adapterDesc.Description, -1, adapterName, sizeof(adapterName), nullptr, nullptr);
    std::printf("GPU: %s; debug layer disabled\n", adapterName);
    D3D12_ROOT_PARAMETER parameters[2]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[0].Constants.Num32BitValues = 9;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[1].Descriptor.ShaderRegister = 2;
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX; sampler.ShaderRegister = 2;
    D3D12_ROOT_SIGNATURE_DESC signatureDesc{};
    signatureDesc.NumParameters = 2; signatureDesc.pParameters = parameters;
    signatureDesc.NumStaticSamplers = 1; signatureDesc.pStaticSamplers = &sampler;
    signatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
    ComPtr<ID3DBlob> signatureBytes, error;
    Check(D3D12SerializeRootSignature(&signatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBytes, &error), "SerializeRootSignature");
    ComPtr<ID3D12RootSignature> signature;
    Check(device->CreateRootSignature(0, signatureBytes->GetBufferPointer(), signatureBytes->GetBufferSize(), IID_ID3D12RootSignature, reinterpret_cast<void**>(signature.GetAddressOf())), "CreateRootSignature");
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc{};
    pipelineDesc.pRootSignature = signature.Get(); pipelineDesc.CS = {shader.data(), shader.size()};
    ComPtr<ID3D12PipelineState> pipeline;
    Check(device->CreateComputePipelineState(&pipelineDesc, IID_ID3D12PipelineState, reinterpret_cast<void**>(pipeline.GetAddressOf())), "CreateComputePipelineState");
    auto triangles = Cube(reversed, provideNormals);
    if (room) for (auto& triangle:triangles) for (int axis=0;axis<3;++axis) { triangle.a[axis]*=100; triangle.b[axis]*=100; triangle.c[axis]*=100; }
    if (plane) {
        triangles = std::vector<Triangle>(triangles.end()-2, triangles.end());
        for (auto& triangle : triangles) triangle.a[2] = triangle.b[2] = triangle.c[2] = 0.0f;
    }
    std::printf("Mesh input: winding=%s vertex_normals=%s production_CPU_normal_block=true\n", reversed ? "reversed" : "outward", provideNormals ? "outward" : "absent");
    if (plane) std::printf("Mesh topology: open plane; two_sided=%s\n", twoSided ? "true" : "false");
    auto triangleBuffer = Resource(device.Get(), BufferDesc(triangles.size()*sizeof(Triangle)), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    void* mapped = nullptr;
    Check(triangleBuffer->Map(0, nullptr, &mapped), "Map triangles");
    std::memcpy(mapped, triangles.data(), triangles.size()*sizeof(Triangle)); triangleBuffer->Unmap(0, nullptr);
    constexpr UINT resolution = 32;
    D3D12_RESOURCE_DESC textureDesc{};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D; textureDesc.Width = resolution; textureDesc.Height = resolution;
    textureDesc.DepthOrArraySize = resolution; textureDesc.MipLevels = 1; textureDesc.SampleDesc.Count = 1;
    textureDesc.Format = DXGI_FORMAT_R16_FLOAT; textureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    auto texture = Resource(device.Get(), textureDesc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{}; UINT rows{}; UINT64 rowBytes{}, bytes{};
    device->GetCopyableFootprints(&textureDesc, 0, 1, 0, &footprint, &rows, &rowBytes, &bytes);
    auto upload = Resource(device.Get(), BufferDesc(bytes), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    auto readback = Resource(device.Get(), BufferDesc(bytes), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    Check(upload->Map(0, nullptr, &mapped), "Map sentinel");
    std::fill_n(static_cast<unsigned short*>(mapped), bytes/2, static_cast<unsigned short>(0x57b0)); // 123.0 in binary16.
    upload->Unmap(0, nullptr);
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{}; heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = 10; heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> heap;
    Check(device->CreateDescriptorHeap(&heapDesc, IID_ID3D12DescriptorHeap, reinterpret_cast<void**>(heap.GetAddressOf())), "CreateDescriptorHeap");
    D3D12_CPU_DESCRIPTOR_HANDLE handle{};
    heap->GetCPUDescriptorHandleForHeapStart(&handle);
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{}; uav.Format = DXGI_FORMAT_R16_FLOAT;
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D; uav.Texture3D.WSize = resolution;
    device->CreateUnorderedAccessView(texture.Get(), nullptr, &uav, handle);
    handle.ptr += device->GetDescriptorHandleIncrementSize(heapDesc.Type);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{}; srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Buffer.NumElements = static_cast<UINT>(triangles.size()); srv.Buffer.StructureByteStride = sizeof(Triangle);
    device->CreateShaderResourceView(triangleBuffer.Get(), &srv, handle);
    ComPtr<ID3D12CommandQueue> queue; D3D12_COMMAND_QUEUE_DESC queueDesc{};
    Check(device->CreateCommandQueue(&queueDesc, IID_ID3D12CommandQueue, reinterpret_cast<void**>(queue.GetAddressOf())), "CreateCommandQueue");
    ComPtr<ID3D12CommandAllocator> allocator;
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_ID3D12CommandAllocator, reinterpret_cast<void**>(allocator.GetAddressOf())), "CreateCommandAllocator");
    ComPtr<ID3D12GraphicsCommandList> list;
    Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), pipeline.Get(), IID_ID3D12GraphicsCommandList, reinterpret_cast<void**>(list.GetAddressOf())), "CreateCommandList");
    D3D12_TEXTURE_COPY_LOCATION gpuLocation{}; gpuLocation.pResource = texture.Get(); gpuLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION cpuLocation{}; cpuLocation.pResource = upload.Get(); cpuLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; cpuLocation.PlacedFootprint = footprint;
    list->CopyTextureRegion(&gpuLocation, 0, 0, 0, &cpuLocation, nullptr);
    Transition(list.Get(), texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12DescriptorHeap* heaps[] = {heap.Get()}; list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(signature.Get());
    struct Constants { UINT uav, srv, count, resolution; float minimum[3], voxel; } constants{0,1,static_cast<UINT>(triangles.size()),resolution | (twoSided ? 0x80000000u : 0u),{-2,-2,-2},4.0f/resolution};
    if (room) { for (float& coordinate:constants.minimum) coordinate=-200; constants.voxel=400.0f/resolution; }
    static_assert(sizeof(Constants) == 32);
    list->SetComputeRoot32BitConstants(0, 8, &constants, 0); list->Dispatch(8, 8, 8);
    Transition(list.Get(), texture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    cpuLocation.pResource = readback.Get(); list->CopyTextureRegion(&cpuLocation, 0, 0, 0, &gpuLocation, nullptr);
    Check(list->Close(), "Close command list");
    ID3D12CommandList* lists[] = {list.Get()}; queue->ExecuteCommandLists(1, lists);
    ComPtr<ID3D12Fence> fence; Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_ID3D12Fence, reinterpret_cast<void**>(fence.GetAddressOf())), "CreateFence");
    Check(queue->Signal(fence.Get(), 1), "Signal fence");
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event) throw std::runtime_error("CreateEvent failed");
    Check(fence->SetEventOnCompletion(1, event), "SetEventOnCompletion");
    const DWORD waitResult = WaitForSingleObject(event, 30000); CloseHandle(event);
    if (waitResult != WAIT_OBJECT_0) throw std::runtime_error("GPU fence timed out");
    Check(device->GetDeviceRemovedReason(), "GPU execution");
    Check(readback->Map(0, nullptr, &mapped), "Map readback");
    unsigned invalid = 0, wrongSign = 0, unchanged = 0; float maximumError = 0, minimum = INFINITY, maximum = -INFINITY;
    for (UINT z = 0; z < resolution; ++z) for (UINT y = 0; y < resolution; ++y) for (UINT x = 0; x < resolution; ++x) {
        const auto* row = reinterpret_cast<const unsigned short*>(static_cast<const char*>(mapped) + footprint.Offset + (z*rows+y)*footprint.Footprint.RowPitch);
        const float actual = Half(row[x]); float q[3];
        const UINT indices[] = {x,y,z};
        for (int axis = 0; axis < 3; ++axis) q[axis] = std::abs(constants.minimum[axis] + (indices[axis]+0.5f)*constants.voxel)-(room ? 100.0f : 1.0f);
        float squared = 0; for (float d : q) squared += std::max(d,0.0f)*std::max(d,0.0f);
        float expected = plane ? std::sqrt(std::max(q[0],0.0f)*std::max(q[0],0.0f)+std::max(q[1],0.0f)*std::max(q[1],0.0f)+(q[2]+1)*(q[2]+1))
            : std::sqrt(squared) + std::min(std::max({q[0],q[1],q[2]}),0.0f);
        if (room) expected=std::abs(expected);
        invalid += !std::isfinite(actual); unchanged += actual == 123.0f; wrongSign += (actual < 0) != (expected < 0);
        maximumError = std::max(maximumError, std::abs(actual-expected)); minimum = std::min(minimum,actual); maximum = std::max(maximum,actual);
    }
    readback->Unmap(0, nullptr);
    const bool passed = invalid == 0 && wrongSign == 0 && unchanged == 0 && (plane || room ? minimum > 0 : minimum < 0) && maximum > 0 && maximumError <= constants.voxel;
    std::printf("%s mesh SDF: voxels=32768 invalid=%u wrong_sign=%u sentinel=%u range=[%.6f,%.6f] max_error=%.6f tolerance=%.6f\n", passed?"PASS":"FAIL", invalid, wrongSign, unchanged, minimum, maximum, maximumError, constants.voxel);
    if (!passed || argc < 3) return passed ? 0 : 1;
    std::ifstream compositeFile(argv[2], std::ios::binary);
    const std::vector<char> composite{std::istreambuf_iterator<char>(compositeFile), {}};
    if (composite.empty()) throw std::runtime_error("Global composite bytecode is missing");
    pipelineDesc.CS = {composite.data(), composite.size()};
    ComPtr<ID3D12PipelineState> compositePipeline;
    Check(device->CreateComputePipelineState(&pipelineDesc, IID_ID3D12PipelineState, reinterpret_cast<void**>(compositePipeline.GetAddressOf())), "Create global pipeline");
    auto globalDesc=textureDesc; globalDesc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    auto global = Resource(device.Get(), globalDesc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    auto rawGlobal = Resource(device.Get(), textureDesc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    device->GetCopyableFootprints(&globalDesc,0,1,0,&footprint,&rows,&rowBytes,&bytes);
    readback=Resource(device.Get(),BufferDesc(bytes),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    cpuLocation.pResource=readback.Get();
    cpuLocation.PlacedFootprint=footprint;
    handle.ptr += device->GetDescriptorHandleIncrementSize(heapDesc.Type);
    srv = {}; srv.Format = DXGI_FORMAT_R16_FLOAT; srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Texture3D.MipLevels = 1;
    device->CreateShaderResourceView(texture.Get(), &srv, handle);
    handle.ptr += device->GetDescriptorHandleIncrementSize(heapDesc.Type);
    uav.Format=globalDesc.Format; device->CreateUnorderedAccessView(global.Get(), nullptr, &uav, handle);
    D3D12_CPU_DESCRIPTOR_HANDLE rawHandle{}; heap->GetCPUDescriptorHandleForHeapStart(&rawHandle);
    rawHandle.ptr += 8*device->GetDescriptorHandleIncrementSize(heapDesc.Type);
    uav.Format=DXGI_FORMAT_R16_FLOAT; device->CreateUnorderedAccessView(rawGlobal.Get(),nullptr,&uav,rawHandle);
    struct Mesh { float origin[3], scale, extent[3], pad, inverseX[4], inverseY[4], inverseZ[4]; UINT srv, resolution, albedoSrv,metallicSrv;
        float tint[3],metallicMultiplier; UINT materialFlags,padding[3]; };
    static_assert(sizeof(Mesh) == 128);
    Mesh mesh = plane || room ? Mesh{{0,0,0},1,{2,2,2},2.0f/resolution,{1,0,0,0},{0,1,0,0},{0,0,1,0},2,resolution,0,0,{.8f,.2f,.1f},1,0,{0,0,0}}
        : Mesh{{0,0,0},0.5f,{2,2,2},0,{0,2,0,0.5f},{-1,0,0,0.5f},{0,0,2.0f/3,-0.5f},2,resolution,0,0,{.8f,.2f,.1f},1,0,{0,0,0}};
    if (room) { for (float& extent:mesh.extent) extent=200; mesh.pad=200.0f/resolution; }
    if (transformed) {
        mesh.scale=0.5f;
        const float inverse[3][4]={{0,2,0,50},{-1,0,0,50},{0,0,2.0f/3,-50}};
        std::memcpy(mesh.inverseX,inverse,sizeof(inverse));
    }
    auto meshConstants = Resource(device.Get(), BufferDesc(512*sizeof(Mesh)), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    Check(meshConstants->Map(0, nullptr, &mapped), "Map global mesh constants");
    std::memset(mapped, 0, 512*sizeof(Mesh));
    if (!room) {
        Mesh distant=mesh; distant.inverseX[3]+=1000; distant.tint[0]=0.1f; distant.tint[2]=0.8f;
        std::memcpy(mapped,&distant,sizeof(distant)); std::memcpy(static_cast<char*>(mapped)+sizeof(Mesh),&mesh,sizeof(mesh));
    } else std::memcpy(mapped, &mesh, sizeof(mesh));
    meshConstants->Unmap(0, nullptr);
    const UINT compositeMeshCount=room ? 1u : 2u;
    for (UINT meshCount : {compositeMeshCount,0u}) {
        if (room && meshCount==0) break;
        Check(allocator->Reset(), "Reset allocator");
        Check(list->Reset(allocator.Get(), compositePipeline.Get()), "Reset global command list");
        if (meshCount != 0) Transition(list.Get(), texture.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        else {
            Transition(list.Get(), global.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Transition(list.Get(), rawGlobal.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        list->SetDescriptorHeaps(1, heaps); list->SetComputeRootSignature(signature.Get());
        struct GlobalConstants { UINT output, count, accumulate, resolution; float center[3], extent; UINT rawOutput; } globalConstants{3,meshCount,0,resolution,{0.5f,-0.25f,0.75f},2,8};
        if (plane || room) for (float& coordinate : globalConstants.center) coordinate = 0;
        if (room) globalConstants.extent=200;
        if (transformed) { globalConstants.center[0]=50; globalConstants.center[1]=-25; globalConstants.center[2]=75; }
        list->SetComputeRoot32BitConstants(0, 9, &globalConstants, 0);
        list->SetComputeRootConstantBufferView(1, meshConstants->GetGPUVirtualAddress()); list->Dispatch(8,8,8);
        Transition(list.Get(), global.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        Transition(list.Get(), rawGlobal.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        gpuLocation.pResource = global.Get(); list->CopyTextureRegion(&cpuLocation,0,0,0,&gpuLocation,nullptr);
        Check(list->Close(), "Close global command list"); queue->ExecuteCommandLists(1, lists);
        const UINT64 signal = meshCount != 0 ? 2 : 3;
        Check(queue->Signal(fence.Get(),signal), "Signal global fence");
        event = CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if (!event) throw std::runtime_error("CreateEvent failed");
        Check(fence->SetEventOnCompletion(signal,event), "Set global fence event");
        const DWORD result = WaitForSingleObject(event,30000); CloseHandle(event);
        if (result != WAIT_OBJECT_0) throw std::runtime_error("Global GPU fence timed out");
        Check(device->GetDeviceRemovedReason(), "Global GPU execution");
        Check(readback->Map(0,nullptr,&mapped), "Map global readback");
        unsigned failures = 0, negative = 0; float farCorner = 0;
        for (UINT z=0; z<resolution; ++z) for (UINT y=0; y<resolution; ++y) for (UINT x=0; x<resolution; ++x) {
            const auto* row = reinterpret_cast<const unsigned short*>(static_cast<const char*>(mapped)+footprint.Offset+(z*rows+y)*footprint.Footprint.RowPitch);
            const float actual = Half(row[4*x]); negative += actual < 0;
            for (UINT channel=0;channel<3;++channel) {
                const float material=Half(row[4*x+channel+1]);
                failures += !std::isfinite(material) || std::abs(material-(meshCount ? mesh.tint[channel] : 0.0f))>0.002f;
            }
            float q[] = {std::abs(-2+(x+0.5f)*0.125f)-1, std::abs(-2+(y+0.5f)*0.125f)-0.5f, std::abs(-2+(z+0.5f)*0.125f)-1.5f};
            float sum=0; for (float d:q) sum+=std::max(d,0.0f)*std::max(d,0.0f);
            const float expected = plane ? std::sqrt(std::max(q[0],0.0f)*std::max(q[0],0.0f)
                +std::max(q[1]-0.5f,0.0f)*std::max(q[1]-0.5f,0.0f)+(q[2]+1.5f)*(q[2]+1.5f))
                : std::sqrt(sum)+std::min(std::max({q[0],q[1],q[2]}),0.0f);
            if (!meshCount) failures += actual != 65504.0f;
            else failures += !std::isfinite(actual) || (!plane && !room && ((actual<0)!=(expected<0))) || (!room && expected>0 && actual>expected+0.002f);
            if (x==0 && y==0 && z==0) farCorner=actual;
        }
        if (plane && meshCount) {
            for (int direction : {-1,1}) {
                float position = -direction * 1.9375f; bool hit = false;
                for (int step = 0; step < 96 && std::abs(position) < 2; ++step) {
                    const UINT z = static_cast<UINT>((position+2)/0.125f);
                    const auto* row = reinterpret_cast<const unsigned short*>(static_cast<const char*>(mapped)+footprint.Offset+(z*rows+16)*footprint.Footprint.RowPitch);
                    const float distance = Half(row[16*4]);
                    if (distance <= 0.01f) { hit = true; break; }
                    position += direction * std::max(distance, 0.01f);
                }
                failures += !hit;
                std::printf("%s global two-sided plane ray: directionZ=%d hitZ=%.6f\n", hit ? "PASS" : "FAIL", direction, position);
            }
            failures += negative == 0 || negative == resolution*resolution*resolution || farCorner <= 0;
        }
        readback->Unmap(0,nullptr);
        std::printf("%s global SDF: %s voxels=32768 failures=%u negative=%u far_corner=%.6f reflectance=(%.1f,%.1f,%.1f)\n", failures?"FAIL":"PASS",meshCount?(room?"enclosed100cm":plane?"two-sided finite plane":"translated rotated nonuniform cube"):"empty volume",failures,negative,farCorner,meshCount?mesh.tint[0]:0,meshCount?mesh.tint[1]:0,meshCount?mesh.tint[2]:0);
        if (failures) return 1;
        if (meshCount==2) std::printf("PASS nearest material: distant blue instance replaced by nearer red instance\n");
    }
    return argc == 4 && !RadianceCache(device.Get(),heap.Get(),queue.Get(),allocator.Get(),list.Get(),signature.Get(),global.Get(),rawGlobal.Get(),argv[3],room,!room || roomFirstWrite,noHistory,reflections,transformed,mixedValidity,energy,energyUniform) ? 1 : 0;
} catch (const std::exception& exception) { std::fprintf(stderr, "ERROR: %s\n", exception.what()); return 2; }
