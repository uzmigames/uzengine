"""Exercise script discovery/exit semantics with controlled external-tool doubles."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

SCRIPTS = Path(__file__).resolve().parent
PWSH = shutil.which("pwsh")


@unittest.skipUnless(os.name == "nt" and PWSH, "Windows PowerShell gate integration")
class QualityScriptTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="uz quality ")
        self.addCleanup(self.temp.cleanup)
        # pwsh canonicalizes $PSScriptRoot (8.3 RUNNER~1 -> runneradmin); expect canonical paths.
        self.base = Path(self.temp.name).resolve()
        self.root = self.base / "checkout with spaces"
        self.other = self.base / "unrelated cwd"
        self.other.mkdir()
        for script in ("quality_common.ps1", "test.ps1", "run_tests.ps1", "lint.ps1", "format.ps1"):
            target = self.root / "scripts/tools" / script
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(SCRIPTS / script, target)
        (self.root / "runtime").mkdir()
        (self.root / "runtime/build.zig").write_text("// fixture")
        for module, directories in (("runtime", ("engine", "editor", "platform", "tools", "tests")), ("napi", ("src",))):
            entries = []
            for directory in directories:
                source = self.root / module / directory / "fixture.cpp"
                source.parent.mkdir(parents=True, exist_ok=True)
                source.write_text("int fixture;")
                entries.append({"directory": str(self.root), "file": str(source), "command": "clang++ -std=c++23 -c fixture.cpp"})
            (self.root / module / "compile_commands.json").write_text(json.dumps(entries))
        for module in ("extension", "mcp"):
            for tool in ("vitest/vitest.mjs", "eslint/bin/eslint.js", "prettier/bin/prettier.cjs"):
                entry = self.root / module / "node_modules" / tool
                entry.parent.mkdir(parents=True, exist_ok=True)
                entry.write_text("// tool entry fixture")
        bindir = self.base / "tool doubles"
        bindir.mkdir()
        self.log = self.base / "invocations.jsonl"
        wrapper = bindir / "tool.py"
        wrapper.write_text(
            "import json,os,pathlib,sys\n"
            "tool=sys.argv[1]; args=sys.argv[2:]\n"
            "with open(os.environ['UZ_QUALITY_LOG'],'a') as f: f.write(json.dumps({'tool':tool,'args':args,'cwd':os.getcwd()})+'\\n')\n"
            "if tool=='zig' and args==['version']: print('0.15.2'); sys.exit(0)\n"
            "if os.environ.get('UZ_FAIL_TOOL')==tool: sys.exit(7)\n"
            "for arg in args:\n"
            " p=pathlib.Path(arg)\n"
            " if p.suffix=='.cpp' and p.is_file() and 'CONTROLLED_FAILURE' in p.read_text(): sys.exit(8)\n"
        )
        for tool in ("zig", "node", "clang-tidy", "clang-format", "cmake"):
            (bindir / (tool + ".cmd")).write_text(f'@echo off\n"{sys.executable}" "{wrapper}" {tool} %*\nexit /b %errorlevel%\n')
        self.env = {**os.environ, "PATH": str(bindir) + os.pathsep + os.environ["PATH"], "UZ_QUALITY_LOG": str(self.log)}

    def run_gate(self, name, cwd=None, *args):
        return subprocess.run([PWSH, "-NoProfile", "-File", str(self.root / "scripts/tools" / name), *args],
                              cwd=cwd or self.other, env=self.env, capture_output=True, text=True)

    def test_every_gate_runs_from_checkout_and_unrelated_directory(self):
        for cwd in (self.root, self.other):
            for script in ("test.ps1", "run_tests.ps1", "lint.ps1", "format.ps1"):
                with self.subTest(cwd=cwd, script=script):
                    args = ("--check",) if script == "format.ps1" else ()
                    result = self.run_gate(script, cwd, *args)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        calls = [json.loads(line) for line in self.log.read_text().splitlines()]
        lint_args = [a for c in calls if c["tool"] == "clang-tidy" for a in c["args"]]
        self.assertIn(str(self.root / "runtime/engine/fixture.cpp"), lint_args)
        self.assertIn(str(self.root / "runtime/editor/fixture.cpp"), lint_args)
        self.assertIn(str(self.root / "runtime/tests/fixture.cpp"), lint_args)
        self.assertTrue(all(c["cwd"] == str(self.root / "runtime") for c in calls if c["tool"] == "zig"))

    def test_failing_engine_source_propagates(self):
        (self.root / "runtime/engine/fixture.cpp").write_text("CONTROLLED_FAILURE")
        for script in ("lint.ps1", "format.ps1"):
            result = self.run_gate(script, None, *(["--check"] if script == "format.ps1" else []))
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("exit code 8", result.stderr)

    def test_failed_native_or_typescript_command_fails_gate(self):
        for tool, script in (("zig", "run_tests.ps1"), ("zig", "test.ps1"), ("node", "test.ps1"), ("cmake", "test.ps1")):
            with self.subTest(tool=tool, script=script):
                self.env["UZ_FAIL_TOOL"] = tool
                args = ("--compiler=msvc",) if tool == "cmake" else ()
                result = self.run_gate(script, None, *args)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("exit code 7", result.stderr)

    def test_missing_database_and_unknown_compiler_fail(self):
        result = self.run_gate("lint.ps1", None, "-RuntimeCompileDatabase", "missing.json")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("compilation database missing", result.stderr)
        self.assertNotEqual(self.run_gate("test.ps1", None, "--compiler=unknown").returncode, 0)

    def test_success_restores_working_directory_and_preserves_path(self):
        script = str(self.root / "scripts/tools/run_tests.ps1").replace("'", "''")
        command = f"$before = (Get-Location).Path; $oldPath = $env:PATH; & '{script}'; if ((Get-Location).Path -ne $before -or $env:PATH -ne $oldPath) {{ exit 9 }}"
        result = subprocess.run([PWSH, "-NoProfile", "-Command", command], cwd=self.other, env=self.env, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
