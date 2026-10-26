"""Supply-chain pins in scripts/setup.ps1: every clone is commit-pinned, every download hash-pinned.

Static checks read setup.ps1 through the PowerShell parser. Behavioural checks load setup.ps1's
helper functions (never the script body, so nothing is cloned or downloaded from the network) and
drive them against local fixtures: a file:// git repository and file:// downloads.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SETUP = ROOT / "scripts" / "setup.ps1"
PWSH = shutil.which("pwsh")
# setup.bat launches Windows PowerShell 5.1, so the helpers must work there too.
WINDOWS_POWERSHELL = shutil.which("powershell") if sys.platform == "win32" else None
SHELLS = [shell for shell in (PWSH, WINDOWS_POWERSHELL) if shell]

SHA256 = r"^[0-9a-f]{64}$"
COMMIT = r"^[0-9a-f]{40}$"

# The setup.ps1 path travels in $env:SETUP_PS1 and this text runs via -File. Never hand the path to
# pwsh -Command as an extra argument: -Command joins trailing arguments into the command text, which
# would execute setup.ps1 itself.
INVENTORY = r"""
$errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($env:SETUP_PS1, [ref]$null, [ref]$errors)
$names = @("Clone-Repo", "Download-Header", "Download-Extract-Zip", "Download-File")
$calls = foreach ($cmd in $ast.FindAll({ param($n) $n -is [System.Management.Automation.Language.CommandAst] }, $true)) {
    $name = $cmd.GetCommandName()
    if ($names -notcontains $name) { continue }
    $params = @{}
    $elements = $cmd.CommandElements
    for ($i = 1; $i -lt $elements.Count; $i++) {
        $element = $elements[$i]
        if ($element -is [System.Management.Automation.Language.CommandParameterAst]) {
            $value = $element.Argument
            if (-not $value -and $i + 1 -lt $elements.Count -and
                $elements[$i + 1] -isnot [System.Management.Automation.Language.CommandParameterAst]) {
                $value = $elements[$i + 1]
                $i++
            }
            $literal = $null
            if ($value -is [System.Management.Automation.Language.StringConstantExpressionAst]) { $literal = $value.Value }
            $params[$element.ParameterName] = @{ text = "$value"; literal = $literal }
        }
    }
    @{ name = $name; line = $cmd.Extent.StartLineNumber; params = $params }
}
$strings = foreach ($s in $ast.FindAll({ param($n) $n -is [System.Management.Automation.Language.StringConstantExpressionAst] -or
                                                  $n -is [System.Management.Automation.Language.ExpandableStringExpressionAst] }, $true)) {
    if ($s.Value -match '://') { $s.Value }
}
@{ errors = @($errors | ForEach-Object { $_.Message }); calls = @($calls); strings = @($strings) } | ConvertTo-Json -Depth 6
"""

# Loads only the function definitions from setup.ps1 into the current scope.
PRELUDE = r"""
$ErrorActionPreference = "Stop"
$errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($env:SETUP_PS1, [ref]$null, [ref]$errors)
foreach ($fn in $ast.FindAll({ param($n) $n -is [System.Management.Automation.Language.FunctionDefinitionAst] }, $false)) {
    . ([scriptblock]::Create($fn.Extent.Text))
}
$RootDir = $env:FIXTURE_ROOT
$ThirdPartyDir = Join-Path $RootDir "thirdparty"
$TempDir = Join-Path $RootDir "temp"
$NativeLockPath = Join-Path $RootDir "lock.json"
New-Item -ItemType Directory -Force -Path $ThirdPartyDir, $TempDir | Out-Null
function Report($value) { Write-Output ("RESULT:" + ($value | ConvertTo-Json -Compress -Depth 4)) }
"""


def git(*args, cwd):
    result = subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)} failed: {result.stderr}")
    return result.stdout.strip()


def sha256(data):
    return hashlib.sha256(data).hexdigest()


class SetupPinInventoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not PWSH:
            raise unittest.SkipTest("pwsh is required to parse setup.ps1")
        with tempfile.TemporaryDirectory(prefix="setup pins inventory ") as temp:
            script = Path(temp) / "inventory.ps1"
            script.write_text(INVENTORY, encoding="utf-8-sig")
            result = subprocess.run([PWSH, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(script)],
                                    capture_output=True, text=True, check=True,
                                    env=dict(os.environ, SETUP_PS1=str(SETUP)))
        cls.inventory = json.loads(result.stdout)

    def calls(self, name):
        return [call for call in self.inventory["calls"] if call["name"] == name]

    def test_script_parses_without_errors(self):
        self.assertEqual(self.inventory["errors"], [])

    def test_every_clone_is_pinned_to_a_full_commit(self):
        clones = self.calls("Clone-Repo")
        self.assertGreater(len(clones), 30)
        for call in clones:
            commit = call["params"].get("Commit", {}).get("literal") or ""
            self.assertRegex(commit, COMMIT, f"Clone-Repo at line {call['line']} lacks a literal -Commit SHA")

    def test_every_header_download_has_a_literal_hash(self):
        for call in self.calls("Download-Header"):
            digest = call["params"].get("Sha256", {}).get("literal") or ""
            self.assertRegex(digest, SHA256, f"Download-Header at line {call['line']} lacks a literal -Sha256")

    def test_every_archive_and_file_download_passes_a_hash(self):
        downloads = self.calls("Download-Extract-Zip") + self.calls("Download-File")
        self.assertGreater(len(downloads), 5)
        for call in downloads:
            self.assertIn("Sha256", call["params"], f"{call['name']} at line {call['line']} has no -Sha256")
            literal = call["params"]["Sha256"]["literal"]
            if literal is not None:
                self.assertRegex(literal, SHA256)
            if call["name"] == "Download-Extract-Zip" and "VerifyFile" in call["params"]:
                self.assertIn("VerifyFileSha256", call["params"], f"line {call['line']}")

    def test_no_url_follows_a_moving_branch_or_latest_release(self):
        moving = [s for s in self.inventory["strings"]
                  if "raw.githubusercontent.com" in s and any(f"/{b}/" in s for b in ("master", "main"))
                  or "releases/download/latest" in s]
        self.assertEqual(moving, [])


class SetupPinBehaviourTests(unittest.TestCase):
    def setUp(self):
        if not SHELLS:
            self.skipTest("PowerShell is required")
        self.temp = tempfile.TemporaryDirectory(prefix="setup pins ")
        self.root = Path(self.temp.name) / "repo root"
        self.root.mkdir()
        git("init", "-q", cwd=self.root)

    def tearDown(self):
        self.temp.cleanup()

    def run_ps(self, body, shell):
        script = Path(self.temp.name) / "case.ps1"
        script.write_text(PRELUDE + body, encoding="utf-8-sig")
        env = dict(os.environ, SETUP_PS1=str(SETUP), FIXTURE_ROOT=str(self.root))
        result = subprocess.run([shell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(script)],
                                capture_output=True, text=True, env=env)
        lines = [line[len("RESULT:"):] for line in result.stdout.splitlines() if line.startswith("RESULT:")]
        return result, (json.loads(lines[-1]) if lines else None)

    def fixture_repo(self):
        upstream = Path(self.temp.name) / "upstream lib"
        upstream.mkdir()
        git("init", "-q", "-b", "master", cwd=upstream)
        git("config", "uploadpack.allowReachableSHA1InWant", "true", cwd=upstream)
        (upstream / "lib.h").write_text("version 1\n", encoding="utf-8")
        git("add", "lib.h", cwd=upstream)
        git("-c", "user.name=t", "-c", "user.email=t@t", "commit", "-q", "-m", "v1", cwd=upstream)
        first = git("rev-parse", "HEAD", cwd=upstream)
        (upstream / "lib.h").write_text("version 2\n", encoding="utf-8")
        git("-c", "user.name=t", "-c", "user.email=t@t", "commit", "-q", "-am", "v2", cwd=upstream)
        return upstream.as_uri(), first, git("rev-parse", "HEAD", cwd=upstream)

    def test_download_moves_file_only_when_hash_matches(self):
        payload = b"pinned payload\n"
        source = Path(self.temp.name) / "payload.bin"
        source.write_bytes(payload)
        for shell in SHELLS:
            with self.subTest(shell=shell):
                good = self.root / "good.bin"
                bad = self.root / "bad.bin"
                result, value = self.run_ps(f"""
