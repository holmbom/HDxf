#!/usr/bin/env python3
"""Generates the machine-readable type register of phase 9 (step 9.0) from the old plan's appendices.

    python -I tools/gen_type_registry.py            # writes the three files below
    python -I tools/gen_type_registry.py --check    # fails if the files are not what the sources give

Sources: doc/plans/2026-10-25-fas-9-typer.md (appendix A: every 0-record name of the inventory with its
status and sample counts; appendix B: every observed group order per untyped type; appendix C: the
requested names without records) and the dispatch of the reader (src/dxf_reader.cpp parse_single_entity(),
src/object_reader.cpp parse_object()), which decides what is typed in the code now (decision B117: the
current code, not the old plan's status).

Writes
    src/typed_registry_data.inc    the library's register (TypeRegistryEntry rows, name order)
    test/data/typed_types.tsv      appendix A/C: kind, status, step, sample counts per version
    test/data/typed_forms.tsv      appendix B: the observed group orders with their sample and SHA256

The tool reads text only; it uses no HDxf code and no other project's code.
"""

import hashlib
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OLD_PLAN = os.path.join(ROOT, "doc", "plans", "2026-10-25-fas-9-typer.md")
READER = os.path.join(ROOT, "src", "dxf_reader.cpp")
OBJECT_READER = os.path.join(ROOT, "src", "object_reader.cpp")
OUT_INC = os.path.join(ROOT, "src", "typed_registry_data.inc")
OUT_TYPES = os.path.join(ROOT, "test", "data", "typed_types.tsv")
OUT_FORMS = os.path.join(ROOT, "test", "data", "typed_forms.tsv")

# Names the reader recognises that are not 0-names of their own: spellings of another name
ALIASES = {"Coordination Model", "AcDbNavisworksModelDef"}

# Names 1.8.1's reader typed whose reader function a family codec has taken over (their dispatch lines are gone from
# the reader's source): still "typed" in the register, which describes the inventory (B117). Step 9.6.
TYPED_BEFORE_THE_CODEC = {"COORDINATION_MODEL", "NAVISWORKSMODELDEF", "LIGHT", "SUN", "RAPIDRTRENDERSETTINGS",
                          "RAPIDRTRENDERENVIRONMENT", "ACDBDETAILVIEWSTYLE", "ACDBSECTIONVIEWSTYLE"}

# Entities (ENTITIES and BLOCKS sections); every other name is an object
ENTITY_NAMES = {
    "3DFACE", "3DSOLID", "ACAD_TABLE", "ACAD_PROXY_ENTITY", "ARC", "ARC_DIMENSION", "ATTDEF", "ATTRIB", "BODY",
    "CIRCLE", "COORDINATION_MODEL", "DGNUNDERLAY", "DIMENSION", "DWFUNDERLAY", "ELLIPSE", "EXTRUDEDSURFACE",
    "HATCH", "HELIX", "IMAGE", "INSERT", "LARGE_RADIAL_DIMENSION", "LEADER", "LIGHT", "LINE", "LOFTEDSURFACE",
    "LWPOLYLINE", "MESH", "MLINE", "MTEXT", "MULTILEADER", "NURBSURFACE", "OLE2FRAME", "OLEFRAME", "PDFUNDERLAY",
    "PDFREFERENCE", "PLANESURFACE", "POINT", "POLYLINE", "RAY", "REGION", "REVOLVEDSURFACE", "SECTIONOBJECT",
    "SHAPE", "SOLID", "SPLINE", "SURFACE", "SWEPTSURFACE", "TCPOINTENTITY", "TEXT", "TOLERANCE", "TRACE",
    "VIEWPORT", "WIPEOUT", "XLINE",
}
STRUCTURAL_NAMES = {"BLOCK", "ENDBLK", "SEQEND", "VERTEX"}

