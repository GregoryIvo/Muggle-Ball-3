#!/usr/bin/env python3
"""Convert synthetic level lists with tools/campaign.py and check the JSON exactly; with MB3_ASSETS set, check the real list."""
import json
import os
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "tools"))
import campaign as C  # noqa: E402

LEVELS = b"""<root>
  <level fileName="piratos-ostrovok.mb3" musicName="Pirates" />
  <level fileName="Piratos-Kartejniki.mb3" musicName="Pirates" />

  <level fileName="village-apple.mb3" musicName="Village" />
  <level fileName="Village-Cafe.MB3" musicName="Village" />
  <level fileName="piratos-SOS.mb3" musicName="Pirates" />
</root>"""


class ParseTest(unittest.TestCase):
    def test_stages_are_runs_of_the_same_music(self):
        doc = C.parse_levels(LEVELS)
        self.assertEqual(doc, {
            "version": 1,
            "stages": [
                {"name": "Pirates", "levels": ["piratos-ostrovok", "Piratos-Kartejniki"]},
                {"name": "Village", "levels": ["village-apple", "Village-Cafe"]},
                {"name": "Pirates", "levels": ["piratos-SOS"]},  # a run of its own: the file order is the game's order
            ],
        })

    def test_the_order_of_the_file_is_kept(self):
        doc = C.parse_levels(b'<root><level fileName="b.mb3" musicName="X"/><level fileName="a.mb3" musicName="X"/></root>')
        self.assertEqual(doc["stages"][0]["levels"], ["b", "a"])

    def test_only_the_mb3_extension_is_dropped(self):
        doc = C.parse_levels(b'<root><level fileName="a.b.mb3" musicName="X"/></root>')
        self.assertEqual(doc["stages"][0]["levels"], ["a.b"])

    def test_errors(self):
        for xml, message in [
            (b"<root><level", "not well-formed"),
            (b"<levels/>", "expected a <root>"),
            (b"<root/>", "no <level>"),
            (b'<root><level musicName="X"/></root>', "needs a fileName and a musicName"),
            (b'<root><level fileName="a.mb3"/></root>', "needs a fileName and a musicName"),
            (b'<root><level fileName="a.txt" musicName="X"/></root>', "not a .mb3"),
            (b'<root><level fileName=".mb3" musicName="X"/></root>', "not a .mb3"),
            (b'<root><level fileName="a" musicName="X"/></root>', "not a .mb3"),
            (b'<root><level fileName="a.mb3" musicName="X"/><level fileName="A.mb3" musicName="Y"/></root>', "listed twice"),
        ]:
            with self.subTest(xml=xml):
                with self.assertRaises(C.CampaignError) as raised:
                    C.parse_levels(xml)
                self.assertIn(message, str(raised.exception))


class ConvertTest(unittest.TestCase):
    def test_writes_levels_json_next_to_the_other_xml_data(self):
        with tempfile.TemporaryDirectory() as tmp:
            assets = pathlib.Path(tmp)
            (assets / "Data" / "Levels").mkdir(parents=True)
            (assets / "Data" / "Levels" / "levels.xml").write_bytes(LEVELS)
            stats = C.convert(assets)
            self.assertEqual(stats, {"levels": 5, "stages": ["Pirates", "Village", "Pirates"]})
            written = json.loads((assets / "Data" / "xml" / "levels.json").read_text(encoding="utf-8"))
            self.assertEqual(written, C.parse_levels(LEVELS))
            # Data/Levels must stay free of it: every .json there is taken for a level.
            self.assertEqual([p.name for p in (assets / "Data" / "Levels").glob("*.json")], [])

    def test_a_missing_or_bad_list_names_the_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            assets = pathlib.Path(tmp)
            with self.assertRaises(OSError):
                C.convert(assets)
            (assets / "Data" / "Levels").mkdir(parents=True)
            (assets / "Data" / "Levels" / "levels.xml").write_bytes(b"<root/>")
            with self.assertRaises(C.CampaignError) as raised:
                C.convert(assets)
            self.assertIn("levels.xml", str(raised.exception))


@unittest.skipUnless(os.environ.get("MB3_ASSETS"), "MB3_ASSETS is not set")
class RealDataTest(unittest.TestCase):
    def test_the_game_list(self):
        assets = pathlib.Path(os.environ["MB3_ASSETS"])
        levels_xml = assets / "Data" / "Levels" / "levels.xml"
        if not levels_xml.is_file():
            self.skipTest("no levels.xml: run tools/convert_assets.py")
        doc = C.parse_levels(levels_xml.read_bytes())
        self.assertEqual([(s["name"], len(s["levels"])) for s in doc["stages"]],
                         [("Pirates", 20), ("Village", 26), ("Cowboys", 17), ("Knights", 20)])
        self.assertEqual(doc["stages"][0]["levels"][0], "piratos-ostrovok")
        on_disk = {p.stem.lower() for p in (assets / "Data" / "Levels").glob("*.mb3")}
        listed = [name.lower() for s in doc["stages"] for name in s["levels"]]
        self.assertEqual(sorted(listed), sorted(on_disk), "every level file is listed once, and nothing else is")


if __name__ == "__main__":
    unittest.main()