Download-File -Url '{source.as_uri()}' -OutFile '{good}' -Sha256 '{sha256(payload)}'
$caught = $null
try {{ Download-File -Url '{source.as_uri()}' -OutFile '{bad}' -Sha256 '{"0" * 64}' }} catch {{ $caught = "$_" }}
Report @{{ caught = $caught; bad = (Test-Path '{bad}'); partial = (Test-Path '{bad}.download') }}
""", shell)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(good.read_bytes(), payload)
                self.assertFalse((self.root / "good.bin.download").exists())
                self.assertIn("SHA-256 mismatch", value["caught"])
                self.assertFalse(value["bad"])
                self.assertFalse(value["partial"])
                good.unlink()

    def test_clone_checks_out_and_verifies_the_pinned_commit_not_the_branch_tip(self):
        url, first, tip = self.fixture_repo()
        self.assertNotEqual(first, tip)
        for shell in SHELLS:
            with self.subTest(shell=shell):
                result, _ = self.run_ps(f"""
Clone-Repo -Name 'lib' -Url '{url}' -Branch 'master' -Commit '{first}'
Report @{{ ok = $true }}
""", shell)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                lib = self.root / "thirdparty" / "lib"
                self.assertEqual((lib / "lib.h").read_text(encoding="utf-8").strip(), "version 1")
                self.assertFalse((lib / ".git").exists())
                shutil.rmtree(lib)

    def test_unknown_commit_fails_and_leaves_no_tree(self):
        url, _, _ = self.fixture_repo()
        missing = "1" * 40
        for shell in SHELLS:
            with self.subTest(shell=shell):
                result, value = self.run_ps(f"""
