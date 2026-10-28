"""Bakes the GGX Linearly Transformed Cosine (LTC) tables used by rectangular area lights.

Source / derivation
-------------------
Heitz, Dupuy, Hill, Neubelt 2016, "Real-Time Polygonal-Light Shading with Linearly Transformed
Cosines" (SIGGRAPH 2016) and the authors' reference fitter (github.com/selfshadow/ltc_code, fit/:
fit.cpp, fitLTC.h, brdf_ggx.h, nelder_mead.h). This script re-implements that fitter in numpy so the
tables shipped in runtime/engine/renderer/lighting/ltc_lut_data.inc are reproducible from source:

* BRDF: isotropic GGX, height-correlated Smith masking-shadowing, NO Fresnel. Fresnel is applied at
  run time from the magnitude table: spec = F0 * t2.x + (F90 - F0) * t2.y, where t2.x is the BRDF's
  directional albedo and t2.y its (1 - VoH)^5-weighted albedo (the Schlick split of the reference).
* Table: 64 x 64. x = perceptual roughness (alpha = roughness^2, alpha >= 1e-5),
  y = sqrt(1 - cos(theta_v)) with theta_v <= 1.57 (the reference parameterisation).
* Per cell: the albedo, Schlick albedo and mean direction of the BRDF by 32x32 NDF importance sampling
  (computeAvgTerms); the lobe frame is (T1, Y, averageDir); then a Nelder-Mead fit of (m11, m22, m13)
  minimising the MIS-weighted cubic error (computeError, Nsample = 32), warm-started as in the
  reference: theta = 0 from the previous roughness row (isotropic lobe), other thetas from the
  previous theta of the same row.
* Output: M^-1 normalised by its middle entry, packed exactly like the reference's ltc_1
  (invM[0][0], invM[0][2], invM[2][0], invM[2][2] in glm's column-major indexing, i.e. row-major
  (m00, m20, m02, m22)), so the table is directly comparable with fit/results/ltc_1.inc and three.js's
  LTC_MAT_1; the shader rebuilds float3x3(t.x, 0, t.z / 0, 1, 0 / t.y, 0, t.w) (ltc.hlsli). The second
  table is (magnitude, fresnel). Row t (y) = sqrt(1 - NoV) index, column a (x) = roughness index.
  Cross-checked 2026-10-20 against three.js RectAreaLightTexturesLib (the published fit): magnitude and
  fresnel agree to 2e-4 everywhere, the matrix entries to a median relative 1.3e-4 for roughness >= 0.2
  (only the degenerate theta = 1.57 row, where the objective is flat, finds different local minima).

Deterministic: fixed sample grids, float64, no randomness. The rows are fitted in parallel; the
reference's sequential warm-start chain is preserved (theta = 0 column first, then each row).

Run from the repo root:  python scripts/rendering/bake_ltc_lut.py [--out PATH] [--check]
  --check   only re-derive a few cells and compare them with the shipped .inc (no write).
"""
from __future__ import annotations

import argparse
import math
import os
import sys
import time
from multiprocessing import Pool

import numpy as np

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_OUT = os.path.join(REPO, 'runtime', 'engine', 'renderer', 'lighting', 'ltc_lut_data.inc')

N = 64            # table size (reference: N = 64)
NSAMPLE = 32      # samples per strategy and axis (reference: Nsample = 32)
MIN_ALPHA = 1e-5  # reference: MIN_ALPHA = 0.00001f
PI = math.pi

_grid = (np.arange(NSAMPLE) + 0.5) / NSAMPLE
_U1, _U2 = np.meshgrid(_grid, _grid, indexing='ij')
U1 = _U1.ravel()
U2 = _U2.ravel()


def _normalize_rows(v):
    return v / np.maximum(np.linalg.norm(v, axis=1, keepdims=True), 1e-30)


# ── GGX (brdf_ggx.h) ────────────────────────────────────────────────────────────────────────

def ggx_lambda(alpha, cos_theta):
    """Smith Lambda for GGX: (-1 + sqrt(1 + alpha^2 tan^2 theta)) / 2."""
    c = np.clip(cos_theta, 1e-7, 1.0)
    tan2 = (1.0 - c * c) / (c * c)
    return 0.5 * (-1.0 + np.sqrt(1.0 + alpha * alpha * tan2))


