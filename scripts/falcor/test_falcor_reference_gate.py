"""Synthetic evidence tests for the gate, not rendering-parity claims."""

import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from PIL import Image

import falcor_reference_test as gate


class FalcorGateTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.uz = self.root / "uz.png"
        self.falcor = self.root / "falcor.png"
        for path, renderer in ((self.uz, "UzEngine"), (self.falcor, "Falcor")):
            Image.new("RGB", (8, 8), (128, 128, 128)).save(path)
            metadata = {
                "schema_version": 1, "renderer": renderer, "scene_sha256": "1" * 64,
                "source_manifest_sha256": "2" * 64, "executable_sha256": "3" * 64,
                "image_sha256": gate.file_hash(path), "width": 8, "height": 8,
                "camera": {"view_matrix": [1.0] * 16, "projection_matrix": [1.0] * 16},
                "frame": 5, "simulation_time": 0, "exposure": 1,
                "tonemapping": "ACES", "color_space": "sRGB",
            }
            path.with_suffix(".json").write_text(json.dumps(metadata))

    def compare(self):
        return gate.run_comparison(self.uz, self.falcor, "synthetic")

    def test_identical_images_pass_and_leave_reference_unchanged(self):
        before = gate.file_hash(self.falcor)
        result = self.compare()
        self.assertEqual(result["status"], "PASS")
        self.assertEqual(result["psnr"], 100)
        self.assertEqual(gate.file_hash(self.falcor), before)

    def test_threshold_is_inclusive_without_rounding_into_pass(self):
        for score in (24.9, 25, 34.9, 34.9999, 35, 40):
            with self.subTest(score=score), patch.object(gate, "compute_psnr", return_value=score):
                self.assertEqual(self.compare()["status"], "PASS" if score >= 35 else "FAIL")

    def test_invalid_scores_fail(self):
        for score in (float("nan"), float("inf"), float("-inf")):
            with self.subTest(score=score), patch.object(gate, "compute_psnr", return_value=score):
                self.assertEqual(self.compare()["status"], "FAIL")

    def test_required_images_and_sidecars_cannot_skip(self):
        for a, b in ((self.root / "missing.png", self.falcor), (self.uz, self.root / "missing.png")):
            self.assertEqual(gate.run_comparison(a, b, "missing")["status"], "FAIL")
        for index in (0, 1):
            metadata = [self.uz.with_suffix(".json"), self.falcor.with_suffix(".json")]
            metadata[index] = self.root / "absent.json"
            self.assertEqual(gate.run_comparison(self.uz, self.falcor, "missing", *metadata)["status"], "FAIL")

    def test_mismatched_dimensions_are_never_resized(self):
        Image.new("RGB", (4, 4), (128, 128, 128)).save(self.falcor)
        with self.assertRaisesRegex(ValueError, "dimensions differ"):
            gate.compute_psnr(self.uz, self.falcor)
        self.assertEqual(self.compare()["status"], "FAIL")

    def test_metadata_mismatch_and_missing_fields_fail(self):
        path = self.falcor.with_suffix(".json")
        original = json.loads(path.read_text())
        for key in gate.MATCH_FIELDS:
            with self.subTest(key=key):
                metadata = copy.deepcopy(original)
                del metadata[key]
                path.write_text(json.dumps(metadata))
                self.assertEqual(self.compare()["status"], "FAIL")
        for key, value in (("scene_sha256", "a" * 64), ("exposure", 2), ("frame", 6),
                           ("simulation_time", 1), ("color_space", "linear"), ("renderer", "UzEngine"),
                           ("camera", {"view_matrix": [2] * 16, "projection_matrix": [1] * 16})):
            with self.subTest(key=key):
                metadata = copy.deepcopy(original)
                metadata[key] = value
                path.write_text(json.dumps(metadata))
                self.assertEqual(self.compare()["status"], "FAIL")

    def test_stale_sidecar_and_corrupt_image_fail(self):
        Image.new("RGB", (8, 8), (127, 127, 127)).save(self.falcor)
        self.assertIn("hash mismatch", self.compare()["reason"])
        self.falcor.write_bytes(b"not an image")
        self.assertEqual(self.compare()["status"], "FAIL")

    def test_invalid_json_and_camera_fail(self):
        path = self.falcor.with_suffix(".json")
        original = json.loads(path.read_text())
        for value in ("{broken", "[]", "null"):
            path.write_text(value)
            self.assertEqual(self.compare()["status"], "FAIL")
        original["camera"]["view_matrix"][0] = float("nan")
        path.write_text(json.dumps(original))
        self.assertEqual(self.compare()["status"], "FAIL")

    def test_capture_nonzero_exit_cannot_reuse_a_file(self):
        with patch.object(gate.subprocess, "run", return_value=subprocess.CompletedProcess([], 4, "", "crash")):
            self.assertFalse(gate.capture_uzengine(self.uz))

    def test_successful_process_without_new_capture_cannot_reuse_a_file(self):
        falcor_root = self.root / "falcor"
        mogwai = falcor_root / gate.MOGWAI_EXE_REL
        mogwai.parent.mkdir(parents=True)
        mogwai.write_bytes(b"test executable")
        golden = self.root / "pbr_showcase_frame5.png"
        golden.write_bytes(self.falcor.read_bytes())
        with patch.object(gate, "GOLDEN_DIR", self.root), patch.object(
                gate.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")):
            self.assertFalse(gate.capture_uzengine(self.uz))
            self.assertFalse(gate.capture_falcor(falcor_root))
            self.assertFalse(gate.capture_uzengine(self.root / "missing.png"))

    def test_empty_and_nonpass_reports_fail(self):
        for results in ([], [{"name": "missing", "status": "SKIP"}], [{"name": "low", "status": "WARNING"}]):
            self.assertEqual(gate.write_report(results, self.root / "report.json"), 1)

    def test_cli_exit_and_report_for_pass_and_failure(self):
        report = self.root / "reports" / "comparison.json"
        cmd = [sys.executable, str(Path(gate.__file__).resolve()), "--uzengine-image", str(self.uz),
               "--falcor-image", str(self.falcor), "--report", str(report)]
        result = subprocess.run(cmd, cwd=self.root, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        Image.new("RGB", (8, 8), (0, 0, 0)).save(self.uz)
        sidecar = self.uz.with_suffix(".json")
        metadata = json.loads(sidecar.read_text())
        metadata["image_sha256"] = gate.file_hash(self.uz)
        sidecar.write_text(json.dumps(metadata))
        result = subprocess.run(cmd, cwd=self.root, capture_output=True, text=True)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(json.loads(report.read_text())[0]["status"], "FAIL")
        self.assertLess(json.loads(report.read_text())[0]["psnr"], 35)

    def test_report_cannot_overwrite_capture_or_sidecar(self):
        for output in (self.falcor, self.falcor.with_suffix(".json")):
            before = output.read_bytes()
            cmd = [sys.executable, str(Path(gate.__file__).resolve()), "--uzengine-image", str(self.uz),
                   "--falcor-image", str(self.falcor), "--report", str(output)]
            result = subprocess.run(cmd, cwd=self.root, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(output.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
