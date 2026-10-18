"""Constant-buffer layouts: the HLSL side (DXC's layout comments) against the C++ side (`offsetof`
of the struct text cut from its header and compiled with `zig c++`). Lighting block B, item 4,
gate F1. HLSL packs a `float3` or an array element into 16-byte rows, so a C++ struct of scalars
and a shader declaring `float3`s at 4-byte-aligned offsets silently disagree; three buffers did.

Each entry: the shader (profile) that declares the cbuffer, the cbuffer name, the header and the
struct, and the map from HLSL field names to C++ field names (a vector's first component). Every
mapped field must have the same offset on both sides. The pre-fix shaders kept under
`scripts/rendering/test_cbuffer_layouts_fixtures/*.before.hlsl` are the negative control: at least one of
their fields must differ.

Tier A compares member offsets, Tier B compares root-block DWORD counts, and Tier C
compares reflected structured-resource strides. The inventory explicitly lists exclusions;
an exclusion is not a passing layout contract. Missing required production declarations
and existing layout mismatches fail the run, rather than being silently waived.

Usage: python scripts/rendering/test_cbuffer_layouts.py [--keep | --list]
"""
from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DXC = os.path.join(REPO, 'runtime', 'thirdparty', 'dxc', 'bin', 'x64', 'dxc.exe')
SHADERS = os.path.join(REPO, 'runtime', 'shaders', 'runtime')
FIXTURES = os.path.join(REPO, 'scripts', 'rendering', 'test_cbuffer_layouts_fixtures')
CPP_CACHE = {}

ENTRIES = [
    {
        'name': 'VFXBillboardCB (velocity VS)', 'shader': 'vfx/particle_billboard_vs.hlsl', 'profile': 'vs_6_6',
        'header': 'runtime/engine/renderer/passes/vfx/particle_render_pass.h', 'struct': 'VFXBillboardCB',
        'map': {'renderSize': 'renderWidth', 'jitterPixels': 'jitterX'},
    },
    {
        'name': 'VFXBillboardCB (velocity PS)', 'shader': 'vfx/particle_billboard_ps.hlsl', 'profile': 'ps_6_6',
        'header': 'runtime/engine/renderer/passes/vfx/particle_render_pass.h', 'struct': 'VFXBillboardCB',
        'map': {'renderSize': 'renderWidth', 'jitterPixels': 'jitterX'},
    },
    {
        'name': 'VelocityObjectCB', 'shader': 'postprocess/velocity_object_vs.hlsl', 'profile': 'vs_6_6',
        'header': 'runtime/engine/renderer/passes/postprocess/velocity_pass.h', 'struct': 'VelocityObjectCB',
        'map': {'model': 'model', 'prevModel': 'prevModel', 'unjitteredViewProj': 'unjitteredViewProj',
                'prevViewProj': 'prevViewProj', 'screenSize': 'screenSize', '_velocityObjectPad': 'pad'},
    },
    {
        'name': 'VelocityObjectCB (pixel copy)', 'shader': 'postprocess/velocity_object_ps.hlsl', 'profile': 'ps_6_6',
        'header': 'runtime/engine/renderer/passes/postprocess/velocity_pass.h', 'struct': 'VelocityObjectCB',
        'map': {'screenSize': 'screenSize'},
    },
    {
        'name': 'VolumetricFogCB', 'shader': 'atmosphere/volumetric_fog_inject_cs.hlsl', 'profile': 'cs_6_6',
        'header': 'runtime/engine/renderer/passes/atmosphere/volumetric_fog_pass.h', 'struct': 'VolumetricFogCB',
        'map': {'invViewProj': 'invViewProj', 'cameraPos': 'cameraPosX', 'nearZ': 'nearZ', 'farZ': 'farZ',
                'globalDensity': 'globalDensity', 'heightFalloff': 'heightFalloff', 'heightOffset': 'heightOffset',
                'albedo': 'albedoR', 'phaseG': 'phaseG', 'sunDir': 'sunDirX', 'sunIntensity': 'sunIntensity',
                'sunColor': 'sunColorR', 'temporalAlpha': 'temporalAlpha', 'inscatteringUavIdx': 'inscatteringUavIdx',
                'inscatteringSrvIdx': 'inscatteringSrvIdx', 'integratedUavIdx': 'integratedUavIdx',
                'integratedSrvIdx': 'integratedSrvIdx', 'depthSrvIdx': 'depthSrvIdx', 'frameIndex': 'frameIndex',
                'gridX': 'gridX', 'gridY': 'gridY', 'gridZ': 'gridZ', 'extinctionScale': 'extinctionScale',
                'emissive': 'emissiveR', 'emissiveR': 'emissiveR', 'emissiveG': 'emissiveG', 'emissiveB': 'emissiveB',
                'fogStartDistance': 'fogStartDistance', 'nearFadeInDistance': 'nearFadeInDistance',
                'cameraFarZ': 'cameraFarZ', 'sunShadowEnabled': 'sunShadowEnabled'},
        'before': 'volumetric_fog_inject_cs.before.hlsl',
    },
    {
        'name': 'VolumetricFogCB (scatter copy)', 'shader': 'atmosphere/volumetric_fog_scatter_cs.hlsl', 'profile': 'cs_6_6',
        'header': 'runtime/engine/renderer/passes/atmosphere/volumetric_fog_pass.h', 'struct': 'VolumetricFogCB',
        'map': {'emissiveR': 'emissiveR', 'fogStartDistance': 'fogStartDistance', 'nearFadeInDistance': 'nearFadeInDistance',
                'cameraFarZ': 'cameraFarZ', 'gridZ': 'gridZ', 'invViewProj': 'invViewProj'},
    },
    {
        'name': 'VolumetricFogCB (composite copy)', 'shader': 'atmosphere/volumetric_fog_composite_ps.hlsl', 'profile': 'ps_6_6',
        'header': 'runtime/engine/renderer/passes/atmosphere/volumetric_fog_pass.h', 'struct': 'VolumetricFogCB',
        'map': {'_emissiveR': 'emissiveR', '_emissive': 'emissiveR', 'fogStartDistance': 'fogStartDistance',
                'nearFadeInDistance': 'nearFadeInDistance', 'cameraFarZ': 'cameraFarZ', '_integratedSrvIdx': 'integratedSrvIdx',
                'nearZ': 'nearZ', 'sunShadowEnabled': 'sunShadowEnabled', 'debugMode': 'debugMode'},
    },
    {
        'name': 'HeightFogCB', 'shader': 'atmosphere/height_fog_ps.hlsl', 'profile': 'ps_6_6',
        'header': 'runtime/engine/renderer/core/params/constant_buffers.h', 'struct': 'HeightFogCB',
        'map': {'fogColor': 'fogColor', 'fogDensity': 'fogDensity', 'fogHeightFalloff': 'fogHeightFalloff',
                'fogBaseHeight': 'fogBaseHeight', 'fogMaxOpacity': 'fogMaxOpacity', 'fogStartDistance': 'fogStartDistance',
                'fogCutoffDistance': 'fogCutoffDistance', 'maxObserverHeight': 'maxObserverHeight', 'rawDensity': 'rawDensity',
                'secondFogDensity': 'secondFogDensity', 'secondFogHeightFalloff': 'secondFogHeightFalloff',
                'secondFogRawDensity': 'secondFogRawDensity', 'secondFogBaseHeight': 'secondFogBaseHeight',
                'directionalInscatteringColor': 'directionalInscatteringColor',
                'directionalInscatteringExponent': 'directionalInscatteringExponent',
                'directionalInscatteringStartDistance': 'directionalInscatteringStartDistance',
                'skyAtmContribColorScale': 'skyAtmContribColorScale', 'heightFogContribution': 'heightFogContribution',
                'skyLightColorR': 'skyLightColorR', 'skyLightColorG': 'skyLightColorG', 'skyLightColorB': 'skyLightColorB',
                'sunIllumPostTransR': 'sunIllumPostTransR', 'sunIllumPostTransA': 'sunIllumPostTransA',
                'sunDirX': 'sunDirX', 'sunDirY': 'sunDirY', 'sunDirZ': 'sunDirZ', '_sunPad': '_sunPad'},
        'before': 'height_fog_ps.before.hlsl',
    },
    {
        'name': 'CloudCB', 'shader': 'atmosphere/cloud_raymarch_cs.hlsl', 'profile': 'cs_6_6',
        'header': 'runtime/engine/renderer/passes/atmosphere/cloud_pass.cpp', 'struct': 'CloudCB',
        'map': {'g_invViewProj': 'invViewProj', 'g_cameraPos': 'cameraPos', 'g_time': 'time', 'g_sunDir': 'sunDir',
                'g_coverage': 'coverage', 'g_sunColor': 'sunColor', 'g_density': 'density', 'g_ambientColor': 'ambientColor',
                'g_extinction': 'extinction', 'g_scatteringAlbedo': 'scatteringAlbedo', 'g_phaseG': 'phaseG',
                'g_phaseG2': 'phaseG2', 'g_phaseBlend': 'phaseBlend', 'g_layerBottomKm': 'layerBottomKm',
                'g_layerHeightKm': 'layerHeightKm', 'g_windVelocity': 'windVelocity', 'g_windVelocityX': 'windVelocity',
                'g_detailWindScale': 'detailWindScale', 'g_ambientMultiplier': 'ambientMultiplier',
                'g_primarySteps': 'primarySteps', 'g_lightSteps': 'lightSteps', 'g_shapeNoiseSrv': 'shapeNoiseSrv',
                'g_globalCoverage': 'globalCoverage', 'g_cloudType': 'cloudType', 'g_tracingMaxDistanceKm': 'tracingMaxDistanceKm',
                'g_shadowTracingDistance': 'shadowTracingDistance',
                'g_stopTracingTransmittanceThreshold': 'stopTracingTransmittanceThreshold',
                'g_skyLightCloudBottomOcclusion': 'skyLightCloudBottomOcclusion',
                'g_tracingMaxDistanceMode': 'tracingMaxDistanceMode', 'g_tracingStartMaxDistance': 'tracingStartMaxDistance',
                'g_tracingStartDistanceFromCamera': 'tracingStartDistanceFromCamera', 'g_detailNoiseSrv': 'detailNoiseSrv',
                'g_minStepSize': 'minStepSize', 'g_maxStepSize': 'maxStepSize',
                'g_perspectiveStepScale': 'perspectiveStepScale', 'g_stbnSrvIndex': 'stbnSrvIndex',
                'g_frameIndex': 'frameIndex', 'g_proceduralNoise': 'proceduralNoise',
                'g_noiseXYScale': 'noiseXYScale', 'g_noiseZScale': 'noiseZScale',
                'g_shapeOctaves': 'shapeOctaves', 'g_fbmDecay': 'fbmDecay', 'g_fbmLacunarity': 'fbmLacunarity',
                'g_detailErosion': 'detailErosion', 'g_densityScale': 'densityScale', 'g_jitterScale': 'jitterScale',
                'g_frontDepthUav': 'frontDepthUav'},
        'before': 'cloud_raymarch_cs.before.hlsl',
    },
    {
        'name': 'ShadowDataCB (control, matched before this item)', 'shader': 'lighting/deferred_lighting_ps.hlsl', 'profile': 'ps_6_6',
        'header': 'runtime/engine/renderer/core/params/constant_buffers.h', 'struct': 'ShadowDataCB',
        'map': {'cascadeViewProj': 'cascadeViewProj', 'shadowAtlasIndex': 'shadowAtlasIndex', 'cascadeCount': 'cascadeCount',
                'localShadowAtlasIndex': 'localShadowAtlasIndex', 'localShadowCount': 'localShadowCount',
                'localShadowRaw': 'localShadows'},
        # The struct text alone does not compile: the nested struct and the array bound come from the same header.
        'extra_structs': ['LocalLightShadowData'], 'extra_constants': ['kMaxLocalLightShadows'],
    },
    {
        'name': 'LightingCB (block B.7: 512 point / 128 spot lights and the cluster parameters)', 'shader': 'lighting/deferred_lighting_ps.hlsl',
        'profile': 'ps_6_6', 'header': 'runtime/engine/renderer/core/params/constant_buffers.h', 'struct': 'LightingCB',
        'map': {'pointLights': 'pointLights', 'pointLightCount': 'pointLightCount', 'spotLightCount': 'spotLightCount', 'spotLights': 'spotLights',
                'clusterForward': 'clusterForwardX', 'clusterNear': 'clusterNear', 'clusterFar': 'clusterFar', 'clusterLogScale': 'clusterLogScale',
                'clusterTileSize': 'clusterTileSize', 'clusterTilesX': 'clusterTilesX', 'clusterTilesY': 'clusterTilesY', 'clusterSlices': 'clusterSlices',
                'clusterListSrv': 'clusterListSrv', 'clusterListUav': 'clusterListUav', 'clusterSlots': 'clusterSlots', 'clusterFlags': 'clusterFlags',
                'clusterViewWidth': 'clusterViewWidth', 'clusterViewHeight': 'clusterViewHeight'},
        'extra_structs': ['PointLightData', 'SpotLightData'],
        'extra_constants': ['kMaxPointLights', 'kMaxSpotLights', 'kClusterTileSize', 'kClusterSlices', 'kClusterSlotsMax', 'kClusterRecordUints',
                            'kClusterFlag_Enabled', 'kClusterFlag_Debug'],
    },
]

