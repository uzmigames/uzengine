"""Validate report artifacts only; does not run engine or integration tests."""
from pathlib import Path
from collections import Counter
from html.parser import HTMLParser
from urllib.parse import urlsplit, unquote
import hashlib
import json
import re
import ast
import markdown
from PIL import Image

BASE = Path(__file__).resolve().parents[1]
ROOT = BASE.parents[2]


class Links(HTMLParser):
    def __init__(self):
        super().__init__()
        self.links, self.ids = [], []
        self.missing_alt = 0

    def handle_starttag(self, tag, attrs):
        data = dict(attrs)
        self.links.extend(data[k] for k in ("href", "src") if data.get(k))
        if "id" in data:
            self.ids.append(data["id"])
        if tag == "img" and not data.get("alt"):
            self.missing_alt += 1


def main():
    errors = []
    for source in [*BASE.rglob("*.md"), *BASE.glob("tools/*.py")]:
        content = source.read_text(encoding="utf-8")
        for number, line in enumerate(content.splitlines(), 1):
            if line.rstrip() != line:
                errors.append(f"Trailing whitespace: {source.name}:{number}")
        if source.suffix == ".py":
            ast.parse(content, filename=str(source))
    chapters = sorted(BASE.glob("[0-9][0-9]-*.md"))
    if len(chapters) != 9:
        errors.append("Expected nine numbered chapters")
    findings, gaps, link_count = [], [], 0
    markdown_files = [BASE/"README.md", *chapters, BASE/"images/README.md"]
    for path in markdown_files:
        text = path.read_text(encoding="utf-8")
        if path in chapters:
            findings.extend(re.findall(r"^#{2,3}\s+(F-\d{3})(?:[.\s])", text, re.M))
            findings.extend(re.findall(r"^\|\s*(F-\d{3})\s*\|", text, re.M))
            gaps.extend(re.findall(r"^\|\s*(G-\d{3})\s", text, re.M))
        parser = Links()
        parser.feed(markdown.markdown(text, extensions=["tables", "fenced_code"]))
        for link in parser.links:
            url = urlsplit(link)
            if url.scheme or url.netloc or not url.path:
                continue
            link_count += 1
            target = (path.parent/unquote(url.path)).resolve()
            if not target.is_file():
                errors.append(f"Missing local link in {path.name}: {link}")
        if parser.missing_alt:
            errors.append(f"Missing image alt text: {path.name}")
    for name, values, count in [("F", findings, 35), ("G", gaps, 45)]:
        expected = {f"{name}-{n:03}" for n in range(1, count+1)}
        if set(values) != expected:
            errors.append(f"{name} coverage mismatch: missing {expected-set(values)}, extra {set(values)-expected}")
        duplicates = [key for key, value in Counter(values).items() if value != 1]
        if duplicates:
            errors.append(f"Duplicate {name} definitions: {duplicates}")
    manifest = json.loads((BASE/"images/manifest.json").read_text(encoding="utf-8"))
    if manifest.get("revision") != 5 or manifest.get("navigation") != "document-type":
        errors.append("Expected document-oriented visual revision 5")
    visual_source = ast.parse((BASE/"tools/visual_system.py").read_text(encoding="utf-8"))
    palette = next(ast.literal_eval(node.value) for node in visual_source.body
                   if isinstance(node, ast.Assign)
                   and any(isinstance(target, ast.Name) and target.id == "C" for target in node.targets))
    for key in ("bg", "panel", "surface", "field", "line", "subtle", "text", "muted", "faint", "accent", "selection"):
        channels = [int(palette[key][i:i+2], 16) for i in (1, 3, 5)]
        if len(set(channels)) != 1:
            errors.append("Non-neutral UI palette token: "+key)
    runs = manifest["runtime_capture"]["runs"]
    if len(runs) != 7 or sum(len(run["frames"]) for run in runs) != 8:
        errors.append("Expected seven sample runs and eight retained frames")
    for run in runs:
        name=run["name"]
        capture = json.loads((BASE/f"images/sources/{name}-manifest.json").read_text(encoding="utf-8"))
        for frame in run["frames"]:
            evidence = next((item for item in capture["captures"] if item["frame"] == frame), None)
            if not evidence or evidence.get("assets_ready") is not True or evidence.get("preload") is not False:
                errors.append("Capture not ready: "+name)
                continue
            published=BASE/f"images/sources/{name}_{frame}.png"
            original=ROOT/evidence["path"]
            if not original.is_file() or published.read_bytes() != original.read_bytes():
                errors.append("Published capture differs from runtime output: "+name)
            with Image.open(published) as source:
                if source.size != tuple(capture["resolution"]["output"]):
                    errors.append("Capture dimensions mismatch: "+name)
                if all(lo == hi for lo, hi in source.convert("RGB").getextrema()):
                    errors.append("Blank source capture: "+name)
        log=(BASE/f"images/sources/{name}-run.log").read_text(encoding="utf-8")
        if name=="character":
            if run["exit_code"] != 1 or "6881280 bytes" not in log or "Assertion failed" not in log:
                errors.append("Character shutdown failure missing from provenance")
        elif run["exit_code"] != 0 or "Runtime exiting with code 0" not in log:
            errors.append("Capture exit evidence mismatch: "+name)
        if name.startswith("damaged-"):
            if capture["project"] != "sample/projects/damaged_helmet.uzproject":
                errors.append("Helmet capture must use original DamagedHelmet sample")
            expected={"quarter":[260,-273,165],"front":[0,-377,165],
                      "side":[377,0,165],"back":[0,377,165]}[name.removeprefix("damaged-")]
            if capture["camera"]["position"] != expected:
                errors.append("Helmet camera override was not applied: "+name)
    for item in manifest["sources"]:
        source = ROOT/item["path"]
        if not source.is_file() or hashlib.sha256(source.read_bytes()).hexdigest() != item["sha256"]:
            errors.append("Source hash mismatch: "+item["path"])
    if len(manifest["outputs"]) != 13:
        errors.append("Expected twelve main images and one contact sheet")
    with Image.open(ROOT/"icon.ico") as original, Image.open(BASE/"images/sources/uzmi-icon.png") as brand:
        if original.convert("RGBA").tobytes() != brand.convert("RGBA").tobytes():
            errors.append("Uzmi brand image differs from repository icon")
    for item in manifest["outputs"]:
        path = BASE/"images"/item["file"]
        if not path.is_file():
            errors.append("Missing image: "+item["file"])
            continue
        if hashlib.sha256(path.read_bytes()).hexdigest() != item["sha256"]:
            errors.append("Image hash mismatch: "+item["file"])
        with Image.open(path) as image:
            if image.size != (item["width"], item["height"]):
                errors.append("Dimension mismatch: "+item["file"])
            if all(lo == hi for lo, hi in image.convert("RGB").getextrema()):
                errors.append("Blank image: "+item["file"])
    page = BASE/"index.html"
    parser = Links()
    parser.feed(page.read_text(encoding="utf-8"))
    if len(parser.ids) != len(set(parser.ids)):
        errors.append("Duplicate HTML anchor IDs")
    for link in parser.links:
        url = urlsplit(link)
        if url.scheme or url.netloc:
            continue
        if not url.path:
            if url.fragment and unquote(url.fragment) not in parser.ids:
                errors.append("Missing HTML anchor: "+link)
        elif not (BASE/unquote(url.path)).is_file():
            errors.append("Missing HTML file link: "+link)
    if errors:
        raise SystemExit("\n".join(errors))
    print(f"PASS: {len(chapters)} chapters, {len(findings)} unique findings, {len(gaps)} unique gaps")
    print(f"PASS: {link_count} Markdown local links; HTML file links and {len(parser.ids)} unique anchors")
    print(f"PASS: {len(manifest['outputs'])} PNG hashes/dimensions/nonblank checks and source hashes")
    print("PASS: document-oriented revision 5, neutral UI palette and eight asset-ready runtime frames")
    print("PASS: lossless source copies, original DamagedHelmet project/cameras and character exit-1 provenance")
    print("PASS: original Uzmi icon preserved")
    print("PASS: Markdown/Python whitespace and Python syntax")
    print("Scope: document/artifact checks only; no engine, browser, provider or performance tests.")


if __name__ == "__main__":
    main()
