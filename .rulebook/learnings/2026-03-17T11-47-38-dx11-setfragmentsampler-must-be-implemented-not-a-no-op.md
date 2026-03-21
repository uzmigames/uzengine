# DX11 SetFragmentSampler must be implemented — not a no-op
**Source**: manual
**Date**: 2026-03-17
**Related Task**: phase0-0.8.0-uniform-buffer-system
**Tags**: dx11, sampler, font, overlay, stats, SetFragmentSampler
IRHICommandList::SetFragmentSampler() had a default no-op implementation. DX11 never overrode it. Text/font renderers that call SetFragmentSampler(0, m_fontSampler) to bind the font atlas sampler had no sampler bound on DX11 — font atlas sampled with default sampler state producing garbled or invisible text. Fix: implement DX11 override using PSSetSamplers/CSSetSamplers (routes to CS stage when m_computePipelineBound). Always check that all IRHICommandList virtual methods have real implementations on every backend.