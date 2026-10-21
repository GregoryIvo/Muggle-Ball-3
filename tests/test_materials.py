#!/usr/bin/env python3
"""Convert a small synthetic materials.xml with tools/materials.py and check the JSON exactly."""
import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "tools"))
import materials as M  # noqa: E402

# Colours use binary fractions so the expected values are exact. The XML order is A R G B.
XML = """<Materials name="Materials">
    <TexturePreload name="Icons/heart.png" />
    <TexturePreload name="Particles/boom.png" />

    <Materials name="Sea">
        <Materials name="Deep">
            <Material name="Water" density="10.000000" animated="1" animTime="150">
                <Texture texture="01" />
                <Texture texture="02" />
                <RenderState state="19" value="5" />
                <RenderState state="20" value="6" />
                <RenderState state="27" value="1" />
                <NxMaterial staticFriction="0.500000" dynamicFriction="0.250000" restitution="0.125000" frictionCombineMode="1" restitutionCombineMode="2" />
                <D3DMaterial Diffuse="1.000000 0.250000 0.500000 0.750000" Ambient="0.500000 1.000000 0.000000 0.000000" Emissive="1.000000 0.000000 0.000000 0.000000" Specular="0.250000 0.125000 0.125000 0.125000" Power="2.000000" />
                <Extra brickType="1" brickEnergy="3" brickScore="90" exploseType="4" exploseDelay="250" explosePower="1.500000" />
            </Material>
        </Materials>
        <Material name="Shark.jpg" density="0.500000" animated="0" animTime="500" splinter="splinter-blue1.x">
            <Sounds hitSound="" deadSound="old_dead" loopSound="shark_loop" />
            <Sounds dropSound="shark_drop" hitSound="shark_hit" deadSound="shark_dead" />
            <Texture texture="shark" />
            <NxMaterial staticFriction="0.500000" dynamicFriction="0.500000" restitution="0.001000" frictionCombineMode="0" restitutionCombineMode="1" />
            <D3DMaterial Diffuse="1.000000 1.000000 1.000000 1.000000" Ambient="1.000000 0.750000 0.750000 0.750000" Emissive="1.000000 0.000000 0.000000 0.000000" Specular="1.000000 0.000000 0.000000 0.000000" Power="0.000000" />
            <Extra brickType="0" brickEnergy="1" brickScore="91" exploseType="3" exploseDelay="250" explosePower="1.000000" />
        </Material>
    </Materials>

    <Material name="Default" density="10.000000" animated="0" animTime="100">
        <NxMaterial staticFriction="0.125000" dynamicFriction="0.125000" restitution="0.500000" />
        <D3DMaterial Diffuse="1.000000 0.500000 0.500000 0.500000" Ambient="1.000000 0.250000 0.250000 0.250000" Emissive="1.000000 0.000000 0.000000 0.000000" Specular="1.000000 0.000000 0.000000 0.000000" Power="0.000000" />
        <Extra brickType="2" brickEnergy="1" brickScore="10" exploseType="0" exploseDelay="250" explosePower="1.000000" />
    </Material>
    <Material name="default" density="1.000000" animated="0" animTime="0">
        <Texture texture="x" />
        <NxMaterial staticFriction="0.5" dynamicFriction="0.5" restitution="0.5" frictionCombineMode="0" restitutionCombineMode="0" />
        <D3DMaterial Diffuse="1 1 1 1" Ambient="1 1 1 1" Emissive="1 0 0 0" Specular="1 0 0 0" Power="0" />
        <Extra brickType="0" brickEnergy="1" brickScore="0" exploseType="0" exploseDelay="0" explosePower="0" />
    </Material>
</Materials>
""".replace("\n", "\r\n")  # the shipped file has CRLF line endings

