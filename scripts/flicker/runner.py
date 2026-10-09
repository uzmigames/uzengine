"""Helpers to run `uzengine.exe` captures reproducibly and read what came out.

Every capture goes through `run_capture`, so the manifest, the log and the region-dump directory
always live together and a later comparison can prove the two runs were configured the same way.
"""
from __future__ import annotations

import datetime
import json
import os
import shutil
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))          # <repo>/scripts
REPO = os.path.dirname(REPO)                                                # <repo>
EXE = os.path.join(REPO, 'runtime', 'build', 'bin', 'uzengine.exe')


#: Dump retention, one rule for every study that takes repeats.
#:
#: Repeat 0's region dump is kept so a result can be re-examined; repeats above it are released once
#: measured, because a 128-frame capture of a full ROI runs to gigabytes. `--reuse` therefore has
#: exactly one repeat to work from, and studies must say so rather than trying to reload dumps that
#: were deliberately deleted — which is what an earlier version of the bias study did while offering
#: `--reuse` at the same time.
RETAINED_REPEAT = 0


def release_repeat_dump(work_dir: str, repeat_index: int) -> bool:
    """Releases a repeat's region dump unless it is the retained one. True when it was released."""
    if repeat_index == RETAINED_REPEAT:
        return False
    shutil.rmtree(os.path.join(work_dir, 'region'), ignore_errors=True)
    return True


def require_region_dir(path: str, what: str) -> str:
    """Fails with the retention rule spelled out, instead of a bare FileNotFoundError."""
    if not os.path.isdir(path) or not any(f.endswith('.bin') for f in os.listdir(path)):
        raise SystemExit(
            f'{what}: no region dump at {path}. Only repeat {RETAINED_REPEAT} is retained '
            f'(see runner.RETAINED_REPEAT), so --reuse can only re-read that one; re-run without '
            '--reuse to capture again.')
    return path


#: Shaders already failing to compile before this work, excluded from the gate by name rather than
#: by weakening it. `rt_uv_test_lib.hlsl` needs a `[raypayload]` annotation and is unused by these
#: captures.
KNOWN_BROKEN_SHADERS = ('rt_uv_test_lib.hlsl',)


def reuse_repeat_range(repeats: int, reuse: bool) -> range:
    """Which repeats a study should iterate.

    Only the retained repeat survives on disk, so `--reuse` re-reads that one instead of failing on
    the repeats that were deliberately released. A study using this must say in its output that a
    reused run covers one repeat, since "worst of three" and "the only one" are different claims.
    """
    if reuse:
        return range(RETAINED_REPEAT, RETAINED_REPEAT + 1)
    return range(repeats)


def _hash_file(path: str) -> str:
    """SHA-256 of a whole file, so a binary is identified by its contents and not by its date."""
    import hashlib
    h = hashlib.sha256()
    with open(path, 'rb') as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def report_path(out_dir: str, filename: str, reuse: bool) -> str:
    """Where a study writes its report.

    A --reuse run re-reads only the retained repeat, so it must never overwrite the report of a full
    run. On 2026-09-15 three studies did exactly that — door_stages.json, wall_output.json and
    wall_motion_gate.json were each re-analysed with --reuse, and each silently replaced three-repeat
    evidence with one-repeat evidence under the same filename. Reuse reports get their own name.
    """
    stem, ext = os.path.splitext(filename)
    return os.path.join(out_dir, f'{stem}.reuse{ext}' if reuse else filename)


def harness_stamp() -> dict:
    """Identity of the executable present RIGHT NOW.

    Use `capture_identity` instead when reporting on a capture. This stamps the binary at the moment
    of the call, which during a re-analysis is whatever is on disk then — not necessarily what
    produced the dumps being read. Size and modification time are also weak identifiers, so the
    content hash is what actually names the build.
    """
    try:
        st = os.stat(EXE)
        return {'executable': EXE, 'size_bytes': st.st_size, 'sha256': _hash_file(EXE),
                'modified': datetime.datetime.fromtimestamp(st.st_mtime).isoformat(timespec='seconds'),
                'captured_at_analysis_time': True}
    except OSError as e:                                          # noqa: BLE001
        return {'executable': EXE, 'error': str(e)}


