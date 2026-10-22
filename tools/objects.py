#!/usr/bin/env python3
"""Convert the object definitions and particle systems to JSON.

Usage:
    python3 tools/objects.py DATA_DIR          # every XML under DATA_DIR/Objects and DATA_DIR/Particles
    python3 tools/objects.py in.xml [out.json] # one file (out.json defaults to the .json beside it)

DATA_DIR is the extracted game's ``Data`` folder. Each ``X.xml`` / ``X.XML`` is
written to ``X.json`` next to it. The output is a generic element tree, not a
schema: what each element and attribute means is in docs/OBJECTS.md, the JSON
layout in docs/ASSET_FORMATS.md.

The shipped XML was written by hand and by 3ds Max scripts, so the parser is
lenient. It never gives up on a file; everything it had to fix is listed under
``repairs`` in that file's JSON and summarised on the command line. Names that
look like typos are *not* fixed (see ``suspects``): in the shipped data the
original game most likely ignored them.
"""
import argparse
import codecs
import collections
import json
import pathlib
import re
import sys

OBJECTS_JSON_VERSION = 1

# Names the shipped files use. A tag or attribute outside these sets that becomes a
# known name when one character is deleted is reported as a suspected typo.
KNOWN_TAGS = {
    "root", "objects", "object", "shapes", "box", "sphere", "capsule", "convex", "plane",
    "mesh", "skinmesh", "material", "extra", "joints", "joint", "cloth", "file",
    "moveobjects", "rotateobjects", "fixedjointall", "particles", "particle",
}
KNOWN_ATTRS = {
    "name", "id", "type", "simple", "rotfrozen", "trigger", "radius", "height", "dimensions",
    "rotation", "translation", "data", "frameNode", "description", "oreol", "explose", "speed",
    "shadowType", "velocity", "lifeTime", "bulletType", "disableGravity", "disablePrize",
    "standUp", "light", "particles1", "particles2", "shootSound", "Diffuse", "Specular",
    "Range", "Flicker", "x", "y", "z", "cloth", "width", "fWidth", "fWidth2",
    "fHeight", "wind", "ld", "lu", "rd", "ru", "object1", "object2", "anchor", "axis",
    "breakForce", "breakTorque", "freeSpin", "maxForce", "motor", "velTarget",
    "texture", "num", "fade_time_msec", "size", "direction", "colour", "colour1", "colour2",
    "modulate_colour", "inverse_rotation", "speed_angular", "radius_offset", "radius_big",
    "radius_small", "shadow_plane", "shadow_position", "shake_coeff", "shift_coeff", "angle",
    "dimension",
}
KNOWN_ATTRS_LOWER = {a.lower() for a in KNOWN_ATTRS}

# Values that are names or ids even when they look like numbers.
FORCE_STRING = {
    "name", "id", "type", "texture", "data", "frameNode", "description", "oreol", "explose",
    "shadowType", "particles1", "particles2", "shootSound", "object1", "object2",
    "ld", "lu", "rd", "ru",
}
# Attributes that are vectors of a fixed size; always written as lists of floats.
VECTOR_LEN = {"rotation": 4, "translation": 3, "dimensions": 3, "velocity": 3, "axis": 3,
              "anchor": 3, "direction": 3, "shadow_plane": 3, "shadow_position": 3}
BOOL_WORDS = {"yes": True, "no": False, "true": True, "false": False}

_INT = re.compile(r"[+-]?\d+\Z")
_FLOAT = re.compile(r"[+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?\Z")
_HEX = re.compile(r"0[xX][0-9a-fA-F]+\Z")
_TAG_NAME = re.compile(r"[^\s/<>=]+")
_ATTR_NAME = re.compile(r"[^\s=/<>\"']+")
_ENTITY = re.compile(r"&(#[0-9]+|#[xX][0-9a-fA-F]+|[A-Za-z][A-Za-z0-9]*);")
_NAMED_ENTITIES = {"amp": "&", "lt": "<", "gt": ">", "quot": '"', "apos": "'"}
_CYRILLIC = re.compile("[А-яЁё]")


# ---------------------------------------------------------------- decoding

def _cyrillic_score(text):
    return len(_CYRILLIC.findall(text))


