/*
 * rhi_dx12.h -- DX12 backend declaration
 *
 * Declares the rhi_dx12_backend vtable instance.  Only rhi_common.c (for
 * backend selection) and rhi_dx12.c (for implementation) should include
 * this header.  No DX12 types are exposed here.
 */

#ifndef UZ_RHI_DX12_H
#define UZ_RHI_DX12_H

#include "rhi.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const rhi_backend_t rhi_dx12_backend;

/* -----------------------------------------------------------------------
 * TEMPORARY bridge API — exposes raw DX12 pointers for subsystems not yet
 * ported to the RHI (EditorComponent, GizmoRegistry).
 * These will be removed once all rendering code uses rhi()->* exclusively.
 * ----------------------------------------------------------------------- */

#ifdef _WIN32
#include <d3d12.h>

/* Returns the raw ID3D12Device* from the RHI state (borrowed, NOT owned). */
ID3D12Device* rhi_dx12_get_device(void);

/* Returns the root signature from a pipeline slot (borrowed, NOT owned). */
ID3D12RootSignature* rhi_dx12_get_pipeline_root_sig(RhiPipeline pipeline);

/* Returns the raw ID3D12GraphicsCommandList* for a cmd list (borrowed). */
ID3D12GraphicsCommandList* rhi_dx12_get_cmd_list(RhiCmdList cmd);
#endif

#ifdef __cplusplus
}
#endif

#endif /* UZ_RHI_DX12_H */
