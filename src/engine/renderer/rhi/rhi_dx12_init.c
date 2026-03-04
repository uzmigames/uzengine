/*
 * rhi_dx12_init.c -- DX12 RHI backend: lifecycle (init / shutdown / resize)
 *
 * Split from rhi_dx12.c per §FILE-SIZE-LIMIT (CS-21).
 * See rhi_dx12_internal.h for shared state and declarations.
 */

#ifdef _WIN32

#include "rhi_dx12_internal.h"

/* =======================================================================
 * Lifecycle: init
 * ======================================================================= */

bool dx12_init(void *window_handle, uint32_t width, uint32_t height) {
    HRESULT hr;

    assert(!s_dx12.initialized);
    memset(&s_dx12, 0, sizeof(s_dx12));
    s_dx12.width  = width;
    s_dx12.height = height;

    /* ------ Debug layer (debug builds only) ------ */
#if defined(_DEBUG) || defined(DEBUG)
    {
        ID3D12Debug *debug = NULL;
        if (SUCCEEDED(D3D12GetDebugInterface(&IID_ID3D12Debug, (void **)&debug))) {
            ID3D12Debug_EnableDebugLayer(debug);
            s_dx12.debug_controller = debug;
        }
    }
#endif

    /* ------ DXGI Factory ------ */
    {
        UINT flags = 0;
#if defined(_DEBUG) || defined(DEBUG)
        flags = DXGI_CREATE_FACTORY_DEBUG;
#endif
        hr = CreateDXGIFactory2(flags, &IID_IDXGIFactory6,
                                (void **)&s_dx12.factory);
        if (FAILED(hr)) {
            OutputDebugStringA("RHI DX12: CreateDXGIFactory2 failed\n");
            return false;
        }
    }

    /* ------ Adapter selection (prefer high-performance GPU) ------ */
    IDXGIAdapter1 *adapter = NULL;
    {
        for (UINT i = 0;
             IDXGIFactory6_EnumAdapterByGpuPreference(
                 s_dx12.factory, i,
                 DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                 &IID_IDXGIAdapter1, (void **)&adapter) != DXGI_ERROR_NOT_FOUND;
             i++) {
            DXGI_ADAPTER_DESC1 desc;
            IDXGIAdapter1_GetDesc1(adapter, &desc);
            /* Skip software adapter */
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                IDXGIAdapter1_Release(adapter);
                adapter = NULL;
                continue;
            }
            /* Check D3D12 support */
            if (SUCCEEDED(D3D12CreateDevice((IUnknown *)adapter,
                                            D3D_FEATURE_LEVEL_12_0,
                                            &IID_ID3D12Device, NULL))) {
                break; /* Found a suitable adapter */
            }
            IDXGIAdapter1_Release(adapter);
            adapter = NULL;
        }
    }
    if (!adapter) {
        OutputDebugStringA("RHI DX12: No suitable DX12 adapter found\n");
        return false;
    }

    /* ------ Device ------ */
    hr = D3D12CreateDevice((IUnknown *)adapter, D3D_FEATURE_LEVEL_12_0,
                           &IID_ID3D12Device, (void **)&s_dx12.device);
    IDXGIAdapter1_Release(adapter);
    if (FAILED(hr)) {
        OutputDebugStringA("RHI DX12: D3D12CreateDevice failed\n");
        return false;
    }

    /* ------ Command Queue (DIRECT) ------ */
    {
        D3D12_COMMAND_QUEUE_DESC qd;
        memset(&qd, 0, sizeof(qd));
        qd.Type     = D3D12_COMMAND_LIST_TYPE_DIRECT;
        qd.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        qd.Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE;
        qd.NodeMask = 0;
        hr = ID3D12Device_CreateCommandQueue(s_dx12.device, &qd,
                                             &IID_ID3D12CommandQueue,
                                             (void **)&s_dx12.cmd_queue);
        if (FAILED(hr)) {
            OutputDebugStringA("RHI DX12: CreateCommandQueue failed\n");
            return false;
        }
    }

    /* ------ Swap Chain ------ */
    {
        DXGI_SWAP_CHAIN_DESC1 sd;
        memset(&sd, 0, sizeof(sd));
        sd.Width       = width;
        sd.Height      = height;
        sd.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.Stereo      = FALSE;
        sd.SampleDesc.Count   = 1;
        sd.SampleDesc.Quality = 0;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = DX12_BACKBUFFER_COUNT;
        sd.Scaling     = DXGI_SCALING_STRETCH;
        sd.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.AlphaMode   = DXGI_ALPHA_MODE_UNSPECIFIED;
        sd.Flags       = 0;

        IDXGISwapChain1 *sc1 = NULL;
        hr = IDXGIFactory2_CreateSwapChainForHwnd(
            (IDXGIFactory2 *)s_dx12.factory,
            (IUnknown *)s_dx12.cmd_queue,
            (HWND)window_handle,
            &sd, NULL, NULL, &sc1);
        if (FAILED(hr)) {
            OutputDebugStringA("RHI DX12: CreateSwapChainForHwnd failed\n");
            return false;
        }

        hr = IDXGISwapChain1_QueryInterface(sc1, &IID_IDXGISwapChain3,
                                            (void **)&s_dx12.swap_chain);
        IDXGISwapChain1_Release(sc1);
        if (FAILED(hr)) {
            OutputDebugStringA("RHI DX12: QueryInterface for SwapChain3 failed\n");
            return false;
        }

        /* Disable Alt+Enter fullscreen toggle (engine manages its own) */
        IDXGIFactory2_MakeWindowAssociation((IDXGIFactory2 *)s_dx12.factory,
                                            (HWND)window_handle,
                                            DXGI_MWA_NO_ALT_ENTER);
    }

    /* ------ Descriptor Heaps ------ */
    {
        D3D12_DESCRIPTOR_HEAP_DESC hd;
        memset(&hd, 0, sizeof(hd));

        /* RTV heap */
        hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        hd.NumDescriptors = DX12_RTV_HEAP_SIZE;
        hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        hr = ID3D12Device_CreateDescriptorHeap(s_dx12.device, &hd,
                                               &IID_ID3D12DescriptorHeap,
                                               (void **)&s_dx12.rtv_heap);
        assert(SUCCEEDED(hr));

        /* DSV heap */
        hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        hd.NumDescriptors = DX12_DSV_HEAP_SIZE;
        hr = ID3D12Device_CreateDescriptorHeap(s_dx12.device, &hd,
                                               &IID_ID3D12DescriptorHeap,
                                               (void **)&s_dx12.dsv_heap);
        assert(SUCCEEDED(hr));

        /* CBV/SRV/UAV heap (shader-visible) */
        hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = DX12_CBV_SRV_UAV_HEAP_SIZE;
        hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr = ID3D12Device_CreateDescriptorHeap(s_dx12.device, &hd,
                                               &IID_ID3D12DescriptorHeap,
                                               (void **)&s_dx12.cbv_srv_uav_heap);
        assert(SUCCEEDED(hr));

        /* Sampler heap */
        hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        hd.NumDescriptors = DX12_SAMPLER_HEAP_SIZE;
        hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr = ID3D12Device_CreateDescriptorHeap(s_dx12.device, &hd,
                                               &IID_ID3D12DescriptorHeap,
                                               (void **)&s_dx12.sampler_heap);
        assert(SUCCEEDED(hr));

        /* Cache descriptor sizes */
        s_dx12.rtv_desc_size = ID3D12Device_GetDescriptorHandleIncrementSize(
            s_dx12.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        s_dx12.dsv_desc_size = ID3D12Device_GetDescriptorHandleIncrementSize(
            s_dx12.device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        s_dx12.cbv_srv_uav_desc_size = ID3D12Device_GetDescriptorHandleIncrementSize(
            s_dx12.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    /* ------ Frame fence ------ */
    {
        hr = ID3D12Device_CreateFence(s_dx12.device, 0,
                                      D3D12_FENCE_FLAG_NONE,
                                      &IID_ID3D12Fence,
                                      (void **)&s_dx12.frame_fence);
        assert(SUCCEEDED(hr));
        s_dx12.frame_fence_value = 0;
        s_dx12.frame_fence_event = CreateEventW(NULL, FALSE, FALSE, NULL);
        assert(s_dx12.frame_fence_event != NULL);
    }

    /* ------ Acquire swapchain backbuffers ------ */
    dx12_acquire_backbuffers();
    s_dx12.current_backbuffer = IDXGISwapChain3_GetCurrentBackBufferIndex(
        s_dx12.swap_chain);

    /* ------ PSO cache ------ */
    rhi_pso_cache_init();

    s_dx12.initialized = true;
    return true;
}

/* =======================================================================
 * Lifecycle: shutdown
 * ======================================================================= */

void dx12_shutdown(void) {
    if (!s_dx12.initialized) return;

    dx12_wait_for_gpu();

    rhi_pso_cache_shutdown();

    /* Release command lists */
    for (uint32_t i = 1; i < DX12_MAX_CMD_LISTS; i++) {
        if (s_dx12.cmd_lists[i].occupied) {
            if (s_dx12.cmd_lists[i].list)
                ID3D12GraphicsCommandList_Release(s_dx12.cmd_lists[i].list);
            if (s_dx12.cmd_lists[i].allocator)
                ID3D12CommandAllocator_Release(s_dx12.cmd_lists[i].allocator);
        }
    }

    /* Release user fences */
    for (uint32_t i = 1; i < DX12_MAX_FENCES; i++) {
        if (s_dx12.fences[i].occupied) {
            if (s_dx12.fences[i].fence)
                ID3D12Fence_Release(s_dx12.fences[i].fence);
            if (s_dx12.fences[i].event)
                CloseHandle(s_dx12.fences[i].event);
        }
    }

    /* Release pipelines */
    for (uint32_t i = 1; i < DX12_MAX_PIPELINES; i++) {
        if (s_dx12.pipelines[i].occupied) {
            if (s_dx12.pipelines[i].pso)
                ID3D12PipelineState_Release(s_dx12.pipelines[i].pso);
            if (s_dx12.pipelines[i].root_sig)
                ID3D12RootSignature_Release(s_dx12.pipelines[i].root_sig);
        }
    }

    /* Release user buffers */
    for (uint32_t i = 1; i < DX12_MAX_BUFFERS; i++) {
        if (s_dx12.buffers[i].occupied && s_dx12.buffers[i].resource) {
            ID3D12Resource_Release(s_dx12.buffers[i].resource);
        }
    }

    /* Release user textures (non-swapchain) */
    for (uint32_t i = 1; i < DX12_MAX_RESOURCES; i++) {
        if (s_dx12.textures[i].occupied
            && !s_dx12.textures[i].is_swapchain
            && s_dx12.textures[i].resource) {
            ID3D12Resource_Release(s_dx12.textures[i].resource);
        }
    }

    /* Release backbuffers */
    dx12_release_backbuffers();

    /* Release frame fence */
    if (s_dx12.frame_fence)       ID3D12Fence_Release(s_dx12.frame_fence);
    if (s_dx12.frame_fence_event) CloseHandle(s_dx12.frame_fence_event);

    /* Release descriptor heaps */
    if (s_dx12.sampler_heap)      ID3D12DescriptorHeap_Release(s_dx12.sampler_heap);
    if (s_dx12.cbv_srv_uav_heap)  ID3D12DescriptorHeap_Release(s_dx12.cbv_srv_uav_heap);
    if (s_dx12.dsv_heap)          ID3D12DescriptorHeap_Release(s_dx12.dsv_heap);
    if (s_dx12.rtv_heap)          ID3D12DescriptorHeap_Release(s_dx12.rtv_heap);

    /* Release core objects */
    if (s_dx12.swap_chain)        IDXGISwapChain3_Release(s_dx12.swap_chain);
    if (s_dx12.cmd_queue)         ID3D12CommandQueue_Release(s_dx12.cmd_queue);
    if (s_dx12.device)            ID3D12Device_Release(s_dx12.device);
    if (s_dx12.factory)           IDXGIFactory6_Release(s_dx12.factory);
    if (s_dx12.debug_controller)  ID3D12Debug_Release(s_dx12.debug_controller);

    memset(&s_dx12, 0, sizeof(s_dx12));
}

/* =======================================================================
 * Lifecycle: resize
 * ======================================================================= */

void dx12_resize(uint32_t width, uint32_t height) {
    if (!s_dx12.initialized) return;
    if (width == 0 || height == 0) return;

    dx12_wait_for_gpu();
    dx12_release_backbuffers();

    HRESULT hr = IDXGISwapChain3_ResizeBuffers(
        s_dx12.swap_chain,
        DX12_BACKBUFFER_COUNT,
        width, height,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        0);
    assert(SUCCEEDED(hr));
    (void)hr;

    s_dx12.width  = width;
    s_dx12.height = height;

    dx12_acquire_backbuffers();
    s_dx12.current_backbuffer = IDXGISwapChain3_GetCurrentBackBufferIndex(
        s_dx12.swap_chain);
}

#endif /* _WIN32 */