# Entries below reflect the literal production declaration in isolation. Referencing every
# member prevents DXC's dead-code elimination from hiding unused positional mirror fields.
PARAMS = 'runtime/engine/renderer/core/params/'
PASSES = 'runtime/engine/renderer/passes/'
ALIASES = {
    'PerFrameCB': {
        '_viewProj': 'viewProj', '_time': 'timeParams',
        '_transmittanceLutSrv': 'transmittanceLutSrvIndex', '_apBottomRadius': 'apBottomRadiusKm',
        '_apTopRadius': 'apTopRadiusKm', '_apRayBetaX': 'apRayleighBetaX',
        '_apRayBetaY': 'apRayleighBetaY', '_apRayBetaZ': 'apRayleighBetaZ',
        '_apRayScaleH': 'apRayleighScaleH', '_apMieAniso': 'apMieAnisotropy',
        '_apSunI': 'apSunIntensity', '_apRayScale': 'apRayleighScale',
        '_gtaoSrv': 'gtaoSrvIndex', '_filmGrain': 'filmGrainIntensity',
        '_vignette': 'vignetteIntensity', '_chromaAb': 'chromaAbIntensity',
        '_contactShadowLen': 'contactShadowLength', '_ssgiAO': 'ssgiAOSrvIndex',
        '_ssgiEn': 'ssgiEnabled', '_rcEnabled': 'radianceCacheEnabled',
        '_rcProbesSrv': 'rcProbeSHBufferSrv', '_rcExtent': 'rcGridExtent',
        '_rcOriginX': 'rcGridOriginX', '_rcOriginY': 'rcGridOriginY', '_rcOriginZ': 'rcGridOriginZ',
        '_reflProbeCount': 'reflectionProbeCount', '_reflProbeSrv': 'reflectionProbeBufferSrvIndex',
        '_colorSat': 'colorSaturation', '_skip4': 'colorContrast', '_skipA': 'envMisAccumulatedSrv',
        'brixGIDiffuseSrv': 'brixelizerGIDiffuseSrv', '_ddgiIrradSrv': 'ddgiIrradianceSrv',
        '_padSdfRaw': 'sdfSampleRotation',
        '_camPos': 'cameraPos', '_ambient': 'ambientColor', '_near': 'nearPlane', '_far': 'farPlane',
        '_irr': 'irradianceSrvIndex', '_pre': 'prefilteredSrvIndex', '_brdf': 'brdfLutSrvIndex',
        '_mips': 'prefilteredMipLevels', '_skyInt': 'skyLightIntensity', '_skyCR': 'skyLightColorR',
        '_skyCG': 'skyLightColorG', '_skyCB': 'skyLightColorB', '_skyMin': 'skyLightMinAmbient',
        '_sh': 'skyIrradianceSH', '_sDbg': 'shadowDebugMode', '_apEn': 'aerialPerspectiveEnabled',
        '_tLut': 'transmittanceLutSrvIndex', '_apBot': 'apBottomRadiusKm', '_apTop': 'apTopRadiusKm',
        '_rBX': 'apRayleighBetaX', '_rBY': 'apRayleighBetaY', '_rBZ': 'apRayleighBetaZ',
        '_rH': 'apRayleighScaleH', '_mB': 'apMieBeta', '_mH': 'apMieScaleH',
        '_mA': 'apMieAnisotropy', '_sI': 'apSunIntensity', '_rS': 'apRayleighScale', '_mS': 'apMieScale',
        '_tcs': 'toneCurveSlope', '_tct': 'toneCurveToe', '_tcsh': 'toneCurveShoulder',
        '_tcbc': 'toneCurveBlackClip', '_tcwc': 'toneCurveWhiteClip', '_gtao': 'gtaoSrvIndex',
        '_brdfDbg': 'brdfDebugMode', '_fogCont': 'heightFogContribution', '_contact': 'contactShadowLength',
        '_ssrSrv': 'ssrSrvIndex', '_ssgiSrv': 'ssgiSrvIndex', '_rcEn': 'radianceCacheEnabled',
        '_rcSH': 'rcProbeSHBufferSrv', '_rcP': 'rcProbesPerAxis', '_rcE': 'rcGridExtent',
        '_rcOX': 'rcGridOriginX', '_rcOY': 'rcGridOriginY', '_rcOZ': 'rcGridOriginZ', '_rcI': 'rcGiIntensity',
        '_apVol': 'apVolumeSrvIndex', '_rpCnt': 'reflectionProbeCount', '_rpBuf': 'reflectionProbeBufferSrvIndex',
        'rcProbeSrv': 'rcProbeSHBufferSrv', 'rcExtent': 'rcGridExtent', 'rcOrigin': 'rcGridOriginX',
        'rcIntensity': 'rcGiIntensity', 'sdfSkySrv': 'sdfReflectionSkySrv',
        'sdfClipmaps': 'sdfReflectionClipmaps', 'sdfClipmapSrv': 'sdfReflectionClipmapSrv',
        'sdfTraceDistance': 'sdfReflectionTraceDistance', 'sdfVoxelSize': 'sdfReflectionVoxelSize',
        'sdfTraceSteps': 'sdfReflectionTraceSteps',
        '_perFramePad0': 'irradianceSrvIndex', '_perFramePad1': 'prefilteredSrvIndex',
        '_perFramePad2': 'brdfLutSrvIndex', '_perFramePad3': 'prefilteredMipLevels',
        '_perFramePad4': 'skyLightIntensity', '_perFramePad5': 'skyLightColorR',
        '_perFramePad6': 'skyLightColorG', '_perFramePad7': 'skyLightColorB',
    },
    'PerObjectCB': {
        'obj_model': 'model', 'obj_normalMatrix': 'normalMatrix', '_model': 'model', '_nmat': 'normalMatrix',
        'emissiveColor': 'emissiveColorR', '_ec': 'emissiveColorR', '_eInt': 'emissiveIntensity',
        'baseColorTint': 'baseColorTintR', '_rMul': 'roughnessMultiplier', '_mMul': 'metallicMultiplier',
        '_nInt': 'normalIntensity', 'uvScale': 'uvScaleX', '_us': 'uvScaleX',
        'uvOffset': 'uvOffsetX', '_uo': 'uvOffsetX', '_aoI': 'aoIntensity', 'matOpacity': 'opacity',
        '_t0': 'albedoIndex', '_t1': 'normalIndex', '_t2': 'roughnessIndex',
        '_t3': 'metallicIndex', '_t4': 'aoIndex', '_n': 'normalIndex', '_r': 'roughnessIndex',
        '_m': 'metallicIndex', '_a': 'aoIndex', '_pad': 'emissiveIndex',
        'glassThickness': 'displacementScale', 'sceneColorSrvIndex': 'heightmapSrvIndex',
    },
}


