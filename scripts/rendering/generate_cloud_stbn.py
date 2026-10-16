"""Generate engine-owned scalar spatiotemporal blue noise, 128x128x64 R8.

Heitz et al. 2015, "A Low-Discrepancy Sampler that Distributes Monte Carlo
Errors as a Blue Noise in Screen Space"; Wolfe et al. 2022, "Spatiotemporal
Blue Noise Masks": suppress low frequencies in BOTH each XY slice and each
pixel's temporal sequence, not merely the spherical 3D spectrum.

This is an independent alternating spectral/rank projection implementation,
not NVIDIA SDK code or data. Seed, dimensions and iteration count are fixed.
The final quantized asset is checked in; no runtime generation is required.
"""
from pathlib import Path
import hashlib
import numpy as np


def generate():
    shape = (64, 128, 128)
    rng = np.random.default_rng(20261014)
    values = rng.random(shape)
    z = np.abs(np.fft.fftfreq(shape[0]))[:, None, None]
    y = np.fft.fftfreq(shape[1])[None, :, None]
    x = np.fft.rfftfreq(shape[2])[None, None, :]
    spatial = np.sqrt(x * x + y * y)
    # A product rejects frequencies low in either space OR time.
    target = np.sqrt((1 - np.exp(-(spatial / 0.12) ** 4))
                     * (1 - np.exp(-(z / 0.12) ** 4)))
    ranks = (np.arange(values.size, dtype=np.float64) + 0.5) / values.size
    for _ in range(64):
        spectrum = np.fft.rfftn(values)
        spectrum *= target / np.maximum(np.abs(spectrum), 1e-20)
        filtered = np.fft.irfftn(spectrum, s=shape, axes=(0, 1, 2))
        order = np.argsort(filtered, axis=None)
        values.flat[order] = ranks
    data = np.minimum(values * 256, 255).astype(np.uint8)
    centered = data.astype(np.float64) - data.mean()
    space_power = np.mean(abs(np.fft.rfft2(centered)) ** 2, axis=0)
    time_power = np.mean(abs(np.fft.rfft(centered, axis=0)) ** 2, axis=(1, 2))
    radius = spatial[0]
    space_ratio = float(space_power[(radius > 0) & (radius < .08)].mean()
                        / space_power[radius > .25].mean())
    time_ratio = float(time_power[1:5].mean() / time_power[16:].mean())
    if space_ratio >= .1 or time_ratio >= .1:
        raise RuntimeError(f"Not blue over both axes: {space_ratio=}, {time_ratio=}")
    path = Path(__file__).resolve().parents[2] / "runtime/content/atmosphere/stbn128.bin"
    path.write_bytes(data.tobytes())
    print(f"{path}: {data.size} bytes, sha256={hashlib.sha256(data).hexdigest()}")
    print(f"Low/high spectral power: spatial={space_ratio:.6f}, temporal={time_ratio:.6f}")


if __name__ == "__main__":
    generate()
