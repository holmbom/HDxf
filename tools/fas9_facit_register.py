#!/usr/bin/env python3
"""Writes doc/facit/fas9_register.md: the facit register of phase 9 (step 9.0).

    python -I tools/fas9_facit_register.py            # writes doc/facit/fas9_register.md
    python -I tools/fas9_facit_register.py --check    # fails if the file is not what the corpus gives

For each of the plan's 27 facit items (F9.1 ... F9.27, plan 2026-10-27-fas-9-typer.md) it lists the record types the
item asks for and what the corpus has of them: records in files an AutoCAD of known provenance saved (and in
which versions), the first such sample with its handle and SHA-256, and what only other producers' files or
no file has. A type with an AutoCAD sample is not a finished facit: the item's branches (the plan's text for the
item) are what a family step reads out of the file; a type with none is "ej testat" until the family's
script (doc/facit/fas9_<family>.scr, run by tools/fas9_facit_run.py) has made one, or stays so in its outcome.
A step's own text for an item goes in doc/facit/fas9_notes/F9.<n>.md (kept under the item when this is run again).

The corpus is the inventory of tools/inventory_types.py: build/test-data, doc/facit (with doc/facit/web), doc/acad_*.
"""

import fnmatch
import hashlib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import inventory_types as inv  # noqa: E402

OUT = os.path.join(inv.ROOT, "doc", "facit", "fas9_register.md")
# A family step's own text for an item (what its facit showed, its "ej testat"): doc/facit/fas9_notes/F9.<n>.md, put under
# the item's verdict as it is; one file per item, so steps of one wave do not touch the same file
NOTES = os.path.join(inv.ROOT, "doc", "facit", "fas9_notes")

