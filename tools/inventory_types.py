#!/usr/bin/env python3
"""Inventory of the record types in a corpus of DXF files (phase 9, step 9.0).

    python -I tools/inventory_types.py [--root DIR ...] [--json FILE] [--check-registry] [--forms]
    python -I tools/inventory_types.py --self-test

Reads ASCII and binary DXF (R12 one-byte group codes, R13 and later two-byte), counts the 0-record names
of BLOCKS, ENTITIES and OBJECTS (CLASSES is counted apart: a registered class is not a sample of its
record), per name and ACADVER, and notes for every record type the group order it was written in. A file is
counted once per content (SHA-256, the first path in the order build/test-data, doc/facit, doc/acad_*);
a binary file whose ASCII twin was read is a twin, not a second sample. A file that cannot be read (the old
inventory's one invalid template had @@LASTSAVEDBY@@ where a group code stands) is an error in the report,
never a type. Each file is classified by who produced it (an AutoCAD file with a known provenance, another
program's file) from the provenance table below — the provenance table is the project's own knowledge
(test/data/README.md), not a finding of this tool; verify before relying on it.

The default roots are build/test-data, doc/facit and doc/acad_*_dxf.dxf. Standard library only; reads
text, never HDxf's code and never another project's.

--check-registry   compare the names found with src/typed_registry_data.inc (exit 1 for a name not in it)
--forms            write the observed group orders (appendix B of the old plan) with their samples
"""

import argparse
import hashlib
import json
import os
import re
import struct
import sys
import tempfile
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SENTINEL = b"AutoCAD Binary DXF\r\n\x1a\x00"
STRUCTURAL = {"BLOCK", "ENDBLK", "SEQEND", "VERTEX"}
COUNTED_SECTIONS = ("BLOCKS", "ENTITIES", "OBJECTS")

# ---------------------------------------------------------------------------
# Provenance: who produced a file (test/data/README.md; B100: only AutoCAD's own files are facit)
# ---------------------------------------------------------------------------

# (path prefix relative to the root the file is under, class, reason). First match wins.
PROVENANCE = [
    ("doc/facit/web/", "web-unproven", "files from the web: AutoCAD origin probable but not proven (doc/facit/web/SOURCES.md)"),
    ("doc/facit/", "autocad-facit", "saved by AutoCAD 2025's core from the project's scripts (doc/facit/run_all.txt)"),
    ("doc/acad_", "autocad-facit", "saved by AutoCAD 2025 (doc/acad_*_dxf.dxf)"),
    ("acadsharp/", "autocad", "ACadSharp's samples: one drawing saved by AutoCAD in every version (test/data/README.md)"),
    ("jscad/", "autocad", "jscad's sample files: AutoCAD 2017's new drawing and its 3D drawings"),
    ("libre/", "autocad", "LibreDWG's/OpenAEC's test drawings saved by AutoCAD (test/data/README.md)"),
    ("oxcart/", "autocad", "Oxcart's drawings: AutoCAD 2017's 2013 files"),
    ("aspose/", "autocad", "Aspose.CAD's sample drawings (AutoCAD; test/data/README.md)"),
    ("ezdxf/mtext_framed_columns.dxf", "autocad", "ezdxf's test data saved by AutoCAD 2013"),
    ("ezdxf/mtext_text_frame.dxf", "autocad", "ezdxf's test data saved by AutoCAD 2013"),
    ("ezdxf/", "other", "ezdxf's test data: other programs' files (ezdxf, ODA)"),
    ("gvsig/", "autocad", "gvSIG's test resource: AutoCAD R13's file"),
    ("spatialauxetic/", "other", "SpatialAuxetic's drawings (R14 header, written by other software)"),
    ("gngeovane/", "other", "a CNC plotter project's drawing"),
    ("local-r13r14/", "other", "third-party R13/R14 files (build/test-data/local-r13r14/PROVENANCE.txt)"),
]


def provenance(relative):
    rel = relative.replace("\\", "/")
    for prefix, kind, reason in PROVENANCE:
        if rel.startswith(prefix):
            return kind, reason
    return "unknown", "no provenance recorded"


