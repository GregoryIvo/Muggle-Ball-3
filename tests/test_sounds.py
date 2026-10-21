#!/usr/bin/env python3
"""Convert synthetic sounds.xml and musics.xml with tools/sounds.py and check the JSON exactly."""
import json
import os
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "tools"))
import sounds as S  # noqa: E402

# Like the shipped file: CRLF line endings, no XML declaration, a Russian comment in cp866.
SOUNDS_XML = ("""<root>
<!-- Пираты -->
<Sound name="menu_piratos">
  <Sample filename="pirat_period1.ogg" delay="50000" />
</Sound>
<Sound name="ballHitBoard">
  <Sample filename="ballHitBoard.ogg" freqStart="0.75" freqEnd="1.25" />
</Sound>
<Sound name="pirat_period">
  <Sample filename="Pirat_Period1.OGG" delay="30000" />
  <Sample filename="pirat_period2.ogg" delay="35000" />
</Sound>
<Sound name="pirat_drop">
  <Sample filename="pirat_drop.ogg" />
</Sound>
<Sound name="rock_hit">
  <Sample filename="ground_hit1.ogg" />
  <Sample filename="ground_hit9.ogg" />
</Sound>
<Sound name="Meteor">
  <Sample filename="meteor.ogg" delay="100" />
</Sound>
<Sound name="meteor">
  <Sample filename="meteor.ogg" />
</Sound>
<Sound name="slow">
  <Sample filename="meteor.ogg" freqStart="0.5" />
</Sound>
</root>
""").replace("\n", "\r\n").encode("cp866")

SOUND_FILES = ["pirat_period1.ogg", "pirat_period2.ogg", "ballhitboard.ogg", "ground_hit1.ogg", "meteor.ogg",
               "unused.ogg"]

MUSICS_XML = b"""<root>
<Music name="Menu"><Stream filename="menu.ogg"/></Music>
<Music name="Cowboys"><Stream filename="Cowboy.ogg"/></Music>
<Music name="Lost"><Stream filename="gone.ogg"/></Music>
</root>"""


class SoundsTest(unittest.TestCase):
    def test_exact_document(self):
        doc, _ = S.parse_sounds(SOUNDS_XML, SOUND_FILES)
        self.assertEqual(doc, {"version": 1, "sounds": [
            {"name": "menu_piratos", "samples": [{"file": "pirat_period1.ogg", "delayMs": 50000}]},
            {"name": "ballHitBoard", "samples": [{"file": "ballhitboard.ogg", "freqStart": 0.75, "freqEnd": 1.25}]},
            {"name": "pirat_period", "samples": [{"file": "pirat_period1.ogg", "delayMs": 30000},
                                                 {"file": "pirat_period2.ogg", "delayMs": 35000}]},
            {"name": "pirat_drop", "samples": []},
            {"name": "rock_hit", "samples": [{"file": "ground_hit1.ogg"}]},
            {"name": "Meteor", "samples": [{"file": "meteor.ogg", "delayMs": 100}]},
            {"name": "slow", "samples": [{"file": "meteor.ogg", "freqStart": 0.5, "freqEnd": 0.5}]},
        ]})

    def test_the_report_lists_duplicates_and_missing_files(self):
        _, report = S.parse_sounds(SOUNDS_XML, SOUND_FILES)
        self.assertEqual(report["duplicates"], ["meteor"])  # "Meteor" came first and wins
        self.assertEqual(report["unresolved"], [["pirat_drop", "pirat_drop.ogg"], ["rock_hit", "ground_hit9.ogg"]])

    def test_file_names_resolve_to_the_spelling_on_disk(self):
        doc, _ = S.parse_sounds(SOUNDS_XML, ["BALLHITBOARD.OGG", "Pirat_Period1.ogg"])
        by_name = {s["name"]: s["samples"] for s in doc["sounds"]}
        self.assertEqual(by_name["ballHitBoard"][0]["file"], "BALLHITBOARD.OGG")
        self.assertEqual(by_name["pirat_period"][0]["file"], "Pirat_Period1.ogg")

    def test_an_exact_match_beats_a_match_in_another_case(self):
        doc, _ = S.parse_sounds(b'<root><Sound name="a"><Sample filename="a.ogg"/></Sound></root>',
                                ["A.ogg", "a.ogg"])
        self.assertEqual(doc["sounds"][0]["samples"], [{"file": "a.ogg"}])

    def test_utf8_files_are_read_as_utf8(self):
        xml = '<root><!-- Пираты --><Sound name="a"><Sample filename="a.ogg"/></Sound></root>'
        doc, _ = S.parse_sounds(xml.encode("utf-8"), ["a.ogg"])
        self.assertEqual(doc["sounds"][0]["name"], "a")

    def test_errors_name_the_sound(self):
        for bad in (b'<root><Sound name="oops"><Sample filename="a.ogg" delay="soon"/></Sound></root>',
                    b'<root><Sound name="oops"><Sample filename="a.ogg" delay="-5"/></Sound></root>',
                    b'<root><Sound name="oops"><Sample filename="a.ogg" freqStart="x"/></Sound></root>',
                    b'<root><Sound name="oops"><Sample/></Sound></root>'):
            with self.assertRaisesRegex(ValueError, "oops"):
                S.parse_sounds(bad, ["a.ogg"])
        with self.assertRaises(ValueError):
            S.parse_sounds(b'<root><Sound><Sample filename="a.ogg"/></Sound></root>', ["a.ogg"])
        with self.assertRaises(ValueError):
            S.parse_sounds(b"<Something />", [])