# F9 id, short title, the step(s) that own it, record names (fnmatch patterns over the type register)
ITEMS = [
    (1, "OLEFRAME och OLE2FRAME", "9.3", ["OLEFRAME", "OLE2FRAME"]),
    (2, "ACAD_PROXY_ENTITY och ACAD_PROXY_OBJECT", "9.3", ["ACAD_PROXY_ENTITY", "ACAD_PROXY_OBJECT"]),
    (3, "Underlägg", "9.6", ["PDFUNDERLAY", "DWFUNDERLAY", "DGNUNDERLAY", "PDFDEFINITION", "DWFDEFINITION", "DGNDEFINITION"]),
    (4, "Index och filter", "9.4", ["LAYER_INDEX", "SPATIAL_INDEX", "LAYER_FILTER", "IDBUFFER", "SPATIAL_FILTER", "SORTENTSTABLE"]),
    (5, "Tabellernas otäckta grenar", "9.1, 9.5", ["ACAD_TABLE", "TABLESTYLE", "CELLSTYLEMAP", "TABLECONTENT", "TABLEGEOMETRY"]),
    (6, "MATERIAL", "9.7", ["MATERIAL"]),
    (7, "VISUALSTYLE och entitetsreferenser", "9.7", ["VISUALSTYLE"]),
    (8, "FIELD och DIMASSOC", "9.8, 9.17", ["FIELD", "FIELDLIST", "DIMASSOC"]),
    (9, "Dynamiska block", "9.9, 9.13", ["BLOCK[A-Z]*", "ACAD_EVALUATION_GRAPH", "ACDB_BLOCKREPRESENTATION_DATA",
                                         "ACDB_DYNAMICBLOCKPURGEPREVENTER_VERSION"]),
    (10, "Solidernas historik", "9.14", ["ACSH_*"]),
    (11, "Persistenta subentiteter", "9.10", ["ACDBASSOCPERSSUBENTMANAGER", "ACDBPERSSUBENTMANAGER"]),
    (12, "Återstående ACDBASSOC", "9.10, 9.15", ["ACDBASSOC[A-OQ-Z0-9]*", "ACDBASSOCP[A-DF-Z]*", "ASSOCDIMDEPENDENCYBODY"]),
    (13, "Render", "9.11", ["RENDERSETTINGS", "MENTALRAYRENDERSETTINGS", "RAPIDRTRENDERSETTINGS", "RENDERENVIRONMENT",
                            "RENDERGLOBAL", "RAPIDRTRENDERENVIRONMENT", "RENDERENTRY"]),
    (14, "LIGHTLIST", "9.11", ["LIGHTLIST", "LIGHT", "SUN"]),
    (15, "SUNSTUDY", "9.11", ["SUNSTUDY"]),
    (16, "Sektioner", "9.12", ["SECTIONOBJECT", "SECTION_MANAGER", "SECTION_SETTINGS"]),
    (17, "NURBSURFACE", "9.16", ["NURBSURFACE"]),
    (18, "2004-ytornas isolinjer", "9.16", ["SURFACE", "EXTRUDEDSURFACE", "LOFTEDSURFACE", "REVOLVEDSURFACE", "SWEPTSURFACE",
                                           "PLANESURFACE"]),
    (19, "Annotationskontexter", "9.17", ["ACDB_*CONTEXTDATA_CLASS", "SCALE"]),
    (20, "Bågmått och stora radiemått", "9.17", ["ARC_DIMENSION", "LARGE_RADIAL_DIMENSION"]),
    (21, "Hörn-id", "9.2", ["LWPOLYLINE", "POLYLINE", "VERTEX"]),
    (22, "OBJECT_PTR och VBA_PROJECT", "9.8", ["OBJECT_PTR", "VBA_PROJECT"]),
    (23, "Coordination model och xref-paneler", "9.6", ["COORDINATION_MODEL", "NAVISWORKSMODELDEF", "XREFPANELOBJECT",
                                                         "EXACXREFPANELOBJECT"]),
    (24, "AutoCAD Map", "9.18", ["DM[A-Z]*", "MAPMETADATAOBJECT", "MAPRESOURCEMANAGEROBJECT"]),
    (25, "TC och ZWM", "9.18", ["TCOPTIONALDATA", "TCPOINTENTITY", "ZWM*"]),
    (26, "ModelDoc-stilar", "9.12", ["ACDBDETAILVIEWSTYLE", "ACDBSECTIONVIEWSTYLE"]),
    (27, "DATATABLE och kompletterande kodformer", "9.8", ["DATATABLE", "ACDBDATATABLE"]),
]

VERSION_ORDER = ["AC1009", "AC1012", "AC1014", "AC1015", "AC1018", "AC1021", "AC1024", "AC1027", "AC1032"]
VERSION_NAME = {"AC1009": "R12", "AC1012": "R13", "AC1014": "R14", "AC1015": "2000", "AC1018": "2004", "AC1021": "2007",
                "AC1024": "2010", "AC1027": "2013", "AC1032": "2018"}


def registry_rows():
    rows = {}
    path = os.path.join(inv.ROOT, "src", "typed_registry_data.inc")
    with open(path, encoding="utf-8") as f:
        for line in f:
            import re
            m = re.match(r'\{"([^"]+)", ([EOS]), (\w+), "([^"]*)", (\d+), "([^"]*)"\},', line)
            if m:
                rows[m.group(1)] = {"kind": m.group(2), "status": m.group(3), "step": int(m.group(5))}
    return rows


def versions_text(counts):
    parts = []
    for v in VERSION_ORDER:
        if v in counts:
            parts.append("%s:%d" % (VERSION_NAME[v], counts[v]))
    return ", ".join(parts) if parts else "-"


