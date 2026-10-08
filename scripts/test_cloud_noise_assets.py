"""Cloud noise assets reachable from uzengine.exe (Rulebook task phase0_clouds-noise-assets, from the clouds rewrite's
0.B4). CloudPass::LoadNoiseTextures read atmosphere/shape.bin and shape_detail.bin through the project's VFS (the
project's content root, e.g. sample/content) and then the working directory; the files live in runtime/content/atmosphere
and nothing deployed them next to the exe since the pbr_test demo's copy step went away (1bc7c943). So every project ran
without the detail erosion, and the load was retried, with a FileSystem warning, every frame.

Gates (pre-registered in the task before the first run):
  N1  the cloud_test run's log reports both noise volumes loaded ('CloudPass: shape noise loaded' and
      'CloudPass: detail noise loaded'); before: 'could not load atmosphere/shape.bin'.
  N2  the noise files are looked up once: at most two log lines over the run mention atmosphere/shape.bin or
      atmosphere/shape_detail.bin (before: one FileSystem warning per frame).
  N3  the run completes without a device removal and the manifest's gpu_ms reports the cloud compute pass (the
      clouds task's 0.12, no TDR on the current shader), before and after.
  N4  bin/content/atmosphere/shape.bin and shape_detail.bin are installed byte-identical to runtime/content/atmosphere.
  N5  reported: the change of the sky with the detail erosion on (mean |delta| of the screenshot's linearised
      luminance over the frame against the before capture), the cloud pass cost before / after, and a montage for the user.

Usage: python scripts/test_cloud_noise_assets.py [--before] [--reuse]
  --before: capture into test-cloud-noise/before on the current binary (the engine change not built yet).
"""
import argparse
import hashlib
import json
import os
import re
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))
import regiondump  # noqa: E402
import runner  # noqa: E402

REPO = runner.REPO
OUT = os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-cloud-noise')
PROJECT = os.path.join(REPO, 'sample', 'projects', 'cloud_test.uzproject')
W, H = 1280, 720          # cloud_test.uzproject's window
FRAMES, SHOT = 180, 170
SRC = os.path.join(REPO, 'runtime', 'content', 'atmosphere')
DST = os.path.join(REPO, 'runtime', 'build', 'bin', 'content', 'atmosphere')
NOISE = ('shape.bin', 'shape_detail.bin')
REC709 = np.array([0.2126, 0.7152, 0.0722])


def capture(work, reuse):
    if reuse and os.path.isfile(os.path.join(work, 'manifest.json')):
        return json.load(open(os.path.join(work, 'manifest.json')))
    res = runner.run_capture(work, PROJECT, FRAMES, cvars={'r.TechPanel.Show': 0, 'r.Toolbar.Show': 0},
                             timing_first=60, clean=True, timeout=900, screenshot_frames=str(SHOT))
    if res['exit_code'] != 0:
        raise SystemExit(f'{work}: engine exited {res["exit_code"]}')
    return json.load(open(os.path.join(work, 'manifest.json')))


def log_text(work):
    return open(os.path.join(work, 'run.log'), 'rb').read().decode('utf-8', 'replace')


def sha(path):
    return hashlib.sha256(open(path, 'rb').read()).hexdigest() if os.path.isfile(path) else None