def layout_entry(shader, cbuffer, header, struct=None, aliases=None, **kwargs):
    return dict(name=cbuffer, shader=shader, profile='cs_6_6', header=header,
                struct=struct or cbuffer, aliases=aliases or {}, isolated=True, **kwargs)


TIER_A = []
for shader in (
    'gbuffer/gbuffer_ps.hlsl', 'gbuffer/mesh_vs.hlsl', 'gbuffer/mesh_ps.hlsl',
    'gbuffer/translucent_ps.hlsl', 'lighting/deferred_lighting_ps.hlsl',
    'postprocess/tonemap_ps.hlsl', 'postprocess/velocity_ps.hlsl', 'ssgi/ssgi_surface.hlsli',
    'ssgi/ssgi_trace_cs.hlsl', 'ssgi/ssgi_spatial_denoise_cs.hlsl',
    'ssgi/ssgi_temporal_accum_cs.hlsl', 'ssgi/ssgi_upsample_cs.hlsl',
    'vfx/decal_forward_ps.hlsl', 'vfx/particle_billboard_ps.hlsl', 'vfx/particle_billboard_vs.hlsl',
    'vfx/particle_mesh_ps.hlsl', 'vfx/particle_mesh_vs.hlsl', 'vfx/trail_ps.hlsl', 'vfx/trail_vs.hlsl',
    'postprocess/ssr_composite_cs.hlsl', 'postprocess/ssr_resolve_cs.hlsl',
    'postprocess/ssr_temporal_cs.hlsl', 'postprocess/ssr_trace_cs.hlsl',
    'postprocess/svgf_atrous_cs.hlsl', 'postprocess/svgf_filter_moments_cs.hlsl',
    'postprocess/velocity_object_vs.hlsl', 'pathtracing/path_tracer_ps.hlsl',
    'pathtracing/pathtracer_shared.hlsli',
    'lighting/cluster_assign_cs.hlsl', 'lighting/env_mis_temporal_cs.hlsl',
    'debug/debug_view_ps.hlsl', 'atmosphere/height_fog_ps.hlsl',
    'atmosphere/skybox_cubemap_ps.hlsl', 'atmosphere/sky_atmosphere_ps.hlsl',
):
    TIER_A.append(layout_entry(shader, 'PerFrameCB', PARAMS + 'constant_buffers.h'))
for shader in ('gbuffer/gbuffer_ps.hlsl', 'gbuffer/mesh_vs.hlsl', 'gbuffer/mesh_ps.hlsl',
               'gbuffer/translucent_ps.hlsl', 'lighting/shadow_depth_vs.hlsl'):
    TIER_A.append(layout_entry(shader, 'PerObjectCB', PARAMS + 'constant_buffers.h',
                               before='gbuffer_ps.perobject.before.hlsl' if shader == 'gbuffer/gbuffer_ps.hlsl' else None))