class MusicsTest(unittest.TestCase):
    def test_exact_document(self):
        doc, missing = S.parse_musics(MUSICS_XML, ["menu.ogg", "cowboy.ogg"])
        self.assertEqual(doc, {"version": 1, "musics": [
            {"name": "Menu", "file": "menu.ogg"},
            {"name": "Cowboys", "file": "cowboy.ogg"},
        ]})
        self.assertEqual(missing, ["Lost"])

    def test_errors(self):
        with self.assertRaisesRegex(ValueError, "oops"):
            S.parse_musics(b'<root><Music name="oops"></Music></root>', [])
        with self.assertRaises(ValueError):
            S.parse_musics(b'<root><Music><Stream filename="a.ogg"/></Music></root>', ["a.ogg"])


class ConvertTest(unittest.TestCase):
    def test_convert_writes_both_files_and_reports_statistics(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            (root / "Data" / "xml").mkdir(parents=True)
            (root / "Sounds").mkdir()
            (root / "Musics").mkdir()
            (root / "Data" / "xml" / "sounds.xml").write_bytes(SOUNDS_XML)
            (root / "Data" / "xml" / "musics.xml").write_bytes(MUSICS_XML)
            for name in SOUND_FILES:
                (root / "Sounds" / name).write_bytes(b"")
            for name in ("menu.ogg", "cowboy.ogg"):
                (root / "Musics" / name).write_bytes(b"")
            stats = S.convert(root / "Data" / "xml", root / "Sounds", root / "Musics", root / "Data" / "xml")
            sounds = json.loads((root / "Data" / "xml" / "sounds.json").read_text(encoding="utf-8"))
            musics = json.loads((root / "Data" / "xml" / "musics.json").read_text(encoding="utf-8"))
        self.assertEqual(sounds, S.parse_sounds(SOUNDS_XML, SOUND_FILES)[0])
        self.assertEqual(musics["musics"][1], {"name": "Cowboys", "file": "cowboy.ogg"})
        self.assertEqual(stats, {
            "sounds": 7, "samples": 7, "silent": ["pirat_drop"],
            "unresolved": [["pirat_drop", "pirat_drop.ogg"], ["rock_hit", "ground_hit9.ogg"]],
            "duplicates": ["meteor"], "musics": 2, "missingMusics": ["Lost"],
        })


ASSETS = pathlib.Path(os.environ.get("MB3_ASSETS", pathlib.Path(__file__).resolve().parent.parent / "assets"))


@unittest.skipUnless((ASSETS / "Data" / "xml" / "sounds.xml").is_file() and (ASSETS / "Sounds").is_dir(),
                     "no converted game data (set MB3_ASSETS)")
class RealDataTest(unittest.TestCase):
    def test_every_music_resolves_and_every_sample_found_is_a_real_file(self):
        xml = ASSETS / "Data" / "xml"
        sounds, report = S.parse_sounds((xml / "sounds.xml").read_bytes(), os.listdir(ASSETS / "Sounds"))
        musics, missing = S.parse_musics((xml / "musics.xml").read_bytes(), os.listdir(ASSETS / "Musics"))
        self.assertEqual(missing, [])
        self.assertEqual([m["name"] for m in musics["musics"]], ["Menu", "Village", "Pirates", "Cowboys", "Knights"])
        for music in musics["musics"]:
            self.assertTrue((ASSETS / "Musics" / music["file"]).is_file(), music)
        for sound in sounds["sounds"]:
            for sample in sound["samples"]:
                self.assertTrue((ASSETS / "Sounds" / sample["file"]).is_file(), sound)
        self.assertEqual(len(sounds["sounds"]) + len(report["duplicates"]), 287)


if __name__ == "__main__":
    unittest.main()