def decode(data):
    """Return (text, encoding name, repairs) for the bytes of an XML file."""
    repairs = []
    if data.startswith(codecs.BOM_UTF8):
        return data[3:].decode("utf-8"), "utf-8", repairs
    if data.startswith((codecs.BOM_UTF16_LE, codecs.BOM_UTF16_BE)):
        return data.decode("utf-16"), "utf-16", repairs

    declared = None
    m = re.match(rb"\s*<\?xml[^>]*?encoding\s*=\s*[\"']([A-Za-z0-9._:-]+)[\"']", data)
    if m:
        declared = m.group(1).decode("ascii")
        try:
            return data.decode(declared), declared.lower(), repairs
        except (LookupError, UnicodeDecodeError):
            repairs.append({"line": 1, "kind": "encoding",
                            "detail": f"declared encoding {declared!r} is unknown or does not fit the bytes"})
    try:
        return data.decode("utf-8"), "utf-8", repairs
    except UnicodeDecodeError as e:
        line = data.count(b"\n", 0, e.start) + 1
    # The shipped files with non-ASCII bytes are Russian 3ds Max comments in the DOS code page.
    best_name, best_text, best_score = None, None, -1
    for name in ("cp866", "cp1251"):
        try:
            text = data.decode(name)
        except UnicodeDecodeError:
            continue
        score = _cyrillic_score(text)
        if score > best_score:
            best_name, best_text, best_score = name, text, score
    if best_name is None:
        best_name, best_text = "latin-1", data.decode("latin-1")
    repairs.append({"line": line, "kind": "encoding",
                    "detail": f"not valid UTF-8 (first bad byte on line {line}); decoded as {best_name}"})
    return best_text, best_name, repairs


# ---------------------------------------------------------------- parsing

class Node:
    __slots__ = ("tag", "attrs", "children", "text", "line", "typo_of", "attr_typo_of")

    def __init__(self, tag, line):
        self.tag = tag
        self.attrs = {}
        self.children = []
        self.text = []
        self.line = line
        self.typo_of = None
        self.attr_typo_of = {}


def _unescape(s):
    def sub(m):
        body = m.group(1)
        if body[0] == "#":
            try:
                code = int(body[2:], 16) if body[1] in "xX" else int(body[1:])
                return chr(code)
            except (ValueError, OverflowError):
                return m.group(0)
        return _NAMED_ENTITIES.get(body, m.group(0))
    return _ENTITY.sub(sub, s)


