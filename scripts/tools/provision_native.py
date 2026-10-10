"""Provision the locked Windows build profile without modifying vendored sources."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tarfile
import uuid
import urllib.request
import zipfile


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def is_sha256(value: object) -> bool:
    return (isinstance(value, str) and len(value) == 64
            and all(c in "0123456789abcdef" for c in value))


def file_version(path: Path) -> str:
    """PE fixed file version for diagnostics only; never part of a trust decision."""
    if os.name != "nt":
        return "unknown"
    import ctypes
    from ctypes import wintypes
    api = ctypes.WinDLL("version")
    api.GetFileVersionInfoSizeW.argtypes = [wintypes.LPCWSTR, wintypes.LPDWORD]
    api.GetFileVersionInfoW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
    api.VerQueryValueW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR,
                                   ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(wintypes.UINT)]
    size = api.GetFileVersionInfoSizeW(str(path), None)
    data = ctypes.create_string_buffer(size)
    info, length = ctypes.c_void_p(), wintypes.UINT()
    if (not size or not api.GetFileVersionInfoW(str(path), 0, size, data)
            or not api.VerQueryValueW(data, "\\", ctypes.byref(info), ctypes.byref(length))
            or length.value < 16):
        return "unknown"
    # VS_FIXEDFILEINFO: dwSignature, dwStrucVersion, dwFileVersionMS, dwFileVersionLS, ...
    signature, _, high, low = ctypes.cast(info, ctypes.POINTER(ctypes.c_uint32 * 4)).contents
    if signature != 0xFEEF04BD:
        return "unknown"
    return f"{high >> 16}.{high & 0xFFFF}.{low >> 16}.{low & 0xFFFF}"


def check_fxc(fxc: Path, windows_sdk: dict) -> None:
    """Accept only SDK FXC builds whose bytes are listed in the lock."""
    allowed = windows_sdk["fxc_sha256"]
    if isinstance(allowed, str):  # Single-hash locks predate the servicing allow-list.
        allowed = [allowed]
    if not isinstance(allowed, list) or not allowed or not all(is_sha256(h) for h in allowed):
        raise ValueError("Invalid Windows SDK FXC SHA-256 allow-list in native lock")
    actual = digest(fxc)
    if actual not in allowed:
        raise ValueError(f"Windows SDK FXC hash differs from lock: {fxc} (sha256 {actual}, "
                         f"file version {file_version(fxc)}); after verifying this SDK install, "
                         "add the sha256 to windows_sdk.fxc_sha256 in scripts/native-dependencies.lock.json")


def child(root: Path, name: str) -> Path:
    parts = PurePosixPath(name)
    if (not name or "\\" in name or ":" in name or parts.is_absolute()
            or ".." in parts.parts):
        raise ValueError(f"Unsafe relative path: {name}")
    result = root.joinpath(*parts.parts).resolve()
    if not result.is_relative_to(root.resolve()) or result == root.resolve():
        raise ValueError(f"Path escapes destination: {name}")
    return result


def fetch(package: dict, cache: Path, offline: bool) -> Path:
    expected = package["sha256"]
    if not is_sha256(expected):
        raise ValueError("Invalid SHA-256 in native lock")
    target = child(cache, expected + ".archive")
    if not target.exists():
        if offline:
            raise FileNotFoundError(f"Offline cache missing {package['name']}: {target}")
        if not package["url"].startswith("https://"):
            raise ValueError("Native downloads require HTTPS")
        partial = target.with_suffix("." + uuid.uuid4().hex + ".partial")
        with urllib.request.urlopen(package["url"], timeout=120) as source, partial.open("wb") as dest:
            shutil.copyfileobj(source, dest)
        if digest(partial) != expected:
            raise ValueError(f"Download hash mismatch: {package['name']}")
        partial.replace(target)
    if digest(target) != expected:
        raise ValueError(f"Cache hash mismatch: {package['name']}")
    return target


def extract(archive: Path, package: dict, target: Path) -> None:
    """Only regular files and directories; never follow archive links."""
    prefix = package.get("prefix", "")

    def destination(name: str) -> Path | None:
        # Validate even members outside the selected subtree.
        child(target, name.rstrip("/"))
        if prefix and not name.startswith(prefix):
            return None
        relative = name[len(prefix):]
        selected = package.get("include")
        if selected and not any(relative == item or relative.startswith(item.rstrip("/") + "/")
                                for item in selected):
            return None
        return child(target, relative) if relative else None

    if package["format"] == "zip":
        with zipfile.ZipFile(archive) as source:
            for member in source.infolist():
                dest = destination(member.filename)
                if dest is None:
                    continue
                mode = (member.external_attr >> 16) & 0o170000
                if mode == 0o120000:
                    raise ValueError(f"Archive symlink: {member.filename}")
                if dest is None or member.is_dir():
                    continue
                dest.parent.mkdir(parents=True, exist_ok=True)
                with source.open(member) as stream, dest.open("wb") as output:
                    shutil.copyfileobj(stream, output)
    elif package["format"] == "tar.gz":
        with tarfile.open(archive, "r:gz") as source:
            for member in source:
                dest = destination(member.name)
                if dest is None:
                    continue
                if not member.isfile() and not member.isdir():
                    raise ValueError(f"Archive special file: {member.name}")
                if dest is None or member.isdir():
                    continue
                dest.parent.mkdir(parents=True, exist_ok=True)
                with source.extractfile(member) as stream, dest.open("wb") as output:
                    shutil.copyfileobj(stream, output)
    else:
        raise ValueError(f"Unsupported archive format: {package['format']}")
    for name in package["required"]:
        if not child(target, name).is_file():
            raise FileNotFoundError(f"{package['name']} lacks {name}")


def inventory(root: Path) -> dict[str, str]:
    return {p.relative_to(root).as_posix(): digest(p)
            for p in sorted(root.rglob("*")) if p.is_file() and p.name != "provision.json"}


def lua_sources(root: Path, names: list[str]) -> list[Path]:
    vendor = root / "runtime/thirdparty/luajit"
    if "src/msvcbuild.bat" not in names:
        raise ValueError("LuaJIT source list lacks src/msvcbuild.bat")
    paths = [child(vendor, name) for name in names]
    for path in paths:
        if not path.is_file():
            raise FileNotFoundError(f"Missing vendored LuaJIT source: {path}")
    return sorted(paths)


def source_hash(root: Path, names: list[str]) -> str:
    # lua_sources returns resolved paths; an 8.3 or junction root must be resolved to match.
    base = root.resolve()
    entries = {p.relative_to(base).as_posix(): digest(p) for p in lua_sources(root, names)}
    return hashlib.sha256(json.dumps(entries, sort_keys=True).encode()).hexdigest()


def verify(target: Path, lock_hash: str, lua_hash: str) -> dict:
    record = json.loads((target / "provision.json").read_text(encoding="utf-8"))
    if record["lock_sha256"] != lock_hash or record["luajit_source_sha256"] != lua_hash:
        raise ValueError("Native profile inputs changed; choose a new -Destination")
    if inventory(target) != record["files"]:
        raise ValueError("Native profile is incomplete or modified; choose a new -Destination")
    return record


def provision(root: Path, lock: Path, target: Path, cache: Path,
              sdk: Path, vcvars: Path, offline: bool, verify_only: bool) -> dict:
    spec = json.loads(lock.read_text(encoding="utf-8"))
    if spec["schema_version"] != 1 or spec["platform"] != "windows-x64":
        raise ValueError("Unsupported native profile")
    lock_hash, lua_hash = digest(lock), source_hash(root, spec["luajit_sources"])
    if target.exists():
        return verify(target, lock_hash, lua_hash)
    if verify_only:
        raise FileNotFoundError(f"Native profile missing: {target}")
    if os.name != "nt":
        raise RuntimeError("Provisioning requires Windows x64 and MSVC")
    fxc = sdk / "bin" / spec["windows_sdk"]["version"] / "x64/d3dcompiler_47.dll"
    check_fxc(fxc, spec["windows_sdk"])
    if not vcvars.is_file():
        raise FileNotFoundError(f"Missing MSVC environment script: {vcvars}")
    cache.mkdir(parents=True, exist_ok=True)
    target.parent.mkdir(parents=True, exist_ok=True)
    # Preserve failed staging directories for diagnosis. Never erase an existing SDK.
    stage = (target.parent / (target.name + ".staging-" + uuid.uuid4().hex)).resolve()
    if not stage.is_relative_to(target.parent.resolve()):
        raise ValueError("Staging directory escaped target parent")
    # Normal directory inheritance also permits the sandbox to inspect MSVC outputs.
    stage.mkdir()
    for package in spec["packages"]:
        print(f"Provisioning {package['name']}", flush=True)
        extract(fetch(package, cache, offline), package, child(stage, package["name"]))
    fxc_dest = stage / "fxc/bin/x64/d3dcompiler_47.dll"
    fxc_dest.parent.mkdir(parents=True)
    shutil.copyfile(fxc, fxc_dest)
    vendor = (root / "runtime/thirdparty/luajit").resolve()
    for source in lua_sources(root, spec["luajit_sources"]):
        dest = child(stage / "luajit", source.relative_to(vendor).as_posix())
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, dest)
    # Paths are passed as environment values rather than interpolated shell code.
    env = os.environ.copy()
    env["UZ_NATIVE_VCVARS"] = str(vcvars)
    env["UZ_NATIVE_LUA"] = str(stage / "luajit/src")
    # Upstream invokes its freshly built minilua/buildvm by name. Do not relax
    # NoDefaultCurrentDirectoryInExePath; explicitly expose this trusted build dir.
    installer = Path(env.get("ProgramFiles(x86)", "")) / "Microsoft Visual Studio/Installer"
    env["PATH"] = os.pathsep.join([str(stage / "luajit/src"), str(installer), env.get("PATH", "")])
    for name in ("CL", "_CL_", "LINK", "_LINK_", "INCLUDE", "LIB", "LIBPATH", "UseEnv", "ML"):
        env.pop(name, None)
    launcher = stage / "build-luajit.cmd"
    launcher.write_text('@echo off\ncall "%UZ_NATIVE_VCVARS%" x64\n'
                        'if errorlevel 1 exit /b %errorlevel%\n'
                        'cd /d "%UZ_NATIVE_LUA%"\n'
                        'if errorlevel 1 exit /b %errorlevel%\n'
                        'call "%UZ_NATIVE_LUA%\\msvcbuild.bat"\nexit /b %errorlevel%\n', encoding="ascii")
    with (stage / "luajit-build.log").open("w", encoding="utf-8") as log:
        subprocess.run(["cmd.exe", "/d", "/c", str(launcher)], env=env,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    for name in ("lua51.lib", "lua51.dll"):
        if not (stage / "luajit/src" / name).is_file():
            raise FileNotFoundError(f"LuaJIT build did not produce {name}")
    record = {"schema_version": 1, "lock_sha256": lock_hash,
              "luajit_source_sha256": lua_hash, "msvc_environment": str(vcvars),
              "windows_sdk": spec["windows_sdk"]["version"], "files": inventory(stage)}
    (stage / "provision.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    # Both absolute paths are under the same validated parent; target must be absent.
    stage.rename(target)
    return verify(target, lock_hash, lua_hash)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--destination", type=Path)
    parser.add_argument("--cache", type=Path)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--vcvars", type=Path, required=True)
    parser.add_argument("--offline", action="store_true")
    parser.add_argument("--verify-only", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()
    target = (args.destination or root / "runtime/thirdparty/.native").resolve()
    cache = (args.cache or root / ".tools/native-cache").resolve()
    record = provision(root, root / "scripts/native-dependencies.lock.json", target, cache,
                       args.sdk.resolve(), args.vcvars.resolve(), args.offline, args.verify_only)
    print(f"Verified {len(record['files'])} native files: {target}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