EXPECTED = {
    "version": 1,
    "texturePreload": ["Icons/heart.png", "Particles/boom.png"],
    "materials": [
        {
            "name": "Water",
            "group": "Sea/Deep",
            "textures": ["01", "02"],
            "animated": True,
            "animTimeMs": 150,
            "density": 10.0,
            "d3d": {"diffuse": [0.25, 0.5, 0.75, 1.0], "ambient": [1.0, 0.0, 0.0, 0.5],
                    "emissive": [0.0, 0.0, 0.0, 1.0], "specular": [0.125, 0.125, 0.125, 0.25],
                    "power": 2.0},
            "physics": {"staticFriction": 0.5, "dynamicFriction": 0.25, "restitution": 0.125,
                        "frictionCombineMode": 1, "restitutionCombineMode": 2},
            "brick": {"type": 1, "energy": 3, "score": 90, "exploseType": 4,
                      "exploseDelay": 250, "explosePower": 1.5},
            "renderStates": [[19, 5], [20, 6], [27, 1]],
        },
        {
            "name": "Shark.jpg",
            "group": "Sea",
            "textures": ["shark"],
            "animated": False,
            "animTimeMs": 500,
            "density": 0.5,
            "splinter": "splinter-blue1.x",
            "d3d": {"diffuse": [1.0, 1.0, 1.0, 1.0], "ambient": [0.75, 0.75, 0.75, 1.0],
                    "emissive": [0.0, 0.0, 0.0, 1.0], "specular": [0.0, 0.0, 0.0, 1.0],
                    "power": 0.0},
            "physics": {"staticFriction": 0.5, "dynamicFriction": 0.5, "restitution": 0.001,
                        "frictionCombineMode": 0, "restitutionCombineMode": 1},
            "brick": {"type": 0, "energy": 1, "score": 91, "exploseType": 3,
                      "exploseDelay": 250, "explosePower": 1.0},
            "sounds": {"drop": "shark_drop", "hit": "shark_hit", "dead": "shark_dead",
                       "loop": "shark_loop"},
        },
        {
            "name": "Default",
            "group": "",
            "textures": [],
            "animated": False,
            "animTimeMs": 100,
            "density": 10.0,
            "d3d": {"diffuse": [0.5, 0.5, 0.5, 1.0], "ambient": [0.25, 0.25, 0.25, 1.0],
                    "emissive": [0.0, 0.0, 0.0, 1.0], "specular": [0.0, 0.0, 0.0, 1.0],
                    "power": 0.0},
            "physics": {"staticFriction": 0.125, "dynamicFriction": 0.125, "restitution": 0.5,
                        "frictionCombineMode": 0, "restitutionCombineMode": 0},
            "brick": {"type": 2, "energy": 1, "score": 10, "exploseType": 0,
                      "exploseDelay": 250, "explosePower": 1.0},
        },
        {
            "name": "default",
            "group": "",
            "textures": ["x"],
            "animated": False,
            "animTimeMs": 0,
            "density": 1.0,
            "d3d": {"diffuse": [1.0, 1.0, 1.0, 1.0], "ambient": [1.0, 1.0, 1.0, 1.0],
                    "emissive": [0.0, 0.0, 0.0, 1.0], "specular": [0.0, 0.0, 0.0, 1.0],
                    "power": 0.0},
            "physics": {"staticFriction": 0.5, "dynamicFriction": 0.5, "restitution": 0.5,
                        "frictionCombineMode": 0, "restitutionCombineMode": 0},
            "brick": {"type": 0, "energy": 1, "score": 0, "exploseType": 0,
                      "exploseDelay": 0, "explosePower": 0.0},
        },
    ],
}


class ParseTest(unittest.TestCase):
    def test_exact_document(self):
        self.assertEqual(M.parse(XML.encode("ascii")), EXPECTED)

    def test_later_sounds_override_and_empty_values_are_dropped(self):
        shark = M.parse(XML.encode("ascii"))["materials"][1]
        self.assertEqual(shark["sounds"]["hit"], "shark_hit")  # "" in the first element
        self.assertEqual(shark["sounds"]["dead"], "shark_dead")  # overridden
        self.assertEqual(shark["sounds"]["loop"], "shark_loop")  # only in the first element
        solo = M.parse(b'<Materials name="Materials"><Material name="m" density="1" animated="0" animTime="0">'
                       b'<Sounds hitSound="" /></Material></Materials>')
        self.assertNotIn("sounds", solo["materials"][0])

    def test_colours_are_converted_from_argb_to_rgba(self):
        water = M.parse(XML.encode("ascii"))["materials"][0]["d3d"]
        self.assertEqual(water["diffuse"], [0.25, 0.5, 0.75, 1.0])  # A=1 R=.25 G=.5 B=.75
        self.assertEqual(water["ambient"], [1.0, 0.0, 0.0, 0.5])    # A=.5 R=1 G=0 B=0

    def test_rejects_other_documents(self):
        with self.assertRaises(ValueError):
            M.parse(b"<Something />")

    def test_errors_name_the_material(self):
        bad = (b'<Materials name="Materials"><Material name="oops" density="x" animated="0" animTime="0">'
               b'</Material></Materials>')
        with self.assertRaisesRegex(ValueError, "oops"):
            M.parse(bad)
        bad_colour = (b'<Materials name="Materials"><Material name="oops" density="1" animated="0" animTime="0">'
                      b'<D3DMaterial Diffuse="1 2 3" /></Material></Materials>')
        with self.assertRaisesRegex(ValueError, "oops"):
            M.parse(bad_colour)


class ConvertTest(unittest.TestCase):
    def test_convert_writes_the_document_and_reports_statistics(self):
        with tempfile.TemporaryDirectory() as tmp:
            xml, out = pathlib.Path(tmp, "materials.xml"), pathlib.Path(tmp, "materials.json")
            xml.write_bytes(XML.encode("ascii"))
            stats = M.convert(xml, out)
            self.assertEqual(json.loads(out.read_text(encoding="utf-8")), EXPECTED)
        self.assertEqual(stats, {
            "materials": 4,
            "groups": ["Sea/Deep", "Sea"],
            "texturePreload": 2,
            "duplicates": ["Default"],  # "default" repeats it, case-insensitively
            "animated": ["Water"],
            "renderStates": {"Water": [[19, 5], [20, 6], [27, 1]]},
        })


if __name__ == "__main__":
    unittest.main()
