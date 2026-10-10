#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <cstdio>

int main() {
    IDXGIFactory4* factory = nullptr;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) { std::printf("Factory query failed: 0x%08lx\n", static_cast<unsigned long>(hr)); return 2; }
    IDXGIAdapter1* adapter = nullptr;
    hr = factory->EnumAdapters1(0, &adapter);
    factory->Release();
    if (FAILED(hr)) { std::printf("Adapter query failed: 0x%08lx\n", static_cast<unsigned long>(hr)); return 2; }
    DXGI_ADAPTER_DESC1 description{};
    hr = adapter->GetDesc1(&description);
    if (FAILED(hr)) { adapter->Release(); return 2; }
    std::printf("Adapter 0: %ls\n", description.Description);
    ID3D12Device* device = nullptr;
    hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
    adapter->Release();
    if (FAILED(hr)) { std::printf("Device query failed: 0x%08lx\n", static_cast<unsigned long>(hr)); return 2; }
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
    hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options));
    device->Release();
    if (FAILED(hr)) { std::printf("Options5 query failed: 0x%08lx\n", static_cast<unsigned long>(hr)); return 2; }
    std::printf("RaytracingTier=%u\nDXR_1_0=%s\nDXR_1_1_inline=%s\n",
        static_cast<unsigned>(options.RaytracingTier),
        options.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0 ? "supported" : "unsupported",
        options.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1 ? "supported" : "unsupported");
    return 0;
}