TIER_A += [
    layout_entry('vfx/particle_spawn_cs.hlsl', 'VFXSpawnConstants', PASSES + 'vfx/particle_simulation_pass.h',
                 'ParticleSpawnConstants', {'_pad3': '_padUser'}),
    layout_entry('vfx/particle_update_cs.hlsl', 'VFXUpdateConstants', PASSES + 'vfx/particle_simulation_pass.h',
                 'ParticleUpdateConstants'),
    layout_entry('vfx/particle_indirect_args_cs.hlsl', 'VFXIndirectArgsConstants',
                 PASSES + 'vfx/particle_simulation_pass.h', 'ParticleIndirectArgsConstants'),
    layout_entry('vfx/particle_mesh_instance_cs.hlsl', 'VFXMeshInstanceConstants',
                 'runtime/engine/renderer/frame/frame_particles.cpp'),
    layout_entry('vfx/decal_deferred_vs.hlsl', 'DecalConstants', PASSES + 'vfx/decal_pass.h'),
    layout_entry('atmosphere/transmittance_lut_cs.hlsl', 'OzoneParams',
                 PASSES + 'atmosphere/transmittance_lut_pass.h', 'TransmittanceLutOzoneCB',
                 {'ozoneAbsorption': 'absorptionR', 'ozoneLayerWidth': 'layerWidth',
                  'ozoneLinTerm0': 'linTerm0', 'ozoneConstTerm0': 'constTerm0',
                  'ozoneLinTerm1': 'linTerm1', 'ozoneConstTerm1': 'constTerm1'}),
    *[layout_entry(shader, 'VFXBillboardCB', PASSES + 'vfx/particle_render_pass.h',
                   aliases={'emitterPosition': 'emitterPositionX', 'globalColorTint': 'globalColorTintR',
                            'colorLifeStart': 'colorLifeStartR', 'colorLifeEnd': 'colorLifeEndR',
                            '_cbsFlipbookFPS': 'flipbookFPS', '_cbsSpriteChannelMask': 'spriteChannelMask',
                            '_cbsVfxTotalTime': 'vfxTotalTime', '_cbsPad4': '_vfxBillboardPad1',
                            'renderSize': 'renderWidth', 'jitterPixels': 'jitterX'})
      for shader in ('vfx/particle_billboard_vs.hlsl', 'vfx/particle_billboard_ps.hlsl')],
    layout_entry('lighting/shadow_depth_vs.hlsl', 'ShadowCB', PARAMS + 'shadow_pass_params.h',
                 'ShadowCascadeParams', {'shadowViewProj': 'viewProj', 'shadowLightPosition': '_pad[0]',
                                         'shadowLightRadius': '_pad[3]'}),
    layout_entry('lighting/shadow_depth_linear_ps.hlsl', 'ShadowCB', PASSES + 'lighting/shadow_pass.cpp',
                 'ShadowCB', {'shadowViewProj': 'viewProj', 'shadowLightPosition': 'lightPosX',
                             'shadowLightRadius': 'lightRadius'}),
    layout_entry('lighting/shadow_depth_vs.hlsl', 'ShadowCB', PASSES + 'lighting/shadow_pass.cpp',
                 'ShadowCB', {'shadowViewProj': 'viewProj', 'shadowLightPosition': 'lightPosX',
                                         'shadowLightRadius': 'lightRadius'}),
    layout_entry('postprocess/velocity_ps.hlsl', 'VelocityCB', PASSES + 'postprocess/velocity_pass.h',
                 aliases={'screenSize': 'screenWidth', '_velPad': '_pad0'}),
    layout_entry('atmosphere/sky_atmosphere_ps.hlsl', 'SkyAtmosphereCB', PARAMS + 'constant_buffers.h'),
    layout_entry('atmosphere/aerial_perspective_volume_cs.hlsl', 'APVolumeCB',
                 PASSES + 'atmosphere/aerial_perspective_volume_pass.h',
                 aliases={'cameraPosWorld': 'cameraPosX', 'sunDirIntensity': 'sunDirX'}),
    layout_entry('atmosphere/cloud_temporal_cs.hlsl', 'CloudTemporalCB', PASSES + 'atmosphere/cloud_pass.cpp',
                 aliases={'cameraPosKm': 'cameraPos', '_padding': 'padding'}),
    *[layout_entry(shader, 'ShadowCB', PASSES + 'atmosphere/cloud_pass.cpp', 'CloudShadowCB',
                   {'shadowInvViewProj': 'invViewProj', 'depthRangeKm': 'depthRange',
                    'previousDepthRangeKm': 'previousDepthRange'})
      for shader in ('atmosphere/cloud_shadow_cs.hlsl', 'atmosphere/cloud_shadow_resolve_cs.hlsl')],
    *[layout_entry(shader, 'DecalObjectCB', PASSES + 'geometry/decal_pass.h', aliases={'decalWorld': 'world'})
      for shader in ('gbuffer/decal_vs.hlsl', 'gbuffer/decal_ps.hlsl')],
    layout_entry('postprocess/taa_resolve_cs.hlsl', 'TaaCB', PASSES + 'postprocess/taa_upscaler.h',
                 aliases={'jitterOffset': 'jitterX', 'texelSize': 'texelSizeX',
                          'currentFrameWeight': 'blendWeight', 'sceneSrvIndex': 'sceneSrv',
                          'depthSrvIndex': 'depthSrv', 'historySrvIndex': 'historySrv', 'velocitySrvIndex': 'velocitySrv'}),
    layout_entry('ssgi/ssgi_temporal_accum_cs.hlsl', 'HistoryCB', 'runtime/engine/renderer/gi/ssgi_renderer.cpp',
                 aliases={'g_regionCount': 'regionCount', '_regionPad': '_pad', 'g_regionLo': 'regionLo', 'g_regionHi': 'regionHi'}),
    *[layout_entry(shader, 'TraceConstants', 'runtime/engine/renderer/gi/radiance_cache.h',
                   'RadianceCacheTraceConstants',
                   {'_shBufferUav': 'shBufferUav', '_firstWrite': 'firstWrite',
                    'ddgiRayRadianceSrv': 'shBufferUav', 'ddgiRayInfoSrv': 'firstWrite',
                    '_historyShiftPad': 'regionCount'}, extra_constants=['kMaxFieldRegions'])
      for shader in ('gi/gi_trace_common.hlsli', 'gi/ddgi_probe_common.hlsli',
                     'gi/probe_reach_cs.hlsl', 'gi/radiance_cache_trace_cs.hlsl',
                     'gi/ddgi_probe_trace_cs.hlsl', 'gi/ddgi_probe_update_cs.hlsl',
                     'pathtracing/path_tracer_ps.hlsl')],
    layout_entry('vfx/vfx_camera.hlsli', 'VfxCameraCB', 'runtime/engine/vfx/data_interfaces/vfx_camera_di.h',
                 'VfxCameraConstants', {'vfxCameraView': 'viewMatrix', 'vfxCameraProj': 'projMatrix',
                 'vfxCameraPos': 'cameraPos', '_vfxCameraPadPos': '_padPos', 'vfxCameraForward': 'cameraFwd',
                 'vfxCameraFovY': 'fovY', 'vfxCameraNearZ': 'nearZ', 'vfxCameraFarZ': 'farZ',
                 'vfxCameraAspect': 'aspect', '_vfxCameraPadTail': '_padTail',
                 'vfxCameraScreenSize': 'screenSize', '_vfxCameraPadScreen': '_padScreen'}),
    layout_entry('postprocess/ssr_temporal_cs.hlsl', 'SsrTemporalPrevVP',
                 'runtime/engine/renderer/frame/frame_gi_passes.cpp'),
    layout_entry('../editor/gizmo_vs.hlsl', 'GizmoCB', 'runtime/editor/gizmo/gizmo_renderer.h'),
    layout_entry('../editor/grid_ps.hlsl', 'GridCB', 'runtime/editor/grid/editor_grid.cpp'),
    layout_entry('../editor/outline_detect_ps.hlsl', 'OutlineCB', 'runtime/editor/outline/outline_pass.cpp',
                 aliases={'outlineColor': 'colorR', 'texelSize': 'texelSizeX', '_pad': '_pad0'}),
    *[layout_entry(shader, 'OverlayCB', 'runtime/editor/font/text_renderer.cpp',
                   aliases={'invScreenSize': 'invScreenSizeX', 'padding': 'padding'})
      for shader in ('../editor/overlay_vs.hlsl', '../editor/overlay_rrect_vs.hlsl')],
]
ENTRIES.append({**next(e for e in ENTRIES if e['struct'] == 'CloudCB'),
                'shader': 'atmosphere/cloud_raymarch_upscale_cs.hlsl', 'before': None})
TIER_A += [
    layout_entry('atmosphere/cloud_composite_ps.hlsl', 'PerFrameCB', PARAMS + 'constant_buffers.h'),
    layout_entry('atmosphere/cloud_composite_ps.hlsl', 'CloudCompositeCB',
                 PASSES + 'atmosphere/cloud_pass.cpp', 'CloudCB',
                 {'g_invViewProj': 'invViewProj', 'g_cameraPos': 'cameraPos'}),
]