class _Parser:
    def __init__(self, text):
        self.s = text
        self.n = len(text)
        self.i = 0
        self.repairs = []
        self.comments = []
        self.doc = Node("#document", 1)
        self.stack = [self.doc]

    def line(self, pos):
        return self.s.count("\n", 0, pos) + 1

    def repair(self, pos, kind, detail):
        self.repairs.append({"line": self.line(pos), "kind": kind, "detail": detail})

    def run(self):
        s, n = self.s, self.n
        while self.i < n:
            j = s.find("<", self.i)
            if j < 0:
                self.add_text(s[self.i:], self.i)
                break
            if j > self.i:
                self.add_text(s[self.i:j], self.i)
            self.i = j
            if s.startswith("<!--", j):
                end = s.find("-->", j + 4)
                if end < 0:
                    self.repair(j, "unterminated-comment", "comment is never closed; the rest of the file is comment")
                    end = n
                self.comments.append(s[j + 4:end].strip())
                self.i = end + 3
            elif s.startswith("<![CDATA[", j):
                end = s.find("]]>", j + 9)
                if end < 0:
                    self.repair(j, "unterminated-cdata", "CDATA section is never closed")
                    end = n
                self.stack[-1].text.append(s[j + 9:end])
                self.i = end + 3
            elif s.startswith("<?", j):
                end = s.find("?>", j + 2)
                self.i = n if end < 0 else end + 2
            elif s.startswith("<!", j):
                end = s.find(">", j + 2)
                self.i = n if end < 0 else end + 1
            elif s.startswith("</", j):
                self.end_tag()
            else:
                self.start_tag()
        for node in self.stack[1:]:
            self.repairs.append({"line": node.line, "kind": "missing-end-tag",
                                 "detail": f"<{node.tag}> is never closed; closed at the end of the file"})
        return self.doc

    def add_text(self, raw, pos):
        if not raw.strip():
            return
        if len(self.stack) == 1:
            self.repair(pos, "text-outside-root", f"text outside any element ignored: {raw.strip()[:40]!r}")
            return
        self.stack[-1].text.append(_unescape(raw))

    def end_tag(self):
        s = self.s
        start = self.i
        end = s.find(">", start + 2)
        if end < 0:
            self.repair(start, "unterminated-tag", "end tag has no '>'")
            end = self.n
        name = s[start + 2:end].strip()
        self.i = end + 1
        low = name.lower()
        for k in range(len(self.stack) - 1, 0, -1):
            if self.stack[k].tag == low:
                break
        else:
            self.repair(start, "stray-end-tag", f"</{name}> closes nothing; ignored")
            return
        for node in self.stack[k + 1:]:
            self.repair(start, "missing-end-tag",
                        f"<{node.tag}> (opened on line {node.line}) is closed by </{name}>")
        del self.stack[k + 1:]
        self.stack.pop()

    def start_tag(self):
        s, n = self.s, self.n
        start = self.i
        m = _TAG_NAME.match(s, start + 1)
        if not m:
            self.repair(start, "stray-lt", "'<' that does not start a tag; kept as text")
            self.i = start + 1
            self.add_text("<", start)
            return
        name = m.group()
        node = Node(name.lower(), self.line(start))
        i = m.end()
        self_closing = False
        prev_was_value = False
        while True:
            ws_start = i
            while i < n and s[i].isspace():
                i += 1
            had_space = i > ws_start
            if i >= n:
                self.repair(start, "unterminated-tag", f"<{name}> has no '>' before the end of the file")
                break
            c = s[i]
            if c == ">":
                i += 1
                break
            if c == "/" and s.startswith("/>", i):
                i += 2
                self_closing = True
                break
            if c == "<":
                self.repair(start, "unterminated-tag", f"<{name}> is not closed before the next '<'")
                break
            if c in "/=\"'":
                if c in "\"'":
                    end = s.find(c, i + 1)
                    i = n if end < 0 else end + 1
                else:
                    i += 1
                self.repair(i, "stray-character", f"stray {c!r} inside <{name}> ignored")
                prev_was_value = False
                continue
            m = _ATTR_NAME.match(s, i)
            aname = m.group()
            if prev_was_value and not had_space:
                self.repair(i, "missing-whitespace",
                            f"<{name}>: no whitespace before attribute {aname!r}; inserted")
            i = m.end()
            j = i
            while j < n and s[j].isspace():
                j += 1
            if j < n and s[j] == "=":
                j += 1
                while j < n and s[j].isspace():
                    j += 1
                if j < n and s[j] in "\"'":
                    q = s[j]
                    end = s.find(q, j + 1)
                    if end < 0:
                        gt = s.find(">", j + 1)
                        end = n if gt < 0 else gt
                        self.repair(j, "unterminated-attribute", f"<{name} {aname}=...> has no closing quote")
                        value, i = s[j + 1:end], end
                    else:
                        value, i = s[j + 1:end], end + 1
                else:
                    k = j
                    while k < n and not s[k].isspace() and s[k] != ">":
                        k += 1
                    value = s[j:k]
                    if value.endswith("/") and k < n and s[k] == ">":
                        value = value[:-1]
                        k -= 1
                    self.repair(j, "unquoted-attribute", f"<{name} {aname}={value}> value is not quoted")
                    i = k
                prev_was_value = True
            else:
                value = ""
                self.repair(i, "attribute-without-value", f"<{name} {aname}> has no value; taken as empty")
                prev_was_value = False
            value = _unescape(re.sub(r"[\t\r\n]", " ", value))
            if aname in node.attrs:
                self.repair(i, "duplicate-attribute", f"<{name} {aname}=...> given twice; the last one wins")
            node.attrs[aname] = value
        self.i = i
        self.stack[-1].children.append(node)
        if not self_closing:
            self.stack.append(node)


def parse(text):
    """Parse leniently. Returns (root Node, repairs, comments)."""
    p = _Parser(text)
    doc = p.run()
    if not doc.children:
        raise ValueError("no element found")
    if len(doc.children) == 1:
        root = doc.children[0]
    else:
        names = ", ".join(f"<{c.tag}>" for c in doc.children[:4])
        p.repairs.append({"line": doc.children[1].line, "kind": "multiple-roots",
                          "detail": f"{len(doc.children)} top-level elements ({names}...); wrapped in <document>"})
        root = doc
        root.tag = "document"
    return root, p.repairs, p.comments


# ---------------------------------------------------------------- typing

def _scalar(tok):
    if _INT.match(tok):
        digits = tok.lstrip("+-")
        if (len(digits) > 1 and digits[0] == "0") or len(digits) > 15:
            return None  # "007" and 64-bit ids stay text
        return int(tok)
    if _FLOAT.match(tok):
        return float(tok)
    return None