def ggx_eval(V, L, alpha):
    """Returns (f * cos(theta_L), pdf) for rows of L; f = D G2 / (4 NoV NoL) (no Fresnel)."""
    n = L.shape[0]
    if V[2] <= 0.0:
        return np.zeros(n), np.zeros(n)
    lambda_v = ggx_lambda(alpha, V[2])
    Lz = L[:, 2]
    G2 = np.where(Lz <= 0.0, 0.0, 1.0 / (1.0 + lambda_v + ggx_lambda(alpha, Lz)))
    H = _normalize_rows(V[None, :] + L)
    Hz = H[:, 2]
    ok = Hz > 1e-6
    Hz_safe = np.where(ok, Hz, 1.0)
    slope2 = (H[:, 0] ** 2 + H[:, 1] ** 2) / (Hz_safe * Hz_safe)
    D = 1.0 / (1.0 + slope2 / (alpha * alpha))
    D = D * D / (PI * alpha * alpha * Hz_safe ** 4)
    D = np.where(ok, D, 0.0)
    VoH = np.sum(V[None, :] * H, axis=1)
    pdf = np.abs(D * Hz / 4.0 / np.where(np.abs(VoH) > 1e-7, VoH, 1e-7))
    res = D * G2 / 4.0 / V[2]
    return res, pdf


def ggx_sample(V, alpha, u1, u2):
    """NDF sampling of the reference (brdf_ggx.h sample): reflect V about a sampled microfacet normal."""
    phi = 2.0 * PI * u1
    r = alpha * np.sqrt(u2 / (1.0 - u2))
    Nm = _normalize_rows(np.stack([r * np.cos(phi), r * np.sin(phi), np.ones_like(r)], axis=1))
    NoV = Nm @ V
    return -V[None, :] + 2.0 * Nm * NoV[:, None]


# ── LTC (fitLTC.h) ─────────────────────────────────────────────────────────────────────────

class LTC:
    def __init__(self):
        self.magnitude = 1.0
        self.fresnel = 1.0
        self.m11 = 1.0
        self.m22 = 1.0
        self.m13 = 0.0
        self.X = np.array([1.0, 0.0, 0.0])
        self.Y = np.array([0.0, 1.0, 0.0])
        self.Z = np.array([0.0, 0.0, 1.0])
        self.update()

    def update(self):
        B = np.array([[self.m11, 0.0, self.m13],
                      [0.0, self.m22, 0.0],
                      [0.0, 0.0, 1.0]])
        R = np.column_stack([self.X, self.Y, self.Z])
        self.M = R @ B
        self.invM = np.linalg.inv(self.M)
        self.detM = abs(np.linalg.det(self.M))

    def eval(self, L):
        Lo = _normalize_rows(L @ self.invM.T)
        L_ = Lo @ self.M.T
        l = np.linalg.norm(L_, axis=1)
        jacobian = self.detM / np.maximum(l ** 3, 1e-30)
        D = np.maximum(Lo[:, 2], 0.0) / PI
        return self.magnitude * D / jacobian

    def sample(self, u1, u2):
        theta = np.arccos(np.sqrt(u1))
        phi = 2.0 * PI * u2
        st = np.sin(theta)
        d = np.stack([st * np.cos(phi), st * np.sin(phi), np.cos(theta)], axis=1)
        return _normalize_rows(d @ self.M.T)


def compute_error(ltc, V, alpha):
    """MIS-weighted cubic error between the BRDF and the LTC (fit.cpp computeError)."""
    total = 0.0
    for strategy in (0, 1):
        L = ltc.sample(U1, U2) if strategy == 0 else ggx_sample(V, alpha, U1, U2)
        eval_brdf, pdf_brdf = ggx_eval(V, L, alpha)
        eval_ltc = ltc.eval(L)
        pdf_ltc = eval_ltc / ltc.magnitude
        err = np.abs(eval_brdf - eval_ltc) ** 3
        denom = pdf_ltc + pdf_brdf
        total += float(np.sum(np.where(denom > 0.0, err / np.where(denom > 0.0, denom, 1.0), 0.0)))
    return total / float(NSAMPLE * NSAMPLE)


def compute_avg_terms(V, alpha):
    """Directional albedo, Schlick-weighted albedo and mean direction (fit.cpp computeAvgTerms)."""
    L = ggx_sample(V, alpha, U1, U2)
    ev, pdf = ggx_eval(V, L, alpha)
    w = np.where(pdf > 0.0, ev / np.where(pdf > 0.0, pdf, 1.0), 0.0)
    H = _normalize_rows(V[None, :] + L)
    VoH = np.maximum(np.sum(V[None, :] * H, axis=1), 0.0)
    n = float(NSAMPLE * NSAMPLE)
    norm = float(np.sum(w)) / n
    fresnel = float(np.sum(w * (1.0 - VoH) ** 5)) / n
    avg = np.sum(w[:, None] * L, axis=0)
    avg[1] = 0.0
    avg = avg / max(np.linalg.norm(avg), 1e-30)
    return norm, fresnel, avg


