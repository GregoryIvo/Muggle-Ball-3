#!/usr/bin/env python3
"""Convert small synthetic object/particle XML with tools/objects.py and check the JSON.

The synthetic files copy the kinds of damage found in the shipped data: mixed-case tags,
a missing space between attributes, digit-in-the-name typos, comments in the DOS Cyrillic
code page, compound files that include other files.
"""
import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "tools"))
import objects as O  # noqa: E402


def convert(xml, source="Objects/test.xml", files=None):
    data = xml.encode("utf-8") if isinstance(xml, str) else xml
    return O.convert_bytes(data, source, files)


def find(el, tag):
    """All descendants (and el itself) with this tag, in document order."""
    out = [el] if el["tag"] == tag else []
    for c in el.get("children", []):
        out += find(c, tag)
    return out


def kinds(items):
    return sorted(i["kind"] for i in items)


BAT = """<root>
  <objects>
    <object name="BAT" simple="0" type="bat">
        <shapes id="64035220905">
            <sphere name="ph_BoxBottom" trigger="0" radius="3." translation="0. 0. 0. " />
        </shapes>
        <SkinMesh name="bat.x" />
    </object>
  </objects>
</root>
"""


class TypingTest(unittest.TestCase):
    def test_tree_shape_and_lowercase_tags(self):
        doc = convert(BAT)
        self.assertEqual(doc["version"], O.OBJECTS_JSON_VERSION)
        self.assertEqual(doc["source"], "Objects/test.xml")
        self.assertEqual(doc["encoding"], "utf-8")
        self.assertNotIn("repairs", doc)
        self.assertNotIn("suspects", doc)
        root = doc["root"]
        self.assertEqual(root["tag"], "root")
        self.assertEqual(root["children"][0]["tag"], "objects")
        obj = root["children"][0]["children"][0]
        self.assertEqual(obj["tag"], "object")
        self.assertEqual([c["tag"] for c in obj["children"]], ["shapes", "skinmesh"])
        self.assertEqual(obj["children"][1]["attrs"], {"name": "bat.x"})

    def test_attribute_types(self):
        doc = convert('<a n="3" f=".6" g="12." h="-0.5" id="64035220905" name="01" '
                      'v="0 0 -10" w="0. 1. 0." s="Hello world" e="" />')
        a = doc["root"]["attrs"]
        self.assertEqual(a["n"], 3)
        self.assertIsInstance(a["n"], int)
        self.assertEqual(a["f"], 0.6)
        self.assertEqual(a["g"], 12.0)
        self.assertIsInstance(a["g"], float)
        self.assertEqual(a["h"], -0.5)
        self.assertEqual(a["id"], "64035220905")  # ids stay text
        self.assertEqual(a["name"], "01")          # names stay text
        self.assertEqual(a["v"], [0, 0, -10])
        self.assertEqual(a["w"], [0.0, 1.0, 0.0])
        self.assertEqual(a["s"], "Hello world")
        self.assertEqual(a["e"], "")
        self.assertNotIn("raw", doc["root"])

    def test_vectors_and_quaternions_are_float_lists(self):
        doc = convert('<box dimensions="2 2 2" rotation="0 0 0 1" translation="0.00000 1.00000 -0.00000 " '
                      'velocity="0. 3. 40. " />')
        a = doc["root"]["attrs"]
        self.assertEqual(a["dimensions"], [2.0, 2.0, 2.0])
        self.assertTrue(all(isinstance(v, float) for v in a["dimensions"]))
        self.assertEqual(a["rotation"], [0.0, 0.0, 0.0, 1.0])  # x y z w
        self.assertTrue(all(isinstance(v, float) for v in a["rotation"]))
        self.assertEqual(a["translation"], [0.0, 1.0, 0.0])
        self.assertEqual(a["velocity"], [0.0, 3.0, 40.0])
        self.assertNotIn("suspects", doc)

    def test_wrong_size_vector_is_reported_not_changed(self):
        doc = convert('<box rotation="0 0 1" />')
        self.assertEqual(doc["root"]["attrs"]["rotation"], [0, 0, 1])
        self.assertEqual(kinds(doc["suspects"]), ["vector-size"])

    def test_booleans_and_hex_keep_the_original_text(self):
        doc = convert('<particle modulate_colour = "no" inverse_rotation="YES" colour1 = "0x00e2e2ff" '
                      'flag="1" />')
        a, raw = doc["root"]["attrs"], doc["root"]["raw"]
        self.assertIs(a["modulate_colour"], False)
        self.assertIs(a["inverse_rotation"], True)
        self.assertEqual(a["colour1"], 0x00E2E2FF)
        self.assertEqual(a["flag"], 1)  # 0/1 flags are numbers, not booleans
        self.assertEqual(raw, {"modulate_colour": "no", "inverse_rotation": "YES", "colour1": "0x00e2e2ff"})

    def test_text_that_only_looks_numeric_stays_text(self):
        a = convert('<a x="007" y="1e999x" z="nan" w="inf" big="12345678901234567" />')["root"]["attrs"]
        self.assertEqual(a, {"x": "007", "y": "1e999x", "z": "nan", "w": "inf", "big": "12345678901234567"})

    def test_attribute_names_keep_their_case(self):
        a = convert('<Extra Diffuse="0. 1. 1. 0." disableGravity="1" />')["root"]
        self.assertEqual(a["tag"], "extra")
        self.assertEqual(a["attrs"], {"Diffuse": [0.0, 1.0, 1.0, 0.0], "disableGravity": 1})

    def test_entities_and_text(self):
        doc = convert('<a t="x &amp; y &lt;z&gt; &#65;&#x42; &bogus;">hello &amp; bye<![CDATA[ <raw> ]]></a>')
        self.assertEqual(doc["root"]["attrs"]["t"], "x & y <z> AB &bogus;")
        self.assertEqual(doc["root"]["text"], "hello & bye <raw>")


