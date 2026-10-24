#!/usr/bin/env python3
"""tools/vita_pack.py on a tiny synthetic asset tree (no game data): the runtime file-selection rule,
mirroring, the manifest, the PNG/JPEG header parsers, power-of-two math, and the resample fallbacks."""
import json
import pathlib
import struct
import sys
import tempfile
import unittest
import zlib
from unittest import mock

REPO = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
import textures as T  # noqa: E402  (its PNG encoder, for real decodable fixtures)
import vita_pack as V  # noqa: E402

try:
    import PIL  # noqa: F401
    HAVE_PILLOW = True
except ImportError:
    HAVE_PILLOW = False


# --- Fixture builders -------------------------------------------------------------------------------------

def write(path, data=b"x"):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data if isinstance(data, bytes) else data.encode("utf-8"))


def png_fixture(width, height):
    """A real, fully decodable RGBA8 PNG (tools/textures.py's own encoder)."""
    return T.encode_png(width, height, bytes((x * 7 + y * 3) % 256 for y in range(height)
                                              for x in range(width) for _ in range(4)))


def jpeg_fixture(sof_marker, width, height):
    """SOI, a throwaway APP0 segment (so the scanner must skip a non-SOF segment to find the real one),
    then one SOFn segment with one component. Enough for jpeg_size(); not a decodable image."""
    soi = b"\xff\xd8"
    app0_payload = b"JFIF\0\x01\x01\x00\x00\x01\x00\x01\x00\x00"
    app0 = b"\xff\xe0" + struct.pack(">H", len(app0_payload) + 2) + app0_payload
    sof_payload = struct.pack(">BHHB", 8, height, width, 1) + bytes([1, 0x11, 0])
    sof = bytes([0xFF, sof_marker]) + struct.pack(">H", len(sof_payload) + 2) + sof_payload
    return soi + app0 + sof


def menu_form(img_names):
    """A minimal Data/Menu/*.json document (tools/ui.py's generic element tree) whose <form> has one
    <label> per img_name."""
    children = [{"tag": "label", "attributes": {"name": f"w{i}", "img_name": name}, "children": []}
                for i, name in enumerate(img_names)]
    return json.dumps({"version": 1, "tag": "forms", "attributes": {}, "children": [
        {"tag": "form", "attributes": {"name": "Test"}, "children": children}]})


def make_tree(root):
    """A synthetic --assets folder covering every RUNTIME_LOCATIONS entry, several excluded extensions
    and locations (including the one that matters most: Data/Objects/*.json, despite the extension), a
    power-of-two and two non-power-of-two Textures/ files (one UI by folder, one UI by img_name, one 3D),
    and a font atlas. Returns the set of paths (relative, posix) the tool should copy."""
    root = pathlib.Path(root)

    included = {
        "Textures/world.jpg": jpeg_fixture(0xC0, 64, 64),                 # 3D, already power-of-two
        # A real, decodable image (not the header-only JPEG fixture below) so the Pillow resample path
        # under test can actually open and resize it: 3D, NPOT, a resample candidate.
        "Textures/world_npot.png": png_fixture(100, 50),
        "Textures/Icons/icon_npot.png": png_fixture(50, 30),              # UI by folder: never resampled
        "Textures/splash.jpg": jpeg_fixture(0xC0, 90, 40),                # UI by img_name: never resampled
        "Data/Fonts/bookworm_20.json": b"{}",
        "Data/Fonts/bookworm_20.png": png_fixture(8, 8),
        "Data/Levels/knight-river.json": b"{}",
        "Data/Materials/materials.json": b"{}",
        "Data/Menu/MainMenu.json": menu_form(["splash.jpg"]).encode(),
        "Data/Particles/boom.json": b"{}",
        "Data/xml/languages.json": b"{}",
        "Sounds/ballhitbat.ogg": b"x",
        "Sounds/BALLhitWALL.OGG": b"x",  # the shipped data mixes case; must still be found
        "Musics/village.ogg": b"x",
        "Data/Meshes/bg/bg_day.glb": b"x",
    }
    excluded = {
        "Data/Objects/Prizes/speedup.json": b"{}",       # the subtle one: a .json that is never opened
        "Data/Objects/Prizes/speedup.xml": b"<x/>",
        "Data/Levels/knight-river.mb3": b"x",
        "Data/Levels/levels.xml": b"<x/>",                # sits beside the .json this tool DOES copy
        "Data/Materials/materials.xml": b"<x/>",
        "Data/Menu/MainMenu.xml": b"<x/>",
        "Data/Particles/boom.xml": b"<x/>",
        "Data/xml/languages.xml": b"<x/>",
        "Data/SkinMeshes/ball.X": b"x",
        "Data/NxMeshes/hull.cnvx": b"x",
        "Data/Fonts/bookworm_20.crd": b"x",
        "Data/Fonts/bookworm.ttf": b"x",
        "Data/MB3cursor.ani": b"x",
        "AppIcon.icns": b"x",
    }
    for rel, data in {**included, **excluded}.items():
        write(root / rel, data)
    return {pathlib.PurePosixPath(rel).as_posix() for rel in included}


