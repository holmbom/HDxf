"""The batch of step 7.24 (plan phase 7, DXF R13 and R14): ODA File Converter and AutoCAD's core read every R13/R14
original and everything HDxf wrote for them, and one summary says what each tool made of each file.

  python -I tools/older_dxf_batch.py --work <directory outside the repository> [options]

What it does (nothing is judged by exit code alone: the logs, the .err files, the counts and the extents are
collected per file and written to <work>/summary.txt and <work>/summary.tsv):

  1. Copies every R13/R14 original (AC1012/AC1014 in the header; --originals, default build/test-data, which holds
     the pinned files and build/test-data/local-r13r14) to <work>/in/original/. The originals are never touched
     (their SHA256 is compared before and after).
  2. HDxf's output for them and the new typed drawings of the tests: with --run-tests <hdxf_tests.exe> the test
     suite is run with HDXF_EXPORT_DIR set (test/test_coverage_r13_r14.cpp writes the cases of every row of the
     target matrix, each original written again in both formats unchanged, with a change, with a removal and
     written anew from its typed values); with --exports <directory> files already written are used. They are
     copied to <work>/in/hdxf/ (the name says what each is: case__, roundtrip__, changed__, removed__, forced__).
  3. ODA File Converter (ODAFileConverter <in> <out> ACAD13|ACAD14 DXF|DWG 0 1 "*.dxf", the file's own version as
     target) reads each input group; its outputs and .err files are counted.
  4. AutoCAD's core (DWG TrueView's accoreconsole.exe /i <file> /s <script> /l en-US) opens every file: regen, ZOOM E,
     the extents, the number of objects SELECT ALL finds, MASSPROP/LIST for a file with 3DSOLID/REGION/BODY, and
     -WBLOCK * to a DWG, then QUIT. One log per file. It reads originals and HDxf's files alike.
  5. ezdxf (when installed) reads every file as an independent reader: version loaded, audit, entities, tables, objects.
  6. The regression run (--other-versions-tool): the originals of R12 and 2000-2018 in the same roots are rewritten by HDxf
     (the tool's roundtrip) and read by the same tools: phase 7 must not change them.
  7. Original against HDxf's output of it (compare.tsv): ezdxf's entities, tables and objects, the core's LIST text per entity
     and MASSPROP of each 3DSOLID/REGION, the extents of the core's DWG (model and paper), ODA's entity counts.
  8. The checks of the batch itself, as FINDING lines: an expected original or HDxf file that is missing (and why), a skipped
     test, a file ODA made no output of, an ODA .err, a font/SHX/image no tool can find (resources are copied next to each file).
  The summary (summary.txt/.tsv): per file its real $ACADVER, its entity counts, the extents of the header and of the core,
  what ODA, the core and ezdxf made of it, and the totals.

The tools are readers here, never facit: the files AutoCAD itself saved are (CLAUDE.md, "No golden files"). ODA's
output is ODA's own serialisation of what it read; the core's DWG is AutoCAD's. Python's standard library only (ezdxf is optional).

A smoke run of one file (one ODA run, one core run):
  python -I tools/older_dxf_batch.py --work C:/temp/hdxf_batch --only RM2_leg --oda-types DXF
"""
import argparse
import collections
import glob
import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time

ODA_GLOB = r"C:\Program Files\ODA\ODAFileConverter *\ODAFileConverter.exe"
ODA_WANTED = "27.9.1"
CORE = r"C:\Program Files\Autodesk\DWG TrueView 2027 - English\accoreconsole.exe"
TARGET_OF = {"AC1012": "ACAD13", "AC1014": "ACAD14"}
VERSIONS = tuple(TARGET_OF)
# The other versions HDxf reads and writes (the regression run, --other-versions-tool): phase 7 must not change them
OTHER_TARGET_OF = {"AC1009": "ACAD12", "AC1015": "ACAD2000", "AC1018": "ACAD2004", "AC1021": "ACAD2007",
                   "AC1024": "ACAD2010", "AC1027": "ACAD2013", "AC1032": "ACAD2018"}
ALL_TARGET_OF = dict(TARGET_OF, **OTHER_TARGET_OF)
GROUPS = ("original", "hdxf", "other_original", "other_hdxf")
# The cases test/test_coverage_r13_r14.cpp writes (HDXF_EXPORT_DIR): every one is expected, in both formats
EXPECTED_CASES = {"R13": ["acis", "blocks", "classes", "dimensions", "dimstyle", "empty", "head", "objects", "paper",
                          "r12_types", "r13_family", "tables", "text"],
                  "R14": ["acis", "blocks", "classes", "dimensions", "dimstyle", "empty", "head", "objects", "paper",
                          "r12_types", "r13_family", "tables", "text", "hatch", "image", "lwpolyline"]}
EXPECTED_KINDS = ("roundtrip", "changed", "removed", "forced")
RESOURCE_EXTENSIONS = (".shx", ".ttf", ".ttc", ".otf", ".pfb", ".pfm", ".lin", ".pat", ".png", ".jpg", ".jpeg", ".tif", ".tiff",
                       ".bmp", ".gif", ".pdf", ".dwg", ".ctb", ".stb", ".mln")
NO_WINDOW = 0x08000000


# ---------------------------------------------------------------------------
# Reading a DXF (ASCII or binary, R13/R14: two-byte codes, R12-style one-byte codes also read)
# ---------------------------------------------------------------------------

def value_kind(code):
    if 290 <= code <= 299:
        return "b"
    if 310 <= code <= 319 or code == 1004:
        return "x"
    if 10 <= code <= 59 or 110 <= code <= 149 or 210 <= code <= 239 or 460 <= code <= 469 or 1010 <= code <= 1059:
        return "d"
    if 160 <= code <= 169:
        return "q"
    if 90 <= code <= 99 or 420 <= code <= 429 or 440 <= code <= 459 or code == 1071:
        return "i"
    if 60 <= code <= 79 or 170 <= code <= 179 or 270 <= code <= 289 or 370 <= code <= 389 or 400 <= code <= 409 or 1060 <= code <= 1070:
        return "h"
    return "s"