class RepairTest(unittest.TestCase):
    def test_missing_whitespace_between_attributes(self):
        doc = convert('<root>\n<Extra description="SpeedUp"oreol="oreolred" />\n</root>')
        extra = find(doc["root"], "extra")[0]
        self.assertEqual(extra["attrs"], {"description": "SpeedUp", "oreol": "oreolred"})
        self.assertEqual(kinds(doc["repairs"]), ["missing-whitespace"])
        self.assertEqual(doc["repairs"][0]["line"], 2)

    def test_unquoted_value_and_valueless_attribute(self):
        doc = convert('<root><a radius=3.5 flag disabled=1/></root>')
        a = find(doc["root"], "a")[0]
        self.assertEqual(a["attrs"], {"radius": 3.5, "flag": "", "disabled": 1})
        self.assertEqual(kinds(doc["repairs"]), ["attribute-without-value", "unquoted-attribute", "unquoted-attribute"])

    def test_duplicate_attribute_last_wins(self):
        doc = convert('<a x="1" x="2" />')
        self.assertEqual(doc["root"]["attrs"], {"x": 2})
        self.assertEqual(kinds(doc["repairs"]), ["duplicate-attribute"])

    def test_unterminated_attribute_value(self):
        doc = convert('<root><a x="1 /></root>')
        self.assertEqual(kinds(doc["repairs"]), ["missing-end-tag", "unterminated-attribute"])
        self.assertEqual(find(doc["root"], "a")[0]["attrs"], {"x": "1 /"})

    def test_end_tag_closes_open_children_and_stray_end_tags_are_dropped(self):
        doc = convert('<root>\n<objects>\n<object name="a">\n<shapes>\n<box />\n</object>\n</objects></zzz>\n</ROOT>')
        self.assertEqual(kinds(doc["repairs"]), ["missing-end-tag", "stray-end-tag"])
        shapes = find(doc["root"], "shapes")[0]
        self.assertEqual(shapes["children"], [{"tag": "box"}])
        self.assertEqual(len(find(doc["root"], "object")), 1)

    def test_unclosed_elements_close_at_end_of_file(self):
        doc = convert('<root><objects><object name="a">')
        self.assertEqual(kinds(doc["repairs"]), ["missing-end-tag"] * 3)
        self.assertEqual(find(doc["root"], "object")[0]["attrs"], {"name": "a"})

    def test_unterminated_tag_stops_at_next_tag(self):
        doc = convert('<root><a x="1" <b/></root>')
        self.assertIn("unterminated-tag", kinds(doc["repairs"]))
        self.assertEqual(find(doc["root"], "a")[0]["attrs"], {"x": 1})
        self.assertEqual(len(find(doc["root"], "b")), 1)

    def test_end_tag_case_does_not_matter(self):
        doc = convert('<Root><Shapes><Box /></shapes></ROOT>')
        self.assertNotIn("repairs", doc)
        self.assertEqual(find(doc["root"], "box"), [{"tag": "box"}])

    def test_several_top_level_elements_are_wrapped(self):
        doc = convert('<a/><b x="1"/>')
        self.assertEqual(doc["root"]["tag"], "document")
        self.assertEqual([c["tag"] for c in doc["root"]["children"]], ["a", "b"])
        self.assertEqual(kinds(doc["repairs"]), ["multiple-roots"])

    def test_text_outside_the_root_is_ignored_and_reported(self):
        doc = convert('junk <root/> more junk')
        self.assertEqual(doc["root"], {"tag": "root"})
        self.assertEqual(kinds(doc["repairs"]), ["text-outside-root", "text-outside-root"])

    def test_stray_less_than_is_text(self):
        doc = convert('<root>1 < 2</root>')
        self.assertEqual(doc["root"]["text"], "1 < 2")
        self.assertEqual(kinds(doc["repairs"]), ["stray-lt"])

    def test_no_element_is_an_error(self):
        for text in ("", "   ", "<!-- only a comment -->", "plain text"):
            with self.assertRaises(ValueError):
                convert(text)

    def test_repairs_are_sorted_by_line(self):
        doc = convert('<root>\n<a x=1/>\n<b y=2/>\n</oops>\n</root>')
        lines = [r["line"] for r in doc["repairs"]]
        self.assertEqual(lines, sorted(lines))
        self.assertEqual(lines[0], 2)