def type_value(name, text):
    """Return (typed value, needs_raw). needs_raw means the text can't be rebuilt from the value."""
    if name in FORCE_STRING:
        return text, False
    t = text.strip()
    if not t:
        return text, False
    if t.lower() in BOOL_WORDS:
        return BOOL_WORDS[t.lower()], True
    if _HEX.match(t):
        return int(t, 16), True
    vals = [_scalar(tok) for tok in t.split()]
    if all(v is not None for v in vals):
        want = VECTOR_LEN.get(name)
        if want and len(vals) == want:
            return [float(v) for v in vals], False
        return (vals[0] if len(vals) == 1 else vals), False
    return text, False


def _one_deletion(name, known):
    """The known name that `name` becomes when one character is deleted, if exactly one does."""
    hits = {name[:i] + name[i + 1:] for i in range(len(name))} & known
    return next(iter(hits)) if len(hits) == 1 else None


def _typo_pass(node, suspects):
    if node.tag not in KNOWN_TAGS:
        fix = _one_deletion(node.tag, KNOWN_TAGS)
        if fix:
            node.typo_of = fix
            suspects.append({"line": node.line, "kind": "tag-typo",
                             "detail": f"<{node.tag}> looks like a typo of <{fix}>; kept as written"})
    for aname in node.attrs:
        if aname.lower() not in KNOWN_ATTRS_LOWER:
            fix = _one_deletion(aname.lower(), KNOWN_ATTRS_LOWER)
            if fix:
                node.attr_typo_of[aname] = fix
                suspects.append({"line": node.line, "kind": "attribute-typo",
                                 "detail": f"<{node.tag} {aname}=...> looks like a typo of {fix!r}; kept as written"})
    for c in node.children:
        _typo_pass(c, suspects)


def _element(node, suspects):
    out = {"tag": node.tag}
    attrs, raw = {}, {}
    for k, v in node.attrs.items():
        typed, needs_raw = type_value(k, v)
        attrs[k] = typed
        if needs_raw:
            raw[k] = v
        want = VECTOR_LEN.get(k)
        if want and not (isinstance(typed, list) and len(typed) == want):
            suspects.append({"line": node.line, "kind": "vector-size",
                             "detail": f"<{node.tag} {k}={v!r}> is not {want} numbers"})
    if attrs:
        out["attrs"] = attrs
    if raw:
        out["raw"] = raw
    text = "".join(node.text).strip()
    if text:
        out["text"] = text
    if node.typo_of:
        out["typoOf"] = node.typo_of
    if node.attr_typo_of:
        out["attrTypoOf"] = node.attr_typo_of
    if node.children:
        out["children"] = [_element(c, suspects) for c in node.children]
    return out


def _reference_pass(node, files, suspects, seen):
    """A <file name="Data/..."> that matches no shipped file is reported (not a repair)."""
    if node.tag == "file" and files is not None:
        ref = node.attrs.get("name", "").replace("\\", "/")
        key = ref[5:] if ref.lower().startswith("data/") else ref
        if key.lower() not in files and ref not in seen:
            seen.add(ref)
            suspects.append({"line": node.line, "kind": "unresolved-file",
                             "detail": f"<file name={ref!r}> matches no shipped file"})
    for c in node.children:
        _reference_pass(c, files, suspects, seen)


def convert_bytes(data, source, files=None):
    """Convert the bytes of one XML file to the JSON document (a dict).

    files: lower-cased paths relative to Data/ of every shipped file, to check <file> references
    against; None skips that check.
    """
    text, encoding, repairs = decode(data)
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    root, parse_repairs, comments = parse(text)
    repairs += parse_repairs
    suspects = []
    _typo_pass(root, suspects)
    _reference_pass(root, files, suspects, set())
    doc = {"version": OBJECTS_JSON_VERSION, "source": source, "encoding": encoding}
    tree = _element(root, suspects)
    repairs.sort(key=lambda r: r["line"])
    suspects.sort(key=lambda r: r["line"])
    if repairs:
        doc["repairs"] = repairs
    if suspects:
        doc["suspects"] = suspects
    if comments:
        doc["comments"] = comments
    doc["root"] = tree
    return doc


# ---------------------------------------------------------------- writing

def _leaf(el):
    return json.dumps(el, ensure_ascii=False)


def _dump_element(el, depth, lines, trailing):
    pad = " " * depth
    children = el.get("children")
    if not children:
        lines.append(pad + _leaf(el) + trailing)
        return
    head = {k: v for k, v in el.items() if k != "children"}
    lines.append(pad + _leaf(head)[:-1] + ', "children": [')
    for i, c in enumerate(children):
        _dump_element(c, depth + 1, lines, "," if i + 1 < len(children) else "")
    lines.append(pad + "]}" + trailing)