def render():
    result = inv.inventory(inv.default_roots())
    registry = registry_rows()
    fingerprint = hashlib.sha256("\n".join(sorted(u["sha256"] for u in result["provenance"])).encode()).hexdigest()
    types = result["types"]
    out = []
    w = out.append
    w("# Fas 9 — facitregister F9.1–F9.27")
    w("")
    w("Skapad av `python -I tools/fas9_facit_register.py` (steg 9.0) ur inventeringen `tools/inventory_types.py`; "
      "kör om verktyget efter nya original, och `--check` jämför. Inget här är ett facit i sig: ett facit är en fil "
      "AutoCAD själv har sparat (B100). Registret visar vad korpusen har, så att varje familjesteg ser vad som finns "
      "att läsa och vad som är **ej testat** tills ett original har framställts.")
    w("")
    w("## Korpus")
    w("")
    w("- %d filer funna (build/test-data, doc/facit med doc/facit/web, doc/acad_*_dxf.dxf), %d med samma innehåll som en "
      "annan (SHA-256: räknas en gång), %d olika innehåll lästa, %d av dem binära tvillingar av en ASCII-fil som är "
      "räknad, %d räknade, %d ej läsbara."
      % (result["files"], len(result["identical_content"]), result["unique"], len(result["twins"]), result["counted"],
         len(result["errors"])))
    w("- Producent (räknade filer): " + ", ".join("%s %d" % kv for kv in sorted(result["producers"].items())) +
      ". `autocad-facit` = sparade av AutoCAD 2025:s kärna ur doc/facit/*.scr; `autocad` = filer med känd AutoCAD-proveniens "
      "(test/data/README.md); `web-unproven` = doc/facit/web, AutoCAD-ursprung sannolikt men inte bevisat "
      "(doc/facit/web/SOURCES.md); `other` = andra program (ezdxf, tredjeparts R13/R14) — observationsunderlag, inte facit.")
    w("- Fingeravtryck (SHA-256 över de olika innehållens SHA-256, sorterade): `%s`" % fingerprint)
    for e in result["errors"]:
        w("- Ej läsbar: `%s`: %s" % (e["path"], e["error"]))
    w("")
    w("**Sökutfall.** Den gamla planens inventering använde en annan, större korpus (scratch-kataloger som inte längre "
      "finns); dess siffror står i `test/data/typed_types.tsv` och `typed_forms.tsv` som observationsunderlag. Här "
      "räknas bara det som finns nu. Sedan den inventeringen har AutoCAD 2025:s kärna sparat doc/facit/16_dynamic_blocks, "
      "17_associative och flera till: många typer som den gamla planen hade som \"noll poster\" har nu AutoCAD-poster "
      "(se kolumnen). Det som saknas efter denna sökning är **ej testat** per typ, gren, version och format tills "
      "familjens `fas9_*.scr` har framställt ett original eller en webbsökning (fast URL + SHA-256 i "
      "test/data/manifest.cmake) har funnit ett.")
    w("")
    w("R13/R14: AutoCAD 2025 kan inte spara dem. Filfacit för R13/R14 är historiska AutoCAD-original (de pinnade i "
      "manifestet, `build/test-data/local-r13r14/PROVENANCE.txt`) och profilen `doc/reference/hdxf_r13_r14_format.md`; "
      "kolumnen visar dem när de finns.")
    w("")

    full = partial = missing = 0
    for fid, title, steps, patterns in ITEMS:
        names = []
        for p in patterns:
            matched = [n for n in sorted(registry) if fnmatch.fnmatchcase(n, p) and registry[n]["kind"] != "S"]
            for n in matched:
                if n not in names:
                    names.append(n)
            if not matched and "*" not in p and "[" not in p:
                names.append(p)
        w("## F9.%d — %s (steg %s)" % (fid, title, steps))
        w("")
        w("| Typ | Register | AutoCAD-poster (filer) | AutoCAD-versioner | Första AutoCAD-prov | Övriga filer |")
        w("|---|---|---:|---|---|---|")
        have = 0
        for n in names:
            t = types.get(n)
            reg = registry.get(n)
            status = {"Typed": "typad", "Pending": "ej typad", "Zero": "noll poster (gamla planen)", "Alias": "läsalias",
                      "External": "annan produkts klass", "Structural": "struktur"}.get(reg["status"], "?") if reg else "ej i registret"
            if t and t["autocad_records"] > 0:
                have += 1
                sample = t["autocad_sample"]
                sample_text = "`%s` handle `%s` (%s) `%s`" % (sample["path"], sample["handle"] or "-",
                                                             VERSION_NAME.get(sample["acadver"], "?"), sample["sha256"][:12])
                w("| `%s` | %s | %d (%d) | %s | %s | %d |" % (n, status, t["autocad_records"], len(t["autocad_files"]),
                                                              versions_text(t["autocad_versions"]), sample_text,
                                                              t["records"] - t["autocad_records"]))
            elif t:
                w("| `%s` | %s | 0 | - | ej testat (AutoCAD-original saknas) | %d |" % (n, status, t["records"]))
            else:
                w("| `%s` | %s | 0 | - | ej testat (ingen post i korpusen) | 0 |" % (n, status))
        w("")
        if have == len(names):
            verdict = "AutoCAD-poster finns för alla typer i posten. Posternas grenar (listor, valfria fält, relationer, äldre former) läses ur filerna av familjesteget; det som inte syns i dem är ej testat."
            full += 1
        elif have:
            verdict = "AutoCAD-poster finns för %d av %d typer; resten är **ej testat** tills ett original framställs." % (have, len(names))
            partial += 1
        else:
            verdict = "Ingen AutoCAD-post finns i korpusen: **ej testat** för posten tills familjens skript eller en webbsökning har framställt ett original."
            missing += 1
        w(verdict)
        w("")
        note = os.path.join(NOTES, "F9.%d.md" % fid)
        if os.path.exists(note):
            with open(note, encoding="utf-8") as f:
                text = f.read().replace("\r\n", "\n").strip("\n")
            if text:
                w(text)
                w("")
    w("## Sammanfattning")
    w("")
    w("F9-poster med AutoCAD-poster för alla sina typer: %d; för några: %d; för inga: %d (av %d)." % (full, partial, missing, len(ITEMS)))
    w("")
    w("## Gemensam framställning")
    w("")
    w("`tools/fas9_facit_run.py` är den sekvens familjeskripten delar: ett familjeskript `doc/facit/fas9_<familj>.scr` är "
      "vanliga AutoCAD-kommandon (kärnan har inget LISP) med två kommentarsmarkörer, `;SAVE <tema> [<ändring>]` som "
      "sparar det öppna tillståndet för R12, 2000, 2004, 2007, 2010, 2013 och 2018 i ASCII och binärt "
      "(`_.SAVEAS` / `DXF` / `_V` / `<version>` / `16` eller `_B` / `<sökväg>`) och `;EXPECT <NAMN>` som kräver posten i "
      "2018-filen. Körs med `accoreconsole.exe /i <ritning|acad.dwt> /s <skript> /l en-US` (kärnan öppnar acad.dwt som en "
      "ny tom ritning); formen visas av `doc/facit/fas9_example.scr`. Varje fil loggas i `fas9_facit_log.csv` med faktisk "
      "ACADVER/ACADMAINTVER, SHA-256 och antal poster; posthandle och gren läses ur filerna med "
      "`tools/inventory_types.py --json --forms`. R13 och R14 kan AutoCAD 2025 inte spara.")
    w("")
    return "\n".join(out) + "\n"


def main(argv):
    text = render()
    if "--check" in argv:
        have = open(OUT, encoding="utf-8", newline="").read().replace("\r\n", "\n") if os.path.exists(OUT) else None
        if have != text:
            print("out of date: " + os.path.relpath(OUT, inv.ROOT))
            return 1
        return 0
    with open(OUT, "w", encoding="utf-8", newline="") as f:
        f.write(text)
    print("wrote " + os.path.relpath(OUT, inv.ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
