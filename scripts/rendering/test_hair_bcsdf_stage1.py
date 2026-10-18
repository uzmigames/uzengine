"""Bounded RTX hair swatch captures; deterministic scattering proofs are gtests.

Run --case prepare once, then each GPU case separately (each has a 180s deadline).
The generated triangle-ribbon asset has U across each fiber and V root-to-tip.
No new geometry pipeline, shared CB, root constants, or default switches are used.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import struct
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent / "flicker"))
import runner

REPO = Path(runner.REPO)
HERE = Path(__file__).resolve().parent


def prepare():
    vertices, indices = [], []
    segments = 96
    for strand in range(12):
        start = len(vertices)
        for segment in range(segments+1):
            t = segment/segments
            y = 25*math.cos(2*math.pi*t)
            slope = -25*2*math.pi*math.sin(2*math.pi*t)/280
            length = math.sqrt(1+slope*slope)
            for side in range(2):
                x = (strand-5.5)*14+(side-.5)*7
                vertices.append((x, y, 280*t, 0, -1/length, slope/length,
                                 float(side), t, 1, 0, 0, 1))
        for segment in range(segments):
            i = start+segment*2
            indices.extend((i, i+1, i+2, i+1, i+3, i+2))
    vb = b"".join(struct.pack("<12f", *v) for v in vertices)
    ib = struct.pack(f"<{len(indices)}I", *indices)
    vo = 128
    io = (vo+len(vb)+63) & ~63
    header = struct.pack("<16I", 0x534D5A55, 1, 0, 1, 1, len(vertices),
                         len(indices), 48, 4, vo, io, 0, 0, 0, 0, 0)
    lod = struct.pack("<IIIf", 0, 0, 1, 0)
    submesh = struct.pack("<4I", 0, 0, len(indices), 0)
    blob = header+lod+submesh
    blob += bytes(vo-len(blob))+vb
    blob += bytes(io-len(blob))+ib
    asset = HERE / "hair-stage1-strands.uzmesh"
    asset.write_bytes(blob)
    print(f"{asset}: {len(vertices)} vertices, {len(indices)//3} triangles")


def image(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--case", required=True,
                        choices=["prepare", "front", "back", "restir", "nonhair", "default", "metrics"])
    args = parser.parse_args()
    out = args.out.resolve()
    if args.case == "prepare":
        prepare()
        return
    if args.case == "metrics":
        default = image(out / "default/frames/f_95.png")
        reference = image(REPO.parent / "f005-after/frames/f_95.png")
        front = image(out / "front/frames/f_127.png")
        back = image(out / "back/frames/f_127.png")
        nonhair = image(out / "nonhair/frames/f_255.png")
        nonhair_reference = image(REPO.parent / "layered-stage1/base/frames/f_255.png")
        result = {
            "default_max_code_delta": int(np.abs(default-reference).max()),
            "default_png_bytes_identical": (out / "default/frames/f_95.png").read_bytes()
                == (REPO.parent / "f005-after/frames/f_95.png").read_bytes(),
            "nonhair_pt_max_code_delta": int(np.abs(nonhair-nonhair_reference).max()),
            "front_max_code": int(front.max()),
            "back_max_code": int(back.max()),
            "front_nonzero_pixels": int(np.any(front > 0, axis=2).sum()),
            "back_nonzero_pixels": int(np.any(back > 0, axis=2).sum()),
            "front_mean_code": float(front.mean()),
            "back_mean_code": float(back.mean()),
        }
        assert result["default_max_code_delta"] == 0, result
        assert result["nonhair_pt_max_code_delta"] == 0, result
        assert result["front_nonzero_pixels"] > 1000, result
        assert result["back_nonzero_pixels"] > 1000, result
        print(json.dumps(result, indent=2))
        return
    cvars = {"r.TechPanel.Show": 0, "r.Toolbar.Show": 0}
    project = HERE / "hair-stage1.uzproject"
    extra = ["--path-trace"]
    frames, shots = 128, "127"
    if args.case == "default":
        project = HERE / "reflection-reference.uzproject"
        frames, shots, extra = 100, "95", []
        cvars.update({"r.TAA": 1, "r.VolumetricFog": 0, "r.TAA.LogDecisions": 1})
    elif args.case == "back":
        # Same swatch from the other side: the unchanged source is now behind it.
        extra += ["--camera-pos", "0,250,140", "--camera-look", "0,0,140"]
    elif args.case == "restir":
        extra += ["--restir-di"]
    elif args.case == "nonhair":
        project = HERE / "layered-coat-stage1.uzproject"
        frames, shots = 256, "255"
    result = runner.run_capture(str(out / args.case), str(project), frames,
        clean=False, timeout=180, cvars=cvars, extra_args=extra, screenshot_frames=shots)
    assert result["exit_code"] == 0, result["exit_code"]
    log = Path(result["log"]).read_text(encoding="utf-8", errors="replace")
    assert "compile failed" not in log, result["log"]
    if args.case != "default":
        assert "RT-PSO CreateStateObject succeeded" in log, result["log"]
        if args.case != "nonhair":
            assert "HairStage1: tagged swatch Hair" in log, result["log"]
    print(f'{args.case}: exit=0; no compile failed; {result["frames_dir"]}', flush=True)


if __name__ == "__main__":
    main()