def groups_of(data):
    """Every (code, value) of a DXF's bytes; binary data chunks as their hex text."""
    if data.startswith(b"AutoCAD Binary DXF"):
        pos = 22
        width = 1 if data[pos] == 0 and data[pos + 1:pos + 8] == b"SECTION" else 2
        size = len(data)
        while pos < size:
            code = data[pos]
            pos += 1
            if width == 2:
                code |= data[pos] << 8
                pos += 1
            elif code == 255:
                code = data[pos] | (data[pos + 1] << 8)
                pos += 2
            kind = value_kind(code)
            if kind == "s":
                end = data.index(b"\0", pos)
                value = data[pos:end].decode("latin-1")
                pos = end + 1
            elif kind == "d":
                value = repr(struct.unpack_from("<d", data, pos)[0])
                pos += 8
            elif kind == "h":
                value = str(struct.unpack_from("<h", data, pos)[0])
                pos += 2
            elif kind == "i":
                value = str(struct.unpack_from("<i", data, pos)[0])
                pos += 4
            elif kind == "q":
                value = str(struct.unpack_from("<q", data, pos)[0])
                pos += 8
            elif kind == "b":
                value = str(data[pos])
                pos += 1
            else:
                length = data[pos]
                value = data[pos + 1:pos + 1 + length].hex()
                pos += 1 + length
            yield code, value
            if code == 0 and value == "EOF":
                return
        return
    lines = data.decode("latin-1").replace("\r\n", "\n").split("\n")
    for i in range(0, len(lines) - 1, 2):
        try:
            code = int(lines[i].strip())
        except ValueError:
            return
        yield code, lines[i + 1].strip() if code != 1 else lines[i + 1]


def summarize(path):
    """The real $ACADVER, the entity counts, the extents of the header and what the file has (3DSOLID ...)."""
    out = {"acadver": "", "entities": collections.Counter(), "model": 0, "paper": 0, "extmin": "", "extmax": "", "pextmin": "",
           "pextmax": "", "solids": [], "error": ""}
    try:
        data = open(path, "rb").read()
        section = None
        last9 = None
        coords = {}
        entity = None
        previous = (None, None)
        for code, value in groups_of(data):
            if code == 2 and previous == (0, "SECTION"):
                section = value
            if section == "HEADER":
                if code == 9:
                    last9 = value
                elif last9 == "$ACADVER" and code == 1:
                    out["acadver"] = value
                    last9 = None
                elif last9 in ("$EXTMIN", "$EXTMAX", "$PEXTMIN", "$PEXTMAX") and code in (10, 20, 30):
                    coords.setdefault(last9, []).append(value)
            if section in ("ENTITIES", "BLOCKS") and code == 5 and entity and entity[0] in ("3DSOLID", "REGION") and len(entity) == 2:
                out["solids"].append(value)
                entity.append(value)
            if section in ("ENTITIES", "BLOCKS") and code == 0:
                entity = [value, False]
                if value not in ("VERTEX", "SEQEND", "ATTRIB", "BLOCK", "ENDBLK", "SECTION", "ENDSEC"):
                    out["entities"][value] += 1
                    if section == "ENTITIES":
                        out["model"] += 1
            if section == "ENTITIES" and code == 67 and value.strip() == "1" and entity and not entity[1]:
                entity[1] = True
                if entity[0] not in ("VERTEX", "SEQEND", "ATTRIB"):
                    out["model"] -= 1
                    out["paper"] += 1
            previous = (code, value)
        out["extmin"] = ",".join(coords.get("$EXTMIN", []))
        out["extmax"] = ",".join(coords.get("$EXTMAX", []))
        out["pextmin"] = ",".join(coords.get("$PEXTMIN", []))
        out["pextmax"] = ",".join(coords.get("$PEXTMAX", []))
    except Exception as e:  # a file that cannot be read is a finding, not a crash
        out["error"] = "%s: %s" % (type(e).__name__, e)
    return out


def acadver_of_dwg(path):
    with open(path, "rb") as f:
        return f.read(6).decode("latin-1")


# ---------------------------------------------------------------------------
# Running the tools
# ---------------------------------------------------------------------------

def hidden():
    """Start options: no console window, no visible window of the tool."""
    if os.name != "nt":
        return {}
    info = subprocess.STARTUPINFO()
    info.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    info.wShowWindow = 0
    return {"startupinfo": info, "creationflags": NO_WINDOW}


def run(command, timeout, cwd=None, env=None):
    """(return code or None on a timeout, output text, seconds)."""
    started = time.time()
    try:
        done = subprocess.run(command, capture_output=True, timeout=timeout, cwd=cwd, env=env, **hidden())
    except subprocess.TimeoutExpired as e:
        text = ((e.stdout or b"") + (e.stderr or b"")).decode("utf-8", "replace").replace("\0", "")
        return None, text, time.time() - started
    text = (done.stdout + done.stderr).decode("utf-8", "replace").replace("\0", "")
    return done.returncode, text, time.time() - started


def find_oda(explicit):
    if explicit:
        return explicit, os.path.basename(os.path.dirname(explicit))
    found = sorted(glob.glob(ODA_GLOB))
    if not found:
        return None, ""
    for path in found:
        if ODA_WANTED in path:
            return path, os.path.basename(os.path.dirname(path))
    return found[-1], os.path.basename(os.path.dirname(found[-1]))


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def is_older_dxf(path):
    try:
        with open(path, "rb") as f:
            head = f.read(8192)
    except OSError:
        return ""
    for version in VERSIONS:
        if version.encode() in head:
            return version
    return ""


def safe_name(text):
    return re.sub(r"[^A-Za-z0-9._-]", "_", text)


# ---------------------------------------------------------------------------
# The files the batch is to find (a missing one is a finding, never a quiet skip)
# ---------------------------------------------------------------------------

# pinned R13/R14 files of test/data/manifest.cmake (the others are build/test-data/local-r13r14/PROVENANCE.txt)
EXPECTED_PINNED = ["small_r13.dxf", "small_r14.dxf", "bin_dxf_r13.dxf", "bin_dxf_r14.dxf", "r13_v.dxf", "dxfprueba.dxf",
                   "HEAD.DXF", "simp.dxf", "pyramid-base.dxf"]


def expected_originals(roots):
    names = set(EXPECTED_PINNED)
    for root in roots:
        provenance = os.path.join(root, "local-r13r14", "PROVENANCE.txt")
        if os.path.exists(provenance):
            with open(provenance, encoding="utf-8") as f:
                for line in f:
                    if line.strip() and not line.startswith("#"):
                        names.add(line.split("|")[0].strip())
    return names


def expected_hdxf_files(original_names, no_entities):
    """Every file the test suite is to write: the cases and, per original and format, the four kinds."""
    names = set()
    for version, cases in EXPECTED_CASES.items():
        for case in cases:
            for fmt in ("ascii", "binary"):
                names.add("case__%s_%s__%s.dxf" % (case, version, fmt))
    for original in original_names:
        for kind in EXPECTED_KINDS:
            for fmt in ("ascii", "binary"):
                names.add("%s__%s__%s.dxf" % (kind, original, fmt))
    return names