# --- Header parsers ----------------------------------------------------------------------------------------

class PngSizeTest(unittest.TestCase):
    def test_reads_ihdr_dimensions(self):
        self.assertEqual(V.png_size(png_fixture(37, 19)), (37, 19))

    def test_rejects_non_png(self):
        with self.assertRaises(V.HeaderError):
            V.png_size(b"not a png, not even close")


class JpegSizeTest(unittest.TestCase):
    def test_sof0(self):
        self.assertEqual(V.jpeg_size(jpeg_fixture(0xC0, 192, 128)), (192, 128))

    def test_sof2_progressive(self):
        self.assertEqual(V.jpeg_size(jpeg_fixture(0xC2, 100, 50)), (100, 50))

    def test_rejects_non_jpeg(self):
        with self.assertRaises(V.HeaderError):
            V.jpeg_size(b"\x00\x00not a jpeg")

    def test_truncated_file_raises(self):
        with self.assertRaises(V.HeaderError):
            V.jpeg_size(jpeg_fixture(0xC0, 10, 10)[:6])

    def test_skips_fill_bytes_before_a_marker(self):
        # ITU-T.81 B.1.1.2: any number of 0xFF fill bytes may precede a marker's code byte.
        fixture = jpeg_fixture(0xC2, 100, 50)
        soi, rest = fixture[:2], fixture[2:]
        with_fill_bytes = soi + b"\xff\xff\xff" + rest
        self.assertEqual(V.jpeg_size(with_fill_bytes), (100, 50))


# --- Power-of-two math -------------------------------------------------------------------------------------

class NextPotTest(unittest.TestCase):
    def test_table(self):
        cases = {0: 1, 1: 1, 2: 2, 3: 4, 6: 8, 63: 64, 64: 64, 65: 128, 192: 256, 128: 128, 74: 128}
        for n, expected in cases.items():
            self.assertEqual(V.next_pot(n), expected, f"next_pot({n})")


# --- UI vs mipmapped classification -------------------------------------------------------------------------

class UiTextureTest(unittest.TestCase):
    def test_forms_and_icons_folders_are_ui(self):
        self.assertTrue(V.is_ui_texture("Forms/Button.png", set()))
        self.assertTrue(V.is_ui_texture("Icons/Heart.png", set()))

    def test_root_level_file_is_ui_only_when_img_name_says_so(self):
        self.assertFalse(V.is_ui_texture("splash.jpg", set()))
        self.assertTrue(V.is_ui_texture("splash.jpg", {"splash"}))

    def test_extension_and_case_do_not_matter(self):
        # A form can name "forms/button.dds"; the real file is "Forms/Button.png".
        keys = {V._texture_key("forms/button.dds")}
        self.assertTrue(V.is_ui_texture("Forms/Button.png", keys))

    def test_other_textures_are_not_ui(self):
        self.assertFalse(V.is_ui_texture("Pushka.jpg", set()))
        self.assertFalse(V.is_ui_texture("Oreols/02.png", set()))

    def test_ui_texture_keys_walks_nested_children(self):
        document = {"tag": "form", "attributes": {}, "children": [
            {"tag": "panel", "attributes": {}, "children": [
                {"tag": "button", "attributes": {"img_name": "forms/button.dds"}, "children": []}]}]}
        keys = set()
        V._collect_img_names(document, keys)
        self.assertEqual(keys, {"forms/button"})


