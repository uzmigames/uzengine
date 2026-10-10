"""Constant-buffer layouts: the HLSL side (DXC's layout comments) against the C++ side (`offsetof`
of the struct text cut from its header and compiled with `zig c++`). Lighting block B, item 4,
gate F1. HLSL packs a `float3` or an array element into 16-byte rows, so a C++ struct of scalars
and a shader declaring `float3`s at 4-byte-aligned offsets silently disagree; three buffers did.

Each entry: the shader (profile) that declares the cbuffer, the cbuffer name, the header and the
struct, and the map from HLSL field names to C++ field names (a vector's first component). Every
mapped field must have the same offset on both sides. The pre-fix shaders kept under
`scripts/rendering/test_cbuffer_layouts_fixtures/*.before.hlsl` are the negative control: at least one of
their fields must differ.

Usage: python scripts/rendering/test_cbuffer_layouts.py [--keep]
"""
from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DXC = os.path.join(REPO, 'runtime', 'thirdparty', 'dxc', 'bin', 'x64', 'dxc.exe')
SHADERS = os.path.join(REPO, 'runtime', 'shaders', 'runtime')
FIXTURES = os.path.join(REPO, 'scripts', 'rendering', 'test_cbuffer_layouts_fixtures')

ENTRIES = [
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
                'g_tracingStartDistanceFromCamera': 'tracingStartDistanceFromCamera', 'g_detailNoiseSrv': 'detailNoiseSrv'},
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

STUBS = r'''
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
namespace DirectX { struct XMFLOAT4X4 { float m[4][4]; }; }
struct Vec3 { float x, y, z; };
struct Vec4 { float x, y, z, w; };
struct Mat4 { DirectX::XMFLOAT4X4 m; };
static_assert(sizeof(Vec4) == 16 && sizeof(Mat4) == 64, "stand-ins");
#define UZ_OFFSET(F) std::printf("%s %zu\n", #F, offsetof(STRUCT, F));
'''

OFFSET_RE = re.compile(r'^;\s+(?:row_major\s+)?[\w\d<>]+\s+(\w+)(?:\[\d+\])?;\s*;\s*Offset:\s*(\d+)', re.M)


def dxc_offsets(shader_path: str, profile: str, cbuffer: str) -> dict[str, int]:
    # The fixtures live outside the shader tree: their relative includes resolve through the
    # original directory given as an include path.
    out = subprocess.run([DXC, '-T', profile, '-E', 'main', '-D', 'UZ_BINDLESS=1', '-I', SHADERS,
                          '-I', os.path.join(SHADERS, 'atmosphere'), '-I', os.path.join(SHADERS, 'lighting'), shader_path],
                         capture_output=True, text=True, errors='replace')
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
    m = re.search(r'^struct\s+(?:alignas\(\d+\)\s+)?' + struct + r'\s*\{.*?\n\};', src, re.S | re.M)
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
    text = ''.join(constant_text(header_path, c) + '\n' for c in extra_constants)
    text += ''.join(struct_text(header_path, e) + '\n' for e in extra_structs)
    text += struct_text(header_path, struct)
    body = STUBS + 'namespace uz {\n' + text + '\n}\n' + f'using STRUCT = uz::{struct};\n' + 'int main() {\n'
    body += ''.join(f'    UZ_OFFSET({f})\n' for f in fields) + '    return 0;\n}\n'
    src = os.path.join(workdir, f'{struct}_offsets.cpp')
    exe = os.path.join(workdir, f'{struct}_offsets.exe')
    open(src, 'w', encoding='utf-8').write(body)
    r = subprocess.run(['zig', 'c++', '-std=c++17', src, '-o', exe], capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f'cannot compile the C++ side of {struct}:\n{r.stderr[-2000:]}')
    out = subprocess.run([exe], capture_output=True, text=True).stdout
    return {line.split()[0]: int(line.split()[1]) for line in out.splitlines() if line.strip()}


def compare(entry: dict, shader_path: str, workdir: str):
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


def main():
    keep = '--keep' in sys.argv
    workdir = tempfile.mkdtemp(prefix='cbuffer_layouts_')
    failures = 0
    for entry in ENTRIES:
        checked, mismatches = compare(entry, os.path.join(SHADERS, entry['shader']), workdir)
        ok = checked > 0 and not mismatches
        failures += 0 if ok else 1
        print(f'  [{"PASS" if ok else "FAIL"}] {entry["name"]}: {checked} fields' + (f'; {"; ".join(mismatches)}' if mismatches else ''))
        if entry.get('before'):
            _, before = compare(entry, os.path.join(FIXTURES, entry['before']), workdir)
            neg = len(before) > 0
            failures += 0 if neg else 1
            print(f'  [{"PASS" if neg else "FAIL"}] {entry["name"]} negative control (pre-fix shader): {len(before)} fields differ'
                  + (f' ({before[0]} ...)' if before else ''))
    if not keep:
        import shutil
        shutil.rmtree(workdir, ignore_errors=True)
    print(f'{"all layouts agree" if failures == 0 else str(failures) + " failures"}')
    return 0 if failures == 0 else 1


if __name__ == '__main__':
    raise SystemExit(main())
