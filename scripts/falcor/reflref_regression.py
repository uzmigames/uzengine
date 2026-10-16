#!/usr/bin/env python3
"""PT reflection-reference regression: PSNR(golden, test) >= threshold (dB).

Stochastic PT renders are not bit-identical, so the threshold tolerates Monte-Carlo
noise (two independent converged renders self-score ~33-35 dB) while still catching
real breakage. Usage: reflref_regression.py <golden.png> <test.png> <threshold_db>
"""
import sys
import numpy as np
from PIL import Image

golden, test, thr = sys.argv[1], sys.argv[2], float(sys.argv[3])
g = np.array(Image.open(golden).convert("RGB"), dtype=np.float64)
t = np.array(Image.open(test).convert("RGB"), dtype=np.float64)
if g.shape != t.shape:
    print(f"FAIL shape mismatch golden={g.shape} test={t.shape}")
    sys.exit(1)
mse = float(np.mean((g - t) ** 2))
psnr = 99.0 if mse < 1e-9 else 10.0 * np.log10(255.0 ** 2 / mse)
ok = psnr >= thr
print(f"reflection_reference PSNR={psnr:.2f} dB (threshold {thr:.1f}) -> {'PASS' if ok else 'FAIL'}")
sys.exit(0 if ok else 1)