class EncodingTest(unittest.TestCase):
    def test_utf8_bom_and_utf16(self):
        xml = '<root><a name="é" /></root>'
        for data in (b"\xef\xbb\xbf" + xml.encode("utf-8"), xml.encode("utf-16")):
            doc = convert(data)
            self.assertEqual(find(doc["root"], "a")[0]["attrs"]["name"], "é")
            self.assertNotIn("repairs", doc)

    def test_declared_encoding_is_used(self):
        data = '<?xml version="1.0" encoding="windows-1251"?><root><a name="Привет" /></root>'.encode("cp1251")
        doc = convert(data)
        self.assertEqual(find(doc["root"], "a")[0]["attrs"]["name"], "Привет")
        self.assertEqual(doc["encoding"], "windows-1251")

    def test_dos_cyrillic_comment_is_decoded_and_reported(self):
        # "Низ корабля" in code page 866, as in the ship files' 3ds Max comments
        comment = "Низ корабля".encode("cp866")
        data = b'<root>\n<!-- ' + comment + b' -->\n<a x="1"/></root>'
        doc = convert(data)
        self.assertEqual(doc["encoding"], "cp866")
        self.assertEqual(doc["comments"], ["Низ корабля"])
        self.assertEqual(kinds(doc["repairs"]), ["encoding"])
        self.assertEqual(doc["repairs"][0]["line"], 2)

    def test_windows_cyrillic_is_told_apart_from_dos(self):
        data = b'<root><!-- ' + "Привет мир".encode("cp1251") + b' --></root>'
        doc = convert(data)
        self.assertEqual(doc["encoding"], "cp1251")
        self.assertEqual(doc["comments"], ["Привет мир"])

    def test_undecodable_bytes_never_fail(self):
        doc = convert(b'<root><!-- \xff\xfe\x98 --></root>')
        self.assertEqual(find(doc["root"], "root")[0]["tag"], "root")
        self.assertEqual(kinds(doc["repairs"]), ["encoding"])

    def test_crlf_is_normalised(self):
        doc = convert(b'<root>\r\n<!-- a\r\nb -->\r\n<x y="1\r\n2"/>\r\n</root>')
        self.assertEqual(doc["comments"], ["a\nb"])
        self.assertEqual(find(doc["root"], "x")[0]["attrs"]["y"], [1, 2])


