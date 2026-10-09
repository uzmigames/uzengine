"""Froxel volumetric fog — gates of lighting block B, item 4 (correctness and sun shadowing).

  F2  froxel placement: the inject's `FroxelWorldPos` / `SliceToDepth`, cut from the HLSL and compiled
      on the CPU with a reversed-Z perspective camera like Sponza's (near 5, far 10000, fov 58.5), put
      every froxel at a view-space depth equal to its slice depth (|dz| <= 1e-3 relative, slices 0..63,
      centre and corner columns). Negative control: the pre-fix shader kept under
      scripts/test_cbuffer_layouts_fixtures/ places them within 1 m of the camera.
  F2b the composite linearises the reversed-Z depth correctly: with `r.VolumetricFog.Debug 1` the composite
      writes (view depth cm, slice coordinate, transmittance) instead of the fog; over the shaft scene's floor
      pixels the written depth matches the view depth of the dump's world position (median relative error
      <= 3 %, the HDR target's float precision). Negative control: `LinearizeDepthReversedZ` of
      depth_common.hlsli (the standard-Z formula, side finding) evaluated on the CPU for the same pixels
      returns ~nearZ for every one (median error >= 90 %).
  F3  the sun is applied once: the inject multiplies the sun colour by the phase and the shadow only
      (no `sunColor * sunIntensity`), and the host writes `sunIntensity = 1` (source anchors).
  F4a sun shadow in Sponza: Sponza with the fog (`scripts/sponza-fog.uzproject`, uniform fog, extinction
      sigma = density x extinctionScale per cm) rendered with the fog off, on with `r.VolumetricFog.SunShadow 1`
      and on with 0; frame-wide `components` dump at frame 60; floor pixels (|z| < 5 cm) at 8-25 m
      classified sun-shadowed by their `direct_light` (< 5 % of the floor's maximum). Per pixel the fog
      in-scatter is L = taa_output(on) - taa_output(off) * T, with T the composite's own transmittance
      (debug capture). Gate: over shadowed floor mean(L_shadow1 / L_shadow0) <= 0.6. The sun-lit floor is
      reported only: in Sponza a lit floor pixel says nothing about the air on the ray to it (the first
      run measured 0.347 lit against 0.351 shadowed), so the lit control moved to the shaft scene.
  F4b-d sun shadow against an analytic shadow volume: `scripts/fog_shaft_reference.py` (floor, one wall,
      sun at 40 deg, uniform fog), the same three captures. The scatter loop is ported to the CPU per
      pixel (every 4th pixel): I0 = integral of a unit source, Ivis = of the source times the wall's
      analytic sun visibility at each slice's froxel centre on the pixel's ray, read at the surface depth
      as the composite reads the integrated volume; the transmittance T that extracts the in-scatter from
      the captures is the composite's own (debug capture); f = Ivis / I0; predicted ratio
      = a + (1 - a) f with the ambient fraction a from the inject's constants (0.3 / 4pi against
      HG(g = 0.2) x |sun| = 10); the fitted a is reported.
      F4b rays the model finds fully lit (f = 1) keep their in-scatter: mean ratio >= 0.9.
      F4c rays with f <= 0.5: mean |ratio - predicted| <= 0.1.
      F4d the analytic footprint agrees with the rendered floor shadow (`direct_light` classes) on >= 95 %
          of the classified floor pixels: the sun convention the model assumes.
  F5  cost: FroxelFog_Inject + FroxelFog_Scatter + FroxelFog_Composite <= 1.5 ms at 1920x1080 (manifest).
  F6a no change without fog, by construction: every shader that includes `shadow.hlsli` (other than the
      fog's own) compiles to the same DXIL with the committed (HEAD) `shadow.hlsli` and with the working
      copy, on a copy of the shader tree compiled twice from the same path.
  F6b no change without fog, measured: the Sponza Camera C ROI means against the previous build's
      (`.screenshots/flicker-elimination/test-sdf-bake-cache/camera-c-warm`, same project, no fog entity)
      differ by no more than max(1 %, 2 x the largest known run-to-run spread): this build's repeat pair
      and the previous build's cold / warm pair (its sdf_bake_cache.json). Exact equality was the first
      pre-registration; the previous build's own two runs already differed by 0.5-1.1 % (the async SDF
      arrival changes the GI history), so it could never hold.

Usage: python scripts/test_froxel_fog.py [--out DIR] [--reuse] [--skip-camera-c]
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import regiondump  # noqa: E402
import runner  # noqa: E402
import run_probe_side_sponza as sponza  # noqa: E402
import fog_shaft_reference as shaft  # noqa: E402

REPO = runner.REPO
FOG_PROJECT = os.path.join(REPO, 'scripts', 'sponza-fog.uzproject')
INJECT = os.path.join(REPO, 'runtime', 'shaders', 'runtime', 'atmosphere', 'volumetric_fog_inject_cs.hlsl')
INJECT_BEFORE = os.path.join(REPO, 'scripts', 'test_cbuffer_layouts_fixtures', 'volumetric_fog_inject_cs.before.hlsl')
HOST = os.path.join(REPO, 'runtime', 'engine', 'renderer', 'frame', 'frame_atmosphere_passes.cpp')
COMPONENT = os.path.join(REPO, 'runtime', 'engine', 'ecs', 'environment', 'height_fog_component.h')
W, H = 1920, 1080
DUMP_FRAME = 60
REC709 = np.array([0.2126, 0.7152, 0.0722])
F4_SHADOW_MAX, F4_LIT_TOL, F5_MAX_MS = 0.6, 0.10, 1.5
FLOOR_Z_TOL, DIST_MIN, DIST_MAX = 5.0, 800.0, 2500.0
SHAFT_PROJECT = shaft.PROJECT
SHAFT_DIST_MIN, SHAFT_DIST_MAX, STRIDE = 300.0, 3000.0, 4
F4_LIT_MIN, F4_MODEL_MAE, F4_FOOTPRINT_AGREE = 0.9, 0.10, 0.95
F6_FLOOR, F6_SPREAD_MULT = 0.01, 2.0
F2B_MEDIAN_TOL, F2B_CONTROL_MIN = 0.03, 0.90
SHADERS = os.path.join(REPO, 'runtime', 'shaders', 'runtime')
DXC = os.path.join(REPO, 'runtime', 'thirdparty', 'dxc', 'bin', 'x64', 'dxc.exe')
B03_JSON = os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-sdf-bake-cache', 'sdf_bake_cache.json')

CPU_PRELUDE = r'''
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <algorithm>
typedef unsigned int uint;
struct float2 { float x, y; float2() : x(0), y(0) {} float2(float a, float b) : x(a), y(b) {} };
struct float3 { float x, y, z; float3() : x(0), y(0), z(0) {} float3(float a, float b, float c) : x(a), y(b), z(c) {} };
struct float4 { float x, y, z, w; float4() : x(0), y(0), z(0), w(0) {} float4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
                float4(float2 v, float c, float d) : x(v.x), y(v.y), z(c), w(d) {} float4(float3 v, float d) : x(v.x), y(v.y), z(v.z), w(d) {}
                float3 xyz() const { return float3(x, y, z); } };
struct float4x4 { float m[4][4]; };
struct uint2 { uint x, y; uint2(uint a, uint b) : x(a), y(b) {} };
static float2 operator+(float2 a, float b) { return float2(a.x + b, a.y + b); }
static float2 operator*(float2 a, float b) { return float2(a.x * b, a.y * b); }
static float2 operator-(float2 a, float b) { return float2(a.x - b, a.y - b); }
static float2 operator/(float2 a, float2 b) { return float2(a.x / b.x, a.y / b.y); }
static float3 operator+(float3 a, float3 b) { return float3(a.x + b.x, a.y + b.y, a.z + b.z); }
static float3 operator-(float3 a, float3 b) { return float3(a.x - b.x, a.y - b.y, a.z - b.z); }
static float3 operator*(float3 a, float b) { return float3(a.x * b, a.y * b, a.z * b); }
static float3 operator/(float3 a, float b) { return float3(a.x / b, a.y / b, a.z / b); }
static float dot(float3 a, float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static float length(float3 a) { return std::sqrt(dot(a, a)); }
static float3 normalize(float3 a) { return a / length(a); }
static float saturate(float v) { return std::min(1.0f, std::max(0.0f, v)); }
static float max(float a, float b) { return std::max(a, b); }
static float min(float a, float b) { return std::min(a, b); }
static float max(float a, double b) { return std::max(a, (float)b); }
static float4 mul(float4 v, const float4x4& m) {   // row vector times row_major matrix, as HLSL mul(float4, float4x4)
    float4 r; r.x = v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0] + v.w * m.m[3][0];
    r.y = v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1] + v.w * m.m[3][1];
    r.z = v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2] + v.w * m.m[3][2];
    r.w = v.x * m.m[0][3] + v.y * m.m[1][3] + v.z * m.m[2][3] + v.w * m.m[3][3]; return r; }
#define float2u2(v) float2(float((v).x), float((v).y))
// the cbuffer scalars the extracted functions read
static float4x4 invViewProj; static float3 cameraPos; static float nearZ, farZ, fogStartDistance; static uint gridX = 160, gridY = 90, gridZ = 64;
'''

CPU_MAIN = r'''
// Reversed-Z perspective (near 5, far 10000, fov 58.5 deg, 16:9) looking down +y from the origin, z up.
static void Invert(const float4x4& a, float4x4& out) {
    double m[4][8];
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) { m[i][j] = a.m[i][j]; m[i][j + 4] = (i == j) ? 1.0 : 0.0; }
    for (int c = 0; c < 4; ++c) {
        int p = c; for (int r = c + 1; r < 4; ++r) if (std::fabs(m[r][c]) > std::fabs(m[p][c])) p = r;
        for (int j = 0; j < 8; ++j) std::swap(m[c][j], m[p][j]);
        double d = m[c][c]; for (int j = 0; j < 8; ++j) m[c][j] /= d;
        for (int r = 0; r < 4; ++r) if (r != c) { double f = m[r][c]; for (int j = 0; j < 8; ++j) m[r][j] -= f * m[c][j]; }
    }
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) out.m[i][j] = float(m[i][j + 4]);
}
static float4x4 Mul(const float4x4& a, const float4x4& b) { float4x4 r; for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) { float s = 0; for (int k = 0; k < 4; ++k) s += a.m[i][k] * b.m[k][j]; r.m[i][j] = s; } return r; }
int main() {
    const float n = 5.0f, f = 10000.0f, fov = 58.51f * 3.14159265f / 180.0f, aspect = 16.0f / 9.0f;
    // view: camera at (86, 881, 159) looking along -y (toward the atrium), z up; row-vector convention.
    cameraPos = float3(86.0f, 881.0f, 159.0f);
    float3 fwd = normalize(float3(-0.2f, -1.0f, -0.3f)), up(0, 0, 1);
    float3 right = normalize(float3(fwd.y * up.z - fwd.z * up.y, fwd.z * up.x - fwd.x * up.z, fwd.x * up.y - fwd.y * up.x));
    float3 cup = float3(right.y * fwd.z - right.z * fwd.y, right.z * fwd.x - right.x * fwd.z, right.x * fwd.y - right.y * fwd.x);
    float4x4 view = {{{right.x, cup.x, fwd.x, 0}, {right.y, cup.y, fwd.y, 0}, {right.z, cup.z, fwd.z, 0},
                      {-dot(right, cameraPos), -dot(cup, cameraPos), -dot(fwd, cameraPos), 1}}};
    const float ys = 1.0f / std::tan(fov * 0.5f), xs = ys / aspect;
    // reversed-Z: z_ndc = n / z_view at the near plane -> 1, at the far plane -> 0 (finite far)
    const float A = n / (n - f), B = -f * n / (n - f);   // z' = (A z + B) / z : z = n -> 1, z = f -> 0
    float4x4 proj = {{{xs, 0, 0, 0}, {0, ys, 0, 0}, {0, 0, A, 1}, {0, 0, B, 0}}};
    float4x4 vp = Mul(view, proj);
    Invert(vp, invViewProj);
    nearZ = n; farZ = 6400.0f; fogStartDistance = 0.0f;   // volumetricFogDistance default, cm
    int bad = 0; float worst = 0; float minDepth = 1e30f, maxDepth = 0;
    const uint2 cols[5] = {uint2(80, 45), uint2(0, 0), uint2(159, 0), uint2(0, 89), uint2(159, 89)};
    for (uint z = 0; z < 64; ++z) {
        float depth = SliceToDepth(z, 64);
        for (int c = 0; c < 5; ++c) {
            float3 p = FroxelWorldPos(cols[c], depth);
            float viewZ = dot(p - cameraPos, fwd);
            float rel = std::fabs(viewZ - depth) / depth;
            worst = std::max(worst, rel);
            if (rel > 1e-3f) ++bad;
            minDepth = std::min(minDepth, viewZ); maxDepth = std::max(maxDepth, viewZ);
        }
    }
    std::printf("bad %d worst %.3e minViewZ %.2f maxViewZ %.2f\n", bad, worst, minDepth, maxDepth);
    return 0;
}
'''


def extract(src: str, names: list[str]) -> str:
    """The HLSL functions by name (to the closing brace at column 0), lightly translated to the CPU dialect."""
    out = []
    for name in names:
        m = re.search(r'^float3? ' + name + r'\([^)]*\)\s*\n\{.*?^\}', src, re.S | re.M)
        if not m:
            raise SystemExit(f'function {name} not found in the inject shader')
        out.append(m.group(0))
    text = '\n'.join(out)
    text = text.replace('float2(xy)', 'float2u2(xy)').replace('float2(gridX, gridY)', 'float2(float(gridX), float(gridY))')
    text = text.replace('worldH.xyz', 'worldH.xyz()')
    text = text.replace('float2(0.0, 0.0)', 'float2(0.0f, 0.0f)')
    return text


def cpu_f2(shader_path: str, workdir: str, tag: str) -> dict:
    src = open(shader_path, encoding='utf-8', errors='replace').read()
    names = ['SliceToDepth', 'FroxelWorldPos'] + (['UnprojectFar'] if 'UnprojectFar' in src else [])
    body = CPU_PRELUDE + extract(src, [n for n in ['SliceToDepth', 'UnprojectFar', 'FroxelWorldPos'] if n in names]) + CPU_MAIN
    cpp = os.path.join(workdir, f'froxel_{tag}.cpp')
    exe = os.path.join(workdir, f'froxel_{tag}.exe')
    open(cpp, 'w', encoding='utf-8').write(body)
    r = subprocess.run(['zig', 'c++', '-std=c++17', cpp, '-o', exe], capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f'F2 {tag}: cannot compile the extracted functions:\n{r.stderr[-2500:]}')
    out = subprocess.run([exe], capture_output=True, text=True).stdout.strip()
    m = re.match(r'bad (\d+) worst ([0-9.e+-]+) minViewZ ([0-9.e+-]+) maxViewZ ([0-9.e+-]+)', out)
    return {'bad': int(m.group(1)), 'worst': float(m.group(2)), 'min_view_z': float(m.group(3)), 'max_view_z': float(m.group(4)), 'raw': out}


def read_component_default(name: str, fallback: float) -> float:
    src = open(COMPONENT, encoding='utf-8', errors='replace').read()
    m = re.search(name + r'\s*=\s*([0-9.]+)f?', src)
    return float(m.group(1)) if m else fallback


def capture(work, project, reuse, cvars, regions=None, frames=DUMP_FRAME + 6, timing_first=30):
    if not reuse or not os.path.isfile(os.path.join(work, 'manifest.json')):
        kw = {}
        if regions:
            kw.update(regions=regions, region_fields='components', region_first=DUMP_FRAME, region_last=DUMP_FRAME)
        res = runner.run_capture(work, project, frames, cvars=cvars, clean=True, timing_first=timing_first, timeout=900, **kw)
        if res['exit_code'] != 0:
            raise SystemExit(f'{work}: engine exited {res["exit_code"]}')
    with open(os.path.join(work, 'manifest.json'), encoding='utf-8') as fh:
        return json.load(fh)


def frame_data(work):
    fr = regiondump.load_sequence(os.path.join(work, 'region'), DUMP_FRAME, DUMP_FRAME)[0]
    pos = fr.world_positions('FRAME')
    colour = fr.rect('FRAME').channel('taa_output')[..., :3].astype(np.float64)
    direct = fr.rect('FRAME').channel('direct_light')[..., :3].astype(np.float64) @ REC709
    return pos, colour, direct


def hg_phase(cos_theta, g=0.2):
    return (1.0 / (4.0 * np.pi)) * (1.0 - g * g) / np.power(1.0 + g * g - 2.0 * g * cos_theta, 1.5)


def scatter_model(pos, cam, forward, sigma, near_fade, start_z, far_z, n=64):
    """The scatter loop of volumetric_fog_scatter_cs.hlsl per pixel, with the inject's froxel placement
    (camera + ray x slice depth / cos) and fog_shaft_reference.sun_visible as the sun visibility of each
    slice's froxel centre; the accumulated values are read at the surface depth as the composite reads the
    integrated volume (linear between slice centres in the log slice coordinate). Returns per pixel the
    unit-source integral I0, the visible-source integral Ivis, the transmittance T and cos(view, sun)."""
    cam = np.asarray(cam, dtype=np.float64)
    forward = np.asarray(forward, dtype=np.float64)
    ray = pos - cam
    dist = np.linalg.norm(ray, axis=-1)
    ray = ray / dist[..., None]
    cos_f = np.maximum(ray @ forward, 1e-3)
    depth_s = dist * cos_f
    slices = np.arange(n)
    z = start_z * np.power(far_z / start_z, (slices + 0.5) / n)
    z_prev = np.concatenate([[start_z], z[:-1]])
    thick = z - z_prev
    acc_depth = np.cumsum(thick)
    fade = np.clip(acc_depth / near_fade, 0.0, 1.0) if near_fade > 0 else np.ones(n)
    slice_t = np.exp(-sigma * thick)
    unit_sint = fade * (1.0 - slice_t) / sigma
    shape = pos.shape[:-1]
    I0, Ivis, T = np.zeros(shape + (n,)), np.zeros(shape + (n,)), np.zeros(shape + (n,))
    acc_i0, acc_iv, acc_t = np.zeros(shape), np.zeros(shape), np.ones(shape)
    for i in range(n):
        centre = cam + ray * (z[i] / cos_f)[..., None]
        vis = shaft.sun_visible(centre)
        acc_i0 = acc_i0 + acc_t * unit_sint[i]
        acc_iv = acc_iv + acc_t * unit_sint[i] * vis
        acc_t = acc_t * (1.0 - fade[i] + fade[i] * slice_t[i])
        I0[..., i], Ivis[..., i], T[..., i] = acc_i0, acc_iv, acc_t
    t = np.log(np.maximum(depth_s, start_z) / start_z) / np.log(far_z / start_z)
    w = np.clip(t * n - 0.5, 0.0, n - 1.0)
    i0 = np.floor(w).astype(int)
    i1 = np.minimum(i0 + 1, n - 1)
    fr = w - i0

    def pick(a):
        return (np.take_along_axis(a, i0[..., None], -1)[..., 0] * (1.0 - fr)
                + np.take_along_axis(a, i1[..., None], -1)[..., 0] * fr)
    return {'I0': pick(I0), 'Ivis': pick(Ivis), 'T': pick(T), 'cos_sun': ray @ np.asarray(shaft.SUN_DIR)}


def dxil_identical_with_head():
    """Compiles every shader that includes shadow.hlsli (other than the fog's own) twice from one copy of the
    shader tree: first with the committed shadow.hlsli (git show HEAD:...), then with the working copy, and
    compares the DXIL bytes. Returns (identical, differing) lists of shader paths."""
    rel = 'runtime/shaders/runtime/lighting/shadow.hlsli'
    head = subprocess.run(['git', 'show', f'HEAD:{rel}'], cwd=REPO, capture_output=True).stdout
    if not head:
        return [], ['git show HEAD:' + rel + ' failed']
    tmp = tempfile.mkdtemp(prefix='shadow_head_')
    copy = os.path.join(tmp, 'runtime')
    shutil.copytree(SHADERS, copy)
    includers = []
    for root, _, files in os.walk(SHADERS):
        for fn in files:
            if fn.endswith('.hlsl') and 'volumetric_fog' not in fn:
                p = os.path.join(root, fn)
                with open(p, encoding='utf-8', errors='replace') as fh:
                    if 'shadow.hlsli' in fh.read():
                        includers.append(os.path.relpath(p, SHADERS))
    outputs = {}
    for variant, source in (('head', head), ('work', open(os.path.join(SHADERS, 'lighting', 'shadow.hlsli'), 'rb').read())):
        with open(os.path.join(copy, 'lighting', 'shadow.hlsli'), 'wb') as fh:
            fh.write(source)
        for relp in includers:
            profile = {'_ps': 'ps_6_6', '_cs': 'cs_6_6', '_vs': 'vs_6_6'}.get(relp[-8:-5], 'ps_6_6')
            out = os.path.join(tmp, f'{variant}_{len(outputs)}.dxil')
            r = subprocess.run([DXC, '-T', profile, '-E', 'main', '-D', 'UZ_BINDLESS=1', '-I', copy, '-Fo', out,
                                os.path.join(copy, relp)], capture_output=True, text=True, errors='replace')
            outputs[(variant, relp)] = open(out, 'rb').read() if r.returncode == 0 and os.path.isfile(out) else None
    same, differing = [], []
    for relp in sorted(includers):
        a, b = outputs[('head', relp)], outputs[('work', relp)]
        (same if a is not None and a == b else differing).append(relp)
    return same, differing


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-froxel-fog'))
    ap.add_argument('--reuse', action='store_true')
    ap.add_argument('--skip-camera-c', action='store_true')
    args = ap.parse_args()
    results, report = [], {}

    def check(cid, ok, detail):
        results.append((cid, bool(ok), detail))
        print(f'  [{"PASS" if ok else "FAIL"}] {cid} {detail}', flush=True)

    workdir = tempfile.mkdtemp(prefix='froxel_fog_')
    # ── F2 ──
    f2 = cpu_f2(INJECT, workdir, 'now')
    f2b = cpu_f2(INJECT_BEFORE, workdir, 'before')
    report['f2'] = {'now': f2, 'before': f2b}
    check('F2 froxels lie on the camera rays at their slice depth (CPU, Sponza-like camera)', f2['bad'] == 0 and f2['worst'] <= 1e-3,
          f'{f2["bad"]} bad of 320, worst {f2["worst"]:.2e}, view depth {f2["min_view_z"]:.1f}..{f2["max_view_z"]:.0f} cm')
    check('F2 negative control: the pre-fix reconstruction keeps every froxel within 1 m', f2b['max_view_z'] < 100.0,
          f'view depth {f2b["min_view_z"]:.1f}..{f2b["max_view_z"]:.1f} cm')
    # ── F3 ──
    inject_src = open(INJECT, encoding='utf-8', errors='replace').read()
    host_src = open(HOST, encoding='utf-8', errors='replace').read()
    once = ('sunColor * sunIntensity' not in inject_src and 'sunColor * sunVisibility' in inject_src
            and 'fogCB.sunIntensity   = 1.0f;' in host_src)
    check('F3 the sun is applied once (shader multiplies the colour by phase and shadow only; host writes sunIntensity 1)', once, 'source anchors')

    # ── F4 / F5 ──
    sigma = 0.0005 * read_component_default('volumetricFogExtinctionScale', 1.0)
    base = {'r.TechPanel.Show': 0}
    rect = [('FRAME', 0, 0, W, H)]
    print('fog off / on (shadow 1) / on (shadow 0)', flush=True)
    man_off = capture(os.path.join(args.out, 'fog-off'), FOG_PROJECT, args.reuse, {**base, 'r.VolumetricFog': 0}, rect)
    man_on = capture(os.path.join(args.out, 'fog-on'), FOG_PROJECT, args.reuse, {**base, 'r.VolumetricFog': 1, 'r.VolumetricFog.SunShadow': 1}, rect)
    man_noshadow = capture(os.path.join(args.out, 'fog-on-noshadow'), FOG_PROJECT, args.reuse, {**base, 'r.VolumetricFog': 1, 'r.VolumetricFog.SunShadow': 0}, rect)
    capture(os.path.join(args.out, 'fog-on-debug'), FOG_PROJECT, args.reuse, {**base, 'r.VolumetricFog': 1, 'r.VolumetricFog.SunShadow': 1, 'r.VolumetricFog.Debug': 1}, rect)
    pos, c_off, direct = frame_data(os.path.join(args.out, 'fog-off'))
    _, c_on, _ = frame_data(os.path.join(args.out, 'fog-on'))
    _, c_ns, _ = frame_data(os.path.join(args.out, 'fog-on-noshadow'))
    cam = np.array(man_on.get('camera', {}).get('position', [0, 0, 0]), dtype=np.float64)
    dist = np.linalg.norm(pos - cam, axis=-1)
    floor = np.isfinite(pos).all(-1) & (np.abs(pos[..., 2]) < FLOOR_Z_TOL) & (dist > DIST_MIN) & (dist < DIST_MAX)
    dmax = float(np.nanmax(direct[floor])) if floor.any() else 0.0
    lit = floor & (direct > 0.30 * dmax)
    shadowed = floor & (direct < 0.05 * dmax)
    dbg_sp = frame_data(os.path.join(args.out, 'fog-on-debug'))[1]     # R = view depth cm, G = slice coordinate, B = transmittance
    T = dbg_sp[..., 2:3]
    lum = lambda c: c @ REC709
    L_on = lum(c_on - c_off * T)
    L_ns = lum(c_ns - c_off * T)
    ratio = L_on / np.maximum(L_ns, 1e-9)
    lit_ratio = float(np.mean(ratio[lit])) if lit.any() else float('nan')
    sh_ratio = float(np.mean(ratio[shadowed])) if shadowed.any() else float('nan')
    report['f4'] = {'sigma_per_cm': sigma, 'camera': cam.tolist(), 'floor_px': int(floor.sum()), 'lit_px': int(lit.sum()),
                    'transmittance_engine_median': float(np.median(dbg_sp[..., 2][floor])) if floor.any() else None,
                    'transmittance_analytic_median': float(np.median(np.exp(-sigma * dist)[floor])) if floor.any() else None,
                    'shadowed_px': int(shadowed.sum()), 'lit_ratio': lit_ratio, 'shadowed_ratio': sh_ratio,
                    'mean_inscatter_lit_noshadow': float(np.mean(L_ns[lit])) if lit.any() else None,
                    'mean_inscatter_shadowed_noshadow': float(np.mean(L_ns[shadowed])) if shadowed.any() else None}
    print(f'  floor {int(floor.sum())} px (lit {int(lit.sum())}, shadowed {int(shadowed.sum())}); in-scatter ratio shadow/no-shadow: lit {lit_ratio:.3f}, shadowed {sh_ratio:.3f}')
    check('F4a the sun shadow removes >= 40 % of the fog in-scatter over sun-shadowed Sponza floor', shadowed.sum() >= 200 and sh_ratio <= F4_SHADOW_MAX,
          f'shadowed ratio {sh_ratio:.3f} ({int(shadowed.sum())} px); sun-lit floor {lit_ratio:.3f} ({int(lit.sum())} px, reported only)')
    g = man_on.get('gpu_ms') or {}
    fog_ms = sum(v for k, v in g.items() if k.startswith('FroxelFog_'))
    report['f5'] = {k: v for k, v in g.items() if k.startswith('FroxelFog_')}
    check('F5 FroxelFog_* <= 1.5 ms at 1920x1080 with the shadow on', g and fog_ms <= F5_MAX_MS and fog_ms > 0,
          f'{fog_ms:.3f} ms ' + str({k: round(v, 3) for k, v in report['f5'].items()}))

    # ── F4b-d: the fog shaft reference against the analytic shadow volume ──
    print('fog shaft reference: fog off / on (shadow 1) / on (shadow 0)', flush=True)
    sdir = os.path.join(args.out, 'shaft')
    capture(os.path.join(sdir, 'fog-off'), SHAFT_PROJECT, args.reuse, {**base, 'r.VolumetricFog': 0}, rect)
    s_on = capture(os.path.join(sdir, 'fog-on'), SHAFT_PROJECT, args.reuse, {**base, 'r.VolumetricFog': 1, 'r.VolumetricFog.SunShadow': 1}, rect)
    capture(os.path.join(sdir, 'fog-on-noshadow'), SHAFT_PROJECT, args.reuse, {**base, 'r.VolumetricFog': 1, 'r.VolumetricFog.SunShadow': 0}, rect)
    capture(os.path.join(sdir, 'fog-on-debug'), SHAFT_PROJECT, args.reuse, {**base, 'r.VolumetricFog': 1, 'r.VolumetricFog.SunShadow': 1, 'r.VolumetricFog.Debug': 1}, rect)
    sub = (slice(None, None, STRIDE), slice(None, None, STRIDE))
    pos, c_off, direct = [a[sub] for a in frame_data(os.path.join(sdir, 'fog-off'))]
    c_on = frame_data(os.path.join(sdir, 'fog-on'))[1][sub]
    c_ns = frame_data(os.path.join(sdir, 'fog-on-noshadow'))[1][sub]
    dbg = frame_data(os.path.join(sdir, 'fog-on-debug'))[1][sub]        # R = view depth cm, G = slice coordinate, B = transmittance
    cam = np.array(s_on.get('camera', {}).get('position', shaft.CAMERA_EYE), dtype=np.float64)
    target = np.array(s_on.get('camera', {}).get('target', shaft.CAMERA_TARGET), dtype=np.float64)
    forward = (target - cam) / np.linalg.norm(target - cam)
    dist = np.linalg.norm(pos - cam, axis=-1)
    floor = np.isfinite(pos).all(-1) & (np.abs(pos[..., 2]) < FLOOR_Z_TOL) & (dist > SHAFT_DIST_MIN) & (dist < SHAFT_DIST_MAX)
    P = pos[floor]
    near_fade = read_component_default('volumetricFogNearFadeInDistance', 300.0)
    start_z = max(5.0, read_component_default('volumetricFogStartDistance', 0.0))
    far_z = read_component_default('volumetricFogDistance', 6400.0)
    sigma_s = shaft.DENSITY * read_component_default('volumetricFogExtinctionScale', 1.0)
    model = scatter_model(P, cam, forward, sigma_s, near_fade, start_z, far_z)
    # ── F2b: the composite's linear depth against the dump's world positions ──
    view_depth = (P - cam) @ forward
    dbg_depth = dbg[floor][:, 0]
    rel_err = np.abs(dbg_depth - view_depth) / np.maximum(view_depth, 1e-6)
    nz, fz = 5.0, 10000.0                                     # the scene camera (near 5, far 10000)
    device = nz * (fz - view_depth) / (view_depth * (fz - nz))   # reversed-Z device depth of the pixel
    old_formula = (nz * fz) / (fz - device * (fz - nz))          # depth_common.hlsli LinearizeDepthReversedZ
    old_err = np.abs(old_formula - view_depth) / np.maximum(view_depth, 1e-6)
    t_model = np.log(np.maximum(view_depth, start_z) / start_z) / np.log(far_z / start_z)
    report['f2b'] = {'floor_px': int(floor.sum()), 'median_rel_err': float(np.median(rel_err)) if floor.any() else None,
                     'p90_rel_err': float(np.quantile(rel_err, 0.9)) if floor.any() else None,
                     'old_formula_median_err': float(np.median(old_err)) if floor.any() else None,
                     'old_formula_median_cm': float(np.median(old_formula)) if floor.any() else None,
                     'slice_coord_median_abs_diff': float(np.median(np.abs(dbg[floor][:, 1] - t_model))) if floor.any() else None,
                     'transmittance_median_abs_diff_model': float(np.median(np.abs(dbg[floor][:, 2] - model['T']))) if floor.any() else None,
                     'view_depth_range_cm': [float(view_depth.min()), float(view_depth.max())] if floor.any() else None}
    check('F2b the composite linearises the reversed-Z depth: its view depth matches the dump (median relative error <= 3 %)',
          floor.sum() >= 200 and report['f2b']['median_rel_err'] <= F2B_MEDIAN_TOL,
          f"median {report['f2b']['median_rel_err']:.4f}, p90 {report['f2b']['p90_rel_err']:.4f} over {int(floor.sum())} px, "
          f"view depth {report['f2b']['view_depth_range_cm'][0]:.0f}..{report['f2b']['view_depth_range_cm'][1]:.0f} cm; "
          f"slice coordinate |diff| median {report['f2b']['slice_coord_median_abs_diff']:.4f}; transmittance |diff| to the CPU port median {report['f2b']['transmittance_median_abs_diff_model']:.4f}")
    check('F2b negative control: the depth_common LinearizeDepthReversedZ formula returns ~near for every pixel (median error >= 90 %)',
          floor.sum() >= 200 and report['f2b']['old_formula_median_err'] >= F2B_CONTROL_MIN,
          f"median error {report['f2b']['old_formula_median_err']:.3f}, median value {report['f2b']['old_formula_median_cm']:.2f} cm")
    Ts = dbg[floor][:, 2:3]                                   # the composite's own transmittance
    L_on_s = lum(c_on[floor] - c_off[floor] * Ts)
    L_ns_s = lum(c_ns[floor] - c_off[floor] * Ts)
    ratio_s = L_on_s / np.maximum(L_ns_s, 1e-9)
    f = model['Ivis'] / np.maximum(model['I0'], 1e-12)
    ambient = 0.3 / (4.0 * np.pi)
    sun = hg_phase(model['cos_sun']) * 10.0
    a_nom = ambient / (ambient + sun)
    r_pred = a_nom + (1.0 - a_nom) * f
    full_lit = f >= 0.999
    partial = f <= 0.5
    dmax_s = float(np.max(direct[floor])) if floor.any() else 0.0
    lit_surface = direct[floor] > 0.30 * dmax_s
    sh_surface = direct[floor] < 0.05 * dmax_s
    classified = lit_surface | sh_surface
    pred_lit = shaft.sun_visible(P) > 0.5
    agree = float(np.mean((pred_lit == lit_surface)[classified])) if classified.any() else float('nan')
    a_fit = float('nan')
    if partial.sum() >= 2:
        x = 1.0 - f[partial]
        a_fit = float(np.sum((ratio_s[partial] - f[partial]) * x) / max(float(np.sum(x * x)), 1e-12))
    mae = float(np.mean(np.abs(ratio_s[partial] - r_pred[partial]))) if partial.any() else float('nan')
    lit_mean = float(np.mean(ratio_s[full_lit])) if full_lit.any() else float('nan')
    part_mean = float(np.mean(ratio_s[partial])) if partial.any() else float('nan')
    pred_mean = float(np.mean(r_pred[partial])) if partial.any() else float('nan')
    report['f4_shaft'] = {'sigma_per_cm': sigma_s, 'camera': cam.tolist(), 'floor_px': int(floor.sum()), 'stride': STRIDE,
                          'full_lit_px': int(full_lit.sum()), 'partial_px': int(partial.sum()), 'full_lit_ratio': lit_mean,
                          'partial_ratio_mean': part_mean, 'partial_pred_mean': pred_mean, 'model_mae': mae,
                          'ambient_fraction_nominal': float(np.mean(a_nom)) if floor.any() else None, 'ambient_fraction_fit': a_fit,
                          'footprint_agreement': agree, 'classified_px': int(classified.sum()),
                          'mean_inscatter_noshadow': float(np.mean(L_ns_s)) if floor.any() else None,
                          'gpu_ms': {k: v for k, v in (s_on.get('gpu_ms') or {}).items() if k.startswith('FroxelFog_')}}
    print(f'  shaft floor {int(floor.sum())} px (stride {STRIDE}): fully lit {int(full_lit.sum())}, crossing the shadow (f <= 0.5) '
          f'{int(partial.sum())}; lit ratio {lit_mean:.3f}; partial ratio {part_mean:.3f} vs predicted {pred_mean:.3f}, MAE {mae:.3f}; '
          f'ambient fraction nominal {report["f4_shaft"]["ambient_fraction_nominal"]} fitted {a_fit:.3f}; footprint agreement {agree:.3f}', flush=True)
    check('F4d the analytic shadow footprint matches the rendered floor shadow (the sun convention of the model)',
          classified.sum() >= 200 and agree >= F4_FOOTPRINT_AGREE, f'agreement {agree:.3f} on {int(classified.sum())} classified px')
    check('F4b rays the model finds fully sun-lit keep their in-scatter with the shadow on (mean ratio >= 0.9)',
          full_lit.sum() >= 200 and lit_mean >= F4_LIT_MIN, f'lit ratio {lit_mean:.3f} ({int(full_lit.sum())} px)')
    check('F4c rays crossing the shadow volume follow the analytic prediction (mean |ratio - predicted| <= 0.1)',
          partial.sum() >= 200 and mae <= F4_MODEL_MAE,
          f'MAE {mae:.3f} over {int(partial.sum())} px; mean ratio {part_mean:.3f} vs predicted {pred_mean:.3f}')

    # ── F6 ──
    if not args.skip_camera_c:
        same, differing = dxil_identical_with_head()
        report['f6a'] = {'identical': same, 'differing': differing}
        check('F6a every shader including shadow.hlsli compiles to the same DXIL with the committed shadow.hlsli',
              bool(same) and not differing, f'identical {same}, differing {differing}')
        before_dir = os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-sdf-bake-cache', 'camera-c-warm')
        cvars = {**sponza.PROJECT_CVARS, 'r.GI.RadianceCache.ProbeClassification': 3, 'r.GI.RadianceCache.ReachRelocate': 1,
                 'r.GI.RadianceCache.ReachVisibleSeeds': 1}
        runs = {}
        for name in ('camera-c', 'camera-c-repeat'):
            w = os.path.join(args.out, name)
            if not args.reuse or not os.path.isfile(os.path.join(w, 'manifest.json')):
                res = runner.run_capture(w, sponza.QA_PROJECT, sponza.uniform.FRAMES, regions=sponza.ROIS, region_fields='components',
                                         region_first=sponza.uniform.FIRST, region_last=sponza.uniform.LAST, cvars=cvars,
                                         timing_first=200, clean=True)
                if res['exit_code'] != 0:
                    raise SystemExit(f'{w}: engine exited {res["exit_code"]}')
            runs[name] = sponza.roi_support(w, sponza.uniform.FIRST, sponza.uniform.LAST)
        now, rep_run = runs['camera-c'], runs['camera-c-repeat']
        before = sponza.roi_support(before_dir, sponza.uniform.FIRST, sponza.uniform.LAST) if os.path.isdir(before_dir) else None
        if before:
            rel = {n: abs(now[n]['mean'] - before[n]['mean']) / max(abs(before[n]['mean']), 1e-12) for n in now}
            spread = {n: abs(now[n]['mean'] - rep_run[n]['mean']) / max(abs(now[n]['mean']), 1e-12) for n in now}
            prev_spread = {}
            if os.path.isfile(B03_JSON):
                with open(B03_JSON, encoding='utf-8') as fh:
                    prev_spread = json.load(fh).get('report', {}).get('camera_c', {}).get('relative', {}) or {}
            known = max(list(spread.values()) + [float(v) for v in prev_spread.values()])
            thr = max(F6_FLOOR, F6_SPREAD_MULT * known)
            report['f6'] = {'now': now, 'repeat': rep_run, 'before': before, 'relative': rel, 'repeat_spread': spread,
                            'previous_build_spread': prev_spread, 'threshold': thr}
            check('F6b Sponza without fog unchanged (Camera C ROI means within the run-to-run spread of the builds)',
                  all(v <= thr for v in rel.values()),
                  'delta ' + str({n: f'{v:.2e}' for n, v in rel.items()}) + ', repeat spread ' + str({n: f'{v:.2e}' for n, v in spread.items()})
                  + f', threshold {thr:.2e}')
        else:
            check('F6b previous-build Camera C capture available', False, before_dir)

    passed = sum(1 for _, ok, _ in results if ok)
    print(f'{passed}/{len(results)} checks passed')
    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, 'froxel_fog.json'), 'w', encoding='utf-8') as fh:
        json.dump({'report': report, 'checks': [{'id': c, 'pass': ok, 'detail': str(d)} for c, ok, d in results]}, fh, indent=1, default=str)
    return 0 if passed == len(results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
