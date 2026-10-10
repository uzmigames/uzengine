"""Record source, dependency, toolchain and artifact identities for a build.

Run before and after building. A manifest records inputs; it does not certify a
successful build. Ignored native dependency inputs are included explicitly.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
BINARY_SUFFIXES = {".lib", ".dll", ".a", ".so", ".dylib"}
DEPENDENCY_SUFFIXES = BINARY_SUFFIXES | {".h", ".hpp", ".inl", ".inc", ".c", ".cpp", ".asm", ".s"}


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def git(root: Path, *args: str) -> bytes:
    return subprocess.check_output(["git", "-C", str(root), *args], stderr=subprocess.PIPE)


def fingerprint(root: Path, paths: list[Path]) -> dict:
    files = []
    for path in sorted(set(paths)):
        name = path.relative_to(root).as_posix()
        if path.is_symlink():
            files.append({"path": name, "symlink": str(path.readlink())})
        elif path.is_file():
            files.append({"path": name, "sha256": sha256(path)})
        else:
            files.append({"path": name, "missing": True})
    digest = hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest()
    return {"sha256": digest, "files": files}


def tool_version(command: str, *args: str) -> dict:
    executable = shutil.which(command)
    if executable is None:
        return {"status": "unavailable", "command": command}
    result = subprocess.run([executable, *args], capture_output=True, text=True, timeout=30)
    return {"status": "available" if result.returncode == 0 else "failed",
            "path": executable, "sha256": sha256(Path(executable)),
            "exit_code": result.returncode, "version": (result.stdout + result.stderr).strip()}


def dependency_inputs(root: Path) -> list[Path]:
    # SDK libraries under build/ are linked directly by build.zig. Preserve
    # their identities while excluding compiler intermediates and caches.
    return [p for p in root.rglob("*") if p.is_file()
            and p.suffix.lower() in DEPENDENCY_SUFFIXES
            and not {".git", ".zig-cache", "node_modules"}.intersection(p.relative_to(root).parts)
            and not any(part.startswith(".native.staging-") for part in p.relative_to(root).parts)
            and ("build" not in p.relative_to(root).parts or p.suffix.lower() in BINARY_SUFFIXES)]


def create_manifest(root: Path, zig: str, artifacts: list[Path], output: Path) -> dict:
    names = git(root, "ls-files", "-z", "--cached", "--others", "--exclude-standard")
    paths = [root / name.decode("utf-8") for name in names.split(b"\0") if name]
    paths = [p for p in paths if p.resolve() != output.resolve()]
    dependency_root = root / "runtime/thirdparty"
    dependencies = dependency_inputs(dependency_root)
    return {
        "schema_version": 1, "created_utc": datetime.now(timezone.utc).isoformat(),
        "root": str(root), "platform": platform.platform(),
        "head": git(root, "rev-parse", "HEAD").decode().strip(),
        "git_status": git(root, "status", "--porcelain=v1").decode(),
        "source": fingerprint(root, paths), "native_dependencies": fingerprint(root, dependencies),
        "tools": {"zig": tool_version(zig, "version"), "node": tool_version("node", "--version"),
                  "cmake": tool_version("cmake", "--version"),
                  "python": {"version": platform.python_version()}},
        "artifacts": [{"path": str(p), "sha256": sha256(p)} if p.is_file()
                      else {"path": str(p), "status": "missing"} for p in artifacts],
        "acceptance": "input inventory only; build/test logs are required separately",
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--zig", default="zig")
    parser.add_argument("--artifact", action="append", type=Path, default=[])
    args = parser.parse_args()
    manifest = create_manifest(args.root.resolve(), args.zig, args.artifact, args.output)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Source {manifest['source']['sha256']}; {len(manifest['source']['files'])} files")
    print(f"Native dependencies {manifest['native_dependencies']['sha256']}")
    print(args.output)


if __name__ == "__main__":
    main()
