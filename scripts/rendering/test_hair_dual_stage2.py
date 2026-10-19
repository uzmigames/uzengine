"""Bounded dense blonde/dark hair swatches for the biased dual-scattering tail.

prepare writes assets into --out (never overwrites source assets). Each capture
has a 175-second subprocess deadline. Linear HDR taps, not tone-mapped images,
measure mean bias and noise. K is consecutive explicit hair vertices.
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

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "flicker"))
import regiondump
import runner


def prepare(out):
    out.mkdir(parents=True, exist_ok=True)
    vertices, indices = [], []
    for layer in range(10):
        for strand in range(28):
            start = len(vertices)
            for segment in range(25):
                t = segment/24
                y = layer*5+2*math.sin(t*2*math.pi+layer*.37)
                slope = 2*2*math.pi*math.cos(t*2*math.pi+layer*.37)/240
                length = math.sqrt(1+slope*slope)
                for side in range(2):
                    x = (strand-13.5)*6+(layer%3-1)*1.7+(side-.5)*5.6
                    vertices.append((x,y,240*t,0,-1/length,slope/length,
                                     float(side),t,1,0,0,1))
            for segment in range(24):
                i = start+segment*2
                indices.extend((i,i+1,i+2,i+1,i+3,i+2))
    vb = b"".join(struct.pack("<12f", *v) for v in vertices)
    ib = struct.pack(f"<{len(indices)}I", *indices)
    io = (128+len(vb)+63) & ~63
    blob = struct.pack("<16I",0x534D5A55,1,0,1,1,len(vertices),len(indices),
                       48,4,128,io,0,0,0,0,0)
    blob += struct.pack("<IIIf",0,0,1,0)+struct.pack("<4I",0,0,len(indices),0)
    blob += bytes(128-len(blob))+vb
    blob += bytes(io-len(blob))+ib
    (out / "strands.uzmesh").write_bytes(blob)
    for name, color in (("blonde",(.75,.55,.25)),("dark",(.025,.012,.006))):
        lua = f"""function on_start(entity)
    uz.scene.set_material(entity, {{
        shading_model="hair", base_color_r={color[0]}, base_color_g={color[1]},
        base_color_b={color[2]}, roughness_multiplier=.25, metallic_multiplier=.35,
        two_sided=true
    }})
    uz.log("HairDualStage2: {name} dense swatch Hair")
