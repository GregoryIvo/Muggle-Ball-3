#!/usr/bin/env python3
"""Compile the shaders in engine/shaders to the generated headers the engine builds from.

Two independent halves:

- SDL GPU (engine/shaders/*.vert, *.frag, GLSL 450): for every shader this runs
  glslangValidator (Vulkan semantics) to get SPIR-V, then spirv-cross to get MSL, and
  writes engine/shaders/generated/<name>_<stage>.h holding both, so the C++ build needs
  no shader tools.
- GL (engine/shaders/gl/*.vert, *.frag, GLSL ES 1.00): for every shader this embeds the
  body as-is (no preamble: the GL back end's Platform::preamble() prepends the right one
  at runtime) as a string constant `gl_<name>_<stage>` in engine/shaders/generated/gl_shaders.h,
  plus `gl_mesh_frag_nodiscard` (mesh.frag with the lines between the ALPHA_TEST markers
  removed: the discard-free variant). If glslangValidator is on PATH, each body is also
  validated under both the ES preamble ("#version 100" + precision for a fragment shader)
  and the desktop preamble ("#version 120" plus empty lowp/mediump/highp defines); a
  validation error fails the tool. Without glslangValidator the bodies are embedded anyway
  (the C++ build must never need this tool; only regenerating the header does).

Re-run it (and commit the headers) after editing a shader:

    python3 tools/build_shaders.py

Needs glslangValidator and spirv-cross on PATH for the SDL GPU half (brew install glslang
spirv-cross); glslangValidator is optional for the GL half.

The SDL GPU shaders follow its binding rules, which the back end relies on:
  SPIR-V   vertex: uniform buffers in set 1; fragment: sampled textures in set 2,
           uniform buffers in set 3 (binding = slot within that set)
  MSL      [[buffer(n)]] / [[texture(n)]] / [[sampler(n)]] with n = the SPIR-V
           binding, which spirv-cross --msl-decoration-binding produces
"""

import argparse
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import tempfile

STAGES = ("vert", "frag")

# The preambles engine/src/render/gl/GlPlatform.h documents for Platform::preamble(); validation runs the
# GL shader bodies (which carry no preamble of their own) under both so a change that only one accepts is caught.
GL_ES_PREAMBLE = {"vert": "#version 100\n", "frag": "#version 100\nprecision mediump float;\n"}
GL_DESKTOP_PREAMBLE = "#version 120\n#define lowp\n#define mediump\n#define highp\n"

# tools/build_shaders.py makes the discard-free mesh.frag variant by deleting the lines between these markers.
ALPHA_TEST_BEGIN = "// ALPHA_TEST_BEGIN"
ALPHA_TEST_END = "// ALPHA_TEST_END"

# SDL3's Metal back end runs on macOS 10.14 and later; MSL 2.1 is that release's
# version and the shaders need nothing newer, so it works on every supported macOS.
MSL_VERSION = "20100"

# MSVC rejects string literals over about 16 KB, so long MSL is split into
# adjacent raw literals, which the compiler joins again.
MSL_CHUNK_CHARS = 8000

SPIRV_MAGIC = 0x07230203


def find_tool(name):
    path = shutil.which(name)
    if not path:
        sys.exit(f"{name} not found on PATH (brew install glslang spirv-cross)")
    return path


def run(cmd):
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit(f"{' '.join(cmd)}\n{result.stdout}{result.stderr}")


def compile_spirv(glslang, source, out):
    # -V: Vulkan semantics, SPIR-V 1.0 (what SDL3's Vulkan back end needs).
    run([glslang, "-V", "--target-env", "vulkan1.0", str(source), "-o", str(out)])
    data = out.read_bytes()
    if len(data) % 4 or struct.unpack("<I", data[:4])[0] != SPIRV_MAGIC:
        sys.exit(f"{source}: glslangValidator did not write little-endian SPIR-V")
    return struct.unpack(f"<{len(data) // 4}I", data)


def spirv_to_msl(spirv_cross, spv, out):
    run([spirv_cross, "--msl", "--msl-version", MSL_VERSION, "--msl-decoration-binding",
         str(spv), "--output", str(out)])
    text = out.read_text().replace("\r\n", "\n")
    # spirv-cross renames `main` to `main0` because `main` is reserved in MSL.
    match = re.search(r"^(?:vertex|fragment)\s+\S+\s+(\w+)\(", text, re.MULTILINE)
    if not match:
        sys.exit(f"{spv}: no entry point found in the generated MSL")
    return text, match.group(1)


def cpp_words(words):
    lines = []
    for i in range(0, len(words), 8):
        lines.append("    " + ", ".join(f"0x{w:08x}" for w in words[i:i + 8]) + ",")
    return "\n".join(lines)


def cpp_raw_string(text):
    """Split at line ends into raw string literals of at most MSL_CHUNK_CHARS each."""
    chunks, current = [], ""
    for line in text.splitlines(keepends=True):
        if current and len(current) + len(line) > MSL_CHUNK_CHARS:
            chunks.append(current)
            current = ""
        current += line
    if current:
        chunks.append(current)
    if any(")MSL\"" in c for c in chunks):
        sys.exit("generated MSL contains the raw string terminator")
    return "\n".join(f'    R"MSL({c})MSL"' for c in chunks)