class SuspectTest(unittest.TestCase):
    def test_typos_are_kept_as_written_and_flagged(self):
        doc = convert('<root><shapes><b1ox name="w" /><Sphere radius=".1" rotat1ion="0 0 0 1"/>'
                      '<mesfh name="arbuz.x" /><m1esh name="t.x" /></shapes></root>')
        shapes = find(doc["root"], "shapes")[0]
        self.assertEqual([c["tag"] for c in shapes["children"]], ["b1ox", "sphere", "mesfh", "m1esh"])
        self.assertEqual(shapes["children"][0]["typoOf"], "box")
        self.assertEqual(shapes["children"][1]["attrTypoOf"], {"rotat1ion": "rotation"})
        self.assertEqual(shapes["children"][1]["attrs"]["rotat1ion"], [0, 0, 0, 1])  # present, not renamed
        self.assertEqual(shapes["children"][2]["typoOf"], "mesh")
        self.assertEqual(shapes["children"][3]["typoOf"], "mesh")
        self.assertEqual(kinds(doc["suspects"]), ["attribute-typo", "tag-typo", "tag-typo", "tag-typo"])
        self.assertNotIn("repairs", doc)

    def test_unknown_names_that_match_nothing_are_left_alone(self):
        doc = convert('<root><gizmo foo="1" /></root>')
        self.assertNotIn("suspects", doc)
        self.assertEqual(find(doc["root"], "gizmo")[0]["attrs"], {"foo": 1})

    def test_known_names_with_digits_are_not_typos(self):
        doc = convert('<joint object1="a" object2="b" /><extra particles1="trail" particles2="smoke" />')
        self.assertNotIn("suspects", doc)

    def test_unresolved_file_reference(self):
        xml = ('<root><objects>\n<File name="Data/Objects/Ground/trava.xml" />\n<MoveObjects x="4" />\n'
               '<File name="Data/Objects/Grass/fence.xml" />\n<File name="Data/Objects/Grass/fence.xml" />\n'
               '</objects></root>')
        doc = convert(xml, files={"objects/ground/trava.xml"})
        self.assertEqual(kinds(doc["suspects"]), ["unresolved-file"])  # once, however often it is used
        self.assertEqual(doc["suspects"][0]["line"], 4)
        self.assertNotIn("suspects", convert(xml))  # no file list, no check
        moves = find(doc["root"], "moveobjects")
        self.assertEqual(moves[0]["attrs"], {"x": 4})

    def test_file_reference_is_case_insensitive(self):
        doc = convert('<root><File name="Data/Objects/Ground/Trava.XML" /></root>', files={"objects/ground/trava.xml"})
        self.assertNotIn("suspects", doc)

    def test_comments_are_kept(self):
        doc = convert('<root><!-- <object name="old" /> --><a/></root>')
        self.assertEqual(doc["comments"], ['<object name="old" />'])
        self.assertEqual(find(doc["root"], "object"), [])


class WriterTest(unittest.TestCase):
    def test_dump_round_trips_and_is_one_element_per_line(self):
        doc = convert(BAT.replace("<root>", "<root><!-- c -->").replace('simple="0"', 'simple=0 colour="0xff"'))
        text = O.dump_json(doc)
        self.assertEqual(json.loads(text), doc)
        self.assertTrue(text.endswith("\n"))
        lines = text.splitlines()
        self.assertTrue(any(line.strip().startswith('{"tag": "sphere"') for line in lines))
        self.assertLess(max(len(line) for line in lines), 400)

    def test_dump_keeps_non_ascii_readable(self):
        doc = convert('<root><a name="Привет"/></root>')
        self.assertIn("Привет", O.dump_json(doc))

    def test_dump_is_stable(self):
        doc = convert(BAT)
        self.assertEqual(O.dump_json(doc), O.dump_json(json.loads(O.dump_json(doc))))


