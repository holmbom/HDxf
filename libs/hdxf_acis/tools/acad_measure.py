"""AutoCAD's core measures the bodies hdxf_acis_export_cases wrote (plan 2026-10-26-acis-bygg.md, the
AutoCAD and MASSPROP part of the test protocol).

  python acad_measure.py <directory with cases.tsv> [--only NAME] [--timeout SECONDS] [--report FILE]

Each drawing has one body. DWG TrueView's accoreconsole opens it (DXFIN) and runs MASSPROP on it and AREA
(object) on it: volume, centroid and surface area of a solid; area, perimeter and centroid of a region. A BODY
is neither (MASSPROP does not cover it): the core must read it as a BODY (LIST). The values are compared with
the analytic ones of the manifest, and the builder's body with AutoCAD's own body of the same shape (the
facit_* files, cut out of doc/facit).

Not AutoLISP: the core has none, so the commands are typed by a script (one body to a drawing is why). The
core has no AUDIT either. The core is never a facit; what it measures is checked against numbers.
"""
import argparse
import csv
import os
import re
import subprocess
import sys
import tempfile

CORE = r"C:\Program Files\Autodesk\DWG TrueView 2027 - English\accoreconsole.exe"
SCRIPT = "_.MASSPROP\r\n_ALL\r\n\r\n_N\r\n_.AREA\r\n_O\r\n_L\r\n_.QUIT\r\n_Y\r\n"
SCRIPT_BODY = "_.LIST\r\n_ALL\r\n\r\n_.QUIT\r\n_Y\r\n"

# Tolerances, absolute + relative, fixed from the precision of what is compared and not changed after the
# results: MASSPROP prints four decimals (a rounding of 5e-5) and the SAT is written with resabs 1e-6.
TOLERANCE = {
    "volume": (6e-4, 2e-6),             # mm3 (a region's area, mm2)
    "area": (6e-4, 2e-6),               # mm2 (a region's perimeter, mm)
    "length": (2e-3, 0.0),              # mm: the centroid
}

NUMBER = r"([-+]?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?)"


def run_core(dxf, timeout, script_text=SCRIPT):
    """The console log of AutoCAD's core opening the DXF and running the script; None on a timeout."""
    script = os.path.join(tempfile.gettempdir(), "hdxf_acis_measure_%d.scr" % os.getpid())
    with open(script, "w", encoding="ascii", newline="") as f:
        f.write(script_text)
    flags = 0x08000000 if os.name == "nt" else 0       # no console window
    try:
        done = subprocess.run([CORE, "/i", os.path.abspath(dxf), "/s", script, "/l", "en-US"], capture_output=True,
                              timeout=timeout, cwd=tempfile.gettempdir(), creationflags=flags)
    except subprocess.TimeoutExpired:
        return None
    return done.stdout.decode("utf-8", "replace").replace("\0", "")


def problems_in(log):
    """What AutoCAD complained of."""
    found = []
    for line in log.splitlines():
        text = line.strip()
        if re.search(r"Invalid|error|Error|discarded|corrupt|ignored|Unknown|cannot|Cannot", text):
            found.append(text)
    return found


def measurements(log, kind):
    """The numbers in the log: volume or area, surface area or perimeter, centroid."""
    out = {"problems": problems_in(log)}
    if kind == "region":
        m = re.search(r"Area:\s+" + NUMBER, log)
        out["volume"] = float(m.group(1)) if m else None
        m = re.search(r"Perimeter:\s+" + NUMBER, log)
        out["area"] = float(m.group(1)) if m else None
        m = re.search(r"Centroid:\s+X:\s+" + NUMBER + r"\s+Y:\s+" + NUMBER, log)
        out["centroid"] = (float(m.group(1)), float(m.group(2)), 0.0) if m else None
    else:
        m = re.search(r"Volume:\s+" + NUMBER, log)
        out["volume"] = float(m.group(1)) if m else None
        m = re.search(r"Area = " + NUMBER + r", Perimeter", log)
        out["area"] = float(m.group(1)) if m else None
        m = re.search(r"Centroid:\s+X:\s+" + NUMBER + r"\s+Y:\s+" + NUMBER + r"\s+Z:\s+" + NUMBER, log)
        out["centroid"] = (float(m.group(1)), float(m.group(2)), float(m.group(3))) if m else None
    return out


def close(measured, expected, quantity):
    """Whether a measured value is the expected one within the tolerance of its quantity."""
    absolute, relative = TOLERANCE[quantity]
    return measured is not None and abs(measured - expected) <= absolute + relative * abs(expected)


def same_values(a, b):
    """Two measurements of the same shape."""
    return (close(a["volume"], b["volume"], "volume") and close(a["area"], b["area"], "area") and
            a["centroid"] is not None and b["centroid"] is not None and
            all(close(x, y, "length") for x, y in zip(a["centroid"], b["centroid"])))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory")
    parser.add_argument("--only")
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--report")
    args = parser.parse_args()

    with open(os.path.join(args.directory, "cases.tsv"), newline="") as f:
        rows = list(csv.DictReader(f, delimiter="\t"))
    results = []
    bad = 0
    lines = []
    for row in rows:
        if args.only and args.only not in row["name"]:
            continue
        expected = {"volume": float(row["volume"]), "area": float(row["area"]),
                    "centroid": (float(row["cx"]), float(row["cy"]), float(row["cz"]))}
        for version in ("2010", "2018"):
            path = os.path.join(args.directory, row["file_" + version])
            if not os.path.exists(path):
                continue
            is_body = row["kind"] == "body"
            log = run_core(path, args.timeout, SCRIPT_BODY if is_body else SCRIPT)
            label = "%-22s %-6s %s" % (row["name"], row["source"], version)
            if log is None:
                lines.append("%s TIMEOUT" % label)
                bad += 1
                continue
            if is_body:
                problems = problems_in(log)
                listed = re.search(r"^\s*BODY\s+Layer", log, re.M) is not None
                ok = listed and not problems
                bad += 0 if ok else 1
                lines.append("%s %s read as a BODY: %s%s" % (label, "OK " if ok else "BAD", listed,
                                                            (" problems: " + " | ".join(problems[:3])) if problems else ""))
                continue
            m = measurements(log, row["kind"])
            ok = (close(m["volume"], expected["volume"], "volume") and close(m["area"], expected["area"], "area") and
                  m["centroid"] is not None and
                  all(close(a, b, "length") for a, b in zip(m["centroid"], expected["centroid"])))
            ok = ok and not m["problems"]
            bad += 0 if ok else 1
            results.append((row["name"], row["source"], version, m))
            lines.append("%s %s volume %s (%.4f) area %s (%.4f) centroid %s (%s)%s" % (
                label, "OK " if ok else "BAD", m["volume"], expected["volume"], m["area"], expected["area"],
                m["centroid"], expected["centroid"],
                (" problems: " + " | ".join(m["problems"][:3])) if m["problems"] else ""))

    # The builder's body against AutoCAD's own
    by_key = {(n, s, v): m for n, s, v, m in results}
    for name, source, version, m in results:
        if source != "built" or (name, "facit", version) not in by_key:
            continue
        same = same_values(m, by_key[(name, "facit", version)])
        bad += 0 if same else 1
        lines.append("%-22s built against AutoCAD's own %s: %s" % (name, version, "equal" if same else "DIFFERENT"))

    text = "\n".join(lines) + "\n%d of the checks failed\n" % bad
    print(text)
    if args.report:
        with open(args.report, "w", encoding="utf-8") as f:
            f.write(text)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