# ---------------------------------------------------------------------------
# Resources: fonts, SHX, images (what each file refers to must be where the tools look)
# ---------------------------------------------------------------------------

def collect_resources(roots, repo, extra):
    found = {}
    for root in list(roots) + [os.path.join(repo, "test", "data", "synthetic")] + list(extra or []):
        for folder, _, names in os.walk(root):
            for name in names:
                if name.lower().endswith(RESOURCE_EXTENSIONS):
                    found.setdefault(name.lower(), os.path.join(folder, name))
    return found


def references_of(path):
    """The fonts (STYLE 3/4), images (IMAGEDEF 1) and external references (BLOCK 1) a file names."""
    refs = {"font": set(), "image": set(), "xref": set()}
    try:
        record = None
        for code, value in groups_of(open(path, "rb").read()):
            if code == 0:
                record = value
            elif record == "STYLE" and code in (3, 4) and value.strip():
                refs["font"].add(value.strip())
            elif record == "IMAGEDEF" and code == 1 and value.strip():
                refs["image"].add(value.strip())
            elif record == "BLOCK" and code == 1 and value.strip():
                refs["xref"].add(value.strip())
    except Exception:
        pass
    return refs


def font_dirs(core):
    dirs = [os.path.join(os.environ.get("WINDIR", r"C:\Windows"), "Fonts")]
    base = os.path.dirname(core) if core else ""
    for sub in ("Fonts", "Support", os.path.join("Support", "Fonts")):
        dirs.append(os.path.join(base, sub))
    listing = set()
    for d in dirs:
        if os.path.isdir(d):
            listing.update(n.lower() for n in os.listdir(d))
    return listing


def unresolved(refs, input_dir, resources, fonts):
    """The references no tool can resolve: not next to the file, not copied from the roots, not a system or AutoCAD font."""
    missing = []
    for kind, names in refs.items():
        for name in sorted(names):
            base = os.path.basename(name.replace("\\", "/")).lower()
            candidates = [base] if os.path.splitext(base)[1] else [base + ".shx", base + ".ttf"]
            if kind == "font":
                ok = any(c in fonts or c in resources or os.path.exists(os.path.join(input_dir, c)) for c in candidates)
            else:
                ok = any(c in resources or os.path.exists(os.path.join(input_dir, c)) for c in candidates)
            if not ok:
                missing.append("%s:%s" % (kind, name))
    return missing


# ---------------------------------------------------------------------------
# ezdxf: an independent reader (its dump is what original and HDxf output are compared by)
# ---------------------------------------------------------------------------

def ez_norm(value):
    if isinstance(value, float):
        return round(value, 6) + 0.0
    if isinstance(value, (list, tuple)):
        return tuple(ez_norm(v) for v in value)
    if hasattr(value, "xyz"):
        return tuple(round(c, 6) + 0.0 for c in value.xyz)
    return str(value)


def ez_attribs(entity):
    attribs = {}
    for key, value in entity.dxf.all_existing_dxf_attribs().items():
        if key not in ("handle", "owner", "reactors"):
            attribs[key] = ez_norm(value)
    return tuple(sorted(attribs.items()))


def ezdxf_read(path):
    """What ezdxf makes of a file: the version it loaded, the audit, a dump of entities, tables and objects."""
    result = {"available": False}
    try:
        import ezdxf
        from ezdxf import recover
    except ImportError:
        return result
    result["available"] = True
    result.update({"ok": False, "mode": "", "message": "", "loaded": "", "version": "", "entities": 0, "audit_errors": 0,
                   "audit_fixes": 0, "audit_text": [], "dump": [], "tables": {}, "objects": collections.Counter(), "extents": ""})
    try:
        try:
            doc = ezdxf.readfile(path)
            result["mode"] = "read"
        except Exception as e:  # a structure ezdxf does not take: its recovery mode, and the first message
            result["message"] = "%s: %s" % (type(e).__name__, e)
            doc, _ = recover.readfile(path)
            result["mode"] = "recover"
        result["loaded"] = getattr(doc, "loaded_dxfversion", "") or ""
        result["version"] = doc.dxfversion
        auditor = doc.audit()
        result["audit_errors"] = len(auditor.errors)
        result["audit_fixes"] = len(auditor.fixes)
        result["audit_text"] = [str(m.message) for m in list(auditor.errors)[:5]] + [str(m.message) for m in list(auditor.fixes)[:5]]
        dump = []
        for layout in doc.layouts:
            space = layout.name
            for entity in layout:
                dump.append((space, entity.dxftype(), ez_attribs(entity), entity.dxf.handle, ez_children(entity)))
        for block in doc.blocks:
            if block.is_any_layout:
                continue
            for entity in block:
                dump.append(("block:" + block.name, entity.dxftype(), ez_attribs(entity), entity.dxf.handle, ez_children(entity)))
        result["dump"] = dump
        result["entities"] = len(dump)
        tables = {}
        for name in ("layers", "linetypes", "styles", "dimstyles", "appids", "views", "ucs", "viewports"):
            table = getattr(doc, name)
            tables[name] = sorted((e.dxf.name if e.dxf.hasattr("name") else "?", ez_attribs(e)) for e in table)
        tables["blocks"] = sorted((b.name, len(list(b))) for b in doc.blocks)
        result["tables"] = tables
        for obj in doc.objects:
            result["objects"][obj.dxftype()] += 1
        try:
            result["extents"] = "%s %s" % (ez_norm(doc.header.get("$EXTMIN")), ez_norm(doc.header.get("$EXTMAX")))
        except Exception:
            pass
        result["ok"] = True
    except Exception as e:
        result["message"] = ("%s: %s" % (type(e).__name__, e)) if not result["message"] else result["message"] + " | " + \
            "%s: %s" % (type(e).__name__, e)
    return result


def ez_children(entity):
    """The vertices of a POLYLINE and the ATTRIBs of an INSERT, as part of the entity."""
    try:
        kind = entity.dxftype()
        if kind == "POLYLINE":
            return tuple(ez_attribs(v) for v in entity.vertices)
        if kind == "INSERT":
            return tuple(ez_attribs(a) for a in entity.attribs)
    except Exception:
        pass
    return ()


def multiset_diff(left, right):
    """(only in left, only in right) as lists of short descriptions."""
    a, b = collections.Counter(left), collections.Counter(right)
    return list((a - b).elements()), list((b - a).elements())