TIER_B = [
    layout_entry('pathtracing/nrd_common.hlsli', 'NrdRoot',
                 PASSES + 'pathtracing/nrd_denoiser.h'),
    layout_entry('pathtracing/nrd_signal.hlsli', 'NrdSignalRoot',
                 PASSES + 'pathtracing/nrd_denoiser.h', aliases={
                     'nrdEnabled': 'enabled', 'nrdDiffuseUav': 'diffuseUav',
                     'nrdFractionUav': 'fractionUav', 'nrdAlbedoUav': 'albedoUav'}),
    # Private DXR/ReSTIR root: 96 bytes / 24 DWORDs, including history UAVs,
    # sample count and bounce/RR controls. Shared graphics and PerFrameCB do not grow.
    layout_entry('pathtracing/pathtracer_shared.hlsli', 'PathTracerRoot',
                 PASSES + 'pathtracing/pipeline_path_tracer_pass.h', 'PipelinePathRoot'),
    layout_entry('pathtracing/pathtracer_volume.hlsli', 'PathVolumeRoot',
                 PASSES + 'pathtracing/pipeline_path_tracer_pass.h'),
    *[layout_entry(shader, 'VFXBillboardConstants', PASSES + 'vfx/particle_render_pass.h',
                   'ParticleBillboardIndices')
      for shader in ('vfx/particle_billboard_vs.hlsl', 'vfx/particle_billboard_ps.hlsl')],
    *[layout_entry(shader, 'VFXMeshRenderConstants', PASSES + 'vfx/particle_mesh_render_pass.h',
                   'ParticleMeshPassConstants')
      for shader in ('vfx/particle_mesh_vs.hlsl', 'vfx/particle_mesh_ps.hlsl')],
    layout_entry('atmosphere/transmittance_lut_cs.hlsl', 'AtmosphereLutParams',
                 PASSES + 'atmosphere/transmittance_lut_pass.h', 'TransmittanceLutRootConstants'),
    layout_entry('atmosphere/aerial_perspective_volume_cs.hlsl', 'AtmosphereParams',
                 PASSES + 'atmosphere/aerial_perspective_volume_pass.h', 'APVolumeRootConstants'),
    *[layout_entry(shader, cb, PASSES + 'postprocess/gtao_pass.h', cpp)
      for shader, cb, cpp in (
          ('postprocess/gtao_cs.hlsl', 'GTAORootCB', 'GTAORootConstants'),
          ('postprocess/gtao_denoise_cs.hlsl', 'DenoiseParams', 'GTAODenoiseRootConstants'),
          ('postprocess/gtao_temporal_cs.hlsl', 'GTAOTemporalRootCB', 'GTAOTemporalRootConstants'),
          ('postprocess/gtao_upsample_ps.hlsl', 'GTAOUpsampleRootCB', 'GTAOUpsampleRootConstants'))],
    *[layout_entry('postprocess/' + shader + '.hlsl', cb, PASSES + 'postprocess/bloom_pass.cpp', cpp)
      for shader, cb, cpp in (
          ('bloom_threshold_cs', 'BloomThresholdParams', 'BloomThresholdConstants'),
          ('bloom_downsample_cs', 'BloomDownsampleParams', 'BloomDownsampleConstants'),
          ('bloom_upsample_cs', 'BloomUpsampleParams', 'BloomUpsampleConstants'))],
    layout_entry('compute/hiz_pyramid_cs.hlsl', 'HiZConstants', PASSES + 'postprocess/hiz_pyramid_pass.cpp', 'HiZRootConstants'),
    layout_entry('compute/decal_apply_cs.hlsl', 'DecalApplyConstants', PASSES + 'geometry/decal_pass.h'),
    *[layout_entry('postprocess/' + shader + '.hlsl', 'RootConstants', PASSES + 'postprocess/svgf_pass.cpp', cpp)
      for shader, cpp in (('svgf_reproject_cs', 'ReprojectRC'), ('svgf_filter_moments_cs', 'FilterMomRC'),
                          ('svgf_atrous_cs', 'AtrousRC'))],
    *[layout_entry('postprocess/ssr_' + shader + '_cs.hlsl', cb,
                   'runtime/engine/renderer/frame/frame_gi_passes.cpp', cpp)
      for shader, cb, cpp in (
          ('tile_classify', 'SsrClassifyRootCB', 'SsrClassifyConstants'),
          ('prepare_indirect', 'SsrPrepareRootCB', 'SsrPrepareConstants'),
          ('trace', 'SsrTraceRootCB', 'SsrTraceConstants'),
          ('resolve', 'SsrResolveRootCB', 'SsrResolveConstants'),
          ('temporal', 'SsrTemporalRootCB', 'SsrTemporalConstants'),
          ('spatial_denoise', 'SsrDenoiseRootCB', 'SsrDenoiseConstants'))],
    layout_entry('vfx/particle_sort_cs.hlsl', 'VFXSortConstants', 'runtime/engine/vfx/core/gpu_particle_sort.h'),
    layout_entry('postprocess/exposure_histogram_cs.hlsl', 'ExposureParams', PASSES + 'postprocess/auto_exposure.h',
                 'HistogramRootConstants'),
    layout_entry('postprocess/exposure_average_cs.hlsl', 'ExposureParams', PASSES + 'postprocess/auto_exposure.h',
                 'AverageRootConstants'),
]

TIER_C = [
    layout_entry('include/light_bvh.hlsli', 'LightBVHNode',
                 PASSES + 'pathtracing/light_bvh.h', 'LightBVHNode'),
    layout_entry('include/light_bvh.hlsli', 'LightBVHLight',
                 PASSES + 'pathtracing/light_bvh.h', 'LightBVHLight'),
    layout_entry('include/light_bvh.hlsli', 'LightBVHConfig',
                 PASSES + 'pathtracing/light_bvh.h', 'LightBVHConfig'),
    layout_entry('include/restir_di.hlsli', 'LightSample',
                 PASSES + 'pathtracing/pipeline_path_tracer_pass.h', 'RestirLightSample'),
    {**layout_entry('include/restir_di.hlsli', 'Reservoir',
                   PASSES + 'pathtracing/pipeline_path_tracer_pass.h', 'RestirReservoir'),
     'hlsl_structs': ('LightSample',), 'extra_structs': ('RestirLightSample',)},
    layout_entry('include/restir_di.hlsli', 'RestirSurface',
                 PASSES + 'pathtracing/pipeline_path_tracer_pass.h', 'RestirSurface'),
    layout_entry('pathtracing/pathtracer_shared.hlsli', 'PathDirectionalLight',
                 PASSES + 'pathtracing/pipeline_path_tracer_pass.h', 'PipelinePathDirectionalLight'),
    layout_entry('pathtracing/pathtracer_shared.hlsli', 'MaterialData',
                 PASSES + 'pathtracing/pipeline_path_tracer_pass.h', 'PipelinePathMaterial'),
    layout_entry('gbuffer/mesh_vs.hlsl', 'Vertex', 'runtime/engine/renderer/mesh/vertex.h'),
    layout_entry('gbuffer/mesh_vs.hlsl', 'VertexPacked', 'runtime/engine/renderer/mesh/vertex.h'),
    layout_entry('gi/global_sdf_composite_cs.hlsl', 'MeshSdf',
                 'runtime/engine/renderer/gi/global_distance_field.h', 'GdfMeshSdfGPU'),
    layout_entry('gi/gi_trace_common.hlsli', 'GIMeshMaterial',
                 PASSES + 'pathtracing/path_tracer_pass.h', 'GIMeshMaterialGPU'),
]

