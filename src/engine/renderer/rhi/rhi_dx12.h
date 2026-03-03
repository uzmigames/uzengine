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

#ifdef __cplusplus
}
#endif

#endif /* UZ_RHI_DX12_H */
