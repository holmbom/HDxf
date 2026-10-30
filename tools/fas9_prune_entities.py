#!/usr/bin/env python3
"""Prepares the input drawing of a facit run (step 9.6, plan 2026-10-27-fas-9-typer.md): the ENTITIES section of an ASCII DXF
that AutoCAD saved, cut to the named entity types (or, with a first name starting with "!", without them). The file stays
AutoCAD's own for everything else; the facit is what AutoCAD's core then saves from it (tools/fas9_facit_run.py --input).

    python -I tools/fas9_prune_entities.py in.dxf out.dxf TYPE [TYPE ...]      keep only these entity types
    python -I tools/fas9_prune_entities.py in.dxf out.dxf '!TYPE' [TYPE ...]   drop these entity types

Owned records (ATTRIB, VERTEX, SEQEND) follow the entity before them.
"""
import sys

src, dst = sys.argv[1], sys.argv[2]
args = sys.argv[3:]
drop_mode = bool(args) and args[0].startswith("!")
keep = set(a.lstrip("!") for a in args)
raw = open(src, "rb").read().decode("latin-1")
nl = "\r\n" if "\r\n" in raw else "\n"
lines = raw.split(nl)
if lines and lines[-1] == "":
    lines.pop()
pairs = [(lines[i], lines[i + 1]) for i in range(0, len(lines) - 1, 2)]
out = []
i = 0
in_entities = False
removed = 0
while i < len(pairs):
    code, val = pairs[i]
    if code.strip() == "0" and val.strip() == "SECTION" and pairs[i + 1][1].strip() == "ENTITIES":
        in_entities = True
        out += [pairs[i], pairs[i + 1]]
        i += 2
        continue
    if in_entities and code.strip() == "0" and val.strip() == "ENDSEC":
        in_entities = False
    if in_entities and code.strip() == "0":
        j = i + 1
        while j < len(pairs) and pairs[j][0].strip() != "0":
            j += 1
        # owned records (ATTRIB, VERTEX, SEQEND) belong to the entity before them
        while j < len(pairs) and pairs[j][0].strip() == "0" and pairs[j][1].strip() in ("ATTRIB", "VERTEX", "SEQEND"):
            j += 1
            while j < len(pairs) and pairs[j][0].strip() != "0":
                j += 1
        if (val.strip() not in keep) if drop_mode else (val.strip() in keep):
            out += pairs[i:j]
        else:
            removed += 1
        i = j
        continue
    out.append(pairs[i])
    i += 1
with open(dst, "wb") as f:
    f.write((nl.join(a + nl + b for a, b in out) + nl).encode("latin-1"))
print("removed", removed, "entities")