# These are not successful reflection contracts. Keep explicit exclusions visible
# rather than manufacturing HLSL definitions from C++ sizes.
UNTRACKED = {
    ("runtime/vfx/decal_forward_ps.hlsl", "DecalObjectCB"): "Additional DecalObjectCB mirror; independent CPU mapping still required (not claimed as a passing contract).",
    ("runtime/vfx/distortion_haze_ps.hlsl", "DistortionHazeCB"): "Root constants: DistortionHazeCB; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/vfx/flipbook_debug_ps.hlsl", "FlipbookDebugCB"): "Root constants: FlipbookDebugCB; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/vfx/particle_death_spawn_cs.hlsl", "VFXDeathSpawnConstants"): "Additional VFXDeathSpawnConstants mirror; independent CPU mapping still required (not claimed as a passing contract).",
    ("runtime/vfx/trail_ps.hlsl", "TrailRootCB"): "Root constants: TrailRootCB; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/vfx/trail_vs.hlsl", "TrailRootCB"): "Root constants: TrailRootCB; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/vfx/vfx_volumetric_raymarch_ps.hlsl", "VolumetricRaymarchCB"): "Root constants: VolumetricRaymarchCB; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/vfx/vfx_volumetric_splat_cs.hlsl", "VolumetricSplatCB"): "Root constants: VolumetricSplatCB; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/ssgi/prev_color_reduce_cs.hlsl", "RootConstants"): "Root constants: RootConstants; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/ssgi/ssgi_spatial_denoise_cs.hlsl", "RootConstants"): "Root constants: RootConstants; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/ssgi/ssgi_temporal_accum_cs.hlsl", "RootConstants"): "Root constants: RootConstants; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/ssgi/ssgi_trace_cs.hlsl", "RootConstants"): "Root constants: RootConstants; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/ssgi/ssgi_upsample_cs.hlsl", "RootConstants"): "Root constants: RootConstants; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/postprocess/cas_sharpen_ps.hlsl", "RootConstants"): "Root constants: RootConstants; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/postprocess/flip_cs.hlsl", "RootConstants"): "Root constants: RootConstants; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/postprocess/fxaa_ps.hlsl", "RootConstants"): "Root constants: RootConstants; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/postprocess/ssr_composite_cs.hlsl", "SsrCompositeRootCB"): "Root constants: SsrCompositeRootCB; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/postprocess/tonemap_ps.hlsl", "RootConstants"): "Root constants: RootConstants; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/postprocess/velocity_object_vs.hlsl", "push"): "Root constants: PushConstants; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/postprocess/velocity_ps.hlsl", "DepthIndex"): "Root constants: DepthIndex; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/pathtracing/path_tracer_ps.hlsl", "PathTracerRC"): "Root constants: PathTracerRC; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/lighting/deferred_lighting_ps.hlsl", "gBufIndices"): "Root constants: GBufferIndices; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/lighting/env_mis_temporal_cs.hlsl", "RootConstants"): "Root constants: RootConstants; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/lighting/shadow_depth_vs.hlsl", "push"): "Root constants: PushConstants; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/ibl/brdf_lut_cs.hlsl", "RootConstants"): "Root constants: RootConstants; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/ibl/envmap_luminance_pyramid_cs.hlsl", "RootConstants"): "Root constants: RootConstants; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/ibl/equirect_to_cubemap_cs.hlsl", "RootConstants"): "Root constants: RootConstants; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/ibl/irradiance_cs.hlsl", "RootConstants"): "Root constants: RootConstants; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/ibl/pmrem_to_cubemap_cs.hlsl", "RootConstants"): "Root constants: RootConstants; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/ibl/prefilter_cs.hlsl", "RootConstants"): "Root constants: RootConstants; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/gi/ddgi_border_fill_cs.hlsl", "RootConstants"): "Root constants: RootConstants; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/gi/ddgi_probe_trace_cs.hlsl", "RootConstants"): "Root constants: RootConstants; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/gi/ddgi_probe_update_cs.hlsl", "RootConstants"): "Root constants: RootConstants; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/gi/global_sdf_composite_cs.hlsl", "RootConstants"): "Root constants: RootConstants; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/gi/global_sdf_composite_cs.hlsl", "MeshConstants"): "Additional MeshConstants mirror; independent CPU mapping still required (not claimed as a passing contract).",
    ("runtime/gi/mesh_sdf_bake_cs.hlsl", "BakeConstants"): "Root constants: BakeConstants; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/gi/probe_reach_cs.hlsl", "RootConstants"): "Root constants: RootConstants; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/gi/probe_side_cs.hlsl", "RootConstants"): "Root constants: RootConstants; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/gi/probe_side_cs.hlsl", "MeshConstants"): "Additional MeshConstants mirror; independent CPU mapping still required (not claimed as a passing contract).",
    ("runtime/gi/radiance_cache_trace_cs.hlsl", "RootConstants"): "Root constants: RootConstants; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/gbuffer/gbuffer_ps.hlsl", "MaterialParamsCB"): "Material-graph generated parameter payload; layout depends on the graph rather than a fixed C++ struct.",
    ("runtime/gbuffer/mesh_vs.hlsl", "push"): "Root constants: PushConstants; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/debug/debug_view_ps.hlsl", "RootConstants"): "Root constants: RootConstants; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/compute/gpu_skinning_cs.hlsl", "push"): "Root constants: PushConstants; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/atmosphere/cloud_composite_ps.hlsl", "CompositeIndices"): "Root constants: CompositeIndices; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/atmosphere/cloud_raymarch_cs.hlsl", "RootConstants"): "Root constants: RootConstants; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/atmosphere/cloud_raymarch_upscale_cs.hlsl", "RootConstants"): "Root constants: RootConstants; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/atmosphere/god_rays_cs.hlsl", "RootConstants"): "Root constants: RootConstants; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/atmosphere/height_fog_ps.hlsl", "fogIndices"): "Root constants: FogIndices; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/atmosphere/skybox_cubemap_ps.hlsl", "skyboxIndices"): "Root constants: SkyboxIndices; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/atmosphere/sky_atmosphere_ps.hlsl", "skyIndices"): "Root constants: SkyIndices; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/atmosphere/sky_cubemap_cs.hlsl", "SkyCubemapParams"): "Root constants: SkyCubemapParams; uploader-specific block, not a header-backed Tier B contract.",
    ("runtime/atmosphere/volumetric_fog_composite_ps.hlsl", "FogCompositeIndices"): "Root constants: FogCompositeIndices; uploader-specific block, not a header-backed Tier B contract.",
    ("editor/outline_detect_ps.hlsl", "OutlineRootCB"): "Root constants: OutlineRootCB; uploader-specific block, not a header-backed Tier B contract.",
    ("editor/overlay_msdf_ps.hlsl", "RootConstants"): "Root constants: RootConstants; uploader-specific block, not a header-backed Tier B contract.",
    ("editor/overlay_rrect_vs.hlsl", "RootConstants"): "Root constants: RootConstants; uploader-specific block, not a header-backed Tier B contract.",
    ("editor/overlay_sprite_ps.hlsl", "RootConstants"): "Root constants: RootConstants; uploader-specific block, not a header-backed Tier B contract.",
    ("editor/overlay_text_ps.hlsl", "RootConstants"): "Root constants: RootConstants; uploader-specific block, not a header-backed Tier B contract.",
    ("editor/overlay_vs.hlsl", "RootConstants"): "Root constants: RootConstants; uploader-specific block, not a header-backed Tier B contract.",
}

UNTRACKED_CONTRACTS = {
    'CullConstants/GpuCullConstants': 'compute/gpu_cull_cs.hlsl:1 is an empty kernel with no cbuffer; F-018 stage 2 owns implementation; CPU size asserted in engine/renderer/passes/geometry/gpu_cull_pass.h:70',
    'InstanceData': 'instanced vertex shader is unimplemented (F-018); CPU-only transform upload in engine/renderer/frame/frame_sync.cpp:76-102; 128-byte size asserted in engine/renderer/core/instance_batcher.h:42',
    'LightGPU': 'no same-named HLSL element; production lighting uses PointLightData/SpotLightData CB arrays (Tier A); legacy multi-light CPU descriptor size is asserted at engine/renderer/core/lighting/multi_light.h:92',
    'ReflectionProbeGPU': 'no same-named HLSL element; CPU probe records have two float4 rows, 32-byte size asserted at engine/renderer/core/params/constant_buffers.h:873 and upload stride set in engine/renderer/probes/reflection_probe_renderer.cpp:201',
    'SdfObjectGpuData': 'CPU atlas-manager object records have no HLSL consumer under runtime/shaders; 192-byte size asserted at engine/renderer/shadows/sdf_types.h:198 and upload stride set in sdf_atlas_manager.cpp:90',
    'SkinnedVertex': 'raw ByteAddressBuffer contract, not a structured element: compute/gpu_skinning_cs.hlsl:39-45 pins stride 56 and offsets 0/12/24/32/48/52; CPU size asserted at engine/renderer/mesh/vertex.h:139',
}

STUBS = r'''
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
namespace DirectX { struct XMFLOAT4X4 { float m[4][4]; }; }
struct Vec3 { float x, y, z; };
struct Vec2 { float x, y; };
struct uint2 { uint32_t x, y; };
struct ShaderParameterMetadata;
struct Vec4 { float x, y, z, w; };
struct Mat4 { DirectX::XMFLOAT4X4 m; };
static_assert(sizeof(Vec4) == 16 && sizeof(Mat4) == 64, "stand-ins");
#define UZ_OFFSET(F) std::printf("%s %zu\n", #F, offsetof(STRUCT, F));
'''

OFFSET_RE = re.compile(r'^;\s+(?:(?:row_major|column_major)\s+)?[\w\d<>]+\s+(\w+)(?:\[\d+\])?;\s*;\s*Offset:\s*(\d+)', re.M)