# Record names the corpus of 1.8.1 has that the old plan's inventory did not list (its corpus was another one):
# name, kind, status, step, what it is. Found by test_all_typed_records (every record name of the corpus must be
# in the register) and tools/inventory_types.py
CORPUS_EXTRAS = [
    ("ACDB_DYNAMICBLOCKPROXYNODE", "O", "Pending", 13,
     "a graph node that stands for an object of an application that is not loaded (AutoCAD 2025's Sample/Dynamic Blocks, saved as DXF: doc/facit/dyntypes)"),
    ("ACDB_RADIMLRGOBJECTCONTEXTDATA_CLASS", "O", "Pending", 17,
     "the large radial dimension's context data under the DXF name AutoCAD 2025 writes (acad_2010_dxf_annotative)"),
    ("ASSOCDIMDEPENDENCYBODY", "O", "Pending", 15,
     "ACDBASSOCDIMDEPENDENCYBODY under the DXF name AutoCAD 2025 writes in 2000 (acad_2000_dxf_associative)"),
    ("ACDBASSOCRADIALDIMACTIONBODY", "O", "Pending", 15,
     "the action body of a radius dimension on a solid, which the old plan's list of ACDBASSOC names did not have (fas9_assoc_extended)"),
    ("ACDBASSOCIMPLIEDSURFACEORSOLIACTIONBODY", "O", "Pending", 15,
     "the action body SURFTRIM leaves under the DXF name AutoCAD 2025 writes in 2000 (acad_2000_dxf_surfaces_trim_trimmed, step 9.16)"),
    ("ACDBASSOCTRIMMINGBODYACTIONPARAM", "O", "Pending", 15,
     "the parameter of a surface that SURFTRIM cuts with: the face parameter and AcDbAssocTrimmingBodyActionParam 90, 280 "
     "(acad_*_dxf_history_surf, fas9_assoc_history_surf.scr, step 9.15; typed by its codec, AssocTrimmingBodyActionParamData)"),
    ("AMLOADER", "O", "External", 0,
     "an AutoCAD Mechanical object in a third-party R14 file (local-r13r14): another program's class, kept untyped"),
]