def autocad(kind):
    return kind.startswith("autocad")


# ---------------------------------------------------------------------------
# Group reading
# ---------------------------------------------------------------------------

def value_type(code, record):
    """The value type of a group code in binary DXF, as the DXF reference's ranges give it."""
    if code == 71 and record in ("DATATABLE", "ACDBDATATABLE"):
        return "i32"
    if 0 <= code <= 9:
        return "str"
    if 10 <= code <= 59:
        return "f64"
    if 60 <= code <= 79:
        return "i16"
    if 90 <= code <= 99:
        return "i32"
    if 100 <= code <= 102:
        return "str"
    if code == 105:
        return "str"
    if 110 <= code <= 149:
        return "f64"
    if 160 <= code <= 169:
        return "i64"
    if 170 <= code <= 179:
        return "i16"
    if 210 <= code <= 239:
        return "f64"
    if 270 <= code <= 289:
        return "i16"
    if 290 <= code <= 299:
        return "bool"
    if 300 <= code <= 309:
        return "str"
    if 310 <= code <= 319:
        return "bin"
    if 320 <= code <= 369:
        return "str"
    if 370 <= code <= 389:
        return "i16"
    if 390 <= code <= 399:
        return "str"
    if 400 <= code <= 409:
        return "i16"
    if 410 <= code <= 419:
        return "str"
    if 420 <= code <= 429:
        return "i32"
    if 430 <= code <= 439:
        return "str"
    if 440 <= code <= 459:
        return "i32"
    if 460 <= code <= 469:
        return "f64"
    if 470 <= code <= 481:
        return "str"
    if code == 999:
        return "str"
    if 1000 <= code <= 1003:
        return "str"
    if code == 1004:
        return "bin"
    if 1005 <= code <= 1009:
        return "str"
    if 1010 <= code <= 1059:
        return "f64"
    if 1060 <= code <= 1070:
        return "i16"
    if code == 1071:
        return "i32"
    return None


class DxfError(Exception):
    pass