def describe(item):
    return "%s/%s%s" % (item[0], item[1], (" " + str(dict(item[2]).get("layer", ""))) if len(item) > 2 else "")


def compare_ezdxf(original, output, kind):
    """The differences between ezdxf's reading of the original and of HDxf's output of it."""
    if not original.get("ok") or not output.get("ok"):
        return ["ezdxf: original %s, output %s" % ("ok" if original.get("ok") else "FAILED", "ok" if output.get("ok") else "FAILED")]
    diffs = []
    by_handle = kind in ("roundtrip",)
    key = (lambda e: (e[0], e[1], e[2], e[3], e[4])) if by_handle else (lambda e: (e[0], e[1], e[2], e[4]))
    gone, new = multiset_diff([key(e) for e in original["dump"]], [key(e) for e in output["dump"]])
    if gone or new:
        diffs.append("entities only in original %d [%s], only in output %d [%s]" % (
            len(gone), ", ".join(sorted(set(describe(e) for e in gone))[:4]), len(new), ", ".join(sorted(set(describe(e) for e in new))[:4])))
    for table in original["tables"]:
        if table == "blocks":
            gone, new = multiset_diff(original["tables"][table], output["tables"][table])
        else:
            # a table entry compared by name and attributes; the handle is not part of the attributes
            gone, new = multiset_diff(original["tables"][table], output["tables"][table])
        if gone or new:
            diffs.append("table %s: only in original %s, only in output %s" % (
                table, [g[0] for g in gone][:4], [n[0] for n in new][:4]))
    if original["objects"] != output["objects"]:
        d1 = original["objects"] - output["objects"]
        d2 = output["objects"] - original["objects"]
        diffs.append("objects: fewer %s, more %s" % (dict(d1), dict(d2)))
    return diffs


# ---------------------------------------------------------------------------
# The core's log: what LIST and MASSPROP printed
# ---------------------------------------------------------------------------

def core_blocks(text, with_handle):
    """The LIST blocks (one per entity) of a core log, normalised: a multiset of strings."""
    start = text.find("\n_.LIST")
    end = min([i for i in (text.find("\n_.TILEMODE"), text.find("\n_.-WBLOCK")) if i >= 0] or [-1])
    if start < 0 or end < 0:
        return None
    body = text[start:end].replace("\r", "")
    parts = re.split(r"\n(?=\s{8,}\S[^\n]*Layer: )", body)
    blocks = []
    for part in parts[1:]:
        lines = [l.strip() for l in part.splitlines() if l.strip()]
        if not with_handle:
            lines = [l for l in lines if not l.startswith("Handle =")]
        blocks.append("\n".join(lines))
    return blocks


def core_mass(text):
    """{"ALL": [lines]} of the MASSPROP output between the echoed _.MASSPROP and _.LIST commands (empty when the file had no solid)."""
    text = text.replace(chr(13), "")
    start, end = text.find("\n_.MASSPROP"), text.find("\n_.LIST")
    if start < 0 or end < 0:
        return {}
    lines = [re.sub(r"\s+", " ", l.strip()) for l in text[start:end].splitlines()[1:] if l.strip()]
    return {"ALL": [l for l in lines if not l.startswith(("Press F1", "Command:", "Write analysis"))]}


def numbers_close(a, b, rel=1e-6):
    pa = [float(x) for x in re.findall(r"-?\d+\.?\d*(?:[eE][-+]?\d+)?", a)]
    pb = [float(x) for x in re.findall(r"-?\d+\.?\d*(?:[eE][-+]?\d+)?", b)]
    if len(pa) != len(pb):
        return False
    return all(abs(x - y) <= rel * max(1.0, abs(x), abs(y)) for x, y in zip(pa, pb))


def extents_close(a, b, tolerance=1e-6):
    pa = [float(x) for x in a.split(",") if x]
    pb = [float(x) for x in b.split(",") if x]
    return len(pa) == len(pb) and all(abs(x - y) <= tolerance * max(1.0, abs(x), abs(y)) for x, y in zip(pa, pb))


# ---------------------------------------------------------------------------
# The batch
# ---------------------------------------------------------------------------

def file_version(path):
    return summarize(path)["acadver"]


def collect_originals(roots, work_in, only, with_other):
    found, skipped = [], []
    for root in roots:
        for folder, _, names in os.walk(root):
            for name in sorted(names):
                if not name.lower().endswith(".dxf") or (only and only not in name):
                    continue
                path = os.path.join(folder, name)
                version = file_version(path)
                if version in TARGET_OF:
                    group = "original"
                elif with_other and version in OTHER_TARGET_OF:
                    group = "other_original"
                else:
                    skipped.append((path, version or "no $ACADVER"))
                    continue
                relative = os.path.relpath(path, root).replace("\\", "/")
                copy = os.path.join(work_in, group, group + "__" + safe_name(os.path.basename(root.rstrip("/\\")) + "__" + relative))
                if not copy.lower().endswith(".dxf"):
                    copy += ".dxf"
                found.append((path, copy, group))
    return found, skipped