def colour(work):
    """The frame's screenshot (8-bit sRGB, the project runs without TAA so the region dump's TAA field is not
    written), decoded to linear."""
    from PIL import Image
    d = os.path.join(work, 'frames')
    f = sorted(x for x in os.listdir(d) if x.endswith('.png'))[-1]
    c = np.asarray(Image.open(os.path.join(d, f)).convert('RGB'), dtype=np.float64) / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def montage(panels, path):
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return None
    tiles = []
    for label, work in panels:
        d = os.path.join(work, 'frames')
        files = sorted(f for f in os.listdir(d) if f.endswith('.png')) if os.path.isdir(d) else []
        if not files:
            continue
        im = Image.open(os.path.join(d, files[-1])).convert('RGB')
        ImageDraw.Draw(im).text((8, 8), label, fill=(255, 255, 0))
        tiles.append(im)
    if not tiles:
        return None
    sheet = Image.new('RGB', (sum(t.width for t in tiles), tiles[0].height))
    x = 0
    for t in tiles:
        sheet.paste(t, (x, 0))
        x += t.width
    sheet.save(path, quality=88)
    return path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--before', action='store_true')
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    results, report = [], {}

    def check(cid, ok, detail):
        results.append((cid, bool(ok), detail))
        print(f'  [{"PASS" if ok else "FAIL"}] {cid} {detail}', flush=True)

    work = os.path.join(OUT, 'before' if args.before else 'after')
    man = capture(work, args.reuse)
    text = log_text(work)
    shape_loaded = 'CloudPass: shape noise loaded' in text
    detail_loaded = 'CloudPass: detail noise loaded' in text
    noise_lines = [l for l in text.splitlines() if re.search(r'atmosphere[/\\]shape(_detail)?\.bin', l)]
    gpu = man.get('gpu_ms') or {}
    cloud_ms = {k: v for k, v in gpu.items() if k.lower().startswith('cloud')}
    report.update({'shape_loaded': shape_loaded, 'detail_loaded': detail_loaded, 'noise_log_lines': len(noise_lines),
                   'noise_log_examples': noise_lines[:3], 'cloud_gpu_ms': cloud_ms, 'exe': man.get('executable', {}).get('hash')})
    print(f'  log: shape loaded {shape_loaded}, detail loaded {detail_loaded}, {len(noise_lines)} lines about the noise files; cloud gpu_ms {cloud_ms}', flush=True)
    check('N1 both noise volumes loaded', shape_loaded and detail_loaded, f'shape {shape_loaded}, detail {detail_loaded}')
    check('N2 the noise files are looked up once (at most two log lines about them)', len(noise_lines) <= 2,
          f'{len(noise_lines)} lines' + (f', e.g. {noise_lines[0][:140]}' if noise_lines else ''))
    check('N3 the run completes without device removal and gpu_ms reports the cloud compute pass', bool(cloud_ms),
          f'exit 0, {cloud_ms}')
    if not args.before:
        same = {f: (sha(os.path.join(SRC, f)), sha(os.path.join(DST, f))) for f in NOISE}
        check('N4 the noise files are installed next to the exe, byte-identical', all(a and a == b for a, b in same.values()),
              '; '.join(f'{f}: {"identical" if a and a == b else ("missing" if not b else "different")}' for f, (a, b) in same.items()))
        before = os.path.join(OUT, 'before')
        if os.path.isfile(os.path.join(before, 'manifest.json')):
            cb, ca = colour(before), colour(work)
            d = np.abs((ca - cb) @ REC709)
            bman = json.load(open(os.path.join(before, 'manifest.json')))
            bcloud = {k: v for k, v in (bman.get('gpu_ms') or {}).items() if k.lower().startswith('cloud')}
            report['n5'] = {'mean_abs_delta_lum': float(d.mean()), 'p99_abs_delta_lum': float(np.percentile(d, 99)),
                            'changed_px_frac': float((d > 1e-3).mean()), 'cloud_ms_before': bcloud, 'cloud_ms_after': cloud_ms}
            report['montage'] = montage([('before: no noise files reached', before), ('after: shape + detail noise loaded', work)],
                                        os.path.join(OUT, 'cloud_noise_montage.jpg'))
            print(f'  N5 [reported] sky change: mean |dlum| {d.mean():.5f}, p99 {np.percentile(d, 99):.4f}, {100 * (d > 1e-3).mean():.1f} % of pixels changed; '
                  f'cloud gpu_ms {bcloud} -> {cloud_ms}', flush=True)
    passed = sum(1 for _, ok, _ in results if ok)
    print(f'{passed}/{len(results)} checks passed', flush=True)
    os.makedirs(work, exist_ok=True)
    with open(os.path.join(work, 'cloud_noise.json'), 'w', encoding='utf-8') as fh:
        json.dump({'report': report, 'checks': [{'id': c, 'pass': ok, 'detail': str(d)} for c, ok, d in results]}, fh, indent=1, default=str)
    return 0 if passed == len(results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