# The step of phase 9 (plan 2026-10-27-fas-9-typer.md) that completes each name; 0 = nothing left to do
STEP_NAMES = {
    1: ["ACAD_TABLE", "TABLESTYLE", "TABLECONTENT", "CELLSTYLEMAP", "TABLEGEOMETRY"],
    2: ["WIPEOUT", "WIPEOUTVARIABLES", "SCALE", "BLOCK", "ENDBLK", "LWPOLYLINE", "POLYLINE", "VERTEX"],
    3: ["OLEFRAME", "OLE2FRAME", "ACAD_PROXY_ENTITY", "ACAD_PROXY_OBJECT"],
    4: ["SORTENTSTABLE", "SPATIAL_FILTER", "LAYER_INDEX", "SPATIAL_INDEX", "IDBUFFER", "LAYER_FILTER"],
    6: ["PDFUNDERLAY", "DWFUNDERLAY", "DGNUNDERLAY", "PDFDEFINITION", "DWFDEFINITION", "DGNDEFINITION",
        "PDFREFERENCE", "COORDINATION_MODEL", "NAVISWORKSMODELDEF", "XREFPANELOBJECT", "EXACXREFPANELOBJECT"],
    7: ["MATERIAL", "VISUALSTYLE"],
    8: ["FIELD", "FIELDLIST", "DATATABLE", "ACDBDATATABLE", "OBJECT_PTR", "VBA_PROJECT"],
    9: ["ACAD_EVALUATION_GRAPH", "BLOCKLINEARPARAMETER", "BLOCKLINEARGRIP", "BLOCKGRIPLOCATIONCOMPONENT",
        "BLOCKVISIBILITYPARAMETER", "BLOCKVISIBILITYGRIP", "BLOCKSCALEACTION", "ACDB_BLOCKREPRESENTATION_DATA",
        "ACDB_DYNAMICBLOCKPURGEPREVENTER_VERSION"],
    10: ["ACDBASSOCACTION", "ACDBASSOCNETWORK", "ACDBASSOCDEPENDENCY", "ACDBASSOCGEOMDEPENDENCY",
         "ACDBASSOCPERSSUBENTMANAGER", "ACDBPERSSUBENTMANAGER", "ACDBASSOCPATHACTIONPARAM",
         "ACDBASSOCVERTEXACTIONPARAM", "ACDBASSOCOSNAPPOINTREFACTIONPARAM", "ACDBASSOCEDGEACTIONPARAM",
         "ACDBASSOCEXTRUDEDSURFACEACTIONBODY", "ACDBASSOCLOFTEDSURFACEACTIONBODY",
         "ACDBASSOCREVOLVEDSURFACEACTIONBODY", "ACDBASSOCSWEPTSURFACEACTIONBODY",
         "ACDBASSOCPLANESURFACEACTIONBODY", "ACDBASSOCALIGNEDDIMACTIONBODY"],
    11: ["RENDERSETTINGS", "MENTALRAYRENDERSETTINGS", "RAPIDRTRENDERSETTINGS", "RENDERENVIRONMENT", "RENDERGLOBAL",
         "RAPIDRTRENDERENVIRONMENT", "RENDERENTRY", "LIGHTLIST", "SUNSTUDY", "LIGHT", "SUN"],
    12: ["SECTIONOBJECT", "SECTION_MANAGER", "SECTION_SETTINGS", "ACDBDETAILVIEWSTYLE", "ACDBSECTIONVIEWSTYLE"],
    16: ["NURBSURFACE", "SURFACE", "EXTRUDEDSURFACE", "LOFTEDSURFACE", "REVOLVEDSURFACE", "SWEPTSURFACE",
         "PLANESURFACE"],
    17: ["DIMASSOC", "ARC_DIMENSION", "LARGE_RADIAL_DIMENSION"],
    18: ["MAPMETADATAOBJECT", "MAPRESOURCEMANAGEROBJECT", "TCOPTIONALDATA", "TCPOINTENTITY"],
}
STEP_PREFIXES = [
    (13, "BLOCK"),            # the dynamic block names (those listed above for 9.9 are matched first)
    (14, "ACSH_"),
    (15, "ACDBASSOC"),        # the rest of the ACDBASSOC names
    (17, "ACDB_"),            # the annotation context classes (9.9's two ACDB_ names are matched first)
    (18, "DM"),
    (18, "ZWM"),
]


def step_of(name):
    for step, names in STEP_NAMES.items():
        if name in names:
            return step
    for step, prefix in STEP_PREFIXES:
        if name.startswith(prefix):
            return step
    return 0


def typed_names_in_code():
    """The 0-names the reader of this tree has typed data for (the dispatch tables)."""
    names = set()
    with open(READER, encoding="utf-8") as f:
        text = f.read()
    start = text.index("bool DxfReader::parse_single_entity")
    end = text.index("// Unknown entity", start)
    for m in re.finditer(r'\bt == "([^"]+)"', text[start:end]):
        names.add(m.group(1))
    with open(OBJECT_READER, encoding="utf-8") as f:
        text = f.read()
    start = text.index("bool DxfReader::parse_object")
    end = text.index("out.type = ObjectType::Unknown;", start)
    for m in re.finditer(r'\btype == "([^"]+)"', text[start:end]):
        names.add(m.group(1))
    return {n for n in names if n not in ALIASES} | TYPED_BEFORE_THE_CODEC