end
"""
        (out / f"{name}.lua").write_text(lua,encoding="utf-8")
        scene = {"name":f"Dual-scattering {name} swatch","version":"1.0","entities":[
            {"name":"Camera","components":{"transform":{"position":[0,-230,120]},
                "camera":{"fov":42,"near":1,"far":5000,"active":True,"look_at":[0,20,120]}}},
            {"name":"Uniform distant boundary","components":{
                "sky_light":{"intensity":.4,"light_color":[1,1,1]}}},
            {"name":"Aligned dense fibers","components":{
                "transform":{"position":[0,0,0]},"mesh":{"asset":"strands.uzmesh"},
                "material":{"baseColorR":color[0],"baseColorG":color[1],"baseColorB":color[2],
                            "roughnessMultiplier":.25,"metallicMultiplier":.35},
                "script":{"path":f"{name}.lua"}}}]}
        (out / f"{name}.uzscene").write_text(json.dumps(scene,indent=2),encoding="utf-8")
        project = {"name":scene["name"],"version":"1.0","content_root":".",
            "default_scene":f"{name}.uzscene","window":{"width":320,"height":180},
            "cvars":{"r.AutoExposure":0,"r.Exposure":1,"r.Bloom":0,
                "r.TonemapOperator":0,"r.TAA":1,"r.Fog":0,"r.VolumetricFog":0,
                "r.TechPanel.Show":0,"r.Toolbar.Show":0,"r.GTAO":0,"r.SSGI":0,
                "r.RadianceCache":0,"r.GI.FinalGather":0,"r.GI.Reflections":0,
                "r.PathTracer.MaxBounces":16,"r.PathTracer.NEESamples":1,
                "r.PathTracer.RussianRoulette":0}}
        (out / f"{name}.uzproject").write_text(json.dumps(project,indent=2),encoding="utf-8")
    print(f"prepared {len(vertices)} vertices, {len(indices)//3} triangles in {out}")


def probe(path):
    data = regiondump.load_frame(str(path))
    rect = data.rect("swatch")
    footer = data.data[rect.base+rect.pixel_count*data.records_per_pixel+1]
    assert rect.valid and int(footer.view(np.uint32)[0]) == data.frame and footer[3] == 1
    return rect.channel("taa_tap_centre")[...,:3].astype(np.float64)


def block_statistics(case, last, block=128, reference=None):
    # Disjoint sample blocks reconstructed from the progressive mean. Dropping
    # the first block excludes startup; no temporal denoiser output is read.
    manifest = json.loads((case / "manifest.json").read_text())
    preload = manifest["frames"]["preload"]
    # Hair is tagged by on_start at the first post-preload frame, resetting PT.
    log = (case / "run.log").read_text(encoding="utf-8")
    active_log = log.split("Starting render loop",1)[1]
    assert active_log.count("accumulation reset") == 1, case
    blocks = []
    for n in range(2*block,last+2,block):
        current = probe(case / "region" / f"region_{n-1}.bin")
        previous = probe(case / "region" / f"region_{n-block-1}.bin")
        blocks.append(((n-preload)*current-(n-block-preload)*previous)/block)
    blocks = np.stack(blocks)
    result = {"block_samples":block,"blocks":len(blocks),
        "variance":float(np.var(blocks,axis=0,ddof=1).mean()),
        "mean_standard_error":float(np.std(blocks.mean(axis=(1,2,3)),ddof=1)/np.sqrt(len(blocks)))}
    if reference is not None:
        result["mse_to_explicit_reference"] = float(np.mean((blocks-reference)**2))
    return result


def block_variance(case, last, block=128):
    return block_statistics(case,last,block)["variance"]


def metrics(out):
    result = {}
    for color in ("blonde","dark"):
        reference_dir = out / f"{color}-off"
        files = sorted((reference_dir / "region").glob("region_*.bin"),
                       key=lambda p:int(p.stem.split("_")[1]))
        last = int(files[-1].stem.split("_")[1])
        reference = probe(files[-1])
        half = probe(reference_dir / "region" / f"region_{(last+1)//2-1}.bin")
        ref_manifest = json.loads((reference_dir / "manifest.json").read_text())
        ref_gpu = ref_manifest["gpu_ms"]["PipelinePathTracer"]
        # The HDR centre tap passes through RGBA16F. Keep 15 disjoint blocks
        # rather than amplifying that quantization with ever-shorter differences
        # of two nearly converged means.
        block = max(128,(last+1)//16)
        ref_noise = block_statistics(reference_dir,last,block,reference)
        result[color] = {"reference_frame":last,"reference_rgb":reference.mean(axis=(0,1)).tolist(),
            "reference_half_mean_delta":float((half-reference).mean()),
            "reference_half_relative_delta":float(half.mean()/reference.mean()-1),
            "reference_gpu_ms":ref_gpu,"reference_noise":ref_noise,"comparisons":[]}
        for case in sorted(out.glob(f"{color}-k*")):
            if not (case / "region" / f"region_{last}.bin").exists():
                continue
            actual = probe(case / "region" / f"region_{last}.bin")
            early = probe(case / "region/region_63.bin")
            off_early = probe(reference_dir / "region/region_63.bin")
            actual_half = probe(case / "region" / f"region_{(last+1)//2-1}.bin")
            noise = block_statistics(case,last,block)
            gpu = json.loads((case / "manifest.json").read_text())["gpu_ms"]["PipelinePathTracer"]
            equal_cost = block_statistics(case,last,max(1,round(block*ref_gpu/gpu)),reference)
            equal_cost["gpu_cost_ratio"] = equal_cost["block_samples"]*gpu/(block*ref_gpu)
            equal_cost["variance_ratio"] = equal_cost["variance"]/ref_noise["variance"]
            result[color]["comparisons"].append({
                "case":case.name,"rgb":actual.mean(axis=(0,1)).tolist(),
                "mean_relative_bias":float(actual.mean()/reference.mean()-1),
                "rgb_relative_bias":(actual.mean(axis=(0,1))/reference.mean(axis=(0,1))-1).tolist(),
                "half_relative_delta":float(actual_half.mean()/actual.mean()-1),
                "noise":noise,"gpu_ms":gpu,
                "equal_sample_variance_ratio":noise["variance"]/ref_noise["variance"],
                "equal_gpu_cost_variance_ratio":noise["variance"]*gpu/(ref_noise["variance"]*ref_gpu),
                "measured_equal_cost":equal_cost,
                "rmse_to_reference":float(np.sqrt(np.mean((actual-reference)**2))),
                "early_mse_about_own_limit":float(np.mean((early-actual)**2)),
                "off_early_mse_about_own_limit":float(np.mean((off_early-reference)**2)),
                "early_mse_to_explicit_reference":float(np.mean((early-reference)**2))})
        result[color]["depth_convergence"] = []
        for case in sorted(out.glob(f"{color}-off-*")):
            actual = probe(case / "region" / f"region_{last}.bin")
            result[color]["depth_convergence"].append({"case":case.name,
                "mean_relative_delta":float(actual.mean()/reference.mean()-1),
                "max_linear_delta":float(np.abs(actual-reference).max())})
        result[color]["exact_limits"] = []
        for mode, baseline_mode in (("k64","off"),("on","k20")):
            case = out / f"{color}-{mode}"
            baseline = out / f"{color}-{baseline_mode}"
            if not (case / "region" / f"region_{last}.bin").exists():
                continue
            actual = probe(case / "region" / f"region_{last}.bin")
            baseline = probe(baseline / "region" / f"region_{last}.bin")
            result[color]["exact_limits"].append({"case":case.name,
                "baseline":baseline_mode,
                "max_linear_delta":float(np.abs(actual-baseline).max())})
    default = out / "default/frames/f_95.png"
    baseline = Path(runner.REPO).parent / "f005-after/frames/f_95.png"
    if default.exists():
        result["default_max_code_delta"] = int(np.abs(
            np.asarray(Image.open(default),dtype=int)-np.asarray(Image.open(baseline),dtype=int)).max())
        result["default_png_bytes_identical"] = default.read_bytes() == baseline.read_bytes()
    for side in ("front","back"):
        current = out / f"stage1-{side}/frames/f_127.png"
        baseline = Path(runner.REPO).parent / f"hair-stage1/{side}/frames/f_127.png"
        if current.exists():
            result[f"off_stage1_{side}"] = {
                "max_code_delta":int(np.abs(np.asarray(Image.open(current),dtype=int)
                    -np.asarray(Image.open(baseline),dtype=int)).max()),
                "png_bytes_identical":current.read_bytes()==baseline.read_bytes()}
    print(json.dumps(result,indent=2))
    (out / "metrics.json").write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out",type=Path,required=True)
    parser.add_argument("--case",required=True)
    parser.add_argument("--frames",type=int,default=2048)
    parser.add_argument("--bounces",type=int,default=64)
    args = parser.parse_args()
    out = args.out.resolve()
    if args.case == "prepare":
        prepare(out)
        return
    if args.case == "metrics":
        metrics(out)
        return
    if args.case == "default":
        capture = runner.run_capture(str(out / "default"),
            str(HERE / "reflection-reference.uzproject"),100,clean=False,timeout=175,
            cvars={"r.TAA":1,"r.VolumetricFog":0,"r.TAA.LogDecisions":1,
                   "r.TechPanel.Show":0,"r.Toolbar.Show":0},
            screenshot_frames="95")
    elif args.case.startswith("stage1-"):
        extra = ["--path-trace"]
        if args.case == "stage1-back":
            extra += ["--camera-pos","0,250,140","--camera-look","0,0,140"]
        capture = runner.run_capture(str(out / args.case),
            str(HERE / "hair-stage1.uzproject"),128,clean=False,timeout=175,
            cvars={"r.TechPanel.Show":0,"r.Toolbar.Show":0,"r.Hair.MultiScatter":0},
            extra_args=extra,screenshot_frames="127")
    else:
        color, mode, *_ = args.case.split("-")
        frames = args.frames
        cvars = {"r.Hair.MultiScatter":int(mode!="off"),"r.Hair.MaxBounces":args.bounces}
        if mode.startswith("k"):
            cvars["r.Hair.ExplicitBounces"] = int(mode[1:])
        capture = runner.run_capture(str(out / args.case),str(out / f"{color}.uzproject"),
            frames+4,clean=False,timeout=175,cvars=cvars,
            extra_args=["--hidden","--path-trace"],
            regions=[("swatch",144,74,32,32)],region_fields="extra",
            region_first=31,region_last=frames-1,timing_first=32,
            screenshot_frames=f"63-{frames-1}:64")
    assert capture["exit_code"] == 0, capture["exit_code"]
    log = Path(capture["log"]).read_text(encoding="utf-8",errors="replace")
    assert "compile failed" not in log, capture["log"]
    if args.case != "default":
        assert "RT-PSO CreateStateObject succeeded" in log
        assert ("HairStage1:" if args.case.startswith("stage1-") else "HairDualStage2:") in log
    print(f'{args.case}: exit=0, clean shaders; {capture["log"]}',flush=True)


if __name__ == "__main__":
    main()