def dxc_offsets(shader_path: str, profile: str, cbuffer: str) -> dict[str, int]:
    # The fixtures live outside the shader tree: their relative includes resolve through the
    # original directory given as an include path.
    out = subprocess.run([DXC, '-T', profile, '-E', 'main', '-D', 'UZ_BINDLESS=1', '-I', SHADERS,
                          *[arg for directory in ('atmosphere', 'lighting', 'compute', 'postprocess', 'ssgi', 'gi', 'vfx', 'gbuffer')
                            for arg in ('-I', os.path.join(SHADERS, directory))], shader_path],
                         capture_output=True, text=True, errors='replace', timeout=30)
    text = out.stdout + out.stderr
    start = text.find(f'; cbuffer {cbuffer}\n')
    if start < 0:
        start = text.find(f'; cbuffer {cbuffer}\r\n')
    if start < 0:
        raise SystemExit(f'{shader_path}: cbuffer {cbuffer} not in the DXC output (compile error?)\n{text[-1500:]}')
    end = text.find('; }', start)
    block = text[start:end]
    return {m.group(1): int(m.group(2)) for m in OFFSET_RE.finditer(block)}


def struct_text(header_path: str, struct: str) -> str:
    src = open(header_path, encoding='utf-8', errors='replace').read()
    # Anchored at the start of a line: the doc comment of PointLightData / SpotLightData quotes the
    # HLSL struct ("/// struct PointLightData {"), which an unanchored search picked up first (B.7).
    m = re.search(r'^[ \t]*struct\s+(?:alignas\(\d+\)\s+)?' + struct + r'\s*\{.*?\n[ \t]*\};', src, re.S | re.M)
    if not m:
        raise SystemExit(f'{header_path}: struct {struct} not found')
    return m.group(0)


def constant_text(header_path: str, name: str) -> str:
    src = open(header_path, encoding='utf-8', errors='replace').read()
    m = re.search(r'(?:static\s+)?constexpr\s+[\w:]+\s+' + name + r'\s*=\s*[^;]+;', src)
    if not m:
        raise SystemExit(f'{header_path}: constant {name} not found')
    return m.group(0)


def cpp_offsets(header_path: str, struct: str, fields: list[str], workdir: str,
                extra_structs: list[str] = (), extra_constants: list[str] = ()) -> dict[str, int]:
    key = (header_path, struct, tuple(extra_structs), tuple(extra_constants))
    cached = CPP_CACHE.setdefault(key, {})
    fields = [field for field in fields if field not in cached]
    if not fields:
        return cached
    text = ''.join(constant_text(header_path, c) + '\n' for c in extra_constants)
    text += ''.join(struct_text(header_path, e) + '\n' for e in extra_structs)
    text += struct_text(header_path, struct)
    body = STUBS + 'namespace uz {\n' + text + '\n}\n' + f'using STRUCT = uz::{struct};\n' + 'int main() {\n'
    body += ''.join('    std::printf("__size %zu\\n", sizeof(STRUCT));\n' if f == '__size'
                    else f'    UZ_OFFSET({f})\n' for f in fields) + '    return 0;\n}\n'
    src = os.path.join(workdir, f'{struct}_offsets.cpp')
    exe = os.path.join(workdir, f'{struct}_offsets.exe')
    open(src, 'w', encoding='utf-8').write(body)
    r = subprocess.run(['zig', 'c++', '-std=c++17', src, '-o', exe], capture_output=True, text=True, timeout=60)
    if r.returncode != 0:
        raise SystemExit(f'cannot compile the C++ side of {struct}:\n{r.stderr[-2000:]}')
    out = subprocess.run([exe], capture_output=True, text=True, timeout=10).stdout
    cached.update({line.split()[0]: int(line.split()[1]) for line in out.splitlines() if line.strip()})
    return cached


def compare(entry: dict, shader_path: str, workdir: str):
    if entry.get('isolated'):
        return compare_isolated(entry, shader_path, workdir)
    hl = dxc_offsets(shader_path, entry['profile'], entry['name'].split(' ')[0])
    wanted = sorted({c for h, c in entry['map'].items() if h in hl})
    cpp = cpp_offsets(os.path.join(REPO, entry['header']), entry['struct'], wanted, workdir,
                      entry.get('extra_structs', ()), entry.get('extra_constants', ()))
    mismatches = []
    checked = 0
    for h, c in entry['map'].items():
        if h not in hl:
            continue
        checked += 1
        if hl[h] != cpp[c]:
            mismatches.append(f'{h}: HLSL {hl[h]} vs C++ {c} {cpp[c]}')
    return checked, mismatches


def declaration(source, kind, name):
    source = re.sub(r'/\*.*?\*/|//[^\n]*', '', source, flags=re.S)
    match = re.search(r'\b' + kind + r'\s+' + re.escape(name) + r'\b[^{}]*\{', source)
    if not match:
        raise ValueError(f'{kind} {name} not found')
    start, depth, end = match.end() - 1, 1, match.end()
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end] + ';'


def members(text):
    """The scalar/vector/matrix declarations used by the layout probes."""
    result = []
    for statement in text[text.index('{') + 1:text.rindex('}')].split(';'):
        statement = statement.strip()
        if not statement:
            continue
        match = re.fullmatch(r'(?:(?:row_major|column_major)\s+)?(\w+)\s+(.+)', statement, re.S)
        if not match:
            raise ValueError(f'unrecognised member declaration: {statement}')
        typ, declarators = match.groups()
        for item in declarators.split(','):
            field = re.match(r'\s*(\w+)((?:\s*\[\d+\])*)', item)
            if not field:
                raise ValueError(f'unrecognised declarator: {item}')
            name, dimensions = field.groups()
            expression = name + '[0]' * dimensions.count('[')
            if re.fullmatch(r'(?:float|uint|int)\d+x\d+', typ):
                expression += '[0][0]'
            elif re.fullmatch(r'(?:float|uint|int)\d+', typ):
                expression += '.x'
            result.append((name, expression))
    return result


def reflect_declaration(shader_path, name, workdir, kind='cbuffer', extra_structs=()):
    source_text = Path(shader_path).read_text(encoding='utf-8')
    try:
        text = declaration(source_text, kind, name)
    except ValueError:
        # Entry-point shaders may obtain their mirror from a shared include. DXC
        # resolves the real include graph, using the same bindless build variant.
        preprocessed = os.path.join(workdir, 'entry.preprocessed.hlsl')
        out = subprocess.run([DXC, '-P', preprocessed, '-D', 'UZ_BINDLESS=1', '-I', SHADERS,
                              *[arg for directory in ('atmosphere', 'lighting', 'compute', 'postprocess',
                                                      'ssgi', 'gi', 'vfx', 'gbuffer')
                                for arg in ('-I', os.path.join(SHADERS, directory))], shader_path],
                             capture_output=True, text=True, timeout=30)
        if out.returncode:
            raise ValueError(f'{shader_path}: DXC include preprocessing failed:\n{out.stderr}')
        text = declaration(Path(preprocessed).read_text(encoding='utf-8'), kind, name)
    if '#' in text:
        source = os.path.join(workdir, 'conditional.hlsl')
        preprocessed = os.path.join(workdir, 'conditional.preprocessed.hlsl')
        Path(source).write_text(text, encoding='utf-8')
        out = subprocess.run([DXC, '-P', preprocessed, '-D', 'UZ_BINDLESS=1', source],
                             capture_output=True, text=True, timeout=30)
        if out.returncode:
            raise ValueError(f'{shader_path}: DXC preprocessing failed:\n{out.stderr}')
        text = re.sub(r'^\s*#.*$', '', Path(preprocessed).read_text(encoding='utf-8'), flags=re.M)
    field_names = members(text)
    dependencies = []
    for dependency in extra_structs:
        definition = declaration(source_text, 'struct', dependency)
        first_member = members(definition)[0][1]
        nested = re.findall(r'\b' + re.escape(dependency) + r'\s+(\w+)\s*;', text)
        field_names = [(field, expr + '.' + first_member if field in nested else expr)
                       for field, expr in field_names]
        dependencies.append(definition)
    text = '\n'.join(dependencies) + '\n' + text
    if kind == 'struct':
        text += f'\nStructuredBuffer<{name}> elements : register(t0);\n'
        expression = ' + '.join('float(elements[id].' + expr + ')' for _, expr in field_names)
    else:
        expression = ' + '.join('float(' + expr + ')' for _, expr in field_names)
    text += f'\nfloat4 main(uint id : SV_PrimitiveID) : SV_Target {{ return ({expression}).xxxx; }}\n'
    probe = os.path.join(workdir, 'declaration.hlsl')
    Path(probe).write_text(text, encoding='utf-8')
    out = subprocess.run([DXC, '-T', 'ps_6_6', '-E', 'main', probe], capture_output=True,
                         text=True, errors='replace', timeout=30)
    if out.returncode:
        raise ValueError(f'{shader_path}: DXC failed:\n{out.stderr}')
    offsets = {m.group(1): int(m.group(2)) for m in OFFSET_RE.finditer(out.stdout)}
    size_pattern = r'\}\s+\$Element;\s*;\s*Offset:\s*0\s+Size:\s*(\d+)' if kind == 'struct' else r'\}\s+' + name + r';\s*;\s*Offset:\s*0\s+Size:\s*(\d+)'
    size = re.search(size_pattern, out.stdout)
    if not size:
        raise ValueError(f'{shader_path}: DXC size missing:\n{out.stdout[:3000]}')
    return field_names, offsets, int(size.group(1))


