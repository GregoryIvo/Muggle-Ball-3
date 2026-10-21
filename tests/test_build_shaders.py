#!/usr/bin/env python3
"""tools/build_shaders.py: header formatting, and a full run when the shader tools are installed."""
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import build_shaders as B  # noqa: E402


def raw_literals(text):
    return re.findall(r'R"MSL\((.*?)\)MSL"', text, re.DOTALL)


class FormatTest(unittest.TestCase):
    def test_words_are_eight_per_line(self):
        lines = B.cpp_words(list(range(10))).split("\n")
        self.assertEqual(len(lines), 2)
        self.assertEqual(lines[0], "    " + ", ".join(f"0x{i:08x}" for i in range(8)) + ",")

    def test_long_source_is_split_and_rejoins(self):
        source = "".join(f"float4 line{i} = float4(0.0);\n" for i in range(1000))
        literals = raw_literals(B.cpp_raw_string(source))
        self.assertGreater(len(literals), 1)
        self.assertTrue(all(len(lit) <= B.MSL_CHUNK_CHARS for lit in literals))
        self.assertEqual("".join(literals), source)

    def test_header_names_follow_the_file_name(self):
        text = B.header_text("mesh", "vert", "mesh.vert", [0x07230203, 1], "void f();\n", "main0")
        for expected in ("mesh_vert_spirv[]", "mesh_vert_msl[]", 'mesh_vert_msl_entry[] = "main0"',
                         "from engine/shaders/mesh.vert", "0x07230203, 0x00000001,"):
            self.assertIn(expected, text)


@unittest.skipUnless(shutil.which("glslangValidator") and shutil.which("spirv-cross"),
                     "glslangValidator and spirv-cross are not installed")
class EndToEndTest(unittest.TestCase):
    def test_every_shader_gets_a_header(self):
        with tempfile.TemporaryDirectory() as out:
            subprocess.run([sys.executable, str(ROOT / "tools" / "build_shaders.py"), "--out", out],
                           check=True, capture_output=True)
            sources = sorted(p.name for p in (ROOT / "engine" / "shaders").iterdir()
                             if p.suffix in (".vert", ".frag"))
            headers = sorted(p.name for p in pathlib.Path(out).iterdir())
            self.assertEqual(headers, [s.replace(".", "_") + ".h" for s in sources])
            for header in pathlib.Path(out).iterdir():
                text = header.read_text()
                self.assertIn("0x07230203", text)           # SPIR-V magic
                self.assertIn('_msl_entry[] = "main0"', text)
                self.assertIn("#include <metal_stdlib>", text)


if __name__ == "__main__":
    unittest.main()