def check_work(work, originals):
    work_real = os.path.realpath(work)
    repo = os.path.realpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    if work_real == repo or work_real.startswith(repo + os.sep):
        sys.exit("--work must be outside the repository (%s)" % repo)
    for root in originals:
        if work_real.startswith(os.path.realpath(root) + os.sep) or work_real == os.path.realpath(root):
            sys.exit("--work must not be inside the originals (%s)" % root)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--work", required=True, help="the directory the batch works in (outside the repository)")
    parser.add_argument("--originals", action="append", help="where the originals are (default build/test-data)")
    parser.add_argument("--run-tests", help="hdxf_tests.exe (absolute path): run the suite with HDXF_EXPORT_DIR set")
    parser.add_argument("--test-filter", action="append", help="test name filters for --run-tests "
                        "(default coverage_r13_r14 integration_r13_r14)")
    parser.add_argument("--exports", help="a directory of files HDxf wrote (case__*.dxf, roundtrip__*.dxf ...)")
    parser.add_argument("--other-versions-tool", help="roundtrip tool (<exe> <in.dxf> <outdir>: writes roundtrip__<name>__ascii|binary.dxf "
                        "as the file's own version); with it the originals of R12 and 2000-2018 are read by the tools too, as the "
                        "regression run: phase 7 must not change them")
    parser.add_argument("--resources", action="append", help="a directory with fonts, SHX, images the files refer to (e.g. ACadSharp's "
                        "test_shape.shx, which is in no checked-in directory); copied next to every file the tools read")
    parser.add_argument("--only", help="only the files whose name contains this")
    parser.add_argument("--oda", help="ODAFileConverter.exe (default: the newest under C:/Program Files/ODA, 27.9.1 first)")
    parser.add_argument("--core", default=CORE, help="accoreconsole.exe")
    parser.add_argument("--oda-types", default="DXF,DWG", help="ODA's output types (default DXF,DWG)")
    parser.add_argument("--skip-oda", action="store_true")
    parser.add_argument("--skip-core", action="store_true")
    parser.add_argument("--skip-ezdxf", action="store_true")
    parser.add_argument("--timeout", type=int, default=180, help="seconds per tool run (ODA: per directory x 10)")
    args = parser.parse_args()

    repo = os.path.realpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    roots = args.originals or [os.path.join(repo, "build", "test-data")]
    check_work(args.work, roots)
    work_in = os.path.join(args.work, "in")
    for folder in GROUPS:
        os.makedirs(os.path.join(work_in, folder), exist_ok=True)
    started = time.strftime("%Y-%m-%d %H:%M:%S")
    lines = []
    problems_found = []   # the findings of the batch itself: skips, missing files, tool failures

    def say(text):
        print(text, flush=True)
        lines.append(text)

    def finding(text):
        problems_found.append(text)
        say("FINDING: " + text)

    # 1. the originals (copies; the originals are checked before and after) and the resources they need
    with_other = bool(args.other_versions_tool)
    originals, skipped = collect_originals(roots, work_in, args.only, with_other)
    hashes = {}
    inputs = []          # (group, copy path, kind, source)
    for source, copy, group in originals:
        hashes[source] = sha256(source)
        shutil.copyfile(source, copy)
        inputs.append((group, copy, "original", source))
    resources = collect_resources(roots, repo, args.resources)
    say("originals: %d copied from %s (%d R13/R14, %d of other versions)" % (
        len(originals), ", ".join(roots), sum(1 for o in originals if o[2] == "original"), sum(1 for o in originals if o[2] == "other_original")))
    say("resources found for the tools (fonts, SHX, images next to the originals): %d" % len(resources))
    for path, version in skipped:
        say("not in the batch (%s): %s" % (version, path))
    if skipped and not with_other:
        say("  (files of other versions are left out: --other-versions-tool brings them in)")
    elif skipped:
        for path, version in skipped:
            finding("a DXF in the roots is not in the batch (%s): %s" % (version, path))
    found_names = {os.path.basename(o[0]) for o in originals if o[2] == "original"}
    if not args.only:
        for name in sorted(expected_originals(roots) - found_names):
            finding("expected R13/R14 original is missing: %s" % name)

    # 2. what HDxf wrote
    export_dir = args.exports
    test_text = ""
    if args.run_tests:
        export_dir = os.path.join(args.work, "hdxf_raw")
        shutil.rmtree(export_dir, ignore_errors=True)
        os.makedirs(export_dir)
        env = dict(os.environ, HDXF_EXPORT_DIR=export_dir)
        filters = args.test_filter or ["coverage_r13_r14", "integration_r13_r14"]
        code, text, seconds = run([os.path.abspath(args.run_tests)] + filters, args.timeout * 10, env=env)
        test_text = text
        say("tests: %s -> exit %s in %.0f s" % (" ".join(filters), code, seconds))
        with open(os.path.join(args.work, "tests.log"), "w", encoding="utf-8") as f:
            f.write(text)
        summary_line = [l for l in text.splitlines() if re.search(r"\d+ passed, \d+ failed", l)]
        say("tests: " + (summary_line[-1].strip() if summary_line else "no result line"))
        if code != 0 or not summary_line or " 0 failed" not in summary_line[-1]:
            finding("the test run of the batch did not pass: exit %s, %s" % (code, summary_line[-1].strip() if summary_line else "no result line"))
        for l in text.splitlines():
            if re.search(r"\bSKIP", l):
                finding("a test is skipped: " + l.strip())
        if re.search(r"[1-9]\d* skipped", " ".join(summary_line)):
            finding("the test run reports skipped tests: " + summary_line[-1].strip())
    hdxf_files = []
    if export_dir and os.path.isdir(export_dir):
        for name in sorted(os.listdir(export_dir)):
            if not name.lower().endswith(".dxf") or (args.only and args.only not in name):
                continue
            copy = os.path.join(work_in, "hdxf", name)
            shutil.copyfile(os.path.join(export_dir, name), copy)
            kind = name.split("__")[0]
            inputs.append(("hdxf", copy, kind, os.path.join(export_dir, name)))
            hdxf_files.append(copy)
    say("HDxf files (R13/R14): %d" % len(hdxf_files))
    if not args.only and (args.run_tests or args.exports):
        original_names = sorted(found_names)
        expected = expected_hdxf_files(original_names, None)
        present = {os.path.basename(p) for p in hdxf_files}
        for name in sorted(expected - present):
            reason = ""
            m = re.match(r"(removed|forced|changed|roundtrip)__(.+)__(ascii|binary)\.dxf", name)
            if m:
                src = [o[0] for o in originals if os.path.basename(o[0]) == m.group(2)]
                if src:
                    s = summarize(src[0])
                    if m.group(1) == "removed" and not s["model"]:
                        reason = " (the drawing has no entity in model space to remove: the test skips the removal)"
                    elif m.group(1) == "forced":
                        reason = " (forced.save_to_memory() was empty: the test skips it silently)"
            finding("expected file is missing: %s%s" % (name, reason))
        for name in sorted(present - expected):
            finding("unexpected file from the test run: %s" % name)

    # the other versions: HDxf reads and writes each again as its own version (the regression run)
    if with_other:
        regress_dir = os.path.join(args.work, "hdxf_other_raw")
        shutil.rmtree(regress_dir, ignore_errors=True)
        os.makedirs(regress_dir)
        count = 0
        for source, copy, group in originals:
            if group != "other_original":
                continue
            code, text, seconds = run([os.path.abspath(args.other_versions_tool), source, regress_dir], args.timeout)
            for l in text.splitlines():
                if l.startswith("FAIL"):
                    finding("HDxf could not read or write a file of another version: " + l)
                elif l.startswith("WARN"):
                    say("  " + l)
        for name in sorted(os.listdir(regress_dir)):
            copy = os.path.join(work_in, "other_hdxf", name)
            shutil.copyfile(os.path.join(regress_dir, name), copy)
            inputs.append(("other_hdxf", copy, "roundtrip", os.path.join(regress_dir, name)))
            count += 1
        say("HDxf files (other versions, rewritten as their own version): %d" % count)
        for source, copy, group in originals:
            if group == "other_original":
                base = os.path.basename(source)
                for fmt in ("ascii", "binary"):
                    if not os.path.exists(os.path.join(work_in, "other_hdxf", "roundtrip__%s__%s.dxf" % (base, fmt))):
                        finding("expected regression file is missing: roundtrip__%s__%s.dxf" % (base, fmt))
    if not inputs:
        sys.exit("no input files")

    # resources next to every group's files, and what each file refers to that is not there
    fonts = font_dirs(args.core)
    for group in GROUPS:
        for name, source in resources.items():
            shutil.copyfile(source, os.path.join(work_in, group, os.path.basename(source)))

    info = {}
    for group, path, kind, source in inputs:
        info[path] = summarize(path)
        info[path]["group"] = group
        info[path]["kind"] = kind
        info[path]["source"] = source
        info[path]["refs"] = references_of(path)
        info[path]["missing_resources"] = unresolved(info[path]["refs"], os.path.dirname(path), resources, fonts)
    missing_by_file = {os.path.basename(p): i["missing_resources"] for p, i in info.items() if i["missing_resources"] and i["group"] in ("original", "other_original")}
    for name, missing in sorted(missing_by_file.items()):
        say("resources the tools cannot find for %s: %s" % (name, ", ".join(missing)))

    # 2b. ezdxf: the independent reader
    ez = {}
    ez_available = False
    if not args.skip_ezdxf:
        for group, path, kind, source in inputs:
            ez[path] = ezdxf_read(path)
            ez_available = ez[path]["available"]
            if not ez_available:
                break
        if not ez_available:
            ez = {}
            say("ezdxf: not installed (python -c \"import ezdxf\" fails): no independent reader in this run")
        else:
            import ezdxf
            say("ezdxf %s: %d read, %d failed even in recovery mode, %d needed recovery mode, %d with audit errors, %d with audit fixes" % (
                ezdxf.__version__, sum(1 for e in ez.values() if e["ok"]), sum(1 for e in ez.values() if not e["ok"]),
                sum(1 for e in ez.values() if e["mode"] == "recover"), sum(1 for e in ez.values() if e["audit_errors"]),
                sum(1 for e in ez.values() if e["audit_fixes"])))

    # 3. ODA File Converter, one run per (group, version, type): the file's own version as the target
    oda_result = collections.defaultdict(dict)
    oda_path, oda_version = (None, "")
    if not args.skip_oda:
        oda_path, oda_version = find_oda(args.oda)
        if not oda_path or not os.path.exists(oda_path):
            say("ODA File Converter: not found")
        else:
            say("ODA File Converter: %s (%s)%s" % (oda_path, oda_version, "" if ODA_WANTED in oda_path else
                                                    ", NOT %s as the plan says" % ODA_WANTED))
            for group in GROUPS:
                for version in sorted({i["acadver"] for p, i in info.items() if i["group"] == group and i["acadver"] in ALL_TARGET_OF}):
                    members = [p for g, p, k, s in inputs if g == group and info[p]["acadver"] == version]
                    if not members:
                        continue
                    source_dir = os.path.join(args.work, "oda_in", group, version)
                    shutil.rmtree(source_dir, ignore_errors=True)
                    os.makedirs(source_dir)
                    for p in members:
                        shutil.copyfile(p, os.path.join(source_dir, os.path.basename(p)))
                    for name, source in resources.items():
                        shutil.copyfile(source, os.path.join(source_dir, os.path.basename(source)))
                    for kind in [t.strip().upper() for t in args.oda_types.split(",") if t.strip()]:
                        out_dir = os.path.join(args.work, "oda_out", group, version, kind)
                        shutil.rmtree(out_dir, ignore_errors=True)
                        os.makedirs(out_dir)
                        command = [oda_path, source_dir, out_dir, ALL_TARGET_OF[version], kind, "0", "1", "*.dxf"]
                        code, text, seconds = run(command, args.timeout * 10)
                        outputs = [n for n in os.listdir(out_dir) if n.lower().endswith("." + kind.lower())]
                        errors = [n for n in os.listdir(out_dir) if n.lower().endswith(".err")]
                        say("ODA %s %s %s: %d in, %d out, %d .err, exit %s, %.0f s" % (
                            group, ALL_TARGET_OF[version], kind, len(members), len(outputs), len(errors), code, seconds))
                        with open(os.path.join(out_dir, "_oda_output.log"), "w", encoding="utf-8") as f:
                            f.write(text)
                        for p in members:
                            base = os.path.splitext(os.path.basename(p))[0]
                            out_file = os.path.join(out_dir, base + "." + kind.lower())
                            err = [n for n in errors if n.startswith(base)]
                            result = {"made": os.path.exists(out_file), "err": bool(err), "exit": code}
                            if err:
                                with open(os.path.join(out_dir, err[0]), encoding="utf-8", errors="replace") as f:
                                    result["err_text"] = f.read(300).strip().replace("\n", " | ")
                            if result["made"]:
                                if kind == "DXF":
                                    s = summarize(out_file)
                                    result["acadver"] = s["acadver"]
                                    result["entities"] = sum(s["entities"].values())
                                else:
                                    result["acadver"] = acadver_of_dwg(out_file)
                            else:
                                finding("ODA made no %s of %s" % (kind, os.path.basename(p)))
                            oda_result[p][kind] = result

    # 4. AutoCAD's core, one run per file
    core_result = {}
    core_text = {}
    if not args.skip_core:
        if not os.path.exists(args.core):
            say("AutoCAD's core: not found (%s)" % args.core)
        else:
            say("AutoCAD's core: %s" % args.core)
            for group, path, kind, source in inputs:
                folder = os.path.join(args.work, "core", group)
                os.makedirs(folder, exist_ok=True)
                base = os.path.splitext(os.path.basename(path))[0]
                dwg = os.path.join(folder, base + ".dwg")
                script = os.path.join(folder, base + ".scr")
                has = info[path]["entities"]
                # (the core has no SELECT and no EXTMIN/EXTMAX in SETVAR: the extents and the number of entities
                # of what it read are those of the DWG it wrote, which ODA makes DXF below)
                commands = ["_.REGEN", "_.ZOOM", "_E"]
                # (the core has no LISP and stops the script at an unknown command; the log echoes each command as typed,
                # and those lines delimit the MASSPROP and LIST output; MASSPROP takes all the
                # 3DSOLIDs and REGIONs at once, there is no way to pick one by its handle)
                if info[path]["solids"]:
                    commands += ["_.MASSPROP", "_ALL", "", "_N"]
                commands += ["_.LIST", "_ALL", ""]
                if info[path]["paper"]:
                    commands += ["_.TILEMODE", "0", "_.ZOOM", "_E", "_.TILEMODE", "1"]
                commands += ["_.-WBLOCK", os.path.abspath(dwg), "*", "_.QUIT", "_Y"]
                with open(script, "w", encoding="ascii", newline="") as f:
                    f.write("\r\n".join(commands) + "\r\n")
                if os.path.exists(dwg):
                    os.remove(dwg)
                code, text, seconds = run([args.core, "/i", os.path.abspath(path), "/s", script, "/l", "en-US"],
                                          args.timeout, cwd=folder)
                with open(os.path.join(folder, base + ".log"), "w", encoding="utf-8") as f:
                    f.write(text if code is not None else text + "\n[TIMEOUT]\n")
                core_text[path] = text
                problems = [l.strip() for l in text.splitlines()
                            if re.search(r"Invalid|error|Error|discarded|corrupt|ignored|Unknown|cannot|Cannot|Unable|Failed|too low|Missing|Bad handle", l)]
                found = re.findall(r"(\d+) found", text)
                core_result[path] = {"exit": code, "seconds": seconds, "regen": "Regenerating model" in text,
                                     "problems": problems, "found": found[-1] if found else "",
                                     "dwg": os.path.exists(dwg), "timeout": code is None,
                                     "discarded": "drawing discarded" in text}
                if os.path.exists(dwg):
                    core_result[path]["dwg_version"] = acadver_of_dwg(dwg)

    # 4b. the core's DWGs as DXF by ODA (ACAD2018 DXF): the extents and the entities AutoCAD's core made of each file.
    # ODA's own, later serialisation of the core's DWG, not the core's
    core_dxf = {}
    if core_result and oda_path and os.path.exists(oda_path) and not args.skip_oda:
        for group in GROUPS:
            members = [p for g, p, k, s in inputs if g == group and core_result.get(p, {}).get("dwg")]
            if not members:
                continue
            source_dir = os.path.join(args.work, "oda_in", "core_" + group)
            out_dir = os.path.join(args.work, "oda_out", "core_" + group)
            shutil.rmtree(source_dir, ignore_errors=True)
            shutil.rmtree(out_dir, ignore_errors=True)
            os.makedirs(source_dir)
            os.makedirs(out_dir)
            for p in members:
                base = os.path.splitext(os.path.basename(p))[0]
                shutil.copyfile(os.path.join(args.work, "core", group, base + ".dwg"), os.path.join(source_dir, base + ".dwg"))
            code, text, seconds = run([oda_path, source_dir, out_dir, "ACAD2018", "DXF", "0", "1", "*.dwg"], args.timeout * 10)
            outputs = [n for n in os.listdir(out_dir) if n.lower().endswith(".dxf")]
            errors = [n for n in os.listdir(out_dir) if n.lower().endswith(".err")]
            say("ODA of the core's DWG, %s: %d in, %d out, %d .err, exit %s, %.0f s" % (group, len(members), len(outputs), len(errors), code, seconds))
            for p in members:
                base = os.path.splitext(os.path.basename(p))[0]
                out_file = os.path.join(out_dir, base + ".dxf")
                if os.path.exists(out_file):
                    core_dxf[p] = summarize(out_file)

    # 5. the originals are untouched
    for source, _, _ in originals:
        if sha256(source) != hashes[source]:
            say("ORIGINAL CHANGED: %s" % source)
            sys.exit(2)
    say("originals unchanged (SHA256 before and after)")

    # 6. the summary, one row per file
    rows = []
    for group, path, kind, source in inputs:
        i = info[path]
        oda = oda_result.get(path, {})
        core = core_result.get(path, {})
        cdx = core_dxf.get(path, {})
        e = ez.get(path, {})
        rows.append([
            group, kind, os.path.basename(path), i["acadver"] or i["error"], sum(i["entities"].values()), i["model"], i["paper"],
            i["extmin"], i["extmax"],
            "yes" if oda.get("DXF", {}).get("made") else ("no" if "DXF" in oda else "-"),
            oda.get("DXF", {}).get("acadver", ""), oda.get("DXF", {}).get("entities", ""),
            "yes" if oda.get("DWG", {}).get("made") else ("no" if "DWG" in oda else "-"),
            oda.get("DWG", {}).get("acadver", ""),
            "ERR" if any(r.get("err") for r in oda.values()) else ("ok" if oda else "-"),
            ("TIMEOUT" if core.get("timeout") else ("DISCARDED" if core.get("discarded") else ("regen" if core.get("regen") else "NO-REGEN"))) if core else "-",
            len(core.get("problems", [])) if core else "-", core.get("found", ""),
            sum(cdx["entities"].values()) if cdx else "",
            cdx.get("extmin", ""), cdx.get("extmax", ""),
            "yes" if core.get("dwg") else ("no" if core else "-"), core.get("dwg_version", ""),
            (("ok" if e.get("ok") else "FAILED") + ("/recover" if e.get("mode") == "recover" else "")) if e else "-",
            "%s>%s" % (e.get("loaded", ""), e.get("version", "")) if e else "", e.get("entities", "") if e else "",
            ("%d/%d" % (e["audit_errors"], e["audit_fixes"])) if e and e.get("ok") else "",
            cdx.get("pextmin", ""), cdx.get("pextmax", ""), ";".join(i["missing_resources"]),
        ])
    header = ["group", "kind", "file", "ACADVER", "entities", "model", "paper", "EXTMIN", "EXTMAX", "ODA DXF", "ODA ACADVER",
              "ODA entities", "ODA DWG", "DWG ACADVER", "ODA .err", "core", "core problems", "core LIST found",
              "core DWG entities", "core DWG EXTMIN", "core DWG EXTMAX", "core DWG", "core DWG ACADVER",
              "ezdxf", "ezdxf loaded>model", "ezdxf entities", "ezdxf audit errors/fixes", "core DWG PEXTMIN", "core DWG PEXTMAX",
              "missing resources"]
    with open(os.path.join(args.work, "summary.tsv"), "w", encoding="utf-8", newline="") as f:
        f.write("\t".join(header) + "\n")
        for row in rows:
            f.write("\t".join(str(c) for c in row) + "\n")

    # 7. original against HDxf's output of it: ezdxf's entities/tables/objects, the core's LIST blocks and MASSPROP, the extents
    by_source = collections.defaultdict(list)
    for group, path, kind, source in inputs:
        if group in ("original", "other_original"):
            by_source[os.path.basename(source)].append(path)
    compare_lines = []
    compare_rows = []
    for group, path, kind, source in inputs:
        if group not in ("hdxf", "other_hdxf"):
            continue
        m = re.match(r"(roundtrip|changed|removed|forced)__(.+)__(ascii|binary)\.dxf$", os.path.basename(path), re.I)
        if not m:
            continue
        candidates = by_source.get(m.group(2), [])
        if len(candidates) != 1:
            finding("no single original for %s (%d candidates)" % (os.path.basename(path), len(candidates)))
            continue
        orig = candidates[0]
        diffs = []
        if ez.get(orig) and ez.get(path):
            diffs += ["ezdxf " + d for d in compare_ezdxf(ez[orig], ez[path], m.group(1))]
        co, ch = core_result.get(orig, {}), core_result.get(path, {})
        if co and ch:
            if co.get("discarded") != ch.get("discarded"):
                diffs.append("core: original %s, output %s" % ("DISCARDED" if co.get("discarded") else "opened", "DISCARDED" if ch.get("discarded") else "opened"))
            with_handle = m.group(1) in ("roundtrip", "changed", "removed")
            bo, bh = core_blocks(core_text[orig], with_handle), core_blocks(core_text[path], with_handle)
            if bo is not None and bh is not None:
                gone, new = multiset_diff(bo, bh)
                if gone or new:
                    kinds = lambda bl: sorted(set(b.split("\n")[0].split()[0] for b in bl))[:6]
                    diffs.append("core LIST: only in original %d %s, only in output %d %s" % (len(gone), kinds(gone), len(new), kinds(new)))
            mo, mh = core_mass(core_text[orig]), core_mass(core_text[path])
            for handle in sorted(set(mo) | set(mh)):
                if handle not in mo or handle not in mh or not all(numbers_close(a, b) for a, b in zip(mo[handle], mh[handle])) \
                        or len(mo[handle]) != len(mh[handle]):
                    diffs.append("core MASSPROP of %s differs: %s | %s" % (handle, mo.get(handle, "-")[:3], mh.get(handle, "-")[:3]))
        if orig in core_dxf and path in core_dxf:
            for field in ("extmin", "extmax", "pextmin", "pextmax"):
                if not extents_close(core_dxf[orig][field], core_dxf[path][field], 1e-6) and m.group(1) in ("roundtrip", "forced"):
                    diffs.append("core DWG %s: %s | %s" % (field, core_dxf[orig][field], core_dxf[path][field]))
        oo, oh = oda_result.get(orig, {}).get("DXF", {}), oda_result.get(path, {}).get("DXF", {})
        if oo.get("made") and oh.get("made") and oo.get("entities") != oh.get("entities") and m.group(1) in ("roundtrip", "forced"):
            diffs.append("ODA DXF entities %s | %s" % (oo.get("entities"), oh.get("entities")))
        compare_rows.append([m.group(1), m.group(3), os.path.basename(orig), len(diffs), " ;; ".join(diffs)])
        if diffs:
            compare_lines.append("%s %s %s: %s" % (m.group(1), m.group(3), os.path.basename(orig), " ;; ".join(diffs)))
    with open(os.path.join(args.work, "compare.tsv"), "w", encoding="utf-8", newline="") as f:
        f.write("kind\tformat\toriginal\tdifferences\tdetail\n")
        for row in compare_rows:
            f.write("\t".join(str(c) for c in row) + "\n")

    # 8. the totals
    total = len(rows)
    say("")
    say("files: %d (%d R13/R14 originals, %d from HDxf, %d originals of other versions, %d of their HDxf output)" % (
        total, sum(r[0] == "original" for r in rows), sum(r[0] == "hdxf" for r in rows), sum(r[0] == "other_original" for r in rows),
        sum(r[0] == "other_hdxf" for r in rows)))
    if oda_result:
        say("ODA: %d with a DXF out, %d with a DWG out, %d with a .err (of %d read)" % (
            sum(1 for r in rows if r[9] == "yes"), sum(1 for r in rows if r[12] == "yes"), sum(1 for r in rows if r[14] == "ERR"),
            sum(1 for r in rows if r[14] != "-")))
        wrong = [r[2] for r in rows if r[9] == "yes" and r[10] != r[3]]
        say("ODA DXF with another $ACADVER than the input: %d%s" % (len(wrong), (" " + ", ".join(wrong[:5])) if wrong else ""))
        wrong = [r[2] for r in rows if r[12] == "yes" and r[13] != (r[3][:6])]
        say("ODA DWG with another version than the input: %d%s" % (len(wrong), (" " + ", ".join(wrong[:5])) if wrong else ""))
        for p, results in oda_result.items():
            for kind, r in results.items():
                if r.get("err"):
                    finding("ODA .err for %s (%s): %s" % (os.path.basename(p), kind, r.get("err_text", "")))
    if core_result:
        say("core: %d read with a regeneration, %d discarded, %d without regeneration, %d timeouts, %d with messages of a problem, %d DWG written" % (
            sum(1 for r in rows if r[15] == "regen"), sum(1 for r in rows if r[15] == "DISCARDED"), sum(1 for r in rows if r[15] == "NO-REGEN"),
            sum(1 for r in rows if r[15] == "TIMEOUT"), sum(1 for r in rows if isinstance(r[16], int) and r[16] > 0), sum(1 for r in rows if r[21] == "yes")))
    if ez:
        say("ezdxf: %d files, %d not read, %d in recovery mode, loaded versions %s" % (
            len(ez), sum(1 for e in ez.values() if not e["ok"]), sum(1 for e in ez.values() if e["mode"] == "recover"),
            dict(collections.Counter(e["loaded"] + ">" + e["version"] for e in ez.values() if e["ok"]))))
    say("compared with the original: %d HDxf files, %d with a difference (compare.tsv)" % (len(compare_rows), len(compare_lines)))
    say("")
    for row in rows:
        say("  ".join(str(c) for c in row[:6] + row[9:]))
    say("")
    say("differences original -> HDxf output:")
    for line in compare_lines:
        say("  " + line)
    say("")
    say("FINDINGS OF THE BATCH ITSELF (%d):" % len(problems_found))
    for p in problems_found:
        say("  " + p)
    say("")
    say("started %s, finished %s; logs and outputs under %s" % (started, time.strftime("%Y-%m-%d %H:%M:%S"), args.work))
    with open(os.path.join(args.work, "summary.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
