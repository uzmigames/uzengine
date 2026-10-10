import tempfile
import unittest
from pathlib import Path

from build_manifest import dependency_inputs, fingerprint, tool_version


class BuildManifestTests(unittest.TestCase):
    def test_dirty_contents_and_deleted_input_change_fingerprint(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "source.cpp"
            path.write_text("int answer = 1;")
            first = fingerprint(root, [path])
            path.write_text("int answer = 2;")
            self.assertNotEqual(first["sha256"], fingerprint(root, [path])["sha256"])
            missing = fingerprint(root, [root / "missing.cpp"])
            self.assertTrue(missing["files"][0]["missing"])

    def test_order_and_absolute_root_do_not_change_content_identity(self):
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            for root in map(Path, (first, second)):
                (root / "a.cpp").write_text("a")
                (root / "b.lib").write_bytes(b"dependency")
            a = fingerprint(Path(first), [Path(first) / "a.cpp", Path(first) / "b.lib"])
            b = fingerprint(Path(second), [Path(second) / "b.lib", Path(second) / "a.cpp"])
            self.assertEqual(a, b)

    def test_missing_tool_is_explicit(self):
        self.assertEqual(tool_version("uzengine_nonexistent_tool_123")["status"], "unavailable")

    def test_sdk_build_libraries_and_inline_headers_are_inventory_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            names = ["sdk/build/Release/ffx.lib", "sdk/build/compiler_probe.cpp",
                     "math/Matrix.inl", "sdk/.zig-cache/old.lib", ".native.staging-failed/old.dll"]
            for name in names:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"input")
            self.assertEqual({p.relative_to(root).as_posix() for p in dependency_inputs(root)},
                             {"sdk/build/Release/ffx.lib", "math/Matrix.inl"})


if __name__ == "__main__":
    unittest.main()