# --- File selection ----------------------------------------------------------------------------------------

class RuntimeFilesTest(unittest.TestCase):
    def test_selects_exactly_the_runtime_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            assets = pathlib.Path(tmp) / "assets"
            expected = make_tree(assets)
            found = set(V.runtime_files(assets))
            self.assertEqual(found, expected)

    def test_data_objects_json_is_never_selected(self):
        # The one subtlety: despite the extension, nothing under Data/Objects is ever opened at runtime.
        with tempfile.TemporaryDirectory() as tmp:
            assets = pathlib.Path(tmp) / "assets"
            make_tree(assets)
            found = set(V.runtime_files(assets))
            self.assertNotIn("Data/Objects/Prizes/speedup.json", found)


# --- build_pack: mirroring, resampling, the manifest ----------------------------------------------------------

class BuildPackTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.assets = pathlib.Path(self.tmp.name) / "assets"
        self.out = pathlib.Path(self.tmp.name) / "out"
        self.expected = make_tree(self.assets)
        self.logged = []

    def log(self, message):
        self.logged.append(message)

    def test_copies_exactly_the_runtime_files(self):
        report = V.build_pack(self.assets, self.out, log=self.log)
        copied = {p.relative_to(self.out).as_posix() for p in self.out.rglob("*") if p.is_file()}
        copied.discard(V.MANIFEST_NAME)
        self.assertEqual(copied, self.expected)
        self.assertEqual(report["totalFiles"], len(self.expected))

    @unittest.skipUnless(HAVE_PILLOW, "needs Pillow to actually resample")
    def test_mipmapped_npot_texture_is_resampled_to_power_of_two(self):
        report = V.build_pack(self.assets, self.out, log=self.log)
        dst = self.out / "Textures/world_npot.png"
        width, height = V.png_size(dst.read_bytes())
        self.assertEqual((width, height), (V.next_pot(100), V.next_pot(50)))
        paths = {entry["path"] for entry in report["resampledTextures"]}
        self.assertEqual(paths, {"Textures/world_npot.png"})
        self.assertEqual(report["unresampledNpotTextures"], [])

    @unittest.skipUnless(HAVE_PILLOW, "needs Pillow to actually resample")
    def test_ui_npot_textures_are_left_untouched(self):
        V.build_pack(self.assets, self.out, log=self.log)
        for rel in ("Textures/Icons/icon_npot.png", "Textures/splash.jpg"):
            src_bytes = (self.assets / rel).read_bytes()
            dst_bytes = (self.out / rel).read_bytes()
            self.assertEqual(src_bytes, dst_bytes, rel)

    def test_no_resample_copies_npot_textures_unchanged(self):
        report = V.build_pack(self.assets, self.out, resample=False, log=self.log)
        self.assertEqual(report["resampledTextures"], [])
        sizes = {entry["path"] for entry in report["unresampledNpotTextures"]}
        self.assertIn("Textures/world_npot.png", sizes)
        self.assertEqual((self.assets / "Textures/world_npot.png").read_bytes(),
                         (self.out / "Textures/world_npot.png").read_bytes())

    def test_resample_unavailable_still_copies_and_lists_unresampled(self):
        # Simulates an environment with neither Pillow nor sips: the tool must still run.
        with mock.patch.object(V, "_pillow_image_module", return_value=None), \
             mock.patch.object(V.shutil, "which", return_value=None):
            report = V.build_pack(self.assets, self.out, log=self.log)
        self.assertEqual(report["resampledTextures"], [])
        paths = {entry["path"] for entry in report["unresampledNpotTextures"]}
        self.assertIn("Textures/world_npot.png", paths)
        self.assertEqual((self.assets / "Textures/world_npot.png").read_bytes(),
                         (self.out / "Textures/world_npot.png").read_bytes())

    def test_resample_falls_back_to_sips_when_pillow_is_missing(self):
        def fake_sips(cmd, check, capture_output):
            # sips -z H W src --out dst
            dst = pathlib.Path(cmd[-1])
            dst.write_bytes((self.assets / "Textures/world_npot.png").read_bytes())
            return mock.Mock(returncode=0)

        with mock.patch.object(V, "_pillow_image_module", return_value=None), \
             mock.patch.object(V.shutil, "which", return_value="/usr/bin/sips"), \
             mock.patch.object(V.subprocess, "run", side_effect=fake_sips) as run:
            report = V.build_pack(self.assets, self.out, log=self.log)
        run.assert_called_once()
        self.assertEqual(run.call_args.args[0][:2], ["sips", "-z"])
        self.assertEqual([e["method"] for e in report["resampledTextures"]], ["sips"])

    @unittest.skipUnless(HAVE_PILLOW, "needs Pillow to actually resample")
    def test_no_resample_then_resample_fixes_the_npot_texture(self):
        # A --no-resample (or copy-fallback) run must not permanently poison later runs: a matching mtime
        # alone is not "up to date" for a file this run should resample.
        V.build_pack(self.assets, self.out, resample=False, log=self.log)
        report = V.build_pack(self.assets, self.out, log=self.log)
        dst = self.out / "Textures/world_npot.png"
        self.assertEqual(V.png_size(dst.read_bytes()), (V.next_pot(100), V.next_pot(50)))
        paths = {entry["path"] for entry in report["resampledTextures"]}
        self.assertIn("Textures/world_npot.png", paths)
        self.assertEqual(report["unresampledNpotTextures"], [])

    @unittest.skipUnless(HAVE_PILLOW, "needs Pillow to actually resample")
    def test_copy_fallback_then_normal_run_fixes_the_npot_texture(self):
        # Same bug, via the other door: a run where neither Pillow nor sips was available falls back to
        # a verbatim copy but still stamps dst's mtime to match src's.
        with mock.patch.object(V, "_pillow_image_module", return_value=None), \
             mock.patch.object(V.shutil, "which", return_value=None):
            V.build_pack(self.assets, self.out, log=self.log)
        report = V.build_pack(self.assets, self.out, log=self.log)
        dst = self.out / "Textures/world_npot.png"
        self.assertEqual(V.png_size(dst.read_bytes()), (V.next_pot(100), V.next_pot(50)))
        paths = {entry["path"] for entry in report["resampledTextures"]}
        self.assertIn("Textures/world_npot.png", paths)
        self.assertEqual(report["unresampledNpotTextures"], [])

    @unittest.skipUnless(HAVE_PILLOW, "exercises Pillow's own decode failure")
    def test_resample_failure_falls_back_to_copy_and_is_logged(self):
        # An intact IHDR but a body Pillow cannot decode must not abort the whole run.
        truncated = png_fixture(100, 50)[:40]
        write(self.assets / "Textures/trunc_npot.png", truncated)
        report = V.build_pack(self.assets, self.out, log=self.log)
        dst = self.out / "Textures/trunc_npot.png"
        self.assertEqual(dst.read_bytes(), truncated)
        self.assertTrue(any("could not resample" in line and "trunc_npot.png" in line
                             for line in self.logged), self.logged)
        paths = {entry["path"] for entry in report["unresampledNpotTextures"]}
        self.assertIn("Textures/trunc_npot.png", paths)

    def test_sips_failure_falls_back_to_copy_and_is_logged(self):
        def failing_sips(cmd, check, capture_output):
            raise V.subprocess.CalledProcessError(1, cmd)

        with mock.patch.object(V, "_pillow_image_module", return_value=None), \
             mock.patch.object(V.shutil, "which", return_value="/usr/bin/sips"), \
             mock.patch.object(V.subprocess, "run", side_effect=failing_sips):
            report = V.build_pack(self.assets, self.out, log=self.log)
        self.assertEqual((self.assets / "Textures/world_npot.png").read_bytes(),
                         (self.out / "Textures/world_npot.png").read_bytes())
        self.assertTrue(any("could not resample" in line for line in self.logged), self.logged)
        paths = {entry["path"] for entry in report["unresampledNpotTextures"]}
        self.assertIn("Textures/world_npot.png", paths)

    def test_unreadable_texture_header_is_reported_not_silent(self):
        write(self.assets / "Textures/garbage.png", V.PNG_SIGNATURE + b"\x00" * 6)
        report = V.build_pack(self.assets, self.out, log=self.log)
        dst = self.out / "Textures/garbage.png"
        self.assertTrue(dst.exists())
        self.assertEqual(dst.read_bytes(), V.PNG_SIGNATURE + b"\x00" * 6)
        self.assertEqual(report["unreadableTextures"], ["Textures/garbage.png"])
        self.assertTrue(any("unreadable" in line and "garbage.png" in line for line in self.logged),
                         self.logged)
        # Never counted as an unresampled NPOT texture either -- its size is simply unknown.
        paths = {entry["path"] for entry in report["unresampledNpotTextures"]}
        self.assertNotIn("Textures/garbage.png", paths)

    def test_absurd_header_is_reported_and_the_pack_completes(self):
        # A valid PNG whose IHDR claims 20000x20000: resampling it would be a decompression bomb for Pillow
        # (an exception that is not an OSError), so the size is treated as unreadable and the file copied.
        data = bytearray(png_fixture(10, 5))
        ihdr = data.index(b"IHDR")
        data[ihdr + 4:ihdr + 12] = struct.pack(">II", 20000, 20000)
        crc = zlib.crc32(bytes(data[ihdr:ihdr + 17])) & 0xFFFFFFFF
        data[ihdr + 17:ihdr + 21] = struct.pack(">I", crc)
        write(self.assets / "Textures/liar.png", bytes(data))
        report = V.build_pack(self.assets, self.out, log=self.log)
        self.assertEqual((self.out / "Textures/liar.png").read_bytes(), bytes(data))
        self.assertIn("Textures/liar.png", report["unreadableTextures"])
        self.assertTrue((self.out / "vita_pack.json").exists())

    def test_second_run_copies_nothing(self):
        V.build_pack(self.assets, self.out, log=self.log)
        report = V.build_pack(self.assets, self.out, log=self.log)
        self.assertEqual(report["filesCopied"], 0)
        self.assertEqual(report["skippedFiles"], len(self.expected))

    def test_stale_file_is_removed(self):
        V.build_pack(self.assets, self.out, log=self.log)
        (self.assets / "Data/Levels/knight-river.json").unlink()
        report = V.build_pack(self.assets, self.out, log=self.log)
        self.assertEqual(report["removedFiles"], ["Data/Levels/knight-river.json"])
        self.assertFalse((self.out / "Data/Levels/knight-river.json").exists())

    def test_dry_run_writes_nothing(self):
        report = V.build_pack(self.assets, self.out, dry_run=True, log=self.log)
        self.assertFalse(self.out.exists())
        self.assertEqual(report["totalFiles"], len(self.expected))
        self.assertEqual(report["filesCopied"], len(self.expected))
        # A dry run must not claim every NPOT candidate "would be left unresampled" when a real run
        # (resample=True, the default) would in fact resample all of them.
        paths = {entry["path"] for entry in report["resampledTextures"]}
        self.assertIn("Textures/world_npot.png", paths)
        self.assertEqual(report["unresampledNpotTextures"], [])

    def test_dry_run_with_no_resample_still_reports_npot_textures_as_unresampled(self):
        report = V.build_pack(self.assets, self.out, resample=False, dry_run=True, log=self.log)
        self.assertEqual(report["resampledTextures"], [])
        paths = {entry["path"] for entry in report["unresampledNpotTextures"]}
        self.assertIn("Textures/world_npot.png", paths)

    def test_dry_run_does_not_delete_a_stale_file(self):
        V.build_pack(self.assets, self.out, log=self.log)
        (self.assets / "Data/Levels/knight-river.json").unlink()
        report = V.build_pack(self.assets, self.out, dry_run=True, log=self.log)
        self.assertEqual(report["removedFiles"], ["Data/Levels/knight-river.json"])
        self.assertTrue((self.out / "Data/Levels/knight-river.json").exists())

    def test_manifest_file_matches_the_returned_report(self):
        report = V.build_pack(self.assets, self.out, log=self.log)
        on_disk = json.loads((self.out / V.MANIFEST_NAME).read_text())
        self.assertEqual(on_disk, report)
        self.assertEqual(on_disk["ruleVersion"], V.RULE_VERSION)