def capture_identity(work_dir: str) -> dict:
    """The identity a REPORT should carry: inherited from the capture's own manifest.

    Every capture writes `manifest.json` with the executable it ran and the CVars it ran under. A
    report built by re-reading dumps must take its identity from there, because the binary on disk
    at analysis time may be a different build entirely — which is exactly how a full evaluation once
    ran against an engine that did not contain the change under test.

    Falls back to naming the manifest as missing rather than silently substituting the current
    executable, since a wrong identity is worse than an absent one.
    """
    path = os.path.join(work_dir, 'manifest.json')
    try:
        with open(path, encoding='utf-8') as fh:
            m = json.load(fh)
    except (OSError, ValueError) as e:                            # noqa: BLE001
        return {'source': path, 'error': f'manifest unavailable: {e}',
                'inherited_from_manifest': False}
    out = {'source': path, 'inherited_from_manifest': True,
           'executable': m.get('executable'), 'schema': m.get('schema'),
           'resolution': m.get('resolution'), 'cvars_non_default': m.get('cvars_non_default'),
           'cli': m.get('cli')}
    stamp = m.get('harness') or m.get('executable_identity')
    if stamp:
        out['executable_identity'] = stamp
    return out


def rect_arg(rects) -> str:
    """Formats `[(name, x, y, w, h), ...]` for `--regions`."""
    return ';'.join(f'{n}:{x},{y},{w},{h}' for n, x, y, w, h in rects)


def run_capture(out_dir, project, frames, *, regions=None, region_fields='taa',
                region_first=None, region_last=None, cvars=None, extra_args=(),
                screenshot_frames=None, timing_first=200, clean=True, timeout=1800):
    """Runs one capture into `out_dir` (region/, frames/, manifest.json, run.log).

    Returns a dict with the paths, the process exit code and the parsed manifest.
    """
    if clean and os.path.isdir(out_dir):
        shutil.rmtree(out_dir)
    region_dir = os.path.join(out_dir, 'region')
    os.makedirs(region_dir, exist_ok=True)
    manifest_path = os.path.join(out_dir, 'manifest.json')
    log_path = os.path.join(out_dir, 'run.log')

    cmd = [EXE, '--project', project, '--frames', str(frames), '--verbose',
           '--run-manifest', manifest_path, '--timing-first', str(timing_first)]
    for name, value in (cvars or {}).items():
        cmd += ['--cvar', f'{name}={value}']
    if regions:
        cmd += ['--region-dump', region_dir, '--regions', rect_arg(regions),
                '--region-fields', region_fields]
        if region_first is not None:
            cmd += ['--region-first', str(region_first)]
        if region_last is not None:
            cmd += ['--region-last', str(region_last)]
    if screenshot_frames:
        os.makedirs(os.path.join(out_dir, 'frames'), exist_ok=True)
        cmd += ['--screenshot', os.path.join(out_dir, 'frames', 'f.png'),
                '--screenshot-frames', screenshot_frames]
    cmd += list(extra_args)

    with open(log_path, 'wb') as log:
        proc = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, timeout=timeout)

    # Shaders are compiled when the engine loads them, not by `zig build`, so a shader that cannot
    # compile looks exactly like a successful build and the pass using it silently does not run. That
    # cost a 14-capture evaluation on 2026-09-11, where the deferred lighting shader failed and every
    # file came back with `lighting_stamp_missing`. Fail here instead, with the compiler's own words.
    with open(log_path, 'rb') as fh:
        log_text = fh.read().decode('utf-8', 'replace')
    # A device removal invalidates everything in flight and looks nothing like a measurement.
    # 0x887A0005 is DXGI_ERROR_DEVICE_REMOVED: a driver reset or TDR, usually transient under a long
    # batch of back-to-back captures. Name it, so it is never read as a result for whatever CVar
    # happened to be set when it struck.
    if '0x887A0005' in log_text or 'DEVICE_REMOVED' in log_text:
        raise RuntimeError(
            f'{out_dir}: the GPU device was removed during this capture (DXGI_ERROR_DEVICE_REMOVED, '
            '0x887A0005). Every allocation after that point failed, so nothing was measured. This is '
            'a driver/TDR event, not a property of the settings under test — re-run this condition. '
            f'Log: {log_path}')

    broken = []
    for line in log_text.splitlines():
        if 'compile failed for' not in line:
            continue
        if any(known in line for known in KNOWN_BROKEN_SHADERS):
            continue
        broken.append(line.strip())
    if broken:
        raise RuntimeError(
            f'{out_dir}: {len(broken)} shader(s) failed to compile, so the passes using them did '
            f'not run. Nothing was measured. First: {broken[0]}. Log: {log_path}')
    manifest = None
    if os.path.exists(manifest_path):
        with open(manifest_path, encoding='utf-8') as f:
            manifest = json.load(f)

    # A capture whose window was closed by hand exits cleanly with code 0, having rendered a
    # handful of frames, and every downstream script then fails with a confusing "no region_*.bin".
    # Say what actually happened instead: these runs open a real 1920x1080 window (the engine's
    # `headless` mode only creates a device and exits, it cannot render), so closing one aborts
    # that measurement.
    if manifest:
        rendered = manifest.get('frames', {}).get('rendered', 0)
        if rendered < frames:
            with open(log_path, 'rb') as f:
                closed = b'Window close requested' in f.read()
            raise RuntimeError(
                f'{out_dir}: capture interrupted after {rendered} of {frames} frames'
                + (' — the render window was closed' if closed else '')
                + f'. Nothing was measured; re-run it. Log: {log_path}')
    return {
        'dir': out_dir,
        'region_dir': region_dir,
        'frames_dir': os.path.join(out_dir, 'frames'),
        'manifest_path': manifest_path,
        'manifest': manifest,
        'log': log_path,
        'exit_code': proc.returncode,
        'command': cmd,
    }