def dump_json(doc):
    """One line per element, so the files diff and read well."""
    lines = ["{"]
    for key, value in doc.items():
        if key == "root":
            continue
        if isinstance(value, list):
            lines.append(f" {json.dumps(key)}: [")
            for i, item in enumerate(value):
                lines.append("  " + json.dumps(item, ensure_ascii=False) + ("," if i + 1 < len(value) else ""))
            lines.append(" ],")
        else:
            lines.append(f" {json.dumps(key)}: {json.dumps(value, ensure_ascii=False)},")
    lines.append(' "root":')
    _dump_element(doc["root"], 1, lines, "")
    lines.append("}")
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------- driving

def _xml_files(data_dir):
    files = [p for p in (data_dir / "Objects").rglob("*") if p.is_file() and p.suffix.lower() == ".xml"]
    particles = data_dir / "Particles"
    if particles.is_dir():
        files += [p for p in particles.glob("*") if p.is_file() and p.suffix.lower() == ".xml"]
    return sorted(files, key=lambda p: str(p).lower())


def convert_file(src, dst, source, files=None):
    doc = convert_bytes(src.read_bytes(), source, files)
    dst.write_text(dump_json(doc), encoding="utf-8")
    return doc


def convert_all(data_dir):
    """Convert every XML under data_dir/Objects and data_dir/Particles next to itself.

    Returns {"files", "failed": [(path, message)], "repairs": Counter, "suspects": Counter,
    "details": [(source, repairs, suspects)], "elements"}. Raises on two inputs that would
    write the same output file.
    """
    data_dir = pathlib.Path(data_dir)
    files = _xml_files(data_dir)
    outputs = collections.defaultdict(list)
    for p in files:
        outputs[str(p.with_suffix(".json")).lower()].append(p)
    clashes = [v for v in outputs.values() if len(v) > 1]
    if clashes:
        raise ValueError("inputs that would write the same .json: " + "; ".join(
            ", ".join(str(p) for p in c) for c in clashes))

    present = {str(p.relative_to(data_dir)).replace("\\", "/").lower() for p in files}
    stats = {"files": 0, "failed": [], "repairs": collections.Counter(),
             "suspects": collections.Counter(), "details": [], "elements": 0}
    for p in files:
        rel = str(p.relative_to(data_dir)).replace("\\", "/")
        try:
            doc = convert_file(p, p.with_suffix(".json"), rel, present)
        except ValueError as e:
            stats["failed"].append((rel, str(e)))
            continue
        stats["files"] += 1
        repairs = doc.get("repairs", [])
        suspects = doc.get("suspects", [])
        for r in repairs:
            stats["repairs"][r["kind"]] += 1
        for r in suspects:
            stats["suspects"][r["kind"]] += 1
        if repairs or suspects:
            stats["details"].append((rel, repairs, suspects))
        stats["elements"] += _count(doc["root"])
    return stats


def _count(el):
    return 1 + sum(_count(c) for c in el.get("children", []))


def report(stats, out=None):
    out = out or sys.stdout
    print(f"objects: {stats['files']} XML files converted to JSON, {stats['elements']} elements", file=out)
    r, s = stats["repairs"], stats["suspects"]
    print(f"  repairs: {sum(r.values())} " + (str(dict(sorted(r.items()))) if r else ""), file=out)
    print(f"  suspects (kept as written): {sum(s.values())} " + (str(dict(sorted(s.items()))) if s else ""), file=out)
    for rel, repairs, suspects in stats["details"]:
        for item in repairs:
            print(f"  repaired {rel}:{item['line']}: {item['detail']}", file=out)
        for item in suspects:
            print(f"  suspect  {rel}:{item['line']}: {item['detail']}", file=out)
    for rel, message in stats["failed"]:
        print(f"  FAILED {rel}: {message}", file=out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("path", type=pathlib.Path, help="the Data folder, or one .xml file")
    ap.add_argument("out", nargs="?", type=pathlib.Path, help="output for a single file")
    args = ap.parse_args()
    if args.path.is_dir():
        stats = convert_all(args.path)
        report(stats)
        sys.exit(1 if stats["failed"] else 0)
    dst = args.out or args.path.with_suffix(".json")
    try:
        doc = convert_file(args.path, dst, args.path.name)
    except ValueError as e:
        sys.exit(f"{args.path}: {e}")
    for item in doc.get("repairs", []):
        print(f"repaired line {item['line']}: {item['detail']}")
    for item in doc.get("suspects", []):
        print(f"suspect line {item['line']}: {item['detail']}")
    print(f"wrote {dst}")


if __name__ == "__main__":
    main()
