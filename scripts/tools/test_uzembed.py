"""Compile the embedder and verify large generated sources survive C++ compilation."""

import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ShaderEmbeddingTests(unittest.TestCase):
    def test_large_shader_preserves_bytes_and_escapes_literal_delimiter(self):
        local_zig = ROOT / ".tools/zig-x86_64-windows-0.15.2/zig.exe"
        zig = os.environ.get("UZENGINE_TEST_ZIG") or shutil.which("zig")
        if not zig and local_zig.is_file():
            zig = str(local_zig)
        self.assertTrue(zig, "Zig 0.15.2 is required for the embedder compilation regression")
        with tempfile.TemporaryDirectory(prefix="uzembed test ") as directory:
            root = Path(directory)
            env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(ROOT / ".tools/zig-cache"))

            def run(*args):
                result = subprocess.run(list(map(str, args)), cwd=root, env=env,
                                        capture_output=True, timeout=180)
                self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))

            tool = root / "uzembed.exe"
            run(zig, "c++", "-std=c++23", ROOT / "runtime/tools/uzembed.cpp", "-o", tool)
            source = '// Literal delimiter in a shader comment: )hlsl" and )hlsl_"\n'
            source += "// " + "large_shader_payload_" * 4000 + "\n"
            source += "float4 main() : SV_Target { return float4(1, 0, 0, 1); }\n"
            inputs = root / "shader input"
            inputs.mkdir()
            (inputs / "large_ps.hlsl").write_text(source, encoding="utf-8", newline="\n")
            output = root / "generated"
            run(tool, "--input-dir", inputs, "--output-dir", output)
            header = (output / "large_ps.hlsl.gen.h").read_text(encoding="utf-8")
            chunks = re.findall(r'R"(hlsl_*)\((.*?)\)\1"', header, flags=re.DOTALL)
            self.assertGreater(len(chunks), 1)
            self.assertTrue(all(len(content) <= 8192 for _, content in chunks))
            consumer = root / "consumer.cpp"
            consumer.write_text(
                '#include "generated/large_ps.hlsl.gen.h"\n#include <cstdio>\n'
                'int main() { auto* f = std::fopen("roundtrip.bin", "wb"); if (!f) return 2;\n'
                'constexpr auto& s = uz::generated::kLargePS;\n'
                'const auto n = std::fwrite(s, 1, sizeof(s) - 1, f);\n'
                'return std::fclose(f) == 0 && n == sizeof(s) - 1 ? 0 : 3; }\n', encoding="utf-8")
            exe = root / "consumer.exe"
            run(zig, "c++", "-std=c++23", consumer, "-o", exe)
            run(exe)
            self.assertEqual((root / "roundtrip.bin").read_bytes(), ("\n" + source).encode())


if __name__ == "__main__":
    unittest.main()
