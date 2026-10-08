"""Every CVar is registered once (block A.4 / A.8: `r.AerialPerspective` was registered with two defaults and the
registry keeps the first, so the effective default depended on static-initialisation order; the `r.SSR.*`
copies of block B.5 were never read). The registry is idempotent (`cvar.cpp`: a second registration returns
the first), which is exactly why a second registration with another default or range is a trap.

  C1  across runtime/engine, runtime/tools and runtime/demos, every `UZ_CVAR_*("name", ...)` name appears
      exactly once (the duplicates are listed).
  C2  negative control: the fixture `scripts/test_cvar_registrations_fixture/duplicate.cpp` registers a name
      twice and the audit of that file alone reports it.
  C3  no CVar is registered in a header (cvar.h: "NEVER declare in .h files").
  C4  (--gpu) the SSR passes run with the surviving registrations: the reflection reference captured with
      `r.SSR 1` reports SSR_* pass timings (<= 3.0 ms, block B.5's R5 bound) and the `r.SSR.*` values the engine
      reports are the defaults of the registrations that survive (parsed from the source).

Usage: python scripts/test_cvar_registrations.py [--gpu] [--reuse]
"""
import argparse
import collections
import json
import os
import re
import sys

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
ROOTS = [os.path.join(REPO, 'runtime', 'engine'), os.path.join(REPO, 'runtime', 'tools'), os.path.join(REPO, 'runtime', 'demos')]
PATTERN = re.compile(r'UZ_CVAR_(?:FLOAT|BOOL|INT)\s*\(\s*"([^"]+)"')
FIXTURE = os.path.join(REPO, 'scripts', 'test_cvar_registrations_fixture', 'duplicate.cpp')


def strip_comments(text):
    """Blanks `//` and `/* */` comments (keeping the newlines, so line numbers hold): a registration quoted in
    a doc comment (cvar.h's usage example) is not a registration."""
    out, i, n = [], 0, len(text)
    while i < n:
        if text.startswith('//', i):
            j = text.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(''.join('\n' if ch == '\n' else ' ' for ch in text[i:j]))
            i = j
        elif text[i] == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == '\\' else 1
            out.append(text[i:j + 1])
            i = j + 1
        else:
            out.append(text[i])
            i += 1
    return ''.join(out)


def registrations(paths):
    found = collections.defaultdict(list)
    for root in paths:
        if os.path.isfile(root):
            files = [root]
        else:
            files = [os.path.join(d, f) for d, _, fs in os.walk(root) for f in fs if f.endswith(('.cpp', '.h', '.hpp', '.cc'))]
        for f in files:
            try:
                text = open(f, encoding='utf-8', errors='replace').read()
            except OSError:
                continue
            text = strip_comments(text)
            for m in PATTERN.finditer(text):
                line = text.count('\n', 0, m.start()) + 1
                found[m.group(1)].append((os.path.relpath(f, REPO), line))
    return found


def source_defaults(names):
    """The default each surviving registration declares (`UZ_CVAR_*("name", default, ...)`), as a number."""
    out = {}
    for root in ROOTS:
        for d, _, fs in os.walk(root):
            for f in fs:
                if not f.endswith(('.cpp', '.cc')):
                    continue
                text = strip_comments(open(os.path.join(d, f), encoding='utf-8', errors='replace').read())
                for name in names:
                    m = re.search(r'UZ_CVAR_(?:FLOAT|BOOL|INT)\s*\(\s*"' + re.escape(name) + r'"\s*,\s*([^,)]+)', text)
                    if m:
                        tok = m.group(1).strip().rstrip('fF')
                        out[name] = 1.0 if tok == 'true' else 0.0 if tok == 'false' else float(tok)
    return out


SSR_NAMES = ['r.SSR.MaxRoughness', 'r.SSR.NumSteps', 'r.SSR.StartMipLevel', 'r.SSR.SlopeToleranceScale', 'r.SSR.Composite']
C4_MS = 3.0


def gpu_checks(check, reuse):
    sys.path.insert(0, os.path.join(REPO, 'scripts'))
    import reflection_reference as ref  # noqa: E402
    from test_layered_reflections import capture  # noqa: E402
    work = os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-cvar-registrations', 'ref-ssr-on')
    man = capture(work, ref.PROJECT, reuse, {'r.SSR': 1})
    gpu = man.get('gpu_ms') or {}
    ssr = {k: round(v, 3) for k, v in gpu.items() if k.startswith('SSR')}
    total = sum(ssr.values())
    check(f'C4 the SSR passes run with the surviving registrations (reference scene, r.SSR 1): SSR_* timings present and <= {C4_MS} ms',
          bool(ssr) and total <= C4_MS, f'{ssr} total {total:.3f} ms')
    defaults = source_defaults(SSR_NAMES)
    eff = man.get('cvars') or {}
    bad = {n: (eff.get(n), defaults.get(n)) for n in SSR_NAMES if n not in defaults or eff.get(n) is None or abs(float(eff[n]) - defaults[n]) > 1e-6}
    check('C4b the r.SSR.* values the engine reports are the surviving registrations\' defaults', not bad,
          f'{defaults}' if not bad else f'mismatch {bad}')
    return {'ssr_ms': ssr, 'ssr_total_ms': total, 'defaults': defaults, 'reported': {n: eff.get(n) for n in SSR_NAMES}}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--gpu', action='store_true', help='C4: capture the reference scene with SSR on')
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    results, report = [], {}

    def check(cid, ok, detail):
        results.append((cid, bool(ok), detail))
        print(f'  [{"PASS" if ok else "FAIL"}] {cid} {detail}', flush=True)

    reg = registrations(ROOTS)
    dups = {n: locs for n, locs in reg.items() if len(locs) > 1}
    check('C1 every CVar is registered once across runtime/engine, tools and demos', not dups,
          f'{len(reg)} names; duplicates: ' + ('; '.join(f'{n}: {locs}' for n, locs in sorted(dups.items())) if dups else 'none'))
    fx = registrations([FIXTURE]) if os.path.isfile(FIXTURE) else {}
    fx_dups = {n: locs for n, locs in fx.items() if len(locs) > 1}
    check('C2 negative control: the fixture with a name registered twice is reported', bool(fx_dups), f'{list(fx_dups)}' if fx_dups else 'fixture missing or not detected')
    in_headers = {n: [l for l in locs if l[0].endswith(('.h', '.hpp'))] for n, locs in reg.items()}
    in_headers = {n: l for n, l in in_headers.items() if l}
    check('C3 no CVar is registered in a header', not in_headers, '; '.join(f'{n}: {l}' for n, l in in_headers.items()) if in_headers else 'none')
    report['names'] = len(reg)
    if args.gpu:
        report['c4'] = gpu_checks(check, args.reuse)
    passed = sum(1 for _, ok, _ in results if ok)
    print(f'{passed}/{len(results)} checks passed')
    out = os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-cvar-registrations')
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, 'cvar_registrations.json'), 'w', encoding='utf-8') as fh:
        json.dump({'report': report, 'checks': [{'id': c, 'pass': ok, 'detail': str(d)} for c, ok, d in results]}, fh, indent=1, default=str)
    return 0 if passed == len(results) else 1


if __name__ == '__main__':
    sys.exit(main())
