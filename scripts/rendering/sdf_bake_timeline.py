"""Mesh SDF bake timeline of a Sponza (or any) run log: preload span, bake batches, worst batch,
total bake span, meshes baked. Baseline tool of lighting block B, item 3 (async, cached SDF bake).

Usage: python scripts/rendering/sdf_bake_timeline.py <run.log> [--json]
"""
from __future__ import annotations

import json
import re
import sys
from datetime import datetime

TS = re.compile(r"^\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3})\]")
BATCH = "SceneRenderer: batch SDF bake"
BAKED = "SceneRenderer: GPU-baked mesh SDF"
CACHED = "mesh SDF from cache"
PRE_START = "Pre-loading scene geometry"
PRE_END = "Pre-load complete"


def stamp(line):
    m = TS.match(line)
    return datetime.strptime(m.group(1), "%Y-%m-%d %H:%M:%S.%f") if m else None


def parse(path):
    pre_start = pre_end = None
    batches = []   # [start, last_mesh_time, mesh_count]
    baked = cached = 0
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            t = stamp(line)
            if t is None:
                continue
            if PRE_START in line:
                pre_start = t
            elif PRE_END in line:
                pre_end = t
            elif BATCH in line:
                m = re.search(r"(\d+) meshes", line)
                batches.append([t, t, int(m.group(1)) if m else 0])
            elif BAKED in line:
                baked += 1
                if batches:
                    batches[-1][1] = t
            elif CACHED in line:
                cached += 1
    spans = [(b[1] - b[0]).total_seconds() for b in batches]
    return {
        "preload_s": (pre_end - pre_start).total_seconds() if pre_start and pre_end else None,
        "batches": len(batches), "meshes_baked": baked, "meshes_cached": cached,
        "bake_span_s": sum(spans), "worst_batch_s": max(spans) if spans else 0.0,
        "worst_batch_meshes": batches[spans.index(max(spans))][2] if spans else 0,
    }


if __name__ == "__main__":
    r = parse(sys.argv[1])
    if "--json" in sys.argv:
        print(json.dumps(r))
    else:
        print(f"preload {r['preload_s']} s; {r['batches']} bake batches, {r['meshes_baked']} meshes baked, "
              f"{r['meshes_cached']} from cache; bake span {r['bake_span_s']:.1f} s, worst batch {r['worst_batch_s']:.2f} s "
              f"({r['worst_batch_meshes']} meshes)")