def read_ascii(data):
    text = data.decode("latin-1")
    lines = text.split("\n")
    groups = []
    i = 0
    while i < len(lines):
        code_text = lines[i].strip()
        if i + 1 >= len(lines):
            if code_text == "":
                break                       # the end of the last line
            raise DxfError("an ASCII file must have a code line for every value line")
        if not re.fullmatch(r"-?\d+", code_text):
            raise DxfError("group %d: %r is not a group code" % (i // 2 + 1, code_text[:40]))
        groups.append((int(code_text), lines[i + 1].rstrip("\r")))
        i += 2
        if groups[-1] == (0, "EOF"):
            break                           # anything after EOF is ignored
    return groups


def read_binary(data):
    if not data.startswith(SENTINEL):
        raise DxfError("not a binary DXF file")
    pos = len(SENTINEL)
    # the first group is 0/SECTION: one-byte codes in R12 (00 'S'), two-byte codes from R13 (00 00 'S')
    two_byte = len(data) > pos + 1 and data[pos] == 0 and data[pos + 1] == 0
    groups = []
    record = ""
    n = len(data)
    while pos < n:
        if two_byte:
            if pos + 2 > n:
                raise DxfError("a binary file ends inside a group code")
            code = struct.unpack_from("<H", data, pos)[0]
            pos += 2
        else:
            code = data[pos]
            pos += 1
            if code == 255:
                if pos + 2 > n:
                    raise DxfError("a binary file ends inside an extended group code")
                code = struct.unpack_from("<h", data, pos)[0]
                pos += 2
        kind = value_type(code, record)
        if kind is None:
            raise DxfError("group code %d has no known value type (group %d)" % (code, len(groups) + 1))
        try:
            if kind == "str":
                end = data.index(b"\x00", pos)
                value = data[pos:end].decode("latin-1")
                pos = end + 1
            elif kind == "f64":
                value = repr(struct.unpack_from("<d", data, pos)[0])
                pos += 8
            elif kind == "i16":
                value = str(struct.unpack_from("<h", data, pos)[0])
                pos += 2
            elif kind == "i32":
                value = str(struct.unpack_from("<i", data, pos)[0])
                pos += 4
            elif kind == "i64":
                value = str(struct.unpack_from("<q", data, pos)[0])
                pos += 8
            elif kind == "bool":
                value = str(data[pos])
                pos += 1
            else:
                length = data[pos]
                value = data[pos + 1:pos + 1 + length].hex().upper()
                pos += 1 + length
        except (ValueError, struct.error, IndexError):
            raise DxfError("a binary file ends inside a value (group %d)" % (len(groups) + 1))
        if code == 0:
            record = value
        groups.append((code, value))
    return groups


def read_groups(data):
    if data.startswith(SENTINEL):
        return read_binary(data), "binary"
    return read_ascii(data), "ascii"


# ---------------------------------------------------------------------------
# One file
# ---------------------------------------------------------------------------

def tokens_of(record):
    """The group order of a record: from its first subclass to its XDATA, a subclass as S:name, the head
    (before the first 100) and the XDATA (from the first 1001 of the tail) left out. A group of 1010 .. 1071
    before the first 1001 is class data and stays."""
    groups = record
    start = 1
    while start < len(groups) and groups[start][0] != 100:
        start += 1
    tail = len(groups)
    while tail > start and groups[tail - 1][0] >= 1000:
        tail -= 1
    while tail < len(groups) and groups[tail][0] != 1001:
        tail += 1
    out = []
    i = start
    while i < tail:
        code, value = groups[i]
        if code == 102 and value.startswith("{"):
            # an application's 102 block inside the body: its content is not the record's own order
            j = i + 1
            while j < tail and not (groups[j][0] == 102 and not groups[j][1].startswith("{")):
                j += 1
            i = j + 1
            continue
        out.append("S:" + value.strip() if code == 100 else str(code))
        i += 1
    return out


def compress(tokens):
    """Runs of one group and repeated groups as (…)^n, longest cover first (the style of the old plan's appendix B)."""
    out = []
    i = 0
    n = len(tokens)
    while i < n:
        best = (1, 1)           # (period, repeats) covering the most tokens
        for period in range(1, min(80, (n - i) // 2) + 1):
            repeats = 1
            while tokens[i + repeats * period:i + (repeats + 1) * period] == tokens[i:i + period] and i + (repeats + 1) * period <= n:
                repeats += 1
            if repeats >= 2 and period * repeats > best[0] * best[1]:
                best = (period, repeats)
        period, repeats = best
        if repeats >= 2:
            inner = compress(tokens[i:i + period])
            out.append("(%s)^%d" % (" ".join(inner), repeats))
            i += period * repeats
        else:
            out.append(tokens[i])
            i += 1
    return out


def read_file(path, relative):
    """The inventory of one file: header facts, the records counted in BLOCKS/ENTITIES/OBJECTS, the CLASSES."""
    with open(path, "rb") as f:
        data = f.read()
    sha = hashlib.sha256(data).hexdigest()
    groups, form = read_groups(data)
    info = {"path": relative, "sha256": sha, "size": len(data), "format": form,
            "acadver": None, "maintver": None, "records": [], "classes": []}
    kind, reason = provenance(relative)
    info["producer"] = kind
    info["producer_reason"] = reason

    section = None
    i = 0
    n = len(groups)
    header_name = None
    sections_seen = 0
    while i < n:
        code, value = groups[i]
        if code == 0 and value == "SECTION" and i + 1 < n and groups[i + 1][0] == 2:
            section = groups[i + 1][1].strip()
            sections_seen += 1
            i += 2
            continue
        if code == 0 and value == "ENDSEC":
            section = None
            i += 1
            continue
        if section == "HEADER" and code == 9:
            header_name = value.strip()
        elif section == "HEADER" and header_name == "$ACADVER" and code == 1:
            info["acadver"] = value.strip()
            header_name = None
        elif section == "HEADER" and header_name == "$ACADMAINTVER" and code == 70:
            info["maintver"] = value.strip()
            header_name = None
        if code == 0 and section in COUNTED_SECTIONS:
            j = i + 1
            while j < n and groups[j][0] != 0:
                j += 1
            record = groups[i:j]
            name = value.strip()
            if name != "ENDSEC":
                handle = ""
                for c, v in record[1:]:
                    if c in (5, 105):
                        handle = v.strip()
                        break
                    if c == 100:
                        break
                info["records"].append({"name": name, "section": section, "handle": handle, "tokens": tokens_of(record)})
            i = j
            continue
        if code == 0 and section == "CLASSES" and value == "CLASS":
            j = i + 1
            while j < n and groups[j][0] != 0:
                j += 1
            fields = {c: v for c, v in groups[i + 1:j]}
            info["classes"].append({"dxf_name": fields.get(1, "").strip(), "cpp_name": fields.get(2, "").strip(),
                                    "application": fields.get(3, "").strip()})
            i = j
            continue
        i += 1
    if sections_seen == 0:
        raise DxfError("no SECTION: not a DXF file")
    return info


# ---------------------------------------------------------------------------
# The corpus
# ---------------------------------------------------------------------------

def default_roots():
    return [(os.path.join(ROOT, "build", "test-data"), ""),
            (os.path.join(ROOT, "doc", "facit"), "doc/facit/"),
            (os.path.join(ROOT, "doc"), "doc/")]


def collect(roots):
    files = []
    for root, prefix in roots:
        if not os.path.isdir(root):
            continue
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames.sort()
            if prefix == "doc/":
                dirnames[:] = []                 # doc/acad_*_dxf.dxf only; doc/facit is a root of its own
            for name in sorted(filenames):
                if not name.lower().endswith(".dxf"):
                    continue
                if prefix == "doc/" and not name.startswith("acad_"):
                    continue
                full = os.path.join(dirpath, name)
                relative = prefix + os.path.relpath(full, root).replace("\\", "/")
                files.append((full, relative))
    return files


def twin_of(relative):
    name = os.path.basename(relative)
    d = os.path.dirname(relative)

    def swap(a, b):
        if a in name:
            return (d + "/" if d else "") + name.replace(a, b)
        return None
    if name.endswith("_bin.dxf"):
        return swap("_bin.dxf", ".dxf")
    if "_binary.dxf" in name:
        return swap("_binary.dxf", "_ascii.dxf")
    return None


def inventory(roots):
    files = collect(roots)
    seen = {}
    unique = []
    errors = []
    duplicates = []
    for full, relative in files:
        try:
            info = read_file(full, relative)
        except (DxfError, UnicodeDecodeError, OSError) as e:
            errors.append({"path": relative, "error": str(e)})
            continue
        if info["sha256"] in seen:
            duplicates.append({"path": relative, "same_as": seen[info["sha256"]]})
            continue
        seen[info["sha256"]] = relative
        unique.append(info)

    by_path = {u["path"]: u for u in unique}
    # a path whose content another path has is that content (its ASCII or binary twin can be found by either)
    by_sha = {u["sha256"]: u for u in unique}
    for d in duplicates:
        by_path[d["path"]] = by_sha[next(u["sha256"] for u in unique if u["path"] == d["same_as"])]
    # a binary file with an ASCII twin is a twin: counted once, and its types must be the same set
    twins = []
    counted = []
    for u in unique:
        t = twin_of(u["path"]) if u["format"] == "binary" else None
        if t and t in by_path and by_path[t]["format"] == "ascii":
            a = {r["name"] for r in by_path[t]["records"]}
            b = {r["name"] for r in u["records"]}
            twins.append({"binary": u["path"], "ascii": t, "same_types": a == b,
                          "only_ascii": sorted(a - b), "only_binary": sorted(b - a)})
            continue
        counted.append(u)

    types = {}
    for u in counted:
        for r in u["records"]:
            e = types.setdefault(r["name"], {"sections": set(), "records": 0, "files": set(), "versions": defaultdict(int),
                                             "autocad_records": 0, "autocad_files": set(), "forms": {},
                                             "autocad_versions": defaultdict(int), "autocad_sample": None})
            e["sections"].add(r["section"])
            e["records"] += 1
            e["files"].add(u["path"])
            e["versions"][u["acadver"] or "?"] += 1
            if autocad(u["producer"]):
                e["autocad_records"] += 1
                e["autocad_files"].add(u["path"])
                e["autocad_versions"][u["acadver"] or "?"] += 1
                if e["autocad_sample"] is None:
                    e["autocad_sample"] = {"path": u["path"], "handle": r["handle"], "acadver": u["acadver"],
                                           "sha256": u["sha256"]}
            if r["name"] in STRUCTURAL:
                continue
            seq = " ".join(compress(r["tokens"]))
            form = e["forms"].setdefault(seq, {"sequence": seq, "versions": defaultdict(int), "sample": u["path"],
                                               "handle": r["handle"], "sha256": u["sha256"],
                                               "autocad": autocad(u["producer"])})
            form["versions"][u["acadver"] or "?"] += 1
            # prefer an AutoCAD file as the sample
            if autocad(u["producer"]) and not form["autocad"]:
                form.update({"sample": u["path"], "handle": r["handle"], "sha256": u["sha256"], "autocad": True})
    classes = {}
    for u in counted:
        for c in u["classes"]:
            e = classes.setdefault(c["dxf_name"], {"cpp_name": c["cpp_name"], "application": c["application"],
                                                   "files": 0, "versions": defaultdict(int)})
            e["files"] += 1
            e["versions"][u["acadver"] or "?"] += 1
    producers = defaultdict(int)
    for u in counted:
        producers[u["producer"]] += 1
    return {"files": len(files), "identical_content": duplicates, "unique": len(unique), "counted": len(counted),
            "twins": twins, "errors": errors, "types": types, "classes": classes, "producers": dict(producers),
            "provenance": [{"path": u["path"], "producer": u["producer"], "reason": u["producer_reason"],
                            "acadver": u["acadver"], "maintver": u["maintver"], "sha256": u["sha256"]} for u in unique]}


def to_json(result, with_forms):
    out = dict(result)
    types = {}
    for name, e in sorted(result["types"].items()):
        t = {"sections": sorted(e["sections"]), "records": e["records"], "files": len(e["files"]),
             "autocad_records": e["autocad_records"], "autocad_files": len(e["autocad_files"]),
             "versions": dict(sorted(e["versions"].items())),
             "autocad_versions": dict(sorted(e["autocad_versions"].items())),
             "autocad_sample": e["autocad_sample"]}
        if with_forms:
            t["forms"] = [{"sequence": f["sequence"], "versions": dict(sorted(f["versions"].items())), "sample": f["sample"],
                           "handle": f["handle"], "sha256": f["sha256"], "autocad": f["autocad"]}
                          for f in e["forms"].values()]
        else:
            t["forms"] = len(e["forms"])
        types[name] = t
    out["types"] = types
    out["classes"] = {k: {"cpp_name": v["cpp_name"], "application": v["application"], "files": v["files"],
                          "versions": dict(sorted(v["versions"].items()))} for k, v in sorted(result["classes"].items())}
    return out


def registry_names():
    names = set()
    path = os.path.join(ROOT, "src", "typed_registry_data.inc")
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = re.match(r'\{"([^"]+)",', line)
            if m:
                names.add(m.group(1))
    return names


# ---------------------------------------------------------------------------
# Self-test
# ---------------------------------------------------------------------------

def ascii_dxf(version, body, crlf=False):
    lines = ["0", "SECTION", "2", "HEADER", "9", "$ACADVER", "1", version, "9", "$ACADMAINTVER", "70", "25", "0", "ENDSEC",
             "0", "SECTION", "2", "OBJECTS"] + body + ["0", "ENDSEC", "0", "EOF"]
    return (("\r\n" if crlf else "\n").join(lines) + ("\r\n" if crlf else "\n")).encode("latin-1")


def binary_dxf(version, records, r12=False):
    def code(c):
        if r12:
            return bytes([c]) if c < 255 else b"\xff" + struct.pack("<h", c)
        return struct.pack("<H", c)

    def string(c, s):
        return code(c) + s.encode("latin-1") + b"\x00"

    out = bytearray(SENTINEL)
    out += string(0, "SECTION") + string(2, "HEADER") + string(9, "$ACADVER") + string(1, version)
    out += string(0, "ENDSEC") + string(0, "SECTION") + string(2, "OBJECTS")
    for r in records:
        for c, v in r:
            t = value_type(c, "")
            if t == "str":
                out += string(c, v)
            elif t == "i16":
                out += code(c) + struct.pack("<h", int(v))
            elif t == "i32":
                out += code(c) + struct.pack("<i", int(v))
            elif t == "f64":
                out += code(c) + struct.pack("<d", float(v))
            elif t == "bool":
                out += code(c) + bytes([int(v)])
            elif t == "bin":
                raw = bytes.fromhex(v)
                out += code(c) + bytes([len(raw)]) + raw
    out += string(0, "ENDSEC") + string(0, "EOF")
    return bytes(out)


def self_test():
    checks = 0

    def check(condition, message):
        nonlocal checks
        checks += 1
        if not condition:
            raise SystemExit("self-test failed: " + message)

    record = [(0, "FANCYOBJ"), (5, "2A"), (102, "{ACAD_REACTORS"), (330, "1F"), (102, "}"), (330, "1F"),
              (100, "AcDbFancy"), (90, "7"), (91, "1"), (91, "2"), (91, "3"), (91, "4"),
              (100, "AcDbFancy"), (310, "0A0B"), (1010, "1.5"), (1020, "2.5"), (1030, "3.5"), (1071, "9"),
              (1001, "ACAD"), (1000, "x"), (1070, "5")]
    check(tokens_of(record) == ["S:AcDbFancy", "90", "91", "91", "91", "91", "S:AcDbFancy", "310", "1010", "1020", "1030", "1071"],
          "the order of a record: head and XDATA left out, class data kept")
    check(" ".join(compress(tokens_of(record))) == "S:AcDbFancy 90 (91)^4 S:AcDbFancy 310 1010 1020 1030 1071", "run-length form")
    check(" ".join(compress(["91", "93", "91", "93", "91", "93", "5"])) == "(91 93)^3 5", "a repeated pair of groups")
    check(compress(["1", "2", "3"]) == ["1", "2", "3"], "nothing repeats")

    with tempfile.TemporaryDirectory() as tmp:
        sub = os.path.join(tmp, "acadsharp")
        os.makedirs(sub)
        os.makedirs(os.path.join(tmp, "ezdxf"))
        body = []
        for c, v in record:
            body += [str(c), v]
        body += ["0", "PLAIN", "5", "2B", "100", "AcDbPlain", "70", "1"]
        # the same drawing as ASCII (LF), as ASCII with CRLF (the same content under another line ending: another
        # content), as binary (a twin), and an identical copy of the first
        files = {"acadsharp/sample_AC1021_ascii.dxf": ascii_dxf("AC1021", body),
                 "acadsharp/copy_of_it.dxf": ascii_dxf("AC1021", body),
                 "acadsharp/other_crlf.dxf": ascii_dxf("AC1021", body, crlf=True)}
        binary_records = [[(c, v) for c, v in record], [(0, "PLAIN"), (5, "2B"), (100, "AcDbPlain"), (70, "1")]]
        files["acadsharp/sample_AC1021_binary.dxf"] = binary_dxf("AC1021", binary_records)
        files["acadsharp/r12_style_binary.dxf"] = binary_dxf("AC1009", [[(0, "POINT"), (5, "F"), (8, "0")]], r12=True)
        files["acadsharp/invalid_template.dxf"] = b"  0\nSECTION\n  2\nHEADER\n  9\n$LASTSAVEDBY\n@@LASTSAVEDBY@@\nuser\n  0\nENDSEC\n"
        files["ezdxf/not_autocad.dxf"] = ascii_dxf("AC1015", ["0", "PLAIN", "5", "9", "100", "AcDbPlain", "70", "2"])
        for rel, data in files.items():
            with open(os.path.join(tmp, rel), "wb") as f:
                f.write(data)
        result = inventory([(tmp, "")])
        check(result["files"] == 7, "seven files found: %d" % result["files"])
        check(len(result["errors"]) == 1 and "invalid_template" in result["errors"][0]["path"], "the invalid template is an error")
        check("@@LASTSAVEDBY@@" in result["errors"][0]["error"], "the error names the fault")
        check("@@LASTSAVEDBY@@" not in result["types"], "and it is no type")
        check(len(result["identical_content"]) == 1, "an identical copy is one content")
        check(len(result["twins"]) == 1 and result["twins"][0]["same_types"], "the binary file has the types of its ASCII twin")
        check(result["unique"] == 5 and result["counted"] == 4, "the twin is not counted again: %d %d" % (result["unique"], result["counted"]))
        fancy = result["types"]["FANCYOBJ"]
        check(fancy["records"] == 2 and len(fancy["files"]) == 2, "FANCYOBJ in the LF file and the CRLF file (another content)")
        check(fancy["autocad_records"] == 2, "from an AutoCAD file")
        check(list(fancy["forms"])[0] == "S:AcDbFancy 90 (91)^4 S:AcDbFancy 310 1010 1020 1030 1071", "its form")
        plain = result["types"]["PLAIN"]
        check(plain["records"] == 3 and plain["autocad_records"] == 2, "PLAIN: two AutoCAD files and another program's")
        check(result["types"]["POINT"]["sections"] == {"OBJECTS"}, "the R12-style binary file is read with one-byte codes")
        check(result["producers"].get("autocad") == 3 and result["producers"].get("other") == 1, "producers: %r" % result["producers"])
        check(set(result["types"]["PLAIN"]["versions"]) == {"AC1021", "AC1015"}, "versions per name")
        js = to_json(result, True)
        json.dumps(js)
        check(js["types"]["FANCYOBJ"]["forms"][0]["sample"].startswith("acadsharp/"), "the JSON form's sample")
    check(provenance("doc/facit/acad_2018_dxf_x.dxf")[0] == "autocad-facit", "facit provenance")
    check(provenance("doc/facit/web/acad_AC1032_dxf_web_x.dxf")[0] == "web-unproven" and not autocad("web-unproven"),
          "a file from the web is not AutoCAD's until proven")
    check(provenance("ezdxf/mtext_text_frame.dxf")[0] == "autocad" and provenance("ezdxf/small_r13.dxf")[0] == "other", "ezdxf provenance")
    print("inventory_types self-test: %d checks passed" % checks)
    return 0


# ---------------------------------------------------------------------------

def main(argv):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--root", action="append", default=[], help="a directory to scan (default: the project's corpus)")
    p.add_argument("--json", help="write the inventory as JSON to this file")
    p.add_argument("--forms", action="store_true", help="include the observed group orders in the JSON")
    p.add_argument("--check-registry", action="store_true", help="every name found must be in src/typed_registry_data.inc")
    p.add_argument("--self-test", action="store_true")
    args = p.parse_args(argv)
    if args.self_test:
        return self_test()
    roots = [(r, "") for r in args.root] if args.root else default_roots()
    result = inventory(roots)
    print("%d files, %d with the content of another, %d read, %d binary twins, %d counted; %d record names; %d errors" % (
        result["files"], len(result["identical_content"]), result["unique"], len(result["twins"]), result["counted"],
        len(result["types"]), len(result["errors"])))
    print("producers: " + ", ".join("%s %d" % kv for kv in sorted(result["producers"].items())))
    for e in result["errors"]:
        print("  not readable: %s: %s" % (e["path"], e["error"]))
    for t in result["twins"]:
        if not t["same_types"]:
            print("  twin differs: %s: only ASCII %s, only binary %s" % (t["binary"], t["only_ascii"], t["only_binary"]))
    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(to_json(result, args.forms), f, indent=1, sort_keys=True)
    if args.check_registry:
        known = registry_names()
        missing = sorted(n for n in result["types"] if n not in known)
        for n in missing:
            print("  not in the register: " + n)
        return 1 if missing else 0
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
