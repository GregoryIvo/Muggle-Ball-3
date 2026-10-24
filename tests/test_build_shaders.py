#!/usr/bin/env python3
"""tools/build_shaders.py: header formatting, and a full run when the shader tools are installed."""
import os
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
            # gl_shaders.h is the GL half's one header, generated from engine/shaders/gl/*, not from the
            # top-level sources this loop checks.
            headers = sorted(p.name for p in pathlib.Path(out).iterdir() if p.name != "gl_shaders.h")
            self.assertEqual(headers, [s.replace(".", "_") + ".h" for s in sources])
            for header in pathlib.Path(out).iterdir():
                if header.name == "gl_shaders.h":
                    continue
                text = header.read_text()
                self.assertIn("0x07230203", text)           # SPIR-V magic
                self.assertIn('_msl_entry[] = "main0"', text)
                self.assertIn("#include <metal_stdlib>", text)

    def test_gl_header_has_every_program_and_the_discard_free_variant(self):
        with tempfile.TemporaryDirectory() as out:
            subprocess.run([sys.executable, str(ROOT / "tools" / "build_shaders.py"), "--out", out],
                           check=True, capture_output=True)
            text = (pathlib.Path(out) / "gl_shaders.h").read_text()
            gl_dir = ROOT / "engine" / "shaders" / "gl"
            for source in gl_dir.iterdir():
                if source.suffix in (".vert", ".frag"):
                    self.assertIn(f"gl_{source.stem}_{source.suffix[1:]}[]", text)
            self.assertIn("gl_mesh_frag_nodiscard[]", text)
            self.assertNotIn("ALPHA_TEST_BEGIN", text.split("gl_mesh_frag_nodiscard[]")[1])


class GlOnlyTest(unittest.TestCase):
    def test_gl_only_needs_neither_tool_and_writes_only_gl_shaders_h(self):
        # Runs with an empty PATH, so neither glslangValidator nor spirv-cross can be found: --gl-only's
        # whole point is that this still succeeds (the docstring's "glslangValidator is optional for the GL
        # half" claim is otherwise unreachable, since main() used to require both tools before the GL half
        # ever ran, regardless of --gl-only not existing at all).
        with tempfile.TemporaryDirectory() as out:
            env = dict(os.environ, PATH="")
            result = subprocess.run(
                [sys.executable, str(ROOT / "tools" / "build_shaders.py"), "--gl-only", "--out", out],
                capture_output=True, text=True, env=env)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual([p.name for p in pathlib.Path(out).iterdir()], ["gl_shaders.h"])
            text = (pathlib.Path(out) / "gl_shaders.h").read_text()
            self.assertIn("gl_mesh_frag_nodiscard[]", text)


class GlFormatTest(unittest.TestCase):
    def test_strip_alpha_test_removes_the_marked_lines_only(self):
        body = "before\n// ALPHA_TEST_BEGIN\nif (x) discard;\n// ALPHA_TEST_END\nafter\n"
        self.assertEqual(B.strip_alpha_test(body), "before\nafter\n")

    def test_strip_alpha_test_requires_both_markers(self):
        with self.assertRaises(SystemExit):
            B.strip_alpha_test("no markers here\n")

    def test_gl_cpp_string_rejects_the_raw_string_terminator(self):
        with self.assertRaises(SystemExit):
            B.gl_cpp_string("test", 'contains )GLSL" already')


if __name__ == "__main__":
    unittest.main()