$caught = $null
try {{ Clone-Repo -Name 'lib' -Url '{url}' -Branch 'master' -Commit '{missing}' }} catch {{ $caught = "$_" }}
Report @{{ caught = $caught; exists = (Test-Path (Join-Path $ThirdPartyDir 'lib')) }}
""", shell)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn(missing, value["caught"])
                self.assertFalse(value["exists"])

    def test_existing_checkout_at_another_commit_is_rejected(self):
        url, first, tip = self.fixture_repo()
        lib = self.root / "thirdparty" / "lib"
        lib.parent.mkdir(parents=True)
        git("clone", "-q", url, str(lib), cwd=self.root)
        for shell in SHELLS:
            with self.subTest(shell=shell):
                result, value = self.run_ps(f"""
$caught = $null
try {{ Clone-Repo -Name 'lib' -Url '{url}' -Branch 'master' -Commit '{first}' }} catch {{ $caught = "$_" }}
Clone-Repo -Name 'lib' -Url '{url}' -Branch 'master' -Commit '{tip}'
Report @{{ caught = $caught }}
""", shell)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("expected pinned commit " + first, value["caught"])
                self.assertIn("already exists at pinned commit", result.stdout)

    def test_existing_files_are_verified_not_trusted(self):
        content = b"upstream bytes\n"
        good = sha256(content)
        tracked = self.root / "tracked.h"
        tracked.write_bytes(b"locally patched\n")
        git("add", "tracked.h", cwd=self.root)
        git("-c", "user.name=t", "-c", "user.email=t@t", "commit", "-q", "-m", "vendor", cwd=self.root)
        (self.root / "untracked.h").write_bytes(b"tampered\n")
        (self.root / "verified.h").write_bytes(content)
        for shell in SHELLS:
            with self.subTest(shell=shell):
                result, value = self.run_ps(f"""
$state = @{{
    verified = (Test-PinnedFile -Label t -Path (Join-Path $RootDir 'verified.h') -Sha256 '{good}')
    missing = (Test-PinnedFile -Label t -Path (Join-Path $RootDir 'absent.h') -Sha256 '{good}')
    untracked = (Test-PinnedFile -Label t -Path (Join-Path $RootDir 'untracked.h') -Sha256 '{good}')
    tracked = (Test-PinnedFile -Label t -Path (Join-Path $RootDir 'tracked.h') -Sha256 '{good}')
}}
Set-Content -LiteralPath (Join-Path $RootDir 'tracked.h') -Value 'uncommitted edit'
try {{ Test-PinnedFile -Label t -Path (Join-Path $RootDir 'tracked.h') -Sha256 '{good}' | Out-Null }} catch {{ $state.modified = "$_" }}
Report $state
""", shell)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertTrue(value["verified"])
                self.assertFalse(value["missing"])
                self.assertFalse(value["untracked"])
                self.assertTrue(value["tracked"])
                self.assertIn("uncommitted changes", value["modified"])
                git("checkout", "-q", "--", "tracked.h", cwd=self.root)

    def test_locked_package_comes_from_the_native_lock(self):
        lock = {"packages": [{"name": "dxc", "url": "https://example.invalid/dxc.zip", "sha256": "a" * 64},
                             {"name": "broken", "url": "https://example.invalid/b.zip", "sha256": "nothex"}]}
        (self.root / "lock.json").write_text(json.dumps(lock), encoding="utf-8")
        for shell in SHELLS:
            with self.subTest(shell=shell):
                result, value = self.run_ps("""
$pkg = Get-LockedPackage -Name 'dxc'
$errors = @{}
foreach ($name in @('broken', 'absent')) { try { Get-LockedPackage -Name $name | Out-Null } catch { $errors[$name] = "$_" } }
Report @{ url = $pkg.url; sha = $pkg.sha256; broken = $errors.broken; absent = $errors.absent }
""", shell)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(value["url"], "https://example.invalid/dxc.zip")
                self.assertEqual(value["sha"], "a" * 64)
                self.assertIn("valid sha256", value["broken"])
                self.assertIn("found 0", value["absent"])

    def test_real_lock_pins_the_packages_setup_reads(self):
        lock = json.loads((ROOT / "scripts" / "native-dependencies.lock.json").read_text(encoding="utf-8"))
        packages = {package["name"]: package for package in lock["packages"]}
        for name in ("dxc", "agility", "ffmpeg"):
            self.assertRegex(packages[name]["sha256"], SHA256)
            self.assertNotIn("latest", packages[name]["url"])


if __name__ == "__main__":
    unittest.main()