def log_lines(result, needle):
    """Lines of a run's log containing `needle` (the engine logs UTF-8)."""
    with open(result['log'], 'rb') as f:
        text = f.read().decode('utf-8', 'replace')
    return [ln for ln in text.splitlines() if needle in ln]


def png_array(path):
    """Loads a PNG as an (h, w, 3) uint8 array (display code values, sRGB)."""
    from PIL import Image
    import numpy as np
    with Image.open(path) as im:
        return np.asarray(im.convert('RGB'))


def source_fingerprint():
    """Hashes the shader sources and the diagnostic scripts a capture depends on.

    The manifest the engine writes identifies the executable; this identifies the *sources* that
    produced it, so a package can be checked against a later working tree (the engine embeds the
    shaders, but they are also compiled from disk when the cache misses).
    """
    import hashlib
    roots = [os.path.join(REPO, 'runtime', 'shaders'),
             os.path.join(REPO, 'scripts', 'flicker')]
    files, digest = [], hashlib.sha256()
    for root in roots:
        for dirpath, _, names in os.walk(root):
            for n in sorted(names):
                if not n.endswith(('.hlsl', '.hlsli', '.py')):
                    continue
                path = os.path.join(dirpath, n)
                with open(path, 'rb') as f:
                    data = f.read()
                rel = os.path.relpath(path, REPO).replace(os.sep, '/')
                digest.update(rel.encode())
                digest.update(hashlib.sha256(data).digest())
                files.append(rel)
    return {'files': len(files), 'sha256': digest.hexdigest(), 'roots': [os.path.relpath(r, REPO) for r in roots]}


def require_exe():
    if not os.path.exists(EXE):
        print(f'uzengine.exe not found at {EXE} — build first (cd runtime && zig build)', file=sys.stderr)
        raise SystemExit(2)
    return EXE