def strip_alpha_test(body):
    """mesh.frag with the lines between the ALPHA_TEST markers (inclusive) removed: the discard-free variant
    the back end picks when a texture has no alpha below 255 and the material's alpha is 1."""
    out = []
    skipping = False
    found_begin = found_end = False
    for line in body.splitlines(keepends=True):
        if ALPHA_TEST_BEGIN in line:
            skipping, found_begin = True, True
            continue
        if ALPHA_TEST_END in line:
            skipping, found_end = False, True
            continue
        if not skipping:
            out.append(line)
    if not (found_begin and found_end):
        sys.exit(f"mesh.frag: no {ALPHA_TEST_BEGIN} / {ALPHA_TEST_END} markers found")
    return "".join(out)


def validate_gl(glslang, name, stage, body):
    """glslangValidator under both targets' preambles; a no-op if glslangValidator is not on PATH (the GL
    bodies still get embedded: the C++ build must never need this tool)."""
    if not glslang:
        return
    for label, preamble in (("ES", GL_ES_PREAMBLE[stage]), ("desktop", GL_DESKTOP_PREAMBLE)):
        with tempfile.TemporaryDirectory() as tmp:
            source = pathlib.Path(tmp) / f"check.{stage}"
            source.write_text(preamble + body)
            result = subprocess.run([glslang, "-S", stage, str(source)], capture_output=True, text=True)
            if result.returncode != 0:
                sys.exit(f"{name}.{stage} ({label} preamble) failed to validate:\n{result.stdout}{result.stderr}")


def gl_cpp_string(name, text):
    if ")GLSL\"" in text:
        sys.exit(f"{name}: generated GLSL contains the raw string terminator")
    return f'R"GLSL(\n{text})GLSL"'


def gl_header_text(entries):
    body = "\n".join(f"inline constexpr char {ident}[] =\n    {gl_cpp_string(ident, text)};\n"
                     for ident, text in entries)
    return f"""// Generated by tools/build_shaders.py from engine/shaders/gl/*.vert and *.frag. Do not edit.
#pragma once

namespace mb::shaders {{

{body}
}}  // namespace mb::shaders
"""


def process_gl(shaders_dir, glslang):
    """Every engine/shaders/gl/<name>.<stage>, embedded verbatim (no preamble: the back end's
    Platform::preamble() supplies it at runtime), plus the discard-free mesh fragment variant."""
    gl_dir = shaders_dir / "gl"
    sources = sorted(p for stage in STAGES for p in gl_dir.glob(f"*.{stage}"))
    if not sources:
        sys.exit(f"no shaders in {gl_dir}")
    entries = []
    mesh_frag = None
    for source in sources:
        name, stage = source.stem, source.suffix[1:]
        text = source.read_text()
        validate_gl(glslang, name, stage, text)
        entries.append((f"gl_{name}_{stage}", text))
        if name == "mesh" and stage == "frag":
            mesh_frag = text
    if mesh_frag is None:
        sys.exit(f"{gl_dir}/mesh.frag not found (the discard-free variant needs it)")
    nodiscard = strip_alpha_test(mesh_frag)
    validate_gl(glslang, "mesh", "frag", nodiscard)
    entries.append(("gl_mesh_frag_nodiscard", nodiscard))
    return entries


def header_text(name, stage, source_name, words, msl, entry):
    ident = f"{name}_{stage}"
    return f"""// Generated by tools/build_shaders.py from engine/shaders/{source_name}. Do not edit.
#pragma once

#include <cstdint>

namespace mb::shaders {{

inline constexpr std::uint32_t {ident}_spirv[] = {{
{cpp_words(words)}
}};

inline constexpr char {ident}_msl_entry[] = "{entry}";

inline constexpr char {ident}_msl[] =
{cpp_raw_string(msl)};

}}  // namespace mb::shaders
"""


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--shaders", type=pathlib.Path, default=root / "engine" / "shaders")
    parser.add_argument("--out", type=pathlib.Path, default=root / "engine" / "shaders" / "generated")
    args = parser.parse_args()

    glslang = find_tool("glslangValidator")
    spirv_cross = find_tool("spirv-cross")

    sources = sorted(p for stage in STAGES for p in args.shaders.glob(f"*.{stage}"))
    if not sources:
        sys.exit(f"no shaders in {args.shaders}")
    args.out.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        for source in sources:
            name, stage = source.stem, source.suffix[1:]
            words = compile_spirv(glslang, source, tmp / f"{source.name}.spv")
            msl, entry = spirv_to_msl(spirv_cross, tmp / f"{source.name}.spv", tmp / f"{source.name}.metal")
            target = args.out / f"{name}_{stage}.h"
            target.write_bytes(header_text(name, stage, source.name, words, msl, entry).encode())
            print(f"{source.name}: {len(words) * 4} bytes SPIR-V, {len(msl)} chars MSL -> {target}")

    # The GL half: independent of the SDL GPU one above, and of glslangValidator being on PATH (shutil.which,
    # not find_tool, which would exit if it is missing).
    gl_entries = process_gl(args.shaders, shutil.which("glslangValidator"))
    gl_target = args.out / "gl_shaders.h"
    gl_target.write_bytes(gl_header_text(gl_entries).encode())
    print(f"GL: {len(gl_entries)} shader(s) -> {gl_target}")


if __name__ == "__main__":
    main()