def compare_isolated(entry, shader_path, workdir):
    declared, hl, _ = reflect_declaration(shader_path, entry['name'], workdir)
    header_path = os.path.join(REPO, entry['header'])
    cpp_text = struct_text(header_path, entry['struct'])
    aliases = {**ALIASES.get(entry['name'], {}), **entry['aliases']}
    mapping = {}
    for name, _ in declared:
        candidate = aliases.get(name, name)
        if not re.search(r'\b' + re.escape(candidate.split('[')[0]) + r'\b', cpp_text):
            candidate = aliases.get(name, name.lstrip('_'))
        if not re.search(r'\b' + re.escape(candidate.split('[')[0]) + r'\b', cpp_text):
            raise ValueError(f'{entry["shader"]} {entry["name"]}.{name}: explicit C++ alias required')
        mapping[name] = candidate
    cpp = cpp_offsets(header_path, entry['struct'], sorted(set(mapping.values())), workdir,
                      entry.get('extra_structs', ()), entry.get('extra_constants', ()))
    return len(mapping), [f'{h}: HLSL {hl[h]} vs C++ {c} {cpp[c]}'
                          for h, c in mapping.items() if hl[h] != cpp[c]]


def compare_size(entry, workdir, kind='cbuffer'):
    _, _, reflected = reflect_declaration(os.path.join(SHADERS, entry['shader']), entry['name'], workdir, kind,
                                         entry.get('hlsl_structs', ()))
    cpp = cpp_offsets(os.path.join(REPO, entry['header']), entry['struct'], ['__size'], workdir,
                      entry.get('extra_structs', ()))
    return reflected, cpp['__size']


def inventory(list_only=False):
    root = Path(REPO) / 'runtime/shaders'
    pattern = re.compile(r'(?:\bcbuffer\s+(\w+)|\bConstantBuffer\s*<\s*(\w+)\s*>\s*(\w+))'
                         r'(?:\s*:\s*register\s*\(\s*(b\d+)(?:\s*,[^)]*)?\))?')
    mapped = {(os.path.relpath(os.path.join(SHADERS, e['shader']), root).replace('\\', '/'),
               e['name'].split(' ')[0]) for e in ENTRIES + TIER_A + TIER_B}
    mapped.update({('runtime/lighting/light_types.hlsli', 'LightingCB'),
                   ('runtime/lighting/shadow.hlsli', 'ShadowDataCB')})
    failures = 0
    count = 0
    mapped_count = 0
    excluded_count = 0
    for path in sorted(root.rglob('*')):
        if path.suffix not in ('.hlsl', '.hlsli'):
            continue
        source = path.read_text(encoding='utf-8', errors='replace')
        source = re.sub(r'/\*.*?\*/|//[^\n]*',
                        lambda m: '\n' * m.group(0).count('\n'), source, flags=re.S)
        for match in pattern.finditer(source):
            name = match[1] or match[3]
            key = (path.relative_to(root).as_posix(), name)
            line = source[:match.start()].count('\n') + 1
            reason = UNTRACKED.get(key, '')
            tracked = key in mapped
            mapped_count += tracked
            excluded_count += not tracked and bool(reason.strip())
            failures += not tracked and not reason.strip()
            count += 1
            if list_only or not tracked and not reason.strip():
                print(f'{key[0]}:{line} {name} register({match[4] or "implicit"}) '
                      + ('MAPPED' if tracked else f'UNTRACKED: {reason}' if reason else 'UNMAPPED'))
    failures += sum(not reason.strip() for reason in UNTRACKED.values())
    for name, reason in UNTRACKED_CONTRACTS.items():
        failures += not reason.strip()
        if list_only:
            print(f'Contract {name} UNTRACKED: {reason}')
    print(f'Inventory: {count} declarations; {mapped_count} mapped, {excluded_count} explicit exclusions; '
          f'{failures} unmapped/unlisted or reasonless')
    return failures


def main():
    if '--list' in sys.argv:
        return int(inventory(True) != 0)
    keep = '--keep' in sys.argv
    workdir = tempfile.mkdtemp(prefix='cbuffer_layouts_')
    failures = inventory()
    passed_a = 0
    for entry in ENTRIES + TIER_A:
        try:
            checked, mismatches = compare(entry, os.path.join(SHADERS, entry['shader']), workdir)
            ok = checked > 0 and not mismatches
            failures += not ok
            passed_a += ok
            print(f'  [{"PASS" if ok else "FAIL"}] Tier A {entry["shader"]} {entry["name"]}: {checked} fields'
                  + (f'; {"; ".join(mismatches)}' if mismatches else ''), flush=True)
            if entry.get('before'):
                _, before = compare(entry, os.path.join(FIXTURES, entry['before']), workdir)
                neg = len(before) > 0
                failures += not neg
                print(f'  [{"PASS" if neg else "FAIL"}] negative control {entry["before"]}: {len(before)} fields differ'
                      + (f' ({before[0]})' if before else ''), flush=True)
        except (ValueError, SystemExit, subprocess.TimeoutExpired) as error:
            failures += 1
            print(f'  [FAIL] Tier A {entry["shader"]} {entry["name"]}: {error}', flush=True)
    cull = layout_entry('', 'CullConstants', PASSES + 'geometry/gpu_cull_pass.h', 'GpuCullConstants',
                        {'g_HiZMipSrvIndices': 'hizMipSrvIndices'})
    _, mismatches = compare(cull, os.path.join(FIXTURES, 'gpu_cull_cs.before.hlsl'), workdir)
    failures += not mismatches
    print(f'  [{"PASS" if mismatches else "FAIL"}] negative control gpu_cull_cs.before.hlsl: '
          f'{len(mismatches)} fields differ ({mismatches})', flush=True)
    for tier, entries, kind in (('B', TIER_B, 'cbuffer'), ('C', TIER_C, 'struct')):
        passed = 0
        for entry in entries:
            try:
                reflected, cpp = compare_size(entry, workdir, kind)
                ok = reflected == cpp
                failures += not ok
                passed += ok
                print(f'  [{"PASS" if ok else "FAIL"}] Tier {tier} {entry["shader"]} {entry["struct"]}: '
                      f'DXC {reflected} bytes ({reflected / 4:g} DWORDs), sizeof {cpp}', flush=True)
            except (ValueError, SystemExit, subprocess.TimeoutExpired) as error:
                failures += 1
                print(f'  [FAIL] Tier {tier} {entry["struct"]}: {error}', flush=True)
        print(f'Tier {tier}: {passed}/{len(entries)} passing entries')
    for name, reason in UNTRACKED_CONTRACTS.items():
        print(f'  [UNTRACKED] {name}: {reason}')
    if not keep:
        import shutil
        shutil.rmtree(workdir, ignore_errors=True)
    print(f'Tier A: {passed_a}/{len(ENTRIES) + len(TIER_A)} passing entries')
    print(f'{"all layouts agree" if failures == 0 else str(failures) + " failures"}')
    return 0 if failures == 0 else 1


if __name__ == '__main__':
    raise SystemExit(main())