def parse_plan():
    with open(OLD_PLAN, encoding="utf-8") as f:
        lines = f.read().split("\n")
    idx = {l: i for i, l in enumerate(lines)}

    def find(prefix):
        for i, l in enumerate(lines):
            if l.startswith(prefix):
                return i
        raise SystemExit("not found in the plan: " + prefix)

    a0 = find("## Bilaga A")
    b0 = find("## Bilaga B")
    c0 = find("## Bilaga C")

    # Appendix A
    rows = {}
    for l in lines[a0:b0]:
        m = re.match(r"\| `([^`]+)` \| (.*?) \| (\d+)/(\d+) \| (\d+)/(\d+) \| (\d+)/(\d+) \| (.*) \|$", l)
        if not m:
            continue
        name = m.group(1)
        rows[name] = {
            "status_text": m.group(2),
            "n": int(m.group(3)), "f": int(m.group(4)),
            "a_n": int(m.group(5)), "a_f": int(m.group(6)),
            "all_n": int(m.group(7)), "all_f": int(m.group(8)),
            "versions": m.group(9),
        }

    # Appendix B
    forms = []
    current = None
    planned = {}
    for i in range(b0, c0):
        l = lines[i]
        h = re.match(r"### B\.(\d+) ([A-Z0-9_]+) (?:—|-) (.*)$", l)
        if h:
            current = (int(h.group(1)), h.group(2))
            planned[h.group(2)] = h.group(3)
            continue
        fm = re.match(r"- \*\*F(\d+) — (.*?)\.\*\*$", l)
        if fm and current:
            seq = lines[i + 1].strip()
            sample = lines[i + 2].strip()
            sm = re.match(r"Ordning: `(.*)`\.$", seq)
            pm = re.match(r"Prov: `([^`]+)`, handle `([^`]+)`; SHA256 `([0-9a-f ]{64,70})`\.$", sample)
            if not sm or not pm or len(pm.group(3).replace(" ", "")) != 64:
                raise SystemExit("form not understood: %s F%s" % (current[1], fm.group(1)))
            forms.append({
                "name": current[1], "index": int(fm.group(1)), "versions": fm.group(2),
                "sequence": sm.group(1), "sample": pm.group(1), "handle": pm.group(2),
                # (the old plan has a space inside one SHA256, ACDBASSOCPERSSUBENTMANAGER F12)
                "sha256": pm.group(3).replace(" ", ""),
                "appendix": "B.%d" % current[0],
            })

    # Appendix C
    c1 = []
    c2 = []
    section = None
    for l in lines[c0:]:
        if l.startswith("### C.1"):
            section = "C.1"
        elif l.startswith("### C.2"):
            section = "C.2"
        elif l.startswith("### C.3"):
            section = None
        if section == "C.1" and l.startswith("| ") and not l.startswith("| Namn") and not l.startswith("|---"):
            cells = [c.strip() for c in l.strip("|").split("|")]
            c1.append(cells)
        elif section == "C.2" and l.startswith("| `"):
            m = re.match(r"\| `([^`]+)` \| 0 \| (.*?) \| (.*) \|$", l)
            c2.append((m.group(1), m.group(3)))
    return rows, forms, planned, c1, c2


def c1_names(cells):
    """The 0-names in a row of C.1 (the names, not the concepts)."""
    text = cells[0]
    if text.startswith("Coordination model"):
        return []                       # the concept; its names are COORDINATION_MODEL and NAVISWORKSMODELDEF
    return [n.strip() for n in text.split(",")]


