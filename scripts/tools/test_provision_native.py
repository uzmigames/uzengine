"""Negative controls for locked native provisioning; no network or SDK required."""
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile

import provision_native as native


class NativeProvisionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="native profile tests ")
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def archive(self, files):
        path = self.root / "input.zip"
        with zipfile.ZipFile(path, "w") as output:
            for name, content in files.items():
                output.writestr(name, content)
        return path

    def package(self, archive, **values):
        return dict(name="fixture", format="zip", prefix="sdk/", required=["include/api.h", "bin/sdk.dll"],
                    url="https://example.invalid/sdk.zip", sha256=native.digest(archive), **values)

    def test_offline_cache_is_checked_and_tampering_fails(self):
        archive = self.archive({"sdk/include/api.h": "header", "sdk/bin/sdk.dll": "binary"})
        package = self.package(archive)
        cache = self.root / "cache"
        cache.mkdir()
        with self.assertRaisesRegex(FileNotFoundError, "Offline cache missing"):
            native.fetch(package, cache, True)
        cached = cache / (package["sha256"] + ".archive")
        cached.write_bytes(archive.read_bytes())
        # fetch returns the canonical path; CI temp roots are 8.3 aliases (RUNNER~1).
        self.assertEqual(native.fetch(package, cache, True), cached.resolve())
        cached.write_bytes(b"corrupted archive")
        with self.assertRaisesRegex(ValueError, "Cache hash mismatch"):
            native.fetch(package, cache, True)

    def test_headers_and_binaries_come_from_same_archive(self):
        archive = self.archive({"sdk/include/api.h": "header-v2", "sdk/bin/sdk.dll": "binary-v2"})
        dest = self.root / "output"
        native.extract(archive, self.package(archive), dest)
        self.assertEqual((dest / "include/api.h").read_text(), "header-v2")
        self.assertEqual((dest / "bin/sdk.dll").read_text(), "binary-v2")

    def test_missing_required_dll_fails(self):
        archive = self.archive({"sdk/include/api.h": "header"})
        with self.assertRaisesRegex(FileNotFoundError, "lacks bin/sdk.dll"):
            native.extract(archive, self.package(archive), self.root / "output")

    def test_zip_traversal_and_windows_paths_rejected(self):
        for name in ("sdk/../../outside.txt", "C:/outside.txt", "sdk\\..\\outside.txt", "sdk/file:stream"):
            with self.subTest(name=name):
                archive = self.archive({name: "bad"})
                with self.assertRaises(ValueError):
                    native.extract(archive, self.package(archive), self.root / "output")
        self.assertFalse((self.root / "outside.txt").exists())

    def test_tar_selected_library_ignores_upstream_test_links(self):
        path = self.root / "input.tar.gz"
        with tarfile.open(path, "w:gz") as output:
            member = tarfile.TarInfo("zstd/lib/zstd.h")
            member.size = 6
            output.addfile(member, io.BytesIO(b"header"))
            link = tarfile.TarInfo("zstd/tests/unzstd")
            link.type = tarfile.SYMTYPE
            link.linkname = "zstd"
            output.addfile(link)
        package = dict(name="zstd", format="tar.gz", prefix="zstd/", include=["lib"], required=["lib/zstd.h"])
        native.extract(path, package, self.root / "selected")
        self.assertTrue((self.root / "selected/lib/zstd.h").is_file())
        del package["include"]
        with self.assertRaisesRegex(ValueError, "special file"):
            native.extract(path, package, self.root / "all")

    def test_existing_profile_rejects_changed_missing_and_extra_files(self):
        dest = self.root / "profile"
        dest.mkdir()
        sdk = dest / "sdk.dll"
        sdk.write_bytes(b"sdk")
        record = dict(lock_sha256="lock", luajit_source_sha256="sources", files=native.inventory(dest))
        (dest / "provision.json").write_text(json.dumps(record))
        native.verify(dest, "lock", "sources")
        with self.assertRaisesRegex(ValueError, "inputs changed"):
            native.verify(dest, "new-lock", "sources")
        sdk.write_bytes(b"different")
        with self.assertRaisesRegex(ValueError, "modified"):
            native.verify(dest, "lock", "sources")
        sdk.unlink()
        with self.assertRaisesRegex(ValueError, "incomplete"):
            native.verify(dest, "lock", "sources")
        sdk.write_bytes(b"sdk")
        (dest / "unexpected.dll").write_bytes(b"another SDK")
        with self.assertRaisesRegex(ValueError, "modified"):
            native.verify(dest, "lock", "sources")

    def profile_inputs(self, base):
        """Controlled compiler boundary: tests orchestration, not a LuaJIT build."""
        vendor = base / "runtime/thirdparty/luajit/src"
        vendor.mkdir(parents=True)
        (vendor / "msvcbuild.bat").write_text('@echo off\necho import>lua51.lib\necho dll>lua51.dll\nexit /b 0\n')
        vcvars = base / "fake vcvars.bat"
        vcvars.write_text('@echo off\nexit /b 0\n')
        sdk = base / "SDK"
        fxc = sdk / "bin/1/x64/d3dcompiler_47.dll"
        fxc.parent.mkdir(parents=True)
        fxc.write_bytes(b"sdk fixture")
        lock = base / "lock.json"
        # Single-string fxc_sha256 is the pre-allow-list lock schema; it must still be honoured.
        lock.write_text(json.dumps(dict(schema_version=1, platform="windows-x64", packages=[],
                                      luajit_sources=["src/msvcbuild.bat"],
                                      windows_sdk=dict(version="1", fxc_sha256=native.digest(fxc)))))
        return vendor, lock, sdk, vcvars

    def directory_alias(self, real):
        """A junction (Windows) or symlink whose resolved form differs, like an 8.3 path."""
        alias = self.root / "alias"
        if os.name == "nt":
            subprocess.run(["cmd.exe", "/d", "/c", "mklink", "/J", str(alias), str(real)],
                           check=True, capture_output=True)
        else:
            alias.symlink_to(real, target_is_directory=True)
        self.assertNotEqual(alias.resolve(), alias)
        return alias

    @unittest.skipUnless(os.name == "nt", "Windows command and PATH semantics")
    def test_provision_from_path_with_spaces_and_propagate_compiler_failure(self):
        vendor, lock, sdk, vcvars = self.profile_inputs(self.root)
        target = self.root / "result with spaces"
        native.provision(self.root, lock, target, self.root / "cache", sdk, vcvars, True, False)
        self.assertEqual((target / "luajit/src/lua51.dll").read_text().strip(), "dll")
        native.provision(self.root, lock, target, self.root / "cache", sdk, vcvars, True, True)
        (vendor / "msvcbuild.bat").write_text('@echo off\nexit /b 23\n')
        failed = self.root / "failed build"
        with self.assertRaises(subprocess.CalledProcessError) as result:
            native.provision(self.root, lock, failed, self.root / "cache", sdk, vcvars, True, False)
        self.assertEqual(result.exception.returncode, 23)
        self.assertFalse(failed.exists())

    def test_generated_luajit_headers_are_not_source_inputs(self):
        vendor = self.root / "runtime/thirdparty/luajit/src"
        vendor.mkdir(parents=True)
        (vendor / "msvcbuild.bat").write_text("build recipe")
        names = ["src/msvcbuild.bat"]
        before = native.source_hash(self.root, names)
        (vendor / "lj_bcdef.h").write_text("stale generated output")
        self.assertEqual(before, native.source_hash(self.root, names))
        with self.assertRaisesRegex(FileNotFoundError, "Missing vendored"):
            native.lua_sources(self.root, names + ["src/missing.h"])

    def test_source_hash_from_unresolved_root_alias(self):
        real = self.root / "real checkout"
        vendor = real / "runtime/thirdparty/luajit/src"
        vendor.mkdir(parents=True)
        (vendor / "msvcbuild.bat").write_text("build recipe")
        names = ["src/msvcbuild.bat"]
        alias = self.directory_alias(real)
        self.assertEqual(native.source_hash(alias, names), native.source_hash(real, names))

    @unittest.skipUnless(os.name == "nt", "Windows command and PATH semantics")
    def test_provision_from_unresolved_root_alias(self):
        real = self.root / "real checkout"
        real.mkdir()
        _, lock, sdk, vcvars = self.profile_inputs(real)
        alias = self.directory_alias(real)
        target = self.root / "aliased result"
        record = native.provision(alias, lock, target, self.root / "cache", sdk, vcvars, True, False)
        self.assertIn("luajit/src/msvcbuild.bat", record["files"])
        self.assertEqual((target / "luajit/src/lua51.dll").read_text().strip(), "dll")

    def fxc_fixture(self, content=b"servicing revision"):
        fxc = self.root / "d3dcompiler_47.dll"
        fxc.write_bytes(content)
        return fxc, native.digest(fxc)

    def test_fxc_accepts_any_locked_servicing_hash(self):
        fxc, actual = self.fxc_fixture()
        native.check_fxc(fxc, dict(fxc_sha256=["0" * 64, actual]))
        native.check_fxc(fxc, dict(fxc_sha256=actual))

    def test_fxc_unknown_hash_reports_actual_hash_and_version(self):
        fxc, actual = self.fxc_fixture()
        with self.assertRaisesRegex(ValueError, f"differs from lock.*sha256 {actual}.*file version unknown"):
            native.check_fxc(fxc, dict(fxc_sha256=["0" * 64, "f" * 64]))

    def test_fxc_allow_list_cannot_accept_everything(self):
        fxc, actual = self.fxc_fixture()
        for allowed in ([], "", [actual.upper()], [actual[:-1]], [None], {actual: True}):
            with self.subTest(allowed=allowed):
                with self.assertRaisesRegex(ValueError, "Invalid Windows SDK FXC"):
                    native.check_fxc(fxc, dict(fxc_sha256=allowed))

    @unittest.skipUnless(os.name == "nt", "PE version resources are Windows-only")
    def test_file_version_reads_pe_fixed_version(self):
        self.assertRegex(native.file_version(Path(sys.executable)), r"^\d+\.\d+\.\d+\.\d+$")

    def test_lock_fxc_allow_list_is_well_formed(self):
        lock = Path(native.__file__).resolve().parents[1] / "native-dependencies.lock.json"
        allowed = json.loads(lock.read_text(encoding="utf-8"))["windows_sdk"]["fxc_sha256"]
        self.assertIsInstance(allowed, list)
        self.assertTrue(allowed and all(native.is_sha256(value) for value in allowed))


if __name__ == "__main__":
    unittest.main()
