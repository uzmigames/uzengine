# DX11 SetDescriptorTable must differentiate SRV vs UAV table types
**Source**: manual
**Date**: 2026-03-17
**Related Task**: phase0-0.8.0-uniform-buffer-system
**Tags**: dx11, uav, descriptor-table, compute, ibl
DX11's SetDescriptorTable was always routing to SRV binding (PSSetShaderResources/CSSetShaderResources), even for UAV_Table root parameters. Fix: check rootLayout.params[index].type — if UAV_Table, use CSSetUnorderedAccessViews instead. Added FindTextureUAVByPoolIndex and FindBufferUAVByPoolIndex to DX11Device to resolve pool indices to ID3D11UnorderedAccessView*. This was critical for IBL compute bake (irradiance/prefilter dispatches wrote to UAVs that were never bound).