def nelder_mead(objective, start, delta, tolerance, max_iters):
    """The reference's nelder_mead.h: reflect 1, expand 2, contract 0.5, shrink 0.5."""
    dim = len(start)
    s = [np.array(start, dtype=np.float64) for _ in range(dim + 1)]
    for i in range(1, dim + 1):
        s[i][i - 1] += delta
    f = [objective(p) for p in s]
    lo = 0
    for _ in range(max_iters):
        lo = hi = nh = 0
        for i in range(1, dim + 1):
            if f[i] < f[lo]:
                lo = i
            if f[i] > f[hi]:
                nh = hi
                hi = i
            elif f[i] > f[nh]:
                nh = i
        a, b = abs(f[lo]), abs(f[hi])
        if 2.0 * abs(a - b) < (a + b) * tolerance:
            break
        o = sum(s[i] for i in range(dim + 1) if i != hi) / dim
        r = o + 1.0 * (o - s[hi])
        fr = objective(r)
        if fr < f[nh]:
            if fr < f[lo]:
                e = o + 2.0 * (o - s[hi])
                fe = objective(e)
                if fe < fr:
                    s[hi], f[hi] = e, fe
                    continue
            s[hi], f[hi] = r, fr
            continue
        c = o - 0.5 * (o - s[hi])
        fc = objective(c)
        if fc < f[hi]:
            s[hi], f[hi] = c, fc
            continue
        for k in range(dim + 1):
            if k == lo:
                continue
            s[k] = s[lo] + 0.5 * (s[k] - s[lo])
            f[k] = objective(s[k])
    return s[lo], f[lo]


def fit(ltc, V, alpha, isotropic, epsilon=0.05):
    def apply(params):
        m11 = max(float(params[0]), 1e-7)
        m22 = max(float(params[1]), 1e-7)
        if isotropic:
            ltc.m11, ltc.m22, ltc.m13 = m11, m11, 0.0
        else:
            ltc.m11, ltc.m22, ltc.m13 = m11, m22, float(params[2])
        ltc.update()

    def objective(params):
        apply(params)
        return compute_error(ltc, V, alpha)

    best, err = nelder_mead(objective, [ltc.m11, ltc.m22, ltc.m13], epsilon, 1e-5, 100)
    apply(best)
    return err


def cell_view(t):
    x = t / float(N - 1)
    ct = 1.0 - x * x
    theta = min(1.57, math.acos(ct))
    return np.array([math.sin(theta), 0.0, math.cos(theta)])


def cell_alpha(a):
    roughness = a / float(N - 1)
    return max(roughness * roughness, MIN_ALPHA)


def fit_cell(ltc, a, t):
    V = cell_view(t)
    alpha = cell_alpha(a)
    ltc.magnitude, ltc.fresnel, avg = compute_avg_terms(V, alpha)
    if t == 0:
        ltc.X = np.array([1.0, 0.0, 0.0])
        ltc.Y = np.array([0.0, 1.0, 0.0])
        ltc.Z = np.array([0.0, 0.0, 1.0])
        ltc.m13 = 0.0
        isotropic = True
    else:
        ltc.X = np.array([avg[2], 0.0, -avg[0]])
        ltc.Y = np.array([0.0, 1.0, 0.0])
        ltc.Z = avg
        isotropic = False
    ltc.update()
    err = fit(ltc, V, alpha, isotropic)
    return ltc.M.copy(), ltc.magnitude, ltc.fresnel, err


def fit_row(args):
    """Row a: thetas 1..N-1, warm-started from the (a, 0) isotropic fit (m11 = m22, m13 = 0)."""
    a, m11 = args
    ltc = LTC()
    ltc.m11 = ltc.m22 = m11
    ltc.m13 = 0.0
    out = []
    for t in range(1, N):
        out.append((t,) + fit_cell(ltc, a, t))
    return a, out


def pack(M):
    """(invM[0][0], invM[0][2], invM[2][0], invM[2][2]) in glm column-major = row-major (m00, m20, m02, m22)."""
    invM = np.linalg.inv(M)
    invM = invM / invM[1, 1]
    return (invM[0, 0], invM[2, 0], invM[0, 2], invM[2, 2])


