"""Bounded stage-1 captures. PNG metrics are display metrics, never linear-HDR claims."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import sys
import numpy as np
from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "flicker"))
import runner
import regiondump
REPO = HERE.parent.parent
ROI = (35, 85, 205, 270)

def pixels(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.float64)

def roi(a):
    x0,y0,x1,y1 = ROI
    return a[y0:y1,x0:x1]

def last_screenshot(directory):
    return max(directory.glob("f_*.png"),key=lambda p:int(p.stem[2:]))

def metrics(out):
    report = {"metric_domain": "PNG RGB code values; not linear HDR", "roi_xyxy": ROI}
    for suffix in ("default", "denoise"):
        before, after = out/f"baseline-{suffix}/frames/f_95.png", out/f"off-{suffix}/frames/f_95.png"
        if before.exists() and after.exists():
            delta = np.abs(pixels(before)-pixels(after))
            report[f"off_{suffix}_max_code_delta"] = int(delta.max())
        else:
            report[f"off_{suffix}_status"] = "not captured"
    for pose in ("static","motion"):
        pt = pixels(last_screenshot(out/f"pt-{pose}/frames"))
        results = {}
        for mode in ("legacy","unified"):
            frames = [95] if pose=="static" else range(64,72)
            images = [roi(pixels(out/f"{pose}-{mode}/frames/f_{f}.png")) for f in frames]
            errors = [float(np.mean((a-roi(pt))**2)) for a in images]
            results[mode] = {"mse": float(np.mean(errors)), "rmse": float(np.sqrt(np.mean(errors))),
                             "mean": float(np.mean(images)), "p99": float(np.percentile(images,99))}
        results["pt"] = {"mean": float(roi(pt).mean()), "p99": float(np.percentile(roi(pt),99))}
        results["mse_reduction_percent"] = 100*(1-results["unified"]["mse"]/results["legacy"]["mse"])
        report[pose] = results
    canvas = Image.new("RGB",(640*3,390))
    draw = ImageDraw.Draw(canvas)
    for i,(case,label,frame) in enumerate((("motion-legacy","Current",64),
                                        ("motion-unified","Unified",64),("pt-motion","PT fixed pose",None))):
        path=out/case/f"frames/f_{frame}.png" if frame is not None else last_screenshot(out/case/"frames")
        canvas.paste(Image.open(path).convert("RGB"),(640*i,30))
        draw.text((640*i+12,8),label,fill="white")
    canvas.save(out/"motion-montage.png")
    for size in ("","1080-","1440-"):
        for mode in ("legacy","unified"):
            path=out/f"timing-{size}{mode}/manifest.json"
            if path.exists():
                m=json.loads(path.read_text())
                report[f"timing_{size}{mode}"]={"resolution":m["resolution"],
                    "gpu_mean_ms":m.get("gpu_ms"),"wall_frame_ms":m.get("frame_ms")}
    for pose in ("static","motion"):
        reference = out/f"hdr-pt-{pose}/region"
        if not regiondump.sequence_paths(str(reference)):
            continue
        # The CLI's "taa" preset also requests deferred-lighting records. PT does
        # not run deferred lighting. Validate the TAA channel's own stamp instead;
        # no other missing/mismatched record is accepted.
        reference_frame=regiondump.sequence_paths(str(reference))[-1][0]
        ptframes = regiondump.load_sequence(str(reference),reference_frame,reference_frame,require_valid=False)
        for frame in ptframes:
            assert frame.flags & regiondump.FLAGS_INVALID == regiondump.FLAG_LIGHTING_MISSING
            assert frame.rect("block").taa_stamp == frame.frame
        pt = ptframes[0].rect("block").channel("taa_raw")[...,:3]
        values = {}
        for mode in ("legacy","unified"):
            first,last = (88,95) if pose=="static" else (64,71)
            sequence = regiondump.load_sequence(str(out/f"hdr-{pose}-{mode}/region"),first,last)
            images = np.stack([f.rect("block").channel("taa_raw")[...,:3] for f in sequence])
            assert np.isfinite(images).all() and np.isfinite(pt).all()
            luminance = images @ np.asarray(regiondump.REC709)
            pt_luminance = pt @ np.asarray(regiondump.REC709)
            values[mode] = {"mse":float(np.mean((images-pt)**2)),
                            "rmse":float(np.sqrt(np.mean((images-pt)**2))),
                            "mean_luminance":float(luminance.mean()),
                            "mean_bias_percent":float(100*(luminance.mean()/pt_luminance.mean()-1)),
                            "p99_luminance":float(np.percentile(luminance,99)),
                            "temporal_std_mean":float(np.std(luminance,axis=0).mean())}
        values["pt"]={"mean_luminance":float(pt_luminance.mean()),
                      "p99_luminance":float(np.percentile(pt_luminance,99))}
        values["mse_reduction_percent"]=100*(1-values["unified"]["mse"]/values["legacy"]["mse"])
        report[f"pre_taa_hdr_{pose}"]=values
        report[f"pre_taa_hdr_{pose}"]["reference_stamp"] = (
            "TAA independently matches frame; absent deferred-lighting stamp expected on PT")
        report[f"pre_taa_hdr_{pose}"]["reference_frame"] = reference_frame
        shorter=out/f"hdr-pt-{pose}-short/region"
        if regiondump.sequence_paths(str(shorter)):
            short_frame=regiondump.sequence_paths(str(shorter))[-1][0]
            short_sequence=regiondump.load_sequence(str(shorter),short_frame,short_frame,require_valid=False)
            short=short_sequence[0]
            assert short.flags & regiondump.FLAGS_INVALID == regiondump.FLAG_LIGHTING_MISSING
            assert short.rect("block").taa_stamp == short.frame
            short_rgb=short.rect("block").channel("taa_raw")[...,:3]
            short_luminance=short_rgb @ np.asarray(regiondump.REC709)
            values["reference_doubling"]={
                "frames":[short_frame,reference_frame],
                "mean_change_percent":float(100*abs(short_luminance.mean()-pt_luminance.mean())/pt_luminance.mean()),
                "rmse":float(np.sqrt(np.mean((short_rgb-pt)**2))),
                "max_reference_rmse":values["unified"]["rmse"]/5}
    report["capture_error_lines"]={}
    for path in out.glob("*/run.log"):
        errors=[line for line in path.read_text(encoding="utf-8",errors="replace").splitlines()
                if "] [error]" in line or "compile failed" in line or "DEVICE_REMOVED" in line]
        if errors: report["capture_error_lines"][path.parent.name]=errors
    (out/"metrics.json").write_text(json.dumps(report,indent=2))
    print(json.dumps(report,indent=2))

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--out",type=Path,required=True)
    p.add_argument("--case",required=True)
    p.add_argument("--baseline-exe")
    p.add_argument("--width",type=int)
    p.add_argument("--height",type=int)
    p.add_argument("--reference-frames",type=int,default=512)
    a=p.parse_args()
    if a.case=="metrics":
        metrics(a.out); return
    if a.baseline_exe: runner.EXE=a.baseline_exe
    case=a.case
    hdr=case.startswith("hdr-")
    if hdr: case=case[4:]
    project=HERE/"rt-reflections-glossy.uzproject"
    frames,shots,args=100,"95",[]
    cv={"r.TechPanel.Show":0,"r.Toolbar.Show":0,"r.SSR":0,
        "r.RayTracing.Reflections":1,"r.RayTracing.Reflections.MaxRoughness":1,
        "r.RayTracing.Reflections.Glossy":1,"r.RayTracing.Reflections.Indirect":1,
        "r.RayTracing.Reflections.Denoise":1,"r.Denoiser.Unified":int("unified" in case)}
    if case.startswith(("baseline-","off-")):
        if case.endswith("default"):
            project=HERE/"reflection-reference.uzproject"
            cv={"r.TechPanel.Show":0,"r.Toolbar.Show":0,"r.TAA":1,"r.VolumetricFog":0,
                "r.Denoiser.Unified":0}
    elif case.startswith("motion-"):
        project=HERE/"unified-denoiser-motion.uzproject"
        frames,shots=80,"62-72"
    elif case.startswith("pt-"):
        frames=a.reference_frames
        shots=str(frames-1)
        args=["--path-trace"]
        if case.startswith("pt-motion"):
            args+=["--camera-pos=50,0,150","--camera-look=50,600,150"]
    elif case.startswith("timing-"):
        frames,shots=300,None
    region_args={}
    if hdr:
        cv.update({"r.TAA":1,"r.TAA.JitterScale":0})
        first,last=(64,71) if case.startswith("motion-") else ((frames-1,frames-1) if case.startswith("pt-") else (88,95))
        region_args={"regions":[("block",35,85,170,185)],"region_fields":"taa",
                     "region_first":first,"region_last":last}
    if a.baseline_exe: cv.pop("r.Denoiser.Unified",None)
    if a.width: args+=["--width",str(a.width)]
    if a.height: args+=["--height",str(a.height)]
    result=runner.run_capture(str(a.out/a.case),str(project),frames,clean=False,timeout=175,
                              cvars=cv,extra_args=args,screenshot_frames=shots,timing_first=100,**region_args)
    assert result["exit_code"]==0,result
    print(case,"exit=0",result["frames_dir"],flush=True)

if __name__=="__main__": main()