class TreeTest(unittest.TestCase):
    def make(self, tmp):
        data = pathlib.Path(tmp)
        (data / "Objects" / "Prizes").mkdir(parents=True)
        (data / "Objects" / "Ground").mkdir()
        (data / "Particles").mkdir()
        (data / "Objects" / "Prizes" / "speedup.XML").write_text(
            '<root><objects><object name="s" type="Prize"><Extra description="SpeedUp"oreol="oreolred" />'
            '</object></objects></root>')
        (data / "Objects" / "Ground" / "trava.xml").write_text('<root><objects/></root>')
        (data / "Objects" / "Ground" / "matrix.xml").write_bytes(
            b'<root><File name="Data/Objects/Ground/trava.xml" /><File name="Data/Objects/Gone.xml" /></root>')
        (data / "Particles" / "Spark.xml").write_text(
            '<particles>\n  <particle name = "Spark" type = "WaterBubble" num = "10" speed = "1." />\n</particles>')
        (data / "Objects" / "notes.txt").write_text("not xml")
        return data

    def test_convert_all_writes_json_next_to_each_xml(self):
        with tempfile.TemporaryDirectory() as tmp:
            data = self.make(tmp)
            stats = O.convert_all(data)
            self.assertEqual(stats["files"], 4)
            self.assertEqual(stats["failed"], [])
            self.assertEqual(stats["repairs"], {"missing-whitespace": 1})
            self.assertEqual(stats["suspects"], {"unresolved-file": 1})
            self.assertTrue((data / "Objects" / "Prizes" / "speedup.json").is_file())
            self.assertTrue((data / "Objects" / "Ground" / "trava.json").is_file())
            self.assertTrue((data / "Particles" / "Spark.json").is_file())
            self.assertFalse((data / "Objects" / "notes.json").exists())
            doc = json.loads((data / "Objects" / "Prizes" / "speedup.json").read_text(encoding="utf-8"))
            self.assertEqual(doc["source"], "Objects/Prizes/speedup.XML")
            particle = json.loads((data / "Particles" / "Spark.json").read_text(encoding="utf-8"))
            self.assertEqual(particle["root"]["children"][0]["attrs"],
                             {"name": "Spark", "type": "WaterBubble", "num": 10, "speed": 1.0})
            self.assertEqual(stats["elements"], 4 + 2 + 3 + 2)

    def test_second_run_converts_the_same_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            data = self.make(tmp)
            first = O.convert_all(data)
            before = (data / "Objects" / "Ground" / "matrix.json").read_text(encoding="utf-8")
            second = O.convert_all(data)  # the .json files from run one are not inputs
            self.assertEqual(first["files"], second["files"])
            self.assertEqual(before, (data / "Objects" / "Ground" / "matrix.json").read_text(encoding="utf-8"))

    def test_a_broken_file_is_reported_and_the_rest_still_convert(self):
        with tempfile.TemporaryDirectory() as tmp:
            data = self.make(tmp)
            (data / "Objects" / "empty.xml").write_text("")
            stats = O.convert_all(data)
            self.assertEqual(stats["files"], 4)
            self.assertEqual([rel for rel, _ in stats["failed"]], ["Objects/empty.xml"])

    def test_two_inputs_for_one_output_are_refused(self):
        # Two names that differ only in case can't exist side by side on every file system,
        # so hand the converter the list it would get from a case-sensitive one.
        with tempfile.TemporaryDirectory() as tmp:
            data = pathlib.Path(tmp)
            (data / "Objects").mkdir()
            names = [data / "Objects" / "trava.xml", data / "Objects" / "TRAVA.XML"]
            real = O._xml_files
            O._xml_files = lambda d: names
            try:
                with self.assertRaises(ValueError) as ctx:
                    O.convert_all(data)
            finally:
                O._xml_files = real
            self.assertIn("same .json", str(ctx.exception))

    def test_command_line_converts_one_file_or_a_folder(self):
        import contextlib
        import io
        with tempfile.TemporaryDirectory() as tmp:
            data = self.make(tmp)
            src = data / "Objects" / "Prizes" / "speedup.XML"
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                O.sys.argv, saved = ["objects.py", str(src)], O.sys.argv
                try:
                    O.main()
                finally:
                    O.sys.argv = saved
            self.assertIn("repaired line 1", out.getvalue())
            self.assertIn("speedup.json", out.getvalue())
            self.assertTrue((data / "Objects" / "Prizes" / "speedup.json").is_file())

            elsewhere = data / "somewhere.json"
            O.sys.argv, saved = ["objects.py", str(src), str(elsewhere)], O.sys.argv
            try:
                with contextlib.redirect_stdout(io.StringIO()):
                    O.main()
            finally:
                O.sys.argv = saved
            self.assertEqual(json.loads(elsewhere.read_text(encoding="utf-8"))["source"], "speedup.XML")

            (data / "Objects" / "empty.xml").write_text("")
            O.sys.argv, saved = ["objects.py", str(data)], O.sys.argv
            try:
                with contextlib.redirect_stdout(io.StringIO()) as folder_out:
                    with self.assertRaises(SystemExit) as ctx:
                        O.main()
            finally:
                O.sys.argv = saved
            self.assertEqual(ctx.exception.code, 1)  # one file failed
            self.assertIn("FAILED Objects/empty.xml", folder_out.getvalue())
            with self.assertRaises(SystemExit) as ctx:  # a single empty file
                O.sys.argv, saved = ["objects.py", str(data / "Objects" / "empty.xml")], O.sys.argv
                try:
                    O.main()
                finally:
                    O.sys.argv = saved
            self.assertIn("no element found", str(ctx.exception.code))

    def test_report_lists_every_repair(self):
        import io
        with tempfile.TemporaryDirectory() as tmp:
            stats = O.convert_all(self.make(tmp))
            out = io.StringIO()
            O.report(stats, out)
            text = out.getvalue()
            self.assertIn("4 XML files", text)
            self.assertIn("speedup.XML:1", text)
            self.assertIn("Gone.xml", text)


if __name__ == "__main__":
    unittest.main()