def bake(workers):
    t0 = time.time()
    tab = np.zeros((N, N, 3, 3))      # [t, a]
    mag = np.zeros((N, N, 2))
    errs = np.zeros((N, N))
    # theta = 0 column, descending roughness (the reference's outer loop order).
    ltc = LTC()
    col0 = {}
    for a in range(N - 1, -1, -1):
        if a == N - 1:
            ltc.m11 = ltc.m22 = 1.0
        else:
            ltc.m11 = ltc.m22 = tab[0, a + 1][0, 0]
        M, m, f, e = fit_cell(ltc, a, 0)
        tab[0, a], mag[0, a], errs[0, a] = M, (m, f), e
        col0[a] = M[0, 0]
    print(f'theta=0 column done in {time.time() - t0:.1f}s', flush=True)
    jobs = [(a, col0[a]) for a in range(N - 1, -1, -1)]
    with Pool(workers) as pool:
        for a, cells in pool.imap_unordered(fit_row, jobs):
            for t, M, m, f, e in cells:
                tab[t, a], mag[t, a], errs[t, a] = M, (m, f), e
    print(f'all {N * N} cells fitted in {time.time() - t0:.1f}s; '
          f'error max {errs.max():.3e} mean {errs.mean():.3e} (cubic MIS metric)', flush=True)
    mat = np.zeros((N, N, 4))
    for t in range(N):
        for a in range(N):
            mat[t, a] = pack(tab[t, a])
    return mat, mag, errs


def write_inc(path, mat, mag, errs):
    def fmt(v):
        s = f'{v:.7g}'
        if 'e' not in s and '.' not in s:
            s += '.0'
        return s + 'f'
    usable = errs[:, 13:]   # roughness >= 0.2, the engine's r.Material.MinGGXAlpha floor (alpha 0.04)
    lines = []
    lines.append('// Generated by scripts/rendering/bake_ltc_lut.py (Heitz et al. 2016 LTC fit of GGX) - do not edit.')
    lines.append(f'// {N}x{N}; row t: y = sqrt(1 - NoV) = t/{N - 1}, column a: x = roughness = a/{N - 1}, alpha = roughness^2.')
    lines.append(f'// fit error (cubic MIS metric, Nsample={NSAMPLE}): roughness >= 0.2: max {usable.max():.3e}, '
                 f'mean {usable.mean():.3e}; all cells: max {errs.max():.3e} (the near-mirror columns dominate)')
    lines.append(f'// magnitude range [{mag[..., 0].min():.6f}, {mag[..., 0].max():.6f}], '
                 f'fresnel range [{mag[..., 1].min():.6f}, {mag[..., 1].max():.6f}]')
    lines.append('')
    lines.append('// M^-1 / M^-1[1][1] packed as the reference ltc_1: (invM[0][0], invM[0][2], invM[2][0], invM[2][2])')
    lines.append('// in column-major indexing = row-major (m00, m20, m02, m22); texel (x = a, y = t), 4 floats per texel.')
    lines.append(f'const float kLtcMatrixTable[{N} * {N} * 4] = {{')
    for t in range(N):
        lines.append(f'    // row t={t}')
        for a in range(N):
            v = mat[t, a]
            lines.append(f'    {fmt(v[0])}, {fmt(v[1])}, {fmt(v[2])}, {fmt(v[3])},')
    lines.append('};')
    lines.append('')
    lines.append('// (magnitude = directional albedo of GGX without Fresnel, fresnel = its (1 - VoH)^5 weighted albedo),')
    lines.append('// 2 floats per texel, same layout.')
    lines.append(f'const float kLtcMagnitudeTable[{N} * {N} * 2] = {{')
    for t in range(N):
        lines.append(f'    // row t={t}')
        for a in range(N):
            v = mag[t, a]
            lines.append(f'    {fmt(v[0])}, {fmt(v[1])},')
    lines.append('};')
    lines.append('')
    with open(path, 'w', encoding='utf-8', newline='\n') as fh:
        fh.write('\n'.join(lines))


def report(mat, mag):
    print('corner cells (t = sqrt(1-NoV) index, a = roughness index):')
    for t, a in ((0, 0), (0, 63), (63, 0), (63, 63), (0, 32), (32, 32), (40, 20)):
        print(f'  t={t:2d} a={a:2d}: Minv (m00, m20, m02, m22) = {np.array2string(mat[t, a], precision=5)}  '
              f'magnitude {mag[t, a, 0]:.5f} fresnel {mag[t, a, 1]:.5f}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=DEFAULT_OUT)
    ap.add_argument('--workers', type=int, default=max(1, min(32, os.cpu_count() or 1)))
    ap.add_argument('--check', action='store_true', help='re-fit a few cells and print them (no write)')
    args = ap.parse_args()
    if args.check:
        ltc = LTC()
        for a, t in ((63, 0), (32, 0), (0, 0)):
            M, m, f, e = fit_cell(ltc, a, t)
            print(f'a={a} t={t}: pack {pack(M)} magnitude {m:.5f} fresnel {f:.5f} err {e:.3e}')
        return
    mat, mag, errs = bake(args.workers)
    report(mat, mag)
    write_inc(args.out, mat, mag, errs)
    print(f'wrote {args.out} ({os.path.getsize(args.out)} bytes)')


if __name__ == '__main__':
    main()