def build():
    rows, forms, planned, c1, c2 = parse_plan()
    typed = typed_names_in_code()
    entries = {}

    def add(name, status, source, planned_type=""):
        kind = "S" if name in STRUCTURAL_NAMES else ("E" if name in ENTITY_NAMES else "O")
        entries[name] = {"name": name, "kind": kind, "status": status, "planned": planned_type,
                         "step": step_of(name), "source": source}

    for name, row in rows.items():
        text = row["status_text"]
        if name in STRUCTURAL_NAMES:
            status = "Structural"
        elif name in typed:
            status = "Typed"
        elif name == "PDFREFERENCE":
            status = "Alias"
        elif text.startswith("Unknown"):
            status = "Pending"
        else:
            status = "Typed"
        planned_type = ""
        m = re.search(r"→ ([A-Za-z0-9]+)", text)
        if m:
            planned_type = m.group(1)
        add(name, status, "A" + ("+" + _b_ref(forms, name) if _b_ref(forms, name) else ""), planned_type)
    for cells in c1:
        for name in c1_names(cells):
            if name not in entries:
                add(name, "Zero", "C.1")
    for name, _ in c2:
        if name not in entries:
            add(name, "Zero", "C.2")
    # What the reader of this tree types that the appendices do not list (the coordination model and the
    # RAPIDRT objects of 1.7.2)
    for name in sorted(typed):
        if name not in entries:
            add(name, "Typed", "code")
    # Typed in the code although the old plan had it untyped, or typed with no sample (SURFACE)
    for name in list(entries):
        if entries[name]["status"] == "Zero" and name in typed:
            entries[name]["status"] = "Typed"
    for name, kind, status, step, _note in CORPUS_EXTRAS:
        if name in entries:
            raise SystemExit("a corpus name is in the old plan's tables already: " + name)
        entries[name] = {"name": name, "kind": kind, "status": status, "planned": "", "step": step, "source": "corpus"}
    return entries, rows, forms


def _b_ref(forms, name):
    for f in forms:
        if f["name"] == name:
            return f["appendix"]
    return ""


STATUS_CPP = {"Typed": "Typed", "Structural": "Structural", "Pending": "Pending", "Zero": "Zero", "Alias": "Alias",
              "External": "External"}


def render_inc(entries):
    out = ["// Generated by tools/gen_type_registry.py — do not edit by hand (see that file for the sources)",
           "// {name, kind, status, planned type, step, source}"]
    for name in sorted(entries):
        e = entries[name]
        out.append('{"%s", %s, %s, "%s", %d, "%s"},' % (name, e["kind"], STATUS_CPP[e["status"]], e["planned"],
                                                         e["step"], e["source"]))
    return "\n".join(out) + "\n"


def render_types(entries, rows):
    head = ["name", "kind", "status", "planned_type", "step", "source", "records", "files", "autocad_records",
            "autocad_files", "all_records", "all_files", "versions"]
    out = ["\t".join(head)]
    for name in sorted(entries):
        e = entries[name]
        r = rows.get(name)
        out.append("\t".join([
            name, {"E": "entity", "O": "object", "S": "structural"}[e["kind"]], e["status"], e["planned"],
            str(e["step"]), e["source"],
            str(r["n"]) if r else "", str(r["f"]) if r else "", str(r["a_n"]) if r else "",
            str(r["a_f"]) if r else "", str(r["all_n"]) if r else "", str(r["all_f"]) if r else "",
            r["versions"] if r else ""]))
    return "\n".join(out) + "\n"


def render_forms(forms):
    head = ["name", "form", "versions", "sequence", "sample", "handle", "sha256", "appendix"]
    out = ["\t".join(head)]
    for f in forms:
        out.append("\t".join([f["name"], "F%d" % f["index"], f["versions"], f["sequence"], f["sample"],
                              f["handle"], f["sha256"], f["appendix"]]))
    return "\n".join(out) + "\n"


def main(argv):
    entries, rows, forms = build()
    texts = {OUT_INC: render_inc(entries), OUT_TYPES: render_types(entries, rows), OUT_FORMS: render_forms(forms)}
    if "--check" in argv:
        bad = False
        for path, text in texts.items():
            have = open(path, encoding="utf-8", newline="").read() if os.path.exists(path) else None
            if have is None or have.replace("\r\n", "\n") != text:
                print("out of date: " + os.path.relpath(path, ROOT))
                bad = True
        return 1 if bad else 0
    for path, text in texts.items():
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.write(text)
    status = {}
    for e in entries.values():
        status[e["status"]] = status.get(e["status"], 0) + 1
    print("%d names: %s; %d forms of %d names" % (len(entries), ", ".join("%s %d" % kv for kv in sorted(status.items())),
                                                  len(forms), len({f["name"] for f in forms})))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