# --- Refusals ------------------------------------------------------------------------------------------------

class RefusalTest(unittest.TestCase):
    def test_missing_assets_folder(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(V.PackError):
                V.build_pack(pathlib.Path(tmp) / "nope", pathlib.Path(tmp) / "out")

    def test_out_equal_to_assets(self):
        with tempfile.TemporaryDirectory() as tmp:
            assets = pathlib.Path(tmp) / "assets"
            make_tree(assets)
            with self.assertRaises(V.PackError):
                V.build_pack(assets, assets)

    def test_out_inside_the_repository(self):
        with tempfile.TemporaryDirectory() as tmp:
            assets = pathlib.Path(tmp) / "assets"
            make_tree(assets)
            with self.assertRaises(V.PackError):
                V.build_pack(assets, V.REPO_ROOT / "this_should_never_be_created")
            self.assertFalse((V.REPO_ROOT / "this_should_never_be_created").exists())

    def test_out_is_parent_of_assets_leaves_the_source_untouched(self):
        # remove_stale must never run against a folder it did not build: --out one level too high
        # would otherwise see every source file as "stale" and delete it.
        with tempfile.TemporaryDirectory() as tmp:
            assets = pathlib.Path(tmp) / "assets"
            expected = make_tree(assets)
            write(pathlib.Path(tmp) / "unrelated.txt", b"not part of the pack")
            with self.assertRaises(V.PackError):
                V.build_pack(assets, pathlib.Path(tmp))
            found = {p.relative_to(assets).as_posix() for p in assets.rglob("*") if p.is_file()}
            self.assertTrue(expected <= found)
            self.assertTrue((pathlib.Path(tmp) / "unrelated.txt").exists())

    def test_out_inside_assets_is_refused(self):
        # Otherwise a run re-ingests its own previous pack as more "runtime files" on every call.
        with tempfile.TemporaryDirectory() as tmp:
            assets = pathlib.Path(tmp) / "assets"
            make_tree(assets)
            with self.assertRaises(V.PackError):
                V.build_pack(assets, assets / "Textures")

    def test_out_with_unrelated_content_and_no_manifest_is_refused(self):
        # This tool may only ever prune a folder it recognizes as its own (carrying vita_pack.json);
        # otherwise a wrong --out (a memory card path one level too high, say) would delete someone
        # else's files.
        with tempfile.TemporaryDirectory() as tmp:
            assets = pathlib.Path(tmp) / "assets"
            make_tree(assets)
            out = pathlib.Path(tmp) / "out"
            write(out / "thesis.docx", b"not ours")
            write(out / "saves/slot1.sav", b"not ours either")
            with self.assertRaises(V.PackError):
                V.build_pack(assets, out)
            self.assertTrue((out / "thesis.docx").exists())
            self.assertTrue((out / "saves/slot1.sav").exists())

    def test_out_may_be_a_folder_this_tool_already_built(self):
        # The manifest itself is what makes a non-empty --out trustworthy to prune.
        with tempfile.TemporaryDirectory() as tmp:
            assets = pathlib.Path(tmp) / "assets"
            make_tree(assets)
            out = pathlib.Path(tmp) / "out"
            V.build_pack(assets, out)
            V.build_pack(assets, out)  # must not raise

    def test_out_existing_regular_file_is_a_pack_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            assets = pathlib.Path(tmp) / "assets"
            make_tree(assets)
            out_file = pathlib.Path(tmp) / "out"
            write(out_file, b"not a folder")
            with self.assertRaises(V.PackError):
                V.build_pack(assets, out_file)


if __name__ == "__main__":
    unittest.main()
