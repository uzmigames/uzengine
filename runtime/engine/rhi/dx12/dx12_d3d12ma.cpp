/// D3D12MA compilation wrapper with GUID compatibility.
///
/// D3D12MA uses IID_PPV_ARGS which requires __uuidof<T> template
/// specializations. On Clang/MinGW, these come from __CRT_UUID_DECL.
/// This wrapper includes our GUID declarations before D3D12MA.

#include "dx12_guids.h"
#include "D3D12MemAlloc.cpp"